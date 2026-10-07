#pragma once
#include "Installer.h"
namespace RiscProvision {
struct MaintenancePort {
 void* context;
 uint32_t (*now)(void*);
 uint32_t (*nonce)(void*);
 bool (*hash)(void*,const void*,uint32_t,uint8_t*);
 InstallResult (*install)(void*,const void*,uint32_t,const void*,uint32_t);
 void (*reply)(void*,const char*);
 const char* source;
};
// Explicit maintenance-image endpoint. No port discovery or device actions.
class Maintenance {
 public:
 Maintenance(MaintenancePort p,void* buffer,uint32_t capacity):port(p),bytes(static_cast<uint8_t*>(buffer)),capacity(capacity){}
 ~Maintenance(){reset();}
 void feed(uint8_t);void poll();
 private:
 void reset();void command();void complete();
 MaintenancePort port;uint8_t* bytes;uint32_t capacity,profile=0,time=0,used=0,started=0,challenge=0;
 char line[192]{};uint32_t length=0;uint8_t digest[32]{};bool receiving=false,discard=false;
};
}
