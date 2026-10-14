#include "native_first_sd_checkpoint_fixture.h"
#include "native_replica_checked_fixture.h"
namespace Nf=NativeFixture;
struct SavedNative {
 decltype(Nf::nvs) metadata=Nf::nvs;Files banks;
 SavedNative(){for(auto&e:std::filesystem::recursive_directory_iterator(Nf::rootPath))if(e.is_regular_file()){std::ifstream in(e.path(),std::ios::binary);banks[std::filesystem::relative(e.path(),Nf::rootPath).string()]=Bytes(std::istreambuf_iterator<char>(in),{});}}
 void restore()const{Nf::empty();Nf::nvs=metadata;for(auto&e:banks){auto path=std::filesystem::path(Nf::rootPath)/e.first;std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(e.second.data()),e.second.size());assert(out.good());}}
};
static void nativeSeed(){Nf::empty();Nf::Fixture f;std::vector<std::string> keys;std::vector<C::Value> rows;for(unsigned i=0;i<100;++i)keys.push_back("k"+std::to_string(i));for(auto&k:keys)rows.push_back({k.c_str(),"vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv",32});auto map=mapRows(keys);assert(f.core.initialize(rows.data(),rows.size(),identity(map,rows.size()))==0);edit(f.core);}
static void sdRun(FirstSd&sd,F&f){const unsigned calls=Nf::nvsTotal+Nf::fsTotal;Nf::onWorker=false;sd.run(f);Nf::onWorker=true;assert(calls==Nf::nvsTotal+Nf::fsTotal);}
static void complete(FirstSd&sd,F&f){sdRun(sd,f);if(f.state()==F::State::Admitted){assert(f.prepareNative()==0);sdRun(sd,f);}assert(f.state()==F::State::Verified);}
static void bankReuse(){nativeSeed();Files files;Bytes marker;
 {Nf::Fixture f;assert(f.core.load()==0);FirstSd sd(files,&f.core);Hash h;F writer(f.core,h.api(),sd.guard(),sd.authority());assert(writer.start(sd.volume())==0);sdRun(sd,writer);assert(writer.prepareNative()==0);C::ExportMarker m;assert(f.core.preparedExport(&m)==0);marker.assign(m.encoded(),m.encoded()+C::MarkerBytes);printf("first checked store capacity: capture=%u groups=%u v4_header=%u image=%u cap=%u bytes; no-group max capture=(cap-516)/2=%u bytes\n",m.mapSize(),m.prefixBytes(),f.core.headerSize(),f.core.headerSize()+m.mapSize()+m.prefixBytes()+4,f.core.imageCapacity(),(f.core.imageCapacity()-516)/2);writer.cancel();sdRun(sd,writer);for(unsigned i=0;i<7;++i)edit(f.core,"newer");assert(f.core.pendingCount()==8);f.store.finishOperation();}
 {Nf::Fixture f;assert(f.core.load()==0);C::ExportMarker m;assert(f.core.preparedExport(&m)==0&&!memcmp(m.encoded(),marker.data(),marker.size()));FirstSd sd(files,&f.core);Hash h;F writer(f.core,h.api(),sd.guard(),sd.authority());assert(writer.start(sd.volume())==0);complete(sd,writer);assert(writer.acknowledgeNative()==0&&f.core.pendingCount()==7&&get(f.core,"edited")=="newer"&&f.core.formatVersion()==3);f.store.finishOperation();}
 {Nf::Fixture f;assert(f.core.load()==0&&f.core.pendingCount()==7&&get(f.core,"edited")=="newer");}
 puts("first checked store: immutable captured map survives repeated physical bank reuse, reboot and acknowledgement; owner SD polling makes no native I/O PASS");
}
static void nativeCuts(){unsigned reached=0,retained=0,uncertain=0;
 for(unsigned phase=0;phase<2;++phase){nativeSeed();Files files;
  if(phase){Nf::Fixture f;assert(f.core.load()==0);FirstSd sd(files,&f.core);Hash h;F writer(f.core,h.api(),sd.guard(),sd.authority());assert(writer.start(sd.volume())==0);complete(sd,writer);writer.cancel();edit(f.core,"newer");f.store.finishOperation();}
  const SavedNative saved;const Files originalFiles=files;
  for(const char*kind:{"nvs-open","nvs-probe","nvs-read","nvs-set","nvs-commit","write","partial","read","sync","close","rename-before","rename-after"})for(unsigned after=0;after<2;++after)for(unsigned cut=1;cut<256;++cut){saved.restore();files=originalFiles;bool hit=false;
   {Nf::Fixture f;assert(f.core.load()==0);FirstSd sd(files,&f.core);Hash h;F writer(f.core,h.api(),sd.guard(),sd.authority());assert(writer.start(sd.volume())==0);sdRun(sd,writer);assert(writer.state()==(phase?F::State::Verified:F::State::Admitted));Nf::fault=kind;Nf::cut=cut;Nf::after=after;Nf::armed=true;
    const int rc=phase?writer.acknowledgeNative():writer.prepareNative();hit=(Nf::nativeCalls>=cut||Nf::fsCalls>=cut);Nf::armed=false;if(rc==C::Retained)++retained;if(rc==C::Uncertain)++uncertain;
    assert(rc==C::Ok||rc==C::Retained||rc==C::Uncertain||rc==C::Io||rc==C::NoSpace||rc==C::Busy);if(!phase){assert(files.empty()&&!sd.mutations);writer.cancel();sdRun(sd,writer);}f.store.finishOperation();
   }
   Nf::reset();{Nf::Fixture f;assert(f.core.load()==0);if(!phase||f.core.exportPrepared()){FirstSd sd(files,&f.core);Hash h;F retry(f.core,h.api(),sd.guard(),sd.authority());assert(retry.start(sd.volume())==0);complete(sd,retry);assert(retry.acknowledgeNative()==0);}assert(f.core.identity().generation==8&&f.core.pendingCount()==(phase?1u:0u)&&get(f.core,"edited")== (phase?"newer":"local"));f.store.finishOperation();}
   if(!hit)break;
   ++reached;
  }
 }
 printf("first checked store: %u reached AppData/NVS arm/ack faults (%u Retained, %u Uncertain), exact old/new root reload and recovery PASS\n",reached,retained,uncertain);
}
int main(int argc,char**argv){(void)&addLegacy;assert(argc==2);Nf::rootPath=argv[1];bankReuse();nativeCuts();Nf::empty();puts("CHECKED HOST STORE PROOF ONLY: synthetic NVS/space and SD, no hardware qualification");}
