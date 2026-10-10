bool Port::tcpIdle(uint64_t owner) const {
 const auto* b=hw_.tcpListener;return !b || (b->idle && b->idle(b->context,owner));
}
bool Port::tcpSafe() const {
 const auto* b=hw_.tcpListener;return !b || (b->safe && b->safe(b->context));
}
int32_t Port::tcpListen(void* context,uint64_t owner,const risc_tcp_listen_v1* request,uint64_t* out){
 if(out)*out=0;
 auto& p=*static_cast<Port*>(context);const auto* b=p.hw_.tcpListener;
 if(!p.available() || p.sleepRetained_ || p.transferring_ || !b)return RISC_TCP_CONTEXT;
 if(!p.providerStorageSafe())return RISC_TCP_RETAINED;
 p.transferring_=true;const auto result=b->listen(b->context,owner,request,out);p.transferring_=false;return result;
}
int32_t Port::tcpAccept(void* context,uint64_t owner,uint64_t handle,uint64_t* out){
 if(out)*out=0;
 auto& p=*static_cast<Port*>(context);const auto* b=p.hw_.tcpListener;
 if(!p.available() || p.sleepRetained_ || p.transferring_ || !b)return RISC_TCP_CONTEXT;
 if(!p.providerStorageSafe())return RISC_TCP_RETAINED;
 p.transferring_=true;const auto result=b->accept(b->context,owner,handle,out);p.transferring_=false;return result;
}
int32_t Port::tcpRead(void* context,uint64_t owner,uint64_t handle,void* bytes,uint32_t size,uint32_t* count){
 if(count)*count=0;
 auto& p=*static_cast<Port*>(context);const auto* b=p.hw_.tcpListener;
 if(!p.available() || p.sleepRetained_ || p.transferring_ || !b)return RISC_TCP_CONTEXT;
 if(!p.providerStorageSafe())return RISC_TCP_RETAINED;
 p.transferring_=true;const auto result=b->read(b->context,owner,handle,bytes,size,count);p.transferring_=false;return result;
}
int32_t Port::tcpWrite(void* context,uint64_t owner,uint64_t handle,const void* bytes,uint32_t size,uint32_t* count){
 if(count)*count=0;
 auto& p=*static_cast<Port*>(context);const auto* b=p.hw_.tcpListener;
 if(!p.available() || p.sleepRetained_ || p.transferring_ || !b)return RISC_TCP_CONTEXT;
 if(!p.providerStorageSafe())return RISC_TCP_RETAINED;
 p.transferring_=true;const auto result=b->write(b->context,owner,handle,bytes,size,count);p.transferring_=false;return result;
}
int32_t Port::tcpClose(void* context,uint64_t owner,uint64_t handle){
 auto& p=*static_cast<Port*>(context);const auto* b=p.hw_.tcpListener;
 if(!p.hw_.owner || !p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || p.transferring_ || !b)return RISC_TCP_CONTEXT;
 if(!p.tcpSafe())return RISC_TCP_RETAINED;
 p.transferring_=true;const auto result=b->close(b->context,owner,handle);p.transferring_=false;return result;
}
