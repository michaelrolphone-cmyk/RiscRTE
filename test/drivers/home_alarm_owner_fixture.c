/* Controlled scheduler owner; the facade itself is compiled from its pinned owning source. */
#include "AlarmClockChangeV1.h"
#include "PointsServiceProjection.h"
#include "RiscProviderV2.h"
#include <assert.h>
#include <string.h>
#ifndef OWNER_API
#define OWNER_API 1
#endif
#ifndef OWNER_ID
#define OWNER_ID "alarm-owner"
#endif
extern void home_alarm_lifecycle(unsigned);
extern int home_alarm_fault(void);
static unsigned value;
static bool held,quiescence_failed;
static int32_t status(void*c,alarm_status_v1*out){assert(c==&value);home_alarm_lifecycle(3);if(home_alarm_fault())return ALARM_RETAINED;*out=(alarm_status_v1){.api_version=1,.struct_size=sizeof(*out),.state=ALARM_STATE_READY,.snapshot=value};return ALARM_OK;}
static int32_t step(void*c){assert(c==&value);home_alarm_lifecycle(3);++value;return ALARM_OK;}
static int32_t acknowledge(void*c,const alarm_token_v1*t){assert(t&&t->generation==17);return step(c);}
static int32_t prepare(void*c,alarm_sleep_v1*out){assert(c==&value);home_alarm_lifecycle(3);*out=(alarm_sleep_v1){sizeof(*out),value,100,103};return ALARM_OK;}
static int32_t resume(void*c,const alarm_sleep_v1*t){assert(t&&t->snapshot==value);return step(c);}
#if OWNER_API == 1
static int32_t changed(void*c,uint32_t rtc){assert(c==&value&&rtc==1234);value+=100;home_alarm_lifecycle(3);return ALARM_OK;}
#else
static int32_t projection(void*c,points_catalog_projection*out){assert(c==&value&&out&&out->struct_size>=sizeof(*out));home_alarm_lifecycle(3);return ALARM_OK;}
#endif
#if OWNER_API == 1
static alarm_clock_change_v1 api={{{1,sizeof(api),&value,status,step,step,acknowledge,prepare,step},resume},ALARM_CLOCK_CHANGE_TAG,1,changed};
#else
static points_service_v2 api={{{2,sizeof(api),&value,status,step,step,acknowledge,prepare,step},ALARM_SERVICE_DESCRIPTOR_TAG,1,ALARM_MODE_VIBRATE,ALARM_DESCRIPTOR_RESUME_SLEEP,resume},{POINTS_SERVICE_PROJECTION_TAG,1,projection}};
#endif
static bool start(const risc_provider_dependency_v1*d,size_t count){
 (void)d;assert(!held&&!count);held=true;value=0;quiescence_failed=false;home_alarm_lifecycle(1);
#ifdef OWNER_MALFORMED
#if OWNER_API == 1
 api.tag=0;
#else
 api.base.tag=0;
#endif
#endif
 return true;
}
static bool quiesce(void){assert(held);home_alarm_lifecycle(4);if(home_alarm_fault()&&!quiescence_failed){quiescence_failed=true;return false;}return true;}
static void stop(void){assert(held);held=false;home_alarm_lifecycle(2);}
static const risc_driver_v2 driver={2,sizeof(driver),OWNER_ID,ALARM_SERVICE_CAPABILITY,OWNER_API,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
