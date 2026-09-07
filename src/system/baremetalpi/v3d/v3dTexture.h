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

// v3dTexture.h: Convert to/from the native GPU texture formats.

// Texture conversion

/* A UIFblock is a 256-byte region of memory that's 256-byte aligned.  These
 * will be grouped in 4x4 blocks (left-to-right, then top-to-bottom) in a 4KB
 * page.  Those pages are then arranged left-to-right, top-to-bottom, to cover
 * an image.
 *
 * The inside of a UIFblock, for packed pixels, will be split into 4 64-byte
 * utiles.  Utiles may be 8x8 (8bpp), 8x4(16bpp) or 4x4 (32bpp).
 */

#ifndef V3D_TEXTURE_H
#define V3D_TEXTURE_H

#include "v3d.h"

// If you want to disable V3D_ARCH_AARCH64, you need to define it to 0 before including this header.
#ifndef V3D_ARCH_AARCH64
#define V3D_ARCH_AARCH64 1
#endif

typedef struct v3d_texture_box
{
	int x;
	int y;
	int z;
	int width;
	int height;
	int depth;
} v3d_texture_box;

v3d_u32 v3d_utile_width(int componentsPerPixel);
v3d_u32 v3d_utile_height(int componentsPerPixel);
void v3d_load_tiled_image(void *dst, v3d_u32 dst_stride,
                          void *src, v3d_u32 src_stride,
                          enum v3d_memory_format tiling_format, int componentsPerPixel,
                          v3d_u32 image_h,
                          const v3d_texture_box *box);
// Upload to GPU (todo macoy rename)
void v3d_store_tiled_image(void *dst, v3d_u32 dst_stride,
                           void *src, v3d_u32 src_stride,
                           enum v3d_memory_format tiling_format, int componentsPerPixel,
                           v3d_u32 image_h,
                           const v3d_texture_box *box);
#endif // V3D_TEXTURE_H

#ifdef V3D_TEXTURE_IMPLEMENTATION

/** @file v3d_cpu_tiling.h
 *
 * Contains load/store functions common to both v3d and vc4.  The utile layout
 * stayed the same, though the way utiles get laid out has changed.
 */

