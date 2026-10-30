#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {uint32_t struct_size,entered,cancelled,completed;} worker_probe_result;
typedef struct {
 uint32_t api_version,struct_size;
 /* Borrowed app result memory remains mapped until finish has joined. */
 bool (*hold)(bool deliberately_retained,worker_probe_result*);
 bool (*finish)(void);
} worker_probe_api;
