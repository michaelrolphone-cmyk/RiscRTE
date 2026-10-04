#pragma once
#include <cstdint>
namespace RiscBoot {
// Non-reusing, pointer-sized opaque token; exhaustion fails closed. The token is
// never dereferenced. Kept separate so the terminal boundary is host-testable.
inline void* nextKeyValueContext(uintptr_t& generation) {
  if(generation==UINTPTR_MAX)return nullptr;
  return reinterpret_cast<void*>(++generation);
}
}
