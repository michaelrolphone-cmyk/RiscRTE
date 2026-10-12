/* Pinned ESP LittleFS host qualification fixture. Vendor bytes are compiled
 * unchanged; only ESP-IDF, FreeRTOS, newlib and physical-flash boundaries are
 * modeled here. This does not qualify hardware, RTOS scheduling or target ABI. */
#include "native_littlefs_profile_fixture.h"
#include "sdkconfig.h"
#include "esp_littlefs.h"
#include "esp_vfs.h"
#include "littlefs_api.h"
#include "rom/spi_flash.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define FLASH_BYTES (512U * 1024U)
#define HANDLE_LIMIT 128U
static unsigned char flash_bytes[FLASH_BYTES];
static const esp_partition_t flash_partition = { 0x270000U, FLASH_BYTES, "appdata", false };
struct native_lfs_flash_chip g_rom_flashchip = { 256U };
static esp_vfs_t registered_vfs;
static void *registered_context;
static uint64_t mount_generation;
static bool file_live[HANDLE_LIMIT];
/* Pinned lfs_file_close consumes its low-level handle even on sync failure;
 * the ESP VFS then retains its descriptor. Never retry that consumed handle. */
static bool file_lowlevel_closed[HANDLE_LIMIT];
static DIR *directories[HANDLE_LIMIT];
static struct native_lfs_fixture_counters counters;
struct fault_setting { uint64_t remaining; enum native_lfs_fixture_fault_mode mode; };
static struct fault_setting faults[NATIVE_LFS_OPERATION_COUNT];
static native_lfs_fixture_hook_fn operation_hook;
static void *hook_context;
static bool in_hook;
static unsigned operation_depth;

struct native_lfs_mutex { pthread_mutex_t mutex; };
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) {
    SemaphoreHandle_t result = calloc(1U, sizeof(*result));
    pthread_mutexattr_t attributes;
    if (!result) return NULL;
    if (pthread_mutexattr_init(&attributes) != 0) { free(result); return NULL; }
    if (pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE) != 0 ||
        pthread_mutex_init(&result->mutex, &attributes) != 0) {
        (void)pthread_mutexattr_destroy(&attributes); free(result); return NULL;
    }
    (void)pthread_mutexattr_destroy(&attributes);
    return result;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return xSemaphoreCreateRecursiveMutex(); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout) {
    (void)timeout;
    return semaphore && pthread_mutex_lock(&semaphore->mutex) == 0 ? pdTRUE : pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    return semaphore && pthread_mutex_unlock(&semaphore->mutex) == 0 ? pdTRUE : pdFALSE;
}
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t semaphore, TickType_t timeout) {
    return xSemaphoreTake(semaphore, timeout);
}
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t semaphore) { return xSemaphoreGive(semaphore); }
void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    if (semaphore) { (void)pthread_mutex_destroy(&semaphore->mutex); free(semaphore); }
}
size_t strlcat(char *destination, const char *source, size_t capacity) {
    size_t current = strnlen(destination, capacity), added = strlen(source);
    if (current < capacity) {
        size_t copy = added < capacity - current - 1U ? added : capacity - current - 1U;
        memcpy(destination + current, source, copy); destination[current + copy] = '\0';
    }
    return current + added;
}