static inline void v3d_load_utile(void* cpu, v3d_u32 cpu_stride, void* gpu, v3d_u32 gpu_stride)
{
#if defined(V3D_BUILD_NEON) && V3D_ARCH_ARM
	if (gpu_stride == 8)
	{
		__asm__ volatile(
			/* Load from the GPU in one shot, no interleave, to
			 * d0-d7.
			 */
			"vldm %[gpu], {q0, q1, q2, q3}\n"
			/* Store each 8-byte line to cpu-side destination,
			 * incrementing it by the stride each time.
			 */
			"vst1.8 d0, [%[cpu]], %[cpu_stride]\n"
			"vst1.8 d1, [%[cpu]], %[cpu_stride]\n"
			"vst1.8 d2, [%[cpu]], %[cpu_stride]\n"
			"vst1.8 d3, [%[cpu]], %[cpu_stride]\n"
			"vst1.8 d4, [%[cpu]], %[cpu_stride]\n"
			"vst1.8 d5, [%[cpu]], %[cpu_stride]\n"
			"vst1.8 d6, [%[cpu]], %[cpu_stride]\n"
			"vst1.8 d7, [%[cpu]]\n"
			: [ cpu ] "+r"(cpu)
			: [ gpu ] "r"(gpu), [ cpu_stride ] "r"(cpu_stride)
			: "q0", "q1", "q2", "q3");
		return;
	}
	else if (gpu_stride == 16)
	{
		void* cpu2 = cpu + 8;
		__asm__ volatile(
			/* Load from the GPU in one shot, no interleave, to
			 * d0-d7.
			 */
			"vldm %[gpu], {q0, q1, q2, q3};\n"
			/* Store each 16-byte line in 2 parts to the cpu-side
			 * destination.  (vld1 can only store one d-register
			 * at a time).
			 */
			"vst1.8 d0, [%[cpu]], %[cpu_stride]\n"
			"vst1.8 d1, [%[cpu2]],%[cpu_stride]\n"
			"vst1.8 d2, [%[cpu]], %[cpu_stride]\n"
			"vst1.8 d3, [%[cpu2]],%[cpu_stride]\n"
			"vst1.8 d4, [%[cpu]], %[cpu_stride]\n"
			"vst1.8 d5, [%[cpu2]],%[cpu_stride]\n"
			"vst1.8 d6, [%[cpu]]\n"
			"vst1.8 d7, [%[cpu2]]\n"
			: [ cpu ] "+r"(cpu), [ cpu2 ] "+r"(cpu2)
			: [ gpu ] "r"(gpu), [ cpu_stride ] "r"(cpu_stride)
			: "q0", "q1", "q2", "q3");
		return;
	}
#elif V3D_ARCH_AARCH64
	if (gpu_stride == 8)
	{
		__asm__ volatile(
		    /* Load from the GPU in one shot, no interleave, to
		     * d0-d7.
		     */
		    "ld1 {v0.2d, v1.2d, v2.2d, v3.2d}, [%[gpu]]\n"
		    /* Store each 8-byte line to cpu-side destination,
		     * incrementing it by the stride each time.
		     */
		    "st1 {v0.D}[0], [%[cpu]], %[cpu_stride]\n"
		    "st1 {v0.D}[1], [%[cpu]], %[cpu_stride]\n"
		    "st1 {v1.D}[0], [%[cpu]], %[cpu_stride]\n"
		    "st1 {v1.D}[1], [%[cpu]], %[cpu_stride]\n"
		    "st1 {v2.D}[0], [%[cpu]], %[cpu_stride]\n"
		    "st1 {v2.D}[1], [%[cpu]], %[cpu_stride]\n"
		    "st1 {v3.D}[0], [%[cpu]], %[cpu_stride]\n"
		    "st1 {v3.D}[1], [%[cpu]]\n"
		    : [ cpu ] "+r"(cpu)
		    : [ gpu ] "r"(gpu), [ cpu_stride ] "r"(cpu_stride)
		    : "v0", "v1", "v2", "v3");
		return;
	}
	else if (gpu_stride == 16)
	{
		void* cpu2 = cpu + 8;
		__asm__ volatile(
		    /* Load from the GPU in one shot, no interleave, to
		     * d0-d7.
		     */
		    "ld1 {v0.2d, v1.2d, v2.2d, v3.2d}, [%[gpu]]\n"
		    /* Store each 16-byte line in 2 parts to the cpu-side
		     * destination.  (vld1 can only store one d-register
		     * at a time).
		     */
		    "st1 {v0.D}[0], [%[cpu]], %[cpu_stride]\n"
		    "st1 {v0.D}[1], [%[cpu2]],%[cpu_stride]\n"
		    "st1 {v1.D}[0], [%[cpu]], %[cpu_stride]\n"
		    "st1 {v1.D}[1], [%[cpu2]],%[cpu_stride]\n"
		    "st1 {v2.D}[0], [%[cpu]], %[cpu_stride]\n"
		    "st1 {v2.D}[1], [%[cpu2]],%[cpu_stride]\n"
		    "st1 {v3.D}[0], [%[cpu]]\n"
		    "st1 {v3.D}[1], [%[cpu2]]\n"
		    : [ cpu ] "+r"(cpu), [ cpu2 ] "+r"(cpu2)
		    : [ gpu ] "r"(gpu), [ cpu_stride ] "r"(cpu_stride)
		    : "v0", "v1", "v2", "v3");
		return;
	}
#endif

	for (v3d_u32 gpu_offset = 0; gpu_offset < 64; gpu_offset += gpu_stride)
	{
		V3D_memcpy(cpu, gpu + gpu_offset, gpu_stride);
		cpu += cpu_stride;
	}
}

