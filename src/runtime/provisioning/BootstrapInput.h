#pragma once
#include "Profile.h"
namespace RiscProvision {
constexpr uint32_t DescriptorBytes=256, ProfileInputBytes=16384;
enum class InputStatus {Ready,Missing,Unavailable,Invalid};
struct Input {
 void* context=nullptr;
 InputStatus (*read)(void*,const char* key,void*,uint32_t capacity,uint32_t* size)=nullptr;
};
// Read-only owner input. Descriptor chooses a blob key, never an arbitrary
// filesystem, partition or app namespace. UTC is not a descriptor/profile field.
// Caller owns bounded scratch; it is wiped on every exit, including success.
InputStatus loadProfile(Input,void* scratch,uint32_t capacity,Profile&,uint8_t (&digest)[32],
                        bool (*sha256)(const void*,uint32_t,uint8_t*));
}
