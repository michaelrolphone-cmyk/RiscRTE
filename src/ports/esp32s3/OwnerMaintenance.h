#include "NativePlatform.h"
#pragma once
#ifdef RISC_OWNER_INSTALLER
#include "OwnerNvsInstaller.h"
#include "runtime/provisioning/Maintenance.h"
#include <esp_heap_caps.h>
#include <esp_system.h>
#include "runtime/crypto/Sha256.h"
#include <new>
namespace RiscBootstrap {
inline void ownerMaintenance(bool (*safe)()){
 constexpr uint32_t size=RiscProvision::ProfileInputBytes+384;
 auto* payload=static_cast<uint8_t*>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 auto* scratch=static_cast<uint8_t*>(heap_caps_malloc(RiscProvision::ProfileInputBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!payload||!scratch){free(payload);free(scratch);RiscPlatform::console.println("RTE_INSTALL UNAVAILABLE");return;}
 struct Context {uint8_t* scratch;bool (*safe)();} context{scratch,safe};
 {RiscProvision::Maintenance endpoint({&context,[](void*){return uint32_t(RiscPlatform::millis());},[](void*){return esp_random();},
 [](void*,const void* bytes,uint32_t n,uint8_t* out){return RiscCrypto::Sha256::digest(static_cast<const uint8_t*>(bytes),n,out);},
 [](void* p,const void* profile,uint32_t n,const void* time,uint32_t t){auto& c=*static_cast<Context*>(p);return installOwnerNvs(profile,n,time,t,c.scratch,RiscProvision::ProfileInputBytes,c.safe);},
 [](void*,const char* text){RiscPlatform::console.println(text);},RISC_BUILD_SOURCE_SHA},payload,size);
 for(;;){if(!safe())break;unsigned budget=512;while(budget--&&RiscPlatform::console.available()>0){int c=RiscPlatform::console.read();if(c>=0)endpoint.feed(uint8_t(c));}endpoint.poll();RiscPlatform::delay(1);}}
 free(payload);free(scratch);
}
}
#endif
