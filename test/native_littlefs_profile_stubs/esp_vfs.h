#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dirent.h>
#include <utime.h>
#include "esp_err.h"
#define ESP_VFS_PATH_MAX 15
#define ESP_VFS_FLAG_CONTEXT_PTR 1
/* Host-callable members of the ESP-IDF context-pointer VFS boundary. This is
 * not a binary-layout emulation of the target esp_vfs_t. */
typedef struct {
    int flags;
    ssize_t (*write_p)(void *, int, const void *, size_t);
    ssize_t (*pwrite_p)(void *, int, const void *, size_t, off_t);
    off_t (*lseek_p)(void *, int, off_t, int);
    ssize_t (*read_p)(void *, int, void *, size_t);
    ssize_t (*pread_p)(void *, int, void *, size_t, off_t);
    int (*open_p)(void *, const char *, int, int);
    int (*close_p)(void *, int);
    int (*fsync_p)(void *, int);
    int (*fcntl_p)(void *, int, int, int);
    int (*fstat_p)(void *, int, struct stat *);
    int (*stat_p)(void *, const char *, struct stat *);
    int (*link_p)(void *, const char *, const char *);
    int (*unlink_p)(void *, const char *);
    int (*rename_p)(void *, const char *, const char *);
    DIR *(*opendir_p)(void *, const char *);
    struct dirent *(*readdir_p)(void *, DIR *);
    int (*readdir_r_p)(void *, DIR *, struct dirent *, struct dirent **);
    long (*telldir_p)(void *, DIR *);
    void (*seekdir_p)(void *, DIR *, long);
    int (*closedir_p)(void *, DIR *);
    int (*mkdir_p)(void *, const char *, mode_t);
    int (*rmdir_p)(void *, const char *);
    ssize_t (*truncate_p)(void *, const char *, off_t);
    int (*ftruncate_p)(void *, int, off_t);
    int (*utime_p)(void *, const char *, const struct utimbuf *);
} esp_vfs_t;
esp_err_t esp_vfs_register(const char *path, const esp_vfs_t *vfs, void *context);
esp_err_t esp_vfs_unregister(const char *path);
