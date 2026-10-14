#include "bootstrap/Runtime.h"
#include "runtime/storage/StorageWorker.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <RiscStorageSessionAdmissionV1.h>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace RiscCpu;
static bool worker=false,isr=false,otherTask=false,closeOkay=true;
static unsigned ioCalls=0;
static bool (*usbIdleHook)()=nullptr;
static bool owner(){return !worker&&!isr&&!otherTask;}
struct Gate {
 bool syncCredit=false,submitCredit=false,collectCredit=false,cancelCredit=false,released=false;
 const void*request=nullptr;uint32_t bytes=0;uint64_t scope=0,token=0;
 NativeStorageCustody hooks(){return {this,
 [](void*c,uint64_t device){auto&g=*static_cast<Gate*>(c);assert(owner()&&device==5);bool okay=g.syncCredit;g.syncCredit=false;return okay;},
 [](void*c,uint64_t scope,const void*request,uint32_t bytes){auto&g=*static_cast<Gate*>(c);assert(owner());if(!g.submitCredit||scope!=g.scope||request!=g.request||bytes!=g.bytes)return false;g.submitCredit=false;return true;},
 [](void*c,uint64_t scope,int32_t rc,uint64_t token){auto&g=*static_cast<Gate*>(c);assert(owner()&&scope==g.scope);if(rc==0)g.token=token;},
 [](void*c,uint64_t scope,uint64_t token){auto&g=*static_cast<Gate*>(c);assert(owner());if(!g.collectCredit||scope!=g.scope||token!=g.token)return false;g.collectCredit=false;return true;},
 [](void*c,uint64_t scope,uint64_t token){auto&g=*static_cast<Gate*>(c);assert(owner());if(!g.cancelCredit||scope!=g.scope||token!=g.token)return false;g.cancelCredit=false;return true;},
 [](void*c){assert(owner());return static_cast<Gate*>(c)->released;}};}
};
struct Fixture {
 RiscStorage::Worker mailbox;
 StorageWorkBackend backend{&mailbox,[](){return true;},[](){return worker;},[](uint32_t){}};
 Hardware hw{};RiscBoot::Runtime runtime{{}};Port port;
 Fixture():hw(make()),port(hw){
  RiscBoot::Board::Device d{};strcpy(d.type,"gpio.bank");d.hardware.instance_id=5;d.config.gpio={sizeof(risc_hw_gpio_bank_v1),4,1,1,0,{5,41,42,40},0,0,0};
  assert(port.gpioScope(runtime,d,port.gpios_[0]));assert(port.configureStorageWork(port.gpios_[0]));
  port.syncs_[0].port=&port;port.syncs_[0].instance=5;port.syncs_[1].port=&port;port.syncs_[1].instance=9;port.syncCount_=2;
  port.usb_.port=&port;
 }
 Hardware make(){Hardware h{};h.owner=owner;h.storageWork=&backend;
  h.gpioOpen=[](uint8_t,bool,bool,bool){return true;};h.gpioWrite=[](uint8_t,bool){++ioCalls;return true;};h.gpioClose=[](uint8_t){return true;};
  h.sdmmcOpen=[](uint8_t,uint8_t,uint8_t,uint32_t hz,risc_sdmmc_card_info_v1*out){++ioCalls;*out={sizeof(*out),512,64,hz,0};return true;};
  h.sdmmcRead=[](uint64_t,uint32_t n,void*out){++ioCalls;memset(out,0x5a,n*512);return true;};h.sdmmcWrite=[](uint64_t,uint32_t,const void*){++ioCalls;return true;};h.sdmmcSync=[](){++ioCalls;return true;};h.sdmmcClose=[](){++ioCalls;return closeOkay;};
  h.usbPhyIdle=[](){return usbIdleHook?usbIdleHook():true;};h.usbPhySuspend=[](){assert(false);return false;};h.usbPhyResume=[](){return true;};return h;
 }
 risc_storage_worker_v1&api(){return port.storageWork_.api.base.base;}
 void fresh(){auto&a=api();risc_storage_work_scope_v1 scope{5,41,42,40,20000000,true};assert(Port::syncOwner(&port.syncs_[0]));assert(a.claim(a.context,&scope)==0);assert(a.bind(a.context,execute,&a)==0);}
 static int32_t execute(void*c,const void*in,uint32_t n,void*out,uint32_t,uint32_t*size){assert(worker&&n==1);auto&a=*static_cast<risc_storage_worker_v1*>(c);const auto op=*static_cast<const unsigned char*>(in);*size=0;
  if(op==1){assert(a.io->power(a.io->context,false));risc_sdmmc_card_info_v1 card{};assert(a.io->open(a.io->context,&card));}
  else if(op==2){assert(a.io->read(a.io->context,0,1,out));*size=512;}
  else if(op==3){return a.io->close(a.io->context)?0:-1;}
  return 0;
 }
 void cleanup(){auto&a=api();if(port.storageWork_.opened)assert(a.io->close(a.io->context));assert(a.unbind(a.context)==0);assert(a.release(a.context)==0);}
};
static void job(Fixture&f,Gate&g,unsigned char op){auto&a=f.api();uint64_t token=0;g.request=&op;g.bytes=1;g.submitCredit=true;assert(a.submit(a.context,&op,1,&token)==0&&token==g.token);uint64_t serial=91;assert(f.port.nativeStorageSnapshot(f.port.nativeStorageLease_,&serial)==Port::NativeStorageState::Busy&&!serial);assert(!f.port.nativeStorageDetach(f.port.nativeStorageLease_));
 worker=true;assert(f.mailbox.workerStep());worker=false;unsigned char out[512];uint32_t n=0;int32_t status=99;
 assert(a.collect(a.context,token,out,sizeof out,&n,&status)==RISC_STORAGE_WORK_CONTEXT);g.collectCredit=true;assert(a.collect(a.context,token,out,sizeof out,&n,&status)==0);assert(status==(op==3&&!closeOkay?-1:0));}
