/* SPDX-License-Identifier: MIT. Retail E-only session transactions, portable/testable.
 * C: is never an allowed transaction target in this edition.
 */
#include "setupTransaction.hpp"
#include "setupHash.hpp"
#include "3rdparty/json.hpp"
#include <set>
#include <cctype>
namespace transaction {
using json=nlohmann::json;
namespace {
const std::string base="E/RRXD-Lite-Setup/Transactions/";
bool validId(const std::string& id){if(id.empty()||id.size()>12)return false;for(unsigned char c:id)if(!std::isalnum(c)&&c!='-')return false;return true;}
bool allowed(const std::string& p){
  static const std::set<std::string> paths={"E/RRXD-Lite/default.xbe","E/RRXD-Lite/nkpatcher.xbe","E/RRXD-Lite-Setup/recovery.xbe","E/RRXD-Lite-Setup/runtime-owned.json"};
  return paths.count(p)!=0;
}
std::string currentHash(Store& store,const std::string& p){std::string data;return store.exists(p)&&store.read(p,data)?digest(data):"";}
bool authorize(const Context& c){if(!c.sessionIdentityVerified||c.identity.size()!=64)return false;for(char v:c.identity)if(!((v>='0'&&v<='9')||(v>='a'&&v<='f')))return false;return true;}
Result fail(const std::string& s){return {false,s};}
Result validateJournal(Store& s,const std::string& id,const Context& context,json& j){
  if(!validId(id)||!authorize(context))return fail("Verified backup and console identity required");
  std::string text;if(!s.read(base+id+"/journal.json",text))return fail("Transaction journal is unavailable");
  j=json::parse(text,nullptr,false);
  if(!j.is_object()||!j.contains("identity")||!j["identity"].is_string()||j["identity"]!=context.identity||
    !j.contains("schema")||j["schema"]!=1||!j.contains("changes")||!j["changes"].is_array()||j["changes"].empty()||j["changes"].size()>7)return fail("Invalid or foreign-console transaction journal");
  std::set<std::string> seen;unsigned index=0;
  for(auto& r:j["changes"]){
    if(!r.is_object()||!r.contains("target")||!r["target"].is_string()||!r.contains("before")||!r["before"].is_string()||!r.contains("after")||!r["after"].is_string())return fail("Invalid transaction record");
    std::string p=r["target"],before=r["before"],after=r["after"];
    if(!allowed(p)||!seen.insert(p).second||(!before.empty()&&before.size()!=64)||after.size()!=64)return fail("Unsafe transaction target");
    if(!before.empty()&&currentHash(s,base+id+"/before-"+std::to_string(index)+".bin")!=before)return fail("Original transaction backup is damaged");
    ++index;
  }return {true,""};
}
}
std::string digest(const std::string& data){setup::Sha256 h;h.update(reinterpret_cast<const unsigned char*>(data.data()),data.size());return h.finish();}
Result install(Store& s,const std::string& id,const std::vector<Change>& changes,const Context& c){
  if(!validId(id)||!authorize(c)||changes.empty()||changes.size()>7)return fail("Invalid plan or missing verified backup");
  if(s.exists(base+id+"/journal.json"))return fail("Transaction already exists; preserve or restore it first");
  std::set<std::string> seen;json records=json::array();
  // Check every live preimage before writing any staged data.
  for(auto& change:changes){
    if(!allowed(change.target)||!seen.insert(change.target).second||!change.data)return fail("Unsafe or duplicate install target");
    if((change.expectedBefore.empty()&&s.exists(change.target))||(!change.expectedBefore.empty()&&currentHash(s,change.target)!=change.expectedBefore))return fail("Live file differs from the approved plan: "+change.target);
  }
  unsigned i=0;
  for(auto& change:changes){
    if(!change.expectedBefore.empty()){
      std::string old;if(!s.read(change.target,old)||digest(old)!=change.expectedBefore||!s.writeNew(base+id+"/before-"+std::to_string(i)+".bin",old)||currentHash(s,base+id+"/before-"+std::to_string(i)+".bin")!=change.expectedBefore)return fail("Could not preserve the original file");
    }
    std::string staged=change.target+".rrxd-"+id,after=digest(*change.data);
    if(!s.writeNew(staged,*change.data)||currentHash(s,staged)!=after)return fail("Staged file verification failed");
    records.push_back({{"target",change.target},{"before",change.expectedBefore},{"after",after}});++i;
  }
  json journal={{"schema",1},{"identity",c.identity},{"status","prepared"},{"changes",records}};
  auto journalPath=base+id+"/journal.json";auto journalText=journal.dump(2);
  if(!s.writeNew(journalPath,journalText)||currentHash(s,journalPath)!=digest(journalText))return fail("Could not commit the transaction journal");
  // The journal already contains every before/after hash. Recovery can identify
  // a swap even if power is lost before the next journal update.
  for(auto& change:changes){
    if((change.expectedBefore.empty()&&s.exists(change.target))||(!change.expectedBefore.empty()&&currentHash(s,change.target)!=change.expectedBefore))return fail("Live file changed during staging; recovery required");
    auto after=digest(*change.data);
    if(!s.replace(change.target+".rrxd-"+id,change.target)||currentHash(s,change.target)!=after)return fail("File swap failed; transaction journal retained");
  }
  // Keep the recovery journal immutable. An interruption cannot leave a
  // half-written status update that hides the original hashes/backups.
  s.writeNew(base+id+"/committed.marker",digest(journalText));
  return {true,"Transaction committed and verified"};
}
Result restore(Store& s,const std::string& id,const Context& c){
  json j;auto valid=validateJournal(s,id,c,j);if(!valid.ok)return valid;
  // Preflight every record before restoring anything; preserve human edits.
  for(auto& r:j["changes"]){
    std::string p=r["target"],before=r["before"],after=r["after"];
    auto now=currentHash(s,p);
    if(s.exists(p)&&now!=before&&now!=after)return fail("Live file was changed outside this transaction: "+p);
    if(!s.exists(p)&&!before.empty())return fail("Previously existing live file is missing; manual recovery required");
  }
  for(size_t n=j["changes"].size();n>0;--n){
    auto&r=j["changes"][n-1];std::string p=r["target"],before=r["before"];
    if(before.empty()){if(s.exists(p)&&!s.erase(p))return fail("Could not remove a transaction-created file");}
    else if(currentHash(s,p)!=before){
      std::string original;if(!s.read(base+id+"/before-"+std::to_string(n-1)+".bin",original)||digest(original)!=before)return fail("Original file backup is damaged");
      std::string temp=p+".restore-"+id;
      if(s.exists(temp)){if(currentHash(s,temp)!=before)return fail("Conflicting restore staging file");}
      else if(!s.writeNew(temp,original))return fail("Could not stage original file");
      if(!s.replace(temp,p)||currentHash(s,p)!=before)return fail("Could not restore original file");
    }
  }
  s.writeNew(base+id+"/restored.marker",c.identity);
  return {true,"Original files restored; backup and journal preserved"};
}
}
