// Execute the production worker against a deterministic, single-core FreeRTOS
// model. Timer notifications do not unblock vTaskDelay, just as on the target.
// Owner-side stop calls model the other CPU; callback execution is never killed.
#include "ports/esp32s3/NativeRadioIq.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

esp_chip_info_t test_chip{CHIP_ESP32S3,CHIP_FEATURE_WIFI_BGN|CHIP_FEATURE_BLE};
int test_wifi_status=ESP_ERR_WIFI_NOT_INIT;
extern "C" {
extern const uint32_t _rom_chip_id=CHIP_ESP32S3;
extern const int32_t _rom_eco_version=0;
void esp_phy_modem_init(void){assert(false);}
void esp_wifi_bt_power_domain_on(void){assert(false);}
void esp_phy_enable(void){assert(false);}
void esp_phy_disable(void){assert(false);}
void esp_wifi_bt_power_domain_off(void){assert(false);}
void esp_phy_modem_deinit(void){assert(false);}
}
uint32_t iq_reg_read(uint32_t){assert(false);return 0;}

namespace Model {
struct Suspended {};
constexpr uint64_t TickUs=1000;
unsigned taskStorage=0,timerStorage=0,semaphoreStorage=0;
void(*taskEntry)(void*)=nullptr;
void(*timerCallback)(void*)=nullptr;
uint32_t notifications=0,maxTaken=0;
uint64_t time=0,period=0,nextTimer=0,busy=0,maxBusy=0,idle=0;
unsigned captures=0,limit=0,delays=0,taskDeletes=0,timerDeletes=0,semaphoreDeletes=0,joinTimeouts=0;
uint64_t captureUs=0;
bool timerRunning=false,semaphoreGiven=false,inCallback=false,runOnJoin=false,taskFinished=false;
bool failSemaphore=false,failTimer=false,failTask=false,failStart=false,stopDuringDelay=false;
std::vector<uint64_t> stamps;
int argument=42;
void advance(uint64_t us,bool blocked){
 if(blocked){assert(!inCallback);idle+=us;busy=0;}else{busy+=us;maxBusy=std::max(maxBusy,busy);}
 const auto end=time+us;
 while(timerRunning && nextTimer<=end){time=nextTimer;nextTimer+=period;timerCallback(nullptr);}
 time=end;
}
void run(){try{taskEntry(nullptr);assert(false);}catch(const Suspended&) {}}
void requestStop(){
 using namespace RiscCpu::NativeRadioIq;
 const auto task=workerTask;const auto timer=workerTimer;const auto done=workerDone;
 const auto tick=workerTick;const auto arg=workerArgument;
 assert(!workerStop()); // A callback or delay is still owned: retain everything.
 assert(workerTask==task && workerTimer==timer && workerDone==done);
 assert(workerTick==tick && workerArgument==arg && phyPrepared && !cleanup());
 assert(!taskDeletes && !timerDeletes && !semaphoreDeletes);
}
void capture(void* arg,uint64_t stamp){
 assert(arg==&argument && stamp==time && !inCallback);
 inCallback=true;stamps.push_back(stamp);++captures;advance(captureUs,false);
 if(captures==limit && !stopDuringDelay)requestStop();
 inCallback=false;
}
void reset(){
 using namespace RiscCpu::NativeRadioIq;
 assert(!workerTask && !workerTimer && !workerDone);
 phyPrepared=true;workerTick=nullptr;workerArgument=nullptr;
 taskEntry=nullptr;timerCallback=nullptr;notifications=maxTaken=0;
 time=period=nextTimer=busy=maxBusy=idle=0;
 captures=limit=delays=taskDeletes=timerDeletes=semaphoreDeletes=joinTimeouts=0;
 captureUs=0;timerRunning=semaphoreGiven=inCallback=runOnJoin=taskFinished=false;
 failSemaphore=failTimer=failTask=failStart=stopDuringDelay=false;stamps.clear();
}
void assertReleased(){
 using namespace RiscCpu::NativeRadioIq;
 assert(!workerTask && !workerTimer && !workerDone && !workerTick && !workerArgument);
 assert(taskDeletes==1 && timerDeletes==1 && semaphoreDeletes==1);
 assert(workerStop()); // Idempotent, no duplicate destruction.
 assert(taskDeletes==1 && timerDeletes==1 && semaphoreDeletes==1);
}
}

