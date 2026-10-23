#include <cassert>
#include <deque>
#include <map>
#include <vector>
#include "Arduino.h"
#include "M5Unified.h"
#include "driver/twai.h"
uint32_t fakeTime=100,interruptAt=0;
SerialMock Serial; M5Mock M5;
std::deque<twai_message_t> rx;
std::vector<twai_message_t> tx;
std::map<uint16_t,uint32_t> registers;
int motorMode=0,enables=0,countsPerMs=20;
uint32_t lastCommand=100;
bool honorsWatchdog=true,rejectWatch=false,foreign=false,foreignSent=false,probeStops=false,probeEnables=false;
#include "../../firmware/LegacyWatchdogTest/LegacyWatchdogTest.ino"
void modelTick(){if(honorsWatchdog&&motorMode==2&&registers[0x7028]&&fakeTime-lastCommand>=registers[0x7028]/countsPerMs)motorMode=0;}
void feedback(){twai_message_t r={};r.extd=1;r.data_length_code=8;r.identifier=0x0200017e|(uint32_t(motorMode)<<22);rx.push_back(r);}
int twai_receive(twai_message_t* f,int){
 modelTick();
 if(foreign&&!foreignSent&&quiet&&fakeTime-quietBegan>=30){foreignSent=true;*f={};f->identifier=0x0900fd01;f->extd=1;f->data_length_code=8;return 0;}
 if(rx.empty())return -1;*f=rx.front();rx.pop_front();return 0;
}
int twai_transmit(const twai_message_t* f,int){
 assert(!quiet);modelTick();lastCommand=fakeTime;tx.push_back(*f);twai_message_t zeroFault={};zeroFault.extd=1;zeroFault.data_length_code=8;zeroFault.identifier=0x1500017e;rx.push_back(zeroFault);
 const unsigned type=f->identifier>>24;assert(type==3||type==4||type==0x11||type==0x12);
 assert((f->identifier&0xffff)==0x7e01&&f->extd&&f->ss&&f->data_length_code==8);
 const uint16_t index=f->data[0]|(uint16_t(f->data[1])<<8);
 const uint32_t bits=uint32_t(f->data[4])|(uint32_t(f->data[5])<<8)|(uint32_t(f->data[6])<<16)|(uint32_t(f->data[7])<<24);
 if(type==4){motorMode=0;feedback();}
 if(type==3){assert(registers[0x7018]==0x3f800000&&(registers[0x7028]==0||registers[0x7028]==20000)&&registers[0x7005]==2&&registers[0x700a]==0);motorMode=2;enables++;feedback();}
 if(type==0x12){assert(index==0x7005||index==0x700a||index==0x7018||index==0x7028);if(index==0x700a){assert(bits==0);if(probeStops)motorMode=0;if(probeEnables)motorMode=2;}if(!(rejectWatch&&index==0x7028&&bits==20000))registers[index]=bits;feedback();}
 if(type==9||type==0x11){
  twai_message_t r={};r.extd=1;r.data_length_code=8;r.identifier=(type<<24)|0x17e;
  r.data[0]=index&255;r.data[1]=index>>8;r.data[2]=type==9?(index==0x7028||index==0x3022?4:6):0;
  uint32_t value=registers[index];if(index==0x3010)value=registers[0x700a];
  for(int i=0;i<4;i++)r.data[i+4]=(value>>(i*8))&255;rx.push_back(r);
 }
 return 0;
}
void reset(){fakeTime=100;interruptAt=0;rx.clear();tx.clear();registers={{0x7018,0x41300000},{0x7028,0},{0x7019,0x4039999a},{0x3022,0}};motorMode=enables=0;countsPerMs=20;lastCommand=100;honorsWatchdog=true;rejectWatch=foreign=foreignSent=probeStops=probeEnables=false;ready=true;busy=requested=aborted=quiet=foreignTraffic=false;modeCount=reportCount=resetObservedAt=quietBegan=0;sawQuietRun=false;mode=255;faults=0;wantedType=0;readDone=readOK=false;Serial.output.clear();}
void assertPass(){assert(enables==2&&motorMode==0&&registers[0x7028]==20000&&Serial.output.find("WATCHDOG A/B RESULT: PASS")!=std::string::npos);}
void assertFail(){assert(motorMode==0&&Serial.output.find("WATCHDOG A/B RESULT: NOT VERIFIED")!=std::string::npos);}
int main(){
 reset();testWatchdog();assertPass();assert(fakeTime>=3100);
 reset();countsPerMs=200;testWatchdog();assertPass();
 reset();honorsWatchdog=false;testWatchdog();assertFail();assert(enables==2);
 reset();rejectWatch=true;testWatchdog();assertFail();assert(enables==1);
 reset();interruptAt=250;testWatchdog();assertFail();assert(enables==1&&registers[0x7028]==20000);
 reset();foreign=true;testWatchdog();assertFail();assert(foreignSent);
 reset();probeStops=true;testWatchdog();assertFail();assert(enables==1);
 reset();probeEnables=true;testWatchdog();assertFail();assert(enables==0);
 puts("PASS: current RS05 scale and mismatched scale; timeout-disabled A/B control; ignored timeout fails; rejected setting; stop and final nonzero setting after abort; foreign traffic; probe-induced enable/stop rejected; zero speed only; no TX during silence. Simulation only.");
}
