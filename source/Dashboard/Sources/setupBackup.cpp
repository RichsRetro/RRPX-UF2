/* SPDX-License-Identifier: MIT — RRXD installer backup engine, 2026.
 * All output is confined to a newly created backup directory. No boot writes.
 */
#include "setupBackup.hpp"
#include "setupHash.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
#include <set>
#include <algorithm>
#ifdef NXDK
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#else
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#endif
namespace setup {
using json=nlohmann::json;
namespace {
#ifdef NXDK
const char sep='\\';
#else
const char sep='/';
#endif
std::string lastCopyFailure;
struct Entry { std::string name; bool directory; uint64_t size; };
bool safeRelative(const std::string& s) {
  if(s.empty()||s.size()>200||s.front()=='/'||s.back()=='/'||s.find('\\')!=std::string::npos||s.find(':')!=std::string::npos) return false;
  size_t at=0;
  while(at<s.size()) { auto end=s.find('/',at); auto part=s.substr(at,end==std::string::npos?s.size()-at:end-at);
    if(part.empty()||part=="."||part=="..")return false;
    for(unsigned char c:part)if(c<32||c==127)return false;
    if(end==std::string::npos)break; at=end+1;
  } return true;
}
std::string path(const std::string& root,const std::string& relative) {
  std::string s=relative; std::replace(s.begin(),s.end(),'/',sep);
  return root+(root.empty()||root.back()!=sep?std::string(1,sep):"")+s;
}
bool newDirectory(const std::string& name) {
#ifdef NXDK
  return CreateDirectoryA(name.c_str(),nullptr)!=0;
#else
  return mkdir(name.c_str(),0700)==0;
#endif
}
bool list(const std::string& folder,std::vector<Entry>& result) {
#ifdef NXDK
  WIN32_FIND_DATAA entry; HANDLE h=FindFirstFileA(path(folder,"*").c_str(),&entry);
  if(h==INVALID_HANDLE_VALUE) return GetLastError()==ERROR_FILE_NOT_FOUND;
  do { std::string name=entry.cFileName; if(name=="."||name=="..")continue;
    result.push_back({name,(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0,
      (uint64_t(entry.nFileSizeHigh)<<32)|entry.nFileSizeLow});
  } while(FindNextFileA(h,&entry));
  DWORD error=GetLastError(); FindClose(h);
  if(error!=ERROR_NO_MORE_FILES)return false;
#else
  DIR* d=opendir(folder.c_str()); if(!d)return false;
  while(auto e=readdir(d)) { std::string name=e->d_name; if(name=="."||name=="..")continue;
    struct stat st; if(lstat(path(folder,name).c_str(),&st)||(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode))) { closedir(d);return false; }
    result.push_back({name,S_ISDIR(st.st_mode),uint64_t(st.st_size)});
  } closedir(d);
#endif
  std::sort(result.begin(),result.end(),[](const Entry&a,const Entry&b){return a.name<b.name;}); return true;
}
bool writeNew(const std::string& name,const unsigned char* data,size_t size) {
#ifdef NXDK
  HANDLE h=CreateFileA(name.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(h==INVALID_HANDLE_VALUE)return false;
  DWORD written=0; bool ok=size<=0xffffffffULL&&WriteFile(h,data,DWORD(size),&written,nullptr)&&written==size;
  IO_STATUS_BLOCK io; if(ok)ok=NT_SUCCESS(NtFlushBuffersFile(h,&io));
  if(!CloseHandle(h))ok=false; return ok;
#else
  int fd=open(name.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600); if(fd<0)return false;
  size_t at=0; while(at<size) { auto n=write(fd,data+at,size-at); if(n<=0)break; at+=size_t(n); }
  bool ok=at==size&&!fsync(fd); if(close(fd))ok=false; return ok;
#endif
}
bool copyNew(const std::string& src,const std::string& dst,std::string& digest,uint64_t& size) {
  lastCopyFailure.clear();
#ifdef NXDK
  // Use the same native binary I/O family as the working FTP transfer path.
  // Keep exact size, flush, close and full SHA256 read-back checks.
  HANDLE in=CreateFileA(src.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(in==INVALID_HANDLE_VALUE){lastCopyFailure="open source win32="+std::to_string(GetLastError());return false;}
  LARGE_INTEGER expected={};if(!GetFileSizeEx(in,&expected)||expected.QuadPart<0){CloseHandle(in);return false;}
  HANDLE out=CreateFileA(dst.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(out==INVALID_HANDLE_VALUE){lastCopyFailure="create destination win32="+std::to_string(GetLastError());CloseHandle(in);return false;}
  Sha256 hash;unsigned char block[4096];size=0;bool ok=true;
  for(;;){DWORD got=0;if(!ReadFile(in,block,sizeof(block),&got,nullptr)){lastCopyFailure="read win32="+std::to_string(GetLastError());ok=false;break;}if(!got)break;
    DWORD wrote=0;if(!WriteFile(out,block,got,&wrote,nullptr)||wrote!=got){lastCopyFailure="write win32="+std::to_string(GetLastError());ok=false;break;}
    hash.update(block,got);size+=got;
  }
  if(size!=static_cast<uint64_t>(expected.QuadPart)){lastCopyFailure="source size changed";ok=false;}
  if(!CloseHandle(in))ok=false;
  IO_STATUS_BLOCK io={};if(ok){NTSTATUS status=NtFlushBuffersFile(out,&io);if(status==STATUS_PENDING){status=NtWaitForSingleObject(out,FALSE,nullptr);if(NT_SUCCESS(status))status=io.Status;}if(!NT_SUCCESS(status)){char code[48];snprintf(code,sizeof(code),"flush NTSTATUS=%08lx",static_cast<unsigned long>(status));lastCopyFailure=code;ok=false;}}
  if(!CloseHandle(out)){lastCopyFailure="close destination win32="+std::to_string(GetLastError());ok=false;}
  digest=hash.finish();uint64_t readSize=0;if(!ok)return false;auto readHash=hashFile(dst,&readSize);if(readHash!=digest||readSize!=size){lastCopyFailure="read-back SHA256/size mismatch";return false;}return true;
#else
  FILE* in=fopen(src.c_str(),"rb"); if(!in)return false;
#ifdef NXDK
  HANDLE out=CreateFileA(dst.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(out==INVALID_HANDLE_VALUE) { fclose(in);return false; }
#else
  int out=open(dst.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600); if(out<0) { fclose(in);return false; }
#endif
  Sha256 hash; unsigned char buffer[4096]; size=0; bool ok=true;
  while(size_t n=fread(buffer,1,sizeof(buffer),in)) {
    hash.update(buffer,n); size+=n;
#ifdef NXDK
    DWORD done=0; if(!WriteFile(out,buffer,DWORD(n),&done,nullptr)||done!=n) { ok=false;break; }
#else
    size_t done=0; while(done<n) { auto w=write(out,buffer+done,n-done); if(w<=0)break; done+=size_t(w); }
    if(done!=n) { ok=false;break; }
#endif
  }
  if(ferror(in))ok=false; if(fclose(in))ok=false;
#ifdef NXDK
  IO_STATUS_BLOCK io; if(ok)ok=NT_SUCCESS(NtFlushBuffersFile(out,&io)); if(!CloseHandle(out)){lastCopyFailure="close destination win32="+std::to_string(GetLastError());ok=false;}
#else
  if(fsync(out))ok=false; if(close(out))ok=false;
#endif
  digest=hash.finish(); uint64_t readSize=0;
  return ok&&hashFile(dst,&readSize)==digest&&readSize==size;
#endif
}
bool copyTree(const std::string& source,const std::string& destination,const std::string& relative,
              json& records,BackupResult& result,const Progress& progress,unsigned depth) {
  if(depth>32) { result.error="Directory nesting exceeds the backup limit";return false; }
  std::vector<Entry> entries; if(!list(path(source,relative),entries)) { result.error="Cannot enumerate "+relative;return false; }
  for(auto& entry:entries) {
    std::string child=relative.empty()?entry.name:relative+"/"+entry.name;
    if(!safeRelative(child)||path(destination,child).size()>230||records.size()>=10000) { result.error="Unsupported backup path or file count";return false; }
    if(progress&&!progress(child,result.bytes)) { result.error="Cancelled; partial backup preserved";return false; }
    if(entry.directory) {
      if(!newDirectory(path(destination,child))||!copyTree(source,destination,child,records,result,progress,depth+1))return false;
    } else {
      std::string digest; uint64_t bytes=0;
      if(!copyNew(path(source,child),path(destination,child),digest,bytes)) { result.error="Copy/read-back failed: "+child+" ["+lastCopyFailure+"]";return false; }
      if(bytes!=entry.size){result.error="Directory size mismatch: "+child+" listed="+std::to_string(entry.size)+" copied="+std::to_string(bytes);return false;}
      records.push_back({{"path","C/"+child},{"bytes",bytes},{"sha256",digest}});
      result.bytes+=bytes; ++result.files;
    }
  } return true;
}
std::string readSmall(const std::string& filename) {
#ifdef NXDK
  HANDLE h=CreateFileA(filename.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return "";
  std::string data;std::vector<char> block(65536);bool ok=true;
  for(;;){DWORD n=0;if(!ReadFile(h,block.data(),DWORD(block.size()),&n,nullptr)){ok=false;break;}if(!n)break;data.append(block.data(),n);if(data.size()>4*1024*1024){ok=false;break;}}
  if(!CloseHandle(h))ok=false;return ok?data:"";
#else
  FILE*f=fopen(filename.c_str(),"rb"); if(!f)return "";
  std::string data; char b[4096];
  while(size_t n=fread(b,1,sizeof(b),f)) { data.append(b,n); if(data.size()>4*1024*1024) { fclose(f);return ""; } }
  bool ok=!ferror(f); fclose(f); return ok?data:"";
#endif
}
bool verifyTree(const std::string& root,const std::string& relative,std::set<std::string>& paths,unsigned depth) {
  if(depth>32)return false; std::vector<Entry> entries;
  if(!list(path(root,relative),entries))return false;
  for(auto& e:entries) { std::string r=relative.empty()?e.name:relative+"/"+e.name;
    if(!safeRelative(r))return false;
    if(e.directory) { if(!verifyTree(root,r,paths,depth+1))return false; }
    else if(r!="manifest.json")paths.insert(r);
  } return true;
}
bool treeShape(const std::string& root,const std::string& relative,std::set<std::string>& paths,unsigned depth){
  if(depth>32)return false;std::vector<Entry> entries;if(!list(path(root,relative),entries))return false;
  for(auto&e:entries){std::string r=relative.empty()?e.name:relative+"/"+e.name;if(!safeRelative(r))return false;
    paths.insert(r+(e.directory?"/":""));if(e.directory&&!treeShape(root,r,paths,depth+1))return false;
  }return true;
}
}
std::string hashFile(const std::string& name,uint64_t* bytes) {
#ifdef NXDK
  if(bytes)*bytes=0;
  HANDLE in=CreateFileA(name.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(in==INVALID_HANDLE_VALUE)return "";
  Sha256 hash;unsigned char block[4096];uint64_t total=0;bool ok=true;
  for(;;){DWORD got=0;if(!ReadFile(in,block,sizeof(block),&got,nullptr)){lastCopyFailure="read win32="+std::to_string(GetLastError());ok=false;break;}if(!got)break;hash.update(block,got);total+=got;}
  if(!CloseHandle(in))ok=false;if(bytes)*bytes=total;return ok?hash.finish():"";
#else
  FILE*f=fopen(name.c_str(),"rb"); if(!f)return "";
  Sha256 hash; unsigned char b[4096]; uint64_t total=0;
  while(size_t n=fread(b,1,sizeof(b),f)) { hash.update(b,n);total+=n; }
  bool ok=!ferror(f); if(fclose(f))ok=false; if(bytes)*bytes=total;
  return ok?hash.finish():"";
#endif
}
BackupResult createBackup(const BackupInput& input,const Progress& progress) {
  BackupResult result;
  std::string sourceRoot=input.source,parentRoot=input.parent;
  while(sourceRoot.size()>1&&sourceRoot.back()==sep)sourceRoot.pop_back();
  while(parentRoot.size()>1&&parentRoot.back()==sep)parentRoot.pop_back();
#ifdef NXDK
  auto lower=[](unsigned char c){return c>='A'&&c<='Z'?char(c+32):char(c);};
  std::transform(sourceRoot.begin(),sourceRoot.end(),sourceRoot.begin(),lower);
  std::transform(parentRoot.begin(),parentRoot.end(),parentRoot.begin(),lower);
#endif
  if(sourceRoot.empty()||parentRoot.empty()||sourceRoot==parentRoot||
     parentRoot.compare(0,sourceRoot.size()+1,sourceRoot+sep)==0||sourceRoot==std::string(1,sep)) { result.error="Backup destination overlaps source";return result; }
  if(std::all_of(input.eeprom.begin(),input.eeprom.end(),[](unsigned char b){return b==0;})||
     std::all_of(input.eeprom.begin(),input.eeprom.end(),[](unsigned char b){return b==255;})) { result.error="EEPROM read is invalid";return result; }
  // A new session directory never replaces or resumes an earlier backup.
  for(unsigned i=0;i<1000;++i) { char name[24];snprintf(name,sizeof(name),"Backup-%03u",i);
    std::string candidate=path(input.parent,name); if(newDirectory(candidate)) { result.folder=candidate;break; }
  }
  if(result.folder.empty()) { result.error="Cannot create a new backup directory";return result; }
  if(!writeNew(path(result.folder,"eeprom.bin"),input.eeprom.data(),input.eeprom.size())||!newDirectory(path(result.folder,"C"))) { result.error="Cannot create backup files";return result; }
  json records=json::array(); Sha256 eepromHash;eepromHash.update(input.eeprom.data(),input.eeprom.size());
  records.push_back({{"path","eeprom.bin"},{"bytes",256},{"sha256",eepromHash.finish()}});
  if(!copyTree(input.source,path(result.folder,"C"),"",records,result,progress,0))return result;
  if(result.files==0) { result.error="Source C directory is empty";return result; }
  std::string digest; uint64_t bytes=0;
  if(!copyNew(input.executable,path(result.folder,"recovery.xbe"),digest,bytes)) { result.error="Recovery application copy failed";return result; }
  records.push_back({{"path","recovery.xbe"},{"bytes",bytes},{"sha256",digest}});
  json manifest={{"schema",1},{"status","verified-backup"},{"persistent_boot_enabled",false},
    {"console",input.console},{"files",records}};
  std::string text=manifest.dump(2);
  if(!writeNew(path(result.folder,"manifest.json"),reinterpret_cast<const unsigned char*>(text.data()),text.size())) { result.error="Cannot commit backup manifest";return result; }
  result.ok=verifyBackup(result.folder,result.error); return result;
}
bool verifyBackup(const std::string& folder,std::string& error) {
  std::set<std::string> actual;
  if(!verifyTree(folder,"",actual,0)) { error="Unsafe backup tree";return false; }
  auto manifest=json::parse(readSmall(path(folder,"manifest.json")),nullptr,false);
  if(!manifest.is_object()||!manifest.contains("schema")||!manifest["schema"].is_number_integer()||manifest["schema"]!=1||
     !manifest.contains("status")||!manifest["status"].is_string()||manifest["status"]!="verified-backup"||
     !manifest.contains("persistent_boot_enabled")||!manifest["persistent_boot_enabled"].is_boolean()||manifest["persistent_boot_enabled"]!=false||
     !manifest.contains("files")||!manifest["files"].is_array()||manifest["files"].size()<3) { error="No complete backup manifest";return false; }
  std::set<std::string> expected;
  for(auto& entry:manifest["files"]) {
    if(!entry.is_object()||!entry.contains("path")||!entry["path"].is_string()||!entry.contains("sha256")||!entry["sha256"].is_string()||!entry.contains("bytes")||!entry["bytes"].is_number_unsigned()) { error="Invalid manifest record";return false; }
    std::string r=entry["path"],digest=entry["sha256"];
    if(!safeRelative(r)||(r!="eeprom.bin"&&r!="recovery.xbe"&&r.compare(0,2,"C/"))||!expected.insert(r).second||digest.size()!=64) { error="Unsafe or duplicate manifest path";return false; }
    uint64_t size=0;if(hashFile(path(folder,r),&size)!=digest||size!=entry["bytes"].get<uint64_t>()) { error="Backup hash mismatch: "+r;return false; }
  }
  if(!expected.count("eeprom.bin")||!expected.count("recovery.xbe")) { error="Missing recovery files";return false; }
  if(actual!=expected) { error="Backup contains missing or unexpected files";return false; }
  error.clear();return true;
}
std::string findLatestBackup(const std::string& parent) {
  std::vector<Entry> entries;if(!list(parent,entries))return "";
  for(auto i=entries.rbegin();i!=entries.rend();++i) {
    if(!i->directory||i->name.size()!=10||i->name.compare(0,7,"Backup-")||
       !std::all_of(i->name.begin()+7,i->name.end(),[](char c){return c>='0'&&c<='9';}))continue;
    auto manifest=json::parse(readSmall(path(path(parent,i->name),"manifest.json")),nullptr,false);
    if(manifest.is_object()&&manifest.contains("status")&&manifest["status"]=="verified-backup")return path(parent,i->name);
  }return "";
}
bool sourceMatchesVerifiedBackup(const std::string& folder,const std::string& source,std::string& error){
  auto manifest=json::parse(readSmall(path(folder,"manifest.json")),nullptr,false);
  if(!manifest.is_object()||!manifest.contains("files")||!manifest["files"].is_array()){error="Invalid original backup manifest";return false;}
  std::set<std::string> checked;
  for(auto&r:manifest["files"]){
    if(!r.is_object()||!r.contains("path")||!r["path"].is_string()){error="Invalid original file record";return false;}
    std::string name=r["path"];if(name.compare(0,2,"C/"))continue;name=name.substr(2);
    if(!safeRelative(name)||!checked.insert(name).second||!r.contains("bytes")||!r["bytes"].is_number_unsigned()||!r.contains("sha256")||!r["sha256"].is_string()){error="Invalid original C: record";return false;}
    uint64_t size=0;if(hashFile(path(source,name),&size)!=r["sha256"].get<std::string>()||size!=r["bytes"].get<uint64_t>()){error="Original C: file differs: "+name;return false;}
  }
  if(checked.empty()){error="Original C: file table is empty";return false;}
  std::set<std::string> originalShape,liveShape;
  if(!treeShape(path(folder,"C"),"",originalShape,0)||!treeShape(source,"",liveShape,0)||originalShape!=liveShape){error="C: contains missing, extra or unsafe files/folders";return false;}
  error.clear();return true;
}
}
