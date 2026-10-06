#pragma once
#include <cstdint>
// The existing UART/other targets stay unchanged. Hardware-USB targets opt in
// by their already-selected transport; a build can explicitly disable tracing.
#ifndef RISC_SLEEP_DIAGNOSTICS
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT && defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE
#define RISC_SLEEP_DIAGNOSTICS 1
#else
#define RISC_SLEEP_DIAGNOSTICS 0
#endif
#endif
namespace RiscDiagnostics {
#if RISC_SLEEP_DIAGNOSTICS
void start();
void poll();
void line(const char* text);
void lightEnter();
void lightReturn(int32_t result,uint32_t cause);
void deepEnter();
#else
inline void lightEnter(){}
inline void lightReturn(int32_t,uint32_t){}
inline void deepEnter(){}
#endif
}
