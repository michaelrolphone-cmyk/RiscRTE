bool Runtime::registerTcpListener(const TcpListenerBackend* backend){
 if((attempted_ && !registrationOpen_) || !port_.owner() || tcpBackend_ || !backend ||
    !backend->listen || !backend->accept || !backend->read || !backend->write ||
    !backend->close || !backend->idle || !backend->safe)return false;
 tcpBackend_=backend;
 providerTcpTable_={1,sizeof(providerTcpTable_),nullptr,tcpListen,tcpAccept,tcpRead,tcpWrite,tcpClose};
 return registerPlatform(RISC_TCP_LISTENER_CAPABILITY,1,Scope::Global,0,&providerTcpTable_);
}
bool Runtime::providerResourcesSafe(void* context){
 const auto* storage=static_cast<const ProviderStorage*>(context);
 if(!providerFileSafe(context) || !storage || !storage->owner)return false;
 const auto* r=storage->owner;
 return !r->entropyRetained_ && (!r->entropyBackend_ || r->entropyBackend_->safe(r->entropyBackend_->context)) && !r->tcpRetained_ && (!r->tcpBackend_ || r->tcpBackend_->safe(r->tcpBackend_->context));
}
Runtime::ProviderStorage* Runtime::tcpContext(void* context,bool closing){
 auto* r=currentRuntime;
 if(!r || !context || !r->port_.owner() || r->retained_ || r->tcpRetained_ || !r->tcpBackend_)return nullptr;
 ProviderStorage* matched=nullptr;
 for(auto& storage:r->providerStorage_)
  if(storage.owner==r && storage.live && storage.needsTcp && storage.tcp.context==context)matched=&storage;
 if(!matched)return nullptr;
 if(!r->tcpBackend_->safe(r->tcpBackend_->context)){r->tcpRetained_=true;return nullptr;}
 // Close is permitted before ordinary provider release, including after the
 // station disappears. Revoked generation tokens never regain cleanup rights.
 if(!closing && !r->providerStorageSafe()){r->revokeProviders();return nullptr;}
 return matched;
}
int32_t Runtime::tcpResult(int32_t result){
 if(result==RISC_TCP_RETAINED){tcpRetained_=true;revokeProviders();}
 return result;
}
int32_t Runtime::tcpListen(void* context,const risc_tcp_listen_v1* request,uint64_t* out){
 if(out)*out=0;
 auto* s=tcpContext(context);if(!s)return RISC_TCP_CONTEXT;
 auto* r=s->owner;const auto* b=r->tcpBackend_;
 return r->tcpResult(b->listen(b->context,reinterpret_cast<uintptr_t>(context),request,out));
}
int32_t Runtime::tcpAccept(void* context,uint64_t handle,uint64_t* out){
 if(out)*out=0;
 auto* s=tcpContext(context);if(!s)return RISC_TCP_CONTEXT;
 auto* r=s->owner;const auto* b=r->tcpBackend_;
 return r->tcpResult(b->accept(b->context,reinterpret_cast<uintptr_t>(context),handle,out));
}
int32_t Runtime::tcpRead(void* context,uint64_t handle,void* bytes,uint32_t size,uint32_t* count){
 if(count)*count=0;
 auto* s=tcpContext(context);if(!s)return RISC_TCP_CONTEXT;
 auto* r=s->owner;const auto* b=r->tcpBackend_;
 return r->tcpResult(b->read(b->context,reinterpret_cast<uintptr_t>(context),handle,bytes,size,count));
}
int32_t Runtime::tcpWrite(void* context,uint64_t handle,const void* bytes,uint32_t size,uint32_t* count){
 if(count)*count=0;
 auto* s=tcpContext(context);if(!s)return RISC_TCP_CONTEXT;
 auto* r=s->owner;const auto* b=r->tcpBackend_;
 return r->tcpResult(b->write(b->context,reinterpret_cast<uintptr_t>(context),handle,bytes,size,count));
}
int32_t Runtime::tcpClose(void* context,uint64_t handle){
 auto* s=tcpContext(context,true);if(!s)return RISC_TCP_CONTEXT;
 auto* r=s->owner;const auto* b=r->tcpBackend_;
 return r->tcpResult(b->close(b->context,reinterpret_cast<uintptr_t>(context),handle));
}
