#pragma once
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
// Only explicit Core/store/AppData/catalog/source payload requests are counted.
// STL bookkeeping, OpenSSL, RTOS, filesystem/driver/SDK and allocator overhead
// are outside this seam. Prefix bytes model NativeReplicaCore::heap exactly.
struct NativeSettingsPayloadTracker {
 std::mutex lock;std::map<void*,size_t>blocks;size_t live=0,peak=0,total=0,peakBlocks=0;unsigned allocations=0;
 C::Allocator api(){return {this,[](void*p,size_t n)->void*{auto&t=*static_cast<NativeSettingsPayloadTracker*>(p);auto*base=static_cast<uint64_t*>(malloc(n+8));if(!base)return nullptr;*base=n;void*out=base+1;std::lock_guard<std::mutex>hold(t.lock);t.blocks[out]=n;t.live+=n;t.total+=n;++t.allocations;if(t.live>t.peak){t.peak=t.live;t.peakBlocks=t.blocks.size();}return out;},[](void*p,void*out){auto&t=*static_cast<NativeSettingsPayloadTracker*>(p);std::lock_guard<std::mutex>hold(t.lock);auto i=t.blocks.find(out);assert(i!=t.blocks.end());C::wipe(out,i->second);t.live-=i->second;t.blocks.erase(i);free(static_cast<uint64_t*>(out)-1);}};}
 void reset(){std::lock_guard<std::mutex>hold(lock);peak=live;peakBlocks=blocks.size();total=0;allocations=0;}
 void report(unsigned ns,const char*phase){std::lock_guard<std::mutex>hold(lock);printf("payload_budget namespace=%u phase=%s live=%zu peak=%zu peak_default_heap_request=%zu peak_blocks=%zu allocated_total=%zu allocations=%u\n",ns,phase,live,peak,peak+8*peakBlocks,peakBlocks,total,allocations);}
};
static NativeSettingsPayloadTracker payloadTracker;
