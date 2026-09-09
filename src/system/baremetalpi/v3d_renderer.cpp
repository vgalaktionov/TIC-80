// TIC-80 Raspberry Pi 4 V3D CRT renderer
// Copyright (C) 2026 TIC-80 contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "video.h"

#ifndef TIC80_V3D_RENDERER_TEST
#include <circle/synchronize.h>
#include <circle/timer.h>
#endif
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <tic80.h>

#define V3D_ARCH_AARCH64 0
#define V3D_IMPLEMENTATION
#include "v3d/v3d.h"
#define V3D_TEXTURE_IMPLEMENTATION
#include "v3d/v3dTexture.h"

void tic80SerialDebug(const char* message);

namespace
{
static const unsigned OutputWidth = TIC80_WIDTH * TIC80_BAREMETAL_SCREEN_SCALE;
static const unsigned OutputHeight = TIC80_HEIGHT * TIC80_BAREMETAL_SCREEN_SCALE;
static const unsigned TileWidth = 64;
static const unsigned TileHeight = 64;
static const unsigned TilesX = (OutputWidth + TileWidth - 1) / TileWidth;
static const unsigned TilesY = (OutputHeight + TileHeight - 1) / TileHeight;
static const unsigned SourceUifWidth = (TIC80_WIDTH + 31) & ~31u;
static const unsigned SourceUifPaddedHeight = (TIC80_HEIGHT + 7) & ~7u;
static const unsigned MaskUifUbPad = 2;
static const unsigned MaskUifPaddedHeight = ((OutputHeight + 7) & ~7u) + MaskUifUbPad * 8;
static const unsigned SourceUifSize = SourceUifWidth * SourceUifPaddedHeight * sizeof(uint32_t);
static const unsigned MaskUifSize = OutputWidth * MaskUifPaddedHeight * sizeof(uint32_t);
static const unsigned TileAllocationSize = 1024 * 1024;
static const unsigned TileStateSize = TilesX * TilesY * 256;
static const unsigned BlurredSize = TIC80_WIDTH * TIC80_HEIGHT * sizeof(uint32_t);
static const unsigned OutputSize = OutputWidth * OutputHeight * sizeof(uint32_t);
static const unsigned ArenaSize = BlurredSize + SourceUifSize + MaskUifSize + TileAllocationSize
                                  + TileStateSize + OutputSize + 64 + 64 * 1024;
static const unsigned CommandTimeoutUs = 250000;
static const unsigned PowerTimeoutUs = 100000;
static const unsigned V3dCleCt0Ea = V3D_CORE0 + 0x108;
static const unsigned V3dCleCt1Ea = V3D_CORE0 + 0x10c;
static const unsigned V3dCleCt0Ca = V3D_CORE0 + 0x110;
static const unsigned V3dCleCt0Lc = V3D_CORE0 + 0x120;
static const unsigned V3dClePcs = V3D_CORE0 + 0x130;
static const unsigned V3dErrorStatus = V3D_CORE0 + 0xf20;
static const unsigned V3dHubInterruptStatus = V3D_HUB_BASE + 0x50;
static const unsigned V3dHubInterruptClear = V3D_HUB_BASE + 0x58;
static const unsigned V3dMmucControl = V3D_HUB_BASE + 0x1000;
static const uint32_t V3dMmucFlushing = 1u << 2;
static const unsigned V3dMmuControl = V3D_HUB_BASE + 0x1200;
static const unsigned V3dMmuAddressCap = V3D_HUB_BASE + 0x1214;
static const unsigned V3dMmuViolationId = V3D_HUB_BASE + 0x122c;
static const unsigned V3dMmuIllegalAddress = V3D_HUB_BASE + 0x1230;
static const unsigned V3dMmuViolationAddress = V3D_HUB_BASE + 0x1234;
static const unsigned V3dCoreInterruptStatus = V3D_CORE0 + 0x50;
static const unsigned V3dCoreInterruptClear = V3D_CORE0 + 0x58;
static const unsigned V3dGmpStatus = V3D_CORE0 + 0x800;
static const unsigned V3dGmpConfig = V3D_CORE0 + 0x804;
static const uint32_t V3dGmpReadCountMask = 0x7fu << 16;
static const uint32_t V3dGmpWriteCountMask = 0x7fu << 24;
static const uint32_t V3dGmpConfigBusy = 1u << 3;
static const uint32_t V3dGmpStopRequest = 1u << 1;

static_assert(OutputWidth == 960 && OutputHeight == 544, "V3D layout assumes a 4x TIC-80 output");
static_assert(SourceUifPaddedHeight == 136, "unexpected source UIF padding");
static_assert(MaskUifPaddedHeight == 560, "unexpected mask UIF padding");
static_assert(sizeof(v3d_tile_binning_mode_cfg) == 9, "AArch32 changed V3D packet layout");
static_assert(sizeof(v3d_gl_shader_state_record) == 36, "AArch32 changed shader record layout");
static_assert(sizeof(v3d_gl_shader_state_attribute_record) == 16,
              "AArch32 changed attribute record layout");
static_assert(sizeof(v3d_texture_shader_state) == 24, "AArch32 changed texture state layout");
static_assert(sizeof(v3d_sampler_state) == 24, "AArch32 changed sampler state layout");

struct AtlasInstance
{
    float atlasX;
    float atlasY;
    float atlasWidth;
    float atlasHeight;
    float renderX;
    float renderY;
    float renderWidth;
    float renderHeight;
    float colorR;
    float colorG;
    float colorB;
    float colorA;
};

struct Renderer
{
    bool ready;
    bool outputValidated;
    uint32_t* scanout;
    unsigned scanoutPitch;
    uint32_t* framebuffer;
    unsigned framebufferPitch;
    uint8_t* arenaAllocation;
    v3d_static_buffer arena;
    uint32_t* blurred;
    float* vertices;
    AtlasInstance* instance;
    uint8_t* sourceUif;
    uint8_t* maskUif;
    uint8_t* tileAllocation;
    uint8_t* tileState;
    v3d_static_buffer binning;
    v3d_static_buffer rendering;
    v3d_static_buffer indirect;
    v3d_static_buffer state;
};

static Renderer Gpu = {};

enum WaitResult
{
    WaitComplete,
    WaitControllerError,
    WaitTimeout,
};

static const v3d_u64 FragmentShader[] = {
    0x3e903186bb800000ULL, 0x5690b046bbc00000ULL, 0x540020c305ca9000ULL,
    0x3c0021840582b000ULL, 0x3c003886bbf80100ULL, 0x3c003186bb800000ULL,
    0x3c003846bbf800c0ULL, 0x3c913186bb800000ULL, 0x3c903186bb800000ULL,
    0x65e011c6bbf00028ULL, 0x75e01206bbf00028ULL, 0x65e01246bbe00028ULL,
    0x75e01286bbe00028ULL, 0x3e403186bb800000ULL, 0x3e403186bb800000ULL,
    0x3c203886bbf80100ULL, 0x3c203186bb800000ULL, 0x3c003846bbf800c0ULL,
    0x3c913186bb800000ULL, 0x649011c6bbd001c0ULL, 0x74001206bbd00200ULL,
    0x64001246bbc00240ULL, 0x74001286bbc00280ULL, 0x3c2031873583e1c8ULL,
    0x3c0031873583e24aULL, 0x3c003186bb800000ULL,
};

static const v3d_u64 VertexShader[] = {
    0x3de02183bc807000ULL, 0x3de02184bc807001ULL, 0x3de02193bc807002ULL,
    0x3de02185bc807003ULL, 0x3de02186bc807004ULL, 0x3de02187bc807005ULL,
    0x3de02188bc807006ULL, 0x3de02189bc807007ULL, 0x3de0218abc807008ULL,
    0x3de0218bbc807009ULL, 0x3de0218cbc80700aULL, 0x3de0218dbc80700bULL,
    0x3de0218ebc80700cULL, 0x3de0218fbc80700dULL, 0x3de02190bc80700eULL,
    0x55847006bbf800cbULL, 0x3c40318105830240ULL, 0x54003006bba40000ULL,
    0x3c003181f6800000ULL, 0x3c003180f5839000ULL, 0x3de02180f8807000ULL,
    0x54403006bbf8010cULL, 0x3c00318105830280ULL, 0x5584b006bba40000ULL,
    0x3c003181f6800000ULL, 0x3c003180f5839000ULL, 0x3de02180f8807001ULL,
    0x54403006bbf804d2ULL, 0x3c00318105828000ULL, 0x3de02180f880f002ULL,
    0x3de02180f8837443ULL, 0x54003006bbf800c7ULL, 0x3c00318105830140ULL,
    0x3de02180f880f004ULL, 0x54003006bbf80108ULL, 0x3c00318105830180ULL,
    0x3de02180f880f005ULL, 0x3de02180f8837346ULL, 0x3de02180f8837387ULL,
    0x3de02180f88373c8ULL, 0x3de02180f8837409ULL, 0x3c003186bb816000ULL,
    0x3c203186bb800000ULL, 0x3c003186bb800000ULL, 0x3c003186bb800000ULL,
};

static const v3d_u64 CoordinateShader[] = {
    0x3de02183bc807000ULL, 0x3de02184bc807001ULL, 0x3de0218bbc807002ULL,
    0x3de02185bc807003ULL, 0x3de02186bc807004ULL, 0x3de02187bc807005ULL,
    0x3de02188bc807006ULL, 0x55827006bbf800c7ULL, 0x3c40318105830140ULL,
    0x3de02180f880f000ULL, 0x54003006bba40000ULL, 0x3c003181f6800000ULL,
    0x3c003180f5839000ULL, 0x3de02180f8807004ULL, 0x54403006bbf80108ULL,
    0x3c00318105830180ULL, 0x3de02180f880f001ULL, 0x54003006bba40000ULL,
    0x3c003181f6800000ULL, 0x3c003180f5839000ULL, 0x3de02180f8807005ULL,
    0x3de02180f88372c2ULL, 0x3de02180f8837243ULL, 0x3c003186bb816000ULL,
    0x3c203186bb800000ULL, 0x3c003186bb800000ULL, 0x3c003186bb800000ULL,
};

static const float Vertices[] = {
    0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  1.0f, 1.0f, 0.0f,
};

static AtlasInstance Instance = {
    0.0f, 0.0f, 1.0f, 1.0f,
    -1.0f, 1.0f, 2.0f, -2.0f,
    1.0f, 1.0f, 1.0f, 1.0f,
};

static void* allocate(v3d_static_buffer* buffer, unsigned size, unsigned alignment)
{
    return v3d_buffer_allocate(buffer, size, alignment);
}

static bool buffersValid()
{
    return Gpu.framebuffer && Gpu.blurred && Gpu.vertices && Gpu.instance && Gpu.sourceUif
           && Gpu.maskUif && Gpu.tileAllocation
           && Gpu.tileState && Gpu.binning.start && Gpu.rendering.start
           && Gpu.indirect.start && Gpu.state.start;
}

static v3d_texture_shader_state* makeTexture(v3d_static_buffer* state, uint8_t* pixels,
                                             unsigned width, unsigned height, unsigned ubPad,
                                             bool bgra)
{
    v3d_texture_shader_state* texture =
        static_cast<v3d_texture_shader_state*>(allocate(state, sizeof(*texture), 16));
    if (!texture) return 0;
    memset(texture, 0, sizeof(*texture));
    texture->texture_base_pointer_rshift_6 = V3D_ARM_TO_BUS_ADDR(pixels) >> 6;
    texture->image_width = width;
    texture->image_height = height;
    texture->image_depth = 1;
    texture->texture_type = V3D_TEXTURE_DATA_FORMAT_RGBA8;
    texture->swizzle_r = bgra ? v3d_SWIZZLE_BLUE : v3d_SWIZZLE_RED;
    texture->swizzle_g = v3d_SWIZZLE_GREEN;
    texture->swizzle_b = bgra ? v3d_SWIZZLE_RED : v3d_SWIZZLE_BLUE;
    texture->swizzle_a = v3d_SWIZZLE_ALPHA;
    texture->extended = true;
    texture->level_0_ubpad = ubPad;
    texture->level_0_xor_enable = false;
    texture->level_0_is_strictly_uif = true;
    return texture;
}

static v3d_sampler_state* makeSampler(v3d_static_buffer* state)
{
    v3d_sampler_state* sampler =
        static_cast<v3d_sampler_state*>(allocate(state, sizeof(*sampler), 8));
    if (!sampler) return 0;
    memset(sampler, 0, sizeof(*sampler));
    sampler->mag_filter_nearest = true;
    sampler->min_filter_nearest = true;
    sampler->depth_compare_function = V3D_COMPARE_FUNC_NEVER;
    sampler->wrap_s = V3D_WRAP_MODE_CLAMP;
    sampler->wrap_t = V3D_WRAP_MODE_CLAMP;
    sampler->wrap_r = V3D_WRAP_MODE_CLAMP;
    return sampler;
}

static bool writeTextureUniforms(v3d_static_buffer* state, v3d_texture_shader_state* texture,
                                 v3d_sampler_state* sampler)
{
    v3d_tmu_config_parameter_0* p0 =
        static_cast<v3d_tmu_config_parameter_0*>(allocate(state, sizeof(*p0), 4));
    v3d_tmu_config_parameter_1* p1 =
        static_cast<v3d_tmu_config_parameter_1*>(allocate(state, sizeof(*p1), 4));
    if (!p0 || !p1) return false;
    memset(p0, 0, sizeof(*p0));
    memset(p1, 0, sizeof(*p1));
    p0->return_words_of_texture_data = 3;
    p0->texture_state_address_rshift_4 = V3D_ARM_TO_BUS_ADDR(texture) >> 4;
    p1->sampler_state_address_rshift_3 = V3D_ARM_TO_BUS_ADDR(sampler) >> 3;
    return true;
}

static v3d_gl_shader_state_record* prepareShaderState(unsigned* attributeCount)
{
    v3d_texture_shader_state* source = makeTexture(&Gpu.state, Gpu.sourceUif,
                                                   TIC80_WIDTH, TIC80_HEIGHT, 0, true);
    v3d_sampler_state* sourceSampler = makeSampler(&Gpu.state);
    v3d_texture_shader_state* mask = makeTexture(&Gpu.state, Gpu.maskUif,
                                                 OutputWidth, OutputHeight, MaskUifUbPad, false);
    v3d_sampler_state* maskSampler = makeSampler(&Gpu.state);
    if (!source || !sourceSampler || !mask || !maskSampler) return 0;

    uint8_t* fragmentUniforms = V3D_BUFFER_WRITE_HEAD(Gpu.state);
    if (!writeTextureUniforms(&Gpu.state, source, sourceSampler)
        || !writeTextureUniforms(&Gpu.state, mask, maskSampler)) return 0;

    uint8_t* vertexUniforms = V3D_BUFFER_WRITE_HEAD(Gpu.state);
    const float vertexValues[] = {
        1.0f, OutputWidth * 128.0f, OutputHeight * -128.0f, 0.5f, 0.5f,
    };
    v3d_buffer_write(&Gpu.state, const_cast<float*>(vertexValues), sizeof(vertexValues));

    uint8_t* coordinateUniforms = V3D_BUFFER_WRITE_HEAD(Gpu.state);
    const float coordinateValues[] = {
        1.0f, OutputWidth * 128.0f, OutputHeight * -128.0f,
    };
    v3d_buffer_write(&Gpu.state, const_cast<float*>(coordinateValues), sizeof(coordinateValues));

    uint8_t* defaults = V3D_BUFFER_WRITE_HEAD(Gpu.state);
    const float defaultValues[] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (unsigned i = 0; i < 16; ++i)
        v3d_buffer_write(&Gpu.state, const_cast<float*>(defaultValues), sizeof(defaultValues));

    v3d_buffer_align(&Gpu.state, 8);
    uint8_t* fragmentAddress = V3D_BUFFER_WRITE_HEAD(Gpu.state);
    v3d_buffer_write(&Gpu.state, const_cast<v3d_u64*>(FragmentShader), sizeof(FragmentShader));
    v3d_buffer_align(&Gpu.state, 8);
    uint8_t* vertexAddress = V3D_BUFFER_WRITE_HEAD(Gpu.state);
    v3d_buffer_write(&Gpu.state, const_cast<v3d_u64*>(VertexShader), sizeof(VertexShader));
    v3d_buffer_align(&Gpu.state, 8);
    uint8_t* coordinateAddress = V3D_BUFFER_WRITE_HEAD(Gpu.state);
    v3d_buffer_write(&Gpu.state, const_cast<v3d_u64*>(CoordinateShader), sizeof(CoordinateShader));

    v3d_gl_shader_state_record* shader = static_cast<v3d_gl_shader_state_record*>(
        allocate(&Gpu.state, sizeof(*shader), 32));
    if (!shader) return 0;
    memset(shader, 0, sizeof(*shader));
    shader->enable_clipping = true;
    shader->fragment_shader_uses_real_pixel_centre_w_in_addition_to_centroid_w2 = true;
    shader->disable_implicit_point_line_varyings = true;
    shader->number_of_varyings_in_fragment_shader = 2;
    shader->address_of_default_attribute_values = V3D_ARM_TO_BUS_ADDR(defaults);

    shader->fragment_shader_code_address_rshift_3 = V3D_ARM_TO_BUS_ADDR(fragmentAddress) >> 3;
    shader->fragment_shader_uniforms_address = V3D_ARM_TO_BUS_ADDR(fragmentUniforms);
    shader->fragment_shader_4_way_threadable = true;
    shader->fragment_shader_propagate_nans = true;

    shader->vertex_shader_code_address_rshift_3 = V3D_ARM_TO_BUS_ADDR(vertexAddress) >> 3;
    shader->vertex_shader_uniforms_address = V3D_ARM_TO_BUS_ADDR(vertexUniforms);
    shader->vertex_shader_4_way_threadable = true;
    shader->vertex_shader_start_in_final_thread_section = true;
    shader->vertex_shader_propagate_nans = true;
    shader->vertex_shader_output_vpm_segment_size = 2;
    shader->vertex_shader_input_vpm_segment_size = 1;

    shader->coordinate_shader_code_address_rshift_3 = V3D_ARM_TO_BUS_ADDR(coordinateAddress) >> 3;
    shader->coordinate_shader_uniforms_address = V3D_ARM_TO_BUS_ADDR(coordinateUniforms);
    shader->coordinate_shader_4_way_threadable = true;
    shader->coordinate_shader_start_in_final_thread_section = true;
    shader->coordinate_shader_propagate_nans = true;
    shader->coordinate_shader_output_vpm_segment_size = 1;
    shader->coordinate_shader_input_vpm_segment_size = 1;

    v3d_gl_shader_state_attribute_record* attributes =
        static_cast<v3d_gl_shader_state_attribute_record*>(
            v3d_buffer_claim_memory(&Gpu.state, sizeof(*attributes) * 4));
    if (!attributes) return 0;
    memset(attributes, 0, sizeof(*attributes) * 4);

    attributes[0].address = V3D_ARM_TO_BUS_ADDR(Gpu.vertices);
    attributes[0].number_of_values_read_by_vertex_shader = 3;
    attributes[0].number_of_values_read_by_coordinate_shader = 3;
    attributes[0].stride = 3 * sizeof(float);
    attributes[0].maximum_index = 0xffffff;
    attributes[0].vec_size = v3d_VEC_3;
    attributes[0].type = v3d_ATTRIBUTE_FLOAT;

    attributes[1].address = V3D_ARM_TO_BUS_ADDR(&Gpu.instance->atlasX);
    attributes[1].number_of_values_read_by_vertex_shader = 4;
    attributes[1].instance_divisor = 1;
    attributes[1].stride = sizeof(Instance);
    attributes[1].maximum_index = 0xffffff;
    attributes[1].vec_size = v3d_VEC_4;
    attributes[1].type = v3d_ATTRIBUTE_FLOAT;

    attributes[2].address = V3D_ARM_TO_BUS_ADDR(&Gpu.instance->renderX);
    attributes[2].number_of_values_read_by_vertex_shader = 4;
    attributes[2].number_of_values_read_by_coordinate_shader = 4;
    attributes[2].instance_divisor = 1;
    attributes[2].stride = sizeof(Instance);
    attributes[2].maximum_index = 0xffffff;
    attributes[2].vec_size = v3d_VEC_4;
    attributes[2].type = v3d_ATTRIBUTE_FLOAT;

    attributes[3].address = V3D_ARM_TO_BUS_ADDR(&Gpu.instance->colorR);
    attributes[3].number_of_values_read_by_vertex_shader = 4;
    attributes[3].instance_divisor = 1;
    attributes[3].stride = sizeof(Instance);
    attributes[3].maximum_index = 0xffffff;
    attributes[3].vec_size = v3d_VEC_4;
    attributes[3].type = v3d_ATTRIBUTE_FLOAT;

    *attributeCount = 4;
    return shader;
}

static bool prepareBinning(v3d_gl_shader_state_record* shader, unsigned attributeCount)
{
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_TILE_BINNING_MODE_CFG,
                               v3d_tile_binning_mode_cfg, mode);
    if (!mode) return false;
    mode->maximum_bpp_of_all_render_targets = V3D_INTERNAL_BPP_32;
    mode->width_in_pixels_minus_one = OutputWidth - 1;
    mode->height_in_pixels_minus_one = OutputHeight - 1;

    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.binning, v3d_OP_FLUSH_VCD_CACHE,
                                            v3d_flush_vcd_cache);
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_OCCLUSION_QUERY_COUNTER,
                               v3d_occlusion_query_counter, occlusion);
    if (!occlusion) return false;
    occlusion->address = 0;
    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.binning, v3d_OP_START_TILE_BINNING,
                                            v3d_start_tile_binning);

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_CLIPWINDOW, v3d_clipwindow, clip);
    if (!clip) return false;
    clip->clip_window_width_in_pixels = OutputWidth;
    clip->clip_window_height_in_pixels = OutputHeight;

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_CFG_BITS, v3d_cfg_bits, config);
    if (!config) return false;
    config->enable_forward_facing_primitive = true;
    config->enable_reverse_facing_primitive = true;
    config->clockwise_primitives = true;
    config->line_rasterization = V3D_LINE_RASTERIZATION_DIAMOND_EXIT;
    // Fullscreen post-processing has no depth attachment or depth writes.
    config->depth_test_function = V3D_COMPARE_FUNC_ALWAYS;
    config->early_z_updates_enable = false;

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_POINT_SIZE, v3d_point_size, point);
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_LINE_WIDTH, v3d_line_width, line);
    if (!point || !line) return false;
    point->point_size = 1.0f;
    line->line_width = 1.0f;

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_CLIPPER_XY_SCALING,
                               v3d_clipper_xy_scaling, xy);
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_CLIPPER_Z_SCALE_AND_OFFSET,
                               v3d_clipper_z_scale_and_offset, z);
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_CLIPPER_Z_MIN_MAX_CLIPPING_PLANES,
                               v3d_clipper_z_min_max_clipping_planes, zClip);
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_VIEWPORT_OFFSET,
                               v3d_viewport_offset, viewport);
    if (!xy || !z || !zClip || !viewport) return false;
    xy->viewport_half_width_in_1_256th_of_pixel = OutputWidth * 128.0f;
    xy->viewport_half_height_in_1_256th_of_pixel = OutputHeight * -128.0f;
    z->viewport_z_scale_zc_to_zs = 0.5f;
    z->viewport_z_offset_zc_to_zs = 0.5f;
    zClip->maximum_zw = 1.0f;
    viewport->fine_x = V3D_FLOAT_TO_U14_8(OutputWidth / 2.0f);
    viewport->fine_y = V3D_FLOAT_TO_U14_8(OutputHeight / 2.0f);

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_COLOR_WRITE_MASKS,
                               v3d_color_write_masks, masks);
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_BLEND_CONSTANT_COLOR,
                               v3d_blend_constant_color, blend);
    if (!masks || !blend) return false;
    masks->mask = 0;

    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.binning, v3d_OP_ZERO_ALL_FLAT_SHADE_FLAGS,
                                            v3d_zero_all_flat_shade_flags);
    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.binning, v3d_OP_ZERO_ALL_NON_PERSPECTIVE_FLAGS,
                                            v3d_zero_all_non_perspective_flags);
    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.binning, v3d_OP_ZERO_ALL_CENTROID_FLAGS,
                                            v3d_zero_all_centroid_flags);

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_TRANSFORM_FEEDBACK_SPECS,
                               v3d_transform_feedback_specs, transform);
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_OCCLUSION_QUERY_COUNTER,
                               v3d_occlusion_query_counter, occlusion2);
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_SAMPLE_STATE,
                               v3d_sample_state, sample);
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_VCM_CACHE_SIZE,
                               v3d_vcm_cache_size, cache);
    if (!transform || !occlusion2 || !sample || !cache) return false;
    occlusion2->address = 0;
    sample->mask = 0xf;
    sample->coverage = v3d_float_to_f187(1.0f);
    cache->number_of_16_vertex_batches_for_binning = 4;
    cache->number_of_16_vertex_batches_for_rendering = 4;

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_GL_SHADER_STATE,
                               v3d_gl_shader_state, shaderCommand);
    if (!shaderCommand) return false;
    shaderCommand->number_of_attribute_arrays = attributeCount;
    shaderCommand->address_rshift_5 = V3D_ARM_TO_BUS_ADDR(shader) >> 5;

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.binning, v3d_OP_VERTEX_ARRAY_INSTANCED_PRIMS,
                               v3d_vertex_array_instanced_prims, primitives);
    if (!primitives) return false;
    primitives->mode = V3D_PRIM_TRIANGLES;
    primitives->instance_length = 6;
    primitives->number_of_instances = 1;
    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.binning, v3d_OP_FLUSH, v3d_flush);
    return !v3d_buffer_out_of_memory(&Gpu.binning);
}

