#include "bootstrap/Runtime.h"
#include "runtime/storage/StorageWorker.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace RiscCpu;
static bool worker=false,closeOkay=true;
static unsigned transfers,powerWrites;
static RiscStorage::Worker mailbox;
static bool owner(){return !worker;}
static StorageWorkBackend backend{&mailbox,[](){return true;},[](){return worker;},[](uint32_t){}};
int main(){
 Hardware h{};h.owner=owner;h.storageWork=&backend;
 h.gpioOpen=[](uint8_t pin,bool output,bool initial,bool pullup){assert(!worker&&pin==5&&output&&initial&&!pullup);return true;};
 h.gpioWrite=[](uint8_t pin,bool){assert(pin==5);++powerWrites;return true;};h.gpioClose=[](uint8_t pin){assert(pin==5);return true;};
 h.sdmmcOpen=[](uint8_t c,uint8_t m,uint8_t d,uint32_t hz,risc_sdmmc_card_info_v1*out){assert(worker&&c==41&&m==42&&d==40&&hz==20000000);*out={sizeof(*out),512,64,hz,0};return true;};
 h.sdmmcRead=[](uint64_t,uint32_t n,void*out){assert(worker);++transfers;memset(out,0x5a,n*512);return true;};
 h.sdmmcWrite=[](uint64_t,uint32_t,const void*){assert(worker);return true;};h.sdmmcSync=[](){return true;};h.sdmmcClose=[](){return closeOkay;};
 RiscBoot::Runtime rt({});RiscBoot::Board::Device d{};strcpy(d.type,"gpio.bank");d.hardware.instance_id=5;
 d.config.gpio={sizeof(risc_hw_gpio_bank_v1),4,1,1,0,{5,41,42,40},0,0,0};
 Port p(h);auto&g=p.gpios_[0];assert(p.gpioScope(rt,d,g));assert(p.configureStorageWork(g));
 auto&a=g.work;risc_storage_work_scope_v1 scope{5,41,42,40,20000000,true};
 auto invalid=scope;invalid.cmd=39;assert(a.claim(a.context,&invalid)==RISC_STORAGE_WORK_INVALID);
 assert(a.claim(a.context,&scope)==0);assert(p.pins_[5].owner&&p.pins_[41].owner&&!p.transferring_);
 uint64_t pin=0;assert(!g.api.base.claim(&g,41,true,false,false,&pin));
 assert(a.bind(a.context,[](void*c,const void*in,uint32_t n,void*out,uint32_t,uint32_t*size)->int32_t{
   auto&a=*static_cast<const risc_storage_worker_v1*>(c);auto&g=*static_cast<Port::Gpio*>(a.context);
   assert(!g.port->hw_.owner());uint64_t token=0;assert(!g.api.base.claim(&g,5,true,true,false,&token));
   assert(a.submit(a.context,in,n,&token)==RISC_STORAGE_WORK_CONTEXT); // no recursive/native-owner escalation
   assert(a.io->power(a.io->context,false));risc_sdmmc_card_info_v1 card{};assert(a.io->open(a.io->context,&card));
   assert(a.io->read(a.io->context,0,1,out));*size=512;return 19;
 },&a)==0);
 uint64_t token=0;unsigned char request=1,output[512];assert(a.submit(a.context,&request,1,&token)==0);
 assert(!a.io->power(a.io->context,true)); // owner cannot race pending worker I/O
 assert(!p.appExitSafe()&&!p.quiescent()&&!p.transferring_);
 assert(a.release(a.context)==RISC_STORAGE_WORK_BUSY&&a.unbind(a.context)==RISC_STORAGE_WORK_BUSY);
 worker=true;assert(mailbox.workerStep());worker=false;
 uint32_t size=0;int32_t status=0;assert(a.collect(a.context,token,output,sizeof(output),&size,&status)==0&&status==19&&size==512&&output[0]==0x5a);
 assert(transfers==1&&powerWrites==1&&p.appExitSafe()&&!p.transferring_);
 assert(a.unbind(a.context)==0);assert(a.release(a.context)==RISC_STORAGE_WORK_RETAINED);
 closeOkay=false;assert(!a.io->close(a.io->context));assert(!p.providerStorageSafe());assert(a.release(a.context)==RISC_STORAGE_WORK_RETAINED);
 closeOkay=true;assert(a.io->close(a.io->context));assert(a.release(a.context)==0&&p.quiescent());
 assert(!a.io->power(a.io->context,false));
 puts("storage worker scope: exact pins, worker-only transport, owner race rejection, no global transfer fence, retained cleanup PASS");
}
