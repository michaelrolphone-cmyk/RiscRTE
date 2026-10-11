#include <stdint.h>
static volatile uint32_t initialized=0x12345678;
static volatile uint32_t zeroed[16];
__attribute__((visibility("default"))) int app_module_init(void) {
  ++zeroed[0];return initialized==0x12345678 && zeroed[0]==1 ? 0 : -1;
}
__attribute__((visibility("default"))) void app_main(void) { ++initialized; }
__attribute__((visibility("default"))) void app_module_fini(void) { ++zeroed[1]; }