static void getSuperTiles(unsigned* width, unsigned* height,
                          unsigned* frameWidth, unsigned* frameHeight)
{
    *width = 1;
    *height = 1;
    for (;;)
    {
        *frameWidth = (TilesX + *width - 1) / *width;
        *frameHeight = (TilesY + *height - 1) / *height;
        if (*frameWidth * *frameHeight < 256) return;
        if (*width < *height) ++*width;
        else ++*height;
    }
}

static bool prepareRendering()
{
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.rendering, v3d_OP_TILE_RENDERING_MODE_CFG_COMMON,
                               v3d_tile_rendering_mode_cfg_common, common);
    if (!common) return false;
    common->sub_id = 0;
    common->number_of_render_targets_minus_one = 0;
    common->image_width_pixels = OutputWidth;
    common->image_height_pixels = OutputHeight;
    common->maximum_bpp_of_all_render_targets = V3D_INTERNAL_BPP_32;
    common->early_z_test_and_update_direction = v3d_EARLY_Z_DIRECTION_LT_LE;
    common->internal_depth_type = V3D_INTERNAL_TYPE_DEPTH16;
    common->early_z_disable = true;

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.rendering, v3d_OP_TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART1,
                               v3d_tile_rendering_mode_cfg_clear_colors_part1, clearColor);
    if (!clearColor) return false;
    clearColor->sub_id = 3;
    clearColor->render_target_number = 0;
    clearColor->clear_color_low_32_bits = 0xff000000;

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.rendering, v3d_OP_TILE_RENDERING_MODE_CFG_COLOR,
                               v3d_tile_rendering_mode_cfg_color, color);
    if (!color) return false;
    color->sub_id = 1;
    color->render_target_0_internal_bpp = V3D_INTERNAL_BPP_32;
    color->render_target_0_internal_type = V3D_INTERNAL_TYPE_8;
    color->render_target_0_clamp = V3D_RENDER_TARGET_CLAMP_NONE;

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.rendering, v3d_OP_TILE_RENDERING_MODE_CFG_ZS_CLEAR_VALUES,
                               v3d_tile_rendering_mode_cfg_zs_clear_values, zsClear);
    if (!zsClear) return false;
    zsClear->sub_id = 2;
    zsClear->z_clear_value = 1.0f;

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.rendering, v3d_OP_TILE_LIST_INITIAL_BLOCK_SIZE,
                               v3d_tile_list_initial_block_size, blockSize);
    if (!blockSize) return false;
    blockSize->size_of_first_block_in_chained_tile_lists = v3d_TILE_ALLOCATION_BLOCK_SIZE_64B;
    blockSize->use_auto_chained_tile_lists = true;

    unsigned superWidth, superHeight, frameWidth, frameHeight;
    getSuperTiles(&superWidth, &superHeight, &frameWidth, &frameHeight);

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.rendering, v3d_OP_MULTICORE_RENDERING_TILE_LIST_SET_BASE,
                               v3d_multicore_rendering_tile_list_set_base, tileBase);
    if (!tileBase) return false;
    tileBase->tile_list_set_number = 0;
    tileBase->address_rshift_6 = V3D_ARM_TO_BUS_ADDR(Gpu.tileAllocation) >> 6;

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.rendering, v3d_OP_MULTICORE_RENDERING_SUPERTILE_CFG,
                               v3d_multicore_rendering_supertile_cfg, superConfig);
    if (!superConfig) return false;
    superConfig->supertile_width_in_tiles_minus_one = superWidth - 1;
    superConfig->supertile_height_in_tiles_minus_one = superHeight - 1;
    superConfig->total_frame_width_in_supertiles = frameWidth;
    superConfig->total_frame_height_in_supertiles = frameHeight;
    superConfig->total_frame_width_in_tiles = TilesX;
    superConfig->total_frame_height_in_tiles = TilesY;

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.rendering, v3d_OP_TILE_COORDINATES,
                               v3d_tile_coordinates, clearTile);
    if (!clearTile) return false;
    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.rendering, v3d_OP_END_OF_LOADS,
                                            v3d_end_of_loads);
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.rendering, v3d_OP_STORE_TILE_BUFFER_GENERAL,
                               v3d_store_tile_buffer_general, clearStore);
    if (!clearStore) return false;
    clearStore->buffer_to_store = v3d_NONE;
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.rendering, v3d_OP_CLEAR_TILE_BUFFERS,
                               v3d_clear_tile_buffers, clearBuffers);
    if (!clearBuffers) return false;
    clearBuffers->clear_all_render_targets = true;
    clearBuffers->clear_z_stencil_buffer = true;
    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.rendering, v3d_OP_END_OF_TILE_MARKER,
                                            v3d_end_of_tile_marker);

    // Required dummy store avoids a documented V3D tile-list startup race.
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.rendering, v3d_OP_TILE_COORDINATES,
                               v3d_tile_coordinates, dummyTile);
    if (!dummyTile) return false;
    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.rendering, v3d_OP_END_OF_LOADS,
                                            v3d_end_of_loads);
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.rendering, v3d_OP_STORE_TILE_BUFFER_GENERAL,
                               v3d_store_tile_buffer_general, dummyStore);
    if (!dummyStore) return false;
    dummyStore->buffer_to_store = v3d_NONE;
    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.rendering, v3d_OP_END_OF_TILE_MARKER,
                                            v3d_end_of_tile_marker);
    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.rendering, v3d_OP_FLUSH_VCD_CACHE,
                                            v3d_flush_vcd_cache);

    uint8_t* indirectStart = V3D_BUFFER_WRITE_HEAD(Gpu.indirect);
    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.indirect, v3d_OP_TILE_COORDINATES_IMPLICIT,
                                            v3d_tile_coordinates_implicit);
    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.indirect, v3d_OP_END_OF_LOADS,
                                            v3d_end_of_loads);
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.indirect, v3d_OP_PRIM_LIST_FORMAT,
                               v3d_prim_list_format, primitiveFormat);
    if (!primitiveFormat) return false;
    primitiveFormat->primitive_type = v3d_LIST_TRIANGLES;
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.indirect, v3d_OP_SET_INSTANCEID,
                               v3d_set_instanceid, instanceId);
    if (!instanceId) return false;
    instanceId->instance_id = 0;
    V3D_BUFFER_ALLOC_OPERATION(&Gpu.indirect, v3d_OP_BRANCH_TO_IMPLICIT_TILE_LIST,
                               v3d_branch_to_implicit_tile_list, branch);
    if (!branch) return false;
    branch->tile_list_set_number = 0;

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.indirect, v3d_OP_STORE_TILE_BUFFER_GENERAL,
                               v3d_store_tile_buffer_general, targetStore);
    if (!targetStore) return false;
    targetStore->buffer_to_store = v3d_RENDER_TARGET_0;
    targetStore->memory_format = V3D_MEMORY_FORMAT_RASTER;
    targetStore->dither_mode = V3D_DITHER_MODE_NONE;
    targetStore->decimate_mode = V3D_DECIMATE_MODE_SAMPLE_0;
    targetStore->output_image_format = V3D_OUTPUT_IMAGE_FORMAT_RGBA8;
    targetStore->r_b_swap = true;
    targetStore->height_in_ub_or_stride = Gpu.framebufferPitch * sizeof(uint32_t);
    targetStore->address = V3D_ARM_TO_BUS_ADDR(Gpu.framebuffer);

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.indirect, v3d_OP_CLEAR_TILE_BUFFERS,
                               v3d_clear_tile_buffers, tileClear);
    if (!tileClear) return false;
    tileClear->clear_all_render_targets = true;
    tileClear->clear_z_stencil_buffer = true;
    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.indirect, v3d_OP_END_OF_TILE_MARKER,
                                            v3d_end_of_tile_marker);
    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.indirect, v3d_OP_RETURN_FROM_SUB_LIST,
                                            v3d_return_from_sub_list);

    V3D_BUFFER_ALLOC_OPERATION(&Gpu.rendering, v3d_OP_START_ADDRESS_OF_GENERIC_TILE_LIST,
                               v3d_start_address_of_generic_tile_list, generic);
    if (!generic) return false;
    generic->start = V3D_ARM_TO_BUS_ADDR(indirectStart);
    generic->end = V3D_ARM_TO_BUS_ADDR(V3D_BUFFER_WRITE_HEAD(Gpu.indirect));

    for (unsigned y = 0; y < frameHeight; ++y)
    {
        for (unsigned x = 0; x < frameWidth; ++x)
        {
            V3D_BUFFER_ALLOC_OPERATION(&Gpu.rendering, v3d_OP_SUPERTILE_COORDINATES,
                                       v3d_supertile_coordinates, coordinates);
            if (!coordinates) return false;
            coordinates->column_number_in_supertiles = x;
            coordinates->row_number_in_supertiles = y;
        }
    }
    V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(&Gpu.rendering, v3d_OP_END_OF_RENDERING,
                                            v3d_end_of_rendering);

    return !v3d_buffer_out_of_memory(&Gpu.rendering)
           && !v3d_buffer_out_of_memory(&Gpu.indirect);
}

