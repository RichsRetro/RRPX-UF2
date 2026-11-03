/* SPDX-License-Identifier: MIT. Settings-independent session identity. */
#pragma once
#include "setupTransaction.hpp"
#include <string>
namespace setup { namespace sessionIdentity {
inline bool valid(const std::string& e) {
  if(e.size()!=256)return false;
  bool zero=true,ones=true;for(unsigned char c:e){zero&=c==0;ones&=c==255;}
  return !zero&&!ones;
}
inline bool sameConsole(const std::string&a,const std::string&b) {
  // Preserve the encrypted authenticated HDD-key block and factory serial/MAC.
  // A re-key or re-encryption deliberately requires manual investigation.
  return valid(a)&&valid(b)&&a.compare(0,48,b,0,48)==0&&a.compare(0x34,18,b,0x34,18)==0;
}
inline std::string stable(const std::string&e) {
  return valid(e)?transaction::digest("RRXD-session-identity-v2:"+e.substr(0,48)+e.substr(0x34,18)):"";
}
inline bool accepts(const std::string&current,const std::string&recorded,const std::string&historical) {
  if(!valid(current))return false;
  return recorded==stable(current)||recorded==transaction::digest(current)||
    (sameConsole(current,historical)&&transaction::digest(historical)==recorded);
}
} }
