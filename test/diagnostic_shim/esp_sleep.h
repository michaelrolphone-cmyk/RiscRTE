#pragma once
#include <cstdint>
inline uint32_t wake=0;
inline uint32_t esp_sleep_get_wakeup_cause(){return wake;}

constexpr int ESP_OK=0;
