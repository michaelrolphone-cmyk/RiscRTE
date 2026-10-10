// App/provider owner callbacks copy only; no SDK or waiting occurs here.
int32_t Port::radioAsyncBegin(void* context,uint64_t token,const risc_radio_request_v1* request,uint32_t* out){
  if(out)*out=0;
  auto& c=*static_cast<Radio*>(context);auto& p=*c.port;
  if(!out || !request || request->struct_size!=sizeof(*request))return RISC_RADIO_INVALID;
  if(!p.available() || p.sleepRetained_ || p.transferring_ || !token || token!=c.token)return RISC_RADIO_INVALID;
  if(!p.hw_.radioAsync || c.asyncUnavailable)return RISC_RADIO_UNAVAILABLE;
  if(c.active || c.closing || c.operation || p.iq_.token || p.hci_.closing || (p.hw_.hciSafe && !p.hw_.hciSafe()))return RISC_RADIO_BUSY;
  const int32_t result=p.hw_.radioAsync->begin(request,out);
  if(result==RISC_RADIO_ACCEPTED && *out){
    c.operation=*out;c.completed=0;c.active=true;c.scanning=request->kind==RISC_RADIO_REQUEST_SCAN;
  }else if(result==RISC_RADIO_ACCEPTED){c.closing=true;return RISC_RADIO_RETAINED;}
  return result;
}
int32_t Port::radioAsyncPoll(void* context,uint64_t token,uint32_t operation,risc_radio_progress_v1* out){
  auto& c=*static_cast<Radio*>(context);auto& p=*c.port;
  if(!out || out->struct_size<sizeof(*out) || !operation)return RISC_RADIO_INVALID;
  if(!p.hw_.owner || !p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || p.transferring_ || !token || token!=c.token)return RISC_RADIO_INVALID;
  if(!p.hw_.radioAsync)return RISC_RADIO_UNAVAILABLE;
  if(operation!=c.operation && operation!=c.completed)return RISC_RADIO_STALE;
  risc_radio_progress_v1 next{};next.struct_size=sizeof(next);
  const int32_t result=p.hw_.radioAsync->poll(operation,&next);
  if(result==RISC_RADIO_AGAIN)return result;
  if(result!=RISC_RADIO_PENDING && result!=RISC_RADIO_QUIESCENT && result!=RISC_RADIO_RETAINED)return result;
  bool valid=next.struct_size==sizeof(next) && next.operation_id==operation &&
    next.phase>=RISC_RADIO_QUEUED && next.phase<=RISC_RADIO_CLEANUP_FAILED &&
    next.failure<=RISC_RADIO_FAILURE_CANCELLED && next.owned<=1 && next.quiescent<=1 && next.station_state<=2 &&
    next.scan.struct_size==sizeof(next.scan) && next.scan.count<=GARDEN_RADIO_SCAN_MAX && next.scan.state<=GARDEN_RADIO_SCAN_FAILED && !next.scan.reserved;
  for(auto byte:next.reserved)if(byte)valid=false;
  for(unsigned i=0;i<next.scan.count;++i){const auto& e=next.scan.entries[i];
    if(strnlen(e.ssid,sizeof(e.ssid))>32 || e.reserved || e.channel>14 || (e.auth>GARDEN_RADIO_AUTH_WPA2_WPA3_PSK && e.auth!=GARDEN_RADIO_AUTH_UNSUPPORTED))valid=false;
  }
  if(result==RISC_RADIO_QUIESCENT){
    if(next.phase!=RISC_RADIO_IDLE || !next.quiescent || next.owned || !p.hw_.radioIdle())valid=false;
  }else if(next.quiescent || !next.owned)valid=false;
  if(!valid){c.closing=true;return RISC_RADIO_RETAINED;}
  if(result==RISC_RADIO_QUIESCENT){c.active=c.closing=c.scanning=false;c.completed=operation;c.operation=0;}
  if(result==RISC_RADIO_RETAINED || next.phase==RISC_RADIO_CLEANUP_FAILED)c.closing=true;
  *out=next;return result;
}
int32_t Port::radioAsyncCancel(void* context,uint64_t token,uint32_t operation){
  auto& c=*static_cast<Radio*>(context);auto& p=*c.port;
  if(!p.hw_.owner || !p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || p.transferring_ || !token || token!=c.token || !operation)return RISC_RADIO_INVALID;
  if(!p.hw_.radioAsync)return RISC_RADIO_UNAVAILABLE;
  if(operation!=c.operation && operation!=c.completed)return RISC_RADIO_STALE;
  if(!p.tcpIdle() || (p.hw_.httpIdle && !p.hw_.httpIdle()))return RISC_RADIO_BUSY;
  const int32_t result=p.hw_.radioAsync->cancel(operation);
  if(result==RISC_RADIO_QUIESCENT){
    if(!p.hw_.radioIdle()){c.closing=true;return RISC_RADIO_RETAINED;}
    c.active=c.closing=c.scanning=false;c.completed=operation;c.operation=0;
  }else if(result==RISC_RADIO_RETAINED)c.closing=true;
  // Ordinary STOPPING deliberately leaves c.closing false: KV/time/input and
  // display keep their owner context; exit/sleep/IQ are fenced by c.active.
  return result;
}

int32_t Port::radioServiceBegin(void* context,uint64_t token,uint32_t* out){
  if(out)*out=0;
  auto& c=*static_cast<Radio*>(context);auto& p=*c.port;
  if(!out || !p.available() || p.sleepRetained_ || p.transferring_ || !token || token!=c.token)return RISC_RADIO_INVALID;
  if(!p.hw_.radioAsync || !p.hw_.radioAsync->tryShared || !p.hw_.radioAsync->endShared)return RISC_RADIO_UNAVAILABLE;
  if(c.closing || !p.providerStorageSafe())return RISC_RADIO_RETAINED;
  if(c.serviceLease)return RISC_RADIO_BUSY;
  if(c.nextServiceLease==UINT32_MAX)return RISC_RADIO_UNAVAILABLE;
  if(!p.hw_.radioAsync->tryShared())return RISC_RADIO_BUSY;
  c.serviceLease=++c.nextServiceLease;*out=c.serviceLease;return RISC_RADIO_ACCEPTED;
}
int32_t Port::radioServiceEnd(void* context,uint64_t token,uint32_t lease){
  auto& c=*static_cast<Radio*>(context);auto& p=*c.port;
  if(!p.hw_.owner || !p.hw_.owner() || p.sleeping_ || !token || token!=c.token || !lease)return RISC_RADIO_INVALID;
  if(lease!=c.serviceLease)return RISC_RADIO_STALE;
  // Always unwind the exact acquired lease, even if independent native custody
  // changed during the service. End does not establish radio quiescence.
  p.hw_.radioAsync->endShared();c.serviceLease=0;return RISC_RADIO_ACCEPTED;
}
