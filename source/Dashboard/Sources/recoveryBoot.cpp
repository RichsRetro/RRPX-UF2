/* SPDX-License-Identifier: MIT. Asset-independent RRXD recovery entry. */
#include "recoveryBoot.hpp"
extern "C" {
#include "recoveryFtp.h"
}
#include <SDL.h>
#include <windows.h>
#include <hal/debug.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <xboxkrnl/xboxkrnl.h>
#include <nxdk/mount.h>
#include <nxdk/net.h>
#include <lwip/netif.h>
#include <atomic>
#include <cstdio>
extern "C" { extern struct netif* g_pnetif; }
namespace recovery {
namespace {
std::atomic<int> network{0};
char address[48]="";
DWORD WINAPI serve(void*) {
  if(nxNetInit(nullptr)) { network=-1;return 0; }
  snprintf(address,sizeof(address),"%s",ip4addr_ntoa(netif_ip4_addr(g_pnetif)));
  network=1;
  if(ftp_run(nullptr,21))network=-1;
  return 0;
}
SDL_GameController* firstPad() {
  for(int i=0;i<SDL_NumJoysticks();++i)
    if(SDL_IsGameController(i))return SDL_GameControllerOpen(i);
  return nullptr;
}
bool combo(SDL_GameController* pad) {
  return pad&&SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_START)&&
    SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_BACK);
}
void run(SDL_GameController* pad) {
  const char letters[]="CEFGXYZ";const unsigned parts[]={2,1,6,7,3,4,5};
  for(unsigned i=0;i<7;++i)if(!nxIsDriveMounted(letters[i])) {
    char native[64];snprintf(native,sizeof(native),"\\Device\\Harddisk0\\Partition%u",parts[i]);nxMountDrive(letters[i],native);
  }
  if(!nxIsDriveMounted('R'))nxMountDrive('R',"\\Device\\CdRom0");
  ftp_prepare();
  HANDLE thread=CreateThread(nullptr,0,serve,nullptr,0,nullptr);
  if(thread)CloseHandle(thread);else network=-1;
  bool info=false;DWORD last=0;
  for(;;) {
    SDL_Event event;
    while(SDL_PollEvent(&event)) {
      if(event.type==SDL_CONTROLLERDEVICEADDED&&!pad)pad=firstPad();
      if(event.type==SDL_CONTROLLERDEVICEREMOVED&&pad&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))==event.cdevice.which) { SDL_GameControllerClose(pad);pad=nullptr; }
      if(event.type==SDL_CONTROLLERBUTTONDOWN) {
        if(event.cbutton.button==SDL_CONTROLLER_BUTTON_X)info=!info;
        if(event.cbutton.button==SDL_CONTROLLER_BUTTON_B&&ftp_client_count()==0&&ftp_transfer_count()==0) { ftp_stop();XReboot(); }
        last=0;
      }
    }
    DWORD now=GetTickCount();
    if(!last||now-last>=200) {
      debugClearScreen();debugResetCursor();
      debugPrint("\n  RRXD LITE - RECOVERY 0.7\n\n");
      debugPrint("  Runs without dashboard settings, fonts or artwork.\n\n");
      int state=network.load();
      if(state==1&&g_pnetif)ip4addr_ntoa_r(netif_ip4_addr(g_pnetif),address,sizeof(address));
      debugPrint("  FTP: %s\n",state<0?"Unavailable":ftp_is_listening()?"Running":"Starting / waiting for Ethernet");
      debugPrint("  Address: %s\n",state==1?address:"Waiting for network");
      debugPrint("  Port: 21   Login: xbox / xbox   Passive FTP\n");
      debugPrint("  Clients: %d   Transfers: %d\n\n",ftp_client_count(),ftp_transfer_count());
      debugPrint("  Backups: /E/RRXD-Lite-Setup/Backup-NNN\n");
      debugPrint("  File repair uses the FTP client on your PC.\n\n");
      debugPrint("  X: toggle diagnostics\n  B: restart (disconnect FTP clients first)\n\n");
      debugPrint("  Kernel: %u.%u.%u.%u\n",XboxKrnlVersion.Major,XboxKrnlVersion.Minor,XboxKrnlVersion.Build,XboxKrnlVersion.Qfe);
      if(info) {
        MM_STATISTICS memory={};memory.Length=sizeof(memory);
        if(NT_SUCCESS(MmQueryStatistics(&memory)))debugPrint("  RAM: %lu MiB total, %lu KiB free\n",memory.TotalPhysicalPages/256,memory.AvailablePages*4);
        if(pad) {
          debugPrint("  Pad: %s\n",SDL_GameControllerName(pad));
          debugPrint("  LX %d  LY %d  RX %d  RY %d\n",SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX),SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTY),SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_RIGHTX),SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_RIGHTY));
        } else debugPrint("  Controller: disconnected\n");
      }
      debugPrint("\n  Permanent softmod activation is not enabled.\n");
      last=now;
    }
    Sleep(10);
  }
}
}
void bootSelector(bool force) {
  XVideoSetMode(640,480,32,REFRESH_DEFAULT);
  debugClearScreen();debugResetCursor();
  debugPrint("\n\n  RRXD Lite\n\n  Hold START + BACK for recovery.\n  Dashboard starts in two seconds.\n");
  if(SDL_Init(SDL_INIT_GAMECONTROLLER)) { SDL_Quit();return; }
  SDL_GameController* pad=firstPad();DWORD start=GetTickCount();bool selected=force;
  while(!selected&&GetTickCount()-start<2000) {
    SDL_Event event;while(SDL_PollEvent(&event)) {
      if(event.type==SDL_CONTROLLERDEVICEADDED&&!pad)pad=firstPad();
      if(event.type==SDL_CONTROLLERDEVICEREMOVED&&pad&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))==event.cdevice.which) { SDL_GameControllerClose(pad);pad=nullptr; }
    }
    SDL_GameControllerUpdate();if(combo(pad)) { selected=true;break; }Sleep(10);
  }
  if(selected)run(pad);
  if(pad)SDL_GameControllerClose(pad);
  // Keep the USB/input subsystem alive; the dashboard adds video/font services.
}
}
