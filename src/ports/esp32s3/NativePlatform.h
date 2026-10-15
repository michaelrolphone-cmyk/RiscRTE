#pragma once
// Native SDK services used by Runtime only. Nothing here is exported to ELFs.
// The legacy adapter exists only to keep delivered IDF4 builds reproducible.
#include <cstddef>
#include <cstdint>
#include "NativeTime.h"
#if RISC_NATIVE_ESP_IDF
#include <esp_timer.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace RiscPlatform {
inline uint32_t freeHeap(){return esp_get_free_heap_size();}
inline void delay(uint32_t ms){vTaskDelay(ms?pdMS_TO_TICKS(ms)+1:1);}
class Console {
  bool installed_=false;
  int core_=-1,cached_=-1;
  size_t txBytes_=256;
public:
  Console()=default;
  Console(const Console&)=delete;
  Console& operator=(const Console&)=delete;
  Console(Console&&)=delete;
  Console& operator=(Console&&)=delete;
  void begin(unsigned baud);
  void end();
  explicit operator bool()const;
  int available();
  int read();
  // Upper bound for one nonblocking write; write may accept fewer bytes.
  int availableForWrite()const;
  size_t write(const uint8_t*,size_t);
  size_t println(const char*);
  size_t setTxBufferSize(size_t);
  void setTxTimeoutMs(unsigned){} // Native writes always have a zero timeout.
};
extern Console console;
}
#else
#include <Arduino.h>
namespace RiscPlatform {
uint32_t freeHeap();
#if RISC_ENABLE_USB_PHY || defined(RISC_OWNER_INSTALLER)
inline void delay(uint32_t ms){::delay(ms);}
#endif
inline auto& console=Serial;
}
#endif
