/* SPDX-License-Identifier: MIT. Original procedural 2001-style boot artwork. */
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace retailBootArt {
struct Canvas {
 uint32_t* pixels;int width,height;
 void rect(int x,int y,int w,int h,uint32_t color){
  const int right=std::min(width,x+w),bottom=std::min(height,y+h);
  for(int row=std::max(0,y);row<bottom;++row)
   for(int col=std::max(0,x);col<right;++col)pixels[row*width+col]=color;
 }
 void line(int x,int y,int dx,int dy,int length,int thickness,uint32_t color){
  for(int i=0;i<length;++i)rect(x+i*dx,y+i*dy,thickness,thickness,color);
 }
};
inline void draw(uint32_t* pixels,int width,int height,unsigned ticks,bool complete){
 if(!pixels||width!=640||height!=480)return;
 Canvas c{pixels,width,height};c.rect(0,0,width,height,0x00020403);
 // Quiet phosphor grid, kept within the television's overscan-safe centre.
 for(int y=112;y<390;y+=24)c.rect(70,y,500,1,0x0005100a);
 for(int x=80;x<570;x+=40)c.rect(x,112,1,278,0x00040c08);
 c.rect(94,309,452,38,0x00051008);c.rect(98,313,444,30,0x000d3015);
 c.rect(102,317,436,22,0x00215a27);c.rect(104,319,432,18,0x00030704);
 unsigned step=(ticks/14)%800;const int travel=step<=400?int(step):800-int(step);
 const int position=110+travel;
 for(int x=108;x<532;x+=8){
  const int distance=std::abs(x-position);
  uint32_t color=complete?0x0097ee38:distance<38?0x0075ee31:distance<80?0x00277115:0x000b2810;
  c.rect(x,322,5,12,color);c.rect(x,322,5,2,complete?0x00d5ff86:distance<38?0x00c8ff7a:0x00183d18);
 }
 // The tiny console really hops along the bar; there is no fake percentage.
 unsigned phase=(ticks/18)%32;int bounce=complete?0:int(phase*(32-phase))/15;
 int x=std::max(110,std::min(482,position-25));int y=281-bounce;
 c.rect(x+4,307,44,3,0x000d2310);c.rect(x+10,310,32,2,0x00153112);
 for(int i=1;i<6;++i){int trail=x-i*12;int yy=y+24+(i%2)*4;c.rect(trail,yy,3,3,0x000b350d);}
 // Black case, beveled top, green X, four controller sockets and power LED.
 c.rect(x,y+3,52,24,0x002c3230);c.rect(x+3,y,46,5,0x00454d46);
 c.rect(x+2,y+5,48,19,0x00161d18);c.rect(x,y+25,52,9,0x00090d0a);
 c.rect(x+2,y+26,48,1,0x00333b33);
 c.line(x+9,y+3,1,1,17,3,0x001c671c);c.line(x+39,y+3,-1,1,17,3,0x001c671c);
 c.line(x+13,y+4,1,1,14,2,0x0062cf2a);c.line(x+35,y+4,-1,1,14,2,0x0062cf2a);
 c.rect(x+23,y+11,7,4,0x00c6ff77);c.rect(x+24,y+28,5,4,0x007cfa25);
 for(int i=0;i<4;++i)c.rect(x+5+i*11,y+29,5,3,0x00303932);
 // A few dancing sparks above the case, rather than a text-heavy patch page.
 c.rect(x+23,y-9,2,5,0x0064a936);c.rect(x+15,y-5,3,2,0x002e6e23);
 if((phase/4)%2)c.rect(x+37,y-4,3,3,0x0096e34d);
}
}
