bool Runtime::registerEntropy(const EntropyBackend* backend){
 if((attempted_ && !registrationOpen_) || !port_.owner() || entropyBackend_ || !backend ||
    !backend->fill || !backend->idle || !backend->safe)return false;
 entropyBackend_=backend;
 providerEntropyTable_={1,sizeof(providerEntropyTable_),nullptr,entropyFill};
 return registerPlatform(RISC_ENTROPY_CAPABILITY,1,Scope::Global,0,&providerEntropyTable_);
}
int32_t Runtime::entropyFill(void* context,void* output,uint32_t size){
 auto* r=currentRuntime;
 if(!r || !context || !r->port_.owner() || r->retained_ || !r->entropyBackend_)return RISC_ENTROPY_CONTEXT;
 ProviderStorage* matched=nullptr;
 for(auto& storage:r->providerStorage_)
  if(storage.owner==r && storage.live && storage.needsEntropy && storage.entropy.context==context)matched=&storage;
 if(!matched)return RISC_ENTROPY_CONTEXT;
 if(r->entropyBusy_)return RISC_ENTROPY_BUSY;
 if(!r->providerStorageSafe() || !r->entropyBackend_->safe(r->entropyBackend_->context)){
  r->entropyRetained_=true;r->revokeProviders();return RISC_ENTROPY_RETAINED;
 }
 if(!output || !size || size>RISC_ENTROPY_BYTES_MAX)return RISC_ENTROPY_INVALID;
 struct Scratch {
  uint8_t bytes[RISC_ENTROPY_BYTES_MAX]{};
  ~Scratch(){volatile uint8_t* p=bytes;for(uint32_t i=0;i<sizeof(bytes);++i)p[i]=0;}
 } scratch;
 r->entropyBusy_=true;
 auto result=r->entropyBackend_->fill(r->entropyBackend_->context,reinterpret_cast<uintptr_t>(context),scratch.bytes,size);
 r->entropyBusy_=false;
 if(!r->port_.owner() || !matched->live || matched->entropy.context!=context ||
    !r->providerStorageSafe() || !r->entropyBackend_->safe(r->entropyBackend_->context))result=RISC_ENTROPY_RETAINED;
 if(result==RISC_ENTROPY_OK)std::memcpy(output,scratch.bytes,size);
 else if(result!=RISC_ENTROPY_BUSY && result!=RISC_ENTROPY_UNAVAILABLE && result!=RISC_ENTROPY_INVALID){
  r->entropyRetained_=true;r->revokeProviders();result=RISC_ENTROPY_RETAINED;
 }
 return result;
}
