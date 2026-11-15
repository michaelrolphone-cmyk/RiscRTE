#include "RiscNativeTerminalV1.h"
_Static_assert(offsetof(risc_driver_native_terminal_v1,terminal_tag)==sizeof(risc_driver_native_root_v1),"Complete NRV1 prefix");
int fixture_terminal_abi_c(void){return sizeof(risc_native_terminal_result_v1)==4;}
