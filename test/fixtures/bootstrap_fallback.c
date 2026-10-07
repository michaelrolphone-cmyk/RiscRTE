#include <RiscRuntimeV1.h>
__attribute__((visibility("default"))) void app_main(void){
 const risc_runtime_api_v1* api=risc_runtime_get_api(1);
 if(api){api->diagnostic("BOOTSTRAP_INSTALLED_DEFAULT");if(api->struct_size>=sizeof(*api)&&api->confirm_boot)api->confirm_boot();}
}
