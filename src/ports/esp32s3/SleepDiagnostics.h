#pragma once
#include <cstdint>
#include "diagnostics/StageLog.h"
// Recovery follows the selected fixed-function hardware USB transport, even
// when retained diagnostics are disabled. UART and TinyUSB remain unchanged.
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT && defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1
#define RISC_HWCDC_SERIAL 1
#else
#define RISC_HWCDC_SERIAL 0
#endif
#ifndef RISC_ENABLE_USB_PHY
#define RISC_ENABLE_USB_PHY 0
#endif
#if RISC_ENABLE_USB_PHY && !RISC_HWCDC_SERIAL
#error "USB PHY handoff currently requires the ESP32-S3 HWCDC boot console"
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
#ifndef RISC_NATIVE_DIAGNOSTIC_OBSERVER
#define RISC_NATIVE_DIAGNOSTIC_OBSERVER 0
#endif
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
#include <RiscDiagnosticSourceV1.h>
#endif
#ifndef RISC_PERFORMANCE_TRACE
#define RISC_PERFORMANCE_TRACE 0
#endif
// The adapter also supplies bounded live output when the recorder is opted out.
#define RISC_DIAGNOSTIC_ADAPTER (RISC_SLEEP_DIAGNOSTICS || RISC_HWCDC_SLEEP_RECOVERY || RISC_PERFORMANCE_TRACE || RISC_STAGE_LOGS || RISC_NATIVE_DIAGNOSTIC_OBSERVER || RISC_ENABLE_USB_PHY)
namespace RiscDiagnostics {
#if RISC_ENABLE_USB_PHY
bool usbPhyIdle();
bool suspendUsbPhy();
bool resumeUsbPhy();
#endif
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
// Native composition only. Null when the optional read hook is absent.
const risc_diagnostic_source_api_v1* nativeSource();
#endif
#if RISC_STAGE_LOGS && RISC_HWCDC_SERIAL
// Only before Serial.begin(), or after Serial.end() during owner recovery.
// HWCDC setTxBufferSize deletes its old ring; never resize a live transport.
bool prepareSerial();
#endif
#if RISC_DIAGNOSTIC_ADAPTER
void start();
void poll();
void line(const char* text);
// Private native diagnostic adapter admission; never an ELF export.
bool providerDiagnosticReady();
void lightEnter();
void lightReturn(int32_t result,uint32_t cause);
void deepEnter();
#else
inline bool providerDiagnosticReady(){return false;}
inline void lightEnter(){}
inline void lightReturn(int32_t,uint32_t){}
inline void deepEnter(){}
#endif
}
