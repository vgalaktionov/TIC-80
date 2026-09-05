// TIC-80 Raspberry Pi bare-metal video renderer
// Copyright (C) 2026 TIC-80 contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "video.h"

#include <cstring>
#include <tic80.h>

static inline uint32_t scaleColor(uint32_t color, unsigned brightness,
                                  unsigned redMask, unsigned greenMask, unsigned blueMask)
{
    unsigned blue = color & 0xff;
    unsigned green = (color >> 8) & 0xff;
    unsigned red = (color >> 16) & 0xff;

    red = red * brightness * redMask >> 16;
    green = green * brightness * greenMask >> 16;
    blue = blue * brightness * blueMask >> 16;

    return 0xff000000 | red << 16 | green << 8 | blue;
}

static inline uint32_t blurPixel(const uint32_t* line, int x)
{
    const uint32_t center = line[x];
    const uint32_t left = line[x > 0 ? x - 1 : x];
    const uint32_t right = line[x + 1 < TIC80_WIDTH ? x + 1 : x];

    const unsigned blue = ((left & 0xff) + 6 * (center & 0xff) + (right & 0xff)) >> 3;
    const unsigned green = (((left >> 8) & 0xff) + 6 * ((center >> 8) & 0xff)
                            + ((right >> 8) & 0xff)) >> 3;
    const unsigned red = (((left >> 16) & 0xff) + 6 * ((center >> 16) & 0xff)
                          + ((right >> 16) & 0xff)) >> 3;

    return 0xff000000 | red << 16 | green << 8 | blue;
}

static void copyNearest(uint32_t* output, unsigned outputPitch, const uint32_t* source)
{
    for (int y = 0; y < TIC80_HEIGHT; y++)
    {
        const uint32_t* inputLine = source + (y + TIC80_MARGIN_TOP) * TIC80_FULLWIDTH
                                    + TIC80_MARGIN_LEFT;
        uint32_t* outputLine = output + outputPitch * y * TIC80_BAREMETAL_SCREEN_SCALE;

        for (int x = 0; x < TIC80_WIDTH; x++)
        {
            const uint32_t color = inputLine[x];
            uint32_t* pixel = outputLine + x * TIC80_BAREMETAL_SCREEN_SCALE;

            for (unsigned column = 0; column < TIC80_BAREMETAL_SCREEN_SCALE; column++)
            {
                pixel[column] = color;
            }
        }

        for (unsigned row = 1; row < TIC80_BAREMETAL_SCREEN_SCALE; row++)
        {
            memcpy(outputLine + outputPitch * row, outputLine,
                   TIC80_WIDTH * TIC80_BAREMETAL_SCREEN_SCALE * sizeof(uint32_t));
        }
    }
}

static void copyCrt(uint32_t* output, unsigned outputPitch, const uint32_t* source)
{
    static const unsigned ScanlineBrightness[TIC80_BAREMETAL_SCREEN_SCALE] =
        {224, 256, 240, 176};
    static const unsigned PhosphorMask[3][3] =
    {
        {256, 216, 216},
        {216, 256, 216},
        {216, 216, 256},
    };

    for (int y = 0; y < TIC80_HEIGHT; y++)
    {
        const uint32_t* inputLine = source + (y + TIC80_MARGIN_TOP) * TIC80_FULLWIDTH
                                    + TIC80_MARGIN_LEFT;

        for (int x = 0; x < TIC80_WIDTH; x++)
        {
            const uint32_t color = blurPixel(inputLine, x);

            for (unsigned row = 0; row < TIC80_BAREMETAL_SCREEN_SCALE; row++)
            {
                uint32_t* pixel = output + outputPitch
                                  * (y * TIC80_BAREMETAL_SCREEN_SCALE + row)
                                  + x * TIC80_BAREMETAL_SCREEN_SCALE;

                for (unsigned column = 0; column < TIC80_BAREMETAL_SCREEN_SCALE; column++)
                {
                    const unsigned* mask =
                        PhosphorMask[(x * TIC80_BAREMETAL_SCREEN_SCALE + column) % 3];
                    pixel[column] = scaleColor(color, ScanlineBrightness[row],
                                               mask[0], mask[1], mask[2]);
                }
            }
        }
    }
}

void tic80_baremetal_render(uint32_t* output, unsigned outputPitch,
                            const uint32_t* source, bool crt)
{
    if (crt)
    {
        copyCrt(output, outputPitch, source);
    }
    else
    {
        copyNearest(output, outputPitch, source);
    }
}
