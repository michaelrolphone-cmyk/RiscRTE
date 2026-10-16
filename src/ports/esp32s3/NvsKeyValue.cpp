#if defined(ESP_PLATFORM) && !defined(RISC_EMBEDDED_BOOTSTORE)
#include "NvsKeyValue.h"
#include <atomic>
namespace {
std::atomic<esp_err_t> status{ESP_ERR_INVALID_STATE};
bool attempted=false;
}
extern "C" esp_err_t __real_nvs_flash_init();
extern "C" esp_err_t __wrap_nvs_flash_init(){
  if(RiscNvs::quarantined())return ESP_FAIL;
  if(!attempted){attempted=true;status=__real_nvs_flash_init();}
  // Arduino 2.0.17 initArduino erases the *whole NVS partition* on these two
  // errors. Preserve the real error for the backend, but return generic failure
  // so Arduino logs and continues without entering its erase/recovery branch.
  const auto result=status.load();
  return result==ESP_ERR_NVS_NO_FREE_PAGES || result==ESP_ERR_NVS_NEW_VERSION_FOUND ? ESP_FAIL : result;
}
namespace RiscNvs {
esp_err_t initializationStatus(){return quarantined()?ESP_FAIL:status.load();}
// No reset API: a failed mutation can invalidate the live SDK cache for every
// namespace. Preserve raw flash and reject all native clients until boot.
void quarantine(){detail::mutationQuarantined.store(true);}
}
#endif
