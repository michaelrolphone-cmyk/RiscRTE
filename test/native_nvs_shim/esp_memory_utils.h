#pragma once
#include <stdbool.h>
/* NVS protocol fixture: all its host controls model internal SRAM. Dedicated
 * native_idf6_storage_atomic_test separately qualifies placement rejection. */
static inline bool esp_ptr_in_dram(const void*p){return p!=0;}
