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

static inline unsigned attenuateChannel(unsigned value, unsigned firstShift,
                                        unsigned secondShift)
{
    return value - (value >> firstShift) - (secondShift ? value >> secondShift : 0);
}

static inline uint32_t attenuate(uint32_t color, unsigned firstShift,
                                 unsigned secondShift = 0)
{
    const unsigned blue = attenuateChannel(color & 0xff, firstShift, secondShift);
    const unsigned green = attenuateChannel((color >> 8) & 0xff, firstShift, secondShift);
    const unsigned red = attenuateChannel((color >> 16) & 0xff, firstShift, secondShift);

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

static inline void writeMask(uint32_t* output, const uint32_t colors[3], unsigned phase)
{
    switch (phase)
    {
    case 0:
        output[0] = colors[0];
        output[1] = colors[1];
        output[2] = colors[2];
        output[3] = colors[0];
        break;
    case 1:
        output[0] = colors[1];
        output[1] = colors[2];
        output[2] = colors[0];
        output[3] = colors[1];
        break;
    default:
        output[0] = colors[2];
        output[1] = colors[0];
        output[2] = colors[1];
        output[3] = colors[2];
        break;
    }
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
    for (int y = 0; y < TIC80_HEIGHT; y++)
    {
        const uint32_t* inputLine = source + (y + TIC80_MARGIN_TOP) * TIC80_FULLWIDTH
                                    + TIC80_MARGIN_LEFT;
        uint32_t* outputRows[TIC80_BAREMETAL_SCREEN_SCALE];
        for (unsigned row = 0; row < TIC80_BAREMETAL_SCREEN_SCALE; row++)
        {
            outputRows[row] = output + outputPitch
                               * (y * TIC80_BAREMETAL_SCREEN_SCALE + row);
        }

        unsigned phase = 0;

        for (int x = 0; x < TIC80_WIDTH; x++)
        {
            const uint32_t color = blurPixel(inputLine, x);
            const uint32_t dim = attenuate(color, 3, 5);
            const uint32_t phosphor[3] =
            {
                (dim & ~0x00ff0000) | (color & 0x00ff0000),
                (dim & ~0x0000ff00) | (color & 0x0000ff00),
                (dim & ~0x000000ff) | (color & 0x000000ff),
            };
            uint32_t scanline[3];

            for (unsigned index = 0; index < 3; index++)
            {
                scanline[index] = attenuate(phosphor[index], 3);
            }
            writeMask(outputRows[0] + x * TIC80_BAREMETAL_SCREEN_SCALE, scanline, phase);

            writeMask(outputRows[1] + x * TIC80_BAREMETAL_SCREEN_SCALE, phosphor, phase);

            for (unsigned index = 0; index < 3; index++)
            {
                scanline[index] = attenuate(phosphor[index], 4);
            }
            writeMask(outputRows[2] + x * TIC80_BAREMETAL_SCREEN_SCALE, scanline, phase);

            for (unsigned index = 0; index < 3; index++)
            {
                scanline[index] = attenuate(phosphor[index], 2, 4);
            }
            writeMask(outputRows[3] + x * TIC80_BAREMETAL_SCREEN_SCALE, scanline, phase);

            if (++phase == 3) phase = 0;
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
