#include <cassert>
#include "Arduino.h"
#include "M5Unified.h"
#include "driver/twai.h"
uint32_t fakeTime=100,interruptAt=0;
SerialMock Serial; M5Mock M5;
#include "../../firmware/WatchdogObserver/WatchdogObserver.ino"
int twai_receive(twai_message_t*,int){return -1;}
int twai_transmit(const twai_message_t*,int){assert(false && "observer must never transmit control frames");return -1;}
void feed(uint32_t id,uint32_t at,bool nonzero=false){fakeTime=at;twai_message_t f={};f.extd=1;f.data_length_code=8;f.identifier=id;f.data[0]=nonzero;observe(f);}
void reset(){ready=haveCommand=sawRun=explicitStop=timedStop=invalid=false;lastCommand=lastSummary=0;lastMode=lastFaults=255;Serial.output.clear();}
int main(){
 reset();setup();feed(0x03007e01,100);feed(0x0280017e,101);feed(0x12007e01,150);feed(0x18800101,1140);feed(0x18000101,1150);assert(timedStop&&!invalid&&Serial.output.find("1000 ms")!=std::string::npos&&Serial.output.find("TIMING consistent")!=std::string::npos);feed(0x12007e01,1200);feed(0x18000101,1210);assert(!invalid);feed(0x03007e01,1215);assert(timedStop&&invalid);feed(0x18800101,1220);assert(invalid&&Serial.output.find("UNEXPECTED RUN")!=std::string::npos);
 reset();feed(0x03007e01,100);feed(0x0280017e,101);feed(0x04007e01,400);feed(0x0200017e,410);assert(!timedStop&&Serial.output.find("not a watchdog observation")!=std::string::npos);
 reset();feed(0x03007e01,100);feed(0x0280017e,101);feed(0x1200fd01,500);feed(0x0200017e,1100);assert(invalid&&Serial.output.find("TIMING NOT VERIFIED")!=std::string::npos);
 reset();feed(0x03007e01,100);feed(0x0280017e,101);feed(0x1500017e,1000,true);feed(0x0200017e,1100);assert(invalid);
 puts("PASS: receive-only observer; 1-second observation; active feedback addressed to motor; explicit stop excluded; foreign traffic and faults invalidate result; unexpected restart flagged.");
}