static uint8_t dim27(unsigned value)
{
    return static_cast<uint8_t>(value - (value >> 3) - (value >> 5));
}

static uint8_t scanline(unsigned value, unsigned row)
{
    if (row == 0) return static_cast<uint8_t>(value - (value >> 3));
    if (row == 1) return static_cast<uint8_t>(value);
    if (row == 2) return static_cast<uint8_t>(value - (value >> 4));
    return static_cast<uint8_t>(value - (value >> 2) - (value >> 4));
}

static void prepareMask()
{
    const uint8_t dim = dim27(255);
    for (unsigned y = 0; y < OutputHeight; ++y)
    {
        for (unsigned x = 0; x < OutputWidth; ++x)
        {
            uint8_t channels[3] = {dim, dim, dim};
            channels[x % 3] = 255;
            const unsigned row = y % TIC80_BAREMETAL_SCREEN_SCALE;
            const uint32_t rgba = scanline(channels[0], row)
                                  | static_cast<uint32_t>(scanline(channels[1], row)) << 8
                                  | static_cast<uint32_t>(scanline(channels[2], row)) << 16
                                  | 0xff000000;
            const unsigned offset = v3d_get_uif_no_xor_pixel_offset(
                sizeof(uint32_t), MaskUifPaddedHeight, x, y);
            memcpy(Gpu.maskUif + offset, &rgba, sizeof rgba);
        }
    }
}

