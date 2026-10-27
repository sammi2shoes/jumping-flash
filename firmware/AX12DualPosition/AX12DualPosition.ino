#include <Arduino.h>
#include <M5Unified.h>
#include <driver/twai.h>
#include <cmath>
#include <cstring>
#ifdef JF_DUAL_HOST
#include "TestTransport.h"
#else
#include "../AX12SingleLeg/AX12Transport.h"
#endif

// RS05 CAN2.0 0.5.0.14. DUAL POSITION BENCH ONLY. Manual fixed references; no gait, jump or ESC output.
constexpr uint8_t HOST=126;
constexpr float CURRENT=1.f, SPEED=1.f, TRAVEL=80.f*PI/180.f;
constexpr uint32_t WATCHDOG=20000;
struct Leg {
  Leg(uint8_t address,float direction):id(address),sign(direction){}
  uint8_t id;
  float sign;
  float origin=0,target=0;
  float position=0, voltage=NAN;
  uint32_t positionAt=0, modeCount=0;
  uint8_t mode=255, flags=0;
  bool watchdog=false;
};
Leg legs[2]={{1,-1.f},{2,1.f}}; // right, left; mirrored extension signs.
enum Stage { DIR_OFFLINE, DIR_DISABLED, DIR_ARMING, DIR_ACTIVE, DIR_STOPPING, DIR_WATCHDOG_TEST };
Stage stage=DIR_OFFLINE;
bool driverReady=false, configured=false, cancelled=false, stopRequested=false, uncertain=false;
bool watchdogFailed=false;
bool testing=false, quiet=false, foreignTraffic=false, readDone=false, readOK=false, pending=false;
bool testRequested=false;
int pendingMotor=-1;
uint16_t pendingIndex=0;
uint32_t readBits=0, retryAt=0, pollAt=0, signatureAt=0, displayAt=0;
uint8_t selection=0;
int signatureLeg=0;
bool referenceCaptured=false,referenceValid=false,referenceReleased=false;
uint32_t referenceHoldAt=0;

