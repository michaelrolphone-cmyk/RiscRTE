// One native SD controller and one selected device scope. Owner reserves pins;
// worker mutates only its storage state and hardware, never Port token/pin maps.
bool Port::workIoAllowed(void*ctx){
 auto&g=*static_cast<Gpio*>(ctx);auto&p=*g.port;
 if(!p.hw_.storageWork)return false;
 if(p.hw_.storageWork->isWorker())return p.storageWork_.gpio==&g;
 return p.available()&&!p.sleepRetained_&&p.storageWork_.gpio==&g&&!p.storageWorkBusy();
}
int32_t Port::workClaim(void*ctx,const risc_storage_work_scope_v1*scope){
 auto&g=*static_cast<Gpio*>(ctx);auto&p=*g.port;auto&w=p.storageWork_;
 if(!scope||!p.available()||p.sleepRetained_||p.transferring_||!p.hw_.storageWork||w.gpio||p.sdmmc_.token)return RISC_STORAGE_WORK_BUSY;
 if(scope->max_hz!=RISC_SDMMC_MAX_HZ||!p.hw_.sdmmcOpen||!p.hw_.sdmmcRead||!p.hw_.sdmmcWrite||!p.hw_.sdmmcSync||!p.hw_.sdmmcClose)return RISC_STORAGE_WORK_INVALID;
 const uint8_t pins[]={scope->power,scope->clk,scope->cmd,scope->dat0};
 for(unsigned i=0;i<4;++i){
  if(!pinBit(pins[i])||!(g.output&pinBit(pins[i]))||p.pins_[pins[i]].owner)return RISC_STORAGE_WORK_INVALID;
  for(unsigned j=0;j<i;++j)if(pins[i]==pins[j])return RISC_STORAGE_WORK_INVALID;
 }
 if((g.input&(pinBit(scope->cmd)|pinBit(scope->dat0)))!=(pinBit(scope->cmd)|pinBit(scope->dat0))||
    (g.pullup&(pinBit(scope->cmd)|pinBit(scope->dat0)))!=(pinBit(scope->cmd)|pinBit(scope->dat0)))return RISC_STORAGE_WORK_INVALID;
 if(!p.hw_.storageWork->prepare())return RISC_STORAGE_WORK_BUSY;
 for(uint8_t pin:pins)if(!p.reserve(pin,&w))return RISC_STORAGE_WORK_RETAINED; // prechecked, owner serialized
 w.gpio=&g;w.scope=*scope;w.powerClaimed=true;
 if(!p.hw_.gpioOpen(scope->power,true,scope->initial_power_level,false)){w.fault=true;return RISC_STORAGE_WORK_RETAINED;}
 return RISC_STORAGE_WORK_OK;
}
int32_t Port::workRelease(void*ctx){
 auto&g=*static_cast<Gpio*>(ctx);auto&p=*g.port;auto&w=p.storageWork_;
 if(!p.hw_.owner()||!p.hw_.storageWork||w.gpio!=&g)return RISC_STORAGE_WORK_CONTEXT;
 if(p.storageWorkBusy()||p.hw_.storageWork->mailbox->bound())return RISC_STORAGE_WORK_BUSY;
 if(w.opened||w.fault)return RISC_STORAGE_WORK_RETAINED;
 if(w.powerClaimed&&!p.hw_.gpioClose(w.scope.power))return RISC_STORAGE_WORK_RETAINED;
 const uint8_t pins[]={w.scope.power,w.scope.clk,w.scope.cmd,w.scope.dat0};
 for(uint8_t pin:pins)p.unreserve(pin,&w);
 auto io=w.io;auto api=w.api;w={};w.io=io;w.api=api;return RISC_STORAGE_WORK_OK;
}
int32_t Port::workBind(void*ctx,risc_storage_work_execute_v1 fn,void*provider){
 auto&g=*static_cast<Gpio*>(ctx);auto&p=*g.port;
 if(!p.available()||p.storageWork_.gpio!=&g)return RISC_STORAGE_WORK_CONTEXT;
 return p.hw_.storageWork->mailbox->bind(fn,provider);
}
int32_t Port::workSubmit(void*ctx,const void*in,uint32_t n,uint64_t*out){
 if(out)*out=0;
 auto&g=*static_cast<Gpio*>(ctx);auto&p=*g.port;
 if(!p.available()||p.sleepRetained_||p.storageWork_.gpio!=&g)return RISC_STORAGE_WORK_CONTEXT;
 return p.hw_.storageWork->mailbox->submit(in,n,out);
}
int32_t Port::workCollect(void*ctx,uint64_t token,void*out,uint32_t cap,uint32_t*n,int32_t*status){
 if(n)*n=0;
 auto&g=*static_cast<Gpio*>(ctx);auto&p=*g.port;
 if(!p.hw_.owner()||p.storageWork_.gpio!=&g)return RISC_STORAGE_WORK_CONTEXT;
 return p.hw_.storageWork->mailbox->collect(token,out,cap,n,status);
}
int32_t Port::workCancel(void*ctx,uint64_t token){
 auto&g=*static_cast<Gpio*>(ctx);auto&p=*g.port;
 if(!p.hw_.owner()||p.storageWork_.gpio!=&g)return RISC_STORAGE_WORK_CONTEXT;
 return p.hw_.storageWork->mailbox->cancel(token);
}
int32_t Port::workUnbind(void*ctx){
 auto&g=*static_cast<Gpio*>(ctx);auto&p=*g.port;
 if(!p.hw_.owner()||p.storageWork_.gpio!=&g)return RISC_STORAGE_WORK_CONTEXT;
 return p.hw_.storageWork->mailbox->unbind();
}
bool Port::configureStorageWork(Gpio&g){
 if(!hw_.storageWork||!hw_.storageWork->prepare||!hw_.storageWork->isWorker||!hw_.storageWork->delayMs||storageWork_.io.context)return false;
 if(!hw_.storageWork->prepare()||!hw_.storageWork->mailbox)return false;
 storageWork_.io={&g,
  [](void*c){auto&g=*static_cast<Gpio*>(c);auto&p=*g.port;return p.hw_.storageWork&&p.hw_.storageWork->isWorker()&&p.storageWork_.gpio==&g;},
  [](void*c)->uint64_t{auto&g=*static_cast<Gpio*>(c);return workIoAllowed(c)&&g.port->hw_.now?g.port->hw_.now():0;},
  [](void*c,bool level){auto&g=*static_cast<Gpio*>(c);auto&p=*g.port;if(!workIoAllowed(c))return false;const bool ok=p.hw_.gpioWrite(p.storageWork_.scope.power,level);if(!ok)p.storageWork_.fault=true;return ok;},
  [](void*c,uint32_t ms){auto&g=*static_cast<Gpio*>(c);if(workIoAllowed(c)&&ms<=5000)g.port->hw_.storageWork->delayMs(ms);},
  [](void*c,risc_sdmmc_card_info_v1*out){if(out)*out={};auto&g=*static_cast<Gpio*>(c);auto&p=*g.port;auto&w=p.storageWork_;
   if(!out||!workIoAllowed(c)||w.opened||w.fault)return false;
   w.opened=true;const bool ok=p.hw_.sdmmcOpen(w.scope.clk,w.scope.cmd,w.scope.dat0,w.scope.max_hz,out);
   if(!ok||out->struct_size!=sizeof(*out)||out->reserved||out->sector_size!=512||!out->sector_count||out->sector_count>uint64_t(UINT32_MAX)+1||!out->clock_hz||out->clock_hz>w.scope.max_hz){w.fault=true;return false;}
   w.sectors=out->sector_count;return true;},
  [](void*c,uint64_t lba,uint32_t n,void*out){auto&g=*static_cast<Gpio*>(c);auto&p=*g.port;auto&w=p.storageWork_;
   if(!workIoAllowed(c)||!w.opened||w.fault||!out||!n||n>RISC_SDMMC_MAX_SECTORS||lba>=w.sectors||n>w.sectors-lba||reinterpret_cast<uintptr_t>(out)>UINTPTR_MAX-size_t(n)*512)return false;
   const bool ok=p.hw_.sdmmcRead(lba,n,out);if(!ok)w.fault=true;
   return ok;},
  [](void*c,uint64_t lba,uint32_t n,const void*in){auto&g=*static_cast<Gpio*>(c);auto&p=*g.port;auto&w=p.storageWork_;
   if(!workIoAllowed(c)||!w.opened||w.fault||!in||!n||n>RISC_SDMMC_MAX_SECTORS||lba>=w.sectors||n>w.sectors-lba||reinterpret_cast<uintptr_t>(in)>UINTPTR_MAX-size_t(n)*512)return false;
   const bool ok=p.hw_.sdmmcWrite(lba,n,in);if(!ok)w.fault=true;
   return ok;},
  [](void*c){auto&g=*static_cast<Gpio*>(c);auto&p=*g.port;auto&w=p.storageWork_;
   if(!workIoAllowed(c)||!w.opened||w.fault)return false;
   const bool ok=p.hw_.sdmmcSync();if(!ok)w.fault=true;
   return ok;},
  [](void*c){auto&g=*static_cast<Gpio*>(c);auto&p=*g.port;auto&w=p.storageWork_;
   if(!workIoAllowed(c))return false;
   if(!w.opened)return !w.fault;
   if(!p.hw_.sdmmcClose()){w.fault=true;return false;}
   w.opened=w.fault=false;return true;}};
 storageWork_.api={1,sizeof(storageWork_.api),&g,workBind,workSubmit,workCollect,workCancel,workUnbind,workClaim,workRelease,&storageWork_.io};return true;
}
