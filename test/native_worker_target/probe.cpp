// Test-owned startup wrapper. No production ABI or SDK replacement.
#include "ports/esp32s3/CpuPort.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <atomic>
#include <cstdio>
#include "RiscBuildIdentity.h"
namespace {
TaskHandle_t ownerTask;
RiscWorkers::ResidentWorker* worker;
std::atomic<unsigned> entered{0};
std::atomic<bool> foreignDone{false};
int32_t foreignCancel,foreignJoin,foreignRelease;
uint64_t liveToken;
bool owner(){return !xPortInIsrContext()&&xTaskGetCurrentTaskHandle()==ownerTask;}
[[noreturn]] void park(){for(;;)vTaskDelay(pdMS_TO_TICKS(1000));}
void require(bool ok,const char* reason){if(!ok){std::printf("WORKER_PROBE result=unqualified reason=%s retained=1\n",reason);park();}}
void record(const char* name){std::printf("WORKER_PROBE case=%s result=passed\n",name);}
void immediate(void*,const risc_worker_control_v1*){entered.fetch_add(1,std::memory_order_release);}
void cancellable(void*,const risc_worker_control_v1*c){entered.fetch_add(1,std::memory_order_release);while(!c->cancelled(c->context))c->yield_ms(c->context,1);}
void foreign(void*){
 foreignCancel=worker->cancel(1,liveToken);foreignJoin=worker->join(1,liveToken,0);foreignRelease=worker->release(1,liveToken);
 foreignDone.store(true,std::memory_order_release); // Last shared test-state access.
 vTaskDelete(nullptr);
}
bool waitCount(unsigned count){const auto begin=esp_timer_get_time();while(entered.load(std::memory_order_acquire)!=count){if(esp_timer_get_time()-begin>2000000)return false;vTaskDelay(1);}return true;}
}
extern "C" void __wrap_app_main(){
 ownerTask=xTaskGetCurrentTaskHandle();
 std::printf("WORKER_PROBE source=%s coverage=direct-authority target_execution=1\n",RISC_BUILD_SOURCE_SHA);
 // This obtains the authority actually compiled into NativeHardware.cpp, rather
 // than instantiating a second header-local trampoline or host scheduler model.
 const auto hardware=RiscCpu::nativeHardware(owner);worker=hardware.residentWorker;
 require(worker&&worker->idle()&&worker->owner(),"native_authority_admission");
 uint64_t token=99;
 require(worker->start(1,immediate,nullptr,0,1,-1,&token)==RISC_WORKER_INVALID&&!token,"invalid_start");
 vTaskDelay(2);require(entered.load()==0&&worker->idle(),"failed_start_future_entry");record("invalid-start-no-future-entry");
 require(uxTaskPriorityGet(nullptr)<5,"early_completion_priority_precondition");
 require(worker->start(1,immediate,nullptr,4096,5,xPortGetCoreID(),&token)==0,"early_start");
 require(entered.load(std::memory_order_acquire)==1,"early_completion_not_observed");
 require(!worker->idle()&&worker->release(1,token)==RISC_WORKER_BUSY,"completed_unjoined_retention");
 require(worker->join(1,token,0)==0&&!worker->idle(),"early_completion_zero_wait_join");
 require(worker->release(1,token)==0&&worker->idle(),"early_release");record("early-completion-join-release");
 require(worker->cancel(1,token)==RISC_WORKER_STALE&&worker->join(1,token,0)==RISC_WORKER_STALE&&worker->release(1,token)==RISC_WORKER_STALE,"stale_token");record("stale-token");
 entered.store(0);
 require(worker->start(1,cancellable,nullptr,4096,1,-1,&liveToken)==0&&waitCount(1),"live_start");
 require(worker->join(1,liveToken,1)==RISC_WORKER_TIMEOUT&&worker->release(1,liveToken)==RISC_WORKER_BUSY&&!worker->idle(),"timeout_retention");record("join-timeout-retention");
 require(worker->cancel(2,liveToken)==RISC_WORKER_STALE&&worker->join(2,liveToken,0)==RISC_WORKER_STALE&&worker->release(2,liveToken)==RISC_WORKER_STALE,"wrong_scope");record("wrong-scope");
 require(xTaskCreatePinnedToCore(foreign,"worker-foreign",4096,nullptr,1,nullptr,tskNO_AFFINITY)==pdPASS,"foreign_task_create");
 const auto began=esp_timer_get_time();while(!foreignDone.load(std::memory_order_acquire)){require(esp_timer_get_time()-began<=2000000,"foreign_task_timeout");vTaskDelay(1);}
 require(foreignCancel==RISC_WORKER_CONTEXT&&foreignJoin==RISC_WORKER_CONTEXT&&foreignRelease==RISC_WORKER_CONTEXT,"wrong_owner");record("wrong-owner");
 require(worker->cancel(1,liveToken)==0&&worker->join(1,liveToken,2000)==0&&!worker->idle(),"cancel_join");
 require(worker->release(1,liveToken)==0&&worker->idle(),"cancel_release");record("cancellation-join-release");
 std::printf("WORKER_PROBE result=scheduler_cases_passed cases=7 provider_graph_qualified=0 allocation_failure_injected=0 os_reclamation_qualified=0\n");
 // No original app_main, flash, provider graph, display or storage is started.
 // Static callback/result storage remains resident; OS reclamation is not proved.
 park();
}
