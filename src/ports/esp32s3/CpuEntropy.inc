bool Port::entropyIdle(uint64_t owner) const {
 const auto* b=hw_.entropy;return !b || (b->idle && b->idle(b->context,owner));
}
bool Port::entropySafe() const {
 const auto* b=hw_.entropy;return !b || (b->safe && b->safe(b->context));
}
int32_t Port::entropyFill(void* context,uint64_t owner,void* output,uint32_t size){
 auto& p=*static_cast<Port*>(context);const auto* b=p.hw_.entropy;
 if(!p.available() || p.sleepRetained_ || !b)return RISC_ENTROPY_CONTEXT;
 if(p.transferring_)return RISC_ENTROPY_BUSY;
 if(!p.providerStorageSafe())return RISC_ENTROPY_RETAINED;
 if(!owner || !output || !size || size>RISC_ENTROPY_BYTES_MAX)return RISC_ENTROPY_INVALID;
 struct Scratch {
  uint8_t bytes[RISC_ENTROPY_BYTES_MAX]{};
  ~Scratch(){volatile uint8_t* p=bytes;for(uint32_t i=0;i<sizeof(bytes);++i)p[i]=0;}
 } scratch;
 p.transferring_=true;
 auto result=b->fill(b->context,owner,scratch.bytes,size);
 p.transferring_=false;
 if(!p.available() || p.sleepRetained_ || !p.entropySafe())result=RISC_ENTROPY_RETAINED;
 if(result==RISC_ENTROPY_OK)std::memcpy(output,scratch.bytes,size);
 else if(result!=RISC_ENTROPY_BUSY && result!=RISC_ENTROPY_UNAVAILABLE && result!=RISC_ENTROPY_INVALID){
  p.poisoned_=true;result=RISC_ENTROPY_RETAINED;
 }
 return result;
}
