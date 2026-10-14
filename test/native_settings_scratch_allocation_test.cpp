#include "native_replica_sd_fixture.h"
#include "experimental/native_replica/CheckedFirstSdCheckpoint.h"
#include "experimental/native_replica/CheckedSdReplicaReader.h"
#include "experimental/native_replica/CheckedLegacySettingsReader.h"
#include "experimental/native_replica/CheckedSdSettingsCatalog.h"
#include "experimental/native_replica/OwnedSettingsScratch.h"
struct Alloc {
 bool fail=false,requireWiped=true;unsigned calls=0;size_t live=0;std::map<void*,size_t> owned;
 C::Allocator api(){return {this,[](void*p,size_t n)->void*{auto&a=*static_cast<Alloc*>(p);++a.calls;if(a.fail)return nullptr;auto*out=malloc(n);assert(out);a.owned[out]=n;a.live+=n;memset(out,0xa5,n);return out;},[](void*p,void*out){auto&a=*static_cast<Alloc*>(p);auto i=a.owned.find(out);assert(i!=a.owned.end());if(a.requireWiped)for(size_t n=0;n<i->second;++n)assert(static_cast<uint8_t*>(out)[n]==0);memset(out,0,i->second);a.live-=i->second;a.owned.erase(i);free(out);}};}
 ~Alloc(){assert(!live&&owned.empty());}
};
int main(){
 {struct Bytes {uint8_t bytes[64];};Alloc a;RiscExperimental::OwnedSettingsScratch<Bytes> bytes;
  a.fail=true;assert(!bytes.ensure(a.api())&&!a.live&&a.calls==1);bytes.wipe();
  a.fail=false;assert(bytes.ensure(a.api())&&a.live==64&&a.calls==2);for(auto b:bytes->bytes)assert(!b);
  memset(bytes->bytes,0x5a,64);assert(bytes.ensure(a.api())&&a.calls==2);bytes.reset();assert(!a.live);bytes.reset();}
 Native n;Files f;seed(n,f);Alloc a;a.requireWiped=false;C core(6,n.api(),limits(),a.api());assert(core.load()==C::Ok);Sd sd(f);Hash h;
 const auto beforeRoot=n.root;const auto beforeObjects=n.objects;const auto calls=n.calls;const auto sdCalls=sd.io;const auto live=a.live;
 a.fail=true;
 {T checkpoint(h.api(),sd.guard());assert(checkpoint.startDurable(core,sd.volume())==C::NoMemory&&checkpoint.exitSafe());}
 {RiscExperimental::CheckedSdReplicaReader reader(core,h.api(),sd.guard(),a.api());assert(reader.start(sd.volume())==C::NoMemory&&reader.exitSafe());}
 {using L=RiscExperimental::CheckedLegacySettingsReader;L reader(core,h.api(),sd.guard(),a.api());assert(reader.start(sd.volume(),L::Source::PagedV2)==C::NoMemory&&reader.exitSafe());}
 {using K=RiscExperimental::CheckedSdSettingsCatalog;K catalog(core,h.api(),sd.guard(),a.api());assert(catalog.start(sd.volume())==C::NoMemory&&catalog.exitSafe());}
 {using F=RiscExperimental::CheckedFirstSdCheckpoint;F first(core,h.api(),sd.guard(),{nullptr,[](void*){return true;},[](void*,uint8_t*out){memset(out,1,16);return true;},[](void*,uint32_t,uint32_t){return true;}});assert(first.start(sd.volume())==C::NoMemory&&first.exitSafe());}
 assert(n.root==beforeRoot&&n.objects==beforeObjects&&n.calls==calls&&sd.io==sdCalls&&a.live==live&&!core.exportPrepared());
 puts("Settings scratch: five clients refuse allocation before native/SD I/O or persistent mutation; zero-init, wipe-before-free, no fallback and idempotent release PASS");
}
