#include "runtime/storage/Lz4Block.h"
static_assert(sizeof(RiscStorage::Lz4Block)<=256,"bounded decoder state");
extern "C" int32_t risc_lz4_target_probe(RiscStorage::Lz4Block::Hooks h,
 size_t compressed,uint8_t* output,size_t expected,size_t capacity,size_t* written) {
 return RiscStorage::Lz4Block::decode(h,compressed,output,expected,capacity,written);
}
