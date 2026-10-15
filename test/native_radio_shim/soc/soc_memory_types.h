#pragma once
#include <cstdint>
inline const void* nativeAtomicRejectedAddress=nullptr;
inline bool esp_ptr_in_dram(const void* p){return p && p!=nativeAtomicRejectedAddress;}
