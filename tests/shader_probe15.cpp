// Isolated real D3D11/librashader experiment. No game, input or guest writes.
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "gpu_present.h"
#include "png_write.h"
#include <vector>
#include <filesystem>
#include <cstdio>
#include <cstdlib>

int main(int argc,char **argv){
 if(argc!=3)return 2;
 SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO))return 3;
 SDL_SetHint(SDL_HINT_RENDER_DRIVER,"direct3d11");
 SDL_Window *window=SDL_CreateWindow("Shader QA",0,0,1280,720,SDL_WINDOW_HIDDEN);
 SDL_Renderer *renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
 if(!renderer)return 4;
 SDL_Texture *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,342,224);
 if(!texture)return 7;
 std::filesystem::create_directories(argv[2]);
 _putenv_s("RR_SHADER_PATH",argv[1]);
 constexpr int w=342,h=224;std::vector<uint32_t> source(w*h),capture(1280*720);
 for(int frame=0;frame<300;frame++){
  // The entire top half remains bit-identical while the lower half changes
  // brightness, like passing road stripes. A CRT must not move the targets.
  unsigned shade=(frame/60)%2?0xffeeeeeeu:0xff111111u;
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){
   uint32_t p=y<112?0xff101010u:shade;
   if(y>=28&&y<86&&((x>=45&&x<53)||(x>=285&&x<293)))p=0xffeeeeeeu;
   if(y>=35&&y<80&&x>=100&&x<242)p=(x&1)?0xffeeeeeeu:0xff101010u;
   source[y*w+x]=p;
  }
  SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
  SDL_UpdateTexture(texture,nullptr,source.data(),w*4);SDL_RenderCopy(renderer,texture,nullptr,nullptr);
  if(!rr_gpu_present(renderer,source.data(),w,w,h,256,frame)){std::fprintf(stderr,"Failed at frame %d: %s\n",frame,SDL_GetError());return 5;}
  if(frame>=120&&frame<240){
   if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,capture.data(),1280*4))return 6;
   // Full output at key frames, fixed marker region on every other frame.
   char name[32];std::snprintf(name,sizeof(name),"frame%03d.png",frame);
   auto path=std::filesystem::path(argv[2])/name;
   png_write_argb(path.string().c_str(),capture.data(),1280,320,1280);
  }
  SDL_RenderPresent(renderer);
 }
 rr_gpu_shutdown();SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
 std::puts("Completed 300 real shader frames, 120 consecutive marker captures.");return 0;
}
