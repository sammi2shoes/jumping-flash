#pragma once
#include "Arduino.h"
#include "../../firmware/AX12SingleLeg/AX12Protocol.h"
struct FakeAx12{
 jf::Receiver rx;bool reports=true;uint32_t cancelAt=0;
 bool ready=false,armed=false,fault=false,needed=true;
 float left=0,right=0;
 void begin(){rx.link=true;rx.have=true;rx.sb=0;rx.at=millis();}
 void enableMaintenance(){rx.allowMaintenance=true;}
 void update(){if(cancelAt && millis()>=cancelAt){rx.maintenanceStopRequested=true;cancelAt=0;}if(reports&&rx.link){rx.have=true;rx.at=millis();}rx.expire(millis());}
 jf::Receiver snapshot(){return rx;}
 bool disarmed(){return rx.fresh(millis())&&rx.sb==0&&!rx.enabled;}
 bool takeMaintenanceStop(){bool r=rx.maintenanceStopRequested;rx.maintenanceStopRequested=false;return r;}
 bool takeMaintenanceRequest(){bool r=rx.maintenanceRequested;rx.maintenanceRequested=false;return r;}
 void requireDisarm(){rx.requireDisarm();}
 void telemetryDirection(bool r,bool a,bool f,float l,float rightPos,float,bool need,bool failed){
  ready=r;armed=a;fault=f;needed=need;left=l;right=rightPos;
 }
} ax12;
