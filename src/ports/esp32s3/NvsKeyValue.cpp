#if defined(ESP_PLATFORM) && !defined(RISC_EMBEDDED_BOOTSTORE)
#include "NvsKeyValue.h"
namespace {
esp_err_t status=ESP_ERR_INVALID_STATE;
bool attempted=false;
}
extern "C" esp_err_t __real_nvs_flash_init();
extern "C" esp_err_t __wrap_nvs_flash_init(){
  if(!attempted){attempted=true;status=__real_nvs_flash_init();}
  // Arduino 2.0.17 initArduino erases the *whole NVS partition* on these two
  // errors. Preserve the real error for the backend, but return generic failure
  // so Arduino logs and continues without entering its erase/recovery branch.
  return status==ESP_ERR_NVS_NO_FREE_PAGES || status==ESP_ERR_NVS_NEW_VERSION_FOUND ? ESP_FAIL : status;
}
namespace RiscNvs {esp_err_t initializationStatus(){return status;}}
#endif
