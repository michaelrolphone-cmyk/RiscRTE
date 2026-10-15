#pragma once
// Host seam for the SDK pointer classifier; target checks use the real header.
bool esp_ptr_internal(const void*);
bool esp_ptr_in_dram(const void*);
