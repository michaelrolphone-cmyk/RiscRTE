bool Runtime::registerTcpListener(const TcpListenerBackend* backend){
 if((attempted_ && !registrationOpen_) || !port_.owner() || tcpBackend_ || !backend ||
    !backend->listen || !backend->accept || !backend->read || !backend->write ||
    !backend->close || !backend->idle || !backend->safe)return false;
 tcpBackend_=backend;
 providerTcpTable_={1,sizeof(providerTcpTable_),nullptr,tcpListen,tcpAccept,tcpRead,tcpWrite,tcpClose,0,nullptr,nullptr,0,nullptr,nullptr,nullptr};
 if(backend->connect && backend->connected){providerTcpTable_.connect_tag=RISC_TCP_CONNECT_TAG;providerTcpTable_.connect=tcpConnect;providerTcpTable_.connected=tcpConnected;}
 if(backend->datagram_bind && backend->datagram_receive && backend->datagram_send){providerTcpTable_.datagram_tag=RISC_DATAGRAM_TAG;providerTcpTable_.datagram_bind=udpBind;providerTcpTable_.datagram_receive=udpReceive;providerTcpTable_.datagram_send=udpSend;}
 return registerPlatform(RISC_TCP_LISTENER_CAPABILITY,1,Scope::Global,0,&providerTcpTable_);
}
bool Runtime::providerResourcesSafe(void* context){
 const auto* storage=static_cast<const ProviderStorage*>(context);
 if(!providerFileSafe(context) || !storage || !storage->owner)return false;
 const auto* r=storage->owner;
 return !r->metadataCloseRetained() && !r->entropyRetained_ && (!r->entropyBackend_ || r->entropyBackend_->safe(r->entropyBackend_->context)) && !r->tcpRetained_ && (!r->tcpBackend_ || r->tcpBackend_->safe(r->tcpBackend_->context));
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

int32_t Runtime::tcpConnect(void* context,const risc_tcp_listen_v1* request,uint64_t* out){
 if(out)*out=0;
 auto* s=tcpContext(context);if(!s)return RISC_TCP_CONTEXT;
 auto* r=s->owner;const auto* b=r->tcpBackend_;
 if(!b->connect)return RISC_TCP_INVALID;
 return r->tcpResult(b->connect(b->context,reinterpret_cast<uintptr_t>(context),request,out));
}
int32_t Runtime::tcpConnected(void* context,uint64_t handle){
 auto* s=tcpContext(context);if(!s)return RISC_TCP_CONTEXT;
 auto* r=s->owner;const auto* b=r->tcpBackend_;
 if(!b->connected)return RISC_TCP_INVALID;
 return r->tcpResult(b->connected(b->context,reinterpret_cast<uintptr_t>(context),handle));
}

int32_t Runtime::udpBind(void* context,const risc_tcp_listen_v1* request,uint64_t* out){
 if(out)*out=0;
 auto* s=tcpContext(context);if(!s)return RISC_TCP_CONTEXT;
 auto* r=s->owner;const auto* b=r->tcpBackend_;if(!b->datagram_bind)return RISC_TCP_INVALID;
 return r->tcpResult(b->datagram_bind(b->context,reinterpret_cast<uintptr_t>(context),request,out));
}

int32_t Runtime::udpReceive(void* context,uint64_t handle,void* bytes,uint32_t size,uint32_t* count,risc_tcp_listen_v1* peer){
 if(count)*count=0;
 auto* s=tcpContext(context);if(!s)return RISC_TCP_CONTEXT;
 auto* r=s->owner;const auto* b=r->tcpBackend_;if(!b->datagram_receive)return RISC_TCP_INVALID;
 return r->tcpResult(b->datagram_receive(b->context,reinterpret_cast<uintptr_t>(context),handle,bytes,size,count,peer));
}

int32_t Runtime::udpSend(void* context,uint64_t handle,const void* bytes,uint32_t size,const risc_tcp_listen_v1* peer){
 auto* s=tcpContext(context);if(!s)return RISC_TCP_CONTEXT;
 auto* r=s->owner;const auto* b=r->tcpBackend_;if(!b->datagram_send)return RISC_TCP_INVALID;
 return r->tcpResult(b->datagram_send(b->context,reinterpret_cast<uintptr_t>(context),handle,bytes,size,peer));
}
