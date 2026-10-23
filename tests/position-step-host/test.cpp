#include <cassert>
#include <deque>
#include <map>
#include "Arduino.h"
#include "M5Unified.h"
#include "driver/twai.h"
constexpr float PI=3.14159265358979323846f;
uint32_t fakeTime=100,interruptAt=0;
SerialMock Serial; M5Mock M5;
std::deque<twai_message_t> rx;
std::map<uint16_t,uint32_t> registers;
int motorMode=0,enables=0;
bool rejectMode=false,stalled=false,lostStop=false,fault=false;
float pos=2.9f,target=2.9f;
#include "../../firmware/PositionStep/PositionStep.ino"
uint32_t bitsOf(float f){uint32_t b;memcpy(&b,&f,4);return b;}
void feedback(){twai_message_t r={};r.extd=1;r.data_length_code=8;r.identifier=0x0200017e|(uint32_t(motorMode)<<22);rx.push_back(r);}
int twai_receive(twai_message_t* f,int){if(rx.empty())return -1;*f=rx.front();rx.pop_front();return 0;}
int twai_transmit(const twai_message_t* f,int){
 unsigned type=f->identifier>>24;assert(type==3||type==4||type==9||type==17||type==18);
 uint16_t index=f->data[0]|(uint16_t(f->data[1])<<8);uint32_t bits;memcpy(&bits,f->data+4,4);
 if(type==4){if(!lostStop){motorMode=0;feedback();}}
 if(type==3){assert(registers[0x7005]==5&&registers[0x7018]==bitsOf(1.f)&&registers[0x7017]==bitsOf(.1f));assert(fabsf(target-pos)<.0001f);motorMode=2;enables++;feedback();}
 if(type==18){assert(index==0x7005||index==0x7018||index==0x7017||index==0x7016);registers[index]=bits;if(index==0x7016)memcpy(&target,&bits,4);}
 if(type==9||type==17){
  twai_message_t r={};r.extd=1;r.data_length_code=8;r.identifier=(type<<24)|0x17e;r.data[0]=index;r.data[1]=index>>8;
  if(type==17&&rejectMode&&index==0x7005)r.identifier|=0x10000;
  r.data[2]=type==9?(index==0x3022?4:6):0;
  uint32_t value=registers[index];
  if(index==0x3016){if(motorMode==2&&!stalled)pos=target;value=bitsOf(pos);}
  if(index==0x3022)value=fault?1:0;
  memcpy(r.data+4,&value,4);rx.push_back(r);
 }
 return 0;
}
void reset(){fakeTime=100;interruptAt=0;rx.clear();registers.clear();motorMode=enables=0;rejectMode=stalled=lostStop=fault=false;pos=target=2.9f;cancelled=false;Serial.output.clear();}
int main(){
 reset();runStep();assert(enables==1&&motorMode==0&&fabsf(pos-(2.9f+STEP))<.0001f&&Serial.output.find("STEP COMPLETE.")!=std::string::npos);
 reset();rejectMode=true;runStep();assert(!enables&&Serial.output.find("READ rejected")!=std::string::npos);
 reset();stalled=true;runStep();assert(enables==1&&motorMode==0&&fakeTime>=2100&&Serial.output.find("Movement timed out")!=std::string::npos);
 reset();stalled=true;interruptAt=200;runStep();assert(enables==1&&motorMode==0&&fakeTime<400&&Serial.output.find("STEP ABORTED")!=std::string::npos);
 reset();fault=true;runStep();assert(!enables);
 reset();lostStop=true;runStep();assert(!enables&&Serial.output.find("STOP UNCONFIRMED")!=std::string::npos);
 puts("PASS: relative step, limits before enable, target preload, read-error rejection, timeout, serial abort, fault blocks enable, unconfirmed stop reported. Simulated hardware only.");
}