static inline void v3d_store_utile(void* gpu, v3d_u32 gpu_stride, void* cpu, v3d_u32 cpu_stride)
{
#if defined(V3D_BUILD_NEON) && V3D_ARCH_ARM
	if (gpu_stride == 8)
	{
		__asm__ volatile(
			/* Load each 8-byte line from cpu-side source,
			 * incrementing it by the stride each time.
			 */
			"vld1.8 d0, [%[cpu]], %[cpu_stride]\n"
			"vld1.8 d1, [%[cpu]], %[cpu_stride]\n"
			"vld1.8 d2, [%[cpu]], %[cpu_stride]\n"
			"vld1.8 d3, [%[cpu]], %[cpu_stride]\n"
			"vld1.8 d4, [%[cpu]], %[cpu_stride]\n"
			"vld1.8 d5, [%[cpu]], %[cpu_stride]\n"
			"vld1.8 d6, [%[cpu]], %[cpu_stride]\n"
			"vld1.8 d7, [%[cpu]]\n"
			/* Load from the GPU in one shot, no interleave, to
			 * d0-d7.
			 */
			"vstm %[gpu], {q0, q1, q2, q3}\n"
			: [ cpu ] "+r"(cpu)
			: [ gpu ] "r"(gpu), [ cpu_stride ] "r"(cpu_stride)
			: "q0", "q1", "q2", "q3");
		return;
	}
	else if (gpu_stride == 16)
	{
		void* cpu2 = cpu + 8;
		__asm__ volatile(
			/* Load each 16-byte line in 2 parts from the cpu-side
			 * destination.  (vld1 can only store one d-register
			 * at a time).
			 */
			"vld1.8 d0, [%[cpu]], %[cpu_stride]\n"
			"vld1.8 d1, [%[cpu2]],%[cpu_stride]\n"
			"vld1.8 d2, [%[cpu]], %[cpu_stride]\n"
			"vld1.8 d3, [%[cpu2]],%[cpu_stride]\n"
			"vld1.8 d4, [%[cpu]], %[cpu_stride]\n"
			"vld1.8 d5, [%[cpu2]],%[cpu_stride]\n"
			"vld1.8 d6, [%[cpu]]\n"
			"vld1.8 d7, [%[cpu2]]\n"
			/* Store to the GPU in one shot, no interleave. */
			"vstm %[gpu], {q0, q1, q2, q3}\n"
			: [ cpu ] "+r"(cpu), [ cpu2 ] "+r"(cpu2)
			: [ gpu ] "r"(gpu), [ cpu_stride ] "r"(cpu_stride)
			: "q0", "q1", "q2", "q3");
		return;
	}
#elif V3D_ARCH_AARCH64
	if (gpu_stride == 8)
	{
		__asm__ volatile(
		    /* Load each 8-byte line from cpu-side source,
		     * incrementing it by the stride each time.
		     */
		    "ld1 {v0.D}[0], [%[cpu]], %[cpu_stride]\n"
		    "ld1 {v0.D}[1], [%[cpu]], %[cpu_stride]\n"
		    "ld1 {v1.D}[0], [%[cpu]], %[cpu_stride]\n"
		    "ld1 {v1.D}[1], [%[cpu]], %[cpu_stride]\n"
		    "ld1 {v2.D}[0], [%[cpu]], %[cpu_stride]\n"
		    "ld1 {v2.D}[1], [%[cpu]], %[cpu_stride]\n"
		    "ld1 {v3.D}[0], [%[cpu]], %[cpu_stride]\n"
		    "ld1 {v3.D}[1], [%[cpu]]\n"
		    /* Store to the GPU in one shot, no interleave. */
		    "st1 {v0.2d, v1.2d, v2.2d, v3.2d}, [%[gpu]]\n"
		    : [ cpu ] "+r"(cpu)
		    : [ gpu ] "r"(gpu), [ cpu_stride ] "r"(cpu_stride)
		    : "v0", "v1", "v2", "v3");
		return;
	}
	else if (gpu_stride == 16)
	{
		void* cpu2 = cpu + 8;
		__asm__ volatile(
		    /* Load each 16-byte line in 2 parts from the cpu-side
		     * destination.  (vld1 can only store one d-register
		     * at a time).
		     */
		    "ld1 {v0.D}[0], [%[cpu]], %[cpu_stride]\n"
		    "ld1 {v0.D}[1], [%[cpu2]],%[cpu_stride]\n"
		    "ld1 {v1.D}[0], [%[cpu]], %[cpu_stride]\n"
		    "ld1 {v1.D}[1], [%[cpu2]],%[cpu_stride]\n"
		    "ld1 {v2.D}[0], [%[cpu]], %[cpu_stride]\n"
		    "ld1 {v2.D}[1], [%[cpu2]],%[cpu_stride]\n"
		    "ld1 {v3.D}[0], [%[cpu]]\n"
		    "ld1 {v3.D}[1], [%[cpu2]]\n"
		    /* Store to the GPU in one shot, no interleave. */
		    "st1 {v0.2d, v1.2d, v2.2d, v3.2d}, [%[gpu]]\n"
		    : [ cpu ] "+r"(cpu), [ cpu2 ] "+r"(cpu2)
		    : [ gpu ] "r"(gpu), [ cpu_stride ] "r"(cpu_stride)
		    : "v0", "v1", "v2", "v3");
		return;
	}
#endif

	for (v3d_u32 gpu_offset = 0; gpu_offset < 64; gpu_offset += gpu_stride)
	{
		V3D_memcpy(gpu + gpu_offset, cpu, gpu_stride);
		cpu += cpu_stride;
	}
}

