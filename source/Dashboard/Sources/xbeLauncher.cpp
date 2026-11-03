#include "xbeLauncher.hpp"

#include "infoLog.hpp"
#include <cstdio>

#ifdef NXDK
#include <windows.h>

#include <hal/video.h>

#include <hal/fileio.h>

#include <hal/xbox.h>

#include <xboxkrnl/xboxkrnl.h>

#endif

void XBELauncher::shutdown() {

#ifdef NXDK

  HalInitiateShutdown();

#endif

}

void XBELauncher::exitToDashboard() {

  showLaunchImage();

  // TODO: Switch to XLaunchXBE(nullptr) if XboxDev/nxdk#501 is merged.

  exit(0);

}

namespace { std::string pendingLaunchPath, launchTracePath; }

bool XBELauncher::hasPendingLaunch() { return !pendingLaunchPath.empty(); }

void XBELauncher::launch(std::string const& xbePath) {

  // Leave the event handler before shutting down the dashboard's services.

  if (!xbePath.empty() && xbePath.size() < 240) {pendingLaunchPath = xbePath;launchTracePath.clear();noteLaunchStage("queued");}

}

void XBELauncher::launchPending() {

  const std::string xbePath = pendingLaunchPath;

  noteLaunchStage("launchPending entered");
  pendingLaunchPath.clear();

#ifdef NXDK

  // Retail titles access their assets through D:, including HDD-installed games.

  // Resolve the original path before remapping D: so DVD launches stay native.

  if (xbePath.empty() || xbePath.size() >= 240) {

    InfoLog::outputLine(InfoLog::ERROR, "Launch path is empty or too long.\n");

    return;

  }

  char nativePath[512] = {};

  if (XConvertDOSFilenameToXBOX(xbePath.c_str(), nativePath) != 0) {

    InfoLog::outputLine(InfoLog::ERROR, "Could not resolve launch path.\n");

    return;

  }

  std::string launchPath(nativePath);

  if (launchPath.compare(0, 17, "\\Device\\Harddisk0") == 0) {

    auto separator = launchPath.find_last_of('\\');

    if (separator == std::string::npos) return;

    std::string directory = launchPath.substr(0, separator);

    ANSI_STRING drive, target;

    RtlInitAnsiString(&drive, "\\??\\D:");

    RtlInitAnsiString(&target, directory.c_str());

    IoDeleteSymbolicLink(&drive);

    NTSTATUS status = IoCreateSymbolicLink(&drive, &target);

    if (!NT_SUCCESS(status)) {

      ANSI_STRING dvd;

      RtlInitAnsiString(&dvd, "\\Device\\CdRom0");

      IoCreateSymbolicLink(&drive, &dvd);

      InfoLog::outputLine(InfoLog::ERROR, "Could not map game directory to D: (%08lx).\n", (unsigned long)status);

      return;

    }

  }

  showLaunchImage();

  noteLaunchStage("calling XLaunchXBE quick reboot");
  XLaunchXBE(launchPath.c_str());
  noteLaunchStage("XLaunchXBE returned unexpectedly");

  // A failed launch must return to a visible dashboard rather than a black screen.

  XVideoSetVideoEnable(TRUE);

  InfoLog::outputLine(InfoLog::ERROR, "The Xbox returned without launching the executable.\n");

#endif

}

void XBELauncher::showLaunchImage() {

#ifdef NXDK

  // SDL has already released its surfaces. Do not clear a possibly freed FB,
  // and never leave the TV encoder disabled for a non-nxdk target such as HeXEn.
  XVideoSetVideoEnable(TRUE);
#endif

}


void XBELauncher::noteLaunchStage(const char* stage) {
#ifdef NXDK
  if(launchTracePath.empty()){
    if(pendingLaunchPath.empty())return;
    CreateDirectoryA("E:\\RRXD-Lite-Setup",nullptr);
    CreateDirectoryA("E:\\RRXD-Lite-Setup\\LaunchLogs",nullptr);
    for(unsigned i=0;i<1000;++i){char name[100];snprintf(name,sizeof(name),"E:\\RRXD-Lite-Setup\\LaunchLogs\\Launch-%03u.log",i);HANDLE h=CreateFileA(name,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h!=INVALID_HANDLE_VALUE){CloseHandle(h);launchTracePath=name;break;}}
    if(launchTracePath.empty())return;
    HANDLE h=CreateFileA(launchTracePath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h!=INVALID_HANDLE_VALUE){
      unsigned address=0;switch(XboxKrnlVersion.Build){case 3944:case 4034:address=0x8002fd80;break;case 4817:address=0x8002eb60;break;case 5101:address=0x8002eba0;break;case 5530:case 5713:case 5838:address=0x8002ef6f;break;}
      unsigned value=0;if(address&&XboxKrnlVersion.Major==1&&XboxKrnlVersion.Minor==0&&XboxKrnlVersion.Qfe==1)value=*reinterpret_cast<volatile unsigned char*>(address);else address=0;
      char line[512];int n=snprintf(line,sizeof(line),"target=%s\r\nkernel=%u.%u.%u.%u loader_check_address=%08x byte=%02x NKP_expected=eb (single checkpoint only)\r\n",pendingLaunchPath.c_str(),XboxKrnlVersion.Major,XboxKrnlVersion.Minor,XboxKrnlVersion.Build,XboxKrnlVersion.Qfe,address,value);DWORD wrote=0;WriteFile(h,line,DWORD(n),&wrote,nullptr);IO_STATUS_BLOCK io={};NtFlushBuffersFile(h,&io);CloseHandle(h);
    }
  }
  HANDLE h=CreateFileA(launchTracePath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return;
  LARGE_INTEGER end={};if(SetFilePointerEx(h,end,nullptr,FILE_END)){char line[160];int n=snprintf(line,sizeof(line),"%lu %s\r\n",static_cast<unsigned long>(GetTickCount()),stage);DWORD wrote=0;WriteFile(h,line,DWORD(n),&wrote,nullptr);IO_STATUS_BLOCK io={};NtFlushBuffersFile(h,&io);}CloseHandle(h);
#else
  (void)stage;
#endif
}
