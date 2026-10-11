#include <RiscProviderV2.h>
#include "scoped_volume_provider_control.h"
#include <string.h>
#ifndef FIXTURE_ID
#define FIXTURE_ID "fixture-user-storage"
#endif
static const scoped_volume_provider_control* control;
static risc_storage_volume_api_v1_ext volume={.base={.api_version=1,.struct_size=sizeof(volume)}};
static bool start(const risc_provider_dependency_v1*deps,size_t n){
 if(control || !deps || n!=1 || strcmp(deps[0].capability_id,"fixture.volume") || deps[0].api_version!=1 || !deps[0].api)return false;
 const scoped_volume_provider_control*c=deps[0].api;
 if(c->api_version!=1 || c->struct_size!=sizeof(*c) || !c->volume || !c->start || !c->quiesce || !c->stop)return false;
 control=c;volume=*c->volume;return c->start(c->context);
}
static bool quiesce(void){return !control || control->quiesce(control->context);}
static void stop(void){if(control)control->stop(control->context);control=0;memset(&volume,0,sizeof(volume));volume.base.api_version=1;volume.base.struct_size=sizeof(volume);}
static const risc_driver_v2 driver={2,sizeof(driver),FIXTURE_ID,"storage.volume",1,&volume,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:0;}