static void notify(enum native_lfs_fixture_operation operation, enum native_lfs_fixture_phase phase) {
    int saved_errno = errno;
    if (operation_hook && !in_hook) {
        in_hook = true; operation_hook(hook_context, operation, phase); in_hook = false;
    }
    errno = saved_errno;
}
static enum native_lfs_fixture_fault_mode begin(enum native_lfs_fixture_operation operation) {
    enum native_lfs_fixture_fault_mode mode = NATIVE_LFS_FAULT_NONE;
    ++operation_depth; ++counters.calls[operation];
    if (faults[operation].remaining && --faults[operation].remaining == 0U) {
        mode = faults[operation].mode; faults[operation].mode = NATIVE_LFS_FAULT_NONE;
    }
    notify(operation, NATIVE_LFS_BEFORE);
    return mode;
}
static void finish(enum native_lfs_fixture_operation operation) {
    notify(operation, NATIVE_LFS_AFTER); assert(operation_depth); --operation_depth;
}
static bool fail_before(enum native_lfs_fixture_fault_mode mode) {
    if (mode == NATIVE_LFS_FAIL_BEFORE) { errno = EIO; return true; }
    return false;
}
static bool mounted(void) {
    if (!registered_context) { errno = ENODEV; return false; }
    return true;
}
static bool valid_file(int fd) {
    if (fd < 0 || (unsigned)fd >= HANDLE_LIMIT || (!file_live[fd] || file_lowlevel_closed[fd])) { errno = EBADF; return false; }
    return mounted();
}
static size_t directory_slot(void *directory) {
    if (directory) for (size_t i = 0; i < HANDLE_LIMIT; ++i) if (directories[i] == directory) return i;
    errno = EBADF; return HANDLE_LIMIT;
}
static bool flash_range(const esp_partition_t *partition, size_t offset, size_t size) {
    if (partition != &flash_partition || offset > FLASH_BYTES || size > FLASH_BYTES - offset) {
        ++counters.flash_rejected_bounds; return false;
    }
    return true;
}
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label) {
    if (type != ESP_PARTITION_TYPE_DATA || subtype != ESP_PARTITION_SUBTYPE_ANY ||
        (label && strcmp(label, flash_partition.label) != 0)) return NULL;
    return &flash_partition;
}
esp_err_t esp_partition_read(const esp_partition_t *partition, size_t offset, void *destination, size_t size) {
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_FLASH_READ);
    esp_err_t result = ESP_FAIL;
    if (!fail_before(fault) && flash_range(partition, offset, size) && (destination || !size)) {
        if (size) memcpy(destination, flash_bytes + offset, size);
        counters.flash_read_bytes += size; result = ESP_OK;
        if (fault == NATIVE_LFS_FAIL_AFTER) result = ESP_FAIL;
    }
    finish(NATIVE_LFS_FLASH_READ); return result;
}
esp_err_t esp_partition_write(const esp_partition_t *partition, size_t offset, const void *source, size_t size) {
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_FLASH_PROG);
    esp_err_t result = ESP_FAIL;
    if (!fail_before(fault) && flash_range(partition, offset, size) && (source || !size)) {
        const unsigned char *input = source;
        bool valid = true;
        for (size_t i = 0; i < size; ++i) if ((flash_bytes[offset + i] & input[i]) != input[i]) { valid = false; break; }
        if (!valid) ++counters.flash_rejected_program;
        else {
            for (size_t i = 0; i < size; ++i) flash_bytes[offset + i] &= input[i];
            counters.flash_program_bytes += size; result = ESP_OK;
            if (fault == NATIVE_LFS_FAIL_AFTER) result = ESP_FAIL;
        }
    }
    finish(NATIVE_LFS_FLASH_PROG); return result;
}
esp_err_t esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size) {
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_FLASH_ERASE);
    esp_err_t result = ESP_FAIL;
    if (!fail_before(fault) && flash_range(partition, offset, size) && offset % 4096U == 0U && size % 4096U == 0U) {
        memset(flash_bytes + offset, 0xff, size); counters.flash_erase_bytes += size; result = ESP_OK;
        if (fault == NATIVE_LFS_FAIL_AFTER) result = ESP_FAIL;
    }
    finish(NATIVE_LFS_FLASH_ERASE); return result;
}
esp_err_t esp_vfs_register(const char *path, const esp_vfs_t *vfs, void *context) {
    if (registered_context || !path || strcmp(path, "/native") != 0 || !vfs || !context ||
        vfs->flags != ESP_VFS_FLAG_CONTEXT_PTR) return ESP_ERR_INVALID_ARG;
    registered_vfs = *vfs; registered_context = context; ++mount_generation;
    counters.registered = 1; return ESP_OK;
}
esp_err_t esp_vfs_unregister(const char *path) {
    if (!registered_context || !path || strcmp(path, "/native") != 0) return ESP_ERR_INVALID_ARG;
    registered_context = NULL; memset(&registered_vfs, 0, sizeof(registered_vfs));
    counters.registered = 0; return ESP_OK;
}

