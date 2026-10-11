/* Software-only adapter: production Runtime owns sessions, grants and rings. */
#include <RiscStreamSessionProviderV1.h>
#include <assert.h>
#include <string.h>
extern void image_pressure_stream_event(const char*);
extern void* image_pressure_stream_allocate(bool);
extern void image_pressure_stream_free(void*);
extern bool image_pressure_stream_retained(void);
static const risc_stream_provider_v1* host;
static const uint32_t* dependency;
static uint64_t sequence,session;
static uint32_t rx,tx;
static void* memory;
static void* seed;
static bool bind(const risc_stream_provider_v1* h){host=h;return true;}
static bool start(const risc_provider_dependency_v1* deps,size_t count){
    assert(count==1 && deps && !strcmp(deps[0].capability_id,"test.root") && deps[0].api_version==1);
    dependency=deps[0].api;assert(dependency && dependency[0]==1 && dependency[1]==8);
    seed=image_pressure_stream_allocate(false);assert(seed);
    image_pressure_stream_event("start");return true;
}
static bool quiesce(void){assert(dependency && dependency[0]==1 && !session);image_pressure_stream_event("quiesce");return true;}
static void stop(void){assert(!session);if(memory)image_pressure_stream_free(memory);image_pressure_stream_free(seed);memory=seed=0;image_pressure_stream_event("stop");}
static int32_t open_session(const void* request,uint32_t size,uint32_t ms,risc_provider_stream_session_v1* out){
    assert(!session && request && size==4 && *(const uint32_t*)request==42 && ms);
    risc_stream_endpoint_v1 e={sizeof(e),1,RISC_STREAM_READ,37,0,0,0};
    assert(host->publish(host->context,&e,&rx)==RISC_STREAM_OK);
    e.rights=RISC_STREAM_WRITE;e.byte_capacity=41;
    assert(host->publish(host->context,&e,&tx)==RISC_STREAM_OK);
    session=++sequence;out->session=session;out->rx_endpoint=rx;out->tx_endpoint=tx;
    image_pressure_stream_event("open");return RISC_STREAM_OK;
}
static int32_t call_session(uint64_t id,const void* request,uint32_t size,uint32_t ms,void* reply,uint32_t cap,uint32_t* count){
    assert(id==session && request && size==4 && ms);*count=0;
    assert(dependency[0]==1 && dependency[1]==8);
    image_pressure_stream_event("call");
    switch(*(const uint32_t*)request){
    case 1: {
        static const unsigned char bytes[]={0,1,2,3,0xff,0x80,13,10,0,9,8,7,6};
        uint32_t n=0;assert(host->produce(host->context,rx,bytes,sizeof(bytes),&n)==RISC_STREAM_OK && n==sizeof(bytes));break;
    }
    case 2: assert(!memory);memory=image_pressure_stream_allocate(true);assert(memory);break;
    case 3: return host->consume(host->context,tx,reply,cap,count);
    default: assert(false);
    }
    return RISC_STREAM_OK;
}
static int32_t close_session(uint64_t id,uint32_t ms){
    assert(id==session && ms);image_pressure_stream_event("close");
    if(image_pressure_stream_retained())return RISC_STREAM_IO;
    assert(host->close(host->context,rx)==RISC_STREAM_OK && host->close(host->context,tx)==RISC_STREAM_OK);
    session=0;rx=tx=0;return RISC_STREAM_OK;
}
static void poll(uint32_t ms){assert(ms && dependency[0]==1);image_pressure_stream_event("poll");}
static const uint32_t api[2]={1,sizeof(api)};
static const risc_stream_session_provider_v1 adapter={1,sizeof(adapter),open_session,call_session,close_session};
static const risc_driver_stream_sessions_v2 driver={{{{2,sizeof(driver),"pressure-stream","test.pressure.stream",1,api,start,stop,quiesce},0,bind},poll},RISC_DRIVER_STREAM_SESSIONS_TAG_V1,1,&adapter};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver.poll.streams.driver:0;}
