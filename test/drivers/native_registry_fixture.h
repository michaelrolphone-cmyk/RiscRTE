#pragma once
#include <stdint.h>
typedef struct { uint32_t marker; int (*next)(void); } registry_fixture_api;
