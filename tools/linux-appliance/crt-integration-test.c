// Exercise the production SDL_gpu path without replacing the running appliance.
#include "crt-fast.h"
#include <SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    int w = argc > 1 ? atoi(argv[1]) : 1920;
    int h = argc > 2 ? atoi(argv[2]) : 1080;
    if(w < 1 || h < 1 || w > 4096 || h > 4096) return 1;
    GPU_Target* screen = GPU_Init(64, 64, SDL_WINDOW_HIDDEN);
    if(!screen) return 1;
    printf("Renderer: %s\n", glGetString(GL_RENDERER));
    GPU_Image* output = GPU_CreateImage(w, h, GPU_FORMAT_RGBA);
    GPU_Target* target = output ? GPU_LoadTarget(output) : NULL;
    if(!target) return 1;
    // Match the window projection, not SDL_gpu's vertically inverted FBO one.
    GPU_MatrixMode(target, GPU_PROJECTION);
    GPU_LoadIdentity();
    GPU_Ortho(0, w, h, 0, -1, 1);
    GPU_MatrixMode(target, GPU_MODEL);
    unsigned char pixels[256*144*4];
    for(int i = 0; i < 256*144; ++i)
    {
        pixels[i*4] = i%256;
        pixels[i*4+1] = (i/256)*255/143;
        pixels[i*4+2] = (i*13)%256;
        pixels[i*4+3] = 255;
        if(argc > 4)
            pixels[i*4] = pixels[i*4+1] = pixels[i*4+2] = 255;
    }
    double total = 0;
    for(int i = 0; i < 32; ++i)
    {
        Uint64 start = SDL_GetPerformanceCounter();
        GPU_Clear(target);
        if(!crt_fast_draw(pixels, target, 0, 0, w, h)) return 1;
        glFinish();
        if(i >= 2) total += (double)(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency();
    }
    printf("%dx%d: %.3f ms/frame (30 frames, including conversion/upload)\n",w,h,total*1000/30);
    if(argc > 3 && !GPU_SaveImage(output,argv[3],GPU_FILE_PNG)) return 1;
    // Reinitialization catches stale handles across renderer lifetime changes.
    crt_fast_shutdown();
    if(!crt_fast_draw(pixels,target,0,0,w,h)) return 1;
    crt_fast_shutdown();
    GPU_FreeImage(output);
    GPU_Quit();
    return 0;
}
