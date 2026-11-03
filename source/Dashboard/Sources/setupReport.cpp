/* SPDX-License-Identifier: MIT. Read-only compatibility survey and E: report. */
#include "setupReport.hpp"
#include "setupBackup.hpp"
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <cstdio>
namespace setup {
std::string saveCompatibilityReport() {
  using json=nlohmann::json;
  const char* expected="3ae2f3eae3917e0130d1e1a5c8e1c5df207ea31703646a174d98f7be9a769495";
  char kernel[64];snprintf(kernel,sizeof(kernel),"%u.%u.%u.%u",XboxKrnlVersion.Major,XboxKrnlVersion.Minor,XboxKrnlVersion.Build,XboxKrnlVersion.Qfe);
  const unsigned supported[]={3944,4034,4817,5101,5530,5713,5838};bool known=false;
  for(unsigned build:supported)if(XboxKrnlVersion.Major==1&&XboxKrnlVersion.Minor==0&&XboxKrnlVersion.Build==build&&XboxKrnlVersion.Qfe==1)known=true;
  json candidates=json::array();bool found=false;
  for(const char* p:{"C:\\xboxdash.xbe","C:\\xodash\\xonlinedash.xbe","C:\\dashupdate.xbe"}) {
    uint64_t bytes=0;auto hash=hashFile(p,&bytes);bool match=hash==expected;found=found||match;
    candidates.push_back({{"path",p},{"readable",!hash.empty()},{"bytes",bytes},{"sha256",hash},{"known_loader_hash_match",match}});
  }
  json indicators=json::array();
  for(const char* p:{"C:\\nkpatcher\\default.xbe","C:\\fonts\\bert.xtf","C:\\fonts\\ernie.xtf","E:\\NKPatcher\\default.xbe","C:\\shadowc.img"})
    if(GetFileAttributesA(p)!=INVALID_FILE_ATTRIBUTES)indicators.push_back(p);
  auto backup=findLatestBackup("E:\\RRXD-Lite-Setup");
  json report={{"schema",1},{"kernel",kernel},{"known_kernel_profile",known},{"known_loader_hash_found",found},
    {"loader_candidates",candidates},{"existing_mod_indicators",indicators},{"latest_backup_candidate",backup},
    {"backup_reverified_by_this_report",false},{"activation_available",false},
    {"blocking_reason","Permanent boot profile, protection and recovery are not hardware-validated"}};
  CreateDirectoryA("E:\\RRXD-Lite-Setup",nullptr);CreateDirectoryA("E:\\RRXD-Lite-Setup\\Reports",nullptr);
  std::string text=report.dump(2),destination;
  for(unsigned i=0;i<1000;++i) {
    char name[100];snprintf(name,sizeof(name),"E:\\RRXD-Lite-Setup\\Reports\\Report-%03u.json",i);
    HANDLE h=CreateFileA(name,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)continue;
    DWORD written=0;bool ok=WriteFile(h,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size();
    IO_STATUS_BLOCK io;if(ok)ok=NT_SUCCESS(NtFlushBuffersFile(h,&io));if(!CloseHandle(h))ok=false;
    if(!ok)return "Could not finish writing the compatibility report";
    destination=name;break;
  }
  if(destination.empty())return "Could not create a new compatibility report";
  return std::string("Kernel profile: ")+(known?"known":"unknown")+"; boot-loader hash: "+(found?"found":"not found")+
    ". Saved "+destination+". Permanent boot is not enabled.";
}
}
