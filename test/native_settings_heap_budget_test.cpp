// Selected host requested-byte accounting. Excludes fixture/STL/OpenSSL/vendor
// allocations and allocator headers, fragmentation, and hardware heap behavior.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define NATIVE_IMPORT_NO_MAIN
#include "native_import_initialization_test.cpp"
#undef NATIVE_IMPORT_NO_MAIN
#pragma GCC diagnostic pop
using Track=ImportRecoveryTest::Track;
static std::vector<Bytes> valuesBytes;
static std::vector<std::string> valuesKeys;
static std::vector<C::Value> makeValues(uint32_t imageBytes){
 valuesBytes.clear();valuesKeys.clear();uint32_t left=imageBytes-C::Header-4;
 while(left){const uint32_t n=std::min<uint32_t>(C::ValueMax,left-20);assert(left>=21);valuesBytes.emplace_back(n,0x5a);valuesKeys.push_back("k"+std::to_string(valuesKeys.size()));left-=20+n;}
 std::vector<C::Value> out;for(size_t i=0;i<valuesBytes.size();++i)out.push_back({valuesKeys[i].c_str(),valuesBytes[i].data(),uint32_t(valuesBytes[i].size())});return out;
}
static C::Identity base(){C::Identity b{};b.known=true;b.generation=1;b.lineage[0]=1;b.digest[0]=1;return b;}
static void peak(const char*name,Track&t){printf("heap_case=%s live_payload=%zu peak_payload=%zu allocations=%u\n",name,t.active,t.peak,t.calls);}
static void publication(const char*mode){
 reset();Track t;F::fileAllocation=t.api();{
  F::Fixture f(6,t.api());const uint32_t oldSize=!strcmp(mode,"atomic")?65377:!strcmp(mode,"first")?32630:65256;
  auto rows=makeValues(oldSize);assert(!f.core.initialize(rows.data(),rows.size(),base()));f.store.finishOperation();t.peak=t.active;t.calls=0;
  if(!strcmp(mode,"atomic")){const uint8_t value=1;C::Mutation m{"new",C::Put,&value,1};assert(!f.core.apply(f.core.revision(),&m,1,"i",1));assert(Old::word(F::nvs.at("rnr00000006").at("root").data()+12)==65512);peak(mode,t);}
  else {C::Snapshot snapshot;assert(!f.core.captureSnapshot(&snapshot));C::Identity target=base();++target.generation;++target.digest[0];const uint8_t incarnation[16]={1};C::ExportMarker marker;
   const int rc=!strcmp(mode,"first")?f.core.prepareFirstExport(snapshot,target,incarnation,&marker):f.core.prepareExport(snapshot,target,incarnation,&marker);assert(!rc&&Old::word(F::nvs.at("rnr00000006").at("root").data()+12)==65512);peak(mode,t);
  }
  f.store.finishOperation();
 }F::fileAllocation=C::heap();assert(t.live.empty());
}
int main(int argc,char**argv){assert(argc==2);F::rootPath=argv[1];setbuf(stdout,nullptr);std::filesystem::create_directories(F::rootPath);ImportRecoveryTest::allocations();for(const char*mode:{"atomic","sync","first"})publication(mode);reset();}
