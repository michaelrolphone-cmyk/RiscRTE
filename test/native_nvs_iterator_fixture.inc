/* Common declaration-level SDK4/SDK6 boundary for source-collector host tests.
 * Actual SDK6 headers are checked independently by the target compile gate. */
#if RISC_NATIVE_ESP_IDF
static unsigned sdk6IteratorFault=0;
extern "C" esp_err_t nvs_entry_find(const char*p,const char*n,nvs_type_t t,nvs_iterator_t*out){
 if(sdk6IteratorFault==1){*out=nullptr;return ESP_ERR_NO_MEM;}
 if(sdk6IteratorFault==6){*out=nullptr;return ESP_OK;}
 if(sdk6IteratorFault==4){*out=fixture_entry_find(p,n,t);return ESP_FAIL;}
 *out=fixture_entry_find(p,n,t);return *out?ESP_OK:ESP_ERR_NVS_NOT_FOUND;
}
extern "C" esp_err_t nvs_entry_next(nvs_iterator_t*out){
 if(sdk6IteratorFault==2)return ESP_FAIL; // live iterator must still be released
 if(sdk6IteratorFault==5)return ESP_ERR_NVS_NOT_FOUND; // invalid EOF with a live iterator
 if(sdk6IteratorFault==7){while(*out)*out=fixture_entry_next(*out);return ESP_OK;} // invalid success with null
 *out=fixture_entry_next(*out);return *out?ESP_OK:ESP_ERR_NVS_NOT_FOUND;
}
extern "C" esp_err_t nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t*out){
 if(sdk6IteratorFault==3)return ESP_FAIL;
 fixture_entry_info(it,out);return ESP_OK;
}
#else
extern "C" nvs_iterator_t nvs_entry_find(const char*p,const char*n,nvs_type_t t){return fixture_entry_find(p,n,t);}
extern "C" nvs_iterator_t nvs_entry_next(nvs_iterator_t it){return fixture_entry_next(it);}
extern "C" void nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t*out){fixture_entry_info(it,out);}
#endif
