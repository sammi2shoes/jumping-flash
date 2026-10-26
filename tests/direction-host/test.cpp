#include <cassert>
#include <deque>
#include <map>
#include <vector>
#define JF_DUAL_HOST 1
#include "Arduino.h"
#include "M5Unified.h"
#include "driver/twai.h"
uint32_t fakeTime=100,interruptAt=0;
SerialMock Serial;M5Mock M5;
struct Motor{int mode=0;float pos=2.9f;std::map<uint16_t,uint32_t> reg;uint32_t last=0;bool absent=false,honor=true,ignoreStop=false;int enables=0;};
int simulatedBusState=TWAI_STATE_RUNNING;
Motor sim[2];std::deque<twai_message_t> received;
struct Sent{uint32_t at;int motor,type,index;uint32_t bits;};std::vector<Sent> sent;
#include "../../firmware/AX12DirectionTest/AX12DirectionTest.ino"
void advance(int t){
 for(int k=0;k<t;k++){
  ++fakeTime;
  for(auto& m:sim){
   if(m.mode==2)m.pos+=floatOf(m.reg[0x700A])*.001f;
   uint32_t timeout=m.reg[0x7028];if(m.mode==2&&m.honor&&timeout&&uint32_t(fakeTime-m.last)>=timeout/20)m.mode=0;
  }
 }
}
void feedback(int i){twai_message_t f={};f.extd=1;f.data_length_code=8;f.identifier=0x0200007e|(uint32_t(i+1)<<8)|(uint32_t(sim[i].mode)<<22);received.push_back(f);}
int twai_get_status_info(twai_status_info_t*s){*s={};s->state=simulatedBusState;return 0;}
int twai_receive(twai_message_t*f,int){if(received.empty())return -1;*f=received.front();received.pop_front();return 0;}
int twai_transmit(const twai_message_t*f,int){
 assert(f->extd&&f->ss&&f->data_length_code==8&&((f->identifier>>8)&255)==126);
 int type=(f->identifier>>24)&31,i=(f->identifier&255)-1,index=jf::u16(f->data);uint32_t bits;memcpy(&bits,f->data+4,4);
 assert(i==0||i==1);assert(type==3||type==4||type==17||type==18||type==24);
 assert(index!=0x7016&&index!=0x7017); // No position target/profile writes or reads.
 sent.push_back({fakeTime,i,type,index,bits});auto&m=sim[i];m.last=fakeTime;
 if(type==4&&!m.ignoreStop)m.mode=0;
 if(type==3){assert(m.reg[0x7005]==2&&m.reg[0x7018]==bitsOf(1.f));assert(stage==DIR_ARMING||stage==DIR_WATCHDOG_TEST);m.mode=2;++m.enables;}
 if(type==18){assert(index==0x7005||index==0x7018||index==0x7028||index==0x700A);if(index==0x700A)assert(fabsf(floatOf(bits))<=1.f);if(index==0x7018)assert(bits==bitsOf(1.f));m.reg[index]=bits;}
 if(m.absent)return ESP_OK;
 if(type==17){twai_message_t r={};r.extd=1;r.data_length_code=8;r.identifier=0x1100007e|(uint32_t(i+1)<<8);r.data[0]=index;r.data[1]=index>>8;uint32_t b=index==0x7019?bitsOf(m.pos):index==0x701C?bitsOf(i?23.f:24.f):m.reg[index];memcpy(r.data+4,&b,4);received.push_back(r);}
 else feedback(i);
 return ESP_OK;
}
void tick(int ms){uint32_t end=fakeTime+ms;while(fakeTime<end){uint32_t before=fakeTime;loop();if(fakeTime==before)advance(1);}}
void off(){ax12.rx.enabled=false;ax12.rx.sb=0;ax12.rx.forward=ax12.rx.turn=0;}
void reset(){
 simulatedBusState=TWAI_STATE_RUNNING;fakeTime=100;interruptAt=0;sim[0]=Motor();sim[1]=Motor();received.clear();sent.clear();Serial={};M5={};ax12={};
 legs[0]=Leg(1,-1.f);legs[1]=Leg(2,1.f);stage=DIR_OFFLINE;
 watchdogFailed=false;driverReady=configured=cancelled=stopRequested=uncertain=testing=quiet=foreignTraffic=readDone=readOK=pending=testRequested=false;
 pendingMotor=-1;pendingIndex=0;readBits=retryAt=pollAt=signatureAt=displayAt=burstAt=0;selection=0;signatureLeg=0;
 setup();assert(sim[0].enables==0&&sim[1].enables==0);
 for(auto&f:sent)assert(f.type==4);
 tick(1700);assert(configured&&stage==DIR_DISABLED&&!allQualified());
}
void qualify(){off();ax12.rx.maintenanceRequested=true;tick(10);assert(allQualified()&&stage==DIR_DISABLED);assert(sim[0].mode==0&&sim[1].mode==0);assert(sim[0].reg[0x7028]==20000&&sim[1].reg[0x7028]==20000);}
void enable(int which){off();ax12.rx.style=which;tick(100);ax12.rx.sb=1;ax12.rx.enabled=true;tick(100);assert(stage==DIR_ACTIVE);assert(sim[0].mode==(which!=1?2:0));assert(sim[1].mode==(which!=0?2:0));}
int main(){
 reset();ax12.rx.enabled=true;ax12.rx.sb=1;tick(100);assert(sim[0].enables==0&&sim[1].enables==0);qualify();
 for(auto&f:sent)if(f.type==18&&f.index==0x700A)assert(f.bits==bitsOf(0.f));
 // Button B has no reference/zero function and never produces a command.
 M5.BtnB.down=true;tick(2200);M5.BtnB.down=false;assert(stage==DIR_DISABLED);
 enable(0);float r=sim[0].pos,l=sim[1].pos;ax12.rx.forward=1000;tick(500);assert(sim[0].pos<r-.1f&&sim[1].pos==l);
 ax12.rx.forward=0;tick(100);assert(floatOf(sim[0].reg[0x700A])==0.f);off();tick(100);assert(sim[0].mode==0&&sim[1].mode==0);
 enable(1);r=sim[0].pos;l=sim[1].pos;ax12.rx.forward=1000;tick(500);assert(sim[1].pos>l+.1f&&sim[0].pos==r);off();tick(100);
 enable(2);ax12.rx.forward=1000;tick(2000);assert(stage==DIR_DISABLED&&sim[0].mode==0&&sim[1].mode==0);assert(Serial.output.find("80-degree measured rotation guard")!=std::string::npos);
 // With SB still armed, a new command cannot restart after a run guard.
 ax12.rx.forward=1000;tick(200);assert(stage==DIR_DISABLED);
 off();tick(100);enable(2);ax12.rx.forward=100;tick(2800);assert(stage==DIR_DISABLED);assert(Serial.output.find("2.5-second run deadline")!=std::string::npos);
 off();tick(100);enable(2);ax12.rx.forward=-1000;tick(200);ax12.reports=false;tick(300);assert(sim[0].mode==0&&sim[1].mode==0);ax12.reports=true;tick(200);assert(stage==DIR_DISABLED);
 // Loss of even the unselected motor blocks both and enforces silence for CAN watchdog fallback.
 off();tick(100);enable(0);ax12.rx.forward=500;tick(100);sim[1].absent=true;sim[1].ignoreStop=true;tick(300);assert(stage==DIR_OFFLINE&&!configured);assert(sim[0].mode==0);
 auto n=sent.size();tick(900);assert(sent.size()==n); // no maintenance traffic sustaining a failed bus
 reset();qualify();sim[1].absent=true;off();ax12.rx.style=2;tick(100);ax12.rx.sb=1;ax12.rx.enabled=true;tick(500);
 assert(stage==DIR_OFFLINE&&sim[0].mode==0&&sim[1].mode==0); // ID1 was enabled, then ID2 setup failed: ID1 is stopped.
 reset();qualify();enable(2);ax12.rx.forward=1000;tick(100);ax12.rx.maintenanceStopRequested=true;tick(100);assert(stage==DIR_DISABLED&&sim[0].mode==0&&sim[1].mode==0);
 reset();off();ax12.cancelAt=fakeTime+300;ax12.rx.maintenanceRequested=true;tick(10);assert(!allQualified()&&sim[0].mode==0&&sim[1].mode==0);assert(sim[0].reg[0x7028]==20000&&sim[1].reg[0x7028]==20000);
 reset();simulatedBusState=TWAI_STATE_BUS_OFF;tick(100);assert(stage==DIR_OFFLINE&&!configured);off();tick(2800);assert(configured&&stage==DIR_DISABLED&&simulatedBusState==TWAI_STATE_RUNNING);assert(sim[0].enables==0&&sim[1].enables==0);
 reset();sim[1].honor=false;off();ax12.rx.maintenanceRequested=true;tick(10);assert(!allQualified());assert(sim[0].mode==0&&sim[1].mode==0);assert(sim[0].reg[0x7028]==20000&&sim[1].reg[0x7028]==20000);
 puts("PASS: dual ID routing, watchdog A/B gate, direction signs, independent/both selection, no zero/position commands, neutral zero speed, 80deg/time guards, BLE loss, no restart, and both-stop on loss of either motor. Simulation only.");
}
