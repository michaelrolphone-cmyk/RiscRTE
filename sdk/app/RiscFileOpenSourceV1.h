#pragma once
#include "T5FileOpenApi.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_FILE_OPEN_SOURCE_CAPABILITY "file.open.source"
#define RISC_FILE_OPEN_SOURCE_API_V1 1u
#define RISC_FILE_OPEN_SOURCE_NAME_MAX 128u
#define RISC_FILE_OPEN_SOURCE_IO_MAX 4096u
enum {
 RISC_FILE_SOURCE_OK=0, RISC_FILE_SOURCE_UNAVAILABLE=-1,
 RISC_FILE_SOURCE_INVALID=-2, RISC_FILE_SOURCE_BUSY=-3,
 RISC_FILE_SOURCE_NO_SPACE=-4, RISC_FILE_SOURCE_IO=-5,
 RISC_FILE_SOURCE_RETAINED=-6, RISC_FILE_SOURCE_CONTEXT=-7,
 RISC_FILE_SOURCE_INCOMPLETE=-8
};
typedef struct {
 uint32_t struct_size;
 char name[RISC_FILE_OPEN_SOURCE_NAME_MAX];
 uint64_t size,token;
} risc_file_open_source_info_v1;
/* Optional Runtime-owned, disk-backed selected-file handoff. The original
 * file.open@1 table remains unchanged. A deployment must explicitly grant this
 * capability to both caller and receiver and supply a trusted scratch backend.
 * Names are basenames used only for manifest association lookup; never paths.
 * begin/write/finish copy source bytes before handoff with bounded RAM. The
 * caller owns cancellation until open_request succeeds, then returns promptly.
 * No borrowed app pointer survives a callback. Receiver read_at is immutable,
 * bounded and available only to the selected receiver for this generation.
 * Actual capacity is the configured backing store; the API adds no file-size
 * limit. A failed close is terminal RETAINED, never a cleanup retry.
 * A non-BUSY capture failure cancels its private stage. discard remains an
 * idempotent acknowledgement for that token in the same live caller only.
 * A fresh begin, caller unload or accepted handoff revokes that acknowledgement.
 * write/read_at may return a short successful count; advance by that count. */
typedef struct {
 uint32_t api_version,struct_size;
 uint32_t (*handler_count)(const char* name);
 bool (*handler_get)(const char* name,uint32_t index,t5_file_handler_t* out);
 bool (*handler_supported)(const char* app_id);
 int32_t (*begin)(const char* name,uint64_t size,uint64_t* token);
 int32_t (*write)(uint64_t token,const void* bytes,uint32_t length,uint32_t* written);
 int32_t (*finish)(uint64_t token);
 int32_t (*discard)(uint64_t token);
 bool (*open_request)(uint64_t token,const char* app_id,uint64_t cookie);
 bool (*take_result)(int32_t* error,uint64_t* cookie);
 int32_t (*source_info)(risc_file_open_source_info_v1* out);
 int32_t (*read_at)(uint64_t token,uint64_t offset,void* bytes,uint32_t capacity,uint32_t* actual);
} risc_file_open_source_api_v1;
#ifdef __cplusplus
}
#endif
