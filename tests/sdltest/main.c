#include <SDL.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <stdlib.h>
static void geo(SDL_Renderer* r, SDL_Texture* t, float x, float y)
{
    SDL_Vertex v[4] = {
        { { x, y }, { 255, 255, 255, 255 }, { 0, 0 } },
        { { x + 200, y }, { 255, 255, 255, 255 }, { 1, 0 } },
        { { x + 200, y + 200 }, { 255, 255, 255, 255 }, { 1, 1 } },
        { { x, y + 200 }, { 255, 255, 255, 255 }, { 0, 1 } } };
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, t, v, 4, idx, 6);
}
int main(int argc, char** argv)
{
    WHBLogCafeInit();
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* w = SDL_CreateWindow("t", 0, 0, 1280, 720, 0);
    SDL_Renderer* r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    SDL_RenderSetLogicalSize(r, 1280, 720);
    SDL_Texture* rt = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 420, 240);
    SDL_Texture* big = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 2048, 4096);
    uint32_t* px = malloc(2048 * 4096 * 4);
    for (int i = 0; i < 2048 * 4096; i++) px[i] = 0xFF00FF00u ^ (uint32_t)i;
    WHBLogPrintf("stage: update big");
    SDL_UpdateTexture(big, NULL, px, 2048 * 4);
    SDL_SetTextureScaleMode(big, SDL_ScaleModeNearest);
    SDL_BlendMode premul = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    SDL_BlendMode darken = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    for (int f = 0; f < 1200; f++)
    {
        int stage = f / 120;
        if (f % 120 == 0) WHBLogPrintf("stage %d frame %d", stage, f);
        SDL_Event e; while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) f = 5000;
        SDL_SetRenderTarget(r, rt);
        SDL_SetRenderDrawColor(r, 0, 0, 255, 255); SDL_RenderClear(r);
        if (stage >= 1) geo(r, big, 10, 10);
        if (stage >= 2) { SDL_SetTextureBlendMode(big, premul); geo(r, big, 50, 20); }
        if (stage >= 3) { SDL_SetTextureBlendMode(big, darken); SDL_SetTextureAlphaMod(big, 128); geo(r, big, 100, 20); SDL_SetTextureAlphaMod(big, 255); }
        if (stage >= 5 && f % 120 == 0) { uint32_t p[16]; SDL_Rect rr = { 0, 0, 4, 4 }; WHBLogPrintf("readpixels"); SDL_RenderReadPixels(r, &rr, SDL_PIXELFORMAT_RGBA32, p, 16); WHBLogPrintf("readpixels done %08x", p[0]); }
        SDL_SetRenderTarget(r, NULL);
        if (stage >= 4) { SDL_Rect vp = { 10, 0, 1260, 720 }; SDL_RenderSetViewport(r, &vp); }
        SDL_SetRenderDrawColor(r, (f * 3) & 255, 64, 64, 255);
        SDL_RenderClear(r);
        SDL_SetTextureBlendMode(rt, SDL_BLENDMODE_NONE);
        SDL_Rect d = { 100 + f % 300, 100, 840, 480 };
        SDL_RenderCopy(r, rt, NULL, &d);
        if (stage >= 6) geo(r, rt, 400, 400);
        SDL_RenderSetViewport(r, NULL);
        SDL_RenderPresent(r);
    }
    WHBLogPrintf("done");
    SDL_Quit();
    return 0;
}
