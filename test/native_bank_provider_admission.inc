// Production NativeBankStore admission with native I/O/resolver test doubles.
// No ELF is mapped and no provider or peripheral callback may execute here.
#include "native_bank_provider_selection.h"
static std::string providerCustodyReadPath;
static unsigned providerCustodyLoss=0;
static unsigned providerCustodySkipReads=0,providerCustodyEofProbes=0;
static bool providerReadStorageSafe=true;
static FILE* providerRetainedRead=nullptr;
static const uint8_t* providerRetainedReadBytes=nullptr;
extern "C" size_t __real_fread(void*,size_t,size_t,FILE*);
extern "C" size_t __wrap_fread(void* bytes,size_t size,size_t count,FILE* file){
 const size_t result=__real_fread(bytes,size,count,file);
 const auto opened=metadataStreams.find(file);
 if(result && opened!=metadataStreams.end() && opened->second.first==providerCustodyReadPath){
   if(providerCustodySkipReads){--providerCustodySkipReads;return result;}
   providerCustodyReadPath.clear();providerRetainedRead=file;providerRetainedReadBytes=static_cast<const uint8_t*>(bytes);
   if(providerCustodyLoss==1)ownerEnabled=false;
   if(providerCustodyLoss==2)providerReadStorageSafe=false;
   if(providerCustodyLoss==3)operationEnabled=false;
   if(providerCustodyLoss==4)failNextNativeClose=true;
 }
 return result;
}
extern "C" int __real_fgetc(FILE*);
extern "C" int __wrap_fgetc(FILE* file){
 if(file==providerRetainedRead && providerCustodyLoss && providerCustodyLoss!=4)++providerCustodyEofProbes;
 return __real_fgetc(file);
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
 // The real provisioning admission path must inspect every selected provider
 // at the compiled bound and reject an additional provider before activation.
 // These ordinary images require no optional native policy.
 for(unsigned count:{unsigned(RiscLimits::Providers),unsigned(RiscLimits::Providers+1)}){
   auto files=base;files.erase("driver.json");files.erase("driver.elf");
   JsonDocument boot;assert(!deserializeJson(boot,files["boot.json"].data(),files["boot.json"].size()));
   auto providers=boot["drivers"].to<JsonArray>();
   for(unsigned i=0;i<count;++i){
     const std::string id="capacity-"+std::to_string(i);JsonDocument manifest;
     assert(!deserializeJson(manifest,base.at("driver.json").data(),base.at("driver.json").size()));
     manifest["id"]=id;manifest["file_name"]=id+".elf";manifest["provides"][0]["capability"]="test."+id;
     std::string value;serializeJson(manifest,value);files[id+".json"]={value.begin(),value.end()};
     files[id+".elf"]=elf("memcpy","t5_driver_get");providers.add<JsonObject>()["manifest"]=id+".json";
   }
   std::string value;serializeJson(boot,value);files["boot.json"]={value.begin(),value.end()};
   save(provisionRoot,files);auto profile=std::make_unique<RiscProvision::Profile>();
   for(const auto& item:files){auto& f=profile->files[profile->count++];strcpy(f.path,item.first.c_str());f.bytes=item.second.size();SHA256(item.second.data(),item.second.size(),f.sha256);}
   ProvisionState state({},admissionHardware(),nullptr,nullptr);provisionState=&state;provisionProfile=profile.get();
   assert(admitProvisionedStore(provisionRoot.c_str(),*profile)==(count==RiscLimits::Providers));
   assert(!candidateCpu && !provisionReadRetained && !hardwareCalls && !writes && !selectorCalls && !restarts);
   provisionState=nullptr;provisionProfile=nullptr;
 }
 std::cout<<"Production provider capacity: "<<RiscLimits::Providers<<" admitted; "
   <<RiscLimits::Providers+1<<" rejected, zero mapping/hardware/selection PASS\n";
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
   auto candidate=std::make_unique<RiscBoot::Runtime>(ownedPort);static const uint32_t clockTable[]={1,8};
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
   auto candidate=std::make_unique<RiscBoot::Runtime>(ownedPort);assert(candidate->prepare(provisionRoot.c_str()));
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
 auto source=std::make_unique<RiscBoot::Runtime>(port);assert(source->prepare("/bootfs"));runtime=source.get();
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
   source=std::make_unique<RiscBoot::Runtime>(mutablePort);assert(source->prepare("/bootfs"));runtime=source.get();
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
 // candidate, read buffer, FILE and CPU instead of simulating native recovery.
 if(custodyLoss){
   save(fsRoot+"/bootfs",base);save(fsRoot+"/updatefs",base);
   if(!provisioningCustody){
     identityNative("/bootfs","1.0.1",RISC_BUILD_VERSION,std::string(40,'a').c_str());
     identityNative("/updatefs","1.0.2",RISC_BUILD_VERSION,cohortImage.source_revision);
     RiscBoot::Port custodyPort{own,nullptr,[](uint32_t){},nullptr};custodyPort.nativeProviders={&policy,1};
     custodyPort.providerStorageSafe=[](){return providerReadStorageSafe;};
     source=std::make_unique<RiscBoot::Runtime>(custodyPort);assert(source->prepare("/bootfs"));runtime=source.get();
   }else{
     assert(selected && (custodyLoss==1 || secondRead));auto profile=std::make_unique<RiscProvision::Profile>();
     for(const auto& item:base){auto& f=profile->files[profile->count++];strcpy(f.path,item.first.c_str());f.bytes=item.second.size();SHA256(item.second.data(),item.second.size(),f.sha256);}
     provisionProfile=profile.release();provisionState=new ProvisionState({},admissionHardware(),nullptr,nullptr);mounted=true;
   }
   providerCustodyLoss=custodyLoss;providerCustodySkipReads=secondRead?1:0;
   providerCustodyReadPath=fsRoot+"/updatefs/driver.elf";providerRetainedRead=nullptr;
   assert(!(provisioningCustody?admitProvisionedStore("/updatefs",*provisionProfile):validateStore(nullptr,1)));
   assert(providerRetainedRead && providerCustodyReadPath.empty());
   assert(retainedAdmissionRuntime && retainedAdmissionRuntime->nativeProviderReadRetained()==!secondRead);
   if(secondRead)assert(retainedAdmissionFile==providerRetainedRead && retainedAdmissionBytes==providerRetainedReadBytes);
   assert(provisionReadRetained && mounted && !cleanup(nullptr) && !exitSafe());
   assert(metadataStreams.count(providerRetainedRead)==(custodyLoss==4?0u:1u) && !providerCustodyEofProbes);
   assert(failedAdmissionCloses==(custodyLoss==4?1u:0u));
   assert(bool(candidateCpu)==provisioningCustody);
   assert(providerRetainedReadBytes && !memcmp(providerRetainedReadBytes,nativeElf.data(),nativeElf.size()));
   assert(!hardwareCalls && !writes && !selectorCalls && !restarts);
   source.release(); // The terminal native session stays alive to process exit.
   std::cout<<"Production native provider custody: "<<(provisioningCustody?"provisioning":"staged")
     <<" read="<<(secondRead?"second":"prepare")<<" loss="<<custodyLoss
     <<" retains candidate/FILE/buffer"<<(provisioningCustody?"/CPU":"")
     <<", refuses cleanup, zero unauthorized closes/selection PASS\n";
   return;
 }
 fsRoot.clear();delete scratch;scratch=nullptr;
 assert(!hardwareCalls && !writes && !selectorCalls && !restarts);
 std::cout<<"Production native provider bank admission: selected="<<selected
   <<" exact bytes/imports, application/extra isolation, provisioning and immutable staged authority PASS; zero mapping/hardware/selection\n";
}