static inline uint32_t blurPixel(const uint32_t* line, int x)
{
    const uint32_t center = line[x];
    const uint32_t left = line[x > 0 ? x - 1 : x];
    const uint32_t right = line[x + 1 < TIC80_WIDTH ? x + 1 : x];
    const uint32_t redBlue = ((left & 0x00ff00ff) + 6 * (center & 0x00ff00ff)
                              + (right & 0x00ff00ff)) >> 3 & 0x00ff00ff;
    const uint32_t green = ((left & 0x0000ff00) + 6 * (center & 0x0000ff00)
                            + (right & 0x0000ff00)) >> 3 & 0x0000ff00;
    return 0xff000000 | redBlue | green;
}

static void prepareSource(const uint32_t* source)
{
    for (int y = 0; y < TIC80_HEIGHT; ++y)
    {
        const uint32_t* input = source + (y + TIC80_MARGIN_TOP) * TIC80_FULLWIDTH
                                + TIC80_MARGIN_LEFT;
        uint32_t* output = Gpu.blurred + y * TIC80_WIDTH;
        for (int x = 0; x < TIC80_WIDTH; ++x) output[x] = blurPixel(input, x);
    }

    const v3d_texture_box box = {0, 0, TIC80_WIDTH, TIC80_HEIGHT};
    v3d_store_tiled_image(Gpu.sourceUif, SourceUifWidth * sizeof(uint32_t), Gpu.blurred,
                          TIC80_WIDTH * sizeof(uint32_t), V3D_MEMORY_FORMAT_UIF_NO_XOR,
                          sizeof(uint32_t), SourceUifPaddedHeight, &box);
}