// Complete discovery prefixes; this table is used only to test native suffix
// admission. Actual provider behavior is covered by the source-pinned composition.
static risc_storage_volume_api_v1_admission admission(){
 risc_storage_volume_api_v1_admission a{};auto&v=*reinterpret_cast<risc_storage_volume_api_v1*>(&a);
 v.api_version=1;v.struct_size=sizeof(a);
 auto&f=a.base.base.base.fs;f.state.state_tag=RISC_STORAGE_STATE_TAG;f.state.state_version=1;f.state.observe=[](void*)->int32_t{return RISC_STORAGE_STATE_READY;};
 f.fs_tag=RISC_STORAGE_FS_TAG;f.fs_version=1;
 f.dir_tell=[](void*,risc_storage_dir_t,uint64_t*){return true;};f.dir_seek=[](void*,risc_storage_dir_t,uint64_t){return true;};
 f.metadata=[](void*,const char*,risc_storage_metadata_v1*)->int32_t{return 0;};f.file_truncate=[](void*,risc_storage_file_t,uint64_t){return true;};
 f.replace_file=[](void*,const char*,const char*){return true;};f.recover_replace=[](void*,const char*){return true;};
 auto&async=a.base.base.base;async.async_tag=RISC_STORAGE_ASYNC_TAG;async.async_version=3;
 async.begin=[](void*,uint64_t*)->int32_t{return 0;};async.end=[](void*,uint64_t)->int32_t{return 0;};
 async.submit=[](void*,uint64_t,const risc_storage_async_request_v1*,uint64_t*)->int32_t{return 0;};async.collect=[](void*,uint64_t,uint64_t,risc_storage_async_result_v1*)->int32_t{return 0;};
 a.base.base.package_tag=RISC_STORAGE_PACKAGE_TAG;a.base.base.package_version=1;a.base.base.submit_package=[](void*,uint64_t,const risc_storage_package_request_v1*,uint64_t*)->int32_t{return 0;};
 a.base.download_tag=RISC_STORAGE_DOWNLOAD_TAG;a.base.download_version=1;
 a.admission_tag=RISC_STORAGE_SESSION_ADMISSION_TAG;a.admission_version=1;a.begin_checked=[](void*,uint64_t*)->int32_t{return 0;};return a;
}
// Adversarial native callback: another owner action occurs between attachment
// admission checks. It must neither replace custody nor evade later exclusions.
struct AttachCallback {
 Fixture*fixture;Gate*other;const risc_storage_volume_api_v1*volume;unsigned mode,calls=0;
 uint64_t lock=0,job=0;
};
static AttachCallback*attachCallback;
static bool duringUsbIdle(){
 auto&s=*attachCallback;auto&f=*s.fixture;++s.calls;
 usbIdleHook=nullptr; // Nested admission gets an idle result without recursion.
 if(s.mode==0){
  uint64_t lease=71,scope=72;
  assert(!f.port.nativeStorageAttachCheckedWarm(5,s.volume,s.other->hooks(),&lease,&scope)&&!lease&&!scope);
  lease=71;scope=72;
  assert(!f.port.nativeStorageAttach(5,s.other->hooks(),&lease,&scope)&&!lease&&!scope);
  assert(!f.port.nativeStorageAttached());
 }
 if(s.mode==1)f.port.usb_.token=93;
 if(s.mode==2){assert(Port::syncCreate(&f.port.syncs_[0],&s.lock));assert(Port::syncTryLock(&f.port.syncs_[0],s.lock));}
 if(s.mode==3){const unsigned char op=4;assert(f.api().submit(f.api().context,&op,1,&s.job)==0);}
 return s.mode!=4;
}
static void callbackAdmissionTests(){
 for(unsigned mode=0;mode<5;++mode){
  Fixture f;f.fresh();Gate outer,other;auto api=admission();auto*v=reinterpret_cast<risc_storage_volume_api_v1*>(&api);
  AttachCallback callback{&f,&other,v,mode};attachCallback=&callback;usbIdleHook=duringUsbIdle;
  const unsigned before=ioCalls;uint64_t lease=71,scope=72;
  const bool attached=f.port.nativeStorageAttachCheckedWarm(5,v,outer.hooks(),&lease,&scope);
  assert(callback.calls==1&&!usbIdleHook&&ioCalls==before);
  if(mode==0){
   assert(attached&&lease&&scope&&f.port.nativeStorageHeld(lease));
   assert(f.port.nativeStorageCustody_.context==&outer);
  }else{
   assert(!attached&&!lease&&!scope&&!f.port.nativeStorageAttached());
   assert(!f.port.nativeStorageCustody_.context);
   f.port.usb_.token=0;
   if(callback.lock){assert(Port::syncUnlock(&f.port.syncs_[0],callback.lock));assert(Port::syncDestroy(&f.port.syncs_[0],callback.lock));}
   if(callback.job)assert(f.api().cancel(f.api().context,callback.job)==RISC_STORAGE_WORK_CANCELLED);
   // Every refusal clears the transient guard and permits a later clean try.
   assert(f.port.nativeStorageAttachCheckedWarm(5,v,outer.hooks(),&lease,&scope));
  }
  outer.released=true;assert(f.port.nativeStorageDetach(lease));f.cleanup();attachCallback=nullptr;
 }
}
int main(){
 callbackAdmissionTests();
 {Fixture f;f.fresh();Gate g;uint64_t lease=0,scope=0;assert(f.port.nativeStorageAttach(5,g.hooks(),&lease,&scope));g.scope=scope;assert(lease&&scope&&f.port.nativeStorageHeld(lease)&&f.port.nativeStorageFresh(lease));
  const auto count=ioCalls;auto&a=f.api();assert(!a.io->power(a.io->context,false));assert(!a.io->close(a.io->context));assert(ioCalls==count);
  assert(!Port::syncOwner(&f.port.syncs_[0]));assert(Port::syncOwner(&f.port.syncs_[1]));g.syncCredit=true;assert(Port::syncOwner(&f.port.syncs_[0]));assert(!Port::syncOwner(&f.port.syncs_[0]));
  assert(!f.port.providerStorageSafe()&&!f.port.appExitSafe()&&!f.port.restartResourcesSafe()&&!f.port.quiescent());
  uint64_t lock=91;assert(!Port::syncCreate(&f.port.syncs_[0],&lock)&&!lock);assert(!Port::syncTryLock(&f.port.syncs_[0],91));assert(!Port::syncUnlock(&f.port.syncs_[0],91));assert(!Port::syncDestroy(&f.port.syncs_[0],91));
  risc_light_sleep_result_v1 sleep{};sleep.struct_size=sizeof(sleep);assert(Port::gpioLightSleep(&f.port.gpios_[0],99,true,&sleep)==RISC_LIGHT_SLEEP_BUSY);assert(Port::gpioDeepSleep(&f.port.gpios_[0],99,true)==RISC_DEEP_SLEEP_BUSY);assert(Port::gpioDeepSleepHold(&f.port.gpios_[0],99,true)==RISC_DEEP_SLEEP_BUSY);
  assert(Port::gpioLightSleepFor(&f.port.gpios_[0],99,true,1,&sleep)==RISC_LIGHT_SLEEP_BUSY);assert(Port::gpioDeepSleepFor(&f.port.gpios_[0],99,true,1)==RISC_DEEP_SLEEP_BUSY);assert(Port::gpioLightSleepSet(&f.port.gpios_[0],99,true,1,&sleep)==RISC_LIGHT_SLEEP_BUSY);assert(Port::gpioDeepSleepSet(&f.port.gpios_[0],99,true,1)==RISC_DEEP_SLEEP_BUSY);
  uint64_t usb=0;assert(!Port::usbPhyClaim(&f.port.usb_,&usb)&&!usb);assert(Port::gpioWakeSource(&f.port.gpios_[0],99,true,0)==RISC_LIGHT_SLEEP_BUSY);
  assert(a.unbind(a.context)==RISC_STORAGE_WORK_CONTEXT&&a.release(a.context)==RISC_STORAGE_WORK_BUSY);assert(!f.port.nativeStorageDetach(lease));
  unsigned char wrong=4;uint64_t token=3;assert(a.submit(a.context,&wrong,1,&token)==RISC_STORAGE_WORK_CONTEXT&&!token);
  uint64_t serial=1;assert(f.port.nativeStorageSnapshot(lease,&serial)==Port::NativeStorageState::Unavailable&&!serial);
  job(f,g,1);assert(!f.port.nativeStorageFresh(lease));assert(f.port.nativeStorageSnapshot(lease,&serial)==Port::NativeStorageState::Healthy&&serial==2);const auto mounted=serial;
  job(f,g,2);assert(f.port.nativeStorageSnapshot(lease,&serial)==Port::NativeStorageState::Healthy&&serial==mounted);
  unsigned char cancelled=2;uint64_t cancelledToken=0;g.request=&cancelled;g.bytes=1;g.submitCredit=true;assert(a.submit(a.context,&cancelled,1,&cancelledToken)==0);
  assert(a.cancel(a.context,cancelledToken)==RISC_STORAGE_WORK_CONTEXT);g.cancelCredit=true;assert(a.cancel(a.context,cancelledToken+1)==RISC_STORAGE_WORK_CONTEXT);assert(a.cancel(a.context,cancelledToken)==RISC_STORAGE_WORK_CANCELLED);assert(f.port.nativeStorageIdle(lease));
  g.collectCredit=true;uint32_t collected=0;int32_t result=0;assert(a.collect(a.context,cancelledToken+1,nullptr,0,&collected,&result)==RISC_STORAGE_WORK_CONTEXT);g.collectCredit=false;
  assert(f.port.nativeStorageSnapshot(lease,&serial)==Port::NativeStorageState::Healthy&&serial==mounted);
  isr=true;assert(!f.port.nativeStorageOwner()&&!f.port.nativeStorageHeld(lease)&&!Port::syncOwner(&f.port.syncs_[0]));isr=false;
  otherTask=true;assert(!f.port.nativeStorageDetach(lease));otherTask=false;worker=true;assert(!f.port.nativeStorageHeld(lease));worker=false;
  job(f,g,3);assert(f.port.nativeStorageSnapshot(lease,&serial)==Port::NativeStorageState::Unavailable&&serial==mounted+1);
  g.released=true;assert(!f.port.nativeStorageDetach(lease+1));assert(f.port.nativeStorageDetach(lease));assert(!f.port.nativeStorageHeld(lease));assert(f.port.providerStorageSafe());f.cleanup();
  f.fresh();uint64_t laterLease=0,laterScope=0;assert(f.port.nativeStorageAttach(5,g.hooks(),&laterLease,&laterScope));assert(laterScope>scope&&laterLease!=lease);assert(f.port.nativeStorageSnapshot(laterLease,&serial)==Port::NativeStorageState::Unavailable&&serial==mounted+1);assert(f.port.nativeStorageDetach(laterLease));f.cleanup();
 }
 for(unsigned bad=0;bad<9;++bad){Fixture f;f.fresh();Gate g;auto&a=f.api();
  if(bad==0)assert(Port::syncOwner(&f.port.syncs_[0])); // foreign async_begin, no worker operation
  if(bad==1){assert(a.unbind(a.context)==0);assert(a.bind(a.context,Fixture::execute,&a)==0);}
  if(bad==2)assert(a.io->power(a.io->context,false));
  if(bad==3)f.port.storageWork_.submitted=true;
  if(bad==4)f.port.storageWork_.fault=true;
  if(bad==5)f.port.syncs_[0].locks[0].held=true;
  if(bad==6)f.port.usb_.token=5;
  if(bad==7)otherTask=true;
  uint64_t lease=77,scope=88;assert(!f.port.nativeStorageAttach(bad==8?9:5,g.hooks(),&lease,&scope)&&!lease&&!scope);
  otherTask=false;f.port.storageWork_.fault=false;f.port.syncs_[0].locks[0].held=false;f.port.usb_.token=0;f.cleanup();
 }
 {Fixture f;f.fresh();Gate g;uint64_t lease=0,scope=0;assert(f.port.nativeStorageAttach(5,g.hooks(),&lease,&scope));g.scope=scope;job(f,g,1);closeOkay=false;job(f,g,3);uint64_t serial=0;assert(f.port.nativeStorageSnapshot(lease,&serial)==Port::NativeStorageState::Retained);g.released=true;assert(!f.port.nativeStorageDetach(lease));closeOkay=true;job(f,g,3);assert(f.port.nativeStorageDetach(lease));f.cleanup();}
 {Fixture f;f.fresh();f.port.storageTransportSerial_=UINT64_MAX;assert(!f.api().io->power(f.api().io->context,false));assert(f.port.storageWork_.fault);f.port.storageWork_.fault=false;f.cleanup();}
 // Completed ordinary work does not become permanent native custody. Fresh
 // still refuses it; only the explicitly versioned warm path can attach.
 {Fixture f;f.fresh();Gate g;auto admissionApi=admission();auto*v=reinterpret_cast<risc_storage_volume_api_v1*>(&admissionApi);auto&a=f.api();
  assert(risc_storage_volume_session_admission(v));assert(Port::syncOwner(&f.port.syncs_[0]));
  unsigned char op=1;uint64_t token=0;assert(a.submit(a.context,&op,1,&token)==0);
  worker=true;assert(f.mailbox.workerStep());worker=false;uint32_t n=0;int32_t status=99;assert(a.collect(a.context,token,nullptr,0,&n,&status)==0&&status==0);
  uint64_t lease=0,scope=0;assert(!f.port.nativeStorageAttach(5,g.hooks(),&lease,&scope));
  for(unsigned i=0;i<3;++i){assert(f.port.nativeStorageAttachCheckedWarm(5,v,g.hooks(),&lease,&scope));g.scope=scope;
   assert(!f.port.nativeStorageFresh(lease)&&!Port::syncOwner(&f.port.syncs_[0]));uint64_t epoch=0;assert(f.port.nativeStorageSnapshot(lease,&epoch)==Port::NativeStorageState::Healthy&&epoch);
   g.released=true;assert(f.port.nativeStorageDetach(lease));assert(Port::syncOwner(&f.port.syncs_[0])&&f.port.providerStorageSafe());}
  f.cleanup();
 }
 for(unsigned bad=0;bad<12;++bad){Fixture f;f.fresh();Gate g;auto api=admission();auto*v=reinterpret_cast<risc_storage_volume_api_v1*>(&api);uint64_t token=0;unsigned char op=4;
  if(bad==0)api.admission_tag=0;
  if(bad==1)api.admission_version=2;
  if(bad==2)api.begin_checked=nullptr;
  if(bad==3)v->struct_size=sizeof(api)-1;
  if(bad==4)f.port.syncs_[0].locks[0].held=true;
  if(bad==5)assert(f.api().submit(f.api().context,&op,1,&token)==0);
  if(bad==7)f.port.usb_.token=1;
  if(bad==8)f.port.storageWork_.fault=true;
  if(bad==9)otherTask=true;
  if(bad==10)f.port.syncs_[0].ownerCalls=UINT64_MAX;
  if(bad==11)assert(f.api().unbind(f.api().context)==0);
  uint64_t lease=77,scope=88;assert(!f.port.nativeStorageAttachCheckedWarm(bad==6?9:5,v,g.hooks(),&lease,&scope)&&!lease&&!scope);
  otherTask=false;f.port.syncs_[0].locks[0].held=false;f.port.usb_.token=0;f.port.storageWork_.fault=false;
  if(token)assert(f.api().cancel(f.api().context,token)==RISC_STORAGE_WORK_CANCELLED);
  if(bad==11)assert(f.api().bind(f.api().context,Fixture::execute,&f.api())==0);
  f.cleanup();
 }
 puts("native Port custody: fresh bind, idle foreign session refusal, exact gates, closed lifetime, task/ISR, transport serials, checked-warm suffix/exclusions/reentry and retained cleanup PASS");
}
