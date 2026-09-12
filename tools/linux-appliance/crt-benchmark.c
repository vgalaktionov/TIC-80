#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>

#include "crt-shader.inc"

static GLuint shader(GLenum type, const char *source)
{
    GLuint result = glCreateShader(type);
    glShaderSource(result, 1, &source, NULL);
    glCompileShader(result);
    GLint ok;
    glGetShaderiv(result, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char message[4096];
        glGetShaderInfoLog(result, sizeof message, NULL, message);
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
    return result;
}

int main(int argc, char **argv)
{
    int w = argc > 1 ? atoi(argv[1]) : 1920;
    int h = argc > 2 ? atoi(argv[2]) : 1080;
    if (w < 1 || h < 1 || w > 4096 || h > 4096) return 1;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_Window *window = SDL_CreateWindow("CRT timing", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window || !SDL_GL_CreateContext(window)) return 1;
    printf("Renderer: %s\n", glGetString(GL_RENDERER));
    GLuint program = glCreateProgram();
    glAttachShader(program, shader(GL_VERTEX_SHADER, "#version 110\nvarying vec2 texCoord; void main(){gl_Position=gl_Vertex; texCoord=vec2(gl_Vertex.x+1.0,1.0-gl_Vertex.y)*0.5;}"));
    glAttachShader(program, shader(GL_FRAGMENT_SHADER, PixelShader));
    glLinkProgram(program);
    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) return 1;
    GLuint output, input, target;
    glGenTextures(1, &output);
    glBindTexture(GL_TEXTURE_2D, output);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &target);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 1;
    unsigned char pixels[256 * 144 * 4];
    for (int i = 0; i < 256 * 144; ++i) {
        pixels[i * 4] = i % 256;
        pixels[i * 4 + 1] = (i / 256) * 255 / 143;
        pixels[i * 4 + 2] = (i * 13) % 256;
        pixels[i * 4 + 3] = 255;
    }
    glGenTextures(1, &input);
    glBindTexture(GL_TEXTURE_2D, input);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 144, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "source"), 0);
    glUniform1f(glGetUniformLocation(program, "trg_x"), 0);
    glUniform1f(glGetUniformLocation(program, "trg_y"), 0);
    glUniform1f(glGetUniformLocation(program, "trg_w"), w);
    glUniform1f(glGetUniformLocation(program, "trg_h"), h);
    glViewport(0, 0, w, h);
    double total = 0;
    for (int i = 0; i < 12; ++i) {
        Uint64 start = SDL_GetPerformanceCounter();
        glBegin(GL_TRIANGLE_STRIP);
        glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(-1, 1); glVertex2f(1, 1);
        glEnd();
        glFinish();
        if (i >= 2) total += (double)(SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency();
    }
    if (glGetError() != GL_NO_ERROR) return 1;
    printf("%dx%d: %.3f ms/draw (10 draws, glFinish, no VSync)\n", w, h, total * 100);
    if (argc > 3) {
        unsigned char *result = malloc((size_t)w * h * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, result);
        FILE *file = fopen(argv[3], "wb");
        if (!file || glGetError() != GL_NO_ERROR) return 1;
        fprintf(file, "P6\n%d %d\n255\n", w, h);
        if (fwrite(result, 3, (size_t)w * h, file) != (size_t)w * h) return 1;
        fclose(file);
        free(result);
    }
    SDL_Quit();
    return 0;
}
