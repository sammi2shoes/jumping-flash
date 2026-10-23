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
bool link=true,rejectLimit=false,disconnectOnEnable=false,failStop=false,followTarget=true;
uint16_t disconnectDuringRead=0, rejectTelemetryIndex=0;
uint32_t rxReadyAt=0;
uint32_t mailboxUntil=0;
bool mailboxStuck=false;
int twai_get_status_info(twai_status_info_t* s){*s={};s->msgs_to_tx=mailboxStuck || fakeTime<mailboxUntil;return 0;}
int simulatedMode=0,enables=0;
float simulatedPosition=2.9f;
float targetReadOffset=0.000099f;
#include "../../firmware/MotorRecovery/MotorRecovery.ino"
void feedback(){twai_message_t r={};r.identifier=0x0200017e|(uint32_t(simulatedMode)<<22);r.extd=1;r.data_length_code=8;rx.push_back(r);}
int twai_receive(twai_message_t* f,int){if(rx.empty()||fakeTime<rxReadyAt)return -1;*f=rx.front();rx.pop_front();return 0;}
int twai_transmit(const twai_message_t* f,int){
 if(mailboxStuck || fakeTime<mailboxUntil)return -1;
 mailboxUntil=fakeTime+2;
 unsigned type=f->identifier>>24,index=f->data[0]|(unsigned(f->data[1])<<8),bits=0;memcpy(&bits,f->data+4,4);
 assert(type==3||type==4||type==17||type==18||type==24);
 assert(f->extd&&f->ss&&(f->identifier&0xffff)==0x7e01);
 sent.push_back({fakeTime,type,index,bits});if(!link||(type==4&&failStop))return 0;
 if(type==3&&disconnectOnEnable){disconnectOnEnable=false;rxReadyAt=fakeTime+10;BP32.disconnectTarget=pad;BP32.disconnectAt=fakeTime+1;}
 if(type==4){simulatedMode=0;feedback();}
 if(type==24){assert(f->data[6]==0);feedback();}
 if(type==3){assert(held()&&centered()&&phase==ARMING&&regs[0x7028]==20000&&regs[0x7018]==bitsOf(1.f)&&regs[0x7005]==5);assert(fabsf(floatOf(regs[0x7016])-simulatedPosition)<.0001f);simulatedMode=2;++enables;feedback();}
 if(type==18){assert(index==0x7028||index==0x7018||index==0x7005||index==0x7017||index==0x7016);if(!(rejectLimit&&index==0x7018))regs[index]=bits;if(index==0x7016&&simulatedMode==2&&followTarget)simulatedPosition=floatOf(bits);feedback();}
 if(type==17&&disconnectDuringRead==index){
  disconnectDuringRead=0;rxReadyAt=fakeTime+10;BP32.disconnectTarget=pad;BP32.disconnectAt=fakeTime+1;
 }
 if(type==17){twai_message_t r={};r.extd=1;r.data_length_code=8;r.identifier=0x1100017e;if(index==rejectTelemetryIndex)r.identifier|=0x10000;r.data[0]=index;r.data[1]=index>>8;uint32_t value=index==0x7019?bitsOf(simulatedPosition):regs[index];if(index==0x7016)value=bitsOf(floatOf(value)+targetReadOffset);memcpy(r.data+4,&value,4);rx.push_back(r);}
 return 0;
}
void tick(unsigned duration){uint32_t end=fakeTime+duration;while(fakeTime<end)loop();}
void resetBench() {
 fakeTime=100;interruptAt=0;rx.clear();regs.clear();sent.clear();link=true;rejectLimit=false;disconnectOnEnable=failStop=false;followTarget=true;
 disconnectDuringRead=rejectTelemetryIndex=0;rxReadyAt=0;mailboxUntil=0;mailboxStuck=false;simulatedMode=0;enables=0;simulatedPosition=2.9f;targetReadOffset=.000099f;
 M5={};BP32={};Serial.output.clear();phase=OFFLINE;pad=nullptr;driverReady=false;
 cancelled=stopRequested=motorFault=false;padReport=retryAt=pollAt=releasedAt=displayAt=0;
 padReports=lastPadLog=modeCount=0;motorMode=255;motorFlags=0;pendingIndex=0;
 pending=readDone=readOK=false;readBits=0;position=target=origin=targetRate=0;
 targetAtPosition=0;motorIq=motorVelocity=motorVoltage=NAN;
 positionAt=iqAt=velocityAt=voltageAt=traceAt=0;readStatus=255;
 iqAvailable=velocityAvailable=voltageAvailable=true;cycleAtPosition=MANUAL;
 regs[0x701A]=bitsOf(-.75f);regs[0x701B]=bitsOf(-.5f);regs[0x701C]=bitsOf(24.7f);
 clearCycle();cycleButtonDown=true;operationFailure=OP_OK;referenceCaptured=referenceValid=referenceButtonReleased=false;referenceHoldAt=signatureAt=0;
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
 // Controller power-off inside an in-flight CAN read must not invalidate zero.
 for(uint16_t index:{uint16_t(0x7019),uint16_t(0x7016),uint16_t(0x7018),uint16_t(0x701A),uint16_t(0x701B),uint16_t(0x701C)}) {
  initial(c);const float savedZero=origin;disconnectDuringRead=index;tick(800);
  assert(!c.online&&simulatedMode==0&&referenceValid&&origin==savedZero&&enables==1);
  c.online=true;tick(300);assert(phase==WAIT_RELEASE&&enables==1);
  c.hold=false;tick(250);c.hold=true;tick(100);assert(phase==ACTIVE&&enables==2&&origin==savedZero);
 }
 initial(c);const float zero4=origin;c.hold=false;tick(250);disconnectOnEnable=true;c.hold=true;tick(300);
 assert(!c.online&&simulatedMode==0&&referenceValid&&origin==zero4&&enables==2);
 initial(c);const float zero5=origin;disconnectDuringRead=0x7019;failStop=true;tick(800);
 assert(!c.online&&!referenceValid&&origin==zero5); // Cancellation cannot excuse unconfirmed STOP.
 // New cycle trigger is edge gated, runs once, then holds at the fixed endpoint.
 initial(c);const float cycleZero=origin;c.cycleButton=true;tick(4500);
 assert(cycleStage==COMPLETE&&phase==ACTIVE&&fabsf(target-origin+TRAVEL)<.00001f&&origin==cycleZero);
 tick(2000);assert(cycleStage==COMPLETE); // Holding Y never repeats.
 c.x=-512;tick(100);assert(cycleStage==MANUAL&&phase==ACTIVE);c.x=0;tick(300);
 c.cycleButton=false;tick(100);c.cycleButton=true;tick(100);assert(cycleStage==RETRACT);
 c.hold=false;tick(250);assert(cycleStage==MANUAL&&simulatedMode==0&&referenceValid);
 c.hold=true;tick(150);assert(cycleStage==MANUAL&&phase==ACTIVE); // Held Y cannot resume after stop.
 c.cycleButton=false;tick(100);c.cycleButton=true;tick(100);assert(cycleStage==RETRACT);
 c.online=false;tick(100);assert(simulatedMode==0&&cycleStage==MANUAL&&referenceValid);
 initial(c);c.x=200;c.cycleButton=true;tick(100);c.x=0;tick(500);
 assert(cycleStage==MANUAL); // Off-center Y press is discarded, never queued.
 c.cycleButton=false;tick(100);c.cycleButton=true;tick(100);
 for(int n=0;n<50&&cycleStage!=SETTLE;++n)tick(20);
 assert(cycleStage==SETTLE);
 BP32.reports=false;tick(300);assert(cycleStage==MANUAL&&simulatedMode==0&&referenceValid);
 BP32.reports=true;tick(300);assert(phase==WAIT_RELEASE&&cycleStage==MANUAL);
 initial(c);followTarget=false;c.cycleButton=true;tick(7500);
 assert(simulatedMode==0&&cycleStage==MANUAL&&referenceValid&&Serial.output.find("cycle failed to reach/hold endpoint")!=std::string::npos);
 initial(c);c.x=512;tick(55);
 assert(fabsf(targetAtPosition-position)<.0001f&&fabsf(target-targetAtPosition)>.001f);
 tick(150);assert(iqAt>0&&velocityAt>0&&voltageAt>0&&motorIq==-.75f&&motorVelocity==-.5f);
 assert(Serial.output.find("TRACE_HEADER,t_ms,pos_ms")!=std::string::npos&&Serial.output.find("TRACE,")!=std::string::npos);
 initial(c);rejectTelemetryIndex=0x701A;traceAt=0;tick(300);
 assert(phase==ACTIVE&&referenceValid&&!iqAvailable&&std::isnan(motorIq)&&iqAt==0);
 size_t reads=0;for(auto f:sent)if(f.kind==17&&f.index==0x701A)++reads;
 tick(1000);size_t after=0;for(auto f:sent)if(f.kind==17&&f.index==0x701A)++after;
 assert(reads==after&&velocityAt>0&&voltageAt>0); // Unsupported optional field isn't retried or logged as zero.
 puts("PASS: manual stationary reference required; held R1 retained; neutral active hold; fixed zero across mid-stroke rearm, Bluetooth loss/reconnect, button stop; no recapture; CAN/reset/settings uncertainty blocks enable without changing zero; fixed direction and 0..80 limits; readback/transport guards; one-shot cycle, neutral trigger, manual override, cancellation/no resume, endpoint timeout; timestamped telemetry, prior-target alignment, unsupported fields, input loss during telemetry. Simulated hardware only.");
}
