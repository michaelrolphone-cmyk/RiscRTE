#pragma once
#include <GardenPlatformV1.h>
#include <RiscStorageWorkerV1.h>
#include <RiscStoragePackageAsyncV1.h>
#include <RiscProviderSyncV1.h>
#include <RiscGpioSdmmcV1.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Test-only evidence and sector Hardware callbacks from the loaded actual X4
 * provider. None of these implement or substitute Runtime custody callbacks. */
typedef struct {
 const risc_storage_volume_api_v1 *volume;
 const risc_provider_sync_api_v1 *sync;
 const risc_storage_worker_v1 *worker;
 const garden_gpio_v1 *gpio;
 bool (*card_open)(risc_sdmmc_card_info_v1*);
 bool (*card_read)(uint64_t,uint32_t,void*);
 bool (*card_write)(uint64_t,uint32_t,const void*);
 bool (*card_sync)(void);
 bool (*card_close)(void);
 void (*fault)(unsigned);
 /* 2/8 are raw/package submit attempts; 9/10 count successful queue admissions. */
 unsigned (*counter)(unsigned);
 uint64_t (*session)(void);
 bool (*handles)(void);
 void (*rejected)(void);
 bool (*quiesce)(void);
} native_sd_port_fixture;
void native_sd_port_loaded(const native_sd_port_fixture*);
void native_sd_port_unloaded(void);
void native_sd_port_before_read(void);
bool native_sd_port_hardware_allowed(void);
#ifdef __cplusplus
}
#endif