static bool elapsed(unsigned start, unsigned timeout)
{
    return static_cast<unsigned>(CTimer::GetClockTicks() - start) >= timeout;
}

#include "v3d_power.h"

static bool configureDirectAddressing()
{
    // Quiesce AXI before restoring the V3D 4.2 reset-state direct-addressing mode.
    // The VideoCore firmware may have left translation or protection enabled.
    V3D_write(V3dGmpConfig, V3dGmpStopRequest);
    unsigned start = CTimer::GetClockTicks();
    while (V3D_read(V3dGmpStatus)
           & (V3dGmpReadCountMask | V3dGmpWriteCountMask | V3dGmpConfigBusy))
        if (elapsed(start, PowerTimeoutUs)) return false;

    V3D_write(V3dMmucControl, 0);
    V3D_write(V3dMmuControl, 0);
    V3D_write(V3dMmuAddressCap, 0);
    V3D_write(V3dMmuIllegalAddress, 0);
    V3D_write(V3dHubInterruptClear, 0xffffffffu);
    V3D_write(V3dCoreInterruptClear, 0xffffffffu);
    V3D_write(V3dGmpConfig, 0);
    start = CTimer::GetClockTicks();
    while (V3D_read(V3dGmpStatus) & V3dGmpConfigBusy)
        if (elapsed(start, PowerTimeoutUs)) return false;
    DataSyncBarrier();

    return !(V3D_read(V3dMmuControl) & 1u)
           && !(V3D_read(V3dMmucControl) & (V3dMmucFlushing | 1u))
           && !(V3D_read(V3dMmuAddressCap) & 0x80000000u)
           && !(V3D_read(V3dMmuIllegalAddress) & 0x80000000u)
           && !(V3D_read(V3dGmpConfig) & (V3dGmpStopRequest | 1u))
           && !(V3D_read(V3dGmpStatus) & V3dGmpConfigBusy);
}

