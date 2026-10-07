#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_FILE_OPEN_API_VERSION 1u
#define T5_FILE_OPEN_PATH_MAX 512u
#define T5_FILE_HANDLER_ID_MAX 64u
#define T5_FILE_HANDLER_NAME_MAX 96u
#define T5_FILE_HANDLER_ICON_MAX 24u

typedef enum {
    T5_FILE_HANDLER_APP = 0,
    T5_FILE_HANDLER_SYSTEM_READER = 1,
} t5_file_handler_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t reserved[3];
    char app_id[T5_FILE_HANDLER_ID_MAX];
    char display_name[T5_FILE_HANDLER_NAME_MAX];
    char icon[T5_FILE_HANDLER_ICON_MAX];
} t5_file_handler_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // Rebuild the generated association manifest from verified installed apps.
    bool (*refresh)(void);

    // Query handlers for an absolute SD VFS file path. Results include system
    // handlers and installed application handlers declared by JSON metadata.
    uint32_t (*handler_count)(const char *source_path);
    bool (*handler_get)(const char *source_path, uint32_t index,
                        t5_file_handler_t *out);

    // Launch a declared application handler. The caller must return from
    // app_main after a successful request so its ELF can unload before the
    // target maps. Firmware relaunches the caller when the target returns.
    bool (*open_request)(const char *source_path, const char *app_id,
                         uint64_t cookie);
    bool (*open_take_result)(int32_t *esp_error, uint64_t *cookie);

    // Common receiver-side handoff. An app that declares supported_file_types
    // calls this at startup to obtain the file selected by the launching app.
    bool (*source_path_get)(char *out, size_t capacity);
} t5_file_open_api_v1;

const t5_file_open_api_v1 *t5_file_open_get_api(uint32_t version);

#ifdef __cplusplus
}
#endif
