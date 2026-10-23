#include <cassert>
#include <deque>
#include <map>
#include <vector>
#include "Arduino.h"
#include "M5Unified.h"
#include "Bluepad32.h"
#include "driver/twai.h"
uint32_t fakeTime=100,interruptAt=0;
SerialMock Serial; M5Mock M5; BP32Mock BP32;
std::deque<twai_message_t> rx;
std::map<uint16_t,uint32_t> regs;
struct Sent{uint32_t time;unsigned kind,index,bits;};
std::vector<Sent> sent;
bool link=true,rejectLimit=false;
uint32_t mailboxUntil=0;
bool mailboxStuck=false;
int twai_get_status_info(twai_status_info_t* s){*s={};s->msgs_to_tx=mailboxStuck || fakeTime<mailboxUntil;return 0;}
int simulatedMode=0,enables=0;
float simulatedPosition=2.9f;
float targetReadOffset=0.000099f;
#include "../../firmware/MotorRecovery/MotorRecovery.ino"
void feedback(){twai_message_t r={};r.identifier=0x0200017e|(uint32_t(simulatedMode)<<22);r.extd=1;r.data_length_code=8;rx.push_back(r);}
int twai_receive(twai_message_t* f,int){if(rx.empty())return -1;*f=rx.front();rx.pop_front();return 0;}
int twai_transmit(const twai_message_t* f,int){
 if(mailboxStuck || fakeTime<mailboxUntil)return -1;
 mailboxUntil=fakeTime+2;
 unsigned type=f->identifier>>24,index=f->data[0]|(unsigned(f->data[1])<<8),bits=0;memcpy(&bits,f->data+4,4);
 assert(type==3||type==4||type==17||type==18||type==24);
 assert(f->extd&&f->ss&&(f->identifier&0xffff)==0x7e01);
 sent.push_back({fakeTime,type,index,bits});if(!link)return 0;
 if(type==4){simulatedMode=0;feedback();}
 if(type==24){assert(f->data[6]==0);feedback();}
 if(type==3){assert(held()&&centered()&&phase==ARMING&&regs[0x7028]==20000&&regs[0x7018]==bitsOf(1.f)&&regs[0x7005]==5);assert(fabsf(floatOf(regs[0x7016])-simulatedPosition)<.0001f);simulatedMode=2;++enables;feedback();}
 if(type==18){assert(index==0x7028||index==0x7018||index==0x7005||index==0x7017||index==0x7016);if(!(rejectLimit&&index==0x7018))regs[index]=bits;if(index==0x7016&&simulatedMode==2)simulatedPosition=floatOf(bits);feedback();}
 if(type==17){twai_message_t r={};r.extd=1;r.data_length_code=8;r.identifier=0x1100017e;r.data[0]=index;r.data[1]=index>>8;uint32_t value=index==0x7019?bitsOf(simulatedPosition):regs[index];if(index==0x7016)value=bitsOf(floatOf(value)+targetReadOffset);memcpy(r.data+4,&value,4);rx.push_back(r);}
 return 0;
}
void tick(unsigned duration){uint32_t end=fakeTime+duration;while(fakeTime<end)loop();}
void resetBench() {
 fakeTime=100;interruptAt=0;rx.clear();regs.clear();sent.clear();link=true;rejectLimit=false;
 mailboxUntil=0;mailboxStuck=false;simulatedMode=0;enables=0;simulatedPosition=2.9f;targetReadOffset=.000099f;
 M5={};BP32={};Serial.output.clear();phase=OFFLINE;pad=nullptr;driverReady=false;
 cancelled=stopRequested=motorFault=false;padReport=retryAt=pollAt=releasedAt=displayAt=0;
 padReports=lastPadLog=modeCount=0;motorMode=255;motorFlags=0;pendingIndex=0;
 pending=readDone=readOK=false;readBits=0;position=target=origin=targetRate=0;
 referenceCaptured=referenceValid=referenceButtonReleased=false;referenceHoldAt=signatureAt=0;
}
void initial(Controller& c) {
 resetBench();setup();c={};c.gamepad=false;connected(&c);
 assert(pad==&c);tick(1800);assert(phase==OFFLINE&&enables==0&&!inputFresh());
 for(const auto& f:sent)assert(f.kind==4);
 const size_t waitingSends=sent.size();
 c.gamepad=true;BP32.reports=false;tick(300);assert(!inputFresh()&&enables==0&&sent.size()==waitingSends);
 BP32.reports=true;tick(250);assert(phase==NEED_REFERENCE&&!referenceCaptured&&enables==0);
 c.hold=true;tick(200);assert(phase==NEED_REFERENCE&&enables==0);
 M5.BtnB.pressed=true;tick(2200);assert(!referenceCaptured); // R1 must be released.
 M5.BtnB.pressed=false;c.hold=false;tick(100);
 M5.BtnB.pressed=true;tick(1000);M5.BtnB.pressed=false;tick(100);assert(!referenceCaptured);
 M5.BtnB.pressed=true;tick(2200);assert(referenceCaptured&&referenceValid&&origin==simulatedPosition);
 M5.BtnB.pressed=false;tick(250);assert(phase==READY&&enables==0&&simulatedMode==0&&padReports>0);
 c.hold=true;tick(100);assert(phase==ACTIVE&&enables==1);
}
int main() {
 Controller c;initial(c);const float fixedZero=origin;
 targetReadOffset=.001f;assert(!writeChecked(0x7016,bitsOf(target)));
 targetReadOffset=NAN;assert(!writeChecked(0x7016,bitsOf(target)));
 targetReadOffset=.000099f;assert(writeChecked(0x7016,bitsOf(target)));
 mailboxStuck=true;uint32_t started=fakeTime;assert(!sendPacket(17));assert(fakeTime-started==20);mailboxStuck=false;
 assert(fabsf(TRAVEL-80.f*PI/180.f)<.000001f&&SPEED==1.f&&CURRENT==1.f&&ACCELERATION==5.f&&EXTEND_SIGN==-1.f);
 c.x=-512;tick(300);assert(phase==ACTIVE&&fabsf(target-origin)<.00001f&&targetRate==0);
 c.x=512;tick(55);assert(targetRate>0&&targetRate<SPEED);
 tick(2500);assert(phase==ACTIVE&&fabsf(target-origin+TRAVEL)<.00001f);
 c.x=-512;tick(55);assert(targetRate<0&&targetRate>-SPEED);
 tick(2500);assert(phase==ACTIVE&&fabsf(target-origin)<.00001f);
 // Stop midway, then re-arm at a displaced position without shifting the limits.
 c.x=512;tick(650);c.x=0;tick(300);float pause=target;tick(1000);assert(phase==ACTIVE&&fabsf(target-pause)<.00001f);
 c.hold=false;tick(250);assert(simulatedMode==0&&phase==READY&&origin==fixedZero);
 simulatedPosition=origin-40.f*PI/180.f;
 c.hold=true;tick(100);assert(phase==ACTIVE&&enables==2&&origin==fixedZero&&fabsf(target-simulatedPosition)<.00001f);
 M5.BtnB.pressed=true;tick(2200);M5.BtnB.pressed=false;assert(origin==fixedZero); // No recapture while armed.
 BP32.reports=false;tick(300);assert(simulatedMode==0&&referenceValid&&origin==fixedZero);
 BP32.reports=true;tick(500);assert(phase==WAIT_RELEASE&&enables==2); // Held enable cannot re-arm.
 c.hold=false;tick(250);c.hold=true;tick(100);assert(phase==ACTIVE&&enables==3&&origin==fixedZero);
 c.online=false;disconnected(&c);tick(20);assert(simulatedMode==0&&referenceValid);
 c.online=true;connected(&c);tick(300);assert(phase==WAIT_RELEASE&&enables==3&&origin==fixedZero);
 c.hold=false;tick(250);c.x=200;c.hold=true;tick(100);assert(phase==WAIT_RELEASE&&enables==3);
 c.x=0;tick(200);assert(enables==3);c.hold=false;tick(250);c.hold=true;tick(100);assert(enables==4);
 M5.BtnA.clicked=true;tick(20);assert(simulatedMode==0&&referenceValid&&origin==fixedZero);
 c.hold=false;tick(250);c.hold=true;tick(100);assert(phase==ACTIVE&&enables==5);
 // CAN uncertainty invalidates trust, but never overwrites the captured origin.
 link=false;tick(300);assert(phase==OFFLINE&&!referenceValid&&origin==fixedZero);
 size_t quietStart=sent.size();tick(500);assert(sent.size()==quietStart);
 link=true;simulatedMode=0;simulatedPosition=4.2f;c.x=0;tick(2000);
 assert(phase==NEED_REFERENCE&&enables==5&&origin==fixedZero&&!referenceValid);
 c.hold=false;tick(250);M5.BtnB.pressed=true;tick(2500);assert(!referenceValid&&origin==fixedZero&&enables==5);
 // Motor reset/settings drift while disabled also blocks a new arm.
 initial(c);const float zero2=origin;c.hold=false;tick(250);regs[0x7005]=0;tick(700);
 assert(!referenceValid&&origin==zero2&&simulatedMode==0);c.hold=true;tick(1800);assert(enables==1);
 // A motor identification broadcast is treated as possible restart evidence.
 initial(c);twai_message_t restart={};restart.identifier=0x000001fe;restart.extd=1;restart.data_length_code=8;rx.push_back(restart);
 tick(20);assert(!referenceValid&&simulatedMode==0&&enables==1);
 // Position outside the fixed interval must not become a new origin on re-arm.
 initial(c);const float zero3=origin;c.hold=false;tick(250);simulatedPosition=origin-TRAVEL-.1f;c.hold=true;tick(100);
 assert(enables==1&&!referenceValid&&origin==zero3&&simulatedMode==0);
 // Changed current limit/readback cannot be silently rewritten then enabled.
 initial(c);c.hold=false;tick(250);rejectLimit=true;regs[0x7018]=bitsOf(2.f);c.hold=true;tick(300);
 assert(!referenceValid&&enables==1&&simulatedMode==0);
 puts("PASS: manual stationary reference required; held R1 retained; neutral active hold; fixed zero across mid-stroke rearm, Bluetooth loss/reconnect, button stop; no recapture; CAN/reset/settings uncertainty blocks enable without changing zero; fixed direction and 0..80 limits; readback/transport guards. Simulated hardware only.");
}
