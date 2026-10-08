#include <RiscStreamSessionProviderV1.h>
#include <string.h>
extern const char* stream_test_mode(void);
extern void stream_test_event(const char*);
extern void stream_test_slow(void);
extern void stream_test_provider(const void*);
extern void stream_test_grant_failure(unsigned);
extern void stream_test_lock(void);
extern void stream_test_reenter(bool);
static const risc_stream_provider_v1* host;
static const uint32_t* dependency;
static uint64_t sequence;
static struct {uint64_t id;uint32_t rx,tx;} sessions[2];
static int is(const char* s){return !strcmp(stream_test_mode(),s);}
static bool bind(const risc_stream_provider_v1* h){host=h;return true;}
static bool start(const risc_provider_dependency_v1* deps,size_t count){
 if(count!=1 || !deps || strcmp(deps[0].capability_id,"test.root") || deps[0].api_version!=1)return false;
 if(is("lifecycle-reentry"))stream_test_reenter(false);
 dependency=deps[0].api;if(!dependency || dependency[0]!=1 || dependency[1]!=8)return false;stream_test_event("provider:start");stream_test_provider(&sessions);
 if(is("start-retained")){uint32_t endpoint=0;risc_stream_endpoint_v1 e={sizeof(e),1,1,16,0,0,0};
  (void)host->publish(host->context,&e,&endpoint);return false;}
 return true;
}
static bool quiesce(void){stream_test_event("provider:quiesce");if(is("lifecycle-reentry"))stream_test_reenter(false);return dependency && dependency[0]==1 && !is("quiesce-fail") && !is("start-retained");}
static void stop(void){stream_test_event("provider:stop");}
static int32_t open_session(const void* request,uint32_t size,uint32_t ms,risc_provider_stream_session_v1* out){
 stream_test_event("provider:open");
 if(is("reentry"))stream_test_reenter(true);
 if(!request || size!=4 || !ms || *(const uint32_t*)request!=42)return RISC_STREAM_INVALID;
 if(is("open-clean-fail"))return RISC_STREAM_IO;
 if(is("open-retained-zero"))return RISC_STREAM_RETAINED;
 unsigned slot=0;while(slot<2 && sessions[slot].id)++slot;if(slot==2)return RISC_STREAM_LIMIT;
 sessions[slot].id=++sequence;
 risc_stream_endpoint_v1 e={sizeof(e),1,RISC_STREAM_READ,7,0,0,0};
 if(host->publish(host->context,&e,&sessions[slot].rx)!=RISC_STREAM_OK)return RISC_STREAM_RETAINED;
 e.rights=RISC_STREAM_WRITE;
 if(host->publish(host->context,&e,&sessions[slot].tx)!=RISC_STREAM_OK)return RISC_STREAM_RETAINED;
 out->session=sessions[slot].id;out->rx_endpoint=sessions[slot].rx;out->tx_endpoint=sessions[slot].tx;
 if(is("grant1-fail") || is("grant-rollback-retained"))stream_test_grant_failure(1);
 if(is("grant2-fail"))stream_test_grant_failure(2);
 if(is("open-duplicate") && slot)out->session=sessions[0].id;
 if(is("open-foreign"))out->tx_endpoint=0xfefefefe;
 if(is("open-direction")){out->rx_endpoint=sessions[slot].tx;out->tx_endpoint=sessions[slot].rx;}
 if(is("open-malformed"))out->reserved=1;
 if(is("open-retained-token"))return RISC_STREAM_RETAINED;
 if(is("open-partial-error"))return RISC_STREAM_IO;
 if(is("open-slow"))stream_test_slow();
 if(is("open-busy"))stream_test_lock();
 return RISC_STREAM_OK;
}
static int32_t call_session(uint64_t id,const void* request,uint32_t size,uint32_t ms,void* reply,uint32_t cap,uint32_t* count){
 (void)request;(void)size;(void)ms;(void)reply;(void)cap;*count=0;
 stream_test_event("provider:call");
 if(!id)return RISC_STREAM_INVALID;
 if(is("copied-control")){if(size>cap)return RISC_STREAM_INVALID;memcpy(reply,request,size);*count=size;return RISC_STREAM_OK;}
 if(is("call-retained"))return RISC_STREAM_RETAINED;
 if(is("call-overflow"))*count=cap+1;
 if(is("call-slow"))stream_test_slow();
 return RISC_STREAM_OK;
}
static int32_t close_session(uint64_t id,uint32_t ms){
 (void)ms;stream_test_event("provider:close");
 if(is("reentry"))stream_test_reenter(true);
 if(is("close-fail") || is("grant-rollback-retained"))return RISC_STREAM_IO;
 if(is("close-busy")){stream_test_lock();return RISC_STREAM_OK;}
 if(is("close-slow"))stream_test_slow();
 for(unsigned i=0;i<2;i++)if(sessions[i].id==id){
  if(host->close(host->context,sessions[i].rx)!=RISC_STREAM_OK || host->close(host->context,sessions[i].tx)!=RISC_STREAM_OK)return RISC_STREAM_RETAINED;
  memset(&sessions[i],0,sizeof(sessions[i]));return RISC_STREAM_OK;
 }
 return RISC_STREAM_INVALID;
}
static void poll(uint32_t ms){
 (void)ms;stream_test_event("provider:poll");
 if(is("reentry"))stream_test_reenter(false);
 for(unsigned i=0;i<2;i++)if(sessions[i].id){
  if(is("terminal-retained")){(void)host->finish(host->context,sessions[i].rx,RISC_STREAM_RETAINED);return;}
  uint8_t bytes[3];uint32_t n=0,written=0;
  if(host->consume(host->context,sessions[i].tx,bytes,sizeof(bytes),&n)==RISC_STREAM_OK && n)
   (void)host->produce(host->context,sessions[i].rx,bytes,n,&written);
 }
}
static const uint32_t api[2]={1,sizeof(api)};
static const risc_stream_session_provider_v1 adapter={1,sizeof(adapter),open_session,call_session,close_session};
static risc_driver_stream_sessions_v2 driver={{{{2,sizeof(driver),"stream","test.stream",1,api,start,stop,quiesce},0,bind},poll},RISC_DRIVER_STREAM_SESSIONS_TAG_V1,1,&adapter};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){
 if(abi!=2)return 0;
 if(is("unknown-tag")){driver.extension_tag=123;driver.stream_sessions=(const void*)1;}
 if(is("unknown-version")){driver.extension_version=99;driver.stream_sessions=(const void*)1;}
 if(is("prefix-base"))driver.poll.streams.driver.struct_size=sizeof(risc_driver_v2);
 if(is("prefix-diagnostics"))driver.poll.streams.driver.struct_size=sizeof(risc_driver_diagnostics_v2);
 if(is("prefix-streams"))driver.poll.streams.driver.struct_size=sizeof(risc_driver_streams_v2);
 if(is("prefix-poll"))driver.poll.streams.driver.struct_size=sizeof(risc_driver_poll_v2);
 return &driver.poll.streams.driver;
}
