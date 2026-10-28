#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace jf {
inline uint16_t u16(const uint8_t* p) { return p[0] | uint16_t(p[1]) << 8; }
inline void put16(uint8_t* p, uint16_t v) { p[0]=v; p[1]=v>>8; }
inline uint16_t crc(const uint8_t* p, size_t n) {
  uint16_t c=0xffff;
  for(size_t i=0;i<n;++i) { c ^= uint16_t(p[i])<<8; for(int k=0;k<8;++k) c=c&0x8000 ? (c<<1)^0x1021 : c<<1; }
  return c;
}
// Capability bit5 explicitly means manual single-leg bench control, not a gait.
constexpr uint8_t SINGLE_LEG=32;
struct Receiver {
  uint16_t session=0, seq=0;
  uint32_t at=0, disarmGeneration=0;
  bool have=false, link=false, offSeen=false, enabled=false;
  bool allowMaintenance=false, maintenanceRequested=false, maintenanceStopRequested=false;
  bool allowReference=false, referenceRequested=false;
  int16_t forward=0, turn=0;
  uint8_t style=0, sb=255;
  void requireDisarm() { if(enabled)++disarmGeneration; enabled=false; offSeen=false; forward=turn=0; maintenanceRequested=false; maintenanceStopRequested=false; referenceRequested=false; }
  void disconnect() { link=false; have=false; session=0; requireDisarm(); }
  bool fresh(uint32_t now) const { return link && have && uint32_t(now-at)<100; }
  void expire(uint32_t now) { if(!fresh(now)) requireDisarm(); }
  bool accept(const uint8_t* b, size_t n, uint32_t now) {
    expire(now);
    if(!link || n!=20 || b[0]!='J' || b[1]!='F' || b[2]!=2 || b[3]!=1 || crc(b,18)!=u16(b+18)) return false;
    const auto nextSession=u16(b+16), nextSeq=u16(b+4);
    const int16_t f=int16_t(u16(b+8)), t=int16_t(u16(b+10));
    const bool maintenance=(b[15]==1 || b[15]==2 || (b[15]==3 && allowReference)) && allowMaintenance && b[6]==0 && (b[15]==2 || b[14]==0) && f==0 && t==0;
    // This receiver implements no spinner/jump outputs. Reject their flags outright.
    if(!nextSession || (b[6]&~1) || b[7]>2 || b[14]>2 || (b[15]&&!maintenance) || b[12] || f < -1000 || f>1000 || t < -1000 || t>1000 || ((b[6]&1) && b[14]==0)) return false;
    const bool arm=b[6]&1;
    if(!have || session!=nextSession) {
      if(enabled || arm || b[14]!=0) return false; // New sessions start explicitly disarmed.
      requireDisarm(); session=nextSession; have=false;
    }
    if(have) { const uint16_t delta=nextSeq-seq; if(!delta || delta>=32768) return false; }
    if(have && style!=b[7]) requireDisarm();
    seq=nextSeq; at=now; have=true; style=b[7]; sb=b[14];
    if(!arm) {
      if(enabled)++disarmGeneration;
      enabled=false; forward=turn=0;
      // SB must physically select its disarmed position, not just an unarmed packet.
      if(b[14]==0) offSeen=true;
      else offSeen=false;
    } else if(enabled || offSeen) {
      if(!enabled && (f || t)) { requireDisarm(); return true; }
      enabled=true; offSeen=false; forward=f; turn=t;
    }
    if(maintenance){if(b[15]==1)maintenanceRequested=true;else if(b[15]==2)maintenanceStopRequested=true;else referenceRequested=true;}
    return true;
  }
};
}
