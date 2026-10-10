// Run the actual NativeBankStore::validateStore, with only its flash/VFS and
// backend I/O replaced by the existing native harness. No ELF is executed.
#include "native_bank_app_data_export_selection.h"
static void nativeAppDataExportAdmission(const char* directory,bool closeFailure=false) {
  using namespace RiscBankStore;
  namespace fs=std::filesystem;
  const auto expected=RiscUpdate::nativeBankExportFixturePolicy();
  const auto* selected=risc_native_app_data_export_policy_v1();
  const bool enabled=selected->count!=0;
  assert(selected->count==(enabled?1u:0u));
  if(enabled)assert(selected->delegations[0].count==9);
  unsigned backendCalls=0;
  RiscBoot::AppDataBackend backend{&backendCalls,
    [](void* p,uint32_t,const char*,uint32_t*,uint64_t*){++*static_cast<unsigned*>(p);return int32_t(RISC_APP_DATA_IO);},
    [](void* p,uint32_t,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*){++*static_cast<unsigned*>(p);return int32_t(RISC_APP_DATA_IO);},
    [](void* p,uint32_t,const char*,uint64_t,const void*,uint32_t){++*static_cast<unsigned*>(p);return int32_t(RISC_APP_DATA_IO);},
    [](void*){return true;}};
  RiscBoot::Port port{own,nullptr,[](uint32_t){},nullptr};port.appData=&backend;
  auto load=[](const std::string& path) {
    std::ifstream file(fsRoot+path);std::string bytes((std::istreambuf_iterator<char>(file)),{});
    JsonDocument doc;assert(deserializeJson(doc,bytes)==DeserializationError::Ok);return doc;
  };
  auto save=[](const std::string& path,const JsonDocument& doc) {
    std::string text;serializeJson(doc,text);saveNative(path,text);
  };
  auto identity=[&](const char* root,const RiscUpdate::NativeAppDataExportCohortV1& coordinate) {
    JsonDocument doc;doc["schema"]="riscrte.cohort";doc["schema_version"]=1;
    doc["product"]=coordinate.product;doc["version"]=coordinate.version;doc["runtime_version"]=coordinate.runtimeVersion;
    doc["source_repo"]=coordinate.sourceRepo;doc["source_revision"]=coordinate.sourceRevision;
    doc["layout"]=RiscUpdate::Layout;doc["store_abi"]=RiscUpdate::StoreAbi;
    doc["firmware_size"]=8192;doc["firmware_sha256"]=std::string(64,'a');save(std::string(root)+"/cohort.json",doc);
  };
  auto operational=[](unsigned i) {return i?"test.operation-"+std::to_string(i):std::string("storage.installed-files");};
  auto makeStore=[&](const char* root,bool exportMap,const RiscUpdate::NativeAppDataExportCohortV1& coordinate) {
    fs::remove_all(fsRoot+root);fs::create_directories(fsRoot+root);
    saveNative(std::string(root)+"/board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"fixture","revision":"rev1","buses":[],"devices":[]})");
    JsonDocument boot;boot["board"]="board.json";boot["default_app"]="browser.elf";boot["drivers"].to<JsonArray>();
    auto policies=boot["app_capabilities"].to<JsonArray>();
    const char* ids[]={"export-browser","owner-a","owner-b","owner-c","owner-d"};
    const uint32_t namespaces[]={0,41,52,63,74};
    for(unsigned app=0;app<5;++app) {
      std::string stem=app?ids[app]:"browser";
      JsonDocument manifest;manifest["type"]="application";manifest["id"]=ids[app];manifest["version"]="1.0.0";
      manifest["architecture"]="xtensa-esp32s3";manifest["file_name"]=stem+".elf";manifest["entry"]="app_main";
      auto reqs=manifest["requires"].to<JsonArray>();auto policy=policies.add<JsonObject>();policy["manifest"]=stem+".json";
      auto grants=policy["grants"].to<JsonArray>();
      auto grant=[&](const char* capability,uint32_t instance) {
        auto requirement=reqs.add<JsonObject>();requirement["capability"]=capability;requirement["api"]=1;
        auto g=grants.add<JsonObject>();g["capability"]=capability;g["api"]=1;g["instance_id"]=instance;
      };
      if(app)grant(RISC_APP_DATA_CAPABILITY,namespaces[app]);
      else {
        for(unsigned i=0;i<12;++i)grant(operational(i).c_str(),0);
        if(exportMap) {
          grant(RISC_APP_DATA_EXPORT_CAPABILITY,0);auto mapping=policy["app_data_export"].to<JsonObject>();
          mapping["label"]=expected.delegations[0].label;auto files=mapping["files"].to<JsonArray>();
          for(size_t i=0;i<expected.delegations[0].count;++i) {
            const auto& entry=expected.delegations[0].entries[i];auto f=files.add<JsonObject>();
            f["owner"]=entry.owner;f["namespace"]=entry.nameSpace;f["name"]=entry.name;
            f["path"]=entry.path;f["access"]=entry.writable?"read-write":"read";
          }
        }
      }
      save(std::string(root)+"/"+stem+".json",manifest);saveElf(std::string(root)+"/"+stem+".elf",false);
    }
    for(unsigned i=1;i<14;++i) {
      const std::string stem="provider-"+std::to_string(i);
      auto d=boot["drivers"].as<JsonArray>().add<JsonObject>();d["manifest"]=stem+".json";
      JsonDocument manifest;manifest["type"]="driver";manifest["id"]=stem;manifest["version"]="1.0.0";
      manifest["driver_abi"]=2;manifest["architecture"]="xtensa-esp32s3";manifest["file_name"]=stem+".elf";
      manifest["requires"].to<JsonArray>();auto provided=manifest["provides"].to<JsonArray>().add<JsonObject>();
      provided["capability"]=operational(i);provided["api"]=1;
      save(std::string(root)+"/"+stem+".json",manifest);saveElf(std::string(root)+"/"+stem+".elf",true);
    }
    save(std::string(root)+"/boot.json",boot);identity(root,coordinate);
  };
  fsRoot=directory;fs::create_directories(fsRoot);
  isOwner=own;operationIsSafe=safe;prepared=confirmed=true;activeBank=0;scratch=new Scratch;
  for(unsigned b=0;b<2;++b)for(unsigned r=0;r<2;++r)parts[b][r]=&table[b*2+r];
  auto source=[&](bool exportMap,const RiscUpdate::NativeAppDataExportCohortV1& coordinate) {
    runtime=nullptr;makeStore("/bootfs",exportMap,coordinate);
    auto instance=std::make_unique<RiscBoot::Runtime>(port);
    assert(instance->prepare("/bootfs"));runtime=instance.get();return instance;
  };
  auto check=[&](bool accepted) {
    RiscUpdate::CohortIdentity candidate;assert(RiscUpdate::readCohort("/updatefs",candidate));
    cohortImage={};cohortImage.struct_size=sizeof(cohortImage);cohortImage.store_abi=RiscUpdate::StoreAbi;cohortImage.store_size=RiscUpdate::StoreBytes;
    strcpy(cohortImage.product,candidate.product.product);strcpy(cohortImage.version,candidate.product.version);
    strcpy(cohortImage.source_repo,candidate.product.source_repo);strcpy(cohortImage.source_revision,candidate.product.source_revision);
    strcpy(cohortImage.runtime_version,candidate.runtimeVersion);cohortImage.firmware_size=candidate.firmwareSize;
    memcpy(cohortImage.firmware_sha256,candidate.firmwareSha,32);scratch->cohort=cohortImage;replacingCohort=true;
    const bool result=validateStore(nullptr,1);assert(result==accepted);assert(cleanup(nullptr));
    assert(!backendCalls && !hardwareCalls && !writes && !selectorCalls && !restarts);
  };
  auto current=source(false,expected.from);
  makeStore("/updatefs",true,expected.to);check(enabled);check(enabled);
  {
    auto boot=load("/updatefs/boot.json");const auto exact=boot;
    auto entries=boot["app_capabilities"][0]["app_data_export"]["files"].as<JsonArray>();
    entries[0].set(exact["app_capabilities"][0]["app_data_export"]["files"][8]);
    entries[8].set(exact["app_capabilities"][0]["app_data_export"]["files"][0]);
    save("/updatefs/boot.json",boot);check(enabled);save("/updatefs/boot.json",exact);
  }
  // Full source and target binding: each coordinate is independently checked.
  const char* fields[]={"product","version","source_repo","source_revision","runtime_version"};
  const char* wrong[]={"other-product","9.0.9","other/repository","cccccccccccccccccccccccccccccccccccccccc","9.0.9"};
  for(const char* root:{"/bootfs","/updatefs"})for(unsigned field=0;field<5;++field) {
    const auto path=std::string(root)+"/cohort.json";auto original=load(path),changed=original;
    changed[fields[field]]=wrong[field];save(path,changed);check(false);save(path,original);
  }
  const auto exactBoot=load("/updatefs/boot.json");
  for(unsigned bad=0;bad<9;++bad) {
    auto boot=exactBoot;auto mapping=boot["app_capabilities"][0]["app_data_export"];auto entry=mapping["files"][0];
    if(bad==0)entry["owner"]="owner-b";
    if(bad==1)entry["namespace"]=52;
    if(bad==2)entry["name"]="different.bin";
    if(bad==3)entry["path"]="/different/record.bin";
    if(bad==4)entry["access"]="read";
    if(bad==5)mapping["label"]="Different label";
    if(bad==6)mapping["files"].as<JsonArray>().remove(8);
    if(bad==7){auto extra=mapping["files"].as<JsonArray>().add<JsonObject>();extra.set(entry);extra["name"]="extra.bin";extra["path"]="/first/extra.bin";}
    if(bad==8)boot["app_capabilities"][1]["grants"][0]["instance_id"]=42;
    save("/updatefs/boot.json",boot);check(false);
  }
  save("/updatefs/boot.json",exactBoot);
  // All twelve operational grants survive; no new operational privilege can
  // ride along with the sole selected export grant.
  const auto exactManifest=load("/updatefs/browser.json");
  {
    // A different consumer with the same map cannot borrow this delegation.
    auto boot=exactBoot,manifest=exactManifest;manifest["id"]="unselected-browser";
    for(unsigned i=0;i<12;++i){boot["app_capabilities"][0]["grants"].as<JsonArray>().remove(0);manifest["requires"].as<JsonArray>().remove(0);}
    save("/updatefs/boot.json",boot);save("/updatefs/browser.json",manifest);check(false);
    save("/updatefs/browser.json",exactManifest);
    // Nor can one selected delegation cover a second export consumer.
    boot=exactBoot;auto owner=load("/updatefs/owner-a.json");
    auto req=owner["requires"].as<JsonArray>().add<JsonObject>();req["capability"]=RISC_APP_DATA_EXPORT_CAPABILITY;req["api"]=1;
    auto grant=boot["app_capabilities"][1]["grants"].as<JsonArray>().add<JsonObject>();grant["capability"]=RISC_APP_DATA_EXPORT_CAPABILITY;grant["api"]=1;grant["instance_id"]=0;
    boot["app_capabilities"][1]["app_data_export"].set(exactBoot["app_capabilities"][0]["app_data_export"]);
    save("/updatefs/boot.json",boot);save("/updatefs/owner-a.json",owner);check(false);
    makeStore("/updatefs",true,expected.to);
  }
  for(unsigned drop=0;drop<12;++drop) {
    auto boot=exactBoot,manifest=exactManifest;
    boot["app_capabilities"][0]["grants"].as<JsonArray>().remove(drop);manifest["requires"].as<JsonArray>().remove(drop);
    save("/updatefs/boot.json",boot);save("/updatefs/browser.json",manifest);check(false);
  }
  for(unsigned extra=12;extra<14;++extra) {
    auto boot=exactBoot,manifest=exactManifest;auto g=boot["app_capabilities"][0]["grants"].as<JsonArray>().add<JsonObject>();
    g["capability"]=operational(extra);g["api"]=1;g["instance_id"]=0;
    auto req=manifest["requires"].as<JsonArray>().add<JsonObject>();req["capability"]=operational(extra);req["api"]=1;
    save("/updatefs/boot.json",boot);save("/updatefs/browser.json",manifest);check(false);
  }
  // Selection is inert for unchanged policy, including an unrelated cohort.
  makeStore("/updatefs",false,expected.to);check(true);
  auto unrelated=expected.to;unrelated.version="3.0.0";unrelated.sourceRepo="other/product";
  makeStore("/updatefs",false,unrelated);check(true);
  // Simulate the installed export target and repeated/future unchanged maps.
  current=source(true,expected.to);makeStore("/updatefs",true,expected.to);check(true);check(true);
  unrelated=expected.to;unrelated.version="2.0.3";unrelated.sourceRevision="cccccccccccccccccccccccccccccccccccccccc";
  makeStore("/updatefs",true,unrelated);check(true);
  auto changed=load("/updatefs/boot.json");changed["app_capabilities"][0]["app_data_export"]["label"]="Changed";
  save("/updatefs/boot.json",changed);check(false);
  makeStore("/updatefs",false,unrelated);check(false);
  if(closeFailure && enabled) {
    current=source(false,expected.from);makeStore("/updatefs",true,expected.to);
    RiscUpdate::CohortIdentity target;assert(RiscUpdate::readCohort("/updatefs",target));
    strcpy(scratch->cohort.version,target.product.version);strcpy(scratch->cohort.source_revision,target.product.source_revision);
    metadataClosePath=fsRoot+"/bootfs/cohort.json";metadataRequireCandidate=false;
    assert(!validateStore(nullptr,1) && metadataFailedCloses==1 && provisionReadRetained && mounted);
    assert(!cleanup(nullptr) && !exitSafe());
    assert(!backendCalls && !hardwareCalls && !writes && !selectorCalls && !restarts);
    runtime=nullptr;current.reset();
    std::cout<<"Native export source-identity close failure retains mount and refuses cleanup PASS\n";return;
  }
  runtime=nullptr;current.reset();delete scratch;scratch=nullptr;fsRoot.clear();
  std::cout<<"Native app-data export selected="<<enabled<<": exact coordinates/nine-file map, all twelve grants, no extra privilege, native-first bridge and unchanged successors PASS\n";
}
