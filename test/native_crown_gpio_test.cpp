#include "ports/esp32s3/NativeCrownRecovery.h"
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <cassert>
#include <cstdio>
#include <cstring>
namespace {
uint32_t ms=0,reboots=0;bool locked=false,failTask=false;
void(*worker)(void*)=nullptr;const char *scenario;
struct Stop{};struct Reboot{};
bool is(const char*s){return !strcmp(scenario,s);}
}
SemaphoreHandle_t xSemaphoreCreateMutex(){return reinterpret_cast<void*>(1);}
int xSemaphoreTake(SemaphoreHandle_t,TickType_t){if(locked)return 0;locked=true;return pdTRUE;}
void xSemaphoreGive(SemaphoreHandle_t){assert(locked);locked=false;}
int xTaskCreatePinnedToCore(void(*f)(void*),const char*,uint32_t stack,void*,unsigned priority,TaskHandle_t*h,int core){
 assert(stack>=3072 && priority==configMAX_PRIORITIES-2 && core==0);if(failTask)return 0;worker=f;*h=reinterpret_cast<void*>(1);return pdPASS;
}
void vTaskDelay(TickType_t t){
 ms+=t;
 using namespace RiscCpu::NativeCrownRecovery;
 if(is("clean-exit") && exitRequested()){assert(endInvocation());assert(!exitRequested());}
 if(is("close") && ms>=200)assert(gpioClosing(10));
 if(is("wake") && ms>=600 && ms<620)resume(true);
 if(ms>6500)throw Stop{};
}
int gpio_get_level(gpio_num_t pin){
 assert(pin==10);
 if(is("boot-release"))return ms<200;
 if(is("wake"))return (ms>=600 && ms<800);
 if(is("held") || is("close"))return 1;
 return ms>=100 && ms<250;
}
int esp_task_wdt_add(void*){return 0;}
int esp_task_wdt_reset(){return 0;}
int64_t esp_timer_get_time(){return int64_t(ms)*1000;}
esp_reset_reason_t esp_reset_reason(){return reboots?ESP_RST_SW:ESP_RST_POWERON;}
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(){return ESP_SLEEP_WAKEUP_UNDEFINED;}
[[noreturn]] void esp_restart_noos(){++reboots;throw Reboot{};}
bool raw(uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){return true;}
int main(int argc,char**argv){
 assert(argc==2);scenario=argv[1];using namespace RiscCpu::NativeCrownRecovery;
 assert(opened(0,15,14,raw));assert(gpioOpened(9,true));assert(!worker);
 assert(!gpioOpened(10,true));failTask=is("task-failure");
 bool ok=gpioOpened(10,false);
 if(failTask){assert(!ok);assert(gpioClosing(10));return 0;}
 assert(ok && worker);beginInvocation(!is("home-short"));
 if(is("sleep") || is("wake"))assert(pause());
 try{worker(nullptr);}catch(const Stop&){}catch(const Reboot&){locked=false;}
 if(is("held")){assert(reboots==1 && ms>=5020 && ms<5050);assert(previousReason()==1);}
 else if(is("exit-timeout")){assert(reboots==1 && ms>=3270 && ms<3300);assert(previousReason()==2);}
 else {assert(!reboots);assert(!exitRequested());}
 if(is("home-short") || is("clean-exit") || is("exit-timeout"))assert(shortSequence()==1);
 if(is("wake") || is("boot-release") || is("sleep") || is("close"))assert(shortSequence()==0);
 assert(gpioClosing(10));puts("Native GPIO crown supervisor PASS");
}