int64_t esp_timer_get_time(){return static_cast<int64_t>(Model::time);}
int esp_timer_create(const esp_timer_create_args_t* config,esp_timer_handle_t* timer){
 if(Model::failTimer)return -1;
 assert(!strcmp(config->name,"iq-period"));Model::timerCallback=config->callback;
 *timer=&Model::timerStorage;return ESP_OK;
}
int esp_timer_start_periodic(esp_timer_handle_t timer,uint64_t period){
 assert(timer==&Model::timerStorage);if(Model::failStart)return -1;
 Model::period=period;Model::nextTimer=Model::time+period;Model::timerRunning=true;return ESP_OK;
}
int esp_timer_stop(esp_timer_handle_t timer){assert(timer==&Model::timerStorage);Model::timerRunning=false;return ESP_OK;}
int esp_timer_delete(esp_timer_handle_t timer){assert(timer==&Model::timerStorage);++Model::timerDeletes;return ESP_OK;}
void xTaskNotifyGive(TaskHandle_t task){assert(task==&Model::taskStorage);++Model::notifications;}
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t ticks){
 using namespace Model;
 assert(clear==pdTRUE && ticks==portMAX_DELAY);
 if(!notifications){assert(timerRunning);advance(nextTimer-time,true);}
 assert(notifications);const auto n=notifications;notifications=0;maxTaken=std::max(maxTaken,n);return n;
}
void vTaskDelay(TickType_t ticks){
 using namespace Model;
 assert(ticks==1 && !inCallback);++delays;
 if(stopDuringDelay && captures==limit)requestStop();
 // FreeRTOS delays until the next tick, not a whole millisecond from now.
 advance(TickUs-time%TickUs,true);
}
void vTaskSuspend(TaskHandle_t task){assert(!task && Model::semaphoreGiven && !Model::inCallback);throw Model::Suspended{};}
void vTaskDelete(TaskHandle_t task){assert(task==&Model::taskStorage && !Model::inCallback && Model::taskFinished);++Model::taskDeletes;}
BaseType_t xTaskCreatePinnedToCore(void(*entry)(void*),const char* name,uint32_t stack,void* arg,unsigned priority,TaskHandle_t* task,unsigned core){
 assert(!strcmp(name,"iq-envelope") && stack==4096 && !arg && priority==3 && core==0);
 if(Model::failTask)return 0;
 Model::taskEntry=entry;*task=&Model::taskStorage;return pdPASS;
}
SemaphoreHandle_t xSemaphoreCreateBinary(){return Model::failSemaphore?nullptr:&Model::semaphoreStorage;}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore){assert(semaphore==&Model::semaphoreStorage && !Model::inCallback);Model::semaphoreGiven=Model::taskFinished=true;return pdTRUE;}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore,TickType_t timeout){
 assert(semaphore==&Model::semaphoreStorage && timeout==pdMS_TO_TICKS(100));
 if(!Model::semaphoreGiven && Model::runOnJoin)Model::run();
 if(Model::semaphoreGiven){Model::semaphoreGiven=false;return pdTRUE;}
 ++Model::joinTimeouts;return 0;
}
void vSemaphoreDelete(SemaphoreHandle_t semaphore){assert(semaphore==&Model::semaphoreStorage);++Model::semaphoreDeletes;}

int main(int argc,char** argv){
 using namespace Model;using namespace RiscCpu::NativeRadioIq;
 const bool reproduce=argc==2 && !strcmp(argv[1],"--reproduce-starvation");
 reset();captureUs=2000;limit=3001;
 assert(workerStart(1000,capture,&argument));run();
 printf("Native IQ overload: captures=%u max_continuous_busy_us=%llu scheduler_blocks=%u coalesced_notifications=%u\n",captures,static_cast<unsigned long long>(maxBusy),delays,maxTaken);
 if(reproduce){assert(maxBusy>6000000 && !delays && maxTaken>1);puts("PRE-FIX REPRODUCED: pending timer notifications prevent CPU0 worker from blocking for more than six seconds");return 0;}
 assert(captures==limit && maxBusy==captureUs && delays==limit && maxTaken>1 && idle>=limit*TickUs);
 for(unsigned i=1;i<stamps.size();++i)assert(stamps[i]-stamps[i-1]==captureUs+TickUs);
 assert(joinTimeouts==1 && workerStop());assertReleased();
 // Underloaded maximum interval retains timer pacing and real timestamps.
 reset();captureUs=100;limit=4;assert(workerStart(100000,capture,&argument));run();
 assert(captures==4 && delays==4 && maxBusy==100 && maxTaken==1);
 for(unsigned i=1;i<stamps.size();++i)assert(stamps[i]-stamps[i-1]==100000);
 assert(workerStop());assertReleased();
 // A stop concurrent with the scheduler block wakes the notify wait, but must
 // not invoke another provider callback or unmap one still owned by the worker.
 reset();captureUs=100;limit=1;stopDuringDelay=true;
 assert(workerStart(1000,capture,&argument));run();assert(captures==1 && delays==1 && joinTimeouts==1);
 assert(workerStop());assertReleased();
 // Stop before the first timer deadline: no callback, no unnecessary delay.
 reset();runOnJoin=true;assert(workerStart(1000,capture,&argument));assert(workerStop());
 assert(!captures && !delays);assertReleased();
 // Invalid requests and all allocation/start failure paths preserve ownership.
 reset();phyPrepared=false;assert(!workerStart(1000,capture,&argument));phyPrepared=true;
 assert(!workerStart(999,capture,&argument) && !workerStart(100001,capture,&argument) && !workerStart(1000,nullptr,&argument));
 failSemaphore=true;assert(!workerStart(1000,capture,&argument));assert(!workerDone && !workerTimer && !workerTask);
 reset();failTimer=true;assert(!workerStart(1000,capture,&argument));assert(!workerDone && !workerTimer && !workerTask && semaphoreDeletes==1);
 reset();failTask=true;assert(!workerStart(1000,capture,&argument));assert(!workerDone && !workerTimer && !workerTask && semaphoreDeletes==1 && timerDeletes==1);
 reset();failStart=true;runOnJoin=true;assert(!workerStart(1000,capture,&argument));assertReleased();assert(!captures);
 reset();failStart=true;assert(workerStart(1000,capture,&argument));assert(workerTask && workerTimer && workerDone && workerStopping && !cleanup());
 assert(!workerStart(1000,capture,&argument));runOnJoin=true;assert(workerStop());assertReleased();assert(!captures);
 // No late timer notification may target the destroyed task.
 const auto priorNotifications=notifications;workerWake(nullptr);assert(notifications==priorNotifications);
 puts("Native IQ worker: overload idle fairness, real timestamps, timer pacing, stop/join retention, restart and failure rollback PASS");
}
