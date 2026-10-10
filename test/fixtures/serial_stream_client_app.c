#include <PortableSerialClient.h>
#include <assert.h>
#include <string.h>
extern const char* serial_witness_mode(void);
extern void serial_witness_set_phase(unsigned);
extern void serial_witness_event(const char*);
static portable_serial_client client;
static const risc_runtime_api_v1* runtime;
static bool is(const char* s){return !strcmp(serial_witness_mode(),s);}
__attribute__((visibility("default"))) int app_module_init(void){
 runtime=risc_runtime_get_api(1);assert(portable_serial_init(&client,runtime));return 0;
}
__attribute__((visibility("default"))) void app_main(void){
 t5_serial_config_t config={9600,8,T5_SERIAL_PARITY_NONE,1,T5_SERIAL_FLOW_NONE};
 const bool opened=portable_serial_open(&client,0,&config);
 if(is("configure-rollback-retained")){assert(!opened && client.cleanup_required);return;}
 assert(opened && client.stream_mode && client.session && client.rx_stream && client.tx_stream);
 assert(portable_serial_control(&client,true,true) && client.dtr && client.rts);
 config.baud_rate=115200;
 if(is("configure-fail")){assert(!portable_serial_configure(&client,&config) && !client.connected && client.result==PSC_IO && !client.cleanup_required);return;}
 assert(portable_serial_configure(&client,&config));
 uint8_t received[64]={0};size_t total=0;
 assert(portable_serial_queue(&client,"ABCDEFG",0));
 if(is("inventory-recover") || is("inventory-malformed")){
  serial_witness_set_phase(1);assert(!portable_serial_tick(&client,received,sizeof(received),1));
  assert(client.connected && client.device==17 && client.generation==9 && !client.tx_bytes && client.queued==9);
  serial_witness_set_phase(2);
 }
 if(is("disconnect") || is("reconnect")){
  serial_witness_set_phase(1);assert(!portable_serial_tick(&client,received,sizeof(received),1));
  assert(!client.connected && !client.cleanup_required && client.result==PSC_DISCONNECTED);return;
 }
 if(is("eof")){
  runtime->yield_ms(1);assert(!portable_serial_tick(&client,received,sizeof(received),1));
  assert(!client.connected && !client.cleanup_required && client.result==PSC_EOF);return;
 }
 total+=portable_serial_tick(&client,received,sizeof(received),1);
 assert(client.tx_bytes==5 && !total);
 if(is("cancel")){
  portable_serial_cancel(&client);assert(!client.queued && !client.sent && client.result==PSC_CANCELLED && client.tx_bytes==5);
 }else if(is("stall")){
  assert(!portable_serial_tick(&client,received,sizeof(received),2001));
  assert(!client.queued && !client.sent && client.result==PSC_TIMEOUT && client.tx_bytes==5);
 }else{
  for(unsigned tick=2;tick<32 && total<9;tick++){
   runtime->yield_ms(1);total+=portable_serial_tick(&client,received+total,sizeof(received)-total,tick);
  }
  assert(total==9 && !memcmp(received,"ABCDEFG\r\n",9) && client.tx_bytes==9 && client.rx_bytes==9 && !client.queued);
 }
 const bool closed=portable_serial_close(&client);
 if(is("close-retained")){assert(!closed && client.cleanup_required);return;}
 assert(closed && !client.session && !client.rx_stream && !client.tx_stream && !client.grant.api);
 serial_witness_event("client:complete");
}
__attribute__((visibility("default"))) void app_module_fini(void){serial_witness_event("client:fini");}
