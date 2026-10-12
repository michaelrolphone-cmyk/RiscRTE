#ifndef NATIVE_LITTLEFS_PROFILE_FIXTURE_H
#define NATIVE_LITTLEFS_PROFILE_FIXTURE_H
/* Host test boundary only. All metadata and directory entries come from the
 * unchanged, pinned ESP LittleFS VFS, never a POSIX filesystem substitute. */
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#ifdef __cplusplus
extern "C" {
#endif

enum native_lfs_fixture_operation {
    NATIVE_LFS_STAT, NATIVE_LFS_OPEN, NATIVE_LFS_READ, NATIVE_LFS_CLOSE,
    NATIVE_LFS_OPENDIR, NATIVE_LFS_READDIR, NATIVE_LFS_CLOSEDIR,
    NATIVE_LFS_WRITE, NATIVE_LFS_MKDIR, NATIVE_LFS_UNLINK,
    NATIVE_LFS_FSTAT, NATIVE_LFS_FSYNC, NATIVE_LFS_RENAME,
    NATIVE_LFS_FLASH_READ, NATIVE_LFS_FLASH_PROG, NATIVE_LFS_FLASH_ERASE,
    NATIVE_LFS_OPERATION_COUNT
};
enum native_lfs_fixture_fault_mode {
    NATIVE_LFS_FAULT_NONE, NATIVE_LFS_FAIL_BEFORE,
    NATIVE_LFS_FAIL_AFTER, NATIVE_LFS_SHORT_READ
};
enum native_lfs_fixture_phase { NATIVE_LFS_BEFORE, NATIVE_LFS_AFTER };
struct native_lfs_fixture_entry { char name[256]; unsigned char type; };
struct native_lfs_fixture_counters {
    uint64_t calls[NATIVE_LFS_OPERATION_COUNT];
    uint64_t flash_read_bytes, flash_program_bytes, flash_erase_bytes;
    uint64_t flash_rejected_bounds, flash_rejected_program;
    size_t live_files, live_dirs;
    int registered;
};
typedef void (*native_lfs_fixture_hook_fn)(void *context,
    enum native_lfs_fixture_operation operation, enum native_lfs_fixture_phase phase);
/* reset explicitly formats the test flash, then mounts without format-on-failure.
 * It creates /private and /private/n00000006 through the real VFS. */
int native_lfs_fixture_reset(void);
/* Test-owner teardown models losing RAM: no file close, flush or retry. */
void native_lfs_fixture_shutdown(void);
/* Discard volatile handles and remount the same flash with formatting off. */
int native_lfs_fixture_reboot(void);
int native_lfs_fixture_seed(const char *path, const void *bytes, size_t size);
int native_lfs_fixture_mkdir(const char *path);
int native_lfs_fixture_unlink(const char *path);
void native_lfs_fixture_clear_faults(void);
void native_lfs_fixture_clear_counters(void);
/* Trigger once on the nth subsequent call of op; nth=0 disables this fault. */
void native_lfs_fixture_fault(enum native_lfs_fixture_operation op, uint64_t nth,
    enum native_lfs_fixture_fault_mode mode);
void native_lfs_fixture_hook(native_lfs_fixture_hook_fn callback, void *context);
void native_lfs_fixture_get_counters(struct native_lfs_fixture_counters *out);
const void *native_lfs_fixture_context(void);
const void *native_lfs_fixture_partition(void);
uint32_t native_lfs_fixture_partition_address(void);
uint32_t native_lfs_fixture_partition_bytes(void);
int native_lfs_fixture_partition_encrypted(void);
const char *native_lfs_fixture_mount(void);
uint64_t native_lfs_fixture_mount_generation(void);
int native_lfs_fixture_binding_valid(const void *context, const void *partition);
int native_lfs_fixture_has_link_callback(void);
int native_lfs_fixture_restore(const void *image, size_t size);
size_t native_lfs_fixture_flash_size(void);
/* Returns the image length, or 0 with errno=ENOBUFS for insufficient capacity. */
size_t native_lfs_fixture_snapshot(void *destination, size_t capacity);
/* Paths are mount-relative and passed to the captured VFS callbacks unchanged.
 * Unknown paths use the vendor's normal errors. No synthetic stat fields. */
int native_lfs_fixture_stat(const char *path, struct stat *out);
int native_lfs_fixture_fstat(int fd, struct stat *out);
int native_lfs_fixture_fsync(int fd);
int native_lfs_fixture_rename(const char *source, const char *destination);
int native_lfs_fixture_open(const char *path, int flags);
ptrdiff_t native_lfs_fixture_read(int fd, void *destination, size_t size);
ptrdiff_t native_lfs_fixture_write(int fd, const void *source, size_t size);
int native_lfs_fixture_close(int fd);
void *native_lfs_fixture_opendir(const char *path);
/* Returns 1=entry, 0=EOF, -1=error; errno is preserved from actual readdir. */
int native_lfs_fixture_readdir(void *directory, struct native_lfs_fixture_entry *out);
int native_lfs_fixture_closedir(void *directory);
#ifdef __cplusplus
}
#endif
#endif
