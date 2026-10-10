#include "RiscResidentShellV1.h"
extern void capacity_phase(const char *);
__attribute__((visibility("default"))) const risc_resident_app_descriptor_v1_t risc_resident_app_descriptor_v1={1,sizeof(risc_resident_app_descriptor_v1_t),RISC_RESIDENT_ROLE_FOREGROUND,0};
__attribute__((visibility("default"))) int app_module_init(void){return 0;}
__attribute__((visibility("default"))) void app_main(void){capacity_phase("launcher-handoff");}
__attribute__((visibility("default"))) void app_module_fini(void){}
