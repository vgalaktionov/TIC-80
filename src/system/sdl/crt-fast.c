// SPDX-License-Identifier: MIT
#include "crt-fast.h"
#include "crt-fast-shader.h"
#include <SDL_opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { Width = 256, Height = 144, Entries = 1024 };
static struct
{
    GPU_Image *source, *weights;
    Uint32 program;
    GPU_ShaderBlock block;
    int weights_location;
    float *pixels;
    float linear[256];
    bool attempted;
} crt;

void crt_fast_shutdown(void)
{
    GPU_FreeImage(crt.source);
    GPU_FreeImage(crt.weights);
    if(crt.program) GPU_FreeShaderProgram(crt.program);
    free(crt.pixels);
    memset(&crt, 0, sizeof crt);
}

// SDL_gpu owns the texture and its sampling state; only its storage is floating
// point. Flush and restore the binding when using GL outside SDL_gpu's cache.
static bool upload(GPU_Image* image, GLint format, const float* pixels, bool allocate)
{
    GPU_FlushBlitBuffer();
    GLint binding;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    glBindTexture(GL_TEXTURE_2D, GPU_GetTextureHandle(image));
    if(allocate)
        glTexImage2D(GL_TEXTURE_2D, 0, format, image->w, image->h, 0, GL_RGBA, GL_FLOAT, pixels);
    else
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image->w, image->h, GL_RGBA, GL_FLOAT, pixels);
    GLenum error = glGetError();
    glBindTexture(GL_TEXTURE_2D, binding);
    return error == GL_NO_ERROR;
}

static bool initialize(void)
{
    if(crt.attempted) return crt.program != 0;
    crt.attempted = true;
    if(!SDL_GL_ExtensionSupported("GL_ARB_texture_float")) return false;

    static const char Vertex[] =
        "#version 110\n"
        "attribute vec3 gpu_Vertex; attribute vec2 gpu_TexCoord;\n"
        "uniform mat4 gpu_ModelViewProjectionMatrix; varying vec2 texCoord;\n"
        "void main(){texCoord=gpu_TexCoord;\n"
        "gl_Position=gpu_ModelViewProjectionMatrix*vec4(gpu_Vertex,1.0);}\n";
    Uint32 vertex = GPU_CompileShader(GPU_VERTEX_SHADER, Vertex);
    Uint32 pixel = GPU_CompileShader(GPU_PIXEL_SHADER, FastPixelShader);
    if(vertex && pixel) crt.program = GPU_LinkShaders(vertex, pixel);
    if(vertex) GPU_FreeShader(vertex);
    if(pixel) GPU_FreeShader(pixel);
    if(!crt.program) goto failed;
    crt.block = GPU_LoadShaderBlock(crt.program, "gpu_Vertex", "gpu_TexCoord", NULL,
                                    "gpu_ModelViewProjectionMatrix");
    crt.weights_location = GPU_GetUniformLocation(crt.program, "weights");
    if(crt.weights_location < 0) goto failed;
    crt.source = GPU_CreateImage(Width, Height, GPU_FORMAT_RGBA);
    crt.weights = GPU_CreateImage(Entries, 2, GPU_FORMAT_RGBA);
    crt.pixels = malloc(Width * Height * 4 * sizeof(float));
    if(!crt.source || !crt.weights || !crt.pixels) goto failed;
    GPU_SetAnchor(crt.source, 0, 0);
    GPU_SetImageFilter(crt.source, GPU_FILTER_LINEAR);
    GPU_SetImageFilter(crt.weights, GPU_FILTER_LINEAR);
    GPU_SetBlending(crt.source, false);

    for(int i = 0; i < 256; ++i)
    {
        double c = 1.2 * i / 255.0;
        crt.linear[i] = c <= .04045 ? c / 12.92 : pow((c + .055) / 1.055, 2.4);
    }

    // Four horizontal and two vertical taps retain the significant Gaussian
    // weights. The original's faint outer tails are omitted; scanline weights
    // remain unnormalized so the dark gaps and overall brightness are retained.
    float weights[Entries * 2 * 4] = {0};
    for(int i = 0; i < Entries; ++i)
    {
        double f = (double)i / (Entries - 1), w[4];
        for(int j = 0; j < 4; ++j) w[j] = exp2(-3 * (f + 1 - j) * (f + 1 - j));
        double left = w[0] + w[1], right = w[2] + w[3];
        weights[4*i] = -1 + w[1] / left;
        weights[4*i+1] = 1 + w[3] / right;
        weights[4*i+2] = left / (left + right);
        double a = exp2(-12*f*f), b = exp2(-12*(1-f)*(1-f));
        weights[4*(Entries+i)] = b / (a+b);
        weights[4*(Entries+i)+1] = a+b;
    }
    if(!upload(crt.source, GL_RGBA16F, NULL, true)
        || !upload(crt.weights, GL_RGBA32F, weights, true)) goto failed;
    puts("CRT: linear-light filtered renderer");
    return true;

failed:
    fprintf(stderr, "CRT: optimized renderer unavailable; using original shader (%s)\n", GPU_GetShaderMessage());
    crt_fast_shutdown();
    crt.attempted = true;
    return false;
}

bool crt_fast_draw(const void* rgba, GPU_Target* target, float x, float y, float w, float h)
{
    if(!initialize()) return false;
    const Uint8* source = rgba;
    for(int i = 0; i < Width * Height; ++i)
    {
        for(int c = 0; c < 3; ++c) crt.pixels[i*4+c] = crt.linear[source[i*4+c]];
        crt.pixels[i*4+3] = 1;
    }
    if(!upload(crt.source, GL_RGBA16F, crt.pixels, false))
    {
        crt_fast_shutdown();
        crt.attempted = true;
        return false;
    }
    GPU_ActivateShaderProgram(crt.program, &crt.block);
    GPU_SetShaderImage(crt.weights, crt.weights_location, 1);
    GPU_BlitScale(crt.source, NULL, target, x, y, w / Width, h / Height);
    GPU_FlushBlitBuffer();
    GPU_DeactivateShaderProgram();
    return true;
}
