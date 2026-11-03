/* SPDX-License-Identifier: MIT. Stage separate GPL executable; E: only.
 * NKPatcher implementation remains a separate process under its GPL licence.
 */
#include "runtimePatches.hpp"
#include "sessionIdentity.hpp"
#include "setupBackup.hpp"
#include "setupTransaction.hpp"
#include "bootComponent.hpp"
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <nxdk/path.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
namespace setup {
namespace {
using json=nlohmann::json;
bool toNative(const std::string& relative,std::string& native){
  if(relative.size()<3||relative.size()>230||relative[0]!='E'||relative[1]!='/'||relative.find('\\')!=std::string::npos||relative.find(':')!=std::string::npos)return false;
  size_t at=2;while(at<relative.size()){auto end=relative.find('/',at);auto part=relative.substr(at,end==std::string::npos?relative.size()-at:end-at);if(part.empty()||part=="."||part==".."||part.size()>42)return false;for(unsigned char c:part)if(c<32||c==127)return false;if(end==std::string::npos)break;at=end+1;}
  if(relative.compare(0,12,"E/RRXD-Lite/")&&relative.compare(0,18,"E/RRXD-Lite-Setup/"))return false;
  native="E:"+relative.substr(1);std::replace(native.begin(),native.end(),'/','\\');return true;
}
bool parents(const std::string& native){
  for(size_t i=3;i<native.size();++i)if(native[i]=='\\'){
    auto parent=native.substr(0,i);if(!CreateDirectoryA(parent.c_str(),nullptr)){DWORD a=GetFileAttributesA(parent.c_str());if(a==INVALID_FILE_ATTRIBUTES||!(a&FILE_ATTRIBUTE_DIRECTORY))return false;}
  }return true;
}
bool readNative(const std::string& p,std::string& data){
  HANDLE h=CreateFileA(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(h==INVALID_HANDLE_VALUE)return false;data.clear();std::vector<char> block(65536);bool ok=true;
  for(;;){DWORD n=0;if(!ReadFile(h,block.data(),DWORD(block.size()),&n,nullptr)){ok=false;break;}if(!n)break;data.append(block.data(),n);if(data.size()>8*1024*1024){ok=false;break;}}
  if(!CloseHandle(h))ok=false;return ok;
}
class EStore:public transaction::Store {
public:
  bool exists(const std::string&p)override{std::string n;return toNative(p,n)&&GetFileAttributesA(n.c_str())!=INVALID_FILE_ATTRIBUTES;}
  bool read(const std::string&p,std::string&data)override{std::string n;return toNative(p,n)&&readNative(n,data);}
  bool writeNew(const std::string&p,const std::string&data)override{
    std::string n;if(!toNative(p,n)||!parents(n)||data.size()>8*1024*1024)return false;
    HANDLE h=CreateFileA(n.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=WriteFile(h,data.data(),static_cast<DWORD>(data.size()),&written,nullptr)&&written==data.size();
    IO_STATUS_BLOCK io;if(ok)ok=NT_SUCCESS(NtFlushBuffersFile(h,&io));if(!CloseHandle(h))ok=false;return ok;
  }
  bool replace(const std::string&from,const std::string&to)override{
    std::string a,b;if(!toNative(from,a)||!toNative(to,b)||!parents(b))return false;
    ANSI_STRING path;RtlInitAnsiString(&path,a.c_str());OBJECT_ATTRIBUTES attrs;InitializeObjectAttributes(&attrs,&path,OBJ_CASE_INSENSITIVE,ObDosDevicesDirectory(),nullptr);
    IO_STATUS_BLOCK io;HANDLE h;NTSTATUS status=NtOpenFile(&h,DELETE|SYNCHRONIZE|FILE_WRITE_DATA,&attrs,&io,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,FILE_SYNCHRONOUS_IO_NONALERT|FILE_OPEN_FOR_BACKUP_INTENT);
    if(!NT_SUCCESS(status))return false;
    FILE_RENAME_INFORMATION rename={};rename.ReplaceIfExists=TRUE;rename.RootDirectory=ObDosDevicesDirectory();RtlInitAnsiString(&rename.FileName,b.c_str());
    status=NtSetInformationFile(h,&io,&rename,sizeof(rename),FileRenameInformation);
    if(NT_SUCCESS(status))status=NtFlushBuffersFile(h,&io);NTSTATUS closed=NtClose(h);return NT_SUCCESS(status)&&NT_SUCCESS(closed);
  }
  bool erase(const std::string&p)override{std::string n;return toNative(p,n)&&DeleteFileA(n.c_str());}
};
RuntimeResult fail(const std::string&text){RuntimeResult r;r.message=text;return r;}
std::string latestTransaction();
bool identity(transaction::Context& context,std::string& error){
  std::array<unsigned char,256> first,second;
  for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<256;++i){ULONG v=0;if(HalReadSMBusValue(0xa8,static_cast<UCHAR>(i),FALSE,&v)){error="EEPROM read failed";return false;}(pass?second:first)[i]=static_cast<unsigned char>(v);}
  if(first!=second||std::all_of(first.begin(),first.end(),[](unsigned char c){return c==0;})||std::all_of(first.begin(),first.end(),[](unsigned char c){return c==255;})){error="EEPROM identity could not be verified";return false;}
  const std::string current(reinterpret_cast<const char*>(first.data()),first.size());
  context.identity=sessionIdentity::stable(current);
  // Keep legacy identity strings intact: existing immutable undo journals remain valid.
  EStore store;std::string record;
  std::string recordPath="E/RRXD-Lite-Setup/runtime-owned.json";
  if(!store.exists(recordPath)){
    const auto last=latestTransaction();
    recordPath=last.empty()?"":"E/RRXD-Lite-Setup/Transactions/"+last+"/journal.json";
  }
  if(!recordPath.empty()&&store.exists(recordPath)){
    if(!store.read(recordPath,record)){error="Cannot read existing session identity record";return false;}
    auto j=json::parse(record,nullptr,false);
    if(!j.is_object()||!j.contains("identity")||!j["identity"].is_string()){error="Invalid session identity record; preserved";return false;}
    const std::string recorded=j["identity"];
    bool match=sessionIdentity::accepts(current,recorded,"");
    if(!match){
      WIN32_FIND_DATAA e;HANDLE h=FindFirstFileA("E:\\RRXD-Lite-Setup\\*",&e);
      if(h!=INVALID_HANDLE_VALUE){do{
        const std::string n=e.cFileName;
        const auto dash=n.find('-');
        if(!(e.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||dash==std::string::npos||
          (n.substr(0,dash)!="Backup"&&n.substr(0,dash)!="EEPROM")||n.size()!=dash+4||
          !std::all_of(n.begin()+dash+1,n.end(),[](char c){return c>='0'&&c<='9';}))continue;
        std::string historical;
        if(readNative("E:\\RRXD-Lite-Setup\\"+n+"\\eeprom.bin",historical)&&sessionIdentity::accepts(current,recorded,historical)){match=true;break;}
      }while(FindNextFileA(h,&e));FindClose(h);}
    }
    if(!match){error="Session record differs: matching original EEPROM proof required; files preserved";return false;}
    context.identity=recorded;
  }
  // Session identity and per-file journal checks replace the unrelated full-C backup gate.
  context.sessionIdentityVerified=true;return true;
}
std::string latestTransaction(){
  WIN32_FIND_DATAA e;HANDLE h=FindFirstFileA("E:\\RRXD-Lite-Setup\\Transactions\\*",&e);if(h==INVALID_HANDLE_VALUE)return "";std::string latest;
  do{std::string name=e.cFileName;if(!(e.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||name.size()!=6||name.compare(0,3,"Tx-")||!std::all_of(name.begin()+3,name.end(),[](char c){return c>='0'&&c<='9';}))continue;
    std::string p="E:\\RRXD-Lite-Setup\\Transactions\\"+name;
    if(GetFileAttributesA((p+"\\journal.json").c_str())!=INVALID_FILE_ATTRIBUTES&&GetFileAttributesA((p+"\\restored.marker").c_str())==INVALID_FILE_ATTRIBUTES&&name>latest)latest=name;
  }while(FindNextFileA(h,&e));FindClose(h);return latest;
}
}
bool shouldAutoStartSession(){
  char path[MAX_PATH]={};nxGetCurrentXbeNtPath(path);std::string lower=path;
  std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
  if(lower.empty()||lower.find("\\rrxd-lite\\default.xbe")!=std::string::npos)return false;
  // Automatic mode is for the Pico payload, not recovery or arbitrary HDD copies.
  const auto slash=lower.find_last_of('\\');return slash!=std::string::npos&&lower.substr(slash+1)=="payload.xbe";
}
RuntimeResult stageRuntimePatches(){
  const unsigned supported[]={3944,4034,4817,5101,5530,5713,5838};bool known=false;
  for(unsigned b:supported)if(XboxKrnlVersion.Major==1&&XboxKrnlVersion.Minor==0&&XboxKrnlVersion.Build==b&&XboxKrnlVersion.Qfe==1)known=true;
  if(!known)return fail("This exact kernel is not supported by the bundled patcher");
  char native[MAX_PATH]={};nxGetCurrentXbeNtPath(native);std::string lower=native;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
  if(lower.find("\\rrxd-lite\\default.xbe")!=std::string::npos)return fail("Already launched through the runtime chain; test the disc or cold-reboot before retrying");
  transaction::Context context;std::string error;if(!identity(context,error))return fail(error);
  EStore store;auto last=latestTransaction();
  if(!last.empty()&&!store.exists("E/RRXD-Lite-Setup/Transactions/"+last+"/committed.marker"))return fail("An interrupted stage exists. WHITE restores its original files first");
  std::string previous;json owner;
  if(store.exists("E/RRXD-Lite-Setup/runtime-owned.json")){
    if(!store.read("E/RRXD-Lite-Setup/runtime-owned.json",previous))return fail("Cannot read the previous staging record");
    owner=json::parse(previous,nullptr,false);
    if(!owner.is_object()||!owner.contains("identity")||owner["identity"]!=context.identity||!owner.contains("files")||!owner["files"].is_object())return fail("Previous staging record does not match this console");
  }
  const char* filename=strrchr(native,'\\');std::string app;
  if(!filename||!readNative(std::string("A:\\")+(filename+1),app))return fail("Cannot read the running RRXD application");
  auto application=std::make_shared<const std::string>(std::move(app));
  auto patcher=std::make_shared<const std::string>(reinterpret_cast<const char*>(boot_component),boot_component_size);
  if(transaction::digest(*patcher)!=boot_component_sha256)return fail("Bundled GPL patcher integrity check failed");
  if(owner.is_object()){
    const std::string paths[]={"E/RRXD-Lite/default.xbe","E/RRXD-Lite/nkpatcher.xbe","E/RRXD-Lite-Setup/recovery.xbe"};
    bool reusable=!last.empty();
    for(unsigned i=0;i<3;++i){
      const auto expected=transaction::digest(i==1?*patcher:*application);std::string live;
      if(!owner["files"].contains(paths[i])||owner["files"][paths[i]]!=expected||!store.read(paths[i],live)||transaction::digest(live)!=expected)reusable=false;
    }
    if(reusable){RuntimeResult r;r.ok=true;r.launchPath="E:\\RRXD-Lite\\nkpatcher.xbe";r.message="Verified existing session files; launching patcher...";return r;}
  }
  ULARGE_INTEGER free={};if(!GetDiskFreeSpaceExA("E:\\",&free,nullptr,nullptr)||free.QuadPart<application->size()*5ULL+patcher->size()*3ULL+1024*1024)return fail("Not enough E: space to stage and preserve all runtime files");
  std::string id;CreateDirectoryA("E:\\RRXD-Lite-Setup",nullptr);CreateDirectoryA("E:\\RRXD-Lite-Setup\\Transactions",nullptr);
  for(unsigned i=0;i<1000;++i){char value[8];snprintf(value,sizeof(value),"Tx-%03u",i);std::string p="E:\\RRXD-Lite-Setup\\Transactions\\"+std::string(value);if(CreateDirectoryA(p.c_str(),nullptr)){id=value;break;}}
  if(id.empty())return fail("Cannot reserve a new runtime transaction");
  std::vector<transaction::Change> plan;json files=json::object();
  const std::string targets[]={"E/RRXD-Lite/default.xbe","E/RRXD-Lite/nkpatcher.xbe","E/RRXD-Lite-Setup/recovery.xbe"};
  for(unsigned i=0;i<3;++i){auto bytes=i==1?patcher:application;std::string before;
    if(store.exists(targets[i])){
      if(owner.is_null()||!owner["files"].contains(targets[i])||!owner["files"][targets[i]].is_string())return fail("Existing file is not owned by this setup and was preserved: "+targets[i]);
      before=owner["files"][targets[i]].get<std::string>();
    }
    plan.push_back({targets[i],before,bytes});files[targets[i]]=transaction::digest(*bytes);
  }
  json next={{"schema",1},{"identity",context.identity},{"transaction",id},{"files",files},{"permanent_boot_enabled",false}};
  plan.push_back({"E/RRXD-Lite-Setup/runtime-owned.json",previous.empty()?"":transaction::digest(previous),std::make_shared<const std::string>(next.dump(2))});
  auto result=transaction::install(store,id,plan,context);if(!result.ok)return fail(result.message+". WHITE can restore a recorded stage");
  RuntimeResult r;r.ok=true;r.launchPath="E:\\RRXD-Lite\\nkpatcher.xbe";r.message="Session patcher staged and verified. Launching the separate GPL component...";return r;
}
RuntimeResult restoreRuntimeStage(){
  transaction::Context context;std::string error;if(!identity(context,error))return fail(error);
  auto id=latestTransaction();if(id.empty())return fail("No recorded runtime stage to restore");
  EStore store;auto result=transaction::restore(store,id,context);RuntimeResult r;r.ok=result.ok;r.message=result.message;
  if(result.ok)r.message+=". Cold-reboot to clear any RAM patches; stock boot files were not changed.";return r;
}
}
