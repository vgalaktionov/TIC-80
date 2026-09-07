// Emits representative V3D command packets for cross-ABI byte comparison.

#include <stdio.h>

#include "../../src/system/baremetalpi/v3d/v3d.h"

struct PACKED PacketCorpus
{
    v3d_tile_binning_mode_cfg binning;
    v3d_tile_rendering_mode_cfg_common common;
    v3d_tile_rendering_mode_cfg_color color;
    v3d_tile_rendering_mode_cfg_clear_colors_part1 clear_color;
    v3d_multicore_rendering_supertile_cfg supertile;
    v3d_store_tile_buffer_general store;
    v3d_gl_shader_state_record shader;
    v3d_gl_shader_state_attribute_record attribute;
    v3d_texture_shader_state texture;
    v3d_sampler_state sampler;
    v3d_tmu_config_parameter_0 texture_parameter_0;
    v3d_tmu_config_parameter_1 texture_parameter_1;
};

#ifdef V3D_LAYOUT_TEST_EXECUTABLE
#define PROBE_ATTRIBUTES
#else
#define PROBE_ATTRIBUTES __attribute__((section(".v3d_probe"), used))
#endif

const struct PacketCorpus Corpus PROBE_ATTRIBUTES = {
    .binning = {
        .operation = v3d_OP_TILE_BINNING_MODE_CFG,
        .tile_allocation_initial_block_size = 2,
        .tile_allocation_block_size = 1,
        .number_of_render_targets_minus_one = 3,
        .maximum_bpp_of_all_render_targets = 2,
        .multisample_mode_4x = 1,
        .double_buffer_in_non_ms_mode = 1,
        .width_in_pixels_minus_one = 959,
        .height_in_pixels_minus_one = 543,
    },
    .common = {
        .operation = v3d_OP_TILE_RENDERING_MODE_CFG_COMMON,
        .sub_id = 0,
        .number_of_render_targets_minus_one = 2,
        .image_width_pixels = 960,
        .image_height_pixels = 544,
        .maximum_bpp_of_all_render_targets = 1,
        .multisample_mode_4x = 1,
        .double_buffer_in_non_ms_mode = 1,
        .early_z_test_and_update_direction = 1,
        .early_z_disable = 1,
        .internal_depth_type = 2,
        .early_depth_stencil_clear = 1,
    },
    .color = {
        .operation = v3d_OP_TILE_RENDERING_MODE_CFG_COLOR,
        .sub_id = 1,
        .render_target_0_internal_bpp = 2,
        .render_target_0_internal_type = 9,
        .render_target_0_clamp = 2,
        .render_target_1_internal_bpp = 1,
        .render_target_1_internal_type = 6,
        .render_target_1_clamp = 1,
        .render_target_2_internal_bpp = 3,
        .render_target_2_internal_type = 5,
        .render_target_2_clamp = 3,
        .render_target_3_internal_bpp = 2,
        .render_target_3_internal_type = 10,
        .render_target_3_clamp = 2,
    },
    .clear_color = {
        .operation = v3d_OP_TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART1,
        .sub_id = 3,
        .render_target_number = 7,
        .clear_color_low_32_bits = 0x89abcdef,
        .clear_color_next_24_bits = 0x654321,
    },
    .supertile = {
        .operation = v3d_OP_MULTICORE_RENDERING_SUPERTILE_CFG,
        .supertile_width_in_tiles_minus_one = 4,
        .supertile_height_in_tiles_minus_one = 3,
        .total_frame_width_in_supertiles = 7,
        .total_frame_height_in_supertiles = 6,
        .total_frame_width_in_tiles = 0xabc,
        .total_frame_height_in_tiles = 0x789,
        .multicore_enable = 1,
        .supertile_raster_order = 1,
        .number_of_bin_tile_lists_minus_one = 5,
    },
    .store = {
        .operation = v3d_OP_STORE_TILE_BUFFER_GENERAL,
        .buffer_to_store = v3d_Z,
        .memory_format = V3D_MEMORY_FORMAT_UIF_XOR,
        .flip_y = 1,
        .dither_mode = 2,
        .decimate_mode = 3,
        .output_image_format = V3D_OUTPUT_IMAGE_FORMAT_D16,
        .clear_buffer_being_stored = 1,
        .channel_reverse = 1,
        .r_b_swap = 1,
        .height_in_ub_or_stride = 0xabcde,
        .height = 0x7654,
        .address = 0x89abcdef,
    },
    .shader = {
        .enable_clipping = 1,
        .fragment_shader_uses_real_pixel_centre_w_in_addition_to_centroid_w2 = 1,
        .disable_implicit_point_line_varyings = 1,
        .number_of_varyings_in_fragment_shader = 2,
        .coordinate_shader_output_vpm_segment_size = 1,
        .coordinate_shader_input_vpm_segment_size = 1,
        .vertex_shader_output_vpm_segment_size = 2,
        .vertex_shader_input_vpm_segment_size = 1,
        .address_of_default_attribute_values = 0x12345678,
        .fragment_shader_4_way_threadable = 1,
        .fragment_shader_propagate_nans = 1,
        .fragment_shader_code_address_rshift_3 = 0x01234567,
        .fragment_shader_uniforms_address = 0x89abcdef,
        .vertex_shader_4_way_threadable = 1,
        .vertex_shader_start_in_final_thread_section = 1,
        .vertex_shader_propagate_nans = 1,
        .vertex_shader_code_address_rshift_3 = 0x02345678,
        .vertex_shader_uniforms_address = 0xfedcba98,
        .coordinate_shader_4_way_threadable = 1,
        .coordinate_shader_start_in_final_thread_section = 1,
        .coordinate_shader_propagate_nans = 1,
        .coordinate_shader_code_address_rshift_3 = 0x03456789,
        .coordinate_shader_uniforms_address = 0x76543210,
    },
    .attribute = {
        .address = 0x11223344,
        .vec_size = v3d_VEC_3,
        .type = v3d_ATTRIBUTE_FLOAT,
        .number_of_values_read_by_coordinate_shader = 3,
        .number_of_values_read_by_vertex_shader = 4,
        .instance_divisor = 0x5678,
        .stride = 0x12345678,
        .maximum_index = 0x00ffffff,
    },
    .texture = {
        .texture_base_pointer_rshift_6 = 0x0234567,
        .array_stride_64_byte_aligned = 0x0123456,
        .image_width = 960,
        .image_height = 544,
        .image_depth = 1,
        .texture_type = V3D_TEXTURE_DATA_FORMAT_RGBA8,
        .extended = 1,
        .swizzle_r = v3d_SWIZZLE_BLUE,
        .swizzle_g = v3d_SWIZZLE_GREEN,
        .swizzle_b = v3d_SWIZZLE_RED,
        .swizzle_a = v3d_SWIZZLE_ALPHA,
        .level_0_ubpad = 2,
        .level_0_xor_enable = 0,
        .level_0_is_strictly_uif = 1,
    },
    .sampler = {
        .mag_filter_nearest = 1,
        .min_filter_nearest = 1,
        .depth_compare_function = V3D_COMPARE_FUNC_NEVER,
        .min_level_of_detail = 0x123,
        .max_level_of_detail = 0x456,
        .fixed_bias = -123,
        .wrap_s = V3D_WRAP_MODE_CLAMP,
        .wrap_t = V3D_WRAP_MODE_REPEAT,
        .wrap_r = V3D_WRAP_MODE_BORDER,
        .border_color_word_0 = 0x01234567,
        .border_color_word_1 = 0x89abcdef,
        .border_color_word_2 = 0xfedcba98,
        .border_color_word_3 = 0x76543210,
    },
    .texture_parameter_0 = {
        .return_words_of_texture_data = 3,
        .texture_state_address_rshift_4 = 0x0abcdef0,
    },
    .texture_parameter_1 = {
        .output_type_32_bit = 1,
        .unnormalized_coordinates = 1,
        .per_pixel_mask_enable = 1,
        .sampler_state_address_rshift_3 = 0x1234567,
    },
};

#ifdef V3D_LAYOUT_TEST_EXECUTABLE
int main(void)
{
    return fwrite(&Corpus, 1, sizeof Corpus, stdout) == sizeof Corpus ? 0 : 1;
}
#endif