void native_lfs_fixture_clear_faults(void) { memset(faults, 0, sizeof(faults)); }
void native_lfs_fixture_clear_counters(void) {
    size_t files = counters.live_files, dirs = counters.live_dirs;
    memset(&counters, 0, sizeof(counters)); counters.live_files = files; counters.live_dirs = dirs;
    counters.registered = registered_context != NULL;
}
void native_lfs_fixture_fault(enum native_lfs_fixture_operation op, uint64_t nth, enum native_lfs_fixture_fault_mode mode) {
    assert(op >= 0 && op < NATIVE_LFS_OPERATION_COUNT);
    faults[op].remaining = nth; faults[op].mode = mode;
}
void native_lfs_fixture_hook(native_lfs_fixture_hook_fn callback, void *context) {
    operation_hook = callback; hook_context = context;
}
void native_lfs_fixture_get_counters(struct native_lfs_fixture_counters *out) { assert(out); *out = counters; }
const void *native_lfs_fixture_context(void) { return registered_context; }
const void *native_lfs_fixture_partition(void) { return &flash_partition; }
uint32_t native_lfs_fixture_partition_address(void) { return flash_partition.address; }
uint32_t native_lfs_fixture_partition_bytes(void) { return flash_partition.size; }
int native_lfs_fixture_partition_encrypted(void) { return flash_partition.encrypted; }
const char *native_lfs_fixture_mount(void) { return registered_context ? "/native" : NULL; }
uint64_t native_lfs_fixture_mount_generation(void) { return mount_generation; }
int native_lfs_fixture_binding_valid(const void *context, const void *partition) {
    return registered_context && context == registered_context && partition == &flash_partition &&
        ((const esp_littlefs_t *)registered_context)->partition == partition &&
        esp_littlefs_partition_mounted(&flash_partition);
}
int native_lfs_fixture_has_link_callback(void) { return registered_context && registered_vfs.link_p != NULL; }
size_t native_lfs_fixture_flash_size(void) { return FLASH_BYTES; }
size_t native_lfs_fixture_snapshot(void *destination, size_t capacity) {
    if (!destination || capacity < FLASH_BYTES) { errno = ENOBUFS; return 0; }
    memcpy(destination, flash_bytes, FLASH_BYTES); return FLASH_BYTES;
}

