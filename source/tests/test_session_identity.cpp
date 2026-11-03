#include "sessionIdentity.hpp"
#include <cassert>
#include <iostream>
int main(){
  using namespace setup::sessionIdentity;
  std::string old(256,'\0');for(unsigned i=0;i<256;++i)old[i]=char(i);
  auto current=old;for(unsigned i=0xa8;i<=0xaf;++i)current[i]^=0x55;
  const auto legacy=transaction::digest(old);
  assert(stable(old)==stable(current));
  assert(accepts(current,legacy,old));
  assert(!accepts(current,legacy,""));
  assert(accepts(old,legacy,""));
  assert(accepts(current,stable(current),""));
  for(unsigned i: {0u,20u,35u,47u,0x34u,0x40u,0x45u}){
    auto foreign=current;foreign[i]^=1;
    assert(!accepts(foreign,legacy,old));assert(stable(foreign)!=stable(current));
  }
  auto corrupt=old;corrupt[0xb0]^=1;
  assert(!accepts(current,legacy,corrupt));
  assert(!accepts(std::string(255,'a'),legacy,old));
  assert(!valid(std::string(256,'\0')));assert(!valid(std::string(256,char(255))));
  std::cout<<"PASS settings changes, legacy proof, wrong key/serial/MAC, corrupt proof and invalid reads\n";
}