static void logAddressState(const char* label)
{
    char message[192];
    snprintf(message, sizeof message,
             "[tic80] V3D CRT: %s mmuc=%08lx mmu=%08lx cap=%08lx "
             "illegal=%08lx gmp=%08lx/%08lx\n",
             label,
             static_cast<unsigned long>(V3D_read(V3dMmucControl)),
             static_cast<unsigned long>(V3D_read(V3dMmuControl)),
             static_cast<unsigned long>(V3D_read(V3dMmuAddressCap)),
             static_cast<unsigned long>(V3D_read(V3dMmuIllegalAddress)),
             static_cast<unsigned long>(V3D_read(V3dGmpStatus)),
             static_cast<unsigned long>(V3D_read(V3dGmpConfig)));
    tic80SerialDebug(message);
}

static void logControlThreadState(const char* label)
{
    char message[256];
    snprintf(message, sizeof message,
             "[tic80] V3D CRT: %s ct0cs=%08lx ct1cs=%08lx "
             "ct0=%08lx/%08lx ct1=%08lx/%08lx\n",
             label,
             static_cast<unsigned long>(V3D_read(V3D_CLE_CT0CS)),
             static_cast<unsigned long>(V3D_read(V3D_CLE_CT1CS)),
             static_cast<unsigned long>(V3D_read(V3dCleCt0Ca)),
             static_cast<unsigned long>(V3D_read(V3dCleCt0Ea)),
             static_cast<unsigned long>(V3D_read(V3D_CLE_CT1CA)),
             static_cast<unsigned long>(V3D_read(V3dCleCt1Ea)));
    tic80SerialDebug(message);
}

static WaitResult waitForCount(bool rendering, uint8_t previous)
{
    const unsigned start = CTimer::GetClockTicks();
    do
    {
        const uint8_t current = rendering ? v3d_get_render_frame_count()
                                          : v3d_get_binning_flush_count();
        if (current != previous) return WaitComplete;
        const uint32_t status = V3D_read(rendering ? V3D_CLE_CT1CS : V3D_CLE_CT0CS);
        if (status & V3D_CLE_CTNCS_CTERR) return WaitControllerError;
    } while (!elapsed(start, CommandTimeoutUs));
    return WaitTimeout;
}

