#pragma once
#include <algorithm>
#include <cctype>
#include <string>

// Keep custom and G: scan roots; ensure stock and upgraded HDD roots exist.
inline std::string liteGameScanPaths(std::string paths) {
  auto normalized=[](std::string path){
    const auto first=path.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return std::string();
    path=path.substr(first,path.find_last_not_of(" \t\r\n")-first+1);
    std::replace(path.begin(),path.end(),'/','\\');
    while(!path.empty()&&path.back()=='\\')path.pop_back();
    std::transform(path.begin(),path.end(),path.begin(),[](unsigned char c){return char(std::tolower(c));});
    return path;
  };
  for(const char* required:{"E:\\Games","F:\\Games"}){
    bool found=false;size_t start=0;
    do{const auto end=paths.find(',',start);
      if(normalized(paths.substr(start,end==std::string::npos?end:end-start))==normalized(required)){found=true;break;}
      if(end==std::string::npos)break;
      start=end+1;
    }while(start<=paths.size());
    if(!found){if(!paths.empty()&&paths.back()!=',')paths+=',';paths+=required;}
  }
  return paths;
}
