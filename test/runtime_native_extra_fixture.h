#pragma once
// Test-only callbacks for separately pinned actual controller bridges.
#include "runtime_native_settings_preferences_fixture.h"
#ifdef __cplusplus
extern "C" {
#endif
const char *native_extra_key(unsigned);
unsigned native_extra_intents(void);
void native_extra_intent(unsigned,const void*,unsigned);
void native_extra_verify_reads(const risc_runtime_api_v1*);
#ifdef __cplusplus
}
#endif
