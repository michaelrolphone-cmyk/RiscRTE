#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <RiscGpioSdmmcV1.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_STORAGE_WORKER_CAPABILITY "platform.storage-worker"
#define RISC_STORAGE_WORKER_BYTES 4608u
#define RISC_STORAGE_WORKER_API_V1 1u
enum {RISC_STORAGE_WORK_OK=0,RISC_STORAGE_WORK_PENDING=1,RISC_STORAGE_WORK_CANCELLED=2,
 RISC_STORAGE_WORK_INVALID=-1,RISC_STORAGE_WORK_BUSY=-2,RISC_STORAGE_WORK_CONTEXT=-3,
 RISC_STORAGE_WORK_RETAINED=-4};
/* Native task calls only this provider callback. Request/result buffers are at least 8-byte aligned and belong
 * to native storage, not the requesting app. The provider and its dependencies
 * remain pinned until unbind succeeds. Completion is observed on the owner task.
 * Native workers receive only separately scoped storage I/O, never owner-task
 * privileges, Runtime grants, application callbacks or borrowed app buffers. */
typedef int32_t (*risc_storage_work_execute_v1)(void*provider_context,
 const void*request,uint32_t request_size,void*result,uint32_t result_capacity,uint32_t*result_size);
typedef struct {
 uint8_t power,clk,cmd,dat0;uint32_t max_hz;bool initial_power_level;
} risc_storage_work_scope_v1;
typedef struct {
 void*context;
 bool (*on_worker)(void*);
 uint64_t (*now_ms)(void*);
 bool (*power)(void*,bool level);
 void (*delay_ms)(void*,uint32_t);
 bool (*open)(void*,risc_sdmmc_card_info_v1*);
 bool (*read)(void*,uint64_t lba,uint32_t count,void*);
 bool (*write)(void*,uint64_t lba,uint32_t count,const void*);
 bool (*sync)(void*);
 bool (*close)(void*);
} risc_storage_work_io_v1;
typedef struct {
 uint32_t api_version,struct_size;void*context;
 int32_t (*bind)(void*,risc_storage_work_execute_v1,void*provider_context);
 int32_t (*submit)(void*,const void*request,uint32_t size,uint64_t*operation);
 /* PENDING is a nonblocking poll. An undersized result leaves it available.
  * Successful collect retires the operation; stale tokens never match again. */
 int32_t (*collect)(void*,uint64_t operation,void*result,uint32_t capacity,uint32_t*size,int32_t*status);
 /* Cooperative cancellation. PENDING means code/buffers/dependencies are still
  * live; never unload/release. After the worker returns, CANCELLED retires it.
  * Cancellation cannot interrupt an SD/vendor call or undo an accepted write. */
 int32_t (*cancel)(void*,uint64_t operation);
 int32_t (*unbind)(void*);
 /* Owner reserves the exact power+SD pins before any worker can use them.
  * Only this scope's storage I/O is callable by its worker. Ordinary Runtime,
  * GPIO, clock, sync, other provider tables remain owner-only. */
 int32_t (*claim)(void*,const risc_storage_work_scope_v1*);
 int32_t (*release)(void*);
 const risc_storage_work_io_v1*io;
} risc_storage_worker_v1;
#ifdef __cplusplus
}
#endif
