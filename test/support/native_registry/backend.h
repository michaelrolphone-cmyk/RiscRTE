#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* OS mappings behind the production registry. This is not a registry model. */
size_t risc_test_native_mapping_count(void);
size_t risc_test_native_relocation_count(void);
/* Immutable host input snapshots are distinct from live mapped modules. */
size_t risc_test_native_image_count(void);
size_t risc_test_native_read_count(void);
size_t risc_test_native_read_bytes(void);
void risc_test_native_fail_relocations(unsigned count);
/* Optional additional exported names, borrowed until all mappings are closed.
 * app_main, app_module_init/fini and t5_driver_get are always included. */
void risc_test_native_extra_symbols(const char* const* names, size_t count);
/* Optional observer hooks. host_handle is for host-only fixture setup; runtime
 * consumers must use the real production registry's handles and dlsym. */
void risc_test_native_loading(const char* path);
void risc_test_native_loaded(const char* path, void* host_handle);
void risc_test_native_unloading(const char* path, void* host_handle);
#ifdef __cplusplus
}
#endif
