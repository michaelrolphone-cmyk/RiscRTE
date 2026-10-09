/* Included by the native pressure harness: no alternate lifecycle or broker.
 * All calls enter production Runtime, Graph, AppStreamSessions and queue code. */
static risc_stream_client_v1 savedClient{};
static risc_stream_opened_v1 savedOpened{};
static risc_runtime_capability_v1 savedGrant{};
static const risc_runtime_api_v1* savedApi;
static unsigned streamOpens,streamCloses,streamCalls,streamPolls,rootStarts,rootStops,appInits,appFinis,pressureFrees,childFailures;
static void* appSentinel;
static void* appWork;
static void* providerWork;
static constexpr size_t SeedBytes=4096,QueueBytes=37+41;
static const unsigned char rxBytes[]={0,1,2,3,0xff,0x80,13,10,0,9,8,7,6};
static const unsigned char txBytes[]={0xff,0,0x80,5,4,3,2,1,0,13,10,99,98,97,96,95,94};
static bool custodyRetained(){return mode.find("retained")!=std::string::npos;}
static void checkBytes(void* pointer,size_t size,unsigned char byte){
  assert(pointer && indexOf(pointer)<allocCount);
  for(size_t i=0;i<size;++i)assert(static_cast<unsigned char*>(pointer)[i]==byte);
}
static void checkCustodyPressure(){
  // Input-cache reclamation must not touch mapped app/provider/dependency code,
  // native app/provider payloads or the host-owned reserved byte rings.
  assert(risc_test_native_mapping_count()==3);
  assert(RuntimeStreams::Testing::allocatedBytes()==QueueBytes);
  checkBytes(appSentinel,SeedBytes,0x6d);checkBytes(providerMemory,SeedBytes,0x5a);
  ++pressureFrees;
}
extern "C" void stream_test_event(const char* event){
  if(!std::strcmp(event,"root:start"))++rootStarts;
  if(!std::strcmp(event,"root:stop"))++rootStops;
}
extern "C" void stream_test_root(const void*){}
extern "C" void image_pressure_stream_event(const char* event){
  if(!std::strcmp(event,"start"))++providerStarts;
  if(!std::strcmp(event,"stop"))++providerStops;
  if(!std::strcmp(event,"open"))++streamOpens;
  if(!std::strcmp(event,"close"))++streamCloses;
  if(!std::strcmp(event,"call"))++streamCalls;
  if(!std::strcmp(event,"poll"))++streamPolls;
}
extern "C" bool image_pressure_stream_retained(){return custodyRetained();}
extern "C" void* image_pressure_stream_allocate(bool pressure){
  auto allocate=imported<void*(*)(size_t)>("malloc",false);
  void* result=allocate(pressure?WorkBytes:SeedBytes);assert(result);
  std::memset(result,0x5a,pressure?WorkBytes:SeedBytes);
  if(pressure)providerWork=result;else providerMemory=result;
  return result;
}
extern "C" void image_pressure_stream_free(void* pointer){
  assert(pointer==providerMemory || pointer==providerWork);
  checkBytes(pointer,pointer==providerMemory?SeedBytes:WorkBytes,0x5a);
  auto release=imported<void(*)(void*)>("free",false);release(pointer);
  if(pointer==providerMemory)providerMemory=nullptr;else providerWork=nullptr;
}
extern "C" void image_pressure_stream_lifecycle(bool init,bool child){
  assert(!child); // The child must fail before mapping/init/entry.
  if(init)++appInits;else ++appFinis;
}
static void streamCall(const risc_stream_client_v1& client,const risc_stream_opened_v1& opened,uint32_t operation,void* reply=nullptr,uint32_t capacity=0,uint32_t* actual=nullptr){
  uint32_t n=99;
  assert(client.call(client.context,opened.session,&operation,sizeof(operation),1000,reply,capacity,&n)==RISC_STREAM_OK);
  if(actual)*actual=n;else assert(!n);
}
static risc_stream_client_info_v1 streamInfo(const risc_stream_client_v1& client,uint64_t handle){
  risc_stream_client_info_v1 result{sizeof(result)};
  assert(client.info(client.context,handle,&result)==RISC_STREAM_OK);return result;
}
static bool sameInfo(const risc_stream_client_info_v1& a,const risc_stream_client_info_v1& b){
  return a.struct_size==b.struct_size && a.rights==b.rights && a.capacity==b.capacity &&
    a.buffered==b.buffered && a.high_water==b.high_water && a.terminal==b.terminal &&
    a.bytes_read==b.bytes_read && a.bytes_written==b.bytes_written;
}
static void checkLiveGrantBound(const risc_runtime_api_v1* api){
  risc_runtime_capability_v1 grants[15]{};
  for(auto& grant:grants){grant.struct_size=sizeof(grant);assert(api->acquire("test.pressure.stream",1,0,&grant));}
  risc_runtime_capability_v1 extra{sizeof(extra)};
  assert(!api->acquire("test.pressure.stream",1,0,&extra)); // The open session keeps one of 16 live grants.
  for(auto& grant:grants)assert(api->release(&grant));
}
static void staleClient(){
  const auto closes=streamCloses,calls=streamCalls;
  uint32_t n=99;unsigned char byte=0;
  assert(savedClient.read(savedClient.context,savedOpened.rx,&byte,1,&n)==RISC_STREAM_CLOSED && !n);
  n=99;assert(savedClient.write(savedClient.context,savedOpened.tx,&byte,1,&n)==RISC_STREAM_CLOSED && !n);
  n=99;assert(savedClient.call(savedClient.context,savedOpened.session,&byte,1,1000,nullptr,0,&n)==RISC_STREAM_CLOSED && !n);
  assert(savedClient.close(savedClient.context,savedOpened.session,1000)==RISC_STREAM_CLOSED);
  risc_stream_opened_v1 out{sizeof(out)};uint32_t request=42;
  assert(savedClient.open(savedClient.context,&savedGrant,&request,sizeof(request),1000,&out)==RISC_STREAM_DENIED && !out.session && !out.rx && !out.tx);
  assert(streamCloses==closes && streamCalls==calls);
}
extern "C" void image_pressure_stream_app(const risc_runtime_api_v1* api,bool child){
  assert(!child);++invocations;savedApi=api;
  risc_stream_client_v1 client{0,sizeof(client)};assert(api->stream_client(&client));
  if(invocations==1){
    risc_runtime_capability_v1 grant{sizeof(grant)};
    assert(api->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&grant));
    auto* promotion=static_cast<const risc_provider_promotion_api_v1*>(grant.api);
    assert(promotion->promote(promotion->context)==RISC_PROVIDER_PROMOTION_OK && api->release(&grant));
  }else{
    assert(mode=="ledger-final" && invocations==2 && appInits==2 && appFinis==1);
    assert(!failLedgerCount && ledgerAttempts==unsigned(RISC_APP_IMAGE_CACHE?4:3));
    assert(failures==unsigned(RISC_APP_IMAGE_CACHE?2:1));
    assert(!RuntimeStreams::Testing::allocatedBytes() && !risc_test_native_image_count());
    assert(client.context>savedOpened.tx); // Includes the discarded child's context.
    staleClient();
    auto old=savedGrant;assert(!api->release(&old));
  }
  risc_runtime_capability_v1 grant{sizeof(grant)};
  assert(api->acquire("test.pressure.stream",1,0,&grant));
  risc_stream_opened_v1 opened{sizeof(opened)};uint32_t request=42;
  assert(client.open(client.context,&grant,&request,sizeof(request),1000,&opened)==RISC_STREAM_OK);
  assert(opened.session && opened.rx && opened.tx && opened.rx!=opened.tx);
  if(invocations==2){
    assert(opened.session!=savedOpened.session && opened.rx!=savedOpened.rx && opened.tx!=savedOpened.tx);
    assert(grant.generation!=savedGrant.generation);
  }
  savedClient=client;savedOpened=opened;savedGrant=grant;
  checkLiveGrantBound(api);
  assert(providerStarts==1 && rootStarts==1 && !providerStops && !rootStops);
  assert(risc_test_native_mapping_count()==3 && RuntimeStreams::Testing::allocatedBytes()==QueueBytes);
  auto allocate=imported<void*(*)(size_t)>("malloc",true);
  appSentinel=allocate(SeedBytes);assert(appSentinel);std::memset(appSentinel,0x6d,SeedBytes);
  streamCall(client,opened,1);
  uint32_t n=0;assert(client.write(client.context,opened.tx,txBytes,sizeof(txBytes),&n)==RISC_STREAM_OK && n==sizeof(txBytes));
  const auto polls=streamPolls;api->yield_ms(1);assert(streamPolls==polls+1);
  const auto rxBefore=streamInfo(client,opened.rx),txBefore=streamInfo(client,opened.tx);
  assert(rxBefore.buffered==sizeof(rxBytes) && txBefore.buffered==sizeof(txBytes));
  assert(rxBefore.rights==RISC_STREAM_READ && txBefore.rights==RISC_STREAM_WRITE);
  if(mode=="ledger-final"){
    if(invocations==1){
      failLedgerCount=RISC_APP_IMAGE_CACHE?2:1;probeRecursion=true;
      assert(api->request_launch("child.elf"));
      return; // Runtime closes this session and reclaims the native app ledger.
    }
  }else{
    assert(risc_test_native_image_count()==unsigned(RISC_APP_IMAGE_CACHE));
    const auto failed=failures;custodyPressure=true;probeRecursion=true;limit=PressureLimit;
    if(mode.rfind("custody-provider",0)==0)streamCall(client,opened,2);
    else {appWork=allocate(WorkBytes);assert(appWork);std::memset(appWork,0x7e,WorkBytes);}
    custodyPressure=false;probeRecursion=false;limit=SIZE_MAX;
    assert(failures-failed==unsigned(RISC_APP_IMAGE_CACHE));
    assert(pressureFrees==unsigned(RISC_APP_IMAGE_CACHE?2:0)); // One input and cache metadata.
    assert(recursiveCalls==unsigned(RISC_APP_IMAGE_CACHE));
    assert(!risc_test_native_image_count() && !risc_runtime_reclaim_app_images());
    checkBytes(appSentinel,SeedBytes,0x6d);checkBytes(providerMemory,SeedBytes,0x5a);
    if(providerWork)checkBytes(providerWork,WorkBytes,0x5a);
    if(appWork)checkBytes(appWork,WorkBytes,0x7e);
  }
  const auto rxAfter=streamInfo(client,opened.rx),txAfter=streamInfo(client,opened.tx);
  assert(sameInfo(rxBefore,rxAfter) && sameInfo(txBefore,txAfter));
  unsigned char bytes[64]{};
  n=0;assert(client.read(client.context,opened.rx,bytes,sizeof(bytes),&n)==RISC_STREAM_OK && n==sizeof(rxBytes));
  assert(!std::memcmp(bytes,rxBytes,n));
  streamCall(client,opened,3,bytes,sizeof(bytes),&n);assert(n==sizeof(txBytes) && !std::memcmp(bytes,txBytes,n));
  // Refill both queues so retained close has live byte custody to preserve.
  streamCall(client,opened,1);
  n=0;assert(client.write(client.context,opened.tx,txBytes,sizeof(txBytes),&n)==RISC_STREAM_OK && n==sizeof(txBytes));
  assert(streamInfo(client,opened.rx).buffered==sizeof(rxBytes) && streamInfo(client,opened.tx).buffered==sizeof(txBytes));
  if(custodyRetained()){
    // A queued handoff must be canceled by the retained close, despite boot pins.
    assert(api->request_launch("child.elf"));
    const auto calls=streamCalls,polls=streamPolls;
    assert(client.close(client.context,opened.session,1000)==RISC_STREAM_RETAINED);
    assert(runtime->retained() && !risc_runtime_get_api(1));
    staleClient();api->yield_ms(1);
    assert(!api->request_launch("child.elf") && !api->release(&grant));
    assert(streamCalls==calls && streamPolls==polls && streamCloses==1);
    assert(RuntimeStreams::Testing::allocatedBytes()==QueueBytes);
    checkBytes(appSentinel,SeedBytes,0x6d);checkBytes(providerMemory,SeedBytes,0x5a);
    return;
  }
  assert(client.close(client.context,opened.session,1000)==RISC_STREAM_OK);
  assert(!RuntimeStreams::Testing::allocatedBytes() && api->release(&grant));
  staleClient();
}
static int runCustody(const std::string& root){
  static_assert(RiscBoot::Runtime::MaxAppPolicyGrants==RISC_APP_POLICY_ROWS,"explicit metadata bound");
  static_assert(RiscBoot::Runtime::MaxAppRequirements==16,"manifest authority unchanged");
  assert(!risc_runtime_reclaim_app_images() && !RuntimeStreams::Testing::metadataAllocated());
  write(root+"/board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  write(root+"/root.json",R"({"type":"driver","id":"stream-root","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"root.elf","requires":[],"provides":[{"capability":"test.root","api":1}]})");
  write(root+"/stream.json",R"({"type":"driver","id":"pressure-stream","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"stream.elf","requires":[{"capability":"test.root","api":1}],"provides":[{"capability":"test.pressure.stream","api":1}]})");
  write(root+"/custody.json",R"({"type":"application","id":"custody","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"custody.elf","entry":"app_main","requires":[{"capability":"test.pressure.stream","api":1},{"capability":"runtime.provider-promotion","api":1}]})");
  write(root+"/boot.json",R"({"board":"board.json","default_app":"custody.elf","provider_activation":"demand-retained","drivers":[{"manifest":"stream.json"},{"manifest":"root.json"}],"app_capabilities":[{"manifest":"custody.json","grants":[{"capability":"test.pressure.stream","api":1,"instance_id":0},{"capability":"runtime.provider-promotion","api":1,"instance_id":0}]}]})");
  runtime=new RiscBoot::Runtime({[](){return isOwner;},[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char* line){if(!std::strcmp(line,"RTE_APP child=failed action=reload-default"))++childFailures;return true;}});
  assert(runtime->prepare(root.c_str()));
  assert(runtime->run()!=custodyRetained() && runtime->retained()==custodyRetained());
  assert(!risc_runtime_reclaim_app_images() && !risc_test_native_image_count());
  staleClient();
  assert(childFailures==unsigned(mode=="ledger-final"));
  if(custodyRetained()){
    const auto polls=streamPolls,closes=streamCloses,calls=streamCalls;
    assert(invocations==1 && appInits==1 && !appFinis && !providerStops && !rootStops);
    assert(risc_test_native_mapping_count()==3 && RuntimeStreams::Testing::allocatedBytes()==QueueBytes);
    checkBytes(appSentinel,SeedBytes,0x6d);checkBytes(providerMemory,SeedBytes,0x5a);
    if(providerWork)checkBytes(providerWork,WorkBytes,0x5a);
    if(appWork)checkBytes(appWork,WorkBytes,0x7e);
    assert(!runtime->run());savedApi->yield_ms(1);staleClient();
    assert(polls==streamPolls && closes==streamCloses && calls==streamCalls);
    // Retained custody deliberately remains rooted until process exit; never
    // destroy Runtime or force another cleanup pass after an uncertain close.
  }else{
    assert(invocations==unsigned(mode=="ledger-final"?2:1) && appInits==invocations && appFinis==invocations);
    assert(streamOpens==invocations && streamCloses==invocations && providerStops==1 && rootStops==1);
    delete runtime;runtime=nullptr;
    assert(!risc_test_native_mapping_count() && !RuntimeStreams::Testing::allocatedBytes() && !allocCount && !live);
  }
  std::printf("Cache custody flag=%u policy=%u mode=%s: entries=%u ledger_attempts=%u failures=%u session_open/close=%u/%u retained=%u PASS\n",RISC_APP_IMAGE_CACHE,RISC_APP_POLICY_ROWS,mode.c_str(),invocations,ledgerAttempts,failures,streamOpens,streamCloses,custodyRetained());
  return 0;
}
