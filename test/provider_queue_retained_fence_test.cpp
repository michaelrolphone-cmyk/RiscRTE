#include "runtime/streams/ProviderQueueHost.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>
using namespace RuntimeStreams;
using Point=Testing::FenceCheckpoint;
static const auto *host=runtimeProviderStreamHost();
static std::function<void(uint64_t,uint32_t)> action;
static Point wanted;
static unsigned hit;
static void hook(Point point,uint64_t context,uint32_t endpoint) {
  if(point!=wanted)return;
  Testing::setFenceHook(nullptr);++hit;action(context,endpoint);
}
static void interleave(Point point,std::function<void(uint64_t,uint32_t)> fn) {
  wanted=point;action=fn;hit=0;Testing::setFenceHook(hook);
}
static risc_stream_provider_v1 open() {risc_stream_provider_v1 p{};assert(host->open(&p));return p;}
static uint32_t publish(const risc_stream_provider_v1& p,uint32_t rights=RISC_STREAM_READ) {
  risc_stream_endpoint_v1 spec{sizeof(spec),1,rights,16,nullptr,0,0};uint32_t id=0;
  assert(p.publish(p.context,&spec,&id)==RISC_STREAM_OK && id);return id;
}
static void close(const risc_stream_provider_v1& p) {assert(host->revokeChecked(p.context));assert(host->closeChecked(p.context));}
static void grant(const risc_stream_provider_v1& p,uint32_t rx,uint32_t tx) {
  assert(reserveEndpointPair(p.context,7,9,11,rx,tx)==0);
  assert(host->grant(p.context,7,9,rx,RISC_STREAM_READ));
  assert(host->grant(p.context,7,9,tx,RISC_STREAM_WRITE));
}
static auto snapshot(const risc_stream_provider_v1& p,uint32_t id) {
  risc_stream_client_info_v1 out{};bool closed=false;
  assert(Testing::queueSnapshot(p.context,id,&out,&closed));return out;
}
static void retained(const risc_stream_provider_v1& p,uint32_t rx,uint32_t tx,size_t bytes) {
  assert(!host->safe(p.context));char c=0;uint32_t n=99;
  assert(providerStreamRead(p.context,7,9,rx,&c,1,&n)==RISC_STREAM_CLOSED && !n);
  assert(providerStreamWrite(p.context,7,9,tx,"X",1,&n)==RISC_STREAM_CLOSED && !n);
  assert(p.produce(p.context,rx,"X",1,&n)==RISC_STREAM_CLOSED && !n);
  assert(p.close(p.context,rx)==RISC_STREAM_CLOSED);
  assert(!host->revokeChecked(p.context) && !host->closeChecked(p.context));
  if(rx!=tx)assert(releaseEndpointPair(p.context,7,9,11,rx,tx)==RISC_STREAM_CLOSED);
  assert(Testing::allocatedBytes()==bytes);
}
int main(int argc,char **argv) {
  assert(argc==2);const char *mode=argv[1];
  auto p=open();uint32_t rx=publish(p),tx=0,n=0;
  if(!strcmp(mode,"contention") || !strcmp(mode,"notify-pair") || !strcmp(mode,"reserved-close") ||
     !strcmp(mode,"pair-wins") || !strcmp(mode,"transfer-inflight")) {
    tx=publish(p,RISC_STREAM_WRITE);grant(p,rx,tx);
  }
  assert(p.produce(p.context,rx,"abc",3,&n)==0 && n==3);
  if(tx)assert(providerStreamWrite(p.context,7,9,tx,"123",3,&n)==0 && n==3);
  if(!strcmp(mode,"contention")) {
    assert(Testing::lockRegistry());
    int32_t first=0,second=0;
    std::thread worker([&]{first=p.finish(p.context,rx,RISC_STREAM_RETAINED);second=p.finish(p.context,tx,RISC_STREAM_RETAINED);});worker.join();
    assert(first==RISC_STREAM_BUSY && second==RISC_STREAM_BUSY && !host->safe(p.context));
    Testing::unlockRegistry();retained(p,rx,tx,32);
    assert(snapshot(p,rx).buffered==3 && snapshot(p,tx).buffered==3 && snapshot(p,rx).terminal==0);
  } else if(!strcmp(mode,"authority")) {
    auto other=open();uint32_t foreign=publish(other);
    assert(p.finish(p.context,foreign,RISC_STREAM_RETAINED)==RISC_STREAM_DENIED);
    assert(p.finish(p.context,UINT32_MAX,RISC_STREAM_RETAINED)==RISC_STREAM_CLOSED);
    assert(p.finish(0,rx,RISC_STREAM_RETAINED)==RISC_STREAM_INVALID);
    assert(p.finish(p.context,rx,0)==RISC_STREAM_INVALID);
    assert(Testing::lockRegistry());assert(p.finish(p.context,foreign,RISC_STREAM_RETAINED)==RISC_STREAM_BUSY);
    assert(host->safe(p.context) && host->safe(other.context));Testing::unlockRegistry();
    assert(p.finish(p.context,rx,RISC_STREAM_EOF)==0);
    assert(p.finish(p.context,rx,RISC_STREAM_IO)==0);
    assert(p.finish(p.context,rx,RISC_STREAM_EOF)==RISC_STREAM_IO);
    assert(host->safe(p.context));close(p);close(other);
  } else if(!strcmp(mode,"reserved-close")) {
    assert(revokeProviderStreamGrant(p.context,7)==0);
    assert(p.close(p.context,rx)==0 && p.close(p.context,tx)==0);
    assert(p.finish(p.context,rx,RISC_STREAM_RETAINED)==RISC_STREAM_CLOSED && host->safe(p.context));
    assert(Testing::allocatedBytes()==32);
    assert(releaseEndpointPair(p.context,7,9,11,rx,tx)==0 && Testing::allocatedBytes()==0);close(p);
  } else if(!strcmp(mode,"destroy-republish")) {
    assert(p.close(p.context,rx)==0);uint32_t next=publish(p);assert(next!=rx);
    assert(p.finish(p.context,rx,RISC_STREAM_RETAINED)==RISC_STREAM_CLOSED && host->safe(p.context));
    assert(p.finish(p.context,next,RISC_STREAM_RETAINED)==0);retained(p,next,next,16);
  } else if(!strcmp(mode,"reuse")) {
    close(p);auto next=open();uint32_t id=publish(next);
    assert(uint32_t(p.context)==uint32_t(next.context) && p.context!=next.context);
    assert(p.finish(p.context,id,RISC_STREAM_RETAINED)==RISC_STREAM_CLOSED && host->safe(next.context));
    assert(next.finish(next.context,rx,RISC_STREAM_RETAINED)==RISC_STREAM_CLOSED);
    assert(next.finish(next.context,id,RISC_STREAM_RETAINED)==0);retained(next,id,id,16);
  } else if(!strcmp(mode,"notify-close")) {
    interleave(Point::NotificationOwned,[&](uint64_t,uint32_t){assert(p.close(p.context,rx)==RISC_STREAM_RETAINED);});
    assert(p.finish(p.context,rx,RISC_STREAM_RETAINED)==0 && hit==1);retained(p,rx,rx,16);
  } else if(!strcmp(mode,"notify-pair")) {
    assert(revokeProviderStreamGrant(p.context,7)==0);
    interleave(Point::NotificationOwned,[&](uint64_t,uint32_t){assert(releaseEndpointPair(p.context,7,9,11,rx,tx)==RISC_STREAM_RETAINED);});
    assert(p.finish(p.context,rx,RISC_STREAM_RETAINED)==0 && hit==1);retained(p,rx,tx,32);
  } else if(!strcmp(mode,"notify-context")) {
    interleave(Point::NotificationOwned,[&](uint64_t,uint32_t){assert(host->revokeChecked(p.context));assert(!host->closeChecked(p.context));});
    assert(p.finish(p.context,rx,RISC_STREAM_RETAINED)==0 && hit==1);retained(p,rx,rx,16);
    auto next=open();assert(uint32_t(next.context)!=uint32_t(p.context));close(next);
  } else if(!strcmp(mode,"close-notify")) {
    interleave(Point::CloseAdmitted,[&](uint64_t,uint32_t){assert(p.finish(p.context,rx,RISC_STREAM_RETAINED)==RISC_STREAM_BUSY);});
    assert(p.close(p.context,rx)==RISC_STREAM_RETAINED && hit==1);retained(p,rx,rx,16);
  } else if(!strcmp(mode,"close-wins")) {
    interleave(Point::EndpointWithdrawn,[&](uint64_t,uint32_t){assert(p.finish(p.context,rx,RISC_STREAM_RETAINED)==RISC_STREAM_BUSY && host->safe(p.context));});
    assert(p.close(p.context,rx)==0 && hit==1 && Testing::allocatedBytes()==0);close(p);
  } else if(!strcmp(mode,"pair-wins")) {
    assert(revokeProviderStreamGrant(p.context,7)==0);
    interleave(Point::PairWithdrawn,[&](uint64_t,uint32_t){assert(p.finish(p.context,rx,RISC_STREAM_RETAINED)==RISC_STREAM_BUSY && host->safe(p.context));});
    assert(releaseEndpointPair(p.context,7,9,11,rx,tx)==0 && hit==1 && Testing::allocatedBytes()==0);close(p);
  } else if(!strcmp(mode,"context-wins")) {
    assert(host->revokeChecked(p.context));
    interleave(Point::ContextRetiring,[&](uint64_t,uint32_t){assert(p.finish(p.context,rx,RISC_STREAM_RETAINED)==RISC_STREAM_BUSY && !host->safe(p.context));});
    assert(host->closeChecked(p.context) && hit==1 && Testing::allocatedBytes()==0);
    auto next=open();uint32_t id=publish(next);
    assert(p.finish(p.context,id,RISC_STREAM_RETAINED)==RISC_STREAM_CLOSED && host->safe(next.context));close(next);
  } else if(!strcmp(mode,"stale-started") || !strcmp(mode,"stale-observed")) {
    risc_stream_provider_v1 next{};uint32_t next_id=0;
    bool started=!strcmp(mode,"stale-started");uint32_t requested=started?rx+1:rx;
    interleave(started?Point::NotificationStarted:Point::NotificationObserved,[&](uint64_t,uint32_t){close(p);next=open();next_id=publish(next);assert(next_id==rx+1);});
    assert(p.finish(p.context,requested,RISC_STREAM_RETAINED)==RISC_STREAM_CLOSED && hit==1 && host->safe(next.context));
    assert(next.finish(next.context,next_id,RISC_STREAM_RETAINED)==0);retained(next,next_id,next_id,16);
  } else if(!strcmp(mode,"publish-allocated") || !strcmp(mode,"publish-admitted")) {
    const bool admitted=!strcmp(mode,"publish-admitted");
    interleave(admitted?Point::PublishAdmitted:Point::PublishAllocated,[&](uint64_t,uint32_t){assert(p.finish(p.context,rx,RISC_STREAM_RETAINED)==RISC_STREAM_BUSY);});
    risc_stream_endpoint_v1 spec{sizeof(spec),1,RISC_STREAM_READ,16,nullptr,0,0};uint32_t id=99;
    int32_t rc=p.publish(p.context,&spec,&id);
    assert(hit==1 && rc==(admitted?RISC_STREAM_OK:RISC_STREAM_CLOSED) && bool(id)==admitted);
    retained(p,rx,rx,admitted?32:16);if(admitted)assert(snapshot(p,id).capacity==16);
  } else if(!strcmp(mode,"transfer-inflight")) {
    interleave(Point::TransferAdmitted,[&](uint64_t,uint32_t){assert(p.finish(p.context,rx,RISC_STREAM_RETAINED)==RISC_STREAM_BUSY);});
    char byte=0;assert(providerStreamRead(p.context,7,9,rx,&byte,1,&n)==0 && n==1 && byte=='a' && hit==1);
    retained(p,rx,tx,32);assert(snapshot(p,rx).buffered==2 && snapshot(p,rx).bytes_read==1);
  } else if(!strcmp(mode,"revoke-inflight")) {
    interleave(Point::NotificationOwned,[&](uint64_t,uint32_t){assert(host->revokeChecked(p.context));});
    assert(p.finish(p.context,rx,RISC_STREAM_RETAINED)==0 && hit==1);retained(p,rx,rx,16);
  } else if(!strcmp(mode,"endpoint-max")) {
    assert(p.close(p.context,rx)==0);
    assert(Testing::advanceIssuers(uint32_t(p.context>>32),UINT32_MAX-1));uint32_t id=publish(p);assert(id==UINT32_MAX);
    assert(p.finish(p.context,id,RISC_STREAM_RETAINED)==0);retained(p,id,id,16);
  } else if(!strcmp(mode,"generation-max")) {
    close(p);assert(Testing::advanceIssuers((UINT32_MAX>>2)-1,rx));auto last=open();uint32_t id=publish(last);
    assert((last.context>>32)==(UINT32_MAX>>2));close(last);risc_stream_provider_v1 failed{};assert(!host->open(&failed));
    assert(last.finish(last.context,id,RISC_STREAM_RETAINED)==RISC_STREAM_CLOSED && !host->closeChecked(last.context));
  } else if(!strcmp(mode,"claimed-slot-publication")) {
    publish(p);publish(p);uint32_t fourth=0;
    interleave(Point::NotificationOwned,[&](uint64_t,uint32_t){fourth=publish(p);});
    assert(p.finish(p.context,rx,RISC_STREAM_RETAINED)==0 && hit==1 && fourth);retained(p,rx,rx,64);
  } else assert(!"unknown scenario");
  std::printf("provider retention mirror %s: PASS (contexts=%zu queues=%zu bytes)\n",mode,Testing::contextMetadataBytes(),Testing::queueMetadataBytes());
}