static void logCommandFailure(const char* stage, WaitResult result)
{
    char message[640];
    snprintf(message, sizeof message,
             "[tic80] V3D CRT: %s %s; ct0cs=%08lx ct1cs=%08lx "
             "ct0=%08lx/%08lx/%08lx ct1=%08lx/%08lx/%08lx "
             "pcs=%08lx err=%08lx bfc=%u rfc=%u q0=%08lx/%08lx q1=%08lx/%08lx "
             "qts=%08lx hubint=%08lx coreint=%08lx "
             "mmuc=%08lx mmu=%08lx vio=%08lx/%08lx gmp=%08lx/%08lx; "
             "disabled, using CPU renderer\n",
             stage, result == WaitTimeout ? "timeout" : "controller error",
             static_cast<unsigned long>(V3D_read(V3D_CLE_CT0CS)),
             static_cast<unsigned long>(V3D_read(V3D_CLE_CT1CS)),
             static_cast<unsigned long>(V3D_read(V3dCleCt0Ca)),
             static_cast<unsigned long>(V3D_read(V3dCleCt0Ea)),
             static_cast<unsigned long>(V3D_read(V3dCleCt0Lc)),
             static_cast<unsigned long>(V3D_read(V3D_CLE_CT1CA)),
             static_cast<unsigned long>(V3D_read(V3dCleCt1Ea)),
             static_cast<unsigned long>(V3D_read(V3D_CLE_CT1LC)),
             static_cast<unsigned long>(V3D_read(V3dClePcs)),
             static_cast<unsigned long>(V3D_read(V3dErrorStatus)),
             v3d_get_binning_flush_count(), v3d_get_render_frame_count(),
             static_cast<unsigned long>(V3D_read(V3D_CLE_CT0QBA)),
             static_cast<unsigned long>(V3D_read(V3D_CLE_CT0QEA)),
             static_cast<unsigned long>(V3D_read(V3D_CLE_CT1QBA)),
             static_cast<unsigned long>(V3D_read(V3D_CLE_CT1QEA)),
             static_cast<unsigned long>(V3D_read(V3D_CLE_CT0QTS)),
             static_cast<unsigned long>(V3D_read(V3dHubInterruptStatus)),
             static_cast<unsigned long>(V3D_read(V3dCoreInterruptStatus)),
             static_cast<unsigned long>(V3D_read(V3dMmucControl)),
             static_cast<unsigned long>(V3D_read(V3dMmuControl)),
             static_cast<unsigned long>(V3D_read(V3dMmuViolationId)),
             static_cast<unsigned long>(V3D_read(V3dMmuViolationAddress)),
             static_cast<unsigned long>(V3D_read(V3dGmpStatus)),
             static_cast<unsigned long>(V3D_read(V3dGmpConfig)));
    tic80SerialDebug(message);
}

static bool addressRangeValid(const void* pointer, unsigned size)
{
    const uintptr_t start = reinterpret_cast<uintptr_t>(pointer);
    return pointer && start <= 0xffffffffu && size <= 0xffffffffu - start;
}

static uint8_t* allocateBytes(unsigned size, unsigned alignment)
{
    return static_cast<uint8_t*>(allocate(&Gpu.arena, size, alignment));
}

static uint32_t probeColor(unsigned x, unsigned y)
{
    static const uint32_t colors[] = {
        0xffe04020, 0xff2080e0, 0xff40d060, 0xffd0b040,
    };
    return colors[(y >= TIC80_HEIGHT / 2) * 2 + (x >= TIC80_WIDTH / 2)];
}

static bool probePixelsValid(const uint32_t* output, unsigned pitch)
{
    // Sample all four quadrants, scanline phases, and RGB mask phases.
    for (unsigned quadrant = 0; quadrant < 4; ++quadrant)
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned col = 0; col < 3; ++col)
            {
                const unsigned x = (quadrant % 2) * OutputWidth / 2 + 120 + col;
                const unsigned y = (quadrant / 2) * OutputHeight / 2 + 68 + row;
                const uint32_t color = probeColor(x / 4, y / 4);
                const uint32_t pixel = output[y * pitch + x];
                for (unsigned channel = 0; channel < 3; ++channel)
                {
                    const unsigned shift = channel * 8;
                    const unsigned maskChannel = 2 - channel; // BGRA output
                    const unsigned mask = scanline(x % 3 == maskChannel ? 255 : dim27(255), y % 4);
                    const int expected = (((color >> shift) & 255) * mask + 127) / 255;
                    const int actual = (pixel >> shift) & 255;
                    if (abs(expected - actual) > 3)
                    {
                        char message[192];
                        snprintf(message, sizeof message,
                                 "[tic80] V3D CRT: pixel test failed x=%u y=%u pixel=%08lx channel=%u expected=%d actual=%d\n",
                                 x, y, static_cast<unsigned long>(pixel), channel, expected, actual);
                        tic80SerialDebug(message);
                        return false;
                    }
                }
            }
    return true;
}

static bool validateOutput()
{
    uint32_t* source = static_cast<uint32_t*>(malloc(TIC80_FULLWIDTH * TIC80_FULLHEIGHT * sizeof(uint32_t)));
    if (!source) return false;
    uint32_t* output = Gpu.framebuffer;
    memset(output, 0x5a, OutputSize + 64);
    memset(source, 0, TIC80_FULLWIDTH * TIC80_FULLHEIGHT * sizeof(uint32_t));
    for (unsigned y = 0; y < TIC80_HEIGHT; ++y)
        for (unsigned x = 0; x < TIC80_WIDTH; ++x)
            source[(y + TIC80_MARGIN_TOP) * TIC80_FULLWIDTH + x + TIC80_MARGIN_LEFT] = probeColor(x, y);

    CleanAndInvalidateDataCacheRange(reinterpret_cast<uintptr_t>(output), OutputSize + 64);
    bool valid = tic80_baremetal_v3d_render(source);
    // No CPU writes touch this cache-line-aligned region during GPU execution.
    CleanAndInvalidateDataCacheRange(reinterpret_cast<uintptr_t>(output), OutputSize + 64);
    if (valid) valid = probePixelsValid(output, OutputWidth);
    for (unsigned i = 0; i < 16; ++i)
        if (output[OutputWidth * OutputHeight + i] != 0x5a5a5a5a) valid = false;

    free(source);
    return valid;
}
}

