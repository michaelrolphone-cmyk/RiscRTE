#pragma once
#include <RiscAppDataV1.h>
namespace RiscBoot {
/* Compiled-in storage mechanics, never exported raw to applications. The
 * Runtime supplies the namespace from exact boot policy, not caller input. */
struct AppDataBackend {
 void* context;
 int32_t (*stat)(void*,uint32_t,const char*,uint32_t*,uint64_t*);
 int32_t (*read)(void*,uint32_t,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*);
 int32_t (*replace)(void*,uint32_t,const char*,uint64_t,const void*,uint32_t);
 bool (*exitSafe)(void*);
};
}
