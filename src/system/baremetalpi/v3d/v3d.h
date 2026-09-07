// Copyright (C) 2025, Macoy Madson <macoy@macoy.me>
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice (including the next
// paragraph) shall be included in all copies or substantial portions of the
// Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//
// Also contains modified code from Mesa with the following copyrights, under the same terms as
// above:
//
// Copyright © 2017 Broadcom
// Copyright 2008 VMware, Inc.

//                    Raspberry Pi VideoCore VI VPU control
//                              by Macoy Madson
//                              macoy@macoy.me
//
// The goal of this module is to enable hardware accelerated 3D rendering on the Raspberry Pi 4,
// and possibly the 5 in the future. Older Pi models should look for a VC4 module instead.
//
// I am going to keep it lean and mean so that it is easy to integrate into other bare metal projects.
//
// * Usage
//
// This is a single-header library. You need to do the following in exactly ONE source file in your project:
//
//  #define V3D_IMPLEMENTATION
//  #include "v3d.h"
//  #undef V3D_IMPLEMENTATION
//
// Read the header to see what things you can override using #defines. I have deliberately made it
// self-contained while at the same time happy to call your existing functions if you want to
// override the defaults.
//
// * References
//
// The repository which I referenced in writing this is primarily this one:
// https://github.com/Random06457/rpi4-gpu-bare-metal-examples
//
// A more complete source is Mesa, which is where the data I used to generate the types etc. comes from:
// https://gitlab.freedesktop.org/mesa/mesa
//
// https://docs.mesa3d.org/drivers/v3d.html or
// (https://web.archive.org/web/20231126220908/https://docs.mesa3d.org/drivers/v3d.html)
// ...lists the V3D versions:
// - V3D 4.2 (Raspberry Pi 4)
// - V3D 7.1 (Raspberry Pi 5)

#ifndef _v3d_h
#define _v3d_h

// Feel free to redefine these if you have additional logging or something for raw memory/register reads
#ifndef V3D_read
#define V3D_read v3d_read32
#endif

#ifndef V3D_write
#define V3D_write v3d_write32
#endif

#ifndef V3D_memcpy
#define V3D_memcpy memcpy
#endif

#ifndef PACKED
#define PACKED __attribute__((packed))
#endif

#ifndef V3D_ARRAY_SIZE
#define V3D_ARRAY_SIZE(array) (sizeof((array)) / sizeof((array)[0]))
#endif

//
// Basic types
// Redefined hear to avoid any header inclusion

#ifndef FALSE
#define FALSE		0
#define TRUE		1
#endif

// Types with prefix
typedef signed int v3d_int;
typedef unsigned int v3d_u32;
typedef unsigned long long v3d_u64;
// Packet bitfields cross 32-bit word boundaries. Keep their allocation unit
// 64-bit on AArch32, matching unsigned long on the original AArch64 target.
typedef unsigned long long v3d_uint;
typedef float v3d_float;
typedef char v3d_bool;
// Hardware uses 32-bit addresses
typedef unsigned int v3d_address;
typedef unsigned long v3d_uintptr;
// Fixed point, 1 bit sign, 8 bits whole number, 7 bits fraction
typedef unsigned short v3d_f187;
// Fixed point unsigned
typedef unsigned int v3d_u14_8;
typedef unsigned short v3d_u4_8;
typedef signed short v3d_s8_8;
typedef unsigned char v3d_u8;

// Redefine if you e.g. have the MMU configured with virtual memory setting 0xffff... in the high
// bits, then have this macro mask those out back to physical addresses for the v3d/bus.
#ifndef V3D_ARM_TO_BUS_ADDR
#define V3D_ARM_TO_BUS_ADDR(address) ((v3d_address)((v3d_uintptr)(address)))
#endif

#define V3D_FLOAT_TO_U14_8(floatToConvert) (v3d_u14_8)((float)(floatToConvert) * 256.f)
#define V3D_FLOAT_TO_U4_8(floatToConvert) (v3d_u4_8)((float)(floatToConvert) * 256.f)

//
// V3D binary interface
//

// IMPORTANT: Note on addresses
//
// When sending an address to V3D, you may notice fields in the binary interface ending with
// "rshift_N". These indicate that the lower N bits of the address must be zero, which means the
// address must be aligned to the corresponding size required. This is usually used to then use the
// lower bits of the address to encode more information in the packet.
// All addresses in v3d must be <=32 bit.
//
// So, when you see rshift_N, ensure you meet the alignment requirements for the address, then do
// for e.g. an rshift_4 field:
//
//  thing->field_rshift_4 = my_aligned_address >> 4;

// The following is generated from:
// https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/broadcom/cle/v3d_packet.xml
// ...using my generator here: https://macoy.me/code/macoy/v3d-toolkit

typedef enum v3d_compare_function
{
	V3D_COMPARE_FUNC_NEVER = 0,
	V3D_COMPARE_FUNC_LESS = 1,
	V3D_COMPARE_FUNC_EQUAL = 2,
	V3D_COMPARE_FUNC_LEQUAL = 3,
	V3D_COMPARE_FUNC_GREATER = 4,
	V3D_COMPARE_FUNC_NOTEQUAL = 5,
	V3D_COMPARE_FUNC_GEQUAL = 6,
	V3D_COMPARE_FUNC_ALWAYS = 7,
} v3d_compare_function;

typedef enum v3d_blend_factor
{
	V3D_BLEND_FACTOR_ZERO = 0,
	V3D_BLEND_FACTOR_ONE = 1,
	V3D_BLEND_FACTOR_SRCCOLOR = 2,
	V3D_BLEND_FACTOR_INVSRCCOLOR = 3,
	V3D_BLEND_FACTOR_DSTCOLOR = 4,
	V3D_BLEND_FACTOR_INVDSTCOLOR = 5,
	V3D_BLEND_FACTOR_SRCALPHA = 6,
	V3D_BLEND_FACTOR_INVSRCALPHA = 7,
	V3D_BLEND_FACTOR_DSTALPHA = 8,
	V3D_BLEND_FACTOR_INVDSTALPHA = 9,
	V3D_BLEND_FACTOR_CONSTCOLOR = 10,
	V3D_BLEND_FACTOR_INVCONSTCOLOR = 11,
	V3D_BLEND_FACTOR_CONSTALPHA = 12,
	V3D_BLEND_FACTOR_INVCONSTALPHA = 13,
	V3D_BLEND_FACTOR_SRCALPHASATURATE = 14,
} v3d_blend_factor;

typedef enum v3d_blend_mode
{
	V3D_BLEND_MODE_ADD = 0,
	V3D_BLEND_MODE_SUB = 1,
	V3D_BLEND_MODE_RSUB = 2,
	V3D_BLEND_MODE_MIN = 3,
	V3D_BLEND_MODE_MAX = 4,
	V3D_BLEND_MODE_MUL = 5,
	V3D_BLEND_MODE_SCREEN = 6,
	V3D_BLEND_MODE_DARKEN = 7,
	V3D_BLEND_MODE_LIGHTEN = 8,
} v3d_blend_mode;

typedef enum v3d_stencil_op
{
	V3D_STENCIL_OP_ZERO = 0,
	V3D_STENCIL_OP_KEEP = 1,
	V3D_STENCIL_OP_REPLACE = 2,
	V3D_STENCIL_OP_INCR = 3,
	V3D_STENCIL_OP_DECR = 4,
	V3D_STENCIL_OP_INVERT = 5,
	V3D_STENCIL_OP_INCWRAP = 6,
	V3D_STENCIL_OP_DECWRAP = 7,
} v3d_stencil_op;

typedef enum v3d_primitive
{
	V3D_PRIM_POINTS = 0,
	V3D_PRIM_LINES = 1,
	V3D_PRIM_LINELOOP = 2,
	V3D_PRIM_LINESTRIP = 3,
	V3D_PRIM_TRIANGLES = 4,
	V3D_PRIM_TRIANGLESTRIP = 5,
	V3D_PRIM_TRIANGLEFAN = 6,
	V3D_PRIM_POINTSTF = 16,
	V3D_PRIM_LINESTF = 17,
	V3D_PRIM_LINELOOPTF = 18,
	V3D_PRIM_LINESTRIPTF = 19,
	V3D_PRIM_TRIANGLESTF = 20,
	V3D_PRIM_TRIANGLESTRIPTF = 21,
	V3D_PRIM_TRIANGLEFANTF = 22,
} v3d_primitive;

typedef enum v3d_border_color_mode
{
	V3D_BORDER_COLOR_0000 = 0,
	V3D_BORDER_COLOR_0001 = 1,
	V3D_BORDER_COLOR_1111 = 2,
	V3D_BORDER_COLOR_FOLLOWS = 7,
} v3d_border_color_mode;

typedef enum v3d_wrap_mode
{
	V3D_WRAP_MODE_REPEAT = 0,
	V3D_WRAP_MODE_CLAMP = 1,
	V3D_WRAP_MODE_MIRROR = 2,
	V3D_WRAP_MODE_BORDER = 3,
	V3D_WRAP_MODE_MIRRORONCE = 4,
} v3d_wrap_mode;

typedef enum v3d_tmu_op
{
	V3D_TMU_OP_WRITE_ADD_READ_PREFETCH = 0,
	V3D_TMU_OP_WRITE_SUB_READ_CLEAR = 1,
	V3D_TMU_OP_WRITE_XCHG_READ_FLUSH = 2,
	V3D_TMU_OP_WRITE_CMPXCHG_READ_FLUSH = 3,
	V3D_TMU_OP_WRITE_UMIN_FULL_L1_CLEAR = 4,
	V3D_TMU_OP_WRITE_UMAX = 5,
	V3D_TMU_OP_WRITE_SMIN = 6,
	V3D_TMU_OP_WRITE_SMAX = 7,
	V3D_TMU_OP_WRITE_AND_READ_INC = 8,
	V3D_TMU_OP_WRITE_OR_READ_DEC = 9,
	V3D_TMU_OP_WRITE_XOR_READ_NOT = 10,
	V3D_TMU_OP_REGULAR = 15,
} v3d_tmu_op;

typedef enum v3d_varying_flags_action
{
	V3D_VARYING_FLAGS_ACTION_UNCHANGED = 0,
	V3D_VARYING_FLAGS_ACTION_ZEROED = 1,
	V3D_VARYING_FLAGS_ACTION_SET = 2,
} v3d_varying_flags_action;

typedef enum v3d_memory_format
{
	V3D_MEMORY_FORMAT_RASTER = 0,
	V3D_MEMORY_FORMAT_LINEARTILE = 1,
	V3D_MEMORY_FORMAT_UB_LINEAR_1_UIF_BLOCK_WIDE = 2,
	V3D_MEMORY_FORMAT_UB_LINEAR_2_UIF_BLOCKS_WIDE = 3,
	V3D_MEMORY_FORMAT_UIF_NO_XOR = 4,
	V3D_MEMORY_FORMAT_UIF_XOR = 5,
} v3d_memory_format;

typedef enum v3d_decimate_mode
{
	V3D_DECIMATE_MODE_SAMPLE_0 = 0,
	V3D_DECIMATE_MODE_4X = 1,
	V3D_DECIMATE_MODE_ALL_SAMPLES = 3,
} v3d_decimate_mode;

typedef enum v3d_internal_type
{
	V3D_INTERNAL_TYPE_8I = 0,
	V3D_INTERNAL_TYPE_8UI = 1,
	V3D_INTERNAL_TYPE_8 = 2,
	V3D_INTERNAL_TYPE_16I = 4,
	V3D_INTERNAL_TYPE_16UI = 5,
	V3D_INTERNAL_TYPE_16F = 6,
	V3D_INTERNAL_TYPE_32I = 8,
	V3D_INTERNAL_TYPE_32UI = 9,
	V3D_INTERNAL_TYPE_32F = 10,
} v3d_internal_type;

typedef enum v3d_internal_bpp
{
	V3D_INTERNAL_BPP_32 = 0,
	V3D_INTERNAL_BPP_64 = 1,
	V3D_INTERNAL_BPP_128 = 2,
} v3d_internal_bpp;

typedef enum v3d_internal_depth_type
{
	V3D_INTERNAL_TYPE_DEPTH32F = 0,
	V3D_INTERNAL_TYPE_DEPTH24 = 1,
	V3D_INTERNAL_TYPE_DEPTH16 = 2,
} v3d_internal_depth_type;

typedef enum v3d_render_target_clamp
{
	V3D_RENDER_TARGET_CLAMP_NONE = 0,
	V3D_RENDER_TARGET_CLAMP_NORM = 1,
	V3D_RENDER_TARGET_CLAMP_POS = 2,
	V3D_RENDER_TARGET_CLAMP_INT = 3,
} v3d_render_target_clamp;

typedef enum v3d_l2t_flush_mode
{
	L2T_FLUSH_MODE_FLUSH = 0,
	L2T_FLUSH_MODE_CLEAR = 1,
	L2T_FLUSH_MODE_CLEAN = 2,
} v3d_l2t_flush_mode;

typedef enum v3d_output_image_format
{
	V3D_OUTPUT_IMAGE_FORMAT_SRGB8ALPHA8 = 0,
	V3D_OUTPUT_IMAGE_FORMAT_SRGB = 1,
	V3D_OUTPUT_IMAGE_FORMAT_RGB10A2UI = 2,
	V3D_OUTPUT_IMAGE_FORMAT_RGB10A2 = 3,
	V3D_OUTPUT_IMAGE_FORMAT_ABGR1555 = 4,
	V3D_OUTPUT_IMAGE_FORMAT_ALPHA_MASKED_ABGR1555 = 5,
	V3D_OUTPUT_IMAGE_FORMAT_ABGR4444 = 6,
	V3D_OUTPUT_IMAGE_FORMAT_BGR565 = 7,
	V3D_OUTPUT_IMAGE_FORMAT_R11FG11FB10F = 8,
	V3D_OUTPUT_IMAGE_FORMAT_RGBA32F = 9,
	V3D_OUTPUT_IMAGE_FORMAT_RG32F = 10,
	V3D_OUTPUT_IMAGE_FORMAT_R32F = 11,
	V3D_OUTPUT_IMAGE_FORMAT_RGBA32I = 12,
	V3D_OUTPUT_IMAGE_FORMAT_RG32I = 13,
	V3D_OUTPUT_IMAGE_FORMAT_R32I = 14,
	V3D_OUTPUT_IMAGE_FORMAT_RGBA32UI = 15,
	V3D_OUTPUT_IMAGE_FORMAT_RG32UI = 16,
	V3D_OUTPUT_IMAGE_FORMAT_R32UI = 17,
	V3D_OUTPUT_IMAGE_FORMAT_RGBA16F = 18,
	V3D_OUTPUT_IMAGE_FORMAT_RG16F = 19,
	V3D_OUTPUT_IMAGE_FORMAT_R16F = 20,
	V3D_OUTPUT_IMAGE_FORMAT_RGBA16I = 21,
	V3D_OUTPUT_IMAGE_FORMAT_RG16I = 22,
	V3D_OUTPUT_IMAGE_FORMAT_R16I = 23,
	V3D_OUTPUT_IMAGE_FORMAT_RGBA16UI = 24,
	V3D_OUTPUT_IMAGE_FORMAT_RG16UI = 25,
	V3D_OUTPUT_IMAGE_FORMAT_R16UI = 26,
	V3D_OUTPUT_IMAGE_FORMAT_RGBA8 = 27,
	V3D_OUTPUT_IMAGE_FORMAT_RGB8 = 28,
	V3D_OUTPUT_IMAGE_FORMAT_RG8 = 29,
	V3D_OUTPUT_IMAGE_FORMAT_R8 = 30,
	V3D_OUTPUT_IMAGE_FORMAT_RGBA8I = 31,
	V3D_OUTPUT_IMAGE_FORMAT_RG8I = 32,
	V3D_OUTPUT_IMAGE_FORMAT_R8I = 33,
	V3D_OUTPUT_IMAGE_FORMAT_RGBA8UI = 34,
	V3D_OUTPUT_IMAGE_FORMAT_RG8UI = 35,
	V3D_OUTPUT_IMAGE_FORMAT_R8UI = 36,
	V3D_OUTPUT_IMAGE_FORMAT_BSTC8 = 39,
	V3D_OUTPUT_IMAGE_FORMAT_D32F = 40,
	V3D_OUTPUT_IMAGE_FORMAT_D24 = 41,
	V3D_OUTPUT_IMAGE_FORMAT_D16 = 42,
	V3D_OUTPUT_IMAGE_FORMAT_D24S8 = 43,
	V3D_OUTPUT_IMAGE_FORMAT_S8 = 44,
	V3D_OUTPUT_IMAGE_FORMAT_RGBA5551 = 45,
	V3D_OUTPUT_IMAGE_FORMAT_BSTC8SRGB = 46,
	V3D_OUTPUT_IMAGE_FORMAT_BSTC10 = 47,
	V3D_OUTPUT_IMAGE_FORMAT_BSTC10SRGB = 48,
	V3D_OUTPUT_IMAGE_FORMAT_BSTC10PQ = 49,
	V3D_OUTPUT_IMAGE_FORMAT_RGBA10X6 = 50,
	V3D_OUTPUT_IMAGE_FORMAT_BSTC10HLG = 55,
	V3D_OUTPUT_IMAGE_FORMAT_RGBA10X6HLG = 56,
	V3D_OUTPUT_IMAGE_FORMAT_RGB10A2HLG = 57,
	V3D_OUTPUT_IMAGE_FORMAT_BSTC10PQBT1886 = 58,
	V3D_OUTPUT_IMAGE_FORMAT_RGBA10X6PQBT1886 = 59,
	V3D_OUTPUT_IMAGE_FORMAT_RGB10A2PQBT1886 = 60,
	V3D_OUTPUT_IMAGE_FORMAT_BSTC10HLGBT1886 = 61,
	V3D_OUTPUT_IMAGE_FORMAT_RGBA10X6HLGBT1886 = 62,
	V3D_OUTPUT_IMAGE_FORMAT_RGB10A2HLGBT1886 = 63,
} v3d_output_image_format;

typedef enum v3d_dither_mode
{
	V3D_DITHER_MODE_NONE = 0,
	V3D_DITHER_MODE_RGB = 1,
	V3D_DITHER_MODE_A = 2,
	V3D_DITHER_MODE_RGBA = 3,
} v3d_dither_mode;

typedef enum v3d_pack_mode
{
	V3D_PACK_MODE_16_WAY = 0,
	V3D_PACK_MODE_8_WAY = 1,
	V3D_PACK_MODE_4_WAY = 2,
	V3D_PACK_MODE_1_WAY = 3,
} v3d_pack_mode;

typedef enum v3d_tcs_flush_mode
{
	V3D_TCS_FLUSH_MODE_FULLY_PACKED = 0,
	V3D_TCS_FLUSH_MODE_SINGLE_PATCH = 1,
	V3D_TCS_FLUSH_MODE_PACKED_COMPLETE_PATCHES = 2,
} v3d_tcs_flush_mode;

typedef enum v3d_primitive_counters
{
	V3D_PRIM_COUNTS_TFWORDSBUFFER0 = 0,
	V3D_PRIM_COUNTS_TFWORDSBUFFER1 = 1,
	V3D_PRIM_COUNTS_TFWORDSBUFFER2 = 2,
	V3D_PRIM_COUNTS_TFWORDSBUFFER3 = 3,
	V3D_PRIM_COUNTS_WRITTEN = 4,
	V3D_PRIM_COUNTS_TFWRITTEN = 5,
	V3D_PRIM_COUNTS_TFOVERFLOW = 6,
} v3d_primitive_counters;

typedef enum v3d_line_rasterization
{
	V3D_LINE_RASTERIZATION_DIAMOND_EXIT = 0,
	V3D_LINE_RASTERIZATION_PERP_END_CAPS = 1,
} v3d_line_rasterization;

typedef enum v3d_z_clip_mode
{
	V3D_Z_CLIP_MODE_NONE = 0,
	V3D_Z_CLIP_MODE_MINONETOONE = 1,
	V3D_Z_CLIP_MODE_ZEROTOONE = 2,
} v3d_z_clip_mode;

#define v3d_OP_HALT 0
typedef struct PACKED v3d_halt
{
	v3d_uint operation : 8;
} v3d_halt;

#define v3d_OP_NOP 1
typedef struct PACKED v3d_nop
{
	v3d_uint operation : 8;
} v3d_nop;

#define v3d_OP_FLUSH 4
typedef struct PACKED v3d_flush
{
	v3d_uint operation : 8;
} v3d_flush;

#define v3d_OP_FLUSH_ALL_STATE 5
typedef struct PACKED v3d_flush_all_state
{
	v3d_uint operation : 8;
} v3d_flush_all_state;

#define v3d_OP_START_TILE_BINNING 6
typedef struct PACKED v3d_start_tile_binning
{
	v3d_uint operation : 8;
} v3d_start_tile_binning;

#define v3d_OP_INCREMENT_SEMAPHORE 7
typedef struct PACKED v3d_increment_semaphore
{
	v3d_uint operation : 8;
} v3d_increment_semaphore;

#define v3d_OP_WAIT_ON_SEMAPHORE 8
typedef struct PACKED v3d_wait_on_semaphore
{
	v3d_uint operation : 8;
} v3d_wait_on_semaphore;

#define v3d_OP_WAIT_FOR_PREVIOUS_FRAME 9
typedef struct PACKED v3d_wait_for_previous_frame
{
	v3d_uint operation : 8;
} v3d_wait_for_previous_frame;

#define v3d_OP_ENABLE_Z_ONLY_RENDERING 10
typedef struct PACKED v3d_enable_z_only_rendering
{
	v3d_uint operation : 8;
} v3d_enable_z_only_rendering;

#define v3d_OP_DISABLE_Z_ONLY_RENDERING 11
typedef struct PACKED v3d_disable_z_only_rendering
{
	v3d_uint operation : 8;
} v3d_disable_z_only_rendering;

#define v3d_OP_END_OF_Z_ONLY_RENDERING_IN_FRAME 12
typedef struct PACKED v3d_end_of_z_only_rendering_in_frame
{
	v3d_uint operation : 8;
} v3d_end_of_z_only_rendering_in_frame;

#define v3d_OP_END_OF_RENDERING 13
typedef struct PACKED v3d_end_of_rendering
{
	v3d_uint operation : 8;
} v3d_end_of_rendering;

#define v3d_OP_WAIT_FOR_TRANSFORM_FEEDBACK 14
typedef struct PACKED v3d_wait_for_transform_feedback
{
	v3d_uint operation : 8;
	v3d_uint block_count : 8;
} v3d_wait_for_transform_feedback;

#define v3d_OP_BRANCH_TO_AUTO_CHAINED_SUB_LIST 15
typedef struct PACKED v3d_branch_to_auto_chained_sub_list
{
	v3d_uint operation : 8;
	v3d_address address : 32;
} v3d_branch_to_auto_chained_sub_list;

#define v3d_OP_BRANCH 16
typedef struct PACKED v3d_branch
{
	v3d_uint operation : 8;
	v3d_address address : 32;
} v3d_branch;

#define v3d_OP_BRANCH_TO_SUB_LIST 17
typedef struct PACKED v3d_branch_to_sub_list
{
	v3d_uint operation : 8;
	v3d_address address : 32;
} v3d_branch_to_sub_list;

#define v3d_OP_RETURN_FROM_SUB_LIST 18
typedef struct PACKED v3d_return_from_sub_list
{
	v3d_uint operation : 8;
} v3d_return_from_sub_list;

#define v3d_OP_FLUSH_VCD_CACHE 19
typedef struct PACKED v3d_flush_vcd_cache
{
	v3d_uint operation : 8;
} v3d_flush_vcd_cache;

#define v3d_OP_START_ADDRESS_OF_GENERIC_TILE_LIST 20
typedef struct PACKED v3d_start_address_of_generic_tile_list
{
	v3d_uint operation : 8;
	v3d_address start : 32;
	v3d_address end : 32;
} v3d_start_address_of_generic_tile_list;

#define v3d_OP_BRANCH_TO_IMPLICIT_TILE_LIST 21
typedef struct PACKED v3d_branch_to_implicit_tile_list
{
	v3d_uint operation : 8;
	v3d_uint tile_list_set_number : 8;
} v3d_branch_to_implicit_tile_list;

#define v3d_OP_BRANCH_TO_EXPLICIT_SUPERTILE 22
typedef struct PACKED v3d_branch_to_explicit_supertile
{
	v3d_uint operation : 8;
	v3d_uint column_number : 8;
	v3d_uint row_number : 8;
	v3d_uint explicit_supertile_number : 8;
	v3d_address absolute_address_of_explicit_supertile_render_list : 32;
} v3d_branch_to_explicit_supertile;

#define v3d_OP_SUPERTILE_COORDINATES 23
typedef struct PACKED v3d_supertile_coordinates
{
	v3d_uint operation : 8;
	v3d_uint column_number_in_supertiles : 8;
	v3d_uint row_number_in_supertiles : 8;
} v3d_supertile_coordinates;

#define v3d_OP_CLEAR_TILE_BUFFERS 25
typedef struct PACKED v3d_clear_tile_buffers
{
	v3d_uint operation : 8;
	v3d_bool clear_all_render_targets : 1;
	v3d_bool clear_z_stencil_buffer : 1;
} v3d_clear_tile_buffers;

#define v3d_OP_END_OF_LOADS 26
typedef struct PACKED v3d_end_of_loads
{
	v3d_uint operation : 8;
} v3d_end_of_loads;

#define v3d_OP_END_OF_TILE_MARKER 27
typedef struct PACKED v3d_end_of_tile_marker
{
	v3d_uint operation : 8;
} v3d_end_of_tile_marker;

#define v3d_OP_STORE_TILE_BUFFER_GENERAL 29
typedef struct PACKED v3d_store_tile_buffer_general
{
	v3d_uint operation : 8;

#define v3d_RENDER_TARGET_0 0
#define v3d_RENDER_TARGET_1 1
#define v3d_RENDER_TARGET_2 2
#define v3d_RENDER_TARGET_3 3
#define v3d_RENDER_TARGET_4 4
#define v3d_RENDER_TARGET_5 5
#define v3d_RENDER_TARGET_6 6
#define v3d_RENDER_TARGET_7 7
#define v3d_NONE 8
#define v3d_Z 9
#define v3d_STENCIL 10
#define v3d_ZSTENCIL 11
	v3d_uint buffer_to_store : 4;
	v3d_memory_format memory_format : 3;
	v3d_bool flip_y : 1;
	v3d_dither_mode dither_mode : 2;
	v3d_decimate_mode decimate_mode : 2;
	v3d_output_image_format output_image_format : 6;
	v3d_bool clear_buffer_being_stored : 1;
	v3d_bool channel_reverse : 1;
	v3d_bool r_b_swap : 1;
	v3d_u32   _unused21 : 7;
	v3d_uint height_in_ub_or_stride : 20;
	v3d_uint height : 16;
	v3d_address address : 32;
} v3d_store_tile_buffer_general;

#define v3d_OP_LOAD_TILE_BUFFER_GENERAL 30
typedef struct PACKED v3d_load_tile_buffer_general
{
	v3d_uint operation : 8;

#define v3d_RENDER_TARGET_0 0
#define v3d_RENDER_TARGET_1 1
#define v3d_RENDER_TARGET_2 2
#define v3d_RENDER_TARGET_3 3
#define v3d_NONE 8
#define v3d_Z 9
#define v3d_STENCIL 10
#define v3d_ZSTENCIL 11
	v3d_uint buffer_to_load : 4;
	v3d_memory_format memory_format : 3;
	v3d_bool flip_y : 1;
	v3d_u32   _unused8 : 2;
	v3d_decimate_mode decimate_mode : 2;
	v3d_output_image_format input_image_format : 6;
	v3d_bool force_alpha_1 : 1;
	v3d_bool channel_reverse : 1;
	v3d_bool r_b_swap : 1;
	v3d_u32   _unused21 : 7;
	v3d_uint height_in_ub_or_stride : 20;
	v3d_uint height : 16;
	v3d_address address : 32;
} v3d_load_tile_buffer_general;

#define v3d_OP_TRANSFORM_FEEDBACK_FLUSH_AND_COUNT 31
typedef struct PACKED v3d_transform_feedback_flush_and_count
{
	v3d_uint operation : 8;
} v3d_transform_feedback_flush_and_count;

#define v3d_OP_INDEXED_PRIM_LIST 32
typedef struct PACKED v3d_indexed_prim_list
{
	v3d_uint operation : 8;
	v3d_primitive mode : 6;

#define v3d_INDEX_TYPE_8_BIT 0
#define v3d_INDEX_TYPE_16_BIT 1
#define v3d_INDEX_TYPE_32_BIT 2
	v3d_uint index_type : 2;
	v3d_uint length : 31;
	v3d_bool enable_primitive_restarts : 1;
	v3d_uint index_offset : 32;
} v3d_indexed_prim_list;

#define v3d_OP_INDIRECT_INDEXED_INSTANCED_PRIM_LIST 33
typedef struct PACKED v3d_indirect_indexed_instanced_prim_list
{
	v3d_uint operation : 8;
	v3d_primitive mode : 6;

#define v3d_INDEX_TYPE_8_BIT 0
#define v3d_INDEX_TYPE_16_BIT 1
#define v3d_INDEX_TYPE_32_BIT 2
	v3d_uint index_type : 2;
	v3d_uint number_of_draw_indirect_indexed_records : 31;
	v3d_bool enable_primitive_restarts : 1;
	v3d_address address : 32;
	v3d_uint stride_in_multiples_of_4_bytes : 8;
} v3d_indirect_indexed_instanced_prim_list;

#define v3d_OP_INDEXED_INSTANCED_PRIM_LIST 34
typedef struct PACKED v3d_indexed_instanced_prim_list
{
	v3d_uint operation : 8;
	v3d_primitive mode : 6;

#define v3d_INDEX_TYPE_8_BIT 0
#define v3d_INDEX_TYPE_16_BIT 1
#define v3d_INDEX_TYPE_32_BIT 2
	v3d_uint index_type : 2;
	v3d_uint instance_length : 31;
	v3d_bool enable_primitive_restarts : 1;
	v3d_uint number_of_instances : 32;
	v3d_uint index_offset : 32;
} v3d_indexed_instanced_prim_list;

#define v3d_OP_VERTEX_ARRAY_PRIMS 36
typedef struct PACKED v3d_vertex_array_prims
{
	v3d_uint operation : 8;
	v3d_primitive mode : 8;
	v3d_uint length : 32;
	v3d_uint index_of_first_vertex : 32;
} v3d_vertex_array_prims;

#define v3d_OP_INDIRECT_VERTEX_ARRAY_INSTANCED_PRIMS 37
typedef struct PACKED v3d_indirect_vertex_array_instanced_prims
{
	v3d_uint operation : 8;
	v3d_primitive mode : 8;
	v3d_uint number_of_draw_indirect_array_records : 32;
	v3d_address address : 32;
	v3d_uint stride_in_multiples_of_4_bytes : 8;
} v3d_indirect_vertex_array_instanced_prims;

#define v3d_OP_VERTEX_ARRAY_INSTANCED_PRIMS 38
typedef struct PACKED v3d_vertex_array_instanced_prims
{
	v3d_uint operation : 8;
	v3d_primitive mode : 8;
	v3d_uint instance_length : 32;
	v3d_uint number_of_instances : 32;
	v3d_uint index_of_first_vertex : 32;
} v3d_vertex_array_instanced_prims;

#define v3d_OP_VERTEX_ARRAY_SINGLE_INSTANCE_PRIMS 39
typedef struct PACKED v3d_vertex_array_single_instance_prims
{
	v3d_uint operation : 8;
	v3d_primitive mode : 8;
	v3d_uint instance_length : 32;
	v3d_uint instance_id : 32;
	v3d_uint index_of_first_vertex : 32;
} v3d_vertex_array_single_instance_prims;

#define v3d_OP_BASE_VERTEX_BASE_INSTANCE 43
typedef struct PACKED v3d_base_vertex_base_instance
{
	v3d_uint operation : 8;
	v3d_uint base_vertex : 32;
	v3d_uint base_instance : 32;
} v3d_base_vertex_base_instance;

#define v3d_OP_INDEX_BUFFER_SETUP 44
typedef struct PACKED v3d_index_buffer_setup
{
	v3d_uint operation : 8;
	v3d_address address : 32;
	v3d_uint size : 32;
} v3d_index_buffer_setup;

#define v3d_OP_SET_INSTANCEID 54
typedef struct PACKED v3d_set_instanceid
{
	v3d_uint operation : 8;
	v3d_uint instance_id : 32;
} v3d_set_instanceid;

#define v3d_OP_SET_PRIMITIVEID 55
typedef struct PACKED v3d_set_primitiveid
{
	v3d_uint operation : 8;
	v3d_uint primitive_id : 32;
} v3d_set_primitiveid;

#define v3d_OP_PRIM_LIST_FORMAT 56
typedef struct PACKED v3d_prim_list_format
{
	v3d_uint operation : 8;

#define v3d_LIST_POINTS 0
#define v3d_LIST_LINES 1
#define v3d_LIST_TRIANGLES 2
	v3d_uint primitive_type : 6;
	v3d_u32   _unused6 : 1;
	v3d_bool tri_strip_or_fan : 1;
} v3d_prim_list_format;

#define v3d_OP_SERIAL_NUMBER_LIST_START 57
typedef struct PACKED v3d_serial_number_list_start
{
	v3d_uint operation : 8;

#define v3d_BLOCK_SIZE_64B 0
#define v3d_BLOCK_SIZE_128B 1
#define v3d_BLOCK_SIZE_256B 2
	v3d_uint block_size : 2;
	v3d_u32   _unused2 : 4;
	v3d_address address_rshift_6 : 26;
} v3d_serial_number_list_start;

#define v3d_OP_GL_SHADER_STATE 64
typedef struct PACKED v3d_gl_shader_state
{
	v3d_uint operation : 8;
	v3d_uint number_of_attribute_arrays : 5;
	v3d_address address_rshift_5 : 27;
} v3d_gl_shader_state;

#define v3d_OP_GL_SHADER_STATE_INCLUDING_TS 65
typedef struct PACKED v3d_gl_shader_state_including_ts
{
	v3d_uint operation : 8;
	v3d_uint number_of_attribute_arrays : 5;
	v3d_address address_rshift_5 : 27;
} v3d_gl_shader_state_including_ts;

#define v3d_OP_GL_SHADER_STATE_INCLUDING_GS 66
typedef struct PACKED v3d_gl_shader_state_including_gs
{
	v3d_uint operation : 8;
	v3d_uint number_of_attribute_arrays : 5;
	v3d_address address_rshift_5 : 27;
} v3d_gl_shader_state_including_gs;

#define v3d_OP_GL_SHADER_STATE_INCLUDING_TS_GS 67
typedef struct PACKED v3d_gl_shader_state_including_ts_gs
{
	v3d_uint operation : 8;
	v3d_uint number_of_attribute_arrays : 5;
	v3d_address address_rshift_5 : 27;
} v3d_gl_shader_state_including_ts_gs;

#define v3d_OP_VCM_CACHE_SIZE 71
typedef struct PACKED v3d_vcm_cache_size
{
	v3d_uint operation : 8;
	v3d_uint number_of_16_vertex_batches_for_binning : 4;
	v3d_uint number_of_16_vertex_batches_for_rendering : 4;
} v3d_vcm_cache_size;

#define v3d_OP_PRIMITIVE_COUNTS_FEEDBACK 72
typedef struct PACKED v3d_primitive_counts_feedback
{
	v3d_uint operation : 8;

#define v3d_STORE_PRIMITIVE_COUNTS 0
#define v3d_STORE_PRIMITIVE_COUNTS_AND_ZERO 1
#define v3d_STORE_BUFFER_STATE 2
#define v3d_STORE_BUFFER_STATE_CL 3
#define v3d_LOAD_BUFFER_STATE 8
	v3d_uint op : 4;
	v3d_bool read_write_64byte : 1;
	v3d_address address_rshift_5 : 27;
} v3d_primitive_counts_feedback;

#define v3d_OP_TRANSFORM_FEEDBACK_BUFFER 73
typedef struct PACKED v3d_transform_feedback_buffer
{
	v3d_uint operation : 8;
	v3d_uint buffer_number : 2;
	v3d_uint buffer_size_in_32_bit_words : 30;
	v3d_address buffer_address : 32;
} v3d_transform_feedback_buffer;

#define v3d_OP_TRANSFORM_FEEDBACK_SPECS 74
typedef struct PACKED v3d_transform_feedback_specs
{
	v3d_uint operation : 8;
	v3d_uint number_of_16_bit_output_data_specs_following : 5;
	v3d_u32   _unused5 : 2;
	v3d_bool enable : 1;
} v3d_transform_feedback_specs;

#define v3d_OP_FLUSH_TRANSFORM_FEEDBACK_DATA 75
typedef struct PACKED v3d_flush_transform_feedback_data
{
	v3d_uint operation : 8;
} v3d_flush_transform_feedback_data;

#define v3d_OP_L1_CACHE_FLUSH_CONTROL 76
typedef struct PACKED v3d_l1_cache_flush_control
{
	v3d_uint operation : 8;
	v3d_uint instruction_cache_clear : 4;
	v3d_uint uniforms_cache_clear : 4;
	v3d_uint tmu_data_cache_clear : 4;
	v3d_uint tmu_config_cache_clear : 4;
} v3d_l1_cache_flush_control;

#define v3d_OP_L2T_CACHE_FLUSH_CONTROL 77
typedef struct PACKED v3d_l2t_cache_flush_control
{
	v3d_uint operation : 8;
	v3d_address l2t_flush_start : 32;
	v3d_address l2t_flush_end : 32;
	v3d_l2t_flush_mode l2t_flush_mode : 4;
} v3d_l2t_cache_flush_control;

typedef struct PACKED v3d_transform_feedback_output_data_spec
{
	v3d_uint first_shaded_vertex_value_to_output : 8;
	v3d_uint number_of_consecutive_vertex_values_to_output_as_32_bit_values_minus_one : 4;
	v3d_uint output_buffer_to_write_to : 2;
	v3d_uint stream_number : 2;
} v3d_transform_feedback_output_data_spec;

typedef struct PACKED v3d_transform_feedback_output_address
{
	v3d_address address : 32;
} v3d_transform_feedback_output_address;

#define v3d_OP_STENCIL_CFG 80
typedef struct PACKED v3d_stencil_cfg
{
	v3d_uint operation : 8;
	v3d_uint stencil_ref_value : 8;
	v3d_uint stencil_test_mask : 8;
	v3d_compare_function stencil_test_function : 3;
	v3d_stencil_op stencil_test_fail_op : 3;
	v3d_stencil_op depth_test_fail_op : 3;
	v3d_stencil_op stencil_pass_op : 3;
	v3d_bool front_config : 1;
	v3d_bool back_config : 1;
	v3d_u32   _unused30 : 2;
	v3d_uint stencil_write_mask : 8;
} v3d_stencil_cfg;

#define v3d_OP_BLEND_ENABLES 83
typedef struct PACKED v3d_blend_enables
{
	v3d_uint operation : 8;
	v3d_uint mask : 8;
} v3d_blend_enables;

#define v3d_OP_BLEND_CFG 84
typedef struct PACKED v3d_blend_cfg
{
	v3d_uint operation : 8;
	v3d_blend_mode alpha_blend_mode : 4;
	v3d_blend_factor alpha_blend_src_factor : 4;
	v3d_blend_factor alpha_blend_dst_factor : 4;
	v3d_blend_mode color_blend_mode : 4;
	v3d_blend_factor color_blend_src_factor : 4;
	v3d_blend_factor color_blend_dst_factor : 4;
	v3d_uint render_target_mask : 4;
} v3d_blend_cfg;

#define v3d_OP_BLEND_CONSTANT_COLOR 86
typedef struct PACKED v3d_blend_constant_color
{
	v3d_uint operation : 8;
	v3d_uint red_f16 : 16;
	v3d_uint green_f16 : 16;
	v3d_uint blue_f16 : 16;
	v3d_uint alpha_f16 : 16;
} v3d_blend_constant_color;

#define v3d_OP_COLOR_WRITE_MASKS 87
typedef struct PACKED v3d_color_write_masks
{
	v3d_uint operation : 8;
	v3d_uint mask : 32;
} v3d_color_write_masks;

#define v3d_OP_ZERO_ALL_CENTROID_FLAGS 88
typedef struct PACKED v3d_zero_all_centroid_flags
{
	v3d_uint operation : 8;
} v3d_zero_all_centroid_flags;

#define v3d_OP_CENTROID_FLAGS 89
typedef struct PACKED v3d_centroid_flags
{
	v3d_uint operation : 8;
	v3d_uint varying_offset_v0 : 4;
	v3d_varying_flags_action action_for_centroid_flags_of_lower_numbered_varyings : 2;
	v3d_varying_flags_action action_for_centroid_flags_of_higher_numbered_varyings : 2;
	v3d_uint centroid_flags_for_varyings_v024 : 24;
} v3d_centroid_flags;

#define v3d_OP_SAMPLE_STATE 91
typedef struct PACKED v3d_sample_state
{
	v3d_uint operation : 8;
	v3d_uint mask : 4;
	v3d_u32   _unused4 : 12;
	v3d_f187 coverage : 16;
} v3d_sample_state;

#define v3d_OP_OCCLUSION_QUERY_COUNTER 92
typedef struct PACKED v3d_occlusion_query_counter
{
	v3d_uint operation : 8;
	v3d_address address : 32;
} v3d_occlusion_query_counter;

#define v3d_OP_CFG_BITS 96
typedef struct PACKED v3d_cfg_bits
{
	v3d_uint operation : 8;
	v3d_bool enable_forward_facing_primitive : 1;
	v3d_bool enable_reverse_facing_primitive : 1;
	v3d_bool clockwise_primitives : 1;
	v3d_bool enable_depth_offset : 1;
	v3d_line_rasterization line_rasterization : 2;
	v3d_uint rasterizer_oversample_mode : 2;
	v3d_u32   _unused8 : 3;
	v3d_bool direct3d_wireframe_triangles_mode : 1;
	v3d_compare_function depth_test_function : 3;
	v3d_bool z_updates_enable : 1;
	v3d_bool early_z_enable : 1;
	v3d_bool early_z_updates_enable : 1;
	v3d_bool stencil_enable : 1;
	v3d_bool blend_enable : 1;
	v3d_bool direct3d_point_fill_mode : 1;
	v3d_bool direct3d_provoking_vertex : 1;
} v3d_cfg_bits;

#define v3d_OP_ZERO_ALL_FLAT_SHADE_FLAGS 97
typedef struct PACKED v3d_zero_all_flat_shade_flags
{
	v3d_uint operation : 8;
} v3d_zero_all_flat_shade_flags;

#define v3d_OP_FLAT_SHADE_FLAGS 98
typedef struct PACKED v3d_flat_shade_flags
{
	v3d_uint operation : 8;
	v3d_uint varying_offset_v0 : 4;
	v3d_varying_flags_action action_for_flat_shade_flags_of_lower_numbered_varyings : 2;
	v3d_varying_flags_action action_for_flat_shade_flags_of_higher_numbered_varyings : 2;
	v3d_uint flat_shade_flags_for_varyings_v024 : 24;
} v3d_flat_shade_flags;

#define v3d_OP_ZERO_ALL_NON_PERSPECTIVE_FLAGS 99
typedef struct PACKED v3d_zero_all_non_perspective_flags
{
	v3d_uint operation : 8;
} v3d_zero_all_non_perspective_flags;

#define v3d_OP_NON_PERSPECTIVE_FLAGS 100
typedef struct PACKED v3d_non_perspective_flags
{
	v3d_uint operation : 8;
	v3d_uint varying_offset_v0 : 4;
	v3d_varying_flags_action action_for_non_perspective_flags_of_lower_numbered_varyings : 2;
	v3d_varying_flags_action action_for_non_perspective_flags_of_higher_numbered_varyings : 2;
	v3d_uint non_perspective_flags_for_varyings_v024 : 24;
} v3d_non_perspective_flags;

#define v3d_OP_POINT_SIZE 104
typedef struct PACKED v3d_point_size
{
	v3d_uint operation : 8;
	v3d_float point_size;
} v3d_point_size;

#define v3d_OP_LINE_WIDTH 105
typedef struct PACKED v3d_line_width
{
	v3d_uint operation : 8;
	v3d_float line_width;
} v3d_line_width;

#define v3d_OP_DEPTH_OFFSET 106
typedef struct PACKED v3d_depth_offset
{
	v3d_uint operation : 8;
	v3d_f187 depth_offset_factor : 16;
	v3d_f187 depth_offset_units : 16;
	v3d_float limit;
} v3d_depth_offset;

#define v3d_OP_CLIPWINDOW 107
typedef struct PACKED v3d_clipwindow
{
	v3d_uint operation : 8;
	v3d_uint clip_window_left_pixel_coordinate : 16;
	v3d_uint clip_window_bottom_pixel_coordinate : 16;
	v3d_uint clip_window_width_in_pixels : 16;
	v3d_uint clip_window_height_in_pixels : 16;
} v3d_clipwindow;

#define v3d_OP_VIEWPORT_OFFSET 108
typedef struct PACKED v3d_viewport_offset
{
	v3d_uint operation : 8;
	v3d_u14_8 fine_x : 22;
	v3d_int coarse_x : 10;
	v3d_u14_8 fine_y : 22;
	v3d_int coarse_y : 10;
} v3d_viewport_offset;

#define v3d_OP_CLIPPER_Z_MIN_MAX_CLIPPING_PLANES 109
typedef struct PACKED v3d_clipper_z_min_max_clipping_planes
{
	v3d_uint operation : 8;
	v3d_float minimum_zw;
	v3d_float maximum_zw;
} v3d_clipper_z_min_max_clipping_planes;

#define v3d_OP_CLIPPER_XY_SCALING 110
typedef struct PACKED v3d_clipper_xy_scaling
{
	v3d_uint operation : 8;
	v3d_float viewport_half_width_in_1_256th_of_pixel;
	v3d_float viewport_half_height_in_1_256th_of_pixel;
} v3d_clipper_xy_scaling;

#define v3d_OP_CLIPPER_Z_SCALE_AND_OFFSET 111
typedef struct PACKED v3d_clipper_z_scale_and_offset
{
	v3d_uint operation : 8;
	v3d_float viewport_z_scale_zc_to_zs;
	v3d_float viewport_z_offset_zc_to_zs;
} v3d_clipper_z_scale_and_offset;

#define v3d_OP_NUMBER_OF_LAYERS 119
typedef struct PACKED v3d_number_of_layers
{
	v3d_uint operation : 8;
	v3d_uint number_of_layers_minus_one : 8;
} v3d_number_of_layers;

#define v3d_OP_TILE_BINNING_MODE_CFG 120
typedef struct PACKED v3d_tile_binning_mode_cfg
{
	v3d_uint operation : 8;
	v3d_u32   _unused0 : 2;

#define v3d_TILE_ALLOCATION_INITIAL_BLOCK_SIZE_64B 0
#define v3d_TILE_ALLOCATION_INITIAL_BLOCK_SIZE_128B 1
#define v3d_TILE_ALLOCATION_INITIAL_BLOCK_SIZE_256B 2
	v3d_uint tile_allocation_initial_block_size : 2;

#define v3d_TILE_ALLOCATION_BLOCK_SIZE_64B 0
#define v3d_TILE_ALLOCATION_BLOCK_SIZE_128B 1
#define v3d_TILE_ALLOCATION_BLOCK_SIZE_256B 2
	v3d_uint tile_allocation_block_size : 2;
	v3d_u32   _unused6 : 2;
	v3d_uint number_of_render_targets_minus_one : 4;
	v3d_internal_bpp maximum_bpp_of_all_render_targets : 2;
	v3d_bool multisample_mode_4x : 1;
	v3d_bool double_buffer_in_non_ms_mode : 1;
	v3d_u32   _unused16 : 16;
	v3d_uint width_in_pixels_minus_one : 16;
	v3d_uint height_in_pixels_minus_one : 16;
} v3d_tile_binning_mode_cfg;

#define v3d_OP_TILE_RENDERING_MODE_CFG_COMMON 121
typedef struct PACKED v3d_tile_rendering_mode_cfg_common
{
	v3d_uint operation : 8;
	v3d_uint sub_id : 4;
	v3d_uint number_of_render_targets_minus_one : 4;
	v3d_uint image_width_pixels : 16;
	v3d_uint image_height_pixels : 16;

#define v3d_RENDER_TARGET_MAXIMUM_32BPP 0
#define v3d_RENDER_TARGET_MAXIMUM_64BPP 1
#define v3d_RENDER_TARGET_MAXIMUM_128BPP 2
	v3d_internal_bpp maximum_bpp_of_all_render_targets : 2;
	v3d_bool multisample_mode_4x : 1;
	v3d_bool double_buffer_in_non_ms_mode : 1;
	v3d_u32   _unused44 : 1;

#define v3d_EARLY_Z_DIRECTION_LT_LE 0
#define v3d_EARLY_Z_DIRECTION_GT_GE 1
	v3d_uint early_z_test_and_update_direction : 1;
	v3d_bool early_z_disable : 1;
	v3d_internal_depth_type internal_depth_type : 4;
	v3d_bool early_depth_stencil_clear : 1;
	v3d_uint pad : 12;
} v3d_tile_rendering_mode_cfg_common;

#define v3d_OP_TILE_RENDERING_MODE_CFG_COLOR 121
typedef struct PACKED v3d_tile_rendering_mode_cfg_color
{
	v3d_uint operation : 8;
	v3d_uint sub_id : 4;
	v3d_internal_bpp render_target_0_internal_bpp : 2;
	v3d_internal_type render_target_0_internal_type : 4;
	v3d_render_target_clamp render_target_0_clamp : 2;
	v3d_internal_bpp render_target_1_internal_bpp : 2;
	v3d_internal_type render_target_1_internal_type : 4;
	v3d_render_target_clamp render_target_1_clamp : 2;
	v3d_internal_bpp render_target_2_internal_bpp : 2;
	v3d_internal_type render_target_2_internal_type : 4;
	v3d_render_target_clamp render_target_2_clamp : 2;
	v3d_internal_bpp render_target_3_internal_bpp : 2;
	v3d_internal_type render_target_3_internal_type : 4;
	v3d_render_target_clamp render_target_3_clamp : 2;
	v3d_uint pad : 28;
} v3d_tile_rendering_mode_cfg_color;

#define v3d_OP_TILE_RENDERING_MODE_CFG_ZS_CLEAR_VALUES 121
typedef struct PACKED v3d_tile_rendering_mode_cfg_zs_clear_values
{
	v3d_uint operation : 8;
	v3d_uint sub_id : 4;
	v3d_u32   _unused4 : 4;
	v3d_uint stencil_clear_value : 8;
	v3d_float z_clear_value;
	v3d_uint unused : 16;
} v3d_tile_rendering_mode_cfg_zs_clear_values;

#define v3d_OP_TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART1 121
typedef struct PACKED v3d_tile_rendering_mode_cfg_clear_colors_part1
{
	v3d_uint operation : 8;
	v3d_uint sub_id : 4;
	v3d_uint render_target_number : 4;
	v3d_uint clear_color_low_32_bits : 32;
	v3d_uint clear_color_next_24_bits : 24;
} v3d_tile_rendering_mode_cfg_clear_colors_part1;

#define v3d_OP_TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART2 121
typedef struct PACKED v3d_tile_rendering_mode_cfg_clear_colors_part2
{
	v3d_uint operation : 8;
	v3d_uint sub_id : 4;
	v3d_uint render_target_number : 4;
	v3d_uint clear_color_mid_low_32_bits : 32;
	v3d_uint clear_color_mid_high_24_bits : 24;
} v3d_tile_rendering_mode_cfg_clear_colors_part2;

#define v3d_OP_TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART3 121
typedef struct PACKED v3d_tile_rendering_mode_cfg_clear_colors_part3
{
	v3d_uint operation : 8;
	v3d_uint sub_id : 4;
	v3d_uint render_target_number : 4;
	v3d_uint clear_color_high_16_bits : 16;
	v3d_uint raster_row_stride_or_image_height_in_pixels : 16;
	v3d_uint uif_padded_height_in_uif_blocks : 13;
	v3d_uint pad : 11;
} v3d_tile_rendering_mode_cfg_clear_colors_part3;

#define v3d_OP_TILE_COORDINATES 124
typedef struct PACKED v3d_tile_coordinates
{
	v3d_uint operation : 8;
	v3d_uint tile_column_number : 12;
	v3d_uint tile_row_number : 12;
} v3d_tile_coordinates;

#define v3d_OP_MULTICORE_RENDERING_SUPERTILE_CFG 122
typedef struct PACKED v3d_multicore_rendering_supertile_cfg
{
	v3d_uint operation : 8;
	v3d_uint supertile_width_in_tiles_minus_one : 8;
	v3d_uint supertile_height_in_tiles_minus_one : 8;
	v3d_uint total_frame_width_in_supertiles : 8;
	v3d_uint total_frame_height_in_supertiles : 8;
	v3d_uint total_frame_width_in_tiles : 12;
	v3d_uint total_frame_height_in_tiles : 12;
	v3d_bool multicore_enable : 1;
	v3d_u32   _unused57 : 3;
	v3d_bool supertile_raster_order : 1;
	v3d_uint number_of_bin_tile_lists_minus_one : 3;
} v3d_multicore_rendering_supertile_cfg;

#define v3d_OP_MULTICORE_RENDERING_TILE_LIST_SET_BASE 123
typedef struct PACKED v3d_multicore_rendering_tile_list_set_base
{
	v3d_uint operation : 8;
	v3d_uint tile_list_set_number : 4;
	v3d_u32   _unused4 : 2;
	v3d_address address_rshift_6 : 26;
} v3d_multicore_rendering_tile_list_set_base;

#define v3d_OP_TILE_COORDINATES_IMPLICIT 125
typedef struct PACKED v3d_tile_coordinates_implicit
{
	v3d_uint operation : 8;
} v3d_tile_coordinates_implicit;

#define v3d_OP_TILE_LIST_INITIAL_BLOCK_SIZE 126
typedef struct PACKED v3d_tile_list_initial_block_size
{
	v3d_uint operation : 8;

#define v3d_TILE_ALLOCATION_BLOCK_SIZE_64B 0
#define v3d_TILE_ALLOCATION_BLOCK_SIZE_128B 1
#define v3d_TILE_ALLOCATION_BLOCK_SIZE_256B 2
	v3d_uint size_of_first_block_in_chained_tile_lists : 2;
	v3d_bool use_auto_chained_tile_lists : 1;
} v3d_tile_list_initial_block_size;

typedef struct PACKED v3d_gl_shader_state_record
{
	v3d_bool point_size_in_shaded_vertex_data : 1;
	v3d_bool enable_clipping : 1;
	v3d_bool vertex_id_read_by_coordinate_shader : 1;
	v3d_bool instance_id_read_by_coordinate_shader : 1;
	v3d_bool base_instance_id_read_by_coordinate_shader : 1;
	v3d_bool vertex_id_read_by_vertex_shader : 1;
	v3d_bool instance_id_read_by_vertex_shader : 1;
	v3d_bool base_instance_id_read_by_vertex_shader : 1;
	v3d_bool fragment_shader_does_z_writes : 1;
	v3d_bool turn_off_early_z_test : 1;
	v3d_bool coordinate_shader_has_separate_input_and_output_vpm_blocks : 1;
	v3d_bool vertex_shader_has_separate_input_and_output_vpm_blocks : 1;
	v3d_bool fragment_shader_uses_real_pixel_centre_w_in_addition_to_centroid_w2 : 1;
	v3d_bool enable_sample_rate_shading : 1;
	v3d_bool any_shader_reads_hardware_written_primitive_id : 1;
	v3d_bool insert_primitive_id_as_first_varying_to_fragment_shader : 1;
	v3d_bool turn_off_scoreboard : 1;
	v3d_bool do_scoreboard_wait_on_first_thread_switch : 1;
	v3d_bool disable_implicit_point_line_varyings : 1;
	v3d_bool no_prim_pack : 1;
	v3d_u32   _unused20 : 4;
	v3d_uint number_of_varyings_in_fragment_shader : 8;
	v3d_uint coordinate_shader_output_vpm_segment_size : 4;
	v3d_uint min_coord_shader_output_segments_required_in_play_in_addition_to_vcm_cache_size : 4;
	v3d_uint coordinate_shader_input_vpm_segment_size : 4;
	v3d_uint min_coord_shader_input_segments_required_in_play_minus_one : 4;
	v3d_uint vertex_shader_output_vpm_segment_size : 4;
	v3d_uint min_vertex_shader_output_segments_required_in_play_in_addition_to_vcm_cache_size : 4;
	v3d_uint vertex_shader_input_vpm_segment_size : 4;
	v3d_uint min_vertex_shader_input_segments_required_in_play_minus_one : 4;
	v3d_address address_of_default_attribute_values : 32;
	v3d_bool fragment_shader_4_way_threadable : 1;
	v3d_bool fragment_shader_start_in_final_thread_section : 1;
	v3d_bool fragment_shader_propagate_nans : 1;
	v3d_address fragment_shader_code_address_rshift_3 : 29;
	v3d_address fragment_shader_uniforms_address : 32;
	v3d_bool vertex_shader_4_way_threadable : 1;
	v3d_bool vertex_shader_start_in_final_thread_section : 1;
	v3d_bool vertex_shader_propagate_nans : 1;
	v3d_address vertex_shader_code_address_rshift_3 : 29;
	v3d_address vertex_shader_uniforms_address : 32;
	v3d_bool coordinate_shader_4_way_threadable : 1;
	v3d_bool coordinate_shader_start_in_final_thread_section : 1;
	v3d_bool coordinate_shader_propagate_nans : 1;
	v3d_address coordinate_shader_code_address_rshift_3 : 29;
	v3d_address coordinate_shader_uniforms_address : 32;
} v3d_gl_shader_state_record;

typedef struct PACKED v3d_geometry_shader_state_record
{
	v3d_bool geometry_bin_mode_shader_4_way_threadable : 1;
	v3d_bool geometry_bin_mode_shader_start_in_final_thread_section : 1;
	v3d_bool geometry_bin_mode_shader_propagate_nans : 1;
	v3d_address geometry_bin_mode_shader_code_address_rshift_3 : 29;
	v3d_address geometry_bin_mode_shader_uniforms_address : 32;
	v3d_bool geometry_render_mode_shader_4_way_threadable : 1;
	v3d_bool geometry_render_mode_shader_start_in_final_thread_section : 1;
	v3d_bool geometry_render_mode_shader_propagate_nans : 1;
	v3d_address geometry_render_mode_shader_code_address_rshift_3 : 29;
	v3d_address geometry_render_mode_shader_uniforms_address : 32;
} v3d_geometry_shader_state_record;

typedef struct PACKED v3d_tessellation_shader_state_record
{
	v3d_bool tessellation_bin_mode_control_shader_4_way_threadable : 1;
	v3d_bool tessellation_bin_mode_control_shader_start_in_final_thread_section : 1;
	v3d_bool tessellation_bin_mode_control_shader_propagate_nans : 1;
	v3d_address tessellation_bin_mode_control_shader_code_address_rshift_3 : 29;
	v3d_address tessellation_bin_mode_control_shader_uniforms_address : 32;
	v3d_bool tessellation_render_mode_control_shader_4_way_threadable : 1;
	v3d_bool tessellation_render_mode_control_shader_start_in_final_thread_section : 1;
	v3d_bool tessellation_render_mode_control_shader_propagate_nans : 1;
	v3d_address tessellation_render_mode_control_shader_code_address_rshift_3 : 29;
	v3d_address tessellation_render_mode_control_shader_uniforms_address : 32;
	v3d_bool tessellation_bin_mode_evaluation_shader_4_way_threadable : 1;
	v3d_bool tessellation_bin_mode_evaluation_shader_start_in_final_thread_section : 1;
	v3d_bool tessellation_bin_mode_evaluation_shader_propagate_nans : 1;
	v3d_address tessellation_bin_mode_evaluation_shader_code_address_rshift_3 : 29;
	v3d_address tessellation_bin_mode_evaluation_shader_uniforms_address : 32;
	v3d_bool tessellation_render_mode_evaluation_shader_4_way_threadable : 1;
	v3d_bool tessellation_render_mode_evaluation_shader_start_in_final_thread_section : 1;
	v3d_bool tessellation_render_mode_evaluation_shader_propagate_nans : 1;
	v3d_address tessellation_render_mode_evaluation_shader_code_address_rshift_3 : 29;
	v3d_address tessellation_render_mode_evaluation_shader_uniforms_address : 32;
} v3d_tessellation_shader_state_record;

typedef struct PACKED v3d_tessellation_geometry_common_params
{
	v3d_u32   _unused0 : 1;

#define v3d_TESSELLATION_TYPE_TRIANGLE 0
#define v3d_TESSELLATION_TYPE_QUADS 1
#define v3d_TESSELLATION_TYPE_ISOLINES 2
	v3d_uint tessellation_type : 2;
	v3d_bool tessellation_point_mode : 1;

#define v3d_TESSELLATION_EDGE_SPACING_EVEN 0
#define v3d_TESSELLATION_EDGE_SPACING_FRACTIONAL_EVEN 1
#define v3d_TESSELLATION_EDGE_SPACING_FRACTIONAL_ODD 2
	v3d_uint tessellation_edge_spacing : 2;
	v3d_bool tessellation_clockwise : 1;
	v3d_u32   _unused7 : 5;
	v3d_uint tessellation_invocations : 5;

#define v3d_GEOMETRY_SHADER_POINTS 0
#define v3d_GEOMETRY_SHADER_LINE_STRIP 1
#define v3d_GEOMETRY_SHADER_TRI_STRIP 2
	v3d_uint geometry_shader_output_format : 2;
	v3d_uint geometry_shader_instances : 5;
	v3d_uint reserved : 8;
} v3d_tessellation_geometry_common_params;

typedef struct PACKED v3d_tessellation_geometry_shader_params
{
	v3d_tcs_flush_mode tcs_batch_flush_mode : 2;
	v3d_uint per_patch_data_column_depth : 4;
	v3d_u32   _unused6 : 2;
	v3d_uint tcs_output_segment_size_in_sectors : 6;
	v3d_pack_mode tcs_output_segment_pack_mode : 2;
	v3d_uint tes_output_segment_size_in_sectors : 6;
	v3d_pack_mode tes_output_segment_pack_mode : 2;
	v3d_uint gs_output_segment_size_in_sectors : 6;
	v3d_pack_mode gs_output_segment_pack_mode : 2;
	v3d_uint tbg_max_patches_per_tcs_batch_minus_one : 4;
	v3d_uint tbg_max_extra_vertex_segs_for_patches_after_first : 2;
	v3d_uint tbg_min_tcs_output_segments_required_in_play_minus_one : 2;
	v3d_uint tbg_min_per_patch_data_segments_required_in_play_minus_one : 3;
	v3d_u32   _unused43 : 2;
	v3d_uint tpg_max_patches_per_tes_batch_minus_one : 4;
	v3d_uint tpg_max_vertex_segments_per_tes_batch : 2;
	v3d_uint tpg_max_tcs_output_segments_per_tes_batch_minus_one : 3;
	v3d_uint tpg_min_tes_output_segments_required_in_play_minus_one : 3;
	v3d_uint gbg_max_tes_output_vertex_segments_per_gs_batch : 2;
	v3d_uint gbg_min_gs_output_segments_required_in_play_minus_one : 3;
} v3d_tessellation_geometry_shader_params;

typedef struct PACKED v3d_gl_shader_state_attribute_record
{
	v3d_address address : 32;

#define v3d_VEC_4 0
#define v3d_VEC_1 1
#define v3d_VEC_2 2
#define v3d_VEC_3 3
	v3d_uint vec_size : 2;

#define v3d_ATTRIBUTE_HALF_FLOAT 1
#define v3d_ATTRIBUTE_FLOAT 2
#define v3d_ATTRIBUTE_FIXED 3
#define v3d_ATTRIBUTE_BYTE 4
#define v3d_ATTRIBUTE_SHORT 5
#define v3d_ATTRIBUTE_INT 6
#define v3d_ATTRIBUTE_INT2101010 7
	v3d_uint type : 3;
	v3d_bool signed_int_type : 1;
	v3d_bool normalized_int_type : 1;
	v3d_bool read_as_int_uint : 1;
	v3d_uint number_of_values_read_by_coordinate_shader : 4;
	v3d_uint number_of_values_read_by_vertex_shader : 4;
	v3d_uint instance_divisor : 16;
	v3d_uint stride : 32;
	v3d_uint maximum_index : 32;
} v3d_gl_shader_state_attribute_record;

typedef struct PACKED v3d_vpm_generic_block_write_setup
{
	v3d_uint addr : 13;

#define v3d_VPM_SETUP_SIZE_8_BIT 0
#define v3d_VPM_SETUP_SIZE_16_BIT 1
#define v3d_VPM_SETUP_SIZE_32_BIT 2
	v3d_uint size : 2;
	v3d_int stride : 7;
	v3d_bool segs : 1;
	v3d_bool laned : 1;
	v3d_bool horiz : 1;
	v3d_u32   _unused25 : 2;
	v3d_uint id0 : 3;
	v3d_uint id : 2;
} v3d_vpm_generic_block_write_setup;

typedef struct PACKED v3d_vpm_generic_block_read_setup
{
	v3d_uint addr : 13;

#define v3d_VPM_SETUP_SIZE_8_BIT 0
#define v3d_VPM_SETUP_SIZE_16_BIT 1
#define v3d_VPM_SETUP_SIZE_32_BIT 2
	v3d_uint size : 2;
	v3d_int stride : 7;
	v3d_uint num : 5;
	v3d_bool segs : 1;
	v3d_bool laned : 1;
	v3d_bool horiz : 1;
	v3d_uint id : 2;
} v3d_vpm_generic_block_read_setup;

typedef struct PACKED v3d_tmu_config_parameter_0
{
	v3d_uint return_words_of_texture_data : 4;
	v3d_address texture_state_address_rshift_4 : 28;
} v3d_tmu_config_parameter_0;

typedef struct PACKED v3d_tmu_config_parameter_1
{
	v3d_bool output_type_32_bit : 1;
	v3d_bool unnormalized_coordinates : 1;
	v3d_bool per_pixel_mask_enable : 1;
	v3d_address sampler_state_address_rshift_3 : 29;
} v3d_tmu_config_parameter_1;

typedef struct PACKED v3d_tmu_config_parameter_2
{
	v3d_bool offset_format_8 : 1;
	v3d_bool disable_autolod : 1;
	v3d_uint sample_number : 2;
	v3d_bool coefficient_mode : 1;
	v3d_uint gather_component : 2;
	v3d_bool gather_mode : 1;
	v3d_int offset_s : 4;
	v3d_int offset_t : 4;
	v3d_int offset_r : 4;
	v3d_tmu_op op : 4;
	v3d_bool lod_query : 1;
	v3d_uint pad : 7;
} v3d_tmu_config_parameter_2;

typedef struct PACKED v3d_texture_shader_state
{
	v3d_bool flip_texture_x_axis : 1;
	v3d_bool flip_texture_y_axis : 1;
	v3d_bool flip_s_and_t_on_incoming_request : 1;
	v3d_bool srgb : 1;
	v3d_bool ahdr : 1;
	v3d_bool reverse_standard_border_color : 1;
	v3d_address texture_base_pointer_rshift_6 : 26;
	v3d_uint array_stride_64_byte_aligned : 26;
	v3d_uint image_width : 14;
	v3d_uint image_height : 14;
	v3d_uint image_depth : 14;
	v3d_uint texture_type : 7;
	v3d_bool extended : 1;
	v3d_uint swizzle_r : 3;
	v3d_uint swizzle_g : 3;
	v3d_uint swizzle_b : 3;

#define v3d_SWIZZLE_ZERO 0
#define v3d_SWIZZLE_ONE 1
#define v3d_SWIZZLE_RED 2
#define v3d_SWIZZLE_GREEN 3
#define v3d_SWIZZLE_BLUE 4
#define v3d_SWIZZLE_ALPHA 5
	v3d_uint swizzle_a : 3;
	v3d_uint max_level : 4;
	v3d_uint base_level : 4;
	v3d_uint level_0_ubpad : 4;
	v3d_bool level_0_xor_enable : 1;
	v3d_u32   _unused133 : 1;
	v3d_bool level_0_is_strictly_uif : 1;
	v3d_bool uif_xor_disable : 1;
	v3d_uint pad : 56;
} v3d_texture_shader_state;

typedef struct PACKED v3d_sampler_state
{
	v3d_bool mag_filter_nearest : 1;
	v3d_bool min_filter_nearest : 1;
	v3d_bool mip_filter_nearest : 1;
	v3d_bool anisotropy_enable : 1;
	v3d_compare_function depth_compare_function : 3;
	v3d_bool srgb_disable : 1;
	v3d_u4_8 min_level_of_detail : 12;
	v3d_u4_8 max_level_of_detail : 12;
	v3d_s8_8 fixed_bias : 16;
	v3d_wrap_mode wrap_s : 3;
	v3d_wrap_mode wrap_t : 3;
	v3d_wrap_mode wrap_r : 3;
	v3d_bool wrap_i_border : 1;
	v3d_border_color_mode border_color_mode : 3;
	v3d_uint maximum_anisotropy : 2;
	v3d_u32   _unused63 : 1;
	v3d_uint border_color_word_0 : 32;
	v3d_uint border_color_word_1 : 32;
	v3d_uint border_color_word_2 : 32;
	v3d_uint border_color_word_3 : 32;
} v3d_sampler_state;

typedef enum v3d_texture_data_formats
{
	V3D_TEXTURE_DATA_FORMAT_R8 = 0,
	V3D_TEXTURE_DATA_FORMAT_R8_SNORM = 1,
	V3D_TEXTURE_DATA_FORMAT_RG8 = 2,
	V3D_TEXTURE_DATA_FORMAT_RG8_SNORM = 3,
	V3D_TEXTURE_DATA_FORMAT_RGBA8 = 4,
	V3D_TEXTURE_DATA_FORMAT_RGBA8_SNORM = 5,
	V3D_TEXTURE_DATA_FORMAT_RGB565 = 6,
	V3D_TEXTURE_DATA_FORMAT_RGBA4 = 7,
	V3D_TEXTURE_DATA_FORMAT_RGB5A1 = 8,
	V3D_TEXTURE_DATA_FORMAT_RGB10A2 = 9,
	V3D_TEXTURE_DATA_FORMAT_R16 = 10,
	V3D_TEXTURE_DATA_FORMAT_R16_SNORM = 11,
	V3D_TEXTURE_DATA_FORMAT_RG16 = 12,
	V3D_TEXTURE_DATA_FORMAT_RG16_SNORM = 13,
	V3D_TEXTURE_DATA_FORMAT_RGBA16 = 14,
	V3D_TEXTURE_DATA_FORMAT_RGBA16_SNORM = 15,
	V3D_TEXTURE_DATA_FORMAT_R16F = 16,
	V3D_TEXTURE_DATA_FORMAT_RG16F = 17,
	V3D_TEXTURE_DATA_FORMAT_RGBA16F = 18,
	V3D_TEXTURE_DATA_FORMAT_R11FG11FB10F = 19,
	V3D_TEXTURE_DATA_FORMAT_RGB9E5 = 20,
	V3D_TEXTURE_DATA_FORMAT_DEPTH_COMP16 = 21,
	V3D_TEXTURE_DATA_FORMAT_DEPTH_COMP24 = 22,
	V3D_TEXTURE_DATA_FORMAT_DEPTH_COMP32F = 23,
	V3D_TEXTURE_DATA_FORMAT_DEPTH24X8 = 24,
	V3D_TEXTURE_DATA_FORMAT_R4 = 25,
	V3D_TEXTURE_DATA_FORMAT_R1 = 26,
	V3D_TEXTURE_DATA_FORMAT_S8 = 27,
	V3D_TEXTURE_DATA_FORMAT_S16 = 28,
	V3D_TEXTURE_DATA_FORMAT_R32F = 29,
	V3D_TEXTURE_DATA_FORMAT_RG32F = 30,
	V3D_TEXTURE_DATA_FORMAT_RGBA32F = 31,
	V3D_TEXTURE_DATA_FORMAT_RGB8ETC2 = 32,
	V3D_TEXTURE_DATA_FORMAT_RGB8PUNCHTHROUGHALPHA1 = 33,
	V3D_TEXTURE_DATA_FORMAT_R11EAC = 34,
	V3D_TEXTURE_DATA_FORMAT_SIGNEDR11EAC = 35,
	V3D_TEXTURE_DATA_FORMAT_RG11EAC = 36,
	V3D_TEXTURE_DATA_FORMAT_SIGNEDRG11EAC = 37,
	V3D_TEXTURE_DATA_FORMAT_RGBA8ETC2EAC = 38,
	V3D_TEXTURE_DATA_FORMAT_YCBCRLUMA = 39,
	V3D_TEXTURE_DATA_FORMAT_YCBCR420CHROMA = 40,
	V3D_TEXTURE_DATA_FORMAT_BC1 = 48,
	V3D_TEXTURE_DATA_FORMAT_BC2 = 49,
	V3D_TEXTURE_DATA_FORMAT_BC3 = 50,
	V3D_TEXTURE_DATA_FORMAT_ASTC4X4 = 64,
	V3D_TEXTURE_DATA_FORMAT_ASTC5X4 = 65,
	V3D_TEXTURE_DATA_FORMAT_ASTC5X5 = 66,
	V3D_TEXTURE_DATA_FORMAT_ASTC6X5 = 67,
	V3D_TEXTURE_DATA_FORMAT_ASTC6X6 = 68,
	V3D_TEXTURE_DATA_FORMAT_ASTC8X5 = 69,
	V3D_TEXTURE_DATA_FORMAT_ASTC8X6 = 70,
	V3D_TEXTURE_DATA_FORMAT_ASTC8X8 = 71,
	V3D_TEXTURE_DATA_FORMAT_ASTC10X5 = 72,
	V3D_TEXTURE_DATA_FORMAT_ASTC10X6 = 73,
	V3D_TEXTURE_DATA_FORMAT_ASTC10X8 = 74,
	V3D_TEXTURE_DATA_FORMAT_ASTC10X10 = 75,
	V3D_TEXTURE_DATA_FORMAT_ASTC12X10 = 76,
	V3D_TEXTURE_DATA_FORMAT_ASTC12X12 = 77,
	V3D_TEXTURE_DATA_FORMAT_R8I = 96,
	V3D_TEXTURE_DATA_FORMAT_R8UI = 97,
	V3D_TEXTURE_DATA_FORMAT_RG8I = 98,
	V3D_TEXTURE_DATA_FORMAT_RG8UI = 99,
	V3D_TEXTURE_DATA_FORMAT_RGBA8I = 100,
	V3D_TEXTURE_DATA_FORMAT_RGBA8UI = 101,
	V3D_TEXTURE_DATA_FORMAT_R16I = 102,
	V3D_TEXTURE_DATA_FORMAT_R16UI = 103,
	V3D_TEXTURE_DATA_FORMAT_RG16I = 104,
	V3D_TEXTURE_DATA_FORMAT_RG16UI = 105,
	V3D_TEXTURE_DATA_FORMAT_RGBA16I = 106,
	V3D_TEXTURE_DATA_FORMAT_RGBA16UI = 107,
	V3D_TEXTURE_DATA_FORMAT_R32I = 108,
	V3D_TEXTURE_DATA_FORMAT_R32UI = 109,
	V3D_TEXTURE_DATA_FORMAT_RG32I = 110,
	V3D_TEXTURE_DATA_FORMAT_RG32UI = 111,
	V3D_TEXTURE_DATA_FORMAT_RGBA32I = 112,
	V3D_TEXTURE_DATA_FORMAT_RGBA32UI = 113,
	V3D_TEXTURE_DATA_FORMAT_RGB10A2UI = 114,
	V3D_TEXTURE_DATA_FORMAT_A1RGB5 = 115,
} v3d_texture_data_formats;

// End of auto-generated code.

//
// Interface
//

#ifdef __cplusplus
extern "C" {
#endif

// Machine code instructions for the quad processor unit
typedef v3d_u64 v3d_qpu_instruction;

void v3d_power_on(void);
void v3d_invalidate_caches(void);
// Called after a hang is detected
void v3d_reset(void);

v3d_u8 v3d_get_binning_flush_count(void);
v3d_u8 v3d_get_render_frame_count(void);

typedef enum v3d_wait_result
{
	v3d_wait_result_success,
	v3d_wait_result_error_detected,
	v3d_wait_result_timed_out
} v3d_wait_result;

v3d_wait_result v3d_wait_for_binning_flush(v3d_u8 lastFlush);
v3d_wait_result v3d_wait_for_render_frame(v3d_u8 lastFrame);

// tileStateData needs to be aligned to at least 2; it's probably best to align it to 64 to be safe.
void v3d_start_binning_commands(v3d_address binningCommandListStart,
	                            v3d_address binningCommandListEnd, v3d_address tileAllocation,
	                            v3d_u32 tileAllocationSize, v3d_address tileStateData);

void v3d_start_render_commands(v3d_address renderCommandListStart,
	                           v3d_address renderCommandListEnd);

v3d_f187 v3d_float_to_f187(v3d_float floatToConvert);

void* v3d_get_aligned_address(void* addressToAlign, int desiredAlignmentPowerOf2);

// These are not required to set up buffers, but may help.
typedef struct v3d_static_buffer
{
	v3d_u8* start;
	int used;
	int capacity;
} v3d_static_buffer;

#define V3D_BUFFER_WRITE_HEAD(buffer) ((buffer).start + (buffer).used)

// To handle out of memory, check (used >= capacity), which indicates buffer is too small. Do this
// just before using the buffer, for example, rather than after every write.
void v3d_buffer_write(v3d_static_buffer* buffer, void* data, v3d_uintptr dataSize);

// Use up capacity until write head is aligned to the desired alignment
void v3d_buffer_align(v3d_static_buffer* buffer, unsigned int alignment);

// Used to avoid copy; make sure to check the return is not 0 (buffer too small), in which case the
// memory will NOT count as being used
void* v3d_buffer_claim_memory(v3d_static_buffer* buffer, v3d_uintptr dataSize);

// The equivalent of v3d_buffer_align and v3d_buffer_claim_memory
void* v3d_buffer_allocate(v3d_static_buffer* buffer, v3d_uintptr dataSize, unsigned int alignment);

v3d_bool v3d_buffer_out_of_memory(v3d_static_buffer* buffer);

#define V3D_BUFFER_QUEUE_OPERATION_NO_ARGUMENTS(bufferAddress, operationId, type)         \
	{                                                                                     \
		type* quickQueue = (type*)v3d_buffer_claim_memory((bufferAddress), sizeof(type)); \
		if (quickQueue) quickQueue->operation = (operationId);                            \
	}

#define V3D_BUFFER_ALLOC_OPERATION(bufferAddress, operationId, type, pointerVariableName)      \
	type* pointerVariableName = (type*)v3d_buffer_claim_memory((bufferAddress), sizeof(type)); \
	if (pointerVariableName)                                                                   \
	{                                                                                          \
		pointerVariableName->operation = (operationId);                                        \
	}

#define V3D_BUFFER_ALLOC_STRUCT(bufferAddress, type) ((type*)v3d_buffer_claim_memory(bufferAddress, sizeof(type)))

#define V3D_ALIGN(valueToAlign, desiredAlignmentPowerOf2) \
	(((valueToAlign) + (desiredAlignmentPowerOf2) - 1) & ~((desiredAlignmentPowerOf2) - 1))

#ifdef __cplusplus
}
#endif

//
// Implementation
//

#ifdef V3D_IMPLEMENTATION

#ifdef __cplusplus
extern "C" {
#endif

#define PERIPHERAL_BASE (0xFE000000)

// Power management
#define PM_V3DRSTN (1 << 6)
#define PM_PASSWORD (0x5A000000)
#define ASB_REQ_STOP (1 << 0)
#define ASB_ACK (1 << 1)
#define ASB_EMPTY (1 << 2)
#define ASB_FULL (1 << 3)

#define PM_BASE (PERIPHERAL_BASE + 0x100000)
#define PM_GRAFX (PM_BASE + 0x10C)

#define RPIVID_ASB_BASE (PERIPHERAL_BASE + 0xC11000)
#define ASB_V3D_S_CTRL (RPIVID_ASB_BASE + 0x8)
#define ASB_V3D_M_CTRL (RPIVID_ASB_BASE + 0xC)

#define V3D_HUB_BASE (PERIPHERAL_BASE + 0xc00000)

#define V3D_CORE0 (PERIPHERAL_BASE + 0xC04000)

#define V3D_CTL_SLCACTL (V3D_CORE0 + 0x24)
#define V3D_CTL_L2TCACTL (V3D_CORE0 + 0x30)
  #define V3D_L2TCACTL_L2TFLS (1 << 0)
  #define V3D_L2TCACTL_FLM_FLUSH 0
#define V3D_CTL_L2TFLSTA (V3D_CORE0 + 0x34)
#define V3D_CTL_L2TFLEND (V3D_CORE0 + 0x38)

#define V3D_CLE_BFC (V3D_CORE0 + 0x134)
#define V3D_CLE_BFC_FLUSH_COUNT_MASK (0xFF)
#define V3D_CLE_RFC (V3D_CORE0 + 0x138)
#define V3D_CLE_RFC_FRAME_COUNT_MASK (0xFF)

// Control Thread Error
#define V3D_CLE_CTNCS_CTERR (1 << 3)
// Reset bit
#define V3D_CLE_CTNCS_CTRSTA (1 << 15)

// Tile memory address
#define V3D_CLE_CT0QMA (V3D_CORE0 + 0x170)
// Tile memory size
#define V3D_CLE_CT0QMS (V3D_CORE0 + 0x174)
#define V3D_CLE_CT0QTS (V3D_CORE0 + 0x15c)
	#define V3D_CLE_CT0QTS_ENABLE (1 << 1)
// Binning command list status
#define V3D_CLE_CT0CS (V3D_CORE0 + 0x100)
// Binning command list start
#define V3D_CLE_CT0QBA (V3D_CORE0 + 0x160)
// Render Thread 1 Control and Status.
#define V3D_CLE_CT1CS (V3D_CORE0 + 0x104)
// Render Thread 1 Current address
#define V3D_CLE_CT1CA (V3D_CORE0 + 0x114)
// Render Thread 1 List counter
#define V3D_CLE_CT1LC (V3D_CORE0 + 0x124)
// Render Thread 1 Primitive list counter
#define V3D_CLE_CT1PC (V3D_CORE0 + 0x12c)
// Render command list start
#define V3D_CLE_CT1QBA (V3D_CORE0 + 0x164)
// Binning command list end
#define V3D_CLE_CT0QEA (V3D_CORE0 + 0x168)
// Render command list end
#define V3D_CLE_CT1QEA (V3D_CORE0 + 0x16C)

static inline v3d_u32 v3d_read32 (v3d_uintptr nAddress)
{
	return *(v3d_u32 volatile *) nAddress;
}

static inline void v3d_write32 (v3d_uintptr nAddress, v3d_u32 nValue)
{
	*(v3d_u32 volatile *) nAddress = nValue;
}

void v3d_power_on(void)
{
	// Deasserting reset
    V3D_write(PM_GRAFX, (V3D_read(PM_GRAFX) | PM_V3DRSTN) | PM_PASSWORD);

	// Enabling ASB master
	V3D_write(ASB_V3D_M_CTRL, (V3D_read(ASB_V3D_M_CTRL) & ~ASB_REQ_STOP) | PM_PASSWORD);
	// Waiting for acknowledgement
	while (V3D_read(ASB_V3D_M_CTRL) & ASB_ACK)
	{
	}

	// Enabling ASB slave
	V3D_write(ASB_V3D_S_CTRL, (V3D_read(ASB_V3D_S_CTRL) & ~ASB_REQ_STOP) | PM_PASSWORD);
	// Waiting for acknowledgement
	while (V3D_read(ASB_V3D_S_CTRL) & ASB_ACK)
	{
	}
};

// See e.g. https://github.com/raspberrypi/linux/blob/a1073743767f9e7fdc7017ababd2a07ea0c97c1c/drivers/pmdomain/bcm/bcm2835-power.c#L343
void v3d_power_off(void)
{
	// Enabling ASB master
	V3D_write(ASB_V3D_M_CTRL, (V3D_read(ASB_V3D_M_CTRL) | ASB_REQ_STOP) | PM_PASSWORD);
	// Waiting for acknowledgement
	// See https://github.com/raspberrypi/linux/blob/a1073743767f9e7fdc7017ababd2a07ea0c97c1c/drivers/pmdomain/bcm/bcm2835-power.c#L153
	while (!(V3D_read(ASB_V3D_M_CTRL) & ASB_ACK))
	{
	}

	// Enabling ASB slave
	V3D_write(ASB_V3D_S_CTRL, (V3D_read(ASB_V3D_S_CTRL) | ASB_REQ_STOP) | PM_PASSWORD);
	// Waiting for acknowledgement
	while (!(V3D_read(ASB_V3D_S_CTRL) & ASB_ACK))
	{
	}

	// Assert the reset
    V3D_write(PM_GRAFX, (V3D_read(PM_GRAFX) & (~PM_V3DRSTN)) | PM_PASSWORD);
}

void v3d_reset(void)
{
	v3d_power_off();
	v3d_power_on();
}

static void v3d_invalidate_l2t(void)
{
	V3D_write(V3D_CTL_L2TFLSTA, 0);
	V3D_write(V3D_CTL_L2TFLEND, ~0);
	V3D_write(V3D_CTL_L2TCACTL, V3D_L2TCACTL_L2TFLS | V3D_L2TCACTL_FLM_FLUSH);
}

static void v3d_invalidate_slices(void)
{
    V3D_write(V3D_CTL_SLCACTL, ~0);
}

void v3d_invalidate_caches(void)
{
    v3d_invalidate_l2t();
    v3d_invalidate_slices();
}

v3d_u8 v3d_get_binning_flush_count(void)
{
	return V3D_read(V3D_CLE_BFC) & V3D_CLE_BFC_FLUSH_COUNT_MASK;
}

v3d_u8 v3d_get_render_frame_count(void)
{
	return V3D_read(V3D_CLE_RFC) & V3D_CLE_RFC_FRAME_COUNT_MASK;
}

v3d_wait_result v3d_wait_for_binning_flush(v3d_u8 lastFlush)
{
	v3d_u8 currentFlushCount = v3d_get_binning_flush_count();
	// Handle wrap-around
	while (currentFlushCount <= lastFlush && !(currentFlushCount == 0 && lastFlush == 255))
	{
		currentFlushCount = v3d_get_binning_flush_count();
		v3d_u32 status = V3D_read(V3D_CLE_CT0CS);
		if (status & V3D_CLE_CTNCS_CTERR)
		{
			// The control list executor has encountered some error
			return v3d_wait_result_error_detected;
		}
	}
	return v3d_wait_result_success;
}

v3d_wait_result v3d_wait_for_render_frame(v3d_u8 lastFrame)
{
	v3d_u8 currentFrameCount = v3d_get_render_frame_count();
	// Handle wrap-around
	while (currentFrameCount <= lastFrame && !(currentFrameCount == 0 && lastFrame == 255))
	{
		currentFrameCount = v3d_get_render_frame_count();
		v3d_u32 status = V3D_read(V3D_CLE_CT1CS);
		if (status & V3D_CLE_CTNCS_CTERR)
		{
			// The control list executor has encountered some error
			return v3d_wait_result_error_detected;
		}
	}
	return v3d_wait_result_success;
}

void v3d_start_binning_commands(v3d_address binningCommandListStart,
	                            v3d_address binningCommandListEnd, v3d_address tileAllocation,
	                            v3d_u32 tileAllocationSize, v3d_address tileStateData)
{
	if (tileAllocation)
	{
		V3D_write(V3D_CLE_CT0QMA, tileAllocation);
		V3D_write(V3D_CLE_CT0QMS, tileAllocationSize);
	}
	if (tileStateData)
	{
		// Note: Implies alignment!
		V3D_write(V3D_CLE_CT0QTS, tileStateData | V3D_CLE_CT0QTS_ENABLE);
	}

	V3D_write(V3D_CLE_CT0QBA, binningCommandListStart);
    V3D_write(V3D_CLE_CT0QEA, binningCommandListEnd);
}

void v3d_start_render_commands(v3d_address renderCommandListStart, v3d_address renderCommandListEnd)
{
	V3D_write(V3D_CLE_CT1QBA, renderCommandListStart);
	V3D_write(V3D_CLE_CT1QEA, renderCommandListEnd);
}

v3d_f187 v3d_float_to_f187(v3d_float floatToConvert)
{
	typedef union FloatToUint
	{
		v3d_float f;
		v3d_u32 u;
	} FloatToUint;
	FloatToUint converted = {floatToConvert};
	v3d_f187 final = (converted.u) >> 16;
	return final;
}

void* v3d_get_aligned_address(void* addressToAlign, int desiredAlignmentPowerOf2)
{
	v3d_uintptr desiredAddress =
		((v3d_uintptr)addressToAlign + desiredAlignmentPowerOf2 - 1)
		& ~(v3d_uintptr)(desiredAlignmentPowerOf2 - 1);
	return (void*)desiredAddress;
}

void v3d_buffer_write(v3d_static_buffer* buffer, void* data, v3d_uintptr dataSize)
{
	if (!buffer || !data || dataSize == 0)
	{
		return;
	}
	if (!buffer->start || buffer->capacity < 0 || buffer->used < 0
	    || buffer->used > buffer->capacity)
	{
		buffer->used = buffer->capacity > 0 ? buffer->capacity : 0;
		return;
	}

	v3d_uintptr numBytesFree = (v3d_uintptr)(buffer->capacity - buffer->used);
	if (dataSize > numBytesFree) dataSize = numBytesFree;
	V3D_memcpy(buffer->start + buffer->used, data, dataSize);
	buffer->used += (int)dataSize;
}

void v3d_buffer_align(v3d_static_buffer* buffer, unsigned int alignment)
{
	if (!buffer || !buffer->start || buffer->capacity < 0 || buffer->used < 0
	    || buffer->used > buffer->capacity || alignment == 0
	    || (alignment & (alignment - 1)) != 0)
	{
		if (buffer) buffer->used = buffer->capacity > 0 ? buffer->capacity : 0;
		return;
	}

	v3d_uintptr current = (v3d_uintptr)(buffer->start + buffer->used);
	v3d_uintptr desired = (current + alignment - 1) & ~(v3d_uintptr)(alignment - 1);
	v3d_uintptr padding = desired - current;
	if (padding > (v3d_uintptr)(buffer->capacity - buffer->used))
	{
		buffer->used = buffer->capacity;
		return;
	}
	buffer->used += (int)padding;
}

void* v3d_buffer_claim_memory(v3d_static_buffer* buffer, v3d_uintptr dataSize)
{
	if (!buffer || !buffer->start || buffer->capacity < 0 || buffer->used < 0
	    || buffer->used > buffer->capacity)
	{
		return 0;
	}
	v3d_uintptr numBytesFree = (v3d_uintptr)(buffer->capacity - buffer->used);
	if (dataSize > numBytesFree) return 0;
	void* data = buffer->start + buffer->used;
	buffer->used += (int)dataSize;
	return data;
}

void* v3d_buffer_allocate(v3d_static_buffer* buffer, v3d_uintptr dataSize, unsigned int alignment)
{
	v3d_buffer_align(buffer, alignment);
	return v3d_buffer_claim_memory(buffer, dataSize);
}

v3d_bool v3d_buffer_out_of_memory(v3d_static_buffer* buffer)
{
	return buffer->used >= buffer->capacity;
}

#ifdef __cplusplus
}
#endif

#endif // end V3D_IMPLEMENTATION

#endif // end v3d.h