bool tic80_baremetal_v3d_initialize(uint32_t* framebuffer, unsigned framebufferPitch)
{
    if (Gpu.ready) return true;
    if (!framebuffer || framebufferPitch < OutputWidth
        || framebufferPitch > 0xfffffu / sizeof(uint32_t)
        || framebufferPitch > 0xffffffffu / OutputHeight / sizeof(uint32_t)
        || !addressRangeValid(framebuffer, framebufferPitch * OutputHeight * sizeof(uint32_t)))
    {
        tic80SerialDebug("[tic80] V3D CRT: invalid framebuffer; using CPU renderer\n");
        return false;
    }

    Gpu.scanout = framebuffer;
    Gpu.scanoutPitch = framebufferPitch;
    Gpu.framebufferPitch = OutputWidth;
    Gpu.arenaAllocation = static_cast<uint8_t*>(malloc(ArenaSize + 4095));
    if (!Gpu.arenaAllocation)
    {
        tic80SerialDebug("[tic80] V3D CRT: arena allocation failed; using CPU renderer\n");
        return false;
    }

    uint8_t* aligned = static_cast<uint8_t*>(v3d_get_aligned_address(Gpu.arenaAllocation, 4096));
    const unsigned adjustment = aligned - Gpu.arenaAllocation;
    Gpu.arena = {aligned, 0, static_cast<int>(ArenaSize + 4095 - adjustment)};
    memset(aligned, 0, Gpu.arena.capacity);

    Gpu.blurred = reinterpret_cast<uint32_t*>(allocateBytes(BlurredSize, 64));
    Gpu.vertices = reinterpret_cast<float*>(allocateBytes(sizeof Vertices, 16));
    Gpu.instance = reinterpret_cast<AtlasInstance*>(allocateBytes(sizeof Instance, 16));
    Gpu.sourceUif = allocateBytes(SourceUifSize, 4096);
    Gpu.maskUif = allocateBytes(MaskUifSize, 4096);
    Gpu.tileAllocation = allocateBytes(TileAllocationSize, 4096);
    Gpu.tileState = allocateBytes(TileStateSize, 4096);
    Gpu.framebuffer = reinterpret_cast<uint32_t*>(allocateBytes(OutputSize + 64, 64));
    Gpu.binning = {allocateBytes(4096, 64), 0, 4096};
    Gpu.rendering = {allocateBytes(4096, 64), 0, 4096};
    Gpu.indirect = {allocateBytes(4096, 64), 0, 4096};
    Gpu.state = {allocateBytes(32 * 1024, 64), 0, 32 * 1024};

    if (!buffersValid() || v3d_buffer_out_of_memory(&Gpu.arena)
        || !addressRangeValid(aligned, Gpu.arena.used))
    {
        tic80SerialDebug("[tic80] V3D CRT: arena layout failed; using CPU renderer\n");
        return false;
    }
    memcpy(Gpu.vertices, Vertices, sizeof Vertices);
    memcpy(Gpu.instance, &Instance, sizeof Instance);
    if (!powerOn())
    {
        tic80SerialDebug("[tic80] V3D CRT: power-on timeout; using CPU renderer\n");
        return false;
    }

    const uint32_t ident = V3D_read(V3D_HUB_BASE + 0x0c);
    const unsigned version = (ident & 0xf) * 10 + ((ident >> 4) & 0xf);
    if (version != 42)
    {
        char message[96];
        snprintf(message, sizeof message,
                 "[tic80] V3D CRT: unsupported V3D version %u (ident %08lx); using CPU renderer\n",
                 version, static_cast<unsigned long>(ident));
        tic80SerialDebug(message);
        return false;
    }
    logAddressState("initial address state");
    if (!resetGpu())
    {
        tic80SerialDebug("[tic80] V3D CRT: GPU reset timed out; using CPU renderer\n");
        return false;
    }
    logControlThreadState("after full GPU reset");
    if (!configureDirectAddressing())
    {
        logAddressState("failed to enter direct-addressing mode");
        tic80SerialDebug("[tic80] V3D CRT: address setup failed; using CPU renderer\n");
        return false;
    }

    prepareMask();
    unsigned attributeCount = 0;
    v3d_gl_shader_state_record* shader = prepareShaderState(&attributeCount);
    if (!shader || !prepareBinning(shader, attributeCount) || !prepareRendering())
    {
        tic80SerialDebug("[tic80] V3D CRT: command-list construction failed; using CPU renderer\n");
        return false;
    }

    CleanAndInvalidateDataCacheRange(reinterpret_cast<uintptr_t>(aligned), Gpu.arena.used);
    v3d_invalidate_caches();
    Gpu.ready = true;
    char message[192];
    snprintf(message, sizeof message,
             "[tic80] V3D CRT: prepared, execution unverified (V3D 4.2 direct, fb=%08lx arena=%08lx pitch=%u, "
             "bcl=%u rcl=%u bytes)\n",
             static_cast<unsigned long>(reinterpret_cast<uintptr_t>(framebuffer)),
             static_cast<unsigned long>(reinterpret_cast<uintptr_t>(aligned)), framebufferPitch,
             static_cast<unsigned>(Gpu.binning.used),
             static_cast<unsigned>(Gpu.rendering.used));
    tic80SerialDebug(message);
    if (!validateOutput())
    {
        resetGpu();
        Gpu.ready = false;
        tic80SerialDebug("[tic80] V3D CRT: output validation failed; using CPU renderer\n");
        return false;
    }
    tic80SerialDebug("[tic80] V3D CRT: off-screen pixel test passed\n");
    Gpu.outputValidated = true;
    return true;
}

bool tic80_baremetal_v3d_render(const uint32_t* source)
{
    if (!Gpu.ready || !source) return false;
    prepareSource(source);
    CleanAndInvalidateDataCacheRange(reinterpret_cast<uintptr_t>(Gpu.sourceUif), SourceUifSize);
    CleanAndInvalidateDataCacheRange(reinterpret_cast<uintptr_t>(Gpu.framebuffer),
                                     Gpu.framebufferPitch * OutputHeight * sizeof(uint32_t));
    DataSyncBarrier();
    v3d_invalidate_caches();

    const uint8_t binningCount = v3d_get_binning_flush_count();
    v3d_start_binning_commands(V3D_ARM_TO_BUS_ADDR(Gpu.binning.start),
                               V3D_ARM_TO_BUS_ADDR(V3D_BUFFER_WRITE_HEAD(Gpu.binning)),
                               V3D_ARM_TO_BUS_ADDR(Gpu.tileAllocation), TileAllocationSize,
                               V3D_ARM_TO_BUS_ADDR(Gpu.tileState));
    DataSyncBarrier();
    const WaitResult binningResult = waitForCount(false, binningCount);
    if (binningResult != WaitComplete)
    {
        logCommandFailure("binning", binningResult);
        resetGpu();
        Gpu.ready = false;
        return false;
    }

    v3d_invalidate_caches();
    const uint8_t renderCount = v3d_get_render_frame_count();
    v3d_start_render_commands(V3D_ARM_TO_BUS_ADDR(Gpu.rendering.start),
                              V3D_ARM_TO_BUS_ADDR(V3D_BUFFER_WRITE_HEAD(Gpu.rendering)));
    DataSyncBarrier();
    const WaitResult renderResult = waitForCount(true, renderCount);
    if (renderResult != WaitComplete)
    {
        logCommandFailure("render", renderResult);
        resetGpu();
        Gpu.ready = false;
        return false;
    }

    DataSyncBarrier();
    static bool firstFrame = true;
    if (firstFrame)
    {
        tic80SerialDebug("[tic80] V3D CRT: first command pair completed (pixels not yet validated)\n");
        firstFrame = false;
    }
    if (Gpu.outputValidated)
    {
        CleanAndInvalidateDataCacheRange(reinterpret_cast<uintptr_t>(Gpu.framebuffer), OutputSize);
        for (unsigned row = 0; row < OutputHeight; ++row)
            memcpy(Gpu.scanout + row * Gpu.scanoutPitch,
                   Gpu.framebuffer + row * OutputWidth, OutputWidth * sizeof(uint32_t));
    }
    return true;
}
