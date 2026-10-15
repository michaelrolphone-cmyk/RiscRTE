// Include the implementation to inspect the exact guarded storage without
// exporting test-only authority or addresses from the production API.
#define ESP_PLATFORM 1
#define RISC_STREAM_HOST_TESTING 1
#include "runtime/streams/ProviderQueueHost.cpp"
#include <cassert>
#include <cstdio>

int main() {
  using namespace RuntimeStreams;
  const auto* h=runtimeProviderStreamHost();
  risc_stream_provider_v1 p{};
  for (const void* rejected : {static_cast<const void*>(&registry),
       static_cast<const void*>(reinterpret_cast<const unsigned char*>(&registry)+sizeof(registry)-1)}) {
    nativeAtomicRejectedAddress=rejected;
    assert(!h->open(&p) && !p.context);
    assert(!Testing::lockRegistry());
    assert(!h->revokeChecked((uint64_t(1)<<32)|1));
    assert(!h->closeChecked((uint64_t(1)<<32)|1));
    assert(!h->safe((uint64_t(1)<<32)|1));
    nativeAtomicRejectedAddress=nullptr;
    assert(!Testing::metadataAllocated());
  }
  assert(h->open(&p));
  assert((p.context>>32)==1); // Rejected placement consumed no authority.
  risc_stream_endpoint_v1 s{sizeof(s),1,RISC_STREAM_READ,32,nullptr,0,0};
  uint32_t endpoint=0;
  assert(p.publish(p.context,&s,&endpoint)==RISC_STREAM_OK && endpoint);
  for (const void* rejected : {static_cast<const void*>(&registry),
       static_cast<const void*>(reinterpret_cast<const unsigned char*>(&registry)+sizeof(registry)-1)}) {
    nativeAtomicRejectedAddress=rejected;
    assert(!h->revokeChecked(p.context));
    assert(!h->closeChecked(p.context));
    assert(!h->safe(p.context));
    assert(p.finish(p.context,endpoint,RISC_STREAM_RETAINED)==RISC_STREAM_BUSY);
    uint32_t count=99;unsigned char byte=7;
    assert(p.produce(p.context,endpoint,&byte,1,&count)==RISC_STREAM_BUSY && !count);
    uint32_t missing=99;
    assert(p.publish(p.context,&s,&missing)==RISC_STREAM_BUSY && !missing);
    assert(p.close(p.context,endpoint)==RISC_STREAM_BUSY);
    nativeAtomicRejectedAddress=nullptr;
    assert(h->safe(p.context)); // Failed retained notification did not touch gate.
    risc_stream_client_info_v1 snapshot{};bool closed=true;
    assert(Testing::queueSnapshot(p.context,endpoint,&snapshot,&closed));
    assert(!closed && !snapshot.terminal && !snapshot.buffered);
  }
  assert(h->revokeChecked(p.context));
  assert(h->closeChecked(p.context));
  assert(!Testing::allocatedBytes());
  std::puts("Native stream registry placement guards PASS");
}
