#pragma once
#include <cstdint>

namespace RiscPlatform {
// Exact low 32 bits of floor(microseconds / 1000), including the full uint64_t
// domain. Base-65536 long division avoids a general 64-bit divider on Xtensa.
// For h = 1000*k+r, k*2^32 vanishes only at the final uint32_t result;
// r remains essential. Each subsequent dividend is <= 65,535,999 and each
// quotient digit fits 16 bits. No clock origin or error interpretation lives here.
inline uint32_t unsignedMicrosToMillis32(uint64_t microseconds) {
  uint32_t remainder = uint32_t(microseconds >> 32) % 1000u;
  uint32_t dividend = (remainder << 16) |
                      uint32_t((microseconds >> 16) & 0xffffu);
  const uint32_t highDigit = dividend / 1000u;
  remainder = dividend - highDigit * 1000u;
  dividend = (remainder << 16) | uint32_t(microseconds & 0xffffu);
  return (highDigit << 16) | (dividend / 1000u);
}
}
