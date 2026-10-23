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
std::map<uint16_t,uint32_t> values;
bool badStop=false,badKind=false,rejectWrite=false;
#include "../../firmware/LegacySetup/LegacySetup.ino"
int twai_receive(twai_message_t* f,int){if(rx.empty())return -1;*f=rx.front();rx.pop_front();return 0;}
int twai_transmit(const twai_message_t* f,int){
 tx.push_back(*f);const auto type=f->identifier>>24;
 assert(type==4||type==8||type==9); // No enable, OTA, save, or calibration.
 assert((f->identifier&0xFFFF)==0x7E01&&f->extd&&f->ss&&f->data_length_code==8);
 const uint16_t index=f->data[0]|(uint16_t(f->data[1])<<8);
 if(type==4){
  for(auto b:f->data)assert(b==0);
  twai_message_t r={};r.extd=1;r.data_length_code=8;r.identifier=0x0200017E|(badStop?0x800000:0);rx.push_back(r);
 }
 if(type==8){
  assert(index==0x2019||index==0x200C);assert(f->data[3]==0);
  assert(f->data[2]==(index==0x2019?6:4));
  uint32_t bits=uint32_t(f->data[4])|(uint32_t(f->data[5])<<8)|(uint32_t(f->data[6])<<16)|(uint32_t(f->data[7])<<24);
  assert(bits==(index==0x2019?0x3E99999Au:4000u));
  if(!rejectWrite)values[index]=bits;
 }
 if(type==9){
  for(int i=2;i<8;i++)assert(f->data[i]==0);
  twai_message_t r={};r.extd=1;r.data_length_code=8;r.identifier=0x0900017E;
  r.data[0]=index&255;r.data[1]=index>>8;r.data[2]=index==0x2019?6:badKind?6:4;
  for(int i=0;i<4;i++)r.data[4+i]=(values[index]>>(8*i))&255;
  rx.push_back(r);
 }
 return 0;
}
void reset(){rx.clear();tx.clear();values={{0x2019,0x41300000},{0x200C,0}};badStop=badKind=rejectWrite=false;Serial.output.clear();}
int writes(){int count=0;for(auto f:tx)count+=(f.identifier>>24)==8;return count;}
int main(){
 reset();configure();assert(values[0x2019]==0x3E99999A&&values[0x200C]==4000&&writes()==2);assert(Serial.output.find("READBACK VERIFIED")!=std::string::npos);
 reset();badStop=true;configure();assert(writes()==0);
 reset();badKind=true;configure();assert(writes()==0);
 reset();rejectWrite=true;configure();assert(writes()==1&&values[0x200C]==0&&Serial.output.find("FAILED / INCOMPLETE")!=std::string::npos);
 puts("PASS: legacy read/write packet layout, only two permitted parameter writes, stop/header prerequisites, rejected-write handling, exact final readback, and no enable/erase/save frames.");
}