int native_lfs_fixture_stat(const char *path, struct stat *out) {
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_STAT);
    int result = -1;
    if (!fail_before(fault) && mounted()) {
        if (!path || !out) errno = EINVAL;
        else result = registered_vfs.stat_p(registered_context, path, out);
        if (fault == NATIVE_LFS_FAIL_AFTER) { errno = EIO; result = -1; }
    }
    finish(NATIVE_LFS_STAT); return result;
}
int native_lfs_fixture_fstat(int fd, struct stat *out) {
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_FSTAT);
    int result = -1;
    if (!fail_before(fault) && valid_file(fd)) {
        if (!out) errno = EINVAL;
        else result = registered_vfs.fstat_p(registered_context, fd, out);
        if (fault == NATIVE_LFS_FAIL_AFTER) { result = -1; errno = EIO; }
    }
    finish(NATIVE_LFS_FSTAT); return result;
}
int native_lfs_fixture_fsync(int fd) {
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_FSYNC);
    int result = -1;
    if (!fail_before(fault) && valid_file(fd)) {
        result = registered_vfs.fsync_p(registered_context, fd);
        if (fault == NATIVE_LFS_FAIL_AFTER) { result = -1; errno = EIO; }
    }
    finish(NATIVE_LFS_FSYNC); return result;
}
int native_lfs_fixture_rename(const char *source, const char *destination) {
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_RENAME);
    int result = -1;
    if (!fail_before(fault) && mounted()) {
        if (!source || !destination) errno = EINVAL;
        else result = registered_vfs.rename_p(registered_context, source, destination);
        if (fault == NATIVE_LFS_FAIL_AFTER) { result = -1; errno = EIO; }
    }
    finish(NATIVE_LFS_RENAME); return result;
}
int native_lfs_fixture_open(const char *path, int flags) {
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_OPEN);
    int result = -1;
    if (!fail_before(fault) && mounted()) {
        if (!path) errno = EINVAL;
        else if (counters.live_files == HANDLE_LIMIT) errno = EMFILE;
        else {
            result = registered_vfs.open_p(registered_context, path, flags, 0600);
            if (result >= 0) {
                assert((unsigned)result < HANDLE_LIMIT && !file_live[result]);
                file_live[result] = true; file_lowlevel_closed[result] = false; ++counters.live_files;
                if (fault == NATIVE_LFS_FAIL_AFTER) {
                    const int closed = registered_vfs.close_p(registered_context, result);
                    file_lowlevel_closed[result] = true;
                    if (closed == 0) { file_live[result] = false; --counters.live_files; }
                    result = -1; errno = EIO;
                }
            }
        }
    }
    finish(NATIVE_LFS_OPEN); return result;
}
ptrdiff_t native_lfs_fixture_read(int fd, void *destination, size_t size) {
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_READ);
    ssize_t result = -1;
    if (!fail_before(fault) && valid_file(fd)) {
        if (!destination && size) errno = EINVAL;
        else result = registered_vfs.read_p(registered_context, fd, destination,
            fault == NATIVE_LFS_SHORT_READ && size ? size - 1U : size);
        if (fault == NATIVE_LFS_FAIL_AFTER) { result = -1; errno = EIO; }
    }
    finish(NATIVE_LFS_READ); return (ptrdiff_t)result;
}
ptrdiff_t native_lfs_fixture_write(int fd, const void *source, size_t size) {
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_WRITE);
    ssize_t result = -1;
    if (!fail_before(fault) && valid_file(fd)) {
        if (!source && size) errno = EINVAL;
        else result = registered_vfs.write_p(registered_context, fd, source, size);
        if (fault == NATIVE_LFS_FAIL_AFTER) { result = -1; errno = EIO; }
    }
    finish(NATIVE_LFS_WRITE); return (ptrdiff_t)result;
}
int native_lfs_fixture_close(int fd) {
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_CLOSE);
    int result = -1;
    if (!fail_before(fault) && valid_file(fd)) {
        result = registered_vfs.close_p(registered_context, fd);
        file_lowlevel_closed[fd] = true;
        if (result == 0) { file_live[fd] = false; --counters.live_files; }
        if (fault == NATIVE_LFS_FAIL_AFTER) { result = -1; errno = EIO; }
    }
    finish(NATIVE_LFS_CLOSE); return result;
}
void *native_lfs_fixture_opendir(const char *path) {
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_OPENDIR);
    DIR *result = NULL;
    if (!fail_before(fault) && mounted()) {
        if (!path) errno = EINVAL;
        else if (counters.live_dirs == HANDLE_LIMIT) errno = EMFILE;
        else {
            result = registered_vfs.opendir_p(registered_context, path);
            if (result) {
                if (fault == NATIVE_LFS_FAIL_AFTER) {
                    (void)registered_vfs.closedir_p(registered_context, result); result = NULL; errno = EIO;
                } else for (size_t i = 0; i < HANDLE_LIMIT; ++i) if (!directories[i]) {
                    directories[i] = result; ++counters.live_dirs; break;
                }
            }
        }
    }
    finish(NATIVE_LFS_OPENDIR); return result;
}
int native_lfs_fixture_readdir(void *directory, struct native_lfs_fixture_entry *out) {
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_READDIR);
    int result = -1;
    if (!fail_before(fault) && mounted() && directory_slot(directory) < HANDLE_LIMIT) {
        if (!out) errno = EINVAL;
        else {
            errno = 0;
            struct dirent *entry = registered_vfs.readdir_p(registered_context, directory);
            if (!entry) result = errno ? -1 : 0;
            else {
                memcpy(out->name, entry->d_name, sizeof(out->name)); out->name[sizeof(out->name)-1U] = '\0';
                out->type = entry->d_type; result = 1;
            }
            if (fault == NATIVE_LFS_FAIL_AFTER) { result = -1; errno = EIO; }
        }
    }
    finish(NATIVE_LFS_READDIR); return result;
}
int native_lfs_fixture_closedir(void *directory) {
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_CLOSEDIR);
    int result = -1;
    if (!fail_before(fault) && mounted()) {
        size_t slot = directory_slot(directory);
        if (slot < HANDLE_LIMIT) {
            result = registered_vfs.closedir_p(registered_context, directory);
            if (result == 0) { directories[slot] = NULL; --counters.live_dirs; }
            if (fault == NATIVE_LFS_FAIL_AFTER) { result = -1; errno = EIO; }
        }
    }
    finish(NATIVE_LFS_CLOSEDIR); return result;
}
int native_lfs_fixture_mkdir(const char *path) {
    if (operation_depth) { errno = EBUSY; return -1; }
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_MKDIR);
    int result = -1;
    if (!fail_before(fault) && mounted()) {
        if (!path) errno = EINVAL;
        else result = registered_vfs.mkdir_p(registered_context, path, 0700);
        if (fault == NATIVE_LFS_FAIL_AFTER) { result = -1; errno = EIO; }
    }
    finish(NATIVE_LFS_MKDIR); return result;
}
int native_lfs_fixture_unlink(const char *path) {
    if (operation_depth) { errno = EBUSY; return -1; }
    enum native_lfs_fixture_fault_mode fault = begin(NATIVE_LFS_UNLINK);
    int result = -1;
    if (!fail_before(fault) && mounted()) {
        if (!path) errno = EINVAL;
        else result = registered_vfs.unlink_p(registered_context, path);
        if (fault == NATIVE_LFS_FAIL_AFTER) { result = -1; errno = EIO; }
    }
    finish(NATIVE_LFS_UNLINK); return result;
}
int native_lfs_fixture_seed(const char *path, const void *bytes, size_t size) {
    if (operation_depth) { errno = EBUSY; return -1; }
    int fd = native_lfs_fixture_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;
    ptrdiff_t result = native_lfs_fixture_write(fd, bytes, size);
    int saved_errno = errno;
    int closed = native_lfs_fixture_close(fd);
    if (result < 0 || (size_t)result != size) { errno = saved_errno; return -1; }
    return closed;
}
/* The pinned private directory layout is mirrored ONLY to release host heap
 * on simulated RAM loss. Inspection never reads this mirror or manufactures
 * directory/stat results. Vendor unregistration frees its file wrapper list
 * and shared FS caches, but does not free open directory/file cache buffers. */
