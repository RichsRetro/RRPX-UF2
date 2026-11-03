#include "setupBackup.hpp"
#include "setupHash.hpp"
#include <cstdio>
#include <iostream>
int main(int argc,char**argv) {
  if(argc<3)return 2;
  if(std::string(argv[1])=="latest") { std::cout<<setup::findLatestBackup(argv[2]);return 0; }
  if(std::string(argv[1])=="matches") { if(argc<4)return 2;std::string error;bool ok=setup::sourceMatchesVerifiedBackup(argv[2],argv[3],error);std::cout<<error;return ok?0:1; }
  if(std::string(argv[1])=="verify") { std::string error; bool ok=setup::verifyBackup(argv[2],error);std::cout<<error;return ok?0:1; }
  if(argc<5)return 2;
  setup::BackupInput in;in.source=argv[2];in.parent=argv[3];in.executable=argv[4];
  for(unsigned i=0;i<256;++i)in.eeprom[i]=i;
  in.console={{"kernel","1.0.5838.1"},{"fixture",true}};
  if(std::string(argv[1])=="bad-eeprom")in.eeprom.fill(0);
  bool cancel=std::string(argv[1])=="cancel";
  auto r=setup::createBackup(in,[&](const std::string&,uint64_t){return !cancel;});
  std::cout<<nlohmann::json({{"ok",r.ok},{"folder",r.folder},{"error",r.error},{"files",r.files},{"bytes",r.bytes}}).dump();
  return r.ok?0:1;
}
