#include <cassert>
#include <deque>
#include "Arduino.h"
#include "M5Unified.h"
#include "driver/twai.h"
uint32_t fakeTime=100,interruptAt=0;
SerialMock Serial; M5Mock M5;
std::deque<twai_message_t> rx;
int sent=0,writes=0;
bool replies=true,rejectTimeout=false;
#include "../../firmware/PostUpdateDiagnostic/PostUpdateDiagnostic.ino"
int twai_receive(twai_message_t* f,int){if(rx.empty())return -1;*f=rx.front();rx.pop_front();return 0;}
int twai_transmit(const twai_message_t* f,int){
 unsigned type=f->identifier>>24;assert(type==4||type==17||type==18);
 assert(f->extd&&f->ss&&f->data_length_code==8&&(f->identifier&0xffff)==0x7e01);
 unsigned index=unsigned(f->data[0])|(unsigned(f->data[1])<<8);
 if(type==18){assert(index==0x7028&&le32(f->data+4)==20000);writes++;}
 else for(int i=(type==17?2:0);i<8;++i)assert(f->data[i]==0);
 ++sent;if(!replies)return 0;
 twai_message_t r={};r.extd=1;r.data_length_code=8;r.identifier=0x0200017e;
 if(type==17){r.identifier=0x1100017e;memcpy(r.data,f->data,8);if(index==0x7028&&!rejectTimeout){r.data[4]=0x20;r.data[5]=0x4e;}if(index==0x3022)r.identifier|=0x10000;}
 rx.push_back(r);return 0;
}
void reset(){sent=writes=0;fakeTime=100;rx.clear();Serial.output.clear();replies=true;rejectTimeout=false;}
int main(){
 setup();assert(sent==0);
 reset();diagnose();assert(sent==10&&writes==1&&timeoutVerified&&stopped);assert(Serial.output.find("id_status=0x01")!=std::string::npos);assert(Serial.output.find("Final disabled feedback confirmed.")!=std::string::npos);
 reset();replies=false;diagnose();assert(sent==1&&writes==0&&!stopped&&Serial.output.find("STOP NOT CONFIRMED")!=std::string::npos);
 reset();rejectTimeout=true;diagnose();assert(sent==4&&writes==1&&!timeoutVerified&&Serial.output.find("Diagnostic aborted")!=std::string::npos);
 int before=sent;request(3,0,"forbidden");request(18,0x7018,"forbidden");request(9,0,"forbidden");assert(sent==before);
 puts("PASS: no boot TX; no enable/motion; RS05 timeout write only after disabled feedback; checked readback; standard reads only; errors retained; stop failure blocks setting change. Simulation only.");
}
