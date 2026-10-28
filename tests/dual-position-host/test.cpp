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
int dropReads=0,dropMotor=0,dropIndex=0x7019;
int transientTargetMismatches=0;bool chatterOnEnable=false,staleStopReply=false;
uint32_t referenceMoveAt=0;
uint32_t delayedStateAt=0;twai_message_t delayedStateReply={};
Motor sim[2];std::deque<twai_message_t> received;
struct Sent{uint32_t at;int motor,type,index;uint32_t bits;};std::vector<Sent> sent;
#include "../../firmware/AX12DualPosition/AX12DualPosition.ino"
void advance(int t){
 for(int k=0;k<t;k++){
  ++fakeTime;
  if(referenceMoveAt&&fakeTime>=referenceMoveAt){sim[0].pos+=.01f;referenceMoveAt=0;}
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
int twai_receive(twai_message_t*f,int){if(delayedStateAt&&millis()>=delayedStateAt){received.push_back(delayedStateReply);delayedStateAt=0;}if(received.empty())return -1;*f=received.front();received.pop_front();return 0;}
int twai_transmit(const twai_message_t*f,int){
 assert(f->extd&&f->ss==(((f->identifier>>24)&31)==17?0u:1u)&&f->data_length_code==8&&((f->identifier>>8)&255)==126);
 int type=(f->identifier>>24)&31,i=(f->identifier&255)-1,index=jf::u16(f->data);uint32_t bits;memcpy(&bits,f->data+4,4);
 assert(i==0||i==1);assert(type==3||type==4||type==17||type==18||type==24);
 if(type==18 && index==0x7016 && referenceValid){float relative=legs[i].sign*(floatOf(bits)-legs[i].origin);assert(relative>=-.0005f&&relative<=TRAVEL+.0005f);}

 sent.push_back({fakeTime,i,type,index,bits});auto&m=sim[i];m.last=fakeTime;
 int priorMode=m.mode;
 if(type==4&&!m.ignoreStop)m.mode=0;
 if(type==3){assert((m.reg[0x7005]==2||m.reg[0x7005]==5)&&m.reg[0x7018]==bitsOf(1.f));assert(stage==DIR_ARMING||stage==DIR_WATCHDOG_TEST);m.mode=2;++m.enables;}
 if(type==18){assert(index==0x7005||index==0x7018||index==0x7028||index==0x700A||index==0x7016||index==0x7017);if(index==0x700A)assert(fabsf(floatOf(bits))<=1.f);if(index==0x7018)assert(bits==bitsOf(1.f));m.reg[index]=bits;if(rejectLeftCurrentOnArm&&stage==DIR_ARMING&&i==1&&index==0x7018)m.reg[index]=bitsOf(.5f);}
 if(m.absent)return ESP_OK;
 if(type==17&&i==dropMotor&&index==dropIndex&&dropReads>0){--dropReads;return ESP_OK;}
 if(type==17){twai_message_t r={};r.extd=1;r.data_length_code=8;r.identifier=0x1100007e|(uint32_t(i+1)<<8);r.data[0]=index;r.data[1]=index>>8;uint32_t b=index==0x7019?bitsOf(m.pos):index==0x701C?bitsOf(i?23.f:24.f):m.reg[index];if(index==0x7016){b=bitsOf(floatOf(b)+targetReadbackOffset);if(transientTargetMismatches>0){--transientTargetMismatches;b=bitsOf(floatOf(b)+.01f);}}memcpy(r.data+4,&b,4);received.push_back(r);}
 else if(type==4&&staleStopReply&&priorMode==2){
   staleStopReply=false;feedback(i);delayedStateReply=received.back();received.pop_back();delayedStateAt=millis()+10;
   sim[i].mode=2;feedback(i);sim[i].mode=0;
 }else feedback(i);
 if(type==3&&stage==DIR_ARMING&&chatterOnEnable){chatterOnEnable=false;ax12.rx.requireDisarm();ax12.rx.sb=1;ax12.rx.enabled=true;}

 return ESP_OK;
}
void tick(int ms){uint32_t end=fakeTime+ms;while(fakeTime<end){uint32_t before=fakeTime;loop();if(fakeTime==before)advance(1);}}
void off(){if(ax12.rx.enabled)++ax12.rx.disarmGeneration;ax12.rx.enabled=false;ax12.rx.sb=0;ax12.rx.forward=ax12.rx.turn=0;}
void reset(){
 simulatedBusState=TWAI_STATE_RUNNING;targetReadbackOffset=0;rejectLeftCurrentOnArm=false;fakeTime=100;interruptAt=0;sim[0]=Motor();sim[1]=Motor();received.clear();sent.clear();Serial={};M5={};ax12={};
 legs[0]=Leg(1,-1.f);legs[1]=Leg(2,1.f);stage=DIR_OFFLINE;
 watchdogFailed=false;driverReady=configured=cancelled=stopRequested=uncertain=testing=quiet=foreignTraffic=readDone=readOK=pending=testRequested=referenceRequested=false;
 pendingMotor=-1;pendingIndex=0;readBits=retryAt=pollAt=signatureAt=displayAt=0;selection=0;signatureLeg=0;
 referenceCaptured=referenceValid=referenceReleased=false;referenceHoldAt=0;
 armedGeneration=armRequestAt=armRequestGeneration=0;armRequestStyle=0;armRequestPending=false;
 transientTargetMismatches=0;chatterOnEnable=staleStopReply=false;delayedStateAt=referenceMoveAt=0;
 communicationFault=referenceRecoverable=referenceFault=false;diagnostic=0;readRetries=0;dropReads=0;dropMotor=0;dropIndex=0x7019;
 setup();assert(sim[0].enables==0&&sim[1].enables==0);
 for(auto&f:sent)assert(f.type==4);
 tick(1700);assert(configured&&stage==DIR_DISABLED&&allQualified());
}
void qualify(){off();tick(10);assert(allQualified()&&stage==DIR_DISABLED);assert(sim[0].mode==0&&sim[1].mode==0);assert(sim[0].reg[0x7028]==20000&&sim[1].reg[0x7028]==20000);}


void reference(){off();M5.BtnB.down=false;tick(100);M5.BtnB.down=true;tick(2150);M5.BtnB.down=false;tick(100);assert(referenceCaptured&&referenceValid);}
void enable(){off();tick(100);ax12.rx.sb=1;ax12.rx.enabled=true;tick(350);assert(stage==DIR_ACTIVE);assert(sim[0].mode==2&&sim[1].mode==2);}
int main(){
 reset();assert(sim[0].enables==0&&sim[1].enables==0); // automatic config NEVER energizes either motor
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
 reset();qualify();reference();enable();sim[1].absent=true;sim[1].ignoreStop=true;tick(300);assert(stage==DIR_OFFLINE&&!referenceValid&&referenceRecoverable&&!referenceFault&&sim[0].mode==0);auto n=sent.size();tick(900);assert(sent.size()==n);
 sim[1].absent=sim[1].ignoreStop=false;off();int enables=sim[0].enables+sim[1].enables;tick(1600);assert(configured&&referenceValid&&allQualified()&&!referenceRecoverable);assert(sim[0].enables+sim[1].enables==enables);assert(stage==DIR_DISABLED);enable();
 reset();qualify();reference();enable();ax12.rx.maintenanceStopRequested=true;tick(100);assert(stage==DIR_DISABLED&&referenceValid&&sim[0].mode==0&&sim[1].mode==0);
 reset();sim[1].honor=false;off();ax12.rx.maintenanceRequested=true;tick(10);assert(!allQualified()&&!referenceValid&&sim[0].mode==0&&sim[1].mode==0);
 reset();targetReadbackOffset=.0001f;assert(writeChecked(0,0x7016,bitsOf(sim[0].pos)));targetReadbackOffset=.002f;assert(!writeChecked(0,0x7016,bitsOf(sim[0].pos))&&uncertain);
 reset();qualify();reference();int before=sim[0].enables;rejectLeftCurrentOnArm=true;off();tick(100);ax12.rx.sb=1;ax12.rx.enabled=true;tick(450);assert(sim[0].enables==before+1);assert(sim[0].mode==0&&sim[1].mode==0&&!referenceValid);
 reset();qualify();reference();twai_message_t boot={};boot.extd=1;boot.data_length_code=8;boot.identifier=0x000002FE;received.push_back(boot);tick(100);assert(!referenceValid&&referenceCaptured);
 reset();qualify();reference();enable();sim[1].jammed=true;ax12.rx.forward=1000;tick(500);assert(stage==DIR_DISABLED&&sim[0].mode==0&&sim[1].mode==0);assert(Serial.output.find("tracking error exceeds")!=std::string::npos);
 // A single lost reply is recovered while armed, without reconfiguration, enable, STOP or zero changes.
 reset();qualify();reference();enable();float savedR=legs[0].origin,savedL=legs[1].origin;auto boundary=sent.size();dropReads=1;ax12.rx.forward=500;tick(150);
 assert(stage==DIR_ACTIVE&&referenceValid&&allQualified()&&readRetries==1&&legs[0].origin==savedR&&legs[1].origin==savedL);
 for(size_t k=boundary;k<sent.size();k++){assert(sent[k].type!=3&&sent[k].type!=4);if(sent[k].type==18)assert(sent[k].index==0x7016);}
 // Lost target-readback replies retry reads only; no stale target replay.
 boundary=sent.size();dropReads=2;dropIndex=0x7016;tick(150);assert(stage==DIR_ACTIVE&&readRetries==3);
 // Auxiliary voltage loss cannot disable motors that still supply valid control feedback.
 reset();qualify();reference();enable();dropReads=100;dropIndex=0x701C;tick(3500);assert(stage==DIR_ACTIVE&&referenceValid&&allQualified()&&!uncertain&&std::isnan(ax12.voltage));
 // Changed settings during an outage prohibit reference recovery, even after config returns.
 reset();qualify();reference();enable();sim[1].absent=true;tick(250);assert(referenceRecoverable);sim[1].absent=false;sim[1].reg[0x7005]=0;off();tick(1700);assert(!referenceValid&&!referenceRecoverable&&referenceFault);
 // Actual bus-off recovers the driver and validates origins, without any automatic enable.
 reset();qualify();reference();enable();simulatedBusState=TWAI_STATE_BUS_OFF;tick(100);assert(referenceRecoverable&&!referenceValid);off();enables=sim[0].enables+sim[1].enables;tick(3000);assert(configured&&referenceValid&&stage==DIR_DISABLED&&allQualified());assert(sim[0].enables+sim[1].enables==enables);
 // Nonfinite and out-of-range readings get specific hard-fault codes, not a generic CAN label.
 reset();qualify();reference();enable();sim[1].pos=NAN;tick(100);assert(!referenceValid&&referenceFault&&(diagnostic&15)==POSITION_NONFINITE);
 // Three-minute simulated drive with isolated dropped replies: no stop/re-enable/re-zero/config cycles.
 reset();qualify();reference();enable();boundary=sent.size();enables=sim[0].enables+sim[1].enables;
 for(int sec=0;sec<180;sec++){ax12.rx.forward=sec%2? -500:500;if(sec%10==0)dropReads=1;tick(1000);assert(stage==DIR_ACTIVE&&referenceValid&&allQualified());}
 assert(readRetries==18&&sim[0].enables+sim[1].enables==enables);
 for(size_t k=boundary;k<sent.size();k++){assert(sent[k].type!=3&&sent[k].type!=4);if(sent[k].type==18)assert(sent[k].index==0x7016);}
 // Short ON pulses never enable; OFF is not debounced.
 reset();qualify();reference();enables=sim[0].enables+sim[1].enables;
 for(int k=0;k<50;k++){off();tick(30);ax12.rx.sb=1;ax12.rx.enabled=true;tick(30);assert(stage==DIR_DISABLED&&referenceValid);}
 assert(sim[0].enables+sim[1].enables==enables);off();enable();off();tick(10);assert(sim[0].mode==0&&sim[1].mode==0&&referenceValid);
 // An OFF/ON accepted between loop services still cancels an in-progress enable.
 reset();qualify();reference();chatterOnEnable=true;off();tick(100);ax12.rx.sb=1;ax12.rx.enabled=true;tick(350);assert(stage==DIR_DISABLED&&referenceValid&&!uncertain&&sim[0].mode==0&&sim[1].mode==0);
 // Old RUN feedback arriving before STOP acknowledgement is drained without a false reference fault.
 reset();qualify();reference();enable();staleStopReply=true;off();tick(100);assert(stage==DIR_DISABLED&&referenceValid&&!uncertain);
 // One stale target read is confirmed without replaying its write or invalidating zero.
 reset();qualify();reference();enable();transientTargetMismatches=1;tick(100);assert(stage==DIR_ACTIVE&&referenceValid&&!uncertain);assert(Serial.output.find("VERIFY RETRY")!=std::string::npos);
 // Persistent unverified target stops, then validates recovery without overwriting reference.
 targetReadbackOffset=.002f;tick(100);assert(!referenceValid&&referenceRecoverable&&!referenceFault);targetReadbackOffset=0;off();tick(1600);assert(referenceValid&&allQualified()&&stage==DIR_DISABLED);
 // Missing settings replies remain communication faults, not proof of a reset.
 reset();qualify();reference();enable();dropReads=3;dropIndex=0x7005;signatureLeg=0;signatureAt=millis()-500;tick(100);assert(referenceRecoverable&&!referenceFault);dropReads=0;off();tick(1600);assert(referenceValid&&allQualified());
 // App request captures BOTH stationary offsets without any Stick button or motor enable.
 reset();M5.BtnB.down=false;off();enables=sim[0].enables+sim[1].enables;sim[0].pos=3.f;sim[1].pos=-1.f;ax12.rx.referenceRequested=true;tick(150);
 assert(referenceValid&&referenceCaptured&&legs[0].origin==3.f&&legs[1].origin==-1.f&&sim[0].enables+sim[1].enables==enables);
 savedR=legs[0].origin;savedL=legs[1].origin;sim[0].pos-=.1f;tick(100);ax12.rx.referenceRequested=true;tick(150);assert(legs[0].origin==savedR&&legs[1].origin==savedL);
 reset();ax12.rx.sb=1;ax12.rx.referenceRequested=true;tick(150);assert(!referenceCaptured);
 reset();qualify();reference();enable();ax12.rx.referenceRequested=true;tick(100);assert(referenceValid&&stage==DIR_ACTIVE); // ignored while armed
 reset();off();referenceMoveAt=millis()+50;ax12.rx.referenceRequested=true;tick(150);assert(!referenceCaptured&&!referenceValid&&sim[0].mode==0&&sim[1].mode==0);
 // Aileron left extends LEFT only, right extends RIGHT only; elevator still moves both.
 reset();qualify();reference();enable();r=sim[0].pos;l=sim[1].pos;ax12.rx.turn=-1000;tick(500);assert(fabsf(sim[0].pos-r)<.0005f&&sim[1].pos>l+.3f);
 ax12.rx.turn=0;tick(150);ax12.rx.turn=1000;r=sim[0].pos;l=sim[1].pos;tick(500);assert(sim[0].pos<r-.3f&&fabsf(sim[1].pos-l)<.0005f);
 ax12.rx.turn=0;ax12.rx.forward=0;tick(200);r=sim[0].pos;l=sim[1].pos;tick(500);assert(fabsf(sim[0].pos-r)<.0005f&&fabsf(sim[1].pos-l)<.0005f);
 assert(legRate(0,1000,1000)==1.f&&legRate(1,1000,-1000)==1.f);
 assert(legRate(0,-1000,1000)==0.f&&legRate(1,-1000,1000)==-1.f);
 assert(legRate(0,0,80)==0.f&&legRate(1,0,-80)==0.f);
 ax12.rx.turn=1000;tick(2200);assert(fabsf(travel(0)-TRAVEL)<.002f&&travel(1)<TRAVEL-.2f);
 // SC middle: differential from mid-travel; left aileron extends L and retracts R.
 reset();qualify();reference();enable();ax12.rx.forward=1000;tick(650);ax12.rx.forward=0;tick(150);off();tick(100);ax12.rx.style=1;enable();
 r=travel(0);l=travel(1);ax12.rx.turn=-1000;tick(350);assert(travel(0)<r-.2f&&travel(1)>l+.2f);
 savedR=legs[0].origin;savedL=legs[1].origin;ax12.rx.style=2;tick(100);assert(stage==DIR_DISABLED&&referenceValid&&legs[0].origin==savedR&&legs[1].origin==savedL);
 // SC high: aileron selects side, elevator selects direction; aileron alone holds both.
 off();enable();ax12.rx.turn=-1000;ax12.rx.forward=-500;r=travel(0);l=travel(1);tick(350);assert(fabsf(travel(0)-r)<.0005f&&travel(1)<l-.1f);
 ax12.rx.forward=0;tick(150);r=travel(0);l=travel(1);ax12.rx.turn=1000;tick(300);assert(fabsf(travel(0)-r)<.0005f&&fabsf(travel(1)-l)<.0005f);
 ax12.rx.forward=500;tick(350);assert(travel(0)>r+.1f&&fabsf(travel(1)-l)<.0005f);
 assert(legRate(0,-1000,1000,1)==0.f&&legRate(1,-1000,1000,1)==-1.f);
 assert(legRate(0,1000,-251,2)==0.f&&legRate(1,1000,-251,2)==1.f);
 assert(legRate(0,1000,-250,2)==1.f&&legRate(1,1000,-250,2)==1.f);
 puts("PASS: automatic watchdog config without enable, bounded READ-only retries, active continuity through dropped replies, verified reference restoration after communication outage without re-enable, hard reset/settings/encoder faults remain blocked, mirrored limits/hold, explicit A/B failure gate and no auto-rearm. Simulated hardware only.");
}