/** @file v3d_tiling.c
 *
 * Handles information about the V3D tiling formats, and loading and storing
 * from them.
 */

/** Return the width in pixels of a 64-byte microtile. */
v3d_u32 v3d_utile_width(int componentsPerPixel)
{
	switch (componentsPerPixel)
	{
		case 1:
		case 2:
			return 8;
		case 4:
		case 8:
			return 4;
		case 16:
			return 2;
		default:
			return 4;  // unreachable("unknown componentsPerPixel");
	}
}

/** Return the height in pixels of a 64-byte microtile. */
v3d_u32 v3d_utile_height(int componentsPerPixel)
{
	switch (componentsPerPixel)
	{
		case 1:
			return 8;
		case 2:
		case 4:
			return 4;
		case 8:
		case 16:
			return 2;
		default:
			return 4;  // unreachable("unknown componentsPerPixel");
	}
}

/**
 * Returns the byte address for a given pixel within a utile.
 *
 * Utiles are 64b blocks of pixels in raster order, with 32bpp being a 4x4
 * arrangement.
 */
static inline v3d_u32 v3d_get_utile_pixel_offset(v3d_u32 componentsPerPixel, v3d_u32 x, v3d_u32 y)
{
	v3d_u32 utile_w = v3d_utile_width(componentsPerPixel);

	// assert(x < utile_w && y < v3d_utile_height(componentsPerPixel));

	return x * componentsPerPixel + y * utile_w * componentsPerPixel;
}

/**
 * Returns the byte offset for a given pixel in a LINEARTILE layout.
 *
 * LINEARTILE is a single line of utiles in either the X or Y direction.
 */
static inline v3d_u32 v3d_get_lt_pixel_offset(v3d_u32 componentsPerPixel, v3d_u32 image_h,
	                                          v3d_u32 x, v3d_u32 y)
{
	v3d_u32 utile_w = v3d_utile_width(componentsPerPixel);
	v3d_u32 utile_h = v3d_utile_height(componentsPerPixel);
	v3d_u32 utile_index_x = x / utile_w;
	v3d_u32 utile_index_y = y / utile_h;

	// assert(utile_index_x == 0 || utile_index_y == 0);

	return (64 * (utile_index_x + utile_index_y) +
		    v3d_get_utile_pixel_offset(componentsPerPixel, x & (utile_w - 1), y & (utile_h - 1)));
}

