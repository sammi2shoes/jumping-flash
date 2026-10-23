#include <cassert>
#include "Arduino.h"
#include "M5Unified.h"
#include "driver/twai.h"
uint32_t fakeTime=100;
SerialMock Serial;
M5Mock M5;
twai_message_t transmitted;
#include "../../firmware/MotorTelemetry/MotorTelemetry.ino"

twai_message_t reply(size_t i,uint32_t bits) {
 twai_message_t r={};r.identifier=0x1100017E;r.extd=1;r.data_length_code=8;
 r.data[0]=params[i].index&255;r.data[1]=params[i].index>>8;
 for(int b=0;b<4;b++)r.data[b+4]=(bits>>(8*b))&255;
 return r;
}
int main(){
 for(size_t i=0;i<PARAM_COUNT;i++){
  nextParameter=i;requestParameter();
  assert(transmitted.identifier==0x11007E01 && transmitted.extd && transmitted.ss && transmitted.data_length_code==8);
  assert((transmitted.data[0]|(uint16_t(transmitted.data[1])<<8))==params[i].index);
  for(int b=2;b<8;b++)assert(transmitted.data[b]==0);
 }
 nextParameter=0;requestParameter();auto r=reply(0,0x3FC00000); // 1.5f, little endian
 auto wrong=r;wrong.identifier=0x1100027E;receiveParameter(wrong);assert(waiting&&replies==0);
 wrong=r;wrong.data_length_code=7;receiveParameter(wrong);assert(waiting&&replies==0);
 wrong=r;wrong.data[0]^=1;receiveParameter(wrong);assert(waiting&&replies==0);
 receiveParameter(r);assert(!waiting&&replies==1&&Serial.output.find("position,1.50000,rad")!=std::string::npos);
 nextParameter=0;requestParameter();uint32_t before=replies;receiveParameter(reply(0,0x7FC00000));assert(replies==before&&!waiting);
 nextParameter=0;requestParameter();r=reply(0,0);r.identifier|=1u<<16;receiveParameter(r);assert(replies==before&&!waiting);
 canReady=true;nextParameter=1;requestParameter();fakeTime+=201;loop();assert(timeouts==1&&Serial.output.find("TIMEOUT index=0x701B")!=std::string::npos);
 puts("PASS: read-only request encoding; address/index/length filtering; float32 decoding; error/NaN rejection; timeout reporting.");
}
