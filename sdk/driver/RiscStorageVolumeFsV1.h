#pragma once
#include "RiscStorageVolumeStateV1.h"
/* Shared optional filesystem suffix. All existing prefixes and custody rules
 * are unchanged. Ordinary clients discover this through storage.volume@1.
 * Directory positions come from the filesystem, not from replaying entries.
 * They may be reused after reopening the SAME unmodified directory on the same
 * mounted volume; never persist them across media change or modify the directory
 * during a scan. Invalid/out-of-range positions fail, never wrap.
 * modified_seconds is seconds since 1970 in the filesystem's clock domain;
 * MODIFIED_LOCAL means timezone is unknown (ordering is still meaningful).
 * file_truncate only shrinks; it never extends or allocates sparse storage.
 * Paths ending in .risc-replace are reserved for provider recovery.
 * replace_file owns staging and rollback; recover_replace resolves an interrupted
 * replacement before reopening a destination. Both require closed operands and
 * a single directory. Source contents must already be synced/closed. A failure
 * never authorizes deleting the destination or its recovery file.
 */
#define RISC_STORAGE_FS_TAG UINT32_C(0x46535631)
enum {RISC_STORAGE_FS_OK=0,RISC_STORAGE_FS_NOT_FOUND=1,RISC_STORAGE_FS_INVALID=-1,RISC_STORAGE_FS_IO=-2};
enum {RISC_STORAGE_DIRECTORY=1u,RISC_STORAGE_MODIFIED_VALID=2u,RISC_STORAGE_MODIFIED_LOCAL=4u};
typedef struct {uint32_t struct_size,flags;uint64_t size,modified_seconds;} risc_storage_metadata_v1;
typedef struct {
 risc_storage_volume_api_v1_state state;
 uint32_t fs_tag,fs_version;
 bool (*dir_tell)(void*,risc_storage_dir_t,uint64_t*);
 bool (*dir_seek)(void*,risc_storage_dir_t,uint64_t);
 int32_t (*metadata)(void*,const char*,risc_storage_metadata_v1*);
 bool (*file_truncate)(void*,risc_storage_file_t,uint64_t);
 bool (*replace_file)(void*,const char*,const char*);
 bool (*recover_replace)(void*,const char*);
} risc_storage_volume_api_v1_fs;
static inline const risc_storage_volume_api_v1_fs* risc_storage_volume_fs(const risc_storage_volume_api_v1* api){
 if(!risc_storage_volume_state(api)||api->struct_size<sizeof(risc_storage_volume_api_v1_fs))return NULL;
 const risc_storage_volume_api_v1_fs* fs=(const risc_storage_volume_api_v1_fs*)api;
 return fs->fs_tag==RISC_STORAGE_FS_TAG&&fs->fs_version==1&&fs->dir_tell&&fs->dir_seek&&fs->metadata&&fs->file_truncate&&fs->replace_file&&fs->recover_replace?fs:NULL;
}