/**
 * Returns the byte offset for a given pixel in a UBLINEAR layout.
 *
 * UBLINEAR is the layout where pixels are arranged in UIF blocks (2x2
 * utiles), and the UIF blocks are in 1 or 2 columns in raster order.
 */
static inline v3d_u32 v3d_get_ublinear_pixel_offset(v3d_u32 componentsPerPixel, v3d_u32 x,
	                                                v3d_u32 y, int ublinear_number)
{
	v3d_u32 utile_w = v3d_utile_width(componentsPerPixel);
	v3d_u32 utile_h = v3d_utile_height(componentsPerPixel);
	v3d_u32 ub_w = utile_w * 2;
	v3d_u32 ub_h = utile_h * 2;
	v3d_u32 ub_x = x / ub_w;
	v3d_u32 ub_y = y / ub_h;

	return (256 * (ub_y * ublinear_number + ub_x) + ((x & utile_w) ? 64 : 0) +
		    ((y & utile_h) ? 128 : 0) +
		    +v3d_get_utile_pixel_offset(componentsPerPixel, x & (utile_w - 1), y & (utile_h - 1)));
}

static inline v3d_u32 v3d_get_ublinear_2_column_pixel_offset(v3d_u32 componentsPerPixel,
	                                                         v3d_u32 image_h, v3d_u32 x, v3d_u32 y)
{
	return v3d_get_ublinear_pixel_offset(componentsPerPixel, x, y, 2);
}

static inline v3d_u32 v3d_get_ublinear_1_column_pixel_offset(v3d_u32 componentsPerPixel,
	                                                         v3d_u32 image_h, v3d_u32 x, v3d_u32 y)
{
	return v3d_get_ublinear_pixel_offset(componentsPerPixel, x, y, 1);
}

// The below two functions are from Mesa/src/util/bitscan.c:
#ifndef V3D_FFS
#define V3D_FFS
#ifdef HAVE___BUILTIN_FFS
#elif defined(_MSC_VER) && (_M_IX86 || _M_ARM || _M_AMD64 || _M_IA64)
#else
int
v3d_ffs(int i)
{
	int bit = 0;
	if (!i)
		return bit;
	if (!(i & 0xffff)) {
		bit += 16;
		i >>= 16;
	}
	if (!(i & 0xff)) {
		bit += 8;
		i >>= 8;
	}
	if (!(i & 0xf)) {
		bit += 4;
		i >>= 4;
	}
	if (!(i & 0x3)) {
		bit += 2;
		i >>= 2;
	}
	if (!(i & 0x1))
		bit += 1;
	return bit + 1;
}
#endif

#ifdef HAVE___BUILTIN_FFSLL
#elif defined(_MSC_VER) && (_M_AMD64 || _M_ARM64 || _M_IA64)
#else
int
v3d_ffsll(long long int val)
{
	int bit;

	bit = v3d_ffs((unsigned) (val & 0xffffffff));
	if (bit != 0)
		return bit;

	bit = v3d_ffs((unsigned) (val >> 32));
	if (bit != 0)
		return 32 + bit;

	return 0;
}
#endif
#endif // V3D_FFS

/**
 * Returns the byte offset for a given pixel in a UIF layout.
 *
 * UIF is the general V3D tiling layout shared across 3D, media, and scanout.
 * It stores pixels in UIF blocks (2x2 utiles), and UIF blocks are stored in
 * 4x4 groups, and those 4x4 groups are then stored in raster order.
 */