uint32_t bitsOf(float f){uint32_t b;memcpy(&b,&f,4);return b;}
float floatOf(uint32_t b){float f;memcpy(&f,&b,4);return f;}
const char* side(int i){return i==0?"RIGHT":"LEFT";}
float travel(int i){return referenceValid?legs[i].sign*(legs[i].position-legs[i].origin):0;}
bool allQualified(){return legs[0].watchdog && legs[1].watchdog;}
bool centered(){auto s=ax12.snapshot();return s.fresh(millis()) && abs(s.forward)<=20 && abs(s.turn)<=20;}
bool inputOkay(){auto s=ax12.snapshot();return s.fresh(millis()) && s.enabled && s.style==selection;}
bool disarmedInput(){return ax12.disarmed() && centered();}
void invalidate(){for(auto& m:legs)m.watchdog=false;referenceValid=false;uncertain=true;}
void telemetry(){
  auto s=ax12.snapshot();
  const bool livePositions=legs[0].positionAt && legs[1].positionAt &&
    uint32_t(millis()-legs[0].positionAt)<250 && uint32_t(millis()-legs[1].positionAt)<250;
  float volts=NAN;
  if(std::isfinite(legs[0].voltage)&&std::isfinite(legs[1].voltage))volts=fminf(legs[0].voltage,legs[1].voltage);
  const bool ready=configured && allQualified() && livePositions && !uncertain && !testing;
  ax12.telemetryDual(ready,referenceValid,legs[0].mode==2||legs[1].mode==2,uncertain,
    travel(1)*180.f/PI,travel(0)*180.f/PI,volts,!allQualified(),watchdogFailed);
}
void service(){
  M5.update();ax12.update();
  if(ax12.takeMaintenanceStop())stopRequested=true;
  if(ax12.takeMaintenanceRequest() && !testing && stage==DIR_DISABLED)testRequested=true;
  if(M5.BtnA.wasPressed() || M5.BtnA.isPressed())stopRequested=true;
  while(Serial.available()){
    char c=Serial.read();if(c=='x'||c=='X')stopRequested=true;
    if((c=='t'||c=='T') && stage!=DIR_ACTIVE && stage!=DIR_ARMING && !testing)testRequested=true;
  }
  if((stage==DIR_ACTIVE||stage==DIR_ARMING) && (!inputOkay()||stopRequested||uncertain || (stage==DIR_ARMING&&!centered())))cancelled=true;
  if(testing && (!disarmedInput()||stopRequested))cancelled=true;
  twai_message_t f;
  for(int n=0;n<64 && twai_receive(&f,0)==ESP_OK;n++){
    if(!f.extd || f.rtr || f.data_length_code!=8)continue;
    uint8_t type=(f.identifier>>24)&31, sender=(f.identifier>>8)&255, dest=f.identifier&255;
    if(quiet && type!=2 && type!=24){foreignTraffic=true;cancelled=true;}
    for(int i=0;i<2;i++){
      auto& m=legs[i];
      if(sender!=m.id)continue;
      if(type==0 && referenceCaptured){invalidate();cancelled=true;}
      if((type==2||type==24) && dest==HOST){
        ++m.modeCount;m.mode=(f.identifier>>22)&3;m.flags=(f.identifier>>16)&63;
        if(m.flags){invalidate();cancelled=true;}
        if(stage==DIR_DISABLED && m.mode!=0){invalidate();cancelled=true;}
        if(stage==DIR_ACTIVE && m.mode!=2){invalidate();cancelled=true;}
      }
      if(type==21 && dest==HOST){for(int k=0;k<8;k++)if(f.data[k]){invalidate();cancelled=true;}}
      if(type==17 && dest==HOST && pending && pendingMotor==i && jf::u16(f.data)==pendingIndex){
        readDone=true;readOK=((f.identifier>>16)&255)==0;memcpy(&readBits,f.data+4,4);
      }
    }
  }
  telemetry();
}
// Bound every wait, service operator/BLE aborts, and never queue stale targets.
bool sendPacket(int i,uint8_t type,const uint8_t* d=nullptr){
  const uint32_t began=millis();twai_status_info_t status={};
  for(;;){
    if(twai_get_status_info(&status)!=ESP_OK || status.state!=TWAI_STATE_RUNNING){uncertain=true;return false;}
    if(!status.msgs_to_tx)break;
    if(uint32_t(millis()-began)>=20){uncertain=true;return false;}
    service();if(type!=4 && (cancelled||stopRequested||uncertain))return false;delay(1);
  }
  service();if(type!=4 && (cancelled||stopRequested||uncertain))return false;
  twai_message_t f={};f.extd=1;f.ss=1;f.data_length_code=8;
  f.identifier=(uint32_t(type)<<24)|(uint32_t(HOST)<<8)|legs[i].id;
  if(d)memcpy(f.data,d,8);
  if(twai_transmit(&f,pdMS_TO_TICKS(10))!=ESP_OK){uncertain=true;return false;}
  return true;
}
bool readValue(int i,uint16_t index,uint32_t& value){
  pendingMotor=i;pendingIndex=index;pending=true;readDone=readOK=false;
  uint8_t d[8]={uint8_t(index),uint8_t(index>>8)};
  bool sent=sendPacket(i,17,d);const uint32_t began=millis();
  while(sent&&!readDone&&!cancelled&&!stopRequested&&uint32_t(millis()-began)<100){service();delay(1);}
  pending=false;
  if(!sent||!readDone||!readOK){if(!cancelled&&!stopRequested)uncertain=true;return false;}
  if(cancelled||stopRequested)return false;value=readBits;return true;
}
bool writeChecked(int i,uint16_t index,uint32_t wanted){
  uint8_t d[8]={uint8_t(index),uint8_t(index>>8)};memcpy(d+4,&wanted,4);uint32_t got;
  if(!sendPacket(i,18,d)||!readValue(i,index,got))return false;
  bool match=got==wanted;
  if(index==0x7016 || index==0x700A)match=std::isfinite(floatOf(got)) && std::isfinite(floatOf(wanted)) && fabsf(floatOf(got)-floatOf(wanted))<=.0005f;
  if(!match){uncertain=true;Serial.printf("ID%u VERIFY FAILED 0x%04X\n",legs[i].id,index);}
  return match;
}
bool stateCommand(int i,uint8_t type,uint8_t expected){
  service();
  uint32_t before=legs[i].modeCount;const uint32_t began=millis();
  if(!sendPacket(i,type))return false;
  while(legs[i].modeCount==before && uint32_t(millis()-began)<100){
    service();if(type!=4 && (cancelled||stopRequested||uncertain))return false;delay(1);
  }
  bool ok=legs[i].modeCount!=before && legs[i].mode==expected && !legs[i].flags;
  if(!ok)uncertain=true;return ok;
}
bool stopBoth(){
  Stage before=stage;stage=DIR_STOPPING;bool all=true;
  for(int i=0;i<2;i++){
    bool ok=false;for(int n=0;n<2&&!ok;n++)ok=stateCommand(i,4,0);
    if(!ok)all=false;
  }
  stage=before;return all;
}
void loseControl(const char* reason){
  bool wasUncertain=uncertain;
  stage=DIR_STOPPING;ax12.requireDisarm();pending=false;
  bool stopped=stopBoth();
  if(wasUncertain||uncertain||!stopped)invalidate();
  cancelled=stopRequested=false;testing=quiet=false;testRequested=false;
  if(!stopped||uncertain){configured=false;stage=DIR_OFFLINE;retryAt=millis()+1250;}
  else stage=DIR_DISABLED;
  Serial.printf("BOTH DISABLED: %s. STOP %s. Watchdog qualified R=%u L=%u.\n",reason,stopped?"confirmed":"UNCONFIRMED: use BOTH power cutoffs",legs[0].watchdog,legs[1].watchdog);
}
bool readPosition(int i){
  uint32_t b;if(!readValue(i,0x7019,b))return false;
  float p=floatOf(b);if(!std::isfinite(p)){uncertain=true;return false;}
  if(referenceValid){
    float q=legs[i].sign*(p-legs[i].origin);
    if(q<-.02f || q>TRAVEL+.02f || (legs[i].positionAt && fabsf(p-legs[i].position)>.3f)){invalidate();return false;}
  }
  legs[i].position=p;legs[i].positionAt=millis();return true;
}
bool settings(int i){
  uint16_t index[]={0x7005,0x7018,0x7028};uint32_t expected[]={5,bitsOf(CURRENT),WATCHDOG};
  for(int n=0;n<3;n++){uint32_t b;if(!readValue(i,index[n],b))return false;if(b!=expected[n]){uncertain=true;return false;}}
  uint32_t speed;if(!readValue(i,0x7017,speed)||speed!=bitsOf(SPEED)){uncertain=true;return false;}
  return true;
}
bool recoverDriver(){
  twai_status_info_t status={};if(twai_get_status_info(&status)!=ESP_OK)return false;
  if(status.state==TWAI_STATE_BUS_OFF){twai_initiate_recovery();return false;}
  if(status.state==TWAI_STATE_RECOVERING)return false;
  if(status.state==TWAI_STATE_STOPPED)return twai_start()==ESP_OK;
  return status.state==TWAI_STATE_RUNNING;
}
bool configure(){
  stage=DIR_OFFLINE;configured=false;cancelled=stopRequested=false;
  // STOP before setting position mode; preload measured position while disabled.
  uncertain=false;if(!stopBoth())return false;
  for(int i=0;i<2;i++){
    const uint8_t reportsOff[8]={1,2,3,4,5,6,0,8};
    if(!sendPacket(i,24,reportsOff)||!writeChecked(i,0x7028,WATCHDOG)||
       !writeChecked(i,0x7005,5)||!writeChecked(i,0x7018,bitsOf(CURRENT))||
       !writeChecked(i,0x7017,bitsOf(SPEED))||!readPosition(i)||
       !writeChecked(i,0x7016,bitsOf(legs[i].position)))return false;
    uint32_t b;if(!readValue(i,0x701C,b)||!std::isfinite(floatOf(b)))return false;legs[i].voltage=floatOf(b);
  }
  configured=true;stage=DIR_DISABLED;pollAt=signatureAt=millis();
  Serial.println("BOTH IDs configured/disabled. Send t for zero-speed watchdog A/B checks. Then declare BOTH retracted poses with Stick B.");return true;
}
bool zeroProbe(int i,uint8_t expected){
  uint8_t d[8]={0x0A,0x70};const uint32_t before=legs[i].modeCount,began=millis();
  if(!sendPacket(i,18,d))return false;
  while(legs[i].modeCount==before && uint32_t(millis()-began)<100 && !cancelled){service();delay(1);}
  bool ok=legs[i].modeCount!=before&&legs[i].mode==expected&&!legs[i].flags;
  Serial.printf("ID%u zero probe expected=%u actual=%u %s\n",legs[i].id,expected,legs[i].mode,ok?"OK":"FAILED");return ok;
}
bool watchdogTrial(int i,uint32_t timeout,uint8_t after){
  if(!stopBoth()||!writeChecked(i,0x7018,bitsOf(CURRENT))||!writeChecked(i,0x7028,timeout)||
     !writeChecked(i,0x7005,2)||!zeroProbe(i,0))return false;
  uint32_t b;if(!readValue(i,0x700A,b)||b!=bitsOf(0.f)||!readPosition(i))return false;
  float start=legs[i].position;
  if(!stateCommand(i,3,2)||!writeChecked(i,0x7018,bitsOf(CURRENT))||!readValue(i,0x7028,b)||b!=timeout||!zeroProbe(i,2))return false;
  quiet=true;foreignTraffic=false;uint32_t began=millis();
  Serial.printf("ID%u timeout=%lu: 1500ms silence on the WHOLE CAN bus. ZERO speed only.\n",legs[i].id,(unsigned long)timeout);
  while(!cancelled&&!stopRequested&&uint32_t(millis()-began)<1500){service();delay(1);}
  quiet=false;
  bool ok=!cancelled&&!foreignTraffic&&zeroProbe(i,after)&&readPosition(i)&&fabsf(legs[i].position-start)<.03f;
  bool stopped=stopBoth();return ok&&stopped;
}
void watchdogTest(){
  testRequested=false;
  if(!configured||stage!=DIR_DISABLED||!disarmedInput()){Serial.println("t blocked: both IDs configured, SB DISARMED and centered gimbals required.");return;}
  watchdogFailed=false;testing=true;stage=DIR_WATCHDOG_TEST;cancelled=stopRequested=false;
  bool okay=true;
  for(int i=0;i<2&&okay;i++){
    legs[i].watchdog=false;
    bool control=watchdogTrial(i,0,2), timeout=false;
    if(control)timeout=watchdogTrial(i,WATCHDOG,0);
    legs[i].watchdog=control&&timeout;okay=legs[i].watchdog;
    Serial.printf("WATCHDOG %s ID%u: control=%s timeout=%s RESULT=%s\n",side(i),legs[i].id,control?"PASS":"FAIL",timeout?"PASS":"FAIL",okay?"PASS":"NOT VERIFIED");
  }
  quiet=testing=false;
  // STOP and restore a nonzero timeout on BOTH motors even after an aborted trial.
  bool stopped=stopBoth();cancelled=stopRequested=false;
  if(stopped&&!uncertain){for(int i=0;i<2;i++)if(!writeChecked(i,0x7028,WATCHDOG))okay=false;}
  if(okay&&stopped&&!uncertain&&configure()){
    Serial.println("DUAL WATCHDOG PASS. Both motors disabled. Fixed references required; SB arms BOTH.");
  }else{watchdogFailed=true;for(auto& m:legs)m.watchdog=false;loseControl("watchdog test incomplete/failed; no movement allowed");}
  ax12.requireDisarm();
}
bool inRange(int i){float q=travel(i);return referenceValid && q>=-.002f && q<=TRAVEL+.002f;}
bool captureReference(){
  if(referenceCaptured||!configured||!allQualified()||!disarmedInput()||!M5.BtnB.isPressed())return false;
  if(!stopBoth()||!readPosition(0)||!readPosition(1))return false;
  float first[2]={legs[0].position,legs[1].position};uint32_t began=millis();
  while(uint32_t(millis()-began)<100){service();if(stopRequested||uncertain||!disarmedInput()||!M5.BtnB.isPressed())return false;delay(1);}
  for(int i=0;i<2;i++)if(!readPosition(i)||fabsf(legs[i].position-first[i])>.002f)return false;
  if(stopRequested||uncertain||!disarmedInput()||!M5.BtnB.isPressed())return false;
  for(auto& m:legs){m.origin=m.position;m.target=m.position;}
  referenceCaptured=referenceValid=true;ax12.requireDisarm();
  Serial.printf("BOTH REFERENCES CAPTURED R=%.6f L=%.6f rad. Fixed 0..80deg. No motor zero/reset command.\n",legs[0].origin,legs[1].origin);return true;
}
bool arm(){
  auto input=ax12.snapshot();selection=input.style;stage=DIR_ARMING;cancelled=false;
  if(!configured||!allQualified()||!referenceValid||!inputOkay()||!centered())return false;
  // Validate BOTH and preload BOTH before enabling either. Never recapture zero on arm.
  for(int i=0;i<2;i++){
    if(!settings(i)||!readPosition(i)||!inRange(i))return false;
    auto& m=legs[i];m.target=m.origin+m.sign*fmaxf(0,fminf(TRAVEL,travel(i)));
    if(!writeChecked(i,0x7016,bitsOf(m.target)))return false;
  }
  for(int i=0;i<2;i++)if(!stateCommand(i,3,2)||!writeChecked(i,0x7018,bitsOf(CURRENT))||!settings(i))return false;
  service();if(cancelled||!inputOkay()||!centered()||!referenceValid)return false;
  stage=DIR_ACTIVE;pollAt=millis();Serial.println("ARMED BOTH: FIXED 0..80deg, 1A / 1rad/s. Neutral HOLDS; SB OFF disables BOTH.");return true;
}
void setup(){
  auto cfg=M5.config();M5.begin(cfg);Serial.begin(115200);M5.Display.setRotation(1);M5.Display.setTextSize(2);ax12.enableMaintenance();ax12.begin();
  twai_general_config_t g=TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_32,GPIO_NUM_33,TWAI_MODE_NORMAL);g.rx_queue_len=64;g.tx_queue_len=0;
  twai_timing_config_t t=TWAI_TIMING_CONFIG_1MBITS();twai_filter_config_t f=TWAI_FILTER_CONFIG_ACCEPT_ALL();
  esp_err_t e=twai_driver_install(&g,&t,&f);if(e==ESP_OK)e=twai_start();driverReady=e==ESP_OK;
  Serial.printf("AX12 DUAL POSITION v0.1 CAN=%s RIGHT=1(-) LEFT=2(+). No enable on boot.\n",esp_err_to_name(e));
  if(driverReady){bool stopped=stopBoth();Serial.println(stopped?"Startup BOTH disabled confirmed":"Startup STOP unconfirmed. Keep BOTH motor supplies off until ready.");}
  uncertain=false;cancelled=stopRequested=false;retryAt=millis()+1500;
  Serial.println("BOTH motors always selected. t=watchdog checks SB OFF; release then hold Stick B 2s at BOTH retracted poses. A/x=stop BOTH. BARE OUTPUT TEST FIRST.");
}
void loop(){
  service();uint32_t now=millis();
  if(stopRequested || (configured&&uncertain&&!testing) || ((stage==DIR_ACTIVE||stage==DIR_ARMING)&&(cancelled||uncertain||!inputOkay()))){loseControl("operator/BLE/motor stop");return;}
  if(stage==DIR_OFFLINE && driverReady && int32_t(now-retryAt)>=0 && disarmedInput()){
    if(!recoverDriver()){retryAt=millis()+1250;return;}
    if(!configure())loseControl("both-motor configuration not verified");return;
  }
  if(testRequested){watchdogTest();return;}
  if(configured && uint32_t(now-pollAt)>=50){
    float dt=fminf(.05f,uint32_t(now-pollAt)*.001f);pollAt=now;
    if(!readPosition(0)||!readPosition(1)){loseControl("position feedback lost from a motor");return;}
    if(uint32_t(now-signatureAt)>=500){
      signatureAt=now;int i=signatureLeg;signatureLeg=1-signatureLeg;
      if(!settings(i)){if(!cancelled&&!stopRequested)invalidate();loseControl("motor settings/input changed");return;}
      uint32_t b;if(!readValue(i,0x701C,b)||!std::isfinite(floatOf(b))){loseControl("battery telemetry lost");return;}legs[i].voltage=floatOf(b);
    }
    if(stage==DIR_ACTIVE){
      auto input=ax12.snapshot();float rate=abs(input.forward)<=80?0:float(input.forward)/1000.f*SPEED;
      for(int i=0;i<2;i++){
        auto& m=legs[i];float relative=m.sign*(m.target-m.origin);
        relative=fmaxf(0,fminf(TRAVEL,relative+rate*dt));m.target=m.origin+m.sign*relative;
        if(fabsf(m.target-m.position)>.15f){loseControl("tracking error exceeds 0.15rad");return;}
        if(!writeChecked(i,0x7016,bitsOf(m.target))){loseControl("position target/acknowledgement lost");return;}
      }
    }
  }
  if(stage==DIR_DISABLED && configured && !referenceCaptured && allQualified()){
    if(!M5.BtnB.isPressed()){referenceReleased=true;referenceHoldAt=0;}
    else if(referenceReleased && disarmedInput()){
      if(!referenceHoldAt)referenceHoldAt=millis();
      if(uint32_t(millis()-referenceHoldAt)>=2000){referenceReleased=false;referenceHoldAt=0;if(!captureReference())loseControl("reference rejected");}
    }else referenceHoldAt=0;
  }
  if(stage==DIR_DISABLED && configured){
    auto input=ax12.snapshot();
    if(input.enabled && !arm())loseControl("arming blocked/interrupted/unverified");
  }
  if(uint32_t(now-displayAt)>=250){
    displayAt=now;
    auto in=ax12.snapshot();
    M5.Display.setCursor(0,0);M5.Display.printf("DUAL POSITION   \nR1(-) / L2(+)   \nWD: %-10s\nREF: %-10s\nR %5.1f L %5.1f\nA/x STOP BOTH   \n",allQualified()?"PASS":"CHECK APP",referenceValid?"VALID":referenceCaptured?"INVALID":"HOLD B 2s",travel(0)*180.f/PI,travel(1)*180.f/PI);
    Serial.printf("DUAL R=%.2f L=%.2f deg REF=%u WD=%u mode=%u/%u state=%u\n",travel(0)*180.f/PI,travel(1)*180.f/PI,referenceValid,allQualified(),legs[0].mode,legs[1].mode,unsigned(stage));
  }
  delay(1);
}
