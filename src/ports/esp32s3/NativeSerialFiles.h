#pragma once
#include <cstddef>
#include <cstdint>
#ifndef RISC_NATIVE_SERIAL_FILES
#define RISC_NATIVE_SERIAL_FILES 0
#endif
#if RISC_NATIVE_SERIAL_FILES && defined(RISC_OWNER_INSTALLER)
#error "Serial file transfer cannot share the owner installer's private transport"
#endif
#if RISC_NATIVE_SERIAL_FILES
#include "runtime/serial/Files.h"
namespace RiscCpu { namespace NativeSerialFiles {
// Compiled-in admission, never an application capability or a discovered path.
// Each selected RuntimeFiles session, provider table and dependency stays alive
// until explicit checked final stop or restart, including terminal RETAINED.
// Session shutdown alone does not end this lifetime. Admission copies IDs, labels and
// access flags; labels must never be interpreted as mount paths. These sessions
// must be independent of invocation/metadata clients whose exit barriers demand
// no open handles. No provider graph grant or backend is acquired here.
// The getter and ready callback are owner-task RAM-only checks. ready must
// qualify the exact sessions (including provider callback/custody exclusion),
// without acquiring providers, mounting storage, doing I/O or yielding. A healthy
// accepted handle is not itself unsafe; unavailable namespaces may stay listed.
struct Admission {
  const RiscSerial::Namespace* namespaces;
  size_t count;
  bool (*ready)();
};
using Check=bool (*)();
// One firmware-lifetime selection. Absent admission leaves this optional
// endpoint unavailable and does not change the ordinary boot sequence.
using StatObserver=void(*)(uint32_t,uint32_t,uint64_t,int32_t,const RiscBoot::StatObservation&);
using ControlObserver=void (*)(uint32_t,uint32_t,const char*,uint32_t,bool);
bool prepare(const Admission*,Check owner,Check platformSafe,ControlObserver observer=nullptr,StatObserver stat=nullptr);
// These two calls alone can dispatch file I/O. The caller must establish a
// settled Runtime owner boundary, outside provider/capability callbacks and
// ordinary cooperative yield (including the explicit app service checkpoint).
bool checkpoint();
// Owner-only metadata/control replies; never calls a provider or waits.
bool controlCheckpoint();
// Close handles and reset SID/PATH/replies. Prepared service stays admitted.
bool shutdown();
// Final endpoint teardown after a successful reset; no re-prepare/re-admission.
bool stop();
// Transport methods only copy/parser/TX state. They never call file providers.
void input(uint8_t);
void disconnect();
const char* output();
size_t outputSize();
void consumeOutput(size_t);
bool pending();
bool exitSafe();
bool custodySafe();
} }
extern "C" const RiscCpu::NativeSerialFiles::Admission* risc_native_serial_files_admission(void) __attribute__((weak));
#else
namespace RiscCpu { namespace NativeSerialFiles {
inline bool checkpoint(){return true;}
inline bool controlCheckpoint(){return true;}
inline bool shutdown(){return true;}
inline bool stop(){return true;}
inline bool pending(){return false;}
inline bool exitSafe(){return true;}
inline bool custodySafe(){return true;}
} }
#endif
