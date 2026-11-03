#include "setupTransaction.hpp"
#include "3rdparty/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cassert>
#include <unistd.h>
namespace fs=std::filesystem;
using namespace transaction;
struct PowerLoss:std::exception{};
class Disk:public Store {
  fs::path root;
  void before(){++operations;if(operations==failAt&&!failAfter)throw PowerLoss();}
  void after(){if(operations==failAt&&failAfter)throw PowerLoss();}
public:
  unsigned operations=0,failAt=0;bool failAfter=false;
  explicit Disk(fs::path r):root(r){fs::create_directories(root);}
  bool exists(const std::string&p)override{return fs::is_regular_file(root/p);}
  bool read(const std::string&p,std::string&data)override{std::ifstream f(root/p,std::ios::binary);if(!f)return false;data.assign(std::istreambuf_iterator<char>(f),{});return !f.bad();}
  bool writeNew(const std::string&p,const std::string&data)override{before();if(exists(p))return false;fs::create_directories((root/p).parent_path());std::ofstream f(root/p,std::ios::binary);f.write(data.data(),data.size());f.close();bool ok=bool(f);after();return ok;}
  bool replace(const std::string&a,const std::string&b)override{before();std::error_code ec;fs::rename(root/a,root/b,ec);after();return !ec;}
  bool erase(const std::string&p)override{before();bool ok=fs::remove(root/p);after();return ok;}
  void seed(const std::string&p,const std::string&data){unsigned f=failAt;failAt=0;assert(writeNew(p,data));failAt=f;operations=0;}
  std::string get(const std::string&p){std::string x;assert(read(p,x));return x;}
};
std::vector<Change> plan(){return {
  {"E/RRXD-Lite/default.xbe","",std::make_shared<const std::string>("new dash")},
  {"E/RRXD-Lite/nkpatcher.xbe",digest("original font"),std::make_shared<const std::string>("exploit font")},
  {"E/RRXD-Lite-Setup/runtime-owned.json","",std::make_shared<const std::string>("font loader")},
  {"E/RRXD-Lite-Setup/recovery.xbe",digest("original boot"),std::make_shared<const std::string>("new signed loader")}};}
void seed(Disk&d){d.seed("E/RRXD-Lite-Setup/recovery.xbe","original boot");d.seed("E/RRXD-Lite/nkpatcher.xbe","original font");d.seed("E/user-data.bin","keep me");d.operations=0;}
void original(Disk&d){assert(d.get("E/RRXD-Lite-Setup/recovery.xbe")=="original boot");assert(d.get("E/RRXD-Lite/nkpatcher.xbe")=="original font");assert(!d.exists("E/RRXD-Lite-Setup/runtime-owned.json"));assert(!d.exists("E/RRXD-Lite/default.xbe"));assert(d.get("E/user-data.bin")=="keep me");}
int main(){
  auto root=fs::temp_directory_path()/("rrxd-transaction-fixtures-"+std::to_string(getpid()));assert(!fs::exists(root));assert(fs::create_directory(root));
  Context c;c.identity=digest("console identity");c.sessionIdentityVerified=true;
  unsigned installOps=0,restoreOps=0;
  {Disk d(root/"baseline");seed(d);assert(install(d,"Tx-000",plan(),c).ok);installOps=d.operations;assert(d.get("E/RRXD-Lite-Setup/recovery.xbe")=="new signed loader");d.operations=0;assert(restore(d,"Tx-000",c).ok);restoreOps=d.operations;original(d);assert(restore(d,"Tx-000",c).ok);}
  for(bool after:{false,true})for(unsigned fault=1;fault<=installOps;++fault){
    Disk d(root/(std::string("install-")+(after?"after-":"before-")+std::to_string(fault)));seed(d);d.failAt=fault;d.failAfter=after;
    try{install(d,"Tx-000",plan(),c);}catch(const PowerLoss&){}
    d.failAt=0;if(d.exists("E/RRXD-Lite-Setup/Transactions/Tx-000/journal.json"))assert(restore(d,"Tx-000",c).ok);
    original(d);
  }
  for(bool after:{false,true})for(unsigned fault=1;fault<=restoreOps;++fault){
    Disk d(root/(std::string("restore-")+(after?"after-":"before-")+std::to_string(fault)));seed(d);assert(install(d,"Tx-000",plan(),c).ok);d.operations=0;d.failAt=fault;d.failAfter=after;
    try{restore(d,"Tx-000",c);}catch(const PowerLoss&){}
    d.failAt=0;assert(restore(d,"Tx-000",c).ok);original(d);
  }
  {Disk d(root/"gate");seed(d);auto blocked=c;blocked.sessionIdentityVerified=false;assert(!install(d,"Tx-000",plan(),blocked).ok);assert(d.operations==0);original(d);
    for(const char* target:{"C/xboxdash.xbe","C/xodash/xbox.xtf","C/xodash/ernie.xtf"}){std::vector<Change> p={{target,"",std::make_shared<const std::string>("forbidden")}};assert(!install(d,"Tx-001",p,c).ok);assert(d.operations==0);}}

  {Disk d(root/"human-edit");seed(d);assert(install(d,"Tx-000",plan(),c).ok);assert(d.erase("E/RRXD-Lite/nkpatcher.xbe"));d.seed("E/RRXD-Lite/nkpatcher.xbe","human edit");d.operations=0;assert(!restore(d,"Tx-000",c).ok);assert(d.operations==0);assert(d.get("E/RRXD-Lite/nkpatcher.xbe")=="human edit");auto foreign=c;foreign.identity=digest("another console");assert(!restore(d,"Tx-000",foreign).ok);}
  {Disk d(root/"damaged-backup");seed(d);assert(install(d,"Tx-000",plan(),c).ok);auto b="E/RRXD-Lite-Setup/Transactions/Tx-000/before-1.bin";assert(d.erase(b));d.seed(b,"damaged");d.operations=0;assert(!restore(d,"Tx-000",c).ok);assert(d.operations==0);}
  {Disk d(root/"session-without-C-backup");seed(d);Context session;session.identity=c.identity;session.sessionIdentityVerified=true;
    std::vector<Change> p={{"E/RRXD-Lite/default.xbe","",std::make_shared<const std::string>("session dashboard")}};
    assert(install(d,"Tx-000",p,session).ok);assert(d.get("E/RRXD-Lite-Setup/recovery.xbe")=="original boot");assert(restore(d,"Tx-000",session).ok);original(d);
    d.operations=0;std::vector<Change> bad={{"C/xboxdash.xbe","",std::make_shared<const std::string>("forbidden")}};assert(!install(d,"Tx-001",bad,session).ok);assert(d.operations==0);original(d);
    session.sessionIdentityVerified=false;assert(!install(d,"Tx-002",p,session).ok);assert(d.operations==0);
  }
  fs::remove_all(root);
  std::cout<<"PASS: E-only session install/undo without C backup and C-write/identity guards;  install/uninstall round-trip; before/after interruption at every "<<installOps<<" install and "<<restoreOps<<" restore mutation; repeat restoration; E-only target/identity/foreign-console gates; human-edit and damaged-backup preservation\n";
}
