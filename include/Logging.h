#pragma once
#include <esp_log.h>
#define LOG_INF(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define LOG_ERR(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
