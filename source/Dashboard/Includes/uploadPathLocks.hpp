/* SPDX-License-Identifier: MIT. Serialize writers only for the same FATX path. */
#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
class UploadPathLocks {
 std::mutex gate;
 std::map<std::string,std::weak_ptr<std::mutex>> paths;
 static std::string key(const std::string& input){
   std::vector<std::string> parts;std::string part;
   auto append=[&]{if(part==".."){if(parts.size()>1)parts.pop_back();}
     else if(!part.empty()&&part!=".")parts.push_back(part);part.clear();};
   for(unsigned char c:input){if(c=='/'||c=='\\')append();else part+=c>='A'&&c<='Z'?char(c+32):char(c);}append();
   std::string result;for(const auto&p:parts){if(!result.empty())result+='/';result+=p;}return result;
 }
public:
 std::shared_ptr<std::mutex> get(const std::string& path){
   std::lock_guard<std::mutex> lock(gate);
   for(auto it=paths.begin();it!=paths.end();)if(it->second.expired())it=paths.erase(it);else ++it;
   const auto name=key(path);auto existing=paths[name].lock();
   if(existing)return existing;
   auto created=std::make_shared<std::mutex>();paths[name]=created;return created;
 }
};
