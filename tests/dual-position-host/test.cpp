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
struct Motor{int mode=0;float pos=2.9f;std::map<uint16_t,uint32_t> reg;uint32_t last=0;bool absent=false,honor=true,ignoreStop=false,jammed=false;int enables=0;};
int simulatedBusState=TWAI_STATE_RUNNING;
float targetReadbackOffset=0;bool rejectLeftCurrentOnArm=false;
Motor sim[2];std::deque<twai_message_t> received;
struct Sent{uint32_t at;int motor,type,index;uint32_t bits;};std::vector<Sent> sent;
#include "../../firmware/AX12DualPosition/AX12DualPosition.ino"
void advance(int t){
 for(int k=0;k<t;k++){
  ++fakeTime;
  for(auto& m:sim){
   if(m.mode==2&&!m.jammed){
     if(m.reg[0x7005]==2)m.pos+=floatOf(m.reg[0x700A])*.001f;
     else {float delta=floatOf(m.reg[0x7016])-m.pos;float step=floatOf(m.reg[0x7017])*.001f;m.pos+=fmaxf(-step,fminf(step,delta));}
   }
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
 if(type==18 && index==0x7016 && referenceValid){float relative=legs[i].sign*(floatOf(bits)-legs[i].origin);assert(relative>=-.0005f&&relative<=TRAVEL+.0005f);}

 sent.push_back({fakeTime,i,type,index,bits});auto&m=sim[i];m.last=fakeTime;
 if(type==4&&!m.ignoreStop)m.mode=0;
 if(type==3){assert((m.reg[0x7005]==2||m.reg[0x7005]==5)&&m.reg[0x7018]==bitsOf(1.f));assert(stage==DIR_ARMING||stage==DIR_WATCHDOG_TEST);m.mode=2;++m.enables;}
 if(type==18){assert(index==0x7005||index==0x7018||index==0x7028||index==0x700A||index==0x7016||index==0x7017);if(index==0x700A)assert(fabsf(floatOf(bits))<=1.f);if(index==0x7018)assert(bits==bitsOf(1.f));m.reg[index]=bits;if(rejectLeftCurrentOnArm&&stage==DIR_ARMING&&i==1&&index==0x7018)m.reg[index]=bitsOf(.5f);}
 if(m.absent)return ESP_OK;
 if(type==17){twai_message_t r={};r.extd=1;r.data_length_code=8;r.identifier=0x1100007e|(uint32_t(i+1)<<8);r.data[0]=index;r.data[1]=index>>8;uint32_t b=index==0x7019?bitsOf(m.pos):index==0x701C?bitsOf(i?23.f:24.f):m.reg[index];if(index==0x7016)b=bitsOf(floatOf(b)+targetReadbackOffset);memcpy(r.data+4,&b,4);received.push_back(r);}
 else feedback(i);
 return ESP_OK;
}
void tick(int ms){uint32_t end=fakeTime+ms;while(fakeTime<end){uint32_t before=fakeTime;loop();if(fakeTime==before)advance(1);}}
void off(){ax12.rx.enabled=false;ax12.rx.sb=0;ax12.rx.forward=ax12.rx.turn=0;}
void reset(){
 simulatedBusState=TWAI_STATE_RUNNING;targetReadbackOffset=0;rejectLeftCurrentOnArm=false;fakeTime=100;interruptAt=0;sim[0]=Motor();sim[1]=Motor();received.clear();sent.clear();Serial={};M5={};ax12={};
 legs[0]=Leg(1,-1.f);legs[1]=Leg(2,1.f);stage=DIR_OFFLINE;
 watchdogFailed=false;driverReady=configured=cancelled=stopRequested=uncertain=testing=quiet=foreignTraffic=readDone=readOK=pending=testRequested=false;
 pendingMotor=-1;pendingIndex=0;readBits=retryAt=pollAt=signatureAt=displayAt=0;selection=0;signatureLeg=0;
 referenceCaptured=referenceValid=referenceReleased=false;referenceHoldAt=0;
 setup();assert(sim[0].enables==0&&sim[1].enables==0);
 for(auto&f:sent)assert(f.type==4);
 tick(1700);assert(configured&&stage==DIR_DISABLED&&!allQualified());
}
void qualify(){off();ax12.rx.maintenanceRequested=true;tick(10);assert(allQualified()&&stage==DIR_DISABLED);assert(sim[0].mode==0&&sim[1].mode==0);assert(sim[0].reg[0x7028]==20000&&sim[1].reg[0x7028]==20000);}

void reference(){off();M5.BtnB.down=false;tick(100);M5.BtnB.down=true;tick(2150);M5.BtnB.down=false;tick(100);assert(referenceCaptured&&referenceValid);}
void enable(){off();tick(100);ax12.rx.sb=1;ax12.rx.enabled=true;tick(100);assert(stage==DIR_ACTIVE);assert(sim[0].mode==2&&sim[1].mode==2);}
int main(){
 reset();M5.BtnB.down=true;tick(2200);assert(!referenceCaptured);M5.BtnB.down=false;
 qualify();ax12.rx.sb=1;ax12.rx.enabled=true;tick(100);assert(stage==DIR_DISABLED&&sim[0].mode==0&&sim[1].mode==0);
 // Fixed per-motor origins, deliberately different encoder positions.
 off();sim[0].pos=4.f;sim[1].pos=-2.f;tick(100);reference();assert(legs[0].origin==4.f&&legs[1].origin==-2.f);
 enable();ax12.rx.forward=1000;tick(600);assert(travel(0)>.4f&&travel(1)>.4f);assert(sim[0].pos<4.f&&sim[1].pos>-2.f);
 ax12.rx.forward=0;tick(200);float r=sim[0].pos,l=sim[1].pos;tick(500);assert(fabsf(r-sim[0].pos)<.0005f&&fabsf(l-sim[1].pos)<.0005f);assert(stage==DIR_ACTIVE);
 off();tick(100);assert(referenceValid&&sim[0].mode==0&&sim[1].mode==0);enable();assert(legs[0].origin==4.f&&legs[1].origin==-2.f);
 ax12.rx.forward=1000;tick(2500);assert(stage==DIR_ACTIVE);assert(fabsf(travel(0)-TRAVEL)<.002f&&fabsf(travel(1)-TRAVEL)<.002f);
 ax12.rx.forward=-1000;tick(2500);assert(stage==DIR_ACTIVE&&fabsf(travel(0))<.002f&&fabsf(travel(1))<.002f);
 // Mid-stroke stop/rearm preserves BOTH zeros; holding B cannot overwrite them.
 ax12.rx.forward=1000;tick(600);ax12.rx.forward=0;tick(100);off();tick(100);M5.BtnB.down=true;tick(2200);M5.BtnB.down=false;assert(legs[0].origin==4.f&&legs[1].origin==-2.f);enable();
 ax12.reports=false;tick(200);assert(sim[0].mode==0&&sim[1].mode==0&&referenceValid);ax12.reports=true;tick(200);assert(stage==DIR_DISABLED);enable();
 // No out-of-window rearm; no reference recapture after uncertainty.
 off();tick(100);sim[1].pos+=3.f;tick(100);assert(!referenceValid&&referenceCaptured);M5.BtnB.down=true;tick(2400);assert(!referenceValid);
 reset();qualify();reference();enable();sim[1].absent=true;sim[1].ignoreStop=true;tick(300);assert(stage==DIR_OFFLINE&&!referenceValid&&sim[0].mode==0);auto n=sent.size();tick(900);assert(sent.size()==n);
 reset();qualify();reference();enable();ax12.rx.maintenanceStopRequested=true;tick(100);assert(stage==DIR_DISABLED&&referenceValid&&sim[0].mode==0&&sim[1].mode==0);
 reset();sim[1].honor=false;off();ax12.rx.maintenanceRequested=true;tick(10);assert(!allQualified()&&!referenceValid&&sim[0].mode==0&&sim[1].mode==0);
 reset();targetReadbackOffset=.0001f;assert(writeChecked(0,0x7016,bitsOf(sim[0].pos)));targetReadbackOffset=.002f;assert(!writeChecked(0,0x7016,bitsOf(sim[0].pos))&&uncertain);
 reset();qualify();reference();int before=sim[0].enables;rejectLeftCurrentOnArm=true;off();tick(100);ax12.rx.sb=1;ax12.rx.enabled=true;tick(200);assert(sim[0].enables==before+1);assert(sim[0].mode==0&&sim[1].mode==0&&!referenceValid);
 reset();qualify();reference();twai_message_t boot={};boot.extd=1;boot.data_length_code=8;boot.identifier=0x000002FE;received.push_back(boot);tick(100);assert(!referenceValid&&referenceCaptured);
 reset();qualify();reference();enable();sim[1].jammed=true;ax12.rx.forward=1000;tick(500);assert(stage==DIR_DISABLED&&sim[0].mode==0&&sim[1].mode==0);assert(Serial.output.find("tracking error exceeds")!=std::string::npos);
 puts("PASS: dual fixed independent references, mirrored position targets, both watchdog gate, no boot enable, no encoder reset, endpoint clamps, neutral hold, no re-zero on arm/B, BLE loss preserves trusted origins, CAN uncertainty invalidates both, both-stop and no restart. Simulated hardware only.");
}
