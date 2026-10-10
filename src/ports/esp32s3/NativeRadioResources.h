#pragma once
namespace RiscCpu {
// Defined in NativeHardware.cpp beside the unique native radio state. Never
// include the header-only NativeRadio implementation in another native TU.
bool nativeRadioResourceTry();
void nativeRadioResourceEnd();
bool nativeRadioResourceReady();
bool nativeRadioResourceHeldReady();
struct NativeRadioResourceGuard {
  bool held;
  NativeRadioResourceGuard():held(nativeRadioResourceTry()){}
  ~NativeRadioResourceGuard(){if(held)nativeRadioResourceEnd();}
  NativeRadioResourceGuard(const NativeRadioResourceGuard&)=delete;
  NativeRadioResourceGuard& operator=(const NativeRadioResourceGuard&)=delete;
};
}
