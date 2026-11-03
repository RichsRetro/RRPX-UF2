/* SPDX-License-Identifier: MIT. Early E-only staging, before recovery/splash UI. */
#include "startupProgress.hpp"
#include "retailBootArt.hpp"
#include "runtimePatches.hpp"
#include "xbeLauncher.hpp"
#include "dashboardPaths.hpp"
#include "3rdparty/json.hpp"
#include <SDL.h>
#include <windows.h>
#include <hal/video.h>
#include <atomic>
namespace startupProgress {
namespace {
std::string failure;bool recovery=false;
bool enabled(){
 // Read only the one bounded startup preference. Full Config construction is
 // deliberately AFTER the independent recovery selector, so broken menu or
 // settings content cannot take away recovery access.
 HANDLE file=CreateFileA(dashboardConfigPath().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)return true;failure="Cannot read startup preference; automatic staging skipped.";return false;}
 LARGE_INTEGER size={};bool ok=GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=512*1024;
 std::string bytes(ok?size_t(size.QuadPart):0,'\0');DWORD received=0;
 if(ok)ok=ReadFile(file,&bytes[0],DWORD(bytes.size()),&received,nullptr)&&received==bytes.size();
 if(!CloseHandle(file))ok=false;
 if(!ok){failure="Startup configuration could not be read safely; automatic staging skipped.";return false;}
 const auto config=nlohmann::json::parse(bytes,nullptr,false);
 if(!config.is_object()){failure="Startup configuration is invalid; recovery remains available.";return false;}
 if(config.contains("settings")&&config["settings"].is_object()){
  const auto&settings=config["settings"];
  if(settings.contains("automatic_session_patches")){
   if(!settings["automatic_session_patches"].is_boolean()){failure="Startup patch preference is invalid; automatic staging skipped.";return false;}
   return settings["automatic_session_patches"].get<bool>();
  }
 }
 return true;
}
struct Job {std::atomic<bool> done{false};setup::RuntimeResult result;};
DWORD WINAPI stage(void* ptr){auto*job=static_cast<Job*>(ptr);job->result=setup::stageRuntimePatches();job->done=true;return 0;}
SDL_GameController* pad=nullptr;
void input(){
 SDL_Event event;while(SDL_PollEvent(&event)){
  if(event.type==SDL_CONTROLLERDEVICEADDED&&!pad&&SDL_IsGameController(event.cdevice.which))pad=SDL_GameControllerOpen(event.cdevice.which);
  if(event.type==SDL_CONTROLLERDEVICEREMOVED&&pad&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))==event.cdevice.which){SDL_GameControllerClose(pad);pad=nullptr;}
 }
 SDL_GameControllerUpdate();
 if(pad&&SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_START)&&SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_BACK))recovery=true;
}
void draw(unsigned ticks,bool complete=false){
 // The early framebuffer is valid only before the dashboard creates SDL video.
 // Never reuse this pointer after normal video initialization or during launch.
 const auto mode=XVideoGetMode();
 if(mode.width==640&&mode.height==480&&mode.bpp==32){
  retailBootArt::draw(reinterpret_cast<uint32_t*>(XVideoGetFB()),640,480,ticks,complete);
  XVideoFlushFB();
 }
}
void releaseInput(){if(pad){SDL_GameControllerClose(pad);pad=nullptr;}SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);}
}
const std::string& lastError(){return failure;}
bool recoveryRequested(){return recovery;}
void run(){
 if(!setup::shouldAutoStartSession()||!enabled())return;
 if(!XVideoSetMode(640,480,32,REFRESH_DEFAULT)){failure="Early loading display could not start. Apply session patches manually from tools.";return;}
 XVideoSetVideoEnable(TRUE);draw(0);
 if(SDL_Init(SDL_INIT_GAMECONTROLLER)){failure="Controller initialization failed. Automatic staging skipped; use recovery or session tools.";return;}
 for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)){pad=SDL_GameControllerOpen(i);if(pad)break;}
 // Preserve a recovery bypass before any staging. Further requests wait for
 // the E: transaction to finish safely, then enter recovery without launching.
 const DWORD started=GetTickCount();
 while(GetTickCount()-started<600){input();draw(GetTickCount()-started);if(recovery){releaseInput();return;}Sleep(16);}
 Job job;HANDLE worker=CreateThread(nullptr,64*1024,stage,&job,0,nullptr);
 if(!worker){failure="Could not start early session staging; use Session tools.";releaseInput();return;}
 while(!job.done.load()){input();draw(GetTickCount()-started);Sleep(16);}
 WaitForSingleObject(worker,INFINITE);CloseHandle(worker);
 draw(GetTickCount()-started,true);Sleep(100);releaseInput();
 if(recovery)return;
 if(!job.result.ok||job.result.launchPath.empty()){failure=job.result.message;return;}
 // No network, DVD, artwork, scanner or GUI workers have started yet.
 // Keep the encoder enabled, as in the hardware-confirmed DVD handoff fix.
 XBELauncher::launch(job.result.launchPath);XBELauncher::launchPending();
 failure="Session patcher launch returned unexpectedly. Recovery and manual tools remain available.";
}
}
