// Production NativeBankStore admission with native I/O/resolver test doubles.
// No ELF is mapped and no provider or peripheral callback may execute here.
#include "native_bank_provider_selection.h"
static std::string providerCustodyReadPath;
static unsigned providerCustodyLoss=0;
static unsigned providerCustodySkipReads=0,providerCustodyEofProbes=0;
static bool providerReadStorageSafe=true;
static uint32_t providerRetainedRead=0;
static void* providerRetainedReadContext=nullptr;
static const uint8_t* providerRetainedReadBytes=nullptr;
static unsigned providerReadCalls=0,providerCloseCalls=0,providerOpenCalls=0;
static bool providerBudgetActive=false;
static uint32_t providerBudgetReadMillis=0;
static size_t providerBudgetReadBytes=0;
static std::vector<unsigned> providerBudgetReadDelays,providerBudgetHashDelays;
// Decorate the actual generic provider table. The backend still owns all file
// operations; faults and retained identity are observed at its context/token seam.
class ProviderAdmissionSources final {
 struct Source;
 static std::map<void*,Source*>& providers(){static std::map<void*,Source*> value;return value;}
 struct Source {
   const std::string logical,backend;
   RiscStorage::PosixFiles provider;
   risc_storage_files_v1 table;
   RiscBoot::RuntimeFiles files;
   std::map<uint32_t,std::string> opened;
   Source(const RiscTest::FileSources::Mapping& mapping):logical(mapping.logical),backend(mapping.backend),
       provider(backend.c_str()),table(*provider.api()) {
     providers().emplace(table.context,this);
     table.open=[](void* context,const char* path,uint32_t flags,uint32_t* handle)->int32_t {
       auto& s=*providers().at(context);const auto* api=s.provider.api();
       ++providerOpenCalls;const int32_t rc=api->open(context,path,flags,handle);
       if(rc==RISC_FILES_OK)s.opened.emplace(*handle,s.logical+path);
       return rc;
     };
     table.read=[](void* context,uint32_t handle,void* bytes,uint32_t count,uint32_t* actual)->int32_t {
       auto& s=*providers().at(context);const auto* api=s.provider.api();++providerReadCalls;
       if(context==providerRetainedReadContext && handle==providerRetainedRead && providerCustodyLoss!=4)++providerCustodyEofProbes;
       if(providerBudgetActive)providerBudgetReadDelays.push_back(delays);
       const int32_t rc=api->read(context,handle,bytes,count,actual);
       if(providerBudgetActive && rc==RISC_FILES_OK && *actual){providerBudgetReadBytes+=*actual;ticks+=providerBudgetReadMillis;}
       const auto opened=s.opened.find(handle);
       if(rc==RISC_FILES_OK && *actual && opened!=s.opened.end() && opened->second==providerCustodyReadPath){
         if(providerCustodySkipReads){--providerCustodySkipReads;return rc;}
         providerCustodyReadPath.clear();providerRetainedRead=handle;providerRetainedReadContext=context;
         providerRetainedReadBytes=static_cast<const uint8_t*>(bytes);
         if(providerCustodyLoss==1)ownerEnabled=false;
         if(providerCustodyLoss==2)providerReadStorageSafe=false;
         if(providerCustodyLoss==3)operationEnabled=false;
         if(providerCustodyLoss==4)failNextNativeClose=true;
       }
       return rc;
     };
     table.close=[](void* context,uint32_t handle)->int32_t {
       auto& s=*providers().at(context);const auto* api=s.provider.api();
       if(context==providerRetainedReadContext && handle==providerRetainedRead)++providerCloseCalls;
       const int32_t rc=api->close(context,handle);if(rc==RISC_FILES_OK)s.opened.erase(handle);return rc;
     };
     assert(files.bind(&table,own,safe,provider.identityApi(),provider.directoryApi()));
   }
 };
 std::vector<std::unique_ptr<Source>> owned_;
 std::vector<RiscBoot::RuntimeFileSources::Entry> entries_;
 std::unique_ptr<const RiscBoot::RuntimeFileSources> sources_;
 public:
 explicit ProviderAdmissionSources(std::initializer_list<RiscTest::FileSources::Mapping> mappings){
   for(const auto& mapping:mappings){owned_.emplace_back(new Source(mapping));auto& source=*owned_.back();entries_.push_back({source.logical.c_str(),&source.files});}
   sources_.reset(new RiscBoot::RuntimeFileSources(entries_.data(),entries_.size()));
 }
 const RiscBoot::RuntimeFileSources* sources()const{return sources_.get();}
 bool bind(RiscBoot::Runtime& runtime)const{return runtime.bindFileSources(sources());}
 bool owns(const char* root,void* context,uint32_t handle)const {
   const auto it=providers().find(context);return it!=providers().end() &&
     sources()->find(root)==&it->second->files && it->second->opened.count(handle)==1;
 }
};
static void nativeAdmissionBudgets(const std::string& root,const ProviderAdmissionSources& sources){
 using namespace RiscBankStore;
 const auto save=[&](const char* name,const std::vector<uint8_t>& bytes){
   std::ofstream out(root+"/"+name,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());assert(out.good());
 };
 std::ofstream(root+"/boot.json")<<R"({"board":"board.json","default_app":"budget-a.elf","drivers":[]})";
 auto candidate=std::make_unique<RiscBoot::Runtime>(RiscBoot::Port{own,nullptr,nullptr,nullptr});
 assert(sources.bind(*candidate) && candidate->prepare(root.c_str()));
 const auto reset=[&](uint32_t readMillis){
   assert(!nativeHashUpdateHook && !retainedAdmissionRuntime && !retainedAdmissionFile && !retainedAdmissionBytes);
   ticks=1000;delays=0;delayScale=1;unsafeAfterDelay=0;providerBudgetReadMillis=readMillis;
   providerBudgetReadBytes=0;providerBudgetReadDelays.clear();providerBudgetHashDelays.clear();providerBudgetActive=true;
 };
 const std::string path=root+"/budget-a.elf";
 unsigned cases=0;
 // Constant time: only the byte budget can yield, once per complete 32 KiB.
 {
   const std::vector<uint8_t> bytes(64*1024,0x5a);save("budget-a.elf",bytes);reset(0);
   const uint32_t started=ticks;RiscBoot::RuntimeFileImage image;
   assert(readAdmissionImage(candidate.get(),path.c_str(),image,bytes.size(),&started));
   assert(image.size==bytes.size() && !image.handle && !image.retained && !memcmp(image.bytes,bytes.data(),bytes.size()));
   assert(delays==2 && ticks-started==2 && providerBudgetReadBytes==bytes.size() && providerBudgetReadDelays.size()==17);
   for(size_t i=0;i<providerBudgetReadDelays.size();++i)assert(providerBudgetReadDelays[i]==i/8);
   free(image.bytes);++cases;
 }
 // Only 8 KiB: advancing each provider read by 2 ms must yield twice, before
 // the next provider call, without waiting for the byte budget.
 {
   const std::vector<uint8_t> bytes(8192,0x6b);save("budget-a.elf",bytes);reset(2);
   const uint32_t started=ticks;RiscBoot::RuntimeFileImage image;
   assert(readAdmissionImage(candidate.get(),path.c_str(),image,bytes.size(),&started));
   assert(delays==2 && ticks-started==6 && providerBudgetReadBytes==8192);
   assert((providerBudgetReadDelays==std::vector<unsigned>{0,1,2}));free(image.bytes);++cases;
 }
 // An expired shared admission must reject before opening even a valid file.
 {
   reset(0);const uint32_t started=ticks-30000u;const unsigned opens=providerOpenCalls,reads=providerReadCalls;
   RiscBoot::RuntimeFileImage image;assert(!readAdmissionImage(candidate.get(),path.c_str(),image,8192,&started));
   assert(image.error==ETIMEDOUT && !image.bytes && !image.handle && !image.retained);
   assert(providerOpenCalls==opens && providerReadCalls==reads && !delays);++cases;
 }
 auto profile=std::make_unique<RiscProvision::Profile>();profile->count=2;
 const auto images=[&](size_t length){
   auto bytes=elf("memcpy");bytes.resize(length);assert(admitElf(bytes.data(),bytes.size(),ElfRole::Application));
   for(unsigned i=0;i<2;++i){auto& file=profile->files[i];strcpy(file.path,i?"budget-b.elf":"budget-a.elf");file.bytes=bytes.size();SHA256(bytes.data(),bytes.size(),file.sha256);save(file.path,bytes);}
 };
 ProvisionState state({},admissionHardware(),nullptr,nullptr);provisionState=&state;provisionProfile=profile.get();
 // Both actual provisioning inspections share one started value. The first
 // image leaves 2 ms after both reader and ELF-audit yields; the second
 // consumes them and must not start a new budget.
 {
   images(1024);reset(29996);AdmissionImages context{root.c_str(),ticks,candidate.get()};
   assert(inspectProvisionedImage(context,path.c_str(),ElfRole::Application));
   assert(context.seen[0] && !context.seen[1] && ticks-context.started==29998 && delays==2);
   const size_t hashes=nativeHashStarts;providerBudgetReadMillis=2;
   assert(!inspectProvisionedImage(context,(root+"/budget-b.elf").c_str(),ElfRole::Application));
   assert(!context.seen[1] && ticks-context.started==30000 && nativeHashStarts==hashes);
   assert(!provisionReadRetained && !retainedAdmissionFile && !candidate->metadataCloseRetained() && sources.sources()->exitSafe());
   const unsigned opens=providerOpenCalls,reads=providerReadCalls;
   assert(!inspectProvisionedImage(context,(root+"/budget-b.elf").c_str(),ElfRole::Application));
   assert(providerOpenCalls==opens && providerReadCalls==reads);++cases;
 }
 // Count read-pass and hash-pass fairness separately. With no elapsed time,
 // 64 KiB contributes two yields in each pass, plus one ELF-audit yield.
 {
   images(64*1024);reset(0);AdmissionImages context{root.c_str(),ticks,candidate.get()};
   const size_t updates=nativeHashUpdates;assert(inspectProvisionedImage(context,path.c_str(),ElfRole::Application));
   assert(context.seen[0] && delays==5 && ticks-context.started==5 && nativeHashUpdates-updates==16);
   assert(providerBudgetReadDelays.back()==2);++cases;
 }
 // Inject time only in the SHA fixture boundary. The read pass is below both
 // budgets; each 4 KiB hash update then crosses the elapsed 2 ms threshold.
 {
   images(8192);reset(0);AdmissionImages context{root.c_str(),ticks,candidate.get()};
   nativeHashUpdateHook=[](size_t n){assert(n==4096);providerBudgetHashDelays.push_back(delays);ticks+=2;};
   const bool accepted=inspectProvisionedImage(context,path.c_str(),ElfRole::Application);nativeHashUpdateHook=nullptr;
   assert(accepted && context.seen[0] && delays==3 && ticks-context.started==7);
   assert((providerBudgetReadDelays==std::vector<unsigned>{0,0,0}));
   assert((providerBudgetHashDelays==std::vector<unsigned>{0,1}));++cases;
 }
 // Hash time also spends the same aggregate deadline, including the yield.
 {
   images(8192);reset(0);AdmissionImages context{root.c_str(),ticks-29998u,candidate.get()};
   const size_t updates=nativeHashUpdates,finishes=nativeHashFinishes;
   nativeHashUpdateHook=[](size_t n){assert(n==4096);ticks+=2;};
   const bool accepted=inspectProvisionedImage(context,path.c_str(),ElfRole::Application);nativeHashUpdateHook=nullptr;
   assert(!accepted && !context.seen[0] && delays==1 && ticks-context.started==30001);
   assert(nativeHashUpdates-updates==1 && nativeHashFinishes==finishes && sources.sources()->exitSafe());++cases;
 }
 providerBudgetActive=false;providerBudgetReadMillis=0;provisionState=nullptr;provisionProfile=nullptr;
 assert(cases==7 && operationSafe() && !hardwareCalls && !writes && !selectorCalls && !restarts);
 std::cout<<"Production NativeBank reader/hash budgets: "<<cases<<" aggregate-deadline, zero-I/O expiry and 32KiB/2ms fairness cases PASS\n";
}
static void nativeProviderAdmission(const char* directory,unsigned custodyLoss=0,bool provisioningCustody=false,bool secondRead=false){
 using namespace RiscBankStore;
 using namespace RuntimeProviders;
 using Files=std::map<std::string,std::vector<uint8_t>>;
 const auto text=[](const char* value){return std::vector<uint8_t>(value,value+strlen(value));};
 const auto save=[](const std::string& root,const Files& files){
   std::filesystem::remove_all(root);std::filesystem::create_directories(root);
   for(const auto& file:files){std::filesystem::create_directories(std::filesystem::path(root+"/"+file.first).parent_path());
     std::ofstream out(root+"/"+file.first,std::ios::binary);
     out.write(reinterpret_cast<const char*>(file.second.data()),file.second.size());assert(out.good());}
 };
 const auto& policy=nativeBankFixturePolicy();
 auto nativeElf=elf("xTaskGetTickCount","t5_driver_get");
 uint8_t actual[32]{};SHA256(nativeElf.data(),nativeElf.size(),actual);
 assert(!memcmp(actual,policy.sha256,32));
 isOwner=own;operationIsSafe=safe;prepared=confirmed=true;activeBank=0;scratch=new Scratch;
 for(unsigned b=0;b<2;++b)for(unsigned r=0;r<2;++r)parts[b][r]=&table[b*2+r];
 assert(!admitElf(nativeElf.data(),nativeElf.size(),ElfRole::Driver));
 assert(admitElf(nativeElf.data(),nativeElf.size(),ElfRole::Driver,&policy));
 auto wrongCohort=policy;wrongCohort.osCpuAbi=3-policy.osCpuAbi;
 assert(!nativeProviderPolicyValid(wrongCohort));
 assert(!admitElf(nativeElf.data(),nativeElf.size(),ElfRole::Driver,&wrongCohort));
 wrongCohort.osCpuAbi=99;
 assert(!admitElf(nativeElf.data(),nativeElf.size(),ElfRole::Driver,&wrongCohort));
 assert(!admitElf(nativeElf.data(),nativeElf.size(),ElfRole::Either,&policy));
 assert(!admitElf(nativeElf.data(),nativeElf.size(),ElfRole::Application,&policy));
 // Native authority never follows the bytes into an application role, even
 // when a trusted policy's digest is deliberately repinned to that image.
 auto appElf=elf("xTaskGetTickCount");auto changedPolicy=policy;
 SHA256(appElf.data(),appElf.size(),changedPolicy.sha256);
 assert(!admitElf(appElf.data(),appElf.size(),ElfRole::Application,&changedPolicy));
 assert(!admitElf(appElf.data(),appElf.size(),ElfRole::Either,&changedPolicy));
 assert(!admitElf(appElf.data(),appElf.size(),ElfRole::Driver,&changedPolicy));
 auto changed=nativeElf;changed[400]^=1;
 assert(!admitElf(changed.data(),changed.size(),ElfRole::Driver,&policy));
 assert(!admitElf(nativeElf.data(),nativeElf.size()-1,ElfRole::Driver,&policy));
 changedPolicy=policy;changedPolicy.sha256[0]^=1;
 assert(!admitElf(nativeElf.data(),nativeElf.size(),ElfRole::Driver,&changedPolicy));
 auto malformed=nativeElf;reinterpret_cast<elf32_hdr_t*>(malformed.data())->machine=3;
 changedPolicy=policy;SHA256(malformed.data(),malformed.size(),changedPolicy.sha256);
 assert(!admitElf(malformed.data(),malformed.size(),ElfRole::Driver,&changedPolicy));
 const char* unsupported="esp_partition_write";auto forged=elf(unsupported,"t5_driver_get");
 changedPolicy=policy;changedPolicy.imports=&unsupported;SHA256(forged.data(),forged.size(),changedPolicy.sha256);
 assert(!admitElf(forged.data(),forged.size(),ElfRole::Driver,&changedPolicy));
 // A fresh hash cannot hide an undeclared import in either symbol table.
 for(bool secondTable:{false,true}){
   auto undeclared=elf("memcpy","t5_driver_get");
   if(secondTable){
     undeclared=nativeElf;reinterpret_cast<elf32_hdr_t*>(undeclared.data())->shnum=6;
     auto* sections=reinterpret_cast<elf32_shdr_t*>(undeclared.data()+64);
     sections[5].type=SHT_SYMTAB;sections[5].offset=640;sections[5].size=2*sizeof(elf32_sym_t);sections[5].link=4;
     strcpy(reinterpret_cast<char*>(undeclared.data()+560),"memcpy");
     auto* symbols=reinterpret_cast<elf32_sym_t*>(undeclared.data()+640);symbols[1].name=48;symbols[1].info=(STB_GLOBAL<<4)|STT_FUNC;
   }
   changedPolicy=policy;SHA256(undeclared.data(),undeclared.size(),changedPolicy.sha256);
   assert(!admitElf(undeclared.data(),undeclared.size(),ElfRole::Driver,&changedPolicy));
 }
 const char* const extraImports[]={"memcpy","xTaskGetTickCount"};changedPolicy=policy;
 changedPolicy.imports=extraImports;changedPolicy.importCount=2;
 assert(!admitElf(nativeElf.data(),nativeElf.size(),ElfRole::Driver,&changedPolicy));
 // Ordinary and ABI-0 policies refuse diagnostics; explicit ABI 1 is independently checked.
 for(const char* name:{"printf","puts","putchar"}){
   assert(!allowedImport(name));auto stdioElf=elf(name,"t5_driver_get");
   assert(!admitElf(stdioElf.data(),stdioElf.size(),ElfRole::Driver));
   changedPolicy=policy;changedPolicy.imports=&name;SHA256(stdioElf.data(),stdioElf.size(),changedPolicy.sha256);
   assert(!admitElf(stdioElf.data(),stdioElf.size(),ElfRole::Driver,&changedPolicy));
   changedPolicy.diagnosticAbi=1;
   assert(admitElf(stdioElf.data(),stdioElf.size(),ElfRole::Driver,&changedPolicy));
   assert(!admitElf(stdioElf.data(),stdioElf.size(),ElfRole::Application,&changedPolicy));
   assert(!admitElf(stdioElf.data(),stdioElf.size(),ElfRole::Either,&changedPolicy));
   unavailableImport=name;assert(!admitElf(stdioElf.data(),stdioElf.size(),ElfRole::Driver,&changedPolicy));unavailableImport=nullptr;
   changedPolicy.diagnosticAbi=2;assert(!admitElf(stdioElf.data(),stdioElf.size(),ElfRole::Driver,&changedPolicy));
 }
 Files base{
   {"board.json",text(R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})")},
   {"boot.json",text(R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"driver.json"}],"app_capabilities":[{"manifest":"app.json","grants":[]}]})")},
   {"app.json",text(R"({"type":"application","id":"native-bank-default","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[]})")},
   {"driver.json",text(R"({"type":"driver","id":"native-bank-provider","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[],"provides":[{"capability":"test.native-bank","api":1}]})")},
   {"default.elf",elf("memcpy")},{"driver.elf",nativeElf}};
 const bool selected=selectedNativeProviderPoliciesV1().count==1;
 assert(selectedNativeProviderPoliciesV1().count==(selected?1u:0u));
 const std::string provisionRoot=std::string(directory)+"/provision";
 static ProviderAdmissionSources sources({{provisionRoot,provisionRoot},{"/bootfs",std::string(directory)+"/bootfs"},{"/updatefs",std::string(directory)+"/updatefs"}});fileSources=sources.sources();
 for(unsigned scenario=0;scenario<13;++scenario){
   auto files=base;
   if(scenario==1)files["default.elf"]=appElf;
   if(scenario==2)files["extra.elf"]=nativeElf;
   if(scenario==3)files["driver.elf"][400]^=1;
   if(scenario==4)files["driver.elf"]=elf("vTaskDelay","t5_driver_get");
   if((scenario>=5 && scenario<=8) || scenario>=10){
     JsonDocument manifest;assert(deserializeJson(manifest,files["driver.json"].data(),files["driver.json"].size())==DeserializationError::Ok);
     if(scenario==5)manifest["id"]="forged-native-provider";
     if(scenario==6)manifest["version"]="1.0.1";
     if(scenario==7)manifest["provides"][0]["capability"]="test.forged";
     if(scenario==8){manifest["file_name"]="alias.elf";files["alias.elf"]=files["driver.elf"];files.erase("driver.elf");}
     if(scenario==10)manifest["provides"][0]["api"]=2;
     if(scenario==11){auto req=manifest["requires"].as<JsonArray>().add<JsonObject>();req["capability"]="platform.clock";req["api"]=1;}
     if(scenario==12){auto req=manifest["requires"].as<JsonArray>().add<JsonObject>();req["capability"]="platform.bank-store";req["api"]=1;}
     std::string value;serializeJson(manifest,value);files["driver.json"]={value.begin(),value.end()};
   }
   if(scenario==9)unavailableImport="xTaskGetTickCount";
   save(provisionRoot,files);auto profile=std::make_unique<RiscProvision::Profile>();
   for(const auto& item:files){auto& f=profile->files[profile->count++];strcpy(f.path,item.first.c_str());f.bytes=item.second.size();SHA256(item.second.data(),item.second.size(),f.sha256);}
   ProvisionState state({},admissionHardware(),nullptr,nullptr);provisionState=&state;provisionProfile=profile.get();
   assert(admitProvisionedStore(provisionRoot.c_str(),*profile)==(selected && scenario==0));
   assert(!candidateCpu && !provisionReadRetained);provisionState=nullptr;provisionProfile=nullptr;unavailableImport=nullptr;
 }
 // The graph owns the admitted selection, including requirement metadata.
 // Caller changes after prepare cannot replace that authority, and a changed
 // on-disk file still fails the production staged-file callback afterward.
 for(unsigned mismatch=0;mismatch<3;++mismatch){
   auto files=base;JsonDocument manifest;
   assert(deserializeJson(manifest,files["driver.json"].data(),files["driver.json"].size())==DeserializationError::Ok);
   auto required=manifest["requires"].as<JsonArray>().add<JsonObject>();required["capability"]="platform.clock";required["api"]=1;
   std::string value;serializeJson(manifest,value);files["driver.json"]={value.begin(),value.end()};save(provisionRoot,files);
   char requirementName[32]="platform.clock",importName[32]="xTaskGetTickCount",id[96]="native-bank-provider";
   char path[32]="driver.elf",version[32]="1.0.0",capability[32]="test.native-bank";
   const char* imports[]={importName};NativeProviderRequirementV1 requirement{requirementName,1};
   auto mutablePolicy=policy;mutablePolicy.driverId=id;mutablePolicy.relativeElfPath=path;mutablePolicy.version=version;
   mutablePolicy.capability=capability;mutablePolicy.imports=imports;mutablePolicy.requirements=&requirement;mutablePolicy.requirementCount=1;
   if(mismatch==1)strcpy(requirementName,"platform.bank-store");
   if(mismatch==2)requirement.api=2;
   RiscBoot::Port ownedPort{own,nullptr,nullptr,nullptr};ownedPort.nativeProviders={&mutablePolicy,1};
   auto candidate=std::make_unique<RiscBoot::Runtime>(ownedPort);assert(sources.bind(*candidate));static const uint32_t clockTable[]={1,8};
   assert(candidate->registerPlatform("platform.clock",1,RiscBoot::Runtime::Scope::Global,0,clockTable));
   assert(candidate->prepare(provisionRoot.c_str())==(mismatch==0));
   if(mismatch)continue;
   const std::string image=provisionRoot+"/driver.elf";
   const auto* admitted=candidate->nativeProviderPolicyForImage(image.c_str());assert(admitted && admitted!=&mutablePolicy);
   memset(mutablePolicy.sha256,0,32);strcpy(id,"changed");strcpy(path,"other.elf");strcpy(version,"9.0.0");
   strcpy(capability,"test.changed");strcpy(importName,"vTaskDelay");strcpy(requirementName,"platform.bank-store");requirement.api=2;
   assert(!strcmp(admitted->driverId,policy.driverId) && !strcmp(admitted->version,policy.version));
   assert(!strcmp(admitted->relativeElfPath,policy.relativeElfPath) && !strcmp(admitted->capability,policy.capability));
   assert(!strcmp(admitted->imports[0],policy.imports[0]) && !memcmp(admitted->sha256,policy.sha256,32));
   assert(admitted->requirementCount==1 && !strcmp(admitted->requirements[0].capability,"platform.clock") && admitted->requirements[0].api==1);
   assert(admitFile(candidate.get(),image.c_str(),true));
   files["driver.elf"][400]^=1;save(provisionRoot,files);
   assert(!admitFile(candidate.get(),image.c_str(),true));
 }
 // Runtime preparation and the bank's second read retain copied diagnostic ABI
 // authority after caller mutation, and still reject a changed selected image.
 {
   auto files=base;files["driver.elf"]=elf("puts","t5_driver_get");save(provisionRoot,files);
   const char* diagnosticImport="puts";auto mutablePolicy=policy;mutablePolicy.imports=&diagnosticImport;mutablePolicy.diagnosticAbi=1;
   SHA256(files["driver.elf"].data(),files["driver.elf"].size(),mutablePolicy.sha256);
   RiscBoot::Port ownedPort{own,nullptr,nullptr,nullptr};ownedPort.nativeProviders={&mutablePolicy,1};
   auto candidate=std::make_unique<RiscBoot::Runtime>(ownedPort);assert(sources.bind(*candidate));assert(candidate->prepare(provisionRoot.c_str()));
   const std::string image=provisionRoot+"/driver.elf";
   const auto* admitted=candidate->nativeProviderPolicyForImage(image.c_str());assert(admitted && admitted->diagnosticAbi==1);
   mutablePolicy.diagnosticAbi=0;diagnosticImport="xTaskGetTickCount";
   assert(admitted->diagnosticAbi==1 && !strcmp(admitted->imports[0],"puts") && admitFile(candidate.get(),image.c_str(),true));
   files["driver.elf"][400]^=1;save(provisionRoot,files);assert(!admitFile(candidate.get(),image.c_str(),true));
 }
 // A staged candidate has a different absolute path from the running Runtime.
 // Actual validateStore must hand its callback the candidate, not the source.
 fsRoot=directory;save(fsRoot+"/bootfs",base);save(fsRoot+"/updatefs",base);
 cohortImage={};cohortImage.struct_size=sizeof(cohortImage);cohortImage.store_abi=RiscUpdate::StoreAbi;
 cohortImage.store_size=RiscUpdate::StoreBytes;cohortImage.firmware_size=imageSize;
 strcpy(cohortImage.product,"twatch-s3");strcpy(cohortImage.version,"1.0.2");strcpy(cohortImage.runtime_version,RISC_BUILD_VERSION);
 strcpy(cohortImage.source_repo,"owner/watch");memset(cohortImage.source_revision,'b',40);cohortImage.firmware_sha256[0]=1;
 identityNative("/bootfs","1.0.1",RISC_BUILD_VERSION,std::string(40,'a').c_str());
 identityNative("/updatefs","1.0.2",RISC_BUILD_VERSION,cohortImage.source_revision);
 RiscBoot::Port port{own,nullptr,[](uint32_t){},nullptr};port.nativeProviders=selectedNativeProviderPoliciesV1();
 auto source=std::make_unique<RiscBoot::Runtime>(port);assert(sources.bind(*source));assert(source->prepare("/bootfs"));runtime=source.get();
 assert((source->nativeProviderPolicyForImage("/bootfs/driver.elf")!=nullptr)==selected);
 assert(!source->nativeProviderPolicyForImage("/updatefs/driver.elf"));
 assert(!source->nativeProviderPolicyForImage("/bootfs/default.elf"));
 scratch->cohort=cohortImage;replacingCohort=true;
 assert(validateStore(nullptr,1)==selected);assert(cleanup(nullptr));
 if(selected){
   std::ofstream out(fsRoot+"/updatefs/default.elf",std::ios::binary);out.write(reinterpret_cast<const char*>(appElf.data()),appElf.size());out.close();
   assert(!validateStore(nullptr,1));assert(cleanup(nullptr));
 }
 runtime=nullptr;source.reset();
 // Port authority must be copied when the running Runtime is prepared, even
 // for a policy whose provider is first selected by a later staged cohort.
 for(bool selectedInSource:{false,true}){
   auto sourceFiles=base;
   if(!selectedInSource){
     JsonDocument boot;assert(deserializeJson(boot,sourceFiles["boot.json"].data(),sourceFiles["boot.json"].size())==DeserializationError::Ok);
     boot["drivers"].clear();boot["drivers"].to<JsonArray>();std::string value;serializeJson(boot,value);
     sourceFiles["boot.json"]={value.begin(),value.end()};sourceFiles.erase("driver.json");sourceFiles.erase("driver.elf");
   }
   save(fsRoot+"/bootfs",sourceFiles);identityNative("/bootfs","1.0.1",RISC_BUILD_VERSION,std::string(40,'a').c_str());
   char id[96]="native-bank-provider",version[32]="1.0.0",capability[32]="test.native-bank",import[32]="xTaskGetTickCount";
   const char* imports[]={import};auto mutablePolicy=policy;
   mutablePolicy.driverId=id;mutablePolicy.version=version;mutablePolicy.capability=capability;mutablePolicy.imports=imports;
   RiscBoot::Port mutablePort{own,nullptr,[](uint32_t){},nullptr};mutablePort.nativeProviders={&mutablePolicy,1};
   source=std::make_unique<RiscBoot::Runtime>(mutablePort);assert(sources.bind(*source));assert(source->prepare("/bootfs"));runtime=source.get();
   assert(bool(source->nativeProviderPolicyForImage("/bootfs/driver.elf"))==selectedInSource);
   auto changedFiles=base;changedFiles["driver.elf"]=elf("vTaskDelay","t5_driver_get");
   strcpy(id,"replaced-provider");strcpy(version,"2.0.0");strcpy(capability,"test.replaced");strcpy(import,"vTaskDelay");
   SHA256(changedFiles["driver.elf"].data(),changedFiles["driver.elf"].size(),mutablePolicy.sha256);
   assert(nativeProviderPolicyValid(mutablePolicy) && nativeProviderImageValid(mutablePolicy,changedFiles["driver.elf"].data(),changedFiles["driver.elf"].size()));
   JsonDocument manifest;assert(deserializeJson(manifest,changedFiles["driver.json"].data(),changedFiles["driver.json"].size())==DeserializationError::Ok);
   manifest["id"]=id;manifest["version"]=version;manifest["provides"][0]["capability"]=capability;
   std::string value;serializeJson(manifest,value);changedFiles["driver.json"]={value.begin(),value.end()};
   save(fsRoot+"/updatefs",changedFiles);identityNative("/updatefs","1.0.2",RISC_BUILD_VERSION,cohortImage.source_revision);
   // Leave every original caller mutation in place for both admissions.
   assert(!validateStore(nullptr,1));assert(cleanup(nullptr));
   save(fsRoot+"/updatefs",base);identityNative("/updatefs","1.0.2",RISC_BUILD_VERSION,cohortImage.source_revision);
   assert(validateStore(nullptr,1));assert(cleanup(nullptr));
   runtime=nullptr;source.reset();
 }
 // Terminal custody cases each run in a separate process. Preserve the exact
 // candidate, read buffer, provider context/token and CPU without simulating recovery.
 if(custodyLoss){
   save(fsRoot+"/bootfs",base);save(fsRoot+"/updatefs",base);
   if(!provisioningCustody){
     identityNative("/bootfs","1.0.1",RISC_BUILD_VERSION,std::string(40,'a').c_str());
     identityNative("/updatefs","1.0.2",RISC_BUILD_VERSION,cohortImage.source_revision);
     RiscBoot::Port custodyPort{own,nullptr,[](uint32_t){},nullptr};custodyPort.nativeProviders={&policy,1};
     custodyPort.providerStorageSafe=[](){return providerReadStorageSafe;};
     source=std::make_unique<RiscBoot::Runtime>(custodyPort);assert(sources.bind(*source));assert(source->prepare("/bootfs"));runtime=source.get();
   }else{
     assert(selected && (custodyLoss==1 || secondRead));auto profile=std::make_unique<RiscProvision::Profile>();
     for(const auto& item:base){auto& f=profile->files[profile->count++];strcpy(f.path,item.first.c_str());f.bytes=item.second.size();SHA256(item.second.data(),item.second.size(),f.sha256);}
     provisionProfile=profile.release();provisionState=new ProvisionState({},admissionHardware(),nullptr,nullptr);mounted=true;
   }
   providerCustodyLoss=custodyLoss;providerCustodySkipReads=secondRead?1:0;
   providerCustodyReadPath="/updatefs/driver.elf";providerRetainedRead=0;providerRetainedReadContext=nullptr;
   assert(!(provisioningCustody?admitProvisionedStore("/updatefs",*provisionProfile):validateStore(nullptr,1)));
   assert(providerRetainedRead && providerRetainedReadContext && providerCustodyReadPath.empty());
   assert(sources.owns("/updatefs",providerRetainedReadContext,providerRetainedRead));
   assert(retainedAdmissionRuntime && retainedAdmissionRuntime->nativeProviderReadRetained()==!secondRead);
   if(secondRead)assert(retainedAdmissionFile==providerRetainedRead && retainedAdmissionBytes==providerRetainedReadBytes);
   assert(provisionReadRetained && mounted && !cleanup(nullptr) && !exitSafe());
   unsigned openDescriptors=0;for(const auto& descriptor:metadataDescriptors)if(descriptor.second==fsRoot+"/updatefs/driver.elf")++openDescriptors;
   assert(openDescriptors==(custodyLoss==4?0u:1u) && !providerCustodyEofProbes);
   assert(providerCloseCalls==(custodyLoss==4?1u:0u));
   const unsigned reads=providerReadCalls,closes=providerCloseCalls;uint8_t extra=0;uint32_t actual=99;
   auto retainedSource=retainedAdmissionRuntime->imageSource();
   assert(retainedSource.read(providerRetainedRead,&extra,1,&actual)==RISC_FILES_RETAINED && !actual);
   assert(retainedSource.close(providerRetainedRead)==RISC_FILES_RETAINED);
   assert(providerReadCalls==reads && providerCloseCalls==closes);
   assert(failedAdmissionCloses==(custodyLoss==4?1u:0u));
   assert(bool(candidateCpu)==provisioningCustody);
   assert(providerRetainedReadBytes && !memcmp(providerRetainedReadBytes,nativeElf.data(),nativeElf.size()));
   assert(!hardwareCalls && !writes && !selectorCalls && !restarts);
   source.release(); // The terminal native session stays alive to process exit.
   std::cout<<"Production native provider custody: "<<(provisioningCustody?"provisioning":"staged")
     <<" read="<<(secondRead?"second":"prepare")<<" loss="<<custodyLoss
     <<" retains candidate/provider-token/buffer"<<(provisioningCustody?"/CPU":"")
     <<", refuses cleanup, zero unauthorized closes/selection PASS\n";
   return;
 }
 nativeAdmissionBudgets(provisionRoot,sources);
 fsRoot.clear();delete scratch;scratch=nullptr;
 assert(!hardwareCalls && !writes && !selectorCalls && !restarts);
 std::cout<<"Production native provider bank admission: selected="<<selected
   <<" exact bytes/imports, application/extra isolation, provisioning and immutable staged authority PASS; zero mapping/hardware/selection\n";
}
