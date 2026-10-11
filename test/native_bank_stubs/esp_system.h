#pragma once
#include <cstdlib>
// Native bank admission wires the production entropy callback but must not
// execute it. Trap accidental execution rather than fabricating random bytes.
inline void esp_fill_random(void*,size_t){std::abort();}
