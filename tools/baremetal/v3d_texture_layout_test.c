// Bounds-check and round-trip the UIF layouts used by the CRT renderer.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef V3D_ARCH_AARCH64
#define V3D_ARCH_AARCH64 0
#endif
#define V3D_TEXTURE_IMPLEMENTATION
#include "../../src/system/baremetalpi/v3d/v3dTexture.h"

static int check_offsets(unsigned width, unsigned height, unsigned padded_height, unsigned size)
{
    unsigned char* seen = calloc(size, 1);
    if (!seen) return 1;

    for (unsigned y = 0; y < height; ++y)
    {
        for (unsigned x = 0; x < width; ++x)
        {
            unsigned offset = v3d_get_uif_no_xor_pixel_offset(4, padded_height, x, y);
            if (offset > size - 4 || offset % 4 || seen[offset])
            {
                fprintf(stderr,
                        "invalid UIF offset: %ux%u/%u pixel %u,%u -> %u "
                        "(size %u, duplicate %u)\n",
                        width, height, padded_height, x, y, offset, size, seen[offset]);
                free(seen);
                return 1;
            }
            seen[offset] = 1;
        }
    }

    free(seen);
    return 0;
}

int main(void)
{
    const unsigned source_size = 256 * 136 * 4;
    const unsigned mask_size = 960 * 560 * 4;
    if (check_offsets(240, 136, 136, source_size)
        || check_offsets(960, 544, 560, mask_size)) return 1;

    unsigned char* source = malloc(240 * 136 * 4);
    unsigned char* tiled = calloc(source_size, 1);
    unsigned char* restored = calloc(240 * 136 * 4, 1);
    if (!source || !tiled || !restored) return 1;
    for (unsigned i = 0; i < 240 * 136 * 4; ++i)
        source[i] = (unsigned char)(i * 37u + i / 251u);
    const v3d_texture_box box = {
        .x = 0,
        .y = 0,
        .width = 240,
        .height = 136,
    };
    v3d_store_tiled_image(tiled, 256 * 4, source, 240 * 4,
                          V3D_MEMORY_FORMAT_UIF_NO_XOR, 4, 136, &box);
    v3d_load_tiled_image(restored, 240 * 4, tiled, 256 * 4,
                         V3D_MEMORY_FORMAT_UIF_NO_XOR, 4, 136, &box);
    if (memcmp(source, restored, 240 * 136 * 4))
    {
        fprintf(stderr, "UIF source round-trip mismatch\n");
        return 1;
    }
    free(restored);
    free(tiled);
    free(source);
    return 0;
}
