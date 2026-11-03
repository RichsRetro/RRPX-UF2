#include "renderer.hpp"
#include "liteAssets.hpp"

#include <SDL_image.h>

#include <algorithm>

#include <cmath>

#include "infoLog.hpp"

#ifdef NXDK

#include <hal/video.h>

#endif

// One line of text with the default font is 31 pixels high.

// FIXME: Should probably be dynamic and dependent of font settings.

const unsigned int FONT_TEX_SIZE = 31;

Renderer::Renderer() {

#ifdef NXDK

  VIDEO_MODE xmode = XVideoGetMode();

  height = xmode.height;

  width = xmode.width;

  windowFlags = SDL_WINDOW_SHOWN;

#else

  height = 480;

  width = 640;

  windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;

  // renderFlags = SDL_RENDERER_PRESENTVSYNC|SDL_RENDERER_SOFTWARE;

#endif

  overscanCompX = width * 0.075;

  overscanCompY = height * 0.075;

  menuItemCount = (height - (overscanCompY * 2)) / FONT_TEX_SIZE;

  lowerHalf = menuItemCount / 2;

  upperHalf = ceil(menuItemCount / 2.0);

}

Renderer::~Renderer() {
  if (branding) SDL_DestroyTexture(branding);

  if (background != nullptr) {

    SDL_DestroyTexture(background);

  }

  if (renderer != nullptr) {

    SDL_DestroyRenderer(renderer);

  }

  if (window != nullptr) {

    SDL_DestroyWindow(window);

  }

}

int Renderer::init() {

  window = SDL_CreateWindow("Rich's Retro XDash Lite Edition", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,

                            width, height, windowFlags);

  if (window == nullptr) {

    return 1;

  }

  renderer = SDL_CreateRenderer(window, -1, renderFlags);

  if (renderer == nullptr) {

    return 2;

  }

  SDL_SetRenderDrawBlendMode(getRenderer(), SDL_BLENDMODE_BLEND);

  setDrawColor();

  clear();

  return 0;

}

int Renderer::init(const char* bgpath) {

  int ret = init();

  if (ret != 0) {

    return ret;

  }

  // UF2 edition: a small generated backdrop replaces both resolution PNGs.
  (void)bgpath;
  SDL_Surface* bgsurf = SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!bgsurf) return 3;
  for (int y = 0; y < 240; ++y) {
    SDL_Rect line = {0, y, 320, 1};
    SDL_FillRect(bgsurf, &line, SDL_MapRGB(bgsurf->format, 2, 9 + y / 24, 17 + y / 16));
  }
  const Uint32 grid = SDL_MapRGB(bgsurf->format, 3, 28, 40);
  for (int x = 0; x < 320; x += 32) { SDL_Rect line = {x, 0, 1, 240}; SDL_FillRect(bgsurf, &line, grid); }
  for (int y = 0; y < 240; y += 32) { SDL_Rect line = {0, y, 320, 1}; SDL_FillRect(bgsurf, &line, grid); }
  background = SDL_CreateTextureFromSurface(renderer, bgsurf);
  SDL_FreeSurface(bgsurf);
  // ENDGAME only transfers the executable: branding must come from its resource.
  SDL_Surface* mark=IMG_Load_RW(SDL_RWFromConstMem(lite_logo,static_cast<int>(lite_logo_size)),1);
  if(mark){branding=SDL_CreateTextureFromSurface(renderer,mark);SDL_FreeSurface(mark);}
  if(!branding)InfoLog::outputLine(InfoLog::ERROR,"Embedded Lite logo failed: %s",IMG_GetError());

  if (background == nullptr) {

    InfoLog::outputLine(InfoLog::ERROR, "Creating background texture failed.\n");

    return 4;

  }

  return ret;

}

int Renderer::clear() {

  int ret = SDL_RenderClear(renderer);

  return ret;

}

void Renderer::flip() {

  setDrawColor(0, 0, 0, 0xFF);

  SDL_RenderDrawRect(renderer, nullptr);

  setDrawColor();

  SDL_RenderPresent(renderer);

#ifdef NXDK

  XVideoWaitForVBlank();

#endif

}

int Renderer::setDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {

  return SDL_SetRenderDrawColor(renderer, r, g, b, a);

}

void Renderer::drawTexture(SDL_Texture* tex, SDL_Rect& src, SDL_Rect& dst) {

  SDL_RenderCopy(renderer, tex, &src, &dst);

}

void Renderer::drawTexture(SDL_Texture* tex, SDL_Rect& dst) {

  SDL_RenderCopy(renderer, tex, nullptr, &dst);

}

void Renderer::drawTexture(SDL_Texture* tex, int x, int y) {

  SDL_Rect dst = { x, y, 0, 0 };

  SDL_QueryTexture(tex, nullptr, nullptr, &dst.w, &dst.h);

  drawTexture(tex, dst);

}

void Renderer::fillRectangle(const SDL_Rect& dst) {

  SDL_RenderFillRect(renderer, &dst);

}

void Renderer::fillRectangle(const SDL_FRect& dst) {

  SDL_RenderFillRectF(renderer, &dst);

}

void Renderer::blitSurface(SDL_Surface* bg, SDL_Surface* fg, int offset) {

  SDL_Rect dst = { offset, offset, fg->w, fg->h };

  SDL_SetSurfaceBlendMode(fg, SDL_BLENDMODE_BLEND);

  SDL_BlitSurface(fg, NULL, bg, &dst);

}

void Renderer::drawBackground() {

  if (background != nullptr) {

    SDL_Rect destination = { 0, 0, width, height };

    drawTexture(background, destination);

  }

}

void Renderer::setWidescreen(bool enabled) {

  int outputWidth = 0, outputHeight = 0;

  if (!renderer || SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight) < 0 ||

      outputWidth <= 0 || outputHeight <= 0) return;

  // Composite pixels are not square. Use square-pixel layout coordinates,

  // then map the complete layout to the existing physical output mode.

  width = static_cast<int>(std::round(outputHeight * (enabled ? 16.0 / 9.0 : 4.0 / 3.0)));

  height = outputHeight;

  overscanCompX = static_cast<int>(width * 0.075f);

  overscanCompY = static_cast<int>(height * 0.075f);

  SDL_RenderSetScale(renderer, static_cast<float>(outputWidth) / width, 1.0f);

}
void Renderer::drawBranding() {
  if(!branding)return;
  const int side=std::min(static_cast<int>(height*0.36f),static_cast<int>(width*0.43f));
  const int left=static_cast<int>(width*0.08f);
  const int right=static_cast<int>(width*0.54f);
  SDL_Rect box={left+(right-left-side)/2,static_cast<int>(height*0.22f),side,side};
  SDL_RenderCopy(renderer,branding,nullptr,&box);
}

