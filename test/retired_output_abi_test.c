/* Compile as both C11 and C++17 against the frozen Watch consumer prefix. */
#define garden_gpio_v1 watch_gpio_v1
#define garden_spi_v1 watch_spi_v1
#define garden_radio_v1 watch_radio_v1
#include "fixtures/watch/sdk/driver/GardenPlatformV1.h"
#undef garden_gpio_v1
#undef garden_spi_v1
#undef garden_radio_v1
#include <GardenPlatformV1.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef __cplusplus
#define CHECK_ABI static_assert
#else
#define CHECK_ABI _Static_assert
#endif
#define WATCH_FIELD(field) CHECK_ABI(offsetof(watch_gpio_v1,field)==offsetof(garden_gpio_v1,field),#field " offset")
WATCH_FIELD(api_version);WATCH_FIELD(struct_size);WATCH_FIELD(context);
WATCH_FIELD(claim);WATCH_FIELD(write);WATCH_FIELD(read);WATCH_FIELD(pwm);
WATCH_FIELD(release);WATCH_FIELD(waveform);
CHECK_ABI(sizeof(watch_gpio_v1)==offsetof(garden_gpio_v1,light_sleep),"frozen Watch prefix");
typedef struct {
    watch_gpio_v1 base;
    risc_gpio_light_sleep_v1 light_sleep;
    risc_gpio_deep_sleep_v1 deep_sleep;
    risc_gpio_deep_sleep_hold_v1 deep_sleep_hold;
    risc_gpio_light_sleep_for_v1 light_sleep_for;
    risc_gpio_deep_sleep_for_v1 deep_sleep_for;
    risc_gpio_wake_source_v1 wake_source;
    risc_gpio_light_sleep_set_v1 light_sleep_set;
    risc_gpio_deep_sleep_set_v1 deep_sleep_set;
    bool (*retire_held_output)(void*,uint64_t);
} previous_gpio_v1;
#define PREVIOUS_FIELD(field) CHECK_ABI(offsetof(previous_gpio_v1,field)==offsetof(garden_gpio_v1,field),#field " offset")
PREVIOUS_FIELD(light_sleep);PREVIOUS_FIELD(deep_sleep);PREVIOUS_FIELD(deep_sleep_hold);
PREVIOUS_FIELD(light_sleep_for);PREVIOUS_FIELD(deep_sleep_for);PREVIOUS_FIELD(wake_source);
PREVIOUS_FIELD(light_sleep_set);PREVIOUS_FIELD(deep_sleep_set);PREVIOUS_FIELD(retire_held_output);
CHECK_ABI(sizeof(previous_gpio_v1)==GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE,"previous table size");
CHECK_ABI(sizeof(previous_gpio_v1)==offsetof(garden_gpio_v1,read_retired_output),"append-only suffix");
CHECK_ABI(sizeof(garden_gpio_v1)==GARDEN_GPIO_READ_RETIRED_OUTPUT_V1_SIZE,"new suffix size");
static bool supported(const garden_gpio_v1* api){
    return api && api->api_version==1 && api->struct_size>=GARDEN_GPIO_READ_RETIRED_OUTPUT_V1_SIZE && api->read_retired_output;
}
static bool read_pin(void* context,uint8_t pin,bool* level){(void)context;(void)pin;(void)level;return false;}
int main(void){
    /* Exact-size allocations make a speculative suffix read fail under ASan. */
    const size_t sizes[]={sizeof(watch_gpio_v1),sizeof(previous_gpio_v1),GARDEN_GPIO_READ_RETIRED_OUTPUT_V1_SIZE-1};
    size_t i;assert(!supported(NULL));
    for(i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i){
        garden_gpio_v1* old=(garden_gpio_v1*)calloc(1,sizes[i]);assert(old);
        old->api_version=1;old->struct_size=(uint32_t)sizes[i];assert(!supported(old));free(old);
    }
    garden_gpio_v1 api={0};api.api_version=1;api.struct_size=sizeof(api);
    assert(!supported(&api));api.read_retired_output=read_pin;assert(supported(&api));
    api.api_version=2;assert(!supported(&api));
    puts("GPIO ABI: frozen Watch and all previous field offsets, C/C++ suffix sizing and safe old-table rejection PASS");
}
