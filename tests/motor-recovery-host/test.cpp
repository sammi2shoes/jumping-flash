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
void initial(Controller& c){
 setup();c={};c.gamepad=false;connected(&c);
 assert(pad==&c);tick(1800);assert(phase==OFFLINE&&enables==0&&!inputFresh());
 for(const auto& f:sent)assert(f.kind==4);
 const size_t waitingSends=sent.size();
 c.gamepad=true;BP32.reports=false;tick(300);assert(!inputFresh()&&enables==0&&sent.size()==waitingSends);
 BP32.reports=true;tick(250);assert(phase==READY&&enables==0&&simulatedMode==0&&padReports>0);
 c.hold=true;tick(100);assert(phase==ACTIVE&&enables==1);
}
int main(){
 Controller c;initial(c);
 // Measured small discrepancy is accepted throughout startup and active control;
 // a larger difference and nonfinite target readback must still be rejected.
 targetReadOffset=0.001f;assert(!writeChecked(0x7016,bitsOf(target)));
 targetReadOffset=NAN;assert(!writeChecked(0x7016,bitsOf(target)));
 targetReadOffset=0.000099f;assert(writeChecked(0x7016,bitsOf(target)));
 mailboxStuck=true;uint32_t started=fakeTime;assert(!sendPacket(17));assert(fakeTime-started==20);mailboxStuck=false;
 c.x=512;tick(2000);assert(phase==ACTIVE&&simulatedPosition<=origin+TRAVEL+.00001f);
 link=false;tick(300);assert(phase==OFFLINE);size_t quietStart=sent.size();uint32_t before=fakeTime;tick(500);assert(sent.size()==quietStart&&fakeTime-before>=500);
 link=true;simulatedMode=0;simulatedPosition=4.2f;c.x=0;tick(2000);assert(phase==WAIT_RELEASE&&enables==1&&fabsf(target-4.2f)<.00001f);
 tick(1500);assert(enables==1);c.hold=false;tick(250);assert(phase==READY);c.hold=true;tick(100);assert(enables==2&&phase==ACTIVE&&fabsf(origin-4.2f)<.00001f);
 BP32.reports=false;tick(300);assert(phase==OFFLINE&&simulatedMode==0);BP32.reports=true;tick(2000);assert(phase==WAIT_RELEASE&&enables==2);
 c.hold=false;tick(200);c.hold=true;tick(100);assert(phase==ACTIVE&&enables==3);
 c.online=false;disconnected(&c);tick(20);assert(phase==OFFLINE&&simulatedMode==0);c.online=true;connected(&c);tick(2000);assert(enables==3&&phase==WAIT_RELEASE);
 c.hold=false;tick(200);c.x=200;c.hold=true;tick(100);assert(phase==WAIT_RELEASE&&enables==3);c.x=0;tick(200);assert(enables==3);c.hold=false;tick(200);c.hold=true;tick(100);assert(enables==4);
 M5.BtnA.clicked=true;tick(20);assert(phase==OFFLINE&&simulatedMode==0);
 c.hold=false;rejectLimit=true;regs[0x7018]=bitsOf(2.f);tick(2000);assert(phase==OFFLINE&&enables==4);
 puts("PASS: late gamepad classification retained; connection without reports never arms; no boot enable; bounded rate/position; loss stops and quiet interval; recovered position preload; held enable cannot rearm; release/press recovery; stale and disconnected Bluetooth stop; off-center press rejected; button stop; failed limit blocks recovery. Simulated hardware only.");
}
