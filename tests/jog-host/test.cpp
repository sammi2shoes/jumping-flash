#include <cassert>
#include <deque>
#include <map>
#include <vector>
#include "Arduino.h"
#include "M5Unified.h"
#include "driver/twai.h"
uint32_t fakeTime=100,interruptAt=0;
SerialMock Serial;M5Mock M5;
std::deque<twai_message_t> rx;
std::vector<twai_message_t> tx;
std::map<uint16_t,uint32_t> registers;
int mode=0,enables=0;
uint16_t unsupported=0;
bool dropPosition=false,badPosition=false,excessTravel=false,dropStop=false;
float simPosition=2.9f;
uint32_t simLast=100;
#include "../../firmware/MotorJog/MotorJog.ino"

void feedback(){twai_message_t f={};f.identifier=(2ul<<24)|(uint32_t(mode)<<22)|0x17e;f.extd=1;f.data_length_code=8;rx.push_back(f);}
int twai_receive(twai_message_t* f,int){if(rx.empty())return -1;*f=rx.front();rx.pop_front();return 0;}
int twai_transmit(const twai_message_t* f,int){
 tx.push_back(*f);const auto type=f->identifier>>24;
 assert(f->extd&&f->ss&&f->data_length_code==8&&(f->identifier&0xffff)==0x7e01);
 assert(type==3||type==4||type==0x11||type==0x12);
 if(type==4){mode=0;if(!dropStop)feedback();}
 if(type==3){mode=2;simLast=fakeTime;enables++;feedback();}
 uint16_t index=f->data[0]|(uint16_t(f->data[1])<<8);
 if(type==0x12){
  if(index!=unsupported)registers[index]=uint32_t(f->data[4])|(uint32_t(f->data[5])<<8)|(uint32_t(f->data[6])<<16)|(uint32_t(f->data[7])<<24);
 }
 if(type==0x11){
  if(index==0x7019 && dropPosition && mode==2)return 0;
  twai_message_t r={};r.extd=1;r.data_length_code=8;r.identifier=0x1100017e;
  r.data[0]=index&255;r.data[1]=index>>8;uint32_t bits=registers[index];
  if(index==0x7019){
   if(mode==2){simPosition+=bitsFloat(registers[0x700a])*(fakeTime-simLast)*.001f;simLast=fakeTime;if(excessTravel)simPosition+=.3f;}
   bits=badPosition?0x7fc00000:floatBits(simPosition);
  }
  if(index==unsupported)r.identifier|=1ul<<16;
  for(int b=0;b<4;b++)r.data[b+4]=(bits>>(8*b))&255;
  rx.push_back(r);
 }
 return 0;
}
void reset(){fakeTime=100;interruptAt=0;rx.clear();tx.clear();registers.clear();mode=0;enables=0;unsupported=0;dropPosition=badPosition=excessTravel=dropStop=false;simPosition=2.9f;simLast=100;ready=true;busy=startRequested=abortRequested=responseReady=false;responseOK=false;wantedIndex=0;modeReplyCount=0;lastMode=255;Serial.output.clear();}
int main(){
 reset();runJog();assert(enables==1&&mode==0&&simPosition>2.98f&&simPosition<3.02f);assert(Serial.output.find("JOG FINISHED")!=std::string::npos);assert(registers[0x7028]==4000&&fabsf(bitsFloat(registers[0x7018])-.3f)<.0001f);
 reset();unsupported=0x7018;runJog();assert(enables==0&&mode==0);
 reset();unsupported=0x7028;runJog();assert(enables==0&&mode==0);
 reset();badPosition=true;runJog();assert(enables==0&&mode==0);
 reset();dropStop=true;runJog();assert(enables==0&&Serial.output.find("STOP NOT CONFIRMED")!=std::string::npos);
 reset();interruptAt=400;runJog();assert(mode==0&&Serial.output.find("JOG ABORTED")!=std::string::npos);
 reset();dropPosition=true;runJog();assert(enables==1&&mode==0&&Serial.output.find("JOG ABORTED")!=std::string::npos);
 reset();excessTravel=true;runJog();assert(enables==1&&mode==0&&Serial.output.find("JOG ABORTED")!=std::string::npos);
 puts("PASS: bounded one-shot jog; verified current/watchdog prerequisites; no enable after rejected settings/invalid position/failed stop; serial abort; stale position and excess-travel stop paths.");
}
