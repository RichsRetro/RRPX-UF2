#include "startupProgress.hpp"
/* SPDX-License-Identifier: MIT. Session-only tools; no full backup or C writes. */
#include "setupMenu.hpp"
#include "setupBackup.hpp"
#include "setupReport.hpp"
#include "runtimePatches.hpp"
#include "setupTransaction.hpp"
#include "xbeLauncher.hpp"
#include "menu.hpp"
#include "subAppRouter.hpp"
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <atomic>
#include <mutex>
#include <array>
#include <algorithm>
#include <cstdio>
namespace setup {
namespace {
std::string dumpEeprom(){
  std::array<unsigned char,256> a,b;
  for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<256;++i){ULONG v=0;if(HalReadSMBusValue(0xa8,static_cast<UCHAR>(i),FALSE,&v))return "EEPROM read failed; no dump written";(pass?b:a)[i]=static_cast<unsigned char>(v);}
  if(a!=b||std::all_of(a.begin(),a.end(),[](unsigned char v){return v==0;})||std::all_of(a.begin(),a.end(),[](unsigned char v){return v==255;}))return "EEPROM reads invalid or disagree; no dump written";
  CreateDirectoryA("E:\\RRXD-Lite-Setup",nullptr);std::string folder;
  for(unsigned i=0;i<1000;++i){char n[80];snprintf(n,sizeof(n),"E:\\RRXD-Lite-Setup\\EEPROM-%03u",i);if(CreateDirectoryA(n,nullptr)){folder=n;break;}}
  if(folder.empty())return "Could not reserve a new EEPROM dump folder";
  std::string file=folder+"\\eeprom.bin";HANDLE h=CreateFileA(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(h==INVALID_HANDLE_VALUE)return "Cannot create EEPROM dump; earlier dumps preserved";
  DWORD wrote=0;bool ok=WriteFile(h,a.data(),DWORD(a.size()),&wrote,nullptr)&&wrote==a.size();IO_STATUS_BLOCK io={};if(ok)ok=NT_SUCCESS(NtFlushBuffersFile(h,&io));if(!CloseHandle(h))ok=false;
  uint64_t bytes=0;auto expected=transaction::digest(std::string(reinterpret_cast<const char*>(a.data()),a.size()));
  if(!ok||hashFile(file,&bytes)!=expected||bytes!=256)return "EEPROM dump verification failed; partial file preserved";
  return "EEPROM saved and SHA256 verified: "+file+". Download this file to the PC using FTP.";
}
class Screen:public SubApp {
 Renderer& renderer;bool sessionPage;HANDLE worker=nullptr;std::atomic<bool> busy{false};std::mutex lock;
 std::string message="Session patches need no C: backup. EEPROM export is optional.",pendingLaunch;int job=0;bool leaveAfter=false;
 void update(const std::string&s){std::lock_guard<std::mutex>g(lock);message=s;}
 static DWORD WINAPI run(void*p){auto*self=static_cast<Screen*>(p);self->work();self->busy=false;return 0;}
 void work(){
  if(job==2){update(dumpEeprom());return;}if(job==3){update(saveCompatibilityReport());return;}
  auto r=job==0?stageRuntimePatches():restoreRuntimeStage();
  {std::lock_guard<std::mutex>g(lock);if(r.ok&&!r.launchPath.empty())pendingLaunch=r.launchPath;message=r.message;}
 }
 void start(int next){if(busy)return;if(worker){WaitForSingleObject(worker,INFINITE);CloseHandle(worker);worker=nullptr;}job=next;leaveAfter=false;busy=true;
  update(next==0?"Staging the session patcher; preserving only replaced E: files...":next==1?"Verifying and undoing the recorded E: session files...":next==2?"Reading EEPROM twice and saving a verified 256-byte dump...":"Reading console information...");
  worker=CreateThread(nullptr,64*1024,run,this,0,nullptr);if(!worker){busy=false;update("Could not start the tools worker");}
 }
public:
 explicit Screen(Renderer&r,bool session):renderer(r),sessionPage(session){if(!startupProgress::lastError().empty())message=startupProgress::lastError();}
 ~Screen(){if(worker){WaitForSingleObject(worker,INFINITE);CloseHandle(worker);}}
 void beginAutomatic(){start(0);}
 void onAPressed()override{start(sessionPage?0:2);}
 void onBlackPressed()override{start(0);}
 void onWhitePressed()override{start(1);}
 void onYPressed()override{start(2);}
 void onXPressed()override{start(3);}
 void onBPressed()override{if(busy){leaveAfter=true;update("Finishing the current operation, then returning without launching");return;}SubAppRouter::getInstance()->pop();}
 void onBackPressed()override{onBPressed();}
 void render(Font&font)override{
  if(!busy&&leaveAfter){SubAppRouter::getInstance()->pop();return;}
  std::string launch,status;{std::lock_guard<std::mutex>g(lock);status=message;if(!busy){launch=pendingLaunch;pendingLaunch.clear();}}
  if(!launch.empty()){XBELauncher::launch(launch);return;}
  std::string text=sessionPage?"RRPXUF2 Retail - temporary session\n\nA / BLACK  Apply session patches and relaunch RRXD\n":"RRPXUF2 Retail - EEPROM and tools\n\nA  Save verified EEPROM dump\nBLACK  Apply session patches and relaunch RRXD\n";
  text+="WHITE  Undo recorded session files\nY  Save verified EEPROM dump\nX  Save compatibility report\nB  Return\n\n";
  text+=status+"\n\n";
  text+="Temporary edition - no permanent softmod installation.\nOnly our E: session files are staged and recorded.\nRetail C: startup stays unchanged.\n\nRemove Pico and cold boot for normal retail operation.\nFinish FTP transfers first. Keep power on while copying.\n";
  SDL_FRect box={renderer.getWidth()*0.06f,renderer.getHeight()*0.06f,renderer.getWidth()*0.88f,renderer.getHeight()*0.88f};renderer.setDrawColor(0,0,0,235);renderer.fillRectangle(box);font.drawColumnScaled(text,{box.x+12,box.y+12},box.w-24,0.72f);
 }
};
}
void automatic(Menu*m){if(!shouldAutoStartSession())return;auto screen=std::make_shared<Screen>(m->getRenderer(),true);SubAppRouter::getInstance()->push(screen);screen->beginAutomatic();}
void open(Menu*m){SubAppRouter::getInstance()->push(std::make_shared<Screen>(m->getRenderer(),false));}
void session(Menu*m){SubAppRouter::getInstance()->push(std::make_shared<Screen>(m->getRenderer(),true));}
}
