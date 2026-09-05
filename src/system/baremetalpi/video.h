// TIC-80 Raspberry Pi bare-metal video renderer
// Copyright (C) 2026 TIC-80 contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <stdint.h>

static const unsigned TIC80_BAREMETAL_SCREEN_SCALE = 4;

void tic80_baremetal_render(uint32_t* output, unsigned outputPitch,
                            const uint32_t* source, bool crt);
