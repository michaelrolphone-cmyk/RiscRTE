#pragma once
#include <cstdint>
// Recovery follows the selected fixed-function hardware USB transport, even
// when retained diagnostics are disabled. UART and TinyUSB remain unchanged.
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT && defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1
#define RISC_HWCDC_SERIAL 1
#else
#define RISC_HWCDC_SERIAL 0
#endif
#ifndef RISC_SLEEP_DIAGNOSTICS
#if RISC_HWCDC_SERIAL
#define RISC_SLEEP_DIAGNOSTICS 1
#else
#define RISC_SLEEP_DIAGNOSTICS 0
#endif
#endif
// The installer owns a separate transport loop and never enters native sleep.
// Preserve its prior diagnostics selection and raw-Serial transport behavior.
#if RISC_HWCDC_SERIAL && !defined(RISC_OWNER_INSTALLER)
#define RISC_HWCDC_SLEEP_RECOVERY 1
#else
#define RISC_HWCDC_SLEEP_RECOVERY 0
#endif
// The adapter also supplies bounded live output when the recorder is opted out.
#define RISC_DIAGNOSTIC_ADAPTER (RISC_SLEEP_DIAGNOSTICS || RISC_HWCDC_SLEEP_RECOVERY)
namespace RiscDiagnostics {
#if RISC_DIAGNOSTIC_ADAPTER
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
