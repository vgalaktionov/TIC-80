// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <SDL_gpu.h>

bool crt_fast_draw(const void* rgba, GPU_Target* target, float x, float y, float w, float h);
void crt_fast_shutdown(void);
