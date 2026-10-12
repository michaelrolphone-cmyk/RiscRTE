#pragma once
/* Owner-side test controls. These never dispatch storage work on the owner. */
#include <stdint.h>
#include <RiscRuntimeV1.h>
#include <RiscKeyValueV1.h>
#include <RiscKeyValueAtomicV1.h>
#ifdef __cplusplus
extern "C" {
#endif
void native_settings_pause_worker(void);
void native_settings_resume_worker(void);
void native_settings_yield(void);
void native_settings_snapshot_io(void);
void native_settings_assert_no_io(void);
void native_settings_arm_write_pause(void);
void native_settings_wait_write_pause(void);
void native_settings_release_write_pause(void);
void native_settings_arm_close_failure(void);
void native_settings_pending_checks(const risc_key_value_atomic_v1*,uint64_t);
void native_settings_verify_legacy(const risc_runtime_api_v1*,unsigned);
void native_settings_idle_checks(const risc_key_value_atomic_v1*);
void runtime_native_settings_preferences(const risc_key_value_v1*,unsigned);
void native_settings_intervening_preference(void);
void runtime_native_settings_controller(const risc_runtime_api_v1*,unsigned,const char*);
#ifdef __cplusplus
}
#endif