static inline v3d_u32 v3d_get_uif_pixel_offset(v3d_u32 componentsPerPixel, v3d_u32 image_h,
	                                           v3d_u32 x, v3d_u32 y, v3d_bool do_xor)
{
	v3d_u32 utile_w = v3d_utile_width(componentsPerPixel);
	v3d_u32 utile_h = v3d_utile_height(componentsPerPixel);
	v3d_u32 mb_width = utile_w * 2;
	v3d_u32 mb_height = utile_h * 2;
	v3d_u32 log2_mb_width = v3d_ffs(mb_width) - 1;
	v3d_u32 log2_mb_height = v3d_ffs(mb_height) - 1;

	/* Macroblock X, y */
	v3d_u32 mb_x = x >> log2_mb_width;
	v3d_u32 mb_y = y >> log2_mb_height;
	/* X, y within the macroblock */
	v3d_u32 mb_pixel_x = x - (mb_x << log2_mb_width);
	v3d_u32 mb_pixel_y = y - (mb_y << log2_mb_height);

	if (do_xor && (mb_x / 4) & 1)
		mb_y ^= 0x10;

	v3d_u32 mb_h = V3D_ALIGN(image_h, 1 << log2_mb_height) >> log2_mb_height;
	v3d_u32 mb_id = ((mb_x / 4) * ((mb_h - 1) * 4)) + mb_x + mb_y * 4;

	v3d_u32 mb_base_addr = mb_id * 256;

	v3d_bool top = mb_pixel_y < utile_h;
	v3d_bool left = mb_pixel_x < utile_w;

	/* Docs have this in pixels, we do bytes here. */
	v3d_u32 mb_tile_offset = (!top * 128 + !left * 64);

	v3d_u32 utile_x = mb_pixel_x & (utile_w - 1);
	v3d_u32 utile_y = mb_pixel_y & (utile_h - 1);

	v3d_u32 mb_pixel_address = (mb_base_addr + mb_tile_offset +
		                        v3d_get_utile_pixel_offset(componentsPerPixel, utile_x, utile_y));

	return mb_pixel_address;
}

static inline v3d_u32 v3d_get_uif_xor_pixel_offset(v3d_u32 componentsPerPixel, v3d_u32 image_h,
	                                               v3d_u32 x, v3d_u32 y)
{
	return v3d_get_uif_pixel_offset(componentsPerPixel, image_h, x, y, TRUE);
}

static inline v3d_u32 v3d_get_uif_no_xor_pixel_offset(v3d_u32 componentsPerPixel, v3d_u32 image_h,
	                                                  v3d_u32 x, v3d_u32 y)
{
	return v3d_get_uif_pixel_offset(componentsPerPixel, image_h, x, y, FALSE);
}

/* Loads/stores non-utile-aligned boxes by walking over the destination
 * rectangle, computing the address on the GPU, and storing/loading a pixel at
 * a time.
 */
static inline void v3d_move_pixels_unaligned(
	void* gpu, v3d_u32 gpu_stride, void* cpu, v3d_u32 cpu_stride, int componentsPerPixel,
	v3d_u32 image_h, const v3d_texture_box* box,
	v3d_u32 (*get_pixel_offset)(v3d_u32 componentsPerPixel, v3d_u32 image_h, v3d_u32 x, v3d_u32 y),
	v3d_bool is_load)
{
	for (v3d_u32 y = 0; y < box->height; y++)
	{
		void* cpu_row = cpu + y * cpu_stride;

		for (int x = 0; x < box->width; x++)
		{
			v3d_u32 pixel_offset =
				get_pixel_offset(componentsPerPixel, image_h, box->x + x, box->y + y);

			/* if (FALSE) { */
			/*         fprintf(stderr, "%3d,%3d -> %d\n", */
			/*                 box->x + x, box->y + y, */
			/*                 pixel_offset); */
			/* } */

			if (is_load)
			{
				V3D_memcpy(cpu_row + x * componentsPerPixel, gpu + pixel_offset,
					       componentsPerPixel);
			}
			else
			{
				V3D_memcpy(gpu + pixel_offset, cpu_row + x * componentsPerPixel,
					       componentsPerPixel);
			}
		}
	}
}

/* Breaks the image down into utiles and calls either the fast whole-utile
 * load/store functions, or the unaligned fallback case.
 */
