#include <RiscRuntimeV1.h>
#include <RiscStreamClientV1.h>
#include <RiscProviderPromotionV1.h>
#include <assert.h>
#include <string.h>
extern const char* stream_test_mode(void);
extern void stream_test_event(const char*);
extern unsigned stream_test_invocation(void);
extern void stream_test_owner(bool);
extern void stream_test_stale(const risc_stream_client_v1*);
extern void stream_test_save(const risc_stream_client_v1*,const risc_stream_opened_v1*,const risc_runtime_capability_v1*);
extern void stream_test_app(const void*);
static const risc_runtime_api_v1* runtime;
static risc_stream_client_v1 client;
static risc_runtime_capability_v1 grant;
static risc_stream_opened_v1 opened;
static unsigned invocation;
static int is(const char* s){return !strcmp(stream_test_mode(),s);}
static void acquire(void){grant=(risc_runtime_capability_v1){.struct_size=sizeof(grant)};assert(runtime->acquire("test.stream",1,0,&grant));}
static int32_t open_with(risc_runtime_capability_v1* g,risc_stream_opened_v1* out){uint32_t value=42;out->struct_size=sizeof(*out);return client.open(client.context,g,&value,sizeof(value),1,out);}
__attribute__((visibility("default"))) int app_module_init(void){
 runtime=risc_runtime_get_api(1);assert(runtime && runtime->struct_size>=RISC_RUNTIME_STREAM_CLIENT_V1_SIZE);
 client.struct_size=sizeof(client);assert(runtime->stream_client(&client));stream_test_app(&client);
 invocation=stream_test_invocation();stream_test_stale(&client);stream_test_event("app:init");
#ifdef STREAM_CHILD
 if(is("child-init-fail"))return -1;
#endif
 return 0;
}
__attribute__((visibility("default"))) void app_main(void){
 stream_test_event("app:entry");
 if(is("demand-retained") && invocation==1){
  risc_runtime_capability_v1 promotion={.struct_size=sizeof(promotion)};
  assert(runtime->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&promotion));
  const risc_provider_promotion_api_v1* p=promotion.api;assert(p->promote(p->context)==RISC_PROVIDER_PROMOTION_OK);
  assert(runtime->release(&promotion));
 }
 if(is("unknown-version") || is("start-retained")){
  grant=(risc_runtime_capability_v1){.struct_size=sizeof(grant)};assert(!runtime->acquire("test.stream",1,0,&grant));return;
 }
 acquire();
 risc_runtime_capability_v1 forged=grant;forged.generation++;
 assert(open_with(&forged,&opened)==RISC_STREAM_DENIED && !opened.session);
 forged=grant;forged.api=(const void*)1;assert(open_with(&forged,&opened)==RISC_STREAM_DENIED);
 forged=grant;forged.slot=99;assert(open_with(&forged,&opened)==RISC_STREAM_DENIED);
 stream_test_owner(false);assert(open_with(&grant,&opened)==RISC_STREAM_DENIED);stream_test_owner(true);
 int32_t rc=open_with(&grant,&opened);
 if(is("unknown-tag") || !strncmp(stream_test_mode(),"prefix-",7)){assert(rc==RISC_STREAM_UNSUPPORTED);assert(runtime->release(&grant));return;}
 if(is("open-clean-fail")){assert(rc==RISC_STREAM_IO && !opened.session);assert(runtime->release(&grant));return;}
 if(is("grant1-fail") || is("grant2-fail")){assert(rc==RISC_STREAM_LIMIT && !opened.session && !opened.rx && !opened.tx);assert(runtime->release(&grant));return;}
 if(rc==RISC_STREAM_RETAINED){assert(!opened.session && !opened.rx && !opened.tx);stream_test_event("app:fenced");return;}
 assert(rc==RISC_STREAM_OK && opened.session && opened.rx && opened.tx && opened.rx!=opened.tx);
 stream_test_save(&client,&opened,&grant);
 risc_stream_opened_v1 extra={0};assert(open_with(&grant,&extra)==RISC_STREAM_BUSY);
 if(is("open-duplicate")){
  risc_runtime_capability_v1 other={.struct_size=sizeof(other)};assert(runtime->acquire("test.stream",1,0,&other));
  assert(open_with(&other,&extra)==RISC_STREAM_RETAINED);stream_test_event("app:fenced");return;
 }
 uint32_t count=99;char bytes[512]={0};
 assert(client.read(client.context,opened.tx,bytes,1,&count)==RISC_STREAM_DENIED && !count);
 assert(client.write(client.context,opened.rx,"x",1,&count)==RISC_STREAM_DENIED && !count);
 assert(client.read(client.context,UINT64_MAX,bytes,1,&count)==RISC_STREAM_CLOSED && !count);
 assert(client.read(client.context,opened.rx,bytes,1,&count)==RISC_STREAM_AGAIN && !count);
 assert(client.write(client.context,opened.tx,"abcdefghij",10,&count)==RISC_STREAM_OK && count==7);
 assert(client.write(client.context,opened.tx,"x",1,&count)==RISC_STREAM_AGAIN && !count);
 runtime->yield_ms(1);
 assert(client.read(client.context,opened.rx,bytes,sizeof(bytes),&count)==RISC_STREAM_OK && count==3 && !memcmp(bytes,"abc",3));
 risc_stream_client_info_v1 info={.struct_size=sizeof(info)};assert(client.info(client.context,opened.tx,&info)==RISC_STREAM_OK && info.bytes_written==7);
 if(!strncmp(stream_test_mode(),"call-",5)){
  assert(client.call(client.context,opened.session,0,0,1,0,0,&count)==RISC_STREAM_RETAINED && !count);stream_test_event("app:fenced");return;
 }
 if(is("release-open")){assert(runtime->release(&grant));assert(client.close(client.context,opened.session,1)==RISC_STREAM_CLOSED);opened.session=0;return;}
 if(is("forgot-close") || is("fini-close"))return;
 if(is("revoke-busy")){extern void stream_test_lock(void);stream_test_lock();}
 rc=client.close(client.context,opened.session,1);
 if(rc==RISC_STREAM_RETAINED){stream_test_event("app:fenced");return;}
 assert(rc==RISC_STREAM_OK);assert(client.close(client.context,opened.session,1)==RISC_STREAM_CLOSED);opened.session=0;
 if(is("quiesce-fail")){assert(!runtime->release(&grant));stream_test_event("app:fenced");return;}
 assert(runtime->release(&grant));
 if((is("child") || is("child-init-fail") || is("demand-retained")) && invocation==1)assert(runtime->request_launch("child.elf"));
}
__attribute__((visibility("default"))) void app_module_fini(void){
 stream_test_event("app:fini");
 if(is("fini-close") && opened.session){assert(client.close(client.context,opened.session,1)==RISC_STREAM_OK);assert(runtime->release(&grant));}
}
