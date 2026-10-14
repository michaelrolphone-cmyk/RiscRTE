#pragma once
#include <cstddef>
#include <cstdint>
namespace RiscUpdate {
/* SPIFFS exposes a flat root listing of all slash-containing object names.
 * Compare both inventories and every unchanged object, bounded to 160 payload
 * entries plus the optional unchanged 32-byte provisioning identity. At most
 * one store's logical bytes, 30 seconds, with a checkpoint per 2 KiB read.
 * No permission to create, remove or alter any other object is inferred. */
bool auditStore(const char* active,const char* staged,const char* changedElf,const char* changedManifest,
                uint8_t* scratch,size_t capacity,void* context,uint32_t (*now)(void*),bool (*checkpoint)(void*));
}
