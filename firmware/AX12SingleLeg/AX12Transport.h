#pragma once
#include <cmath>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include "AX12Protocol.h"

// Only this adapter touches BLE task state; motor/CAN work remains on Arduino loop.
class AX12Transport : public BLEServerCallbacks, public BLECharacteristicCallbacks {
  portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
  jf::Receiver rx;
  BLECharacteristic* status=nullptr;
  BLEAdvertising* advertising=nullptr;
  bool restartAdvertising=false;
  uint32_t notifyAt=0, generation=0, consumed=0;
  uint16_t telemetrySeq=0;
 public:
  jf::Receiver snapshot() { portENTER_CRITICAL(&mux); auto s=rx; portEXIT_CRITICAL(&mux); return s; }
  void requireDisarm() { portENTER_CRITICAL(&mux); rx.requireDisarm(); portEXIT_CRITICAL(&mux); }
  bool disarmed() { auto s=snapshot(); return s.fresh(millis()) && s.sb==0 && !s.enabled; }
  bool linked() { return snapshot().link; }
  bool fresh() { return snapshot().fresh(millis()); }
  bool held() { auto s=snapshot(); return s.fresh(millis()) && s.enabled; }
  int axis() { auto s=snapshot(); return s.fresh(millis()) ? int(s.forward)*512/1000 : 0; }
  bool update() {
    portENTER_CRITICAL(&mux);
    rx.expire(millis());
    bool changed=generation!=consumed; consumed=generation;
    bool advertise=restartAdvertising; restartAdvertising=false;
    portEXIT_CRITICAL(&mux);
    if(advertise && advertising) advertising->start();
    return changed;
  }
  void onConnect(BLEServer*) override {
    portENTER_CRITICAL(&mux); rx.disconnect(); rx.link=true; ++generation; portEXIT_CRITICAL(&mux);
  }
  void onDisconnect(BLEServer*) override {
    portENTER_CRITICAL(&mux); rx.disconnect(); restartAdvertising=true; ++generation; portEXIT_CRITICAL(&mux);
  }
  void onWrite(BLECharacteristic* c) override {
    auto bytes=c->getValue();
    portENTER_CRITICAL(&mux);
    if(rx.accept(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),millis())) ++generation;
    portEXIT_CRITICAL(&mux);
  }
  void begin() {
    BLEDevice::init("JumpingFlash-Leg");
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    auto security=new BLESecurity();
    security->setAuthenticationMode(ESP_LE_AUTH_BOND);
    security->setCapability(ESP_IO_CAP_NONE);
    security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    auto server=BLEDevice::createServer(); server->setCallbacks(this);
    auto service=server->createService("de88c000-7865-4ce4-9a13-611fad7e0001");
    auto command=service->createCharacteristic("de88c001-7865-4ce4-9a13-611fad7e0001",BLECharacteristic::PROPERTY_WRITE);
    command->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED); command->setCallbacks(this);
    status=service->createCharacteristic("de88c002-7865-4ce4-9a13-611fad7e0001",BLECharacteristic::PROPERTY_NOTIFY);
    status->addDescriptor(new BLE2902()); service->start();
    advertising=BLEDevice::getAdvertising(); advertising->addServiceUUID(service->getUUID());
    advertising->setScanResponse(true); advertising->start();
  }
  void telemetry(bool ready,bool referenced,bool armed,bool fault,float degrees,float volts,uint8_t faults) {
    auto s=snapshot();
    if(!s.link || !s.have || uint32_t(millis()-notifyAt)<40) return;
    notifyAt=millis();
    uint8_t b[20]={'J','F',2,0x81};
    jf::put16(b+4,telemetrySeq++);
    b[6]=(ready?1:0)|(referenced?2:0)|(armed?4:0)|(fault?8:0); b[7]=s.style;
    jf::put16(b+8,referenced && std::isfinite(degrees) ? int16_t(lroundf(degrees*100)) : 0);
    jf::put16(b+10,0); jf::put16(b+12,std::isfinite(volts) ? uint16_t(fminf(65535,fmaxf(0,volts*1000))) : 0);
    b[14]=faults; b[15]=jf::SINGLE_LEG; jf::put16(b+16,s.session); jf::put16(b+18,jf::crc(b,18));
    status->setValue(b,20); status->notify();
  }
} ax12;

// Adapt the proven manual hold/reference controller without changing its CAN path.
class AX12Controller {
 public:
  bool isConnected(){return ax12.linked();} bool isGamepad(){return true;}
  bool hasData(){return ax12.fresh();} bool r1(){return ax12.held();}
  int axisX(){return ax12.axis();} bool x(){return false;} bool y(){return false;}
};
using ControllerPtr=AX12Controller*;
class AX12Facade {
  AX12Controller controller;
 public:
  void setup(void(*connected)(ControllerPtr),void(*)(ControllerPtr)){ax12.begin();connected(&controller);}
  void enableVirtualDevice(bool){} void enableNewBluetoothConnections(bool){}
  bool update(){return ax12.update();}
} BP32;