static inline void v3d_move_pixels_general_percomponentsPerPixel(
	void* gpu, v3d_u32 gpu_stride, void* cpu, v3d_u32 cpu_stride, int componentsPerPixel,
	v3d_u32 image_h, const v3d_texture_box* box,
	v3d_u32 (*get_pixel_offset)(v3d_u32 componentsPerPixel, v3d_u32 image_h, v3d_u32 x, v3d_u32 y),
	v3d_bool is_load)
{
	v3d_u32 utile_w = v3d_utile_width(componentsPerPixel);
	v3d_u32 utile_h = v3d_utile_height(componentsPerPixel);
	v3d_u32 utile_gpu_stride = utile_w * componentsPerPixel;
	v3d_u32 x1 = box->x;
	v3d_u32 y1 = box->y;
	v3d_u32 x2 = box->x + box->width;
	v3d_u32 y2 = box->y + box->height;
	v3d_u32 align_x1 = V3D_ALIGN(x1, utile_w);
	v3d_u32 align_y1 = V3D_ALIGN(y1, utile_h);
	v3d_u32 align_x2 = x2 & ~(utile_w - 1);
	v3d_u32 align_y2 = y2 & ~(utile_h - 1);

	/* Load/store all the whole utiles first. */
	for (v3d_u32 y = align_y1; y < align_y2; y += utile_h)
	{
		void* cpu_row = cpu + (y - box->y) * cpu_stride;

		for (v3d_u32 x = align_x1; x < align_x2; x += utile_w)
		{
			void* utile_gpu = (gpu + get_pixel_offset(componentsPerPixel, image_h, x, y));
			void* utile_cpu = cpu_row + (x - box->x) * componentsPerPixel;

			if (is_load)
			{
				v3d_load_utile(utile_cpu, cpu_stride, utile_gpu, utile_gpu_stride);
			}
			else
			{
				v3d_store_utile(utile_gpu, utile_gpu_stride, utile_cpu, cpu_stride);
			}
		}
	}

	/* If there were no aligned utiles in the middle, load/store the whole
	 * thing unaligned.
	 */
	if (align_y2 <= align_y1 || align_x2 <= align_x1)
	{
		v3d_move_pixels_unaligned(gpu, gpu_stride, cpu, cpu_stride, componentsPerPixel, image_h,
			                      box, get_pixel_offset, is_load);
		return;
	}

	/* Load/store the partial utiles. */
	v3d_texture_box partial_boxes[4] = {
		/* Top */
		{
		    .x = x1,
		    .y = y1,
		    .width = (int)(x2 - x1),
		    .height = align_y1 - y1,
		},
		/* Bottom */
		{
		    .x = x1,
		    .y = align_y2,
		    .width = (int)(x2 - x1),
		    .height = y2 - align_y2,
		},
		/* Left */
		{
		    .x = x1,
		    .y = align_y1,
		    .width = (int)(align_x1 - x1),
		    .height = align_y2 - align_y1,
		},
		/* Right */
		{
		    .x = align_x2,
		    .y = align_y1,
		    .width = (int)(x2 - align_x2),
		    .height = align_y2 - align_y1,
		},
	};
	for (int i = 0; i < V3D_ARRAY_SIZE(partial_boxes); i++)
	{
		void* partial_cpu = (cpu + (partial_boxes[i].y - y1) * cpu_stride +
			                 (partial_boxes[i].x - x1) * componentsPerPixel);

		v3d_move_pixels_unaligned(gpu, gpu_stride, partial_cpu, cpu_stride, componentsPerPixel,
			                      image_h, &partial_boxes[i], get_pixel_offset, is_load);
	}
}

