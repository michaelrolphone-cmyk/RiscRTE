#pragma once
#include <nvs.h>
namespace RiscExperimental { namespace NvsIteratorCompat {
// SDK6 reports iterator failures separately from EOF. Never collapse an error
// into an empty inventory, which would incorrectly authorize fresh settings.
inline esp_err_t state(esp_err_t result,nvs_iterator_t iterator){
 if((result==ESP_OK&&!iterator)||(result==ESP_ERR_NVS_NOT_FOUND&&iterator))return ESP_FAIL;
 return result;
}
inline esp_err_t find(const char*partition,const char*space,nvs_type_t type,nvs_iterator_t&iterator){
#if RISC_NATIVE_ESP_IDF
 const esp_err_t result=nvs_entry_find(partition,space,type,&iterator);
#else
 iterator=nvs_entry_find(partition,space,type);
 const esp_err_t result=iterator?ESP_OK:ESP_ERR_NVS_NOT_FOUND;
#endif
 return state(result,iterator);
}
inline esp_err_t next(nvs_iterator_t&iterator){
#if RISC_NATIVE_ESP_IDF
 const esp_err_t result=nvs_entry_next(&iterator);
#else
 iterator=nvs_entry_next(iterator);
 const esp_err_t result=iterator?ESP_OK:ESP_ERR_NVS_NOT_FOUND;
#endif
 return state(result,iterator);
}
inline esp_err_t info(nvs_iterator_t iterator,nvs_entry_info_t&out){
#if RISC_NATIVE_ESP_IDF
 return nvs_entry_info(iterator,&out);
#else
 nvs_entry_info(iterator,&out);return ESP_OK;
#endif
}
} }
