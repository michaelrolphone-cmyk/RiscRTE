#pragma once
#include <RiscEntropyV1.h>
namespace RiscBoot {
// Native-only boundary. The broker validates the live activation and supplies
// its nonzero key; no manifest or provider argument can choose this identity.
struct EntropyBackend {
 void* context;
 int32_t (*fill)(void*,uint64_t,void*,uint32_t);
 bool (*idle)(void*,uint64_t); // zero queries all owners; no native I/O
 bool (*safe)(void*);         // false after observed owner loss during a fill
};
}
