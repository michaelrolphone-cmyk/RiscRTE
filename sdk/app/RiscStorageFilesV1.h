#pragma once
/* Backend-neutral, synchronous file CRUD. A grant names a provisioned file
 * service; neither paths nor this ABI select a device or storage mechanism.
 * Paths are absolute UTF-8 names in that service's namespace. Components must
 * be nonempty and cannot be '.', '..', or contain a backslash/control byte.
 * Tokens are opaque and scoped to this exact grant, never portable across it.
 * Calls are serialized by the invocation owner, with bounded transfers.
 *
 * NOT_FOUND is an authoritative missing path, never an unavailable service or
 * an I/O error. END is a clean directory EOF. Successful read with zero bytes
 * is file EOF. Short transfers are legal; transferred is valid even on IO.
 * RETAINED is terminal uncertain custody: retain the grant and every dependent
 * resource, admit no more I/O and do not release/unload until a real restart.
 * Failed close retains ownership and MUST return RETAINED. Release with open
 * tokens is refused. A clean BUSY/UNAVAILABLE/DENIED does not imply retention.
 * open(CREATE|EXCLUSIVE) and rename never overwrite an existing destination.
 * APPEND applies to each write. seek cannot extend. truncate only shrinks.
 * sync and successful close prove provider completion, not merely buffering.
 * Directory cursors are scoped to an unmodified open directory, not persisted.
 * No mount, refresh, power, export, media or transport operation is exposed.
 */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_STORAGE_FILES_CAPABILITY "storage.files"
#define RISC_STORAGE_FILES_API_V1 1u
#define RISC_STORAGE_FILES_PATH_MAX 512u
#define RISC_STORAGE_FILES_NAME_MAX 128u
#define RISC_STORAGE_FILES_IO_MAX 4096u
#define RISC_STORAGE_FILES_HANDLE_INVALID 0u
typedef uint32_t risc_storage_files_handle_v1;
enum {
 RISC_FILES_OK=0, RISC_FILES_NOT_FOUND=1, RISC_FILES_END=2, RISC_FILES_EXISTS=3,
 RISC_FILES_INVALID=-1, RISC_FILES_IO=-2, RISC_FILES_UNAVAILABLE=-3,
 RISC_FILES_BUSY=-4, RISC_FILES_DENIED=-5, RISC_FILES_RETAINED=-6,
 RISC_FILES_CONTEXT=-7
};
enum {
 RISC_FILES_READ=1u, RISC_FILES_WRITE=2u, RISC_FILES_CREATE=4u,
 RISC_FILES_TRUNCATE=8u, RISC_FILES_EXCLUSIVE=16u, RISC_FILES_APPEND=32u
};
enum {RISC_FILES_DIRECTORY=1u,RISC_FILES_MODIFIED_VALID=2u,RISC_FILES_MODIFIED_LOCAL=4u};
typedef struct {uint32_t struct_size,flags;uint64_t size,modified_seconds;} risc_storage_files_stat_v1;
typedef struct {uint32_t struct_size,flags;uint64_t size;char name[RISC_STORAGE_FILES_NAME_MAX];} risc_storage_files_entry_v1;
typedef struct {
 uint32_t api_version,struct_size;void *context;
 int32_t (*open)(void*,const char*,uint32_t,risc_storage_files_handle_v1*);
 int32_t (*close)(void*,risc_storage_files_handle_v1);
 int32_t (*read)(void*,risc_storage_files_handle_v1,void*,uint32_t,uint32_t*);
 int32_t (*write)(void*,risc_storage_files_handle_v1,const void*,uint32_t,uint32_t*);
 int32_t (*seek)(void*,risc_storage_files_handle_v1,uint64_t);
 int32_t (*info)(void*,risc_storage_files_handle_v1,uint64_t*,uint64_t*);
 int32_t (*sync)(void*,risc_storage_files_handle_v1);
 int32_t (*truncate)(void*,risc_storage_files_handle_v1,uint64_t);
 int32_t (*stat)(void*,const char*,risc_storage_files_stat_v1*);
 int32_t (*mkdir)(void*,const char*);
 int32_t (*rename)(void*,const char*,const char*);
 int32_t (*remove)(void*,const char*);
 int32_t (*dir_open)(void*,const char*,risc_storage_files_handle_v1*);
 int32_t (*dir_next)(void*,risc_storage_files_handle_v1,risc_storage_files_entry_v1*);
 int32_t (*dir_close)(void*,risc_storage_files_handle_v1);
 int32_t (*dir_rewind)(void*,risc_storage_files_handle_v1);
 int32_t (*dir_tell)(void*,risc_storage_files_handle_v1,uint64_t*);
 int32_t (*dir_seek)(void*,risc_storage_files_handle_v1,uint64_t);
} risc_storage_files_v1;
#ifdef __cplusplus
}
#endif
