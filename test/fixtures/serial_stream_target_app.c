/* Build-only portable client witness; never staged as a product/default app. */
#include <PortableSerialClient.h>
__attribute__((visibility("default"))) void app_main(void){
 const risc_runtime_api_v1* runtime=risc_runtime_get_api(1);
 portable_serial_client client;
 const t5_serial_config_t config={9600,8,T5_SERIAL_PARITY_NONE,1,T5_SERIAL_FLOW_NONE};
 if(!portable_serial_init(&client,runtime))return;
 if(!portable_serial_open(&client,0,&config)){
  if(client.cleanup_required)(void)runtime->retain_invocation();return;
 }
 (void)portable_serial_queue(&client,"test",0);
 uint8_t bytes[PSC_CHUNK];(void)portable_serial_tick(&client,bytes,sizeof(bytes),1);
 if(client.cleanup_required){(void)runtime->retain_invocation();return;}
 if(!portable_serial_close(&client) && client.cleanup_required)(void)runtime->retain_invocation();
}
