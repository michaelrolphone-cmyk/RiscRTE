/* Synthetic physical HCI API loaded through the actual Runtime provider graph. */
#include <RiscProviderV2.h>
#include <PortableBluetoothControl.h>
#include <assert.h>
extern bool native_attached_hci_set(bool);
extern bool native_attached_hci_status(uint8_t*);
static bool set(void*c,bool on){(void)c;return native_attached_hci_set(on);}
static bool status(void*c,uint8_t*out){(void)c;return native_attached_hci_status(out);}
static const portable_bluetooth_control_v1 api={.api_version=1,.struct_size=sizeof(api),.set_enabled=set,.status=status};
static bool start(const risc_provider_dependency_v1*d,size_t n){assert(n==1&&d&&d[0].api);return true;}
static void stop(void){}
static bool quiesce(void){return true;}
static const risc_driver_v2 descriptor={2,sizeof(descriptor),"attached-test-hci","bluetooth.hci",1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t v){return v==2?&descriptor:0;}
