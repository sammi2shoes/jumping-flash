#include <cassert>
#include <cstdio>
#include "../../firmware/AX12SingleLeg/AX12Protocol.h"
void packet(uint8_t* b,uint16_t seq,bool arm,int sb,int forward=0,uint16_t session=11) {
  memset(b,0,20); b[0]='J';b[1]='F';b[2]=2;b[3]=1;
  jf::put16(b+4,seq);b[6]=arm;b[14]=sb;jf::put16(b+8,forward);jf::put16(b+16,session);jf::put16(b+18,jf::crc(b,18));
}
void seal(uint8_t* b){jf::put16(b+18,jf::crc(b,18));}
int main(){
 assert(jf::crc(reinterpret_cast<const uint8_t*>("123456789"),9)==0x29b1);
 jf::Receiver r;r.link=true;uint8_t b[20];
 packet(b,1,true,1);assert(!r.accept(b,20,10)&&!r.enabled); // Initial arm never accepted.
 packet(b,1,false,0);assert(r.accept(b,20,20));
 packet(b,2,true,1);assert(r.accept(b,20,40)&&r.enabled);
 packet(b,3,true,1,1000);assert(r.accept(b,20,60)&&r.forward==1000);
 assert(!r.accept(b,20,100)&&r.at==60); // Duplicates never extend command life.
 r.expire(160);assert(!r.enabled&&!r.offSeen);
 packet(b,4,true,1);assert(r.accept(b,20,161)&&!r.enabled); // No reconnect/restart auto-arm.
 packet(b,5,false,0);assert(r.accept(b,20,180));
 packet(b,6,true,1,1);assert(r.accept(b,20,200)&&!r.enabled); // Must be neutral on arm.
 packet(b,7,false,0);assert(r.accept(b,20,220));
 packet(b,8,true,1);assert(r.accept(b,20,240)&&r.enabled);
 for(int kind=0;kind<11;kind++) {
   auto copy=r;packet(b,9,true,1);
   if(kind==0)b[18]^=1;
   if(kind==1){b[6]|=2;seal(b);} // jump
   if(kind==2){b[6]|=4;seal(b);} // spinner
   if(kind==3){b[12]=1;seal(b);}
   if(kind==4){jf::put16(b+8,1001);seal(b);}
   if(kind==5){b[15]=1;seal(b);}
   if(kind==6){b[7]=3;seal(b);}
   if(kind==7){jf::put16(b+16,22);seal(b);}
   if(kind==8){b[2]=1;seal(b);}
   if(kind==10){b[0]=0;seal(b);}
   assert(!copy.accept(b,kind==9?19:20,260));assert(copy.at==240);copy.expire(340);assert(!copy.enabled);
 }
 packet(b,9,true,1);b[7]=1;seal(b);assert(r.accept(b,20,260)&&!r.enabled); // style change stops
 r.requireDisarm();packet(b,10,true,1);assert(r.accept(b,20,280)&&!r.enabled);
 r.disconnect();assert(!r.fresh(280));r.link=true;
 packet(b,11,true,1);assert(!r.accept(b,20,300));
 packet(b,65535,false,0);assert(r.accept(b,20,320));
 packet(b,0,true,1);assert(r.accept(b,20,340)&&r.enabled); // sequence wrap
 packet(b,65535,true,1);assert(!r.accept(b,20,350));
 // unsigned millis rollover
 r.at=0xfffffff0;assert(r.fresh(20));r.expire(90);assert(!r.enabled);
 puts("AX12 receiver tests passed: framing, neutral arming, deadlines, replay, session, disconnect, unsupported outputs, wrap.");
}
