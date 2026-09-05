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

static const uint32_t AlphaMask = 0xff000000;
static const uint32_t RgbMask = 0x00ffffff;

static inline uint32_t dim7Over8(uint32_t color)
{
    const uint32_t rgb = color & RgbMask;
    return AlphaMask | (rgb - ((rgb >> 3) & 0x001f1f1f));
}

static inline uint32_t dim15Over16(uint32_t color)
{
    const uint32_t rgb = color & RgbMask;
    return AlphaMask | (rgb - ((rgb >> 4) & 0x000f0f0f));
}

static inline uint32_t dim11Over16(uint32_t color)
{
    const uint32_t rgb = color & RgbMask;
    return AlphaMask | (rgb - ((rgb >> 2) & 0x003f3f3f)
                         - ((rgb >> 4) & 0x000f0f0f));
}

static inline uint32_t dim27Over32(uint32_t color)
{
    const uint32_t rgb = color & RgbMask;
    return AlphaMask | (rgb - ((rgb >> 3) & 0x001f1f1f)
                         - ((rgb >> 5) & 0x00070707));
}

static inline uint32_t blurPixel(const uint32_t* line, int x)
{
    const uint32_t center = line[x];
    const uint32_t left = line[x > 0 ? x - 1 : x];
    const uint32_t right = line[x + 1 < TIC80_WIDTH ? x + 1 : x];

    const uint32_t redBlue = ((left & 0x00ff00ff)
                              + 6 * (center & 0x00ff00ff)
                              + (right & 0x00ff00ff)) >> 3 & 0x00ff00ff;
    const uint32_t green = ((left & 0x0000ff00)
                            + 6 * (center & 0x0000ff00)
                            + (right & 0x0000ff00)) >> 3 & 0x0000ff00;

    return AlphaMask | redBlue | green;
}

template<unsigned Phase>
static inline void writeMask(uint32_t* output, uint32_t red, uint32_t green, uint32_t blue)
{
    if (Phase == 0)
    {
        output[0] = red;
        output[1] = green;
        output[2] = blue;
        output[3] = red;
    }
    else if (Phase == 1)
    {
        output[0] = green;
        output[1] = blue;
        output[2] = red;
        output[3] = green;
    }
    else
    {
        output[0] = blue;
        output[1] = red;
        output[2] = green;
        output[3] = blue;
    }
}

template<unsigned Phase>
static inline void copyCrtPixel(uint32_t* outputRows[TIC80_BAREMETAL_SCREEN_SCALE],
                                const uint32_t* inputLine, int x)
{
    const uint32_t color = blurPixel(inputLine, x);
    const uint32_t dim = dim27Over32(color);
    const uint32_t phosphorRed = (dim & ~0x00ff0000) | (color & 0x00ff0000);
    const uint32_t phosphorGreen = (dim & ~0x0000ff00) | (color & 0x0000ff00);
    const uint32_t phosphorBlue = (dim & ~0x000000ff) | (color & 0x000000ff);
    const unsigned offset = x * TIC80_BAREMETAL_SCREEN_SCALE;

    writeMask<Phase>(outputRows[0] + offset,
                     dim7Over8(phosphorRed),
                     dim7Over8(phosphorGreen),
                     dim7Over8(phosphorBlue));
    writeMask<Phase>(outputRows[1] + offset, phosphorRed, phosphorGreen, phosphorBlue);
    writeMask<Phase>(outputRows[2] + offset,
                     dim15Over16(phosphorRed),
                     dim15Over16(phosphorGreen),
                     dim15Over16(phosphorBlue));
    writeMask<Phase>(outputRows[3] + offset,
                     dim11Over16(phosphorRed),
                     dim11Over16(phosphorGreen),
                     dim11Over16(phosphorBlue));
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

        static_assert(TIC80_WIDTH % 3 == 0, "CRT mask loop requires three-pixel groups");
        for (int x = 0; x < TIC80_WIDTH; x += 3)
        {
            copyCrtPixel<0>(outputRows, inputLine, x);
            copyCrtPixel<1>(outputRows, inputLine, x + 1);
            copyCrtPixel<2>(outputRows, inputLine, x + 2);
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
