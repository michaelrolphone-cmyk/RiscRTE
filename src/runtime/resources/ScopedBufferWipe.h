#pragma once
#include <cstddef>
#include <cstdint>
namespace RiscRuntime {
// Wipe only this owned scratch array on every ordinary scope exit. Volatile
// stores prevent dead-store elimination. This does not promise deletion of
// registers, SDK/flash copies, crash dumps, or physical memory remanence.
class ScopedBufferWipe final {
 public:
  template<size_t N> explicit ScopedBufferWipe(uint8_t (&bytes)[N]) noexcept
    : bytes_(bytes),size_(N) {}
  ~ScopedBufferWipe() noexcept {for(size_t i=0;i<size_;++i)bytes_[i]=0;}
  ScopedBufferWipe(const ScopedBufferWipe&)=delete;
  ScopedBufferWipe& operator=(const ScopedBufferWipe&)=delete;
 private:
  volatile uint8_t* bytes_;
  size_t size_;
};
}
