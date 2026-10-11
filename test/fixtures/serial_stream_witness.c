/* Software-only serial protocol witness. This is not a physical transport and
 * is never installed in a Runtime or product catalog. */
#include <RiscSerialPortV1.h>
#include <RiscStreamSessionProviderV1.h>
#include <RiscSerialStreamSessionV1.h>
#include <string.h>
#ifdef RISC_STREAM_TARGET_WITNESS
static const char* serial_witness_mode(void){return "normal";}
static unsigned serial_witness_phase(void){return 0;}
static void serial_witness_event(const char* event){(void)event;}
#else
extern const char* serial_witness_mode(void);
extern unsigned serial_witness_phase(void);
extern void serial_witness_event(const char*);
#endif
static const risc_stream_provider_v1* host;
static uint64_t token,sequence,device,generation;
static uint32_t rx,tx;
static bool stopped;
static risc_serial_stream_config_v1 framing;
static bool is(const char* s){return !strcmp(serial_witness_mode(),s);}
static bool snapshot(risc_serial_device_v1* out,size_t* count){
 if(!count || *count<1 || !out)return false;
 const unsigned phase=serial_witness_phase();
 if((is("inventory-recover") && phase==1) || (is("inventory-malformed") && phase==1)){
  if(is("inventory-recover"))return false;
  out[0]=(risc_serial_device_v1){0,9,0,{0}};*count=1;return true;
 }
 if(is("disconnect") && phase==1){*count=0;return true;}
 out[0]=(risc_serial_device_v1){17,(is("reconnect") && phase==1)?10:9,RISC_SERIAL_TRANSPORT_INTERNAL,{0}};*count=1;return true;
}
static bool configuration(const risc_serial_stream_config_v1* c){return c && c->baud_rate>=300 && c->baud_rate<=3000000 && c->data_bits>=5 && c->data_bits<=8 && c->parity<=4 && (c->stop_bits==1 || c->stop_bits==2) && !c->flow_control;}
static int32_t inventory_check(uint64_t d,uint64_t g){
 risc_serial_device_v1 list[RISC_SERIAL_INVENTORY_MAX_DEVICES];memset(list,0,sizeof(list));size_t n=RISC_SERIAL_INVENTORY_MAX_DEVICES;
 if(!snapshot(list,&n) || n>RISC_SERIAL_INVENTORY_MAX_DEVICES)return RISC_STREAM_IO;
 bool found=false;
 for(size_t i=0;i<n;i++){
  if(!list[i].provider_device || !list[i].generation || list[i].transport>RISC_SERIAL_TRANSPORT_IP)return RISC_STREAM_IO;
  for(unsigned b=0;b<7;b++)if(list[i].reserved[b])return RISC_STREAM_IO;
  for(size_t j=0;j<i;j++)if(list[i].provider_device==list[j].provider_device)return RISC_STREAM_IO;
  if(list[i].provider_device==d && list[i].generation==g)found=true;
 }
 return found?RISC_STREAM_OK:RISC_STREAM_DISCONNECTED;
}
static int32_t close_session(uint64_t id,uint32_t ms){
 if(!ms || ms>1000 || !id || id!=token)return RISC_STREAM_INVALID;
 serial_witness_event("adapter:close");
 // Stop the pump and revoke queue use before the checked physical close.
 stopped=true;
 if((rx && host->finish(host->context,rx,RISC_STREAM_CANCELLED)!=RISC_STREAM_OK) ||
    (tx && host->finish(host->context,tx,RISC_STREAM_CANCELLED)!=RISC_STREAM_OK))return RISC_STREAM_RETAINED;
 if(is("close-retained") || is("configure-rollback-retained"))return RISC_STREAM_RETAINED;
 if((rx && host->close(host->context,rx)!=RISC_STREAM_OK) ||
    (tx && host->close(host->context,tx)!=RISC_STREAM_OK))return RISC_STREAM_RETAINED;
 token=device=generation=0;rx=tx=0;return RISC_STREAM_OK;
}
static int32_t open_session(const void* bytes,uint32_t size,uint32_t ms,risc_provider_stream_session_v1* out){
 serial_witness_event("adapter:open");
 if(!bytes || size!=sizeof(risc_serial_stream_open_v1) || !ms || ms>1000 || !out || out->struct_size<sizeof(*out))return RISC_STREAM_INVALID;
 risc_serial_stream_open_v1 request;memcpy(&request,bytes,sizeof(request));
 if(request.api_version!=1 || request.struct_size!=sizeof(request) || !request.provider_device || !request.device_generation || !configuration(&request.config))return RISC_STREAM_INVALID;
 const int32_t present=inventory_check(request.provider_device,request.device_generation);if(present!=RISC_STREAM_OK)return present;
 if(token)return RISC_STREAM_BUSY;
 if(sequence==UINT64_MAX)return RISC_STREAM_LIMIT;
 token=++sequence;device=request.provider_device;generation=request.device_generation;stopped=false;
 risc_stream_endpoint_v1 e={sizeof(e),1,RISC_STREAM_READ,5,0,0,0};
 if(host->publish(host->context,&e,&rx)!=RISC_STREAM_OK)goto retained;
 e.rights=RISC_STREAM_WRITE;if(host->publish(host->context,&e,&tx)!=RISC_STREAM_OK)goto retained;
 framing=request.config;
 out->session=token;out->rx_endpoint=rx;out->tx_endpoint=tx;
 if(is("configure-rollback-retained")){(void)close_session(token,ms);return RISC_STREAM_RETAINED;}
 return RISC_STREAM_OK;
retained:
 if(close_session(token,ms)==RISC_STREAM_OK)return RISC_STREAM_LIMIT;
 out->session=token;out->rx_endpoint=rx;out->tx_endpoint=tx;return RISC_STREAM_RETAINED;
}
static int32_t call_session(uint64_t id,const void* bytes,uint32_t size,uint32_t ms,void* reply,uint32_t cap,uint32_t* actual){
 (void)reply;(void)cap;if(actual)*actual=0;
 if(!actual || !id || id!=token || !bytes || size!=sizeof(risc_serial_stream_call_v1) || !ms || ms>1000)return RISC_STREAM_INVALID;
 risc_serial_stream_call_v1 r;memcpy(&r,bytes,sizeof(r));
 if(r.api_version!=1 || r.struct_size!=sizeof(r) || r.reserved)return RISC_STREAM_INVALID;
 if(r.operation==RISC_SERIAL_STREAM_CHECK_DEVICE){
  for(unsigned i=0;i<8;i++)if(r.value.zero[i])return RISC_STREAM_INVALID;
  return inventory_check(device,generation);
 }
 if(r.operation==RISC_SERIAL_STREAM_CONFIGURE){
  if(!configuration(&r.value.config))return RISC_STREAM_INVALID;
  if(is("configure-fail"))return RISC_STREAM_IO;
  framing=r.value.config;serial_witness_event("adapter:configure");return RISC_STREAM_OK;
 }
 if(r.operation==RISC_SERIAL_STREAM_CONTROL_LINES){
  if(r.value.lines.dtr>1 || r.value.lines.rts>1)return RISC_STREAM_INVALID;
  for(unsigned i=0;i<6;i++)if(r.value.lines.reserved[i])return RISC_STREAM_INVALID;
  serial_witness_event("adapter:lines");return RISC_STREAM_OK;
 }
 return RISC_STREAM_UNSUPPORTED;
}
static bool bind(const risc_stream_provider_v1* table){host=table;return true;}
static bool start(const risc_provider_dependency_v1* deps,size_t n){(void)deps;serial_witness_event("provider:start");return !n;}
static bool quiesce(void){serial_witness_event("provider:quiesce");return !token;}
static void stop(void){serial_witness_event("provider:stop");}
static void poll(uint32_t ms){
 (void)ms;if(!token || stopped || is("stall"))return;
 if(is("eof")){(void)host->finish(host->context,rx,RISC_STREAM_EOF);return;}
 uint8_t value;uint32_t n=0,w=0;
 if(host->consume(host->context,tx,&value,1,&n)==RISC_STREAM_OK && n){
  serial_witness_event("provider:accepted-byte");(void)host->produce(host->context,rx,&value,1,&w);
 }
}
// Raw prefix callbacks deliberately report attempted bypass. The production
// client must use only snapshot plus the Runtime broker on this suffix path.
static uint64_t raw_open(uint64_t d){(void)d;serial_witness_event("raw:open");return 0;}
static bool raw_config(uint64_t s,uint32_t b,uint8_t d,uint8_t p,uint8_t t){(void)s;(void)b;(void)d;(void)p;(void)t;serial_witness_event("raw:configure");return false;}
static bool raw_lines(uint64_t s,bool d,bool r){(void)s;(void)d;(void)r;serial_witness_event("raw:lines");return false;}
static int32_t raw_read(uint64_t s,uint8_t* d,size_t n,uint32_t t){(void)s;(void)d;(void)n;(void)t;serial_witness_event("raw:read");return -1;}
static int32_t raw_write(uint64_t s,const uint8_t* d,size_t n,uint32_t t){(void)s;(void)d;(void)n;(void)t;serial_witness_event("raw:write");return -1;}
static bool raw_close(uint64_t s){(void)s;serial_witness_event("raw:close");return false;}
static bool raw_endpoints(uint64_t s,uint32_t* r,uint32_t* t){(void)s;(void)r;(void)t;serial_witness_event("raw:endpoints");return false;}
static const risc_serial_port_streams_v1 serial={{{{1,sizeof(serial),raw_open,raw_config,raw_lines,raw_read,raw_write,raw_close},0},snapshot},raw_endpoints};
static const risc_stream_session_provider_v1 adapter={1,sizeof(adapter),open_session,call_session,close_session};
static const risc_driver_stream_sessions_v2 driver={{{{2,sizeof(driver),"serial-witness","serial.port",1,&serial,start,stop,quiesce},0,bind},poll},RISC_DRIVER_STREAM_SESSIONS_TAG_V1,1,&adapter};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver.poll.streams.driver:0;}
