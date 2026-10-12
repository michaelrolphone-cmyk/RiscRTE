/* Compile as C against the selected System tree, then call from native C++.
 * Only synthetic fixture credentials are used. No app copies or replacement
 * codec: these calls execute the checked System PortableWifiAtomic helper. */
#include "PortableWifiAtomic.h"
static portable_wifi_atomic operation;
int wifi_bridge_prepare(const risc_key_value_atomic_v1*api,unsigned kind,const char*ssid,const char*password){
 portable_wifi_credentials value={{0},{0}};strcpy(value.ssid,ssid);strcpy(value.password,password);
 int rc=portable_wifi_atomic_begin(&operation,api,kind,&value);portable_wifi_credentials_clear(&value);
 for(unsigned i=0;rc==PORTABLE_WIFI_ATOMIC_AGAIN&&i<4096;i++)rc=portable_wifi_atomic_step(&operation);
 return rc;
}
int wifi_bridge_submit(void){return portable_wifi_atomic_submit(&operation);}
int wifi_bridge_collect(void){return portable_wifi_atomic_step(&operation);}
int wifi_bridge_cancel(void){return portable_wifi_atomic_cancel(&operation);}
uint64_t wifi_bridge_revision(void){return operation.revision;}
int wifi_bridge_clean(void){
 for(unsigned i=0;i<sizeof(operation.intent);i++)if(operation.intent[i])return 0;
 for(unsigned i=0;i<sizeof(operation.mutations);i++)if(((uint8_t*)operation.mutations)[i])return 0;
 return !operation.desired.password[0]&&!operation.expected.password[0];
}
static const risc_key_value_atomic_v1*reader;
static uint64_t revision;
static int32_t read_key(void*c,const char*k,void*b,uint32_t n,uint32_t*z){(void)c;int rc=reader->get(reader->context,revision,k,b,n,z);return rc==0?0:rc==RISC_ATOMIC_NOT_FOUND?RISC_KEY_VALUE_NOT_FOUND:RISC_KEY_VALUE_IO;}
static int32_t deny(void*c,const char*k,const void*b,uint32_t n){(void)c;(void)k;(void)b;(void)n;return RISC_KEY_VALUE_CONTEXT;}
int wifi_bridge_check(const risc_key_value_atomic_v1*api,unsigned slot,const char*ssid,const char*password){
 risc_key_value_atomic_state_v1 state={.struct_size=sizeof(state)};if(api->state(api->context,&state))return 0;
 reader=api;revision=state.revision;risc_key_value_v1 view={1,sizeof(view),NULL,read_key,deny};portable_wifi_credentials value={{0},{0}};
 int rc=portable_wifi_profile_load(&view,slot,&value);int matches=ssid?rc==0&&!strcmp(value.ssid,ssid)&&!strcmp(value.password,password):rc==PORTABLE_WIFI_CREDENTIALS_EMPTY;
 portable_wifi_credentials_clear(&value);reader=NULL;return matches;
}
