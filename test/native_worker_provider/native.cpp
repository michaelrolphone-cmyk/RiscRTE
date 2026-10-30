#include "ports/esp32s3/CpuPort.h"
#include "runtime/storage/PosixFiles.h"
#include "runtime/drivers/SelectedNativeProviderPolicyV1.h"
#include <esp_elf.h>
#include <esp_heap_caps.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>
#include <cstring>
#include <new>
#include "RiscBuildIdentity.h"
#include "WorkerGraphProfile.h"
extern esp_err_t riscrte_mount_embedded_store();
extern "C" void __real_esp_elf_deinit(esp_elf_t*);
namespace {
TaskHandle_t ownerTask;
RiscCpu::Port* selectedPort;
RiscWorkers::ResidentWorker* workers;
unsigned providerTeardownAttempts,providerTeardownReturns;
bool cleanReported,retainedReported,appFailure;
bool owner(){return !xPortInIsrContext()&&xTaskGetCurrentTaskHandle()==ownerTask;}
[[noreturn]] void park(){for(;;)vTaskDelay(pdMS_TO_TICKS(1000));}
void require(bool ok,const char* why){if(!ok){std::printf("WORKER_GRAPH result=unqualified reason=%s retained=1\n",why);park();}}
bool diagnostic(const char* line){
 if(!owner()||!line)return false;
 if(!std::strcmp(line,"WORKER_GRAPH app=clean-passed"))cleanReported=true;
 if(!std::strcmp(line,"WORKER_GRAPH app=retained-prepared"))retainedReported=true;
 if(std::strstr(line,"WORKER_GRAPH app=unqualified"))appFailure=true;
 std::puts(line);return true;
}
struct Files {
 RiscStorage::PosixFiles provider;
 RiscBoot::RuntimeFiles file;
 RiscBoot::RuntimeFileSources::Entry entry;
 RiscBoot::RuntimeFileSources sources;
 explicit Files(const char* root):provider(root),entry{root,&file},sources(&entry,1){
  require(file.bind(provider.api(),owner,owner,provider.identityApi(),provider.directoryApi()),"typed_file_binding");
 }
};
void runCase(const char* root,bool retained){
 const auto hw=RiscCpu::nativeHardware(owner);workers=hw.residentWorker;require(workers&&workers->idle(),"authority_initial");
 selectedPort=new(std::nothrow) RiscCpu::Port(hw);require(selectedPort,"port_allocation");
 auto* files=new(std::nothrow) Files(root);require(files,"file_owner_allocation");
 RiscBoot::Port port{};port.owner=owner;port.health=[](risc_runtime_health_v1*){return true;};port.delay=[](uint32_t ms){vTaskDelay(pdMS_TO_TICKS(ms)?pdMS_TO_TICKS(ms):1);};port.log=diagnostic;
 port.bindPlatforms=[](RiscBoot::Runtime& runtime){return selectedPort->bind(runtime);};
 port.appExitSafe=[](){return selectedPort->appExitSafe();};port.providerStorageSafe=[](){return selectedPort->providerStorageSafe();};port.nativeProviders=RuntimeProviders::selectedNativeProviderPoliciesV1();
 void* storage=heap_caps_malloc(sizeof(RiscBoot::Runtime),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);require(storage,"runtime_metadata_allocation");
 auto* runtime=new(storage) RiscBoot::Runtime(port);require(runtime->bindFileSources(&files->sources),"file_source_lifetime");
 require(runtime->prepare(root),runtime->error());
 const unsigned beforeAttempts=providerTeardownAttempts,beforeReturns=providerTeardownReturns;
 const bool ran=runtime->run();require(!appFailure,"app_reported_failure");
 if(retained){
  require(retainedReported&&!ran&&runtime->retained()&&!workers->idle(),"retained_runtime_worker");
  const auto usage=runtime->grantUsage();require(usage.selectedProviders==1&&usage.graphLive>0,"retained_dependency_grants");
  require(!runtime->run(),"retained_reentry");
  require(providerTeardownAttempts==beforeAttempts&&providerTeardownReturns==beforeReturns,"retained_image_teardown");
  std::printf("WORKER_GRAPH case=retained result=passed graph_live=%u teardown_attempts=0 teardown_returns=0\n",unsigned(usage.graphLive));
  // Keep Runtime, files, selected Port and both mapped images permanently owned.
 }else{
  require(cleanReported&&ran&&!runtime->retained()&&workers->idle()&&!runtime->metadataCloseRetained(),"clean_runtime_worker");
  require(selectedPort->quiescent(),"clean_pre_destructor_quiescence");
  runtime->~Runtime(); // Only after observed clean custody; metadata allocation retained.
  require(providerTeardownAttempts==beforeAttempts+1&&providerTeardownReturns==beforeReturns+1,"clean_provider_teardown_return");
  require(selectedPort->quiescent(),"clean_port_quiescence");
  std::printf("WORKER_GRAPH case=clean result=passed teardown_attempts=1 teardown_returns=1\n");
  // The diagnostic does not claim all firmware/allocator resources reclaimed.
 }
}
}
extern "C" void __wrap_esp_elf_deinit(esp_elf_t* image){
 bool selected=false;
 // Identify the validated selected provider while its symbol storage is live.
 if(image&&image->symtab)for(uint16_t i=0;i<image->num;++i)if(image->symtab[i].name&&!std::strcmp(image->symtab[i].name,"t5_driver_get"))selected=true;
 if(selected)++providerTeardownAttempts;
 __real_esp_elf_deinit(image);
 // The function is void. This records normal completion, not entry or an
 // invented return code. Never dereference image/symbol storage after teardown.
 if(selected)++providerTeardownReturns;
}
extern "C" void __wrap_app_main(){
 ownerTask=xTaskGetCurrentTaskHandle();std::printf("WORKER_GRAPH source=%s\n",RISC_BUILD_SOURCE_SHA);
 require(riscrte_mount_embedded_store()==ESP_OK,"immutable_store_mount");
 runCase("/bootfs",WORKER_GRAPH_RETAINED);
 std::printf("WORKER_GRAPH result=provider_case_passed profile=" WORKER_GRAPH_PROFILE " retained=1 cleanup_qualified=0\n");park();
}