struct discarded_vendor_directory {
    DIR dir; lfs_dir_t d; struct dirent entry; long offset; char *path;
};
void native_lfs_fixture_shutdown(void) {
    /* Test-owner simulated RAM loss, not a successful runtime quiescence. */
    assert(operation_depth == 0U);
    native_lfs_fixture_hook(NULL, NULL); native_lfs_fixture_clear_faults();
    if (registered_context) {
        esp_littlefs_t *efs = registered_context;
        /* Determine attachment before freeing any list node. A failed actual
         * lfs_file_close has already freed its cache and removed its node. */
        for (vfs_littlefs_file_t *file = efs->file; file; file = file->next) {
            bool attached = false;
            for (struct lfs_mlist *node = efs->fs->mlist; node; node = node->next) {
                if ((void *)node == (void *)&file->file) { attached = true; break; }
            }
            if (attached && file->file.cfg && !file->file.cfg->buffer) {
                free(file->file.cache.buffer); file->file.cache.buffer = NULL;
            }
        }
        for (size_t i = 0; i < HANDLE_LIMIT; ++i) if (directories[i]) {
            struct discarded_vendor_directory *directory = (void *)directories[i];
            free(directory->path); free(directory); directories[i] = NULL;
        }
        /* lfs_unmount only deallocates shared state; it does not sync files. */
        efs->fs->mlist = NULL;
        (void)esp_vfs_littlefs_unregister_partition(&flash_partition);
    }
    memset(file_live, 0, sizeof(file_live));
    memset(file_lowlevel_closed, 0, sizeof(file_lowlevel_closed));
    counters.live_files = 0; counters.live_dirs = 0;
}
static int mount_flash(void) {
    const esp_vfs_littlefs_conf_t configuration = {
        .base_path = "/native", .partition_label = NULL, .partition = &flash_partition,
        .format_if_mount_failed = false, .read_only = false, .dont_mount = false, .grow_on_mount = false
    };
    return esp_vfs_littlefs_register(&configuration) == ESP_OK ? 0 : -1;
}
int native_lfs_fixture_reboot(void) {
    if (operation_depth) { errno = EBUSY; return -1; }
    native_lfs_fixture_shutdown(); return mount_flash();
}
int native_lfs_fixture_reset(void) {
    native_lfs_fixture_shutdown(); native_lfs_fixture_clear_counters();
    memset(flash_bytes, 0xff, sizeof(flash_bytes));
    if (esp_littlefs_format_partition(&flash_partition) != ESP_OK || mount_flash() != 0) return -1;
    if (native_lfs_fixture_mkdir("/private") != 0 || native_lfs_fixture_mkdir("/private/n00000006") != 0) return -1;
    native_lfs_fixture_clear_counters(); return 0;
}
int native_lfs_fixture_restore(const void *image, size_t size) {
    if (operation_depth) { errno = EBUSY; return -1; }
    if (!image || size != FLASH_BYTES) { errno = EINVAL; return -1; }
    native_lfs_fixture_shutdown(); memcpy(flash_bytes, image, FLASH_BYTES);
    native_lfs_fixture_clear_counters(); return mount_flash();
}
