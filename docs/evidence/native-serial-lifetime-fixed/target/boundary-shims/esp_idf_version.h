#pragma once
// Compile-only selector matching the archived SDK 6.1.0 profile.
#define ESP_IDF_VERSION_MAJOR 6
#define ESP_IDF_VERSION_MINOR 1
#define ESP_IDF_VERSION_PATCH 0
#define ESP_IDF_VERSION_VAL(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(6,1,0)
