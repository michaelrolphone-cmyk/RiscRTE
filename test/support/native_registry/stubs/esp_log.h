#pragma once
static inline void risc_test_registry_log(const char* tag, const char* format, ...) {
    (void)tag; (void)format;
}
#define ESP_LOGE(...) risc_test_registry_log(__VA_ARGS__)
#define ESP_LOGW(...) risc_test_registry_log(__VA_ARGS__)
#define ESP_LOGI(...) risc_test_registry_log(__VA_ARGS__)
#define ESP_LOGD(...) risc_test_registry_log(__VA_ARGS__)
