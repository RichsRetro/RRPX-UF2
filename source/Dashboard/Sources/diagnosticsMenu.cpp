/* SPDX-License-Identifier: MIT. RRXD Lite controller and console diagnostics. */
#include "diagnosticsMenu.hpp"
#include "menu.hpp"
#include "subAppRouter.hpp"
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <hal/video.h>
#include <cstdio>
namespace diagnostics {
namespace {
class Screen:public SubApp {
  Renderer& renderer;bool padMode;SDL_GameController* pad=nullptr;Uint32 held=0;
public:
  Screen(Renderer&r,bool mode):renderer(r),padMode(mode){}
  ~Screen(){if(pad)SDL_GameControllerClose(pad);}
  void onBPressed() override { if(!padMode)SubAppRouter::getInstance()->pop(); }
  void render(Font& font) override {
    if(pad&&!SDL_GameControllerGetAttached(pad)){SDL_GameControllerClose(pad);pad=nullptr;}
    if(!pad)for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)){pad=SDL_GameControllerOpen(i);break;}
    std::string text=padMode?"Controller tester\n\n":"Xbox information\n\n";
    char line[220];
    if(padMode) {
      text+=pad?SDL_GameControllerName(pad):"Connect an Xbox controller";text+="\n\n";
      const char* names[]={"A","B","X","Y","Back","Guide","Start","Left stick","Right stick","White","Black","D-pad Up","D-pad Down","D-pad Left","D-pad Right"};
      for(int i=0;i<SDL_CONTROLLER_BUTTON_MAX&&i<15;++i)if(pad&&SDL_GameControllerGetButton(pad,static_cast<SDL_GameControllerButton>(i)))text+=std::string(names[i])+"  ";
      text+="\n\n";
      const char* axes[]={"Left X","Left Y","Right X","Right Y","Left trigger","Right trigger"};
      for(int i=0;i<6;++i){snprintf(line,sizeof(line),"%s: %d\n",axes[i],pad?SDL_GameControllerGetAxis(pad,static_cast<SDL_GameControllerAxis>(i)):0);text+=line;}
      text+="\nHold START + BACK for one second to return.";
      bool exit=pad&&SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_START)&&SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_BACK);
      if(exit){if(!held)held=SDL_GetTicks();if(SDL_GetTicks()-held>=1000){SubAppRouter::getInstance()->pop();return;}}else held=0;
    } else {
      snprintf(line,sizeof(line),"Kernel: %u.%u.%u.%u\n",XboxKrnlVersion.Major,XboxKrnlVersion.Minor,XboxKrnlVersion.Build,XboxKrnlVersion.Qfe);text+=line;
      MM_STATISTICS memory={};memory.Length=sizeof(memory);
      if(NT_SUCCESS(MmQueryStatistics(&memory))){snprintf(line,sizeof(line),"RAM: %lu MiB total / %lu KiB available\n",memory.TotalPhysicalPages/256,memory.AvailablePages*4);text+=line;}
      VIDEO_MODE mode=XVideoGetMode();snprintf(line,sizeof(line),"Video: %lu x %lu\n",static_cast<unsigned long>(mode.width),static_cast<unsigned long>(mode.height));text+=line;
      for(char drive:std::string("CEFG")){char path[]={drive,':','\\',0};ULARGE_INTEGER free={},total={};if(GetDiskFreeSpaceExA(path,&free,&total,nullptr)){snprintf(line,sizeof(line),"%c: %llu MiB free / %llu MiB total\n",drive,static_cast<unsigned long long>(free.QuadPart/1048576),static_cast<unsigned long long>(total.QuadPart/1048576));text+=line;}}
      text+="\nBoard revision is not inferred from kernel version.\nB: return";
    }
    SDL_FRect box={renderer.getWidth()*0.06f,renderer.getHeight()*0.06f,renderer.getWidth()*0.88f,renderer.getHeight()*0.88f};
    renderer.setDrawColor(0,0,0,235);renderer.fillRectangle(box);
    font.drawColumnScaled(text,{box.x+12,box.y+12},box.w-24,0.75f);
  }
};
}
void controller(Menu*m){SubAppRouter::getInstance()->push(std::make_shared<Screen>(m->getRenderer(),true));}
void hardware(Menu*m){SubAppRouter::getInstance()->push(std::make_shared<Screen>(m->getRenderer(),false));}
}