static inline void v3d_move_pixels_general(
	void* gpu, v3d_u32 gpu_stride, void* cpu, v3d_u32 cpu_stride, int componentsPerPixel,
	v3d_u32 image_h, const v3d_texture_box* box,
	v3d_u32 (*get_pixel_offset)(v3d_u32 componentsPerPixel, v3d_u32 image_h, v3d_u32 x, v3d_u32 y),
	v3d_bool is_load)
{
	switch (componentsPerPixel)
	{
		case 1:
			v3d_move_pixels_general_percomponentsPerPixel(gpu, gpu_stride, cpu, cpu_stride, 1,
				                                          image_h, box, get_pixel_offset, is_load);
			break;
		case 2:
			v3d_move_pixels_general_percomponentsPerPixel(gpu, gpu_stride, cpu, cpu_stride, 2,
				                                          image_h, box, get_pixel_offset, is_load);
			break;
		case 4:
			v3d_move_pixels_general_percomponentsPerPixel(gpu, gpu_stride, cpu, cpu_stride, 4,
				                                          image_h, box, get_pixel_offset, is_load);
			break;
		case 8:
			v3d_move_pixels_general_percomponentsPerPixel(gpu, gpu_stride, cpu, cpu_stride, 8,
				                                          image_h, box, get_pixel_offset, is_load);
			break;
		case 16:
			v3d_move_pixels_general_percomponentsPerPixel(gpu, gpu_stride, cpu, cpu_stride, 16,
				                                          image_h, box, get_pixel_offset, is_load);
			break;
	}
}

static inline void v3d_move_tiled_image(void* gpu, v3d_u32 gpu_stride, void* cpu,
	                                    v3d_u32 cpu_stride, enum v3d_memory_format tiling_format,
	                                    int componentsPerPixel, v3d_u32 image_h,
	                                    const v3d_texture_box* box, v3d_bool is_load)
{
	switch (tiling_format)
	{
		case V3D_MEMORY_FORMAT_UIF_XOR:
			v3d_move_pixels_general(gpu, gpu_stride, cpu, cpu_stride, componentsPerPixel, image_h,
				                    box, v3d_get_uif_xor_pixel_offset, is_load);
			break;
		case V3D_MEMORY_FORMAT_UIF_NO_XOR:
			v3d_move_pixels_general(gpu, gpu_stride, cpu, cpu_stride, componentsPerPixel, image_h,
				                    box, v3d_get_uif_no_xor_pixel_offset, is_load);
			break;
		case V3D_MEMORY_FORMAT_UB_LINEAR_2_UIF_BLOCKS_WIDE:
			v3d_move_pixels_general(gpu, gpu_stride, cpu, cpu_stride, componentsPerPixel, image_h,
				                    box, v3d_get_ublinear_2_column_pixel_offset, is_load);
			break;
		case V3D_MEMORY_FORMAT_UB_LINEAR_1_UIF_BLOCK_WIDE:
			v3d_move_pixels_general(gpu, gpu_stride, cpu, cpu_stride, componentsPerPixel, image_h,
				                    box, v3d_get_ublinear_1_column_pixel_offset, is_load);
			break;
		case V3D_MEMORY_FORMAT_LINEARTILE:
			v3d_move_pixels_general(gpu, gpu_stride, cpu, cpu_stride, componentsPerPixel, image_h,
				                    box, v3d_get_lt_pixel_offset, is_load);
			break;
		default:
			/* unreachable("Unsupported tiling format"); */
			break;
	}
}

/**
 * Loads pixel data from the start (microtile-aligned) box in \p src to the
 * start of \p dst according to the given tiling format.
 */
void v3d_load_tiled_image(void* dst, v3d_u32 dst_stride, void* src, v3d_u32 src_stride,
	                      enum v3d_memory_format tiling_format, int componentsPerPixel,
	                      v3d_u32 image_h, const v3d_texture_box* box)
{
	v3d_move_tiled_image(src, src_stride, dst, dst_stride, tiling_format, componentsPerPixel,
		                 image_h, box, TRUE);
}

/**
 * Stores pixel data from the start of \p src into a (microtile-aligned) box in
 * \p dst according to the given tiling format.
 */
void v3d_store_tiled_image(void* dst, v3d_u32 dst_stride, void* src, v3d_u32 src_stride,
	                       enum v3d_memory_format tiling_format, int componentsPerPixel,
	                       v3d_u32 image_h, const v3d_texture_box* box)
{
	v3d_move_tiled_image(dst, dst_stride, src, src_stride, tiling_format, componentsPerPixel,
		                 image_h, box, FALSE);
}
#endif // V3D_TEXTURE_IMPLEMENTATION
