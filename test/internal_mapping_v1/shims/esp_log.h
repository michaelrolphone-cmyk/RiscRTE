#pragma once
#include <stdio.h>
/* Type-check and consume actual logging arguments without producing logs. */
#define ELF_TEST_LOG(tag, format, ...) do { (void)(tag); if (0) fprintf(stderr, format, ##__VA_ARGS__); } while (0)
#define ESP_LOGD(...) ELF_TEST_LOG(__VA_ARGS__)
#define ESP_LOGI(...) ELF_TEST_LOG(__VA_ARGS__)
#define ESP_LOGE(...) ELF_TEST_LOG(__VA_ARGS__)
#define ESP_LOGW(...) ELF_TEST_LOG(__VA_ARGS__)
