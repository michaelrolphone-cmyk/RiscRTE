#pragma once
#include <RiscStorageVolumeV1.h>
typedef struct {
 uint32_t api_version,struct_size;
 void* context;
 const risc_storage_volume_api_v1_ext* volume;
 bool (*start)(void*);
 bool (*quiesce)(void*);
 void (*stop)(void*);
} scoped_volume_provider_control;
