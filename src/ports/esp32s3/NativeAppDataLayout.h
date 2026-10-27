#pragma once
#include <cstdint>
/* Product-selected geometry, shared by the mount and its partition fence.
 * Defaults preserve the established paired-bank layout byte for byte. */
#ifndef RISC_NATIVE_APP_DATA_OFFSET
#define RISC_NATIVE_APP_DATA_OFFSET 0x270000u
#endif
#ifndef RISC_NATIVE_APP_DATA_BYTES
#define RISC_NATIVE_APP_DATA_BYTES 0x80000u
#endif
#ifndef RISC_NATIVE_FLASH_BYTES
#define RISC_NATIVE_FLASH_BYTES 0x1000000u
#endif
namespace RiscNativeAppDataLayout {
constexpr uint64_t Offset=RISC_NATIVE_APP_DATA_OFFSET,Bytes=RISC_NATIVE_APP_DATA_BYTES,FlashBytes=RISC_NATIVE_FLASH_BYTES;
static_assert(Offset>=0x10000 && !(Offset%4096) && Bytes>=0x10000 && !(Bytes%4096),"AppData requires aligned nonempty product geometry");
static_assert(Offset<=FlashBytes && Bytes<=FlashBytes-Offset && FlashBytes<=0x100000000ull,"AppData exceeds product flash geometry");
constexpr bool exact(uint64_t offset,uint64_t bytes){return offset==Offset&&bytes==Bytes;}
constexpr bool overlaps(uint64_t offset,uint64_t bytes){return bytes&&offset<Offset+Bytes&&(offset>=Offset || bytes>Offset-offset);}
}
