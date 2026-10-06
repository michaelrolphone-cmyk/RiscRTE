#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct {uint32_t version,size;bool (*enable)(void);bool (*disable)(void);bool (*poll)(void);} test_hci_api_v1;
