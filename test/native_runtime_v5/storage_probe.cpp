#include "bootstrap/Runtime.h"
#include <cstdio>
int main(){std::printf("Runtime=%zu Module=%zu Graph=%zu NRV1=%zu\n",sizeof(RiscBoot::Runtime),sizeof(RuntimeProviders::ModuleV2),sizeof(RuntimeProviders::GraphV2),sizeof(risc_driver_native_root_v1));}
