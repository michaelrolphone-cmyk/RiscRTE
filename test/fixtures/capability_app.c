#include <RiscRuntimeV1.h>
__attribute__((visibility("default"))) void app_main(void) {
  const risc_runtime_api_v1* api=risc_runtime_get_api(1);
  if(!api || api->api_version!=1 || api->struct_size<RISC_RUNTIME_CAPABILITIES_V1_SIZE || !api->acquire || !api->release)return;
  risc_runtime_capability_v1 grant={0};grant.struct_size=sizeof(grant);
#ifdef CHILD_WITHOUT_POLICY
  if(api->acquire("test.probe",1,0,&grant))api->diagnostic("CAP ERROR child inherited authority");
  else api->diagnostic("CAP child denied");
#else
  if(api->acquire("test.probe",2,0,&grant) || api->acquire("test.probe",1,8,&grant) || api->acquire("platform.gpio",1,0,&grant)) {
    api->diagnostic("CAP ERROR wrong version/instance/raw grant");return;
  }
  grant.struct_size=1;
  if(api->acquire("test.probe",1,0,&grant)){api->diagnostic("CAP ERROR short output");return;}
  grant.struct_size=sizeof(grant);
  if(!api->acquire("test.probe",1,0,&grant)){api->diagnostic("CAP ERROR missing grant");return;}
  const uint32_t* header=grant.api;
  if(!header || header[0]!=1 || header[1]!=8){api->diagnostic("CAP ERROR table");return;}
  risc_runtime_capability_v1 stale=grant;
  if(!api->release(&grant) || api->release(&stale)){api->diagnostic("CAP ERROR stale release");return;}
  if(!api->acquire("test.probe",1,7,&grant)){api->diagnostic("CAP ERROR exact instance");return;}
  api->diagnostic("CAP granted");
  risc_runtime_health_v1 health={0};health.struct_size=sizeof(health);
  if(api->health(&health) && health.uptime_ms==1)api->request_launch("cap-child.elf");
  // Deliberately leave one grant: runtime must revoke it before handoff/return.
#endif
}
