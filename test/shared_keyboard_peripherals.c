/* Physical devices, time, storage and unrelated services only are test doubles. */
#include "RiscRuntimeV1.h"
#include "RiscDisplayOutputSnapshotV1.h"
#include "RiscTouchV1.h"
#include "RiscInputNavigationV1.h"
#include "RiscBatteryGaugeV1.h"
#include "RiscPlatformClockV1.h"
#include "RiscUsbHidV1.h"
#include "RiscRealtimeV1.h"
#include "RiscKeyValueV1.h"
#include "PortableRtcClock.h"
#include "PortableBluetoothControl.h"
#include "WifiApi.h"
#include "ContextsServiceV1.h"
#include "TelemetryBroadcastV1.h"
#include "PointsServiceProjection.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
extern void capacity_observe(void);
extern void capacity_phase(const char *);
static uint64_t ticks,touch_sub,keyboard_sub,sequence,frame_token;
static bool held,pending,unsubscribe_fault;
static uint32_t navigation_event;
static unsigned frames,io_calls;
unsigned capacity_provider_calls(void){return io_calls;}
static uint8_t pixels[48000],completed[48000];
static risc_usb_keyboard_event_v1 keyboard_queue[16];static unsigned queued,read_at;
static risc_touch_event_v1 touch_queue[8];static unsigned touch_queued,touch_at;
bool capacity_hardware(void){return getenv("CAPACITY_INPUT")&&!strcmp(getenv("CAPACITY_INPUT"),"hardware");}
const char *capacity_points_expected(void){return capacity_hardware()?"Originala":"OriginalA";}
const char *capacity_ble_expected(void){return capacity_hardware()?"seeda":"seedA";}
static void touch_key(unsigned command){int x=command==1?75:command==3?60:400,y=command==1?504:command==3?30:688;assert(!touch_queued);touch_queue[0]=(risc_touch_event_v1){.sequence=++sequence,.timestamp_ms=ticks,.kind=RISC_TOUCH_EVENT_DOWN,.id=1,.x=x,.y=y};touch_queue[1]=(risc_touch_event_v1){.sequence=++sequence,.timestamp_ms=ticks+1,.kind=RISC_TOUCH_EVENT_UP,.id=1,.x=x,.y=y};touch_queued=2;touch_at=0;}

void capacity_control(unsigned command){
 if((command==1||command==2||command==3)&&!capacity_hardware()){touch_key(command);}
 else if(command==1||command==2||command==3){assert(queued<16);keyboard_queue[queued++]=(risc_usb_keyboard_event_v1){.sequence=++sequence,.device=7,.kind=3,.usage=(uint8_t)(command==1?4:command==2?40:41)};}
 else if(command==4)pending=true;else if(command==5)pending=false;else if(command==6)navigation_event=RISC_NAV_BACK;else if(command==9)unsubscribe_fault=true;else assert(0);
}
static void io(void){++io_calls;capacity_observe();}
bool capacity_health(risc_runtime_health_v1 *h){io();h->uptime_ms=ticks;return true;}
void capacity_delay(uint32_t n){io();ticks+=n;}
bool capacity_log(const char *s){fprintf(stderr,"%s\n",s);return true;}
static bool display_info(void*c,risc_display_info_v1*out){(void)c;io();*out=(risc_display_info_v1){.api_version=1,.struct_size=sizeof(*out),.width=800,.height=480,.supported_formats=RISC_DISPLAY_FORMAT_BIT(RISC_DISPLAY_FORMAT_MONO1),.preferred_format=RISC_DISPLAY_FORMAT_MONO1,.flags=RISC_DISPLAY_INFO_ASYNC_PRESENT|RISC_DISPLAY_INFO_RETAINS_IMAGE|RISC_DISPLAY_INFO_PARTIAL_DAMAGE|RISC_DISPLAY_INFO_CLEAN_PRESENT|RISC_DISPLAY_INFO_BRIGHTNESS,.damage_x_alignment=8,.damage_width_alignment=8};return true;}
static bool display_acquire(void*c,uint32_t f,risc_display_surface_v1*out){(void)c;io();assert(!held&&f==RISC_DISPLAY_FORMAT_MONO1);held=true;*out=(risc_display_surface_v1){.frame=1,.pixels=pixels,.width=800,.height=480,.stride_bytes=100,.size_bytes=sizeof(pixels),.pixel_format=f};return true;}
static void display_release(void*c,uint64_t f){(void)c;io();assert(held&&f==1);held=false;}
static bool display_submit(void*c,uint64_t f,const risc_display_rect_v1*r,size_t n,const risc_display_present_options_v1*o,uint64_t*t){(void)c;(void)r;(void)n;(void)o;io();assert(held&&f==1);held=false;*t=++frame_token;++frames;capacity_phase("display-submit");return true;}
static bool display_status(void*c,uint64_t f,risc_display_present_status_v1*out){(void)c;io();assert(f);out->state=pending?RISC_DISPLAY_PRESENT_ACTIVE:RISC_DISPLAY_PRESENT_COMPLETE;if(!pending)memcpy(completed,pixels,sizeof(pixels));return true;}
static bool brightness(void*c,uint16_t n,uint16_t max){(void)c;io();return n<=max;}
static bool copy_completed(void*c,uint32_t f,void*out,size_t bytes,uint32_t stride){(void)c;io();assert(f==RISC_DISPLAY_FORMAT_MONO1&&bytes==48000&&stride==100);memcpy(out,completed,bytes);return true;}
static const risc_display_output_api_v1_snapshot display={.metrics={.power={.history={.base={.api_version=1,.struct_size=sizeof(display),.get_info=display_info,.acquire=display_acquire,.release=display_release,.submit=display_submit,.present_status=display_status,.set_brightness=brightness}}}},.snapshot_tag=RISC_DISPLAY_SNAPSHOT_TAG,.snapshot_version=1,.copy_completed=copy_completed};
static uint64_t subscribe(void*c){(void)c;io();assert(!touch_sub);touch_sub=++sequence;return touch_sub;}
static bool unsubscribe(void*c,uint64_t token){(void)c;io();assert(token&&token==touch_sub);if(unsubscribe_fault)return false;touch_sub=0;return true;}
static bool touch_poll(void*c,size_t n){(void)c;(void)n;io();ticks+=10;return true;}
static int32_t touch_next(void*c,uint64_t t,risc_touch_event_v1*e){(void)c;io();assert(t==touch_sub);if(touch_at==touch_queued){touch_at=touch_queued=0;return 0;}*e=touch_queue[touch_at++];return 1;}
static bool touch_snapshot(void*c,risc_touch_snapshot_v1*out){(void)c;io();*out=(risc_touch_snapshot_v1){.sequence=sequence,.width=480,.height=800};return true;}
static const risc_touch_api_v1 touch={1,sizeof(touch),NULL,subscribe,unsubscribe,touch_poll,touch_next,touch_snapshot};
static bool nav_poll(void*c,risc_input_navigation_frame_v1*out){(void)c;io();*out=(risc_input_navigation_frame_v1){.pressed=navigation_event};navigation_event=0;return true;}
static bool nav_foreground(void*c,const risc_input_foreground_v1*f,size_t n){(void)c;(void)f;(void)n;io();return true;}
static bool nav_reset(void*c){(void)c;io();navigation_event=0;return true;}
static const risc_input_navigation_api_v1 nav={1,sizeof(nav),NULL,nav_poll,nav_foreground,nav_reset};
static uint64_t keyboard_subscribe(void*c,uint64_t n){(void)c;(void)n;io();assert(!keyboard_sub);keyboard_sub=++sequence;return keyboard_sub;}
static bool keyboard_unsubscribe(void*c,uint64_t s){(void)c;io();assert(s==keyboard_sub);keyboard_sub=0;return true;}
static bool keyboard_poll(void*c,size_t n){(void)c;(void)n;io();return true;}
static int32_t keyboard_next(void*c,uint64_t s,risc_usb_keyboard_event_v1*out){(void)c;io();assert(s==keyboard_sub);if(read_at==queued){read_at=queued=0;return 0;}*out=keyboard_queue[read_at++];return 1;}
static bool keyboard_snapshot(void*c,risc_usb_keyboard_state_v1*out,size_t*n){(void)c;io();assert(*n);*n=1;*out=(risc_usb_keyboard_state_v1){.device=7,.connected=1};return true;}
static const risc_usb_keyboard_api_v1 keyboard={1,sizeof(keyboard),NULL,keyboard_subscribe,keyboard_unsubscribe,keyboard_poll,keyboard_next,keyboard_snapshot};
static bool battery_read(void*c,risc_battery_sample_v1*out){(void)c;io();*out=(risc_battery_sample_v1){.percent=84,.millivolts=3970};return true;}
static const risc_battery_gauge_api_v1 battery={1,sizeof(battery),NULL,battery_read};
static bool rtc_read(void*c,twatch_rtc_time_v1*out){(void)c;io();*out=(twatch_rtc_time_v1){2026,10,10,6,10,0,0};return true;}
static const twatch_rtc_api_v1 rtc={.api_version=2,.struct_size=sizeof(rtc),.read=rtc_read};
static int32_t realtime_read(void*c,risc_realtime_snapshot_v1*out){(void)c;io();*out=(risc_realtime_snapshot_v1){.struct_size=sizeof(*out),.validity=RISC_REALTIME_VALID,.epoch_seconds=1791626400,.monotonic_before_us=ticks*1000,.monotonic_after_us=ticks*1000};return 0;}
static int32_t realtime_seed(void*c,int64_t t,uint32_t n){(void)c;(void)t;(void)n;io();return 0;}
const risc_realtime_control_api_v1 capacity_realtime={1,sizeof(capacity_realtime),NULL,realtime_read,realtime_seed};
static uint64_t now(void*c){(void)c;return ticks;}
const risc_platform_clock_api_v1 capacity_clock={1,sizeof(capacity_clock),NULL,now,NULL};
static int32_t alarm_status(void*c,alarm_status_v1*out){(void)c;io();*out=(alarm_status_v1){.api_version=1,.struct_size=sizeof(*out),.state=ALARM_STATE_READY,.mode=ALARM_MODE_VISUAL};return 0;}
static int32_t alarm_step(void*c){(void)c;io();return 0;}
static int32_t alarm_ack(void*c,const alarm_token_v1*t){(void)c;(void)t;io();return 0;}
static int32_t alarm_prepare(void*c,alarm_sleep_v1*t){(void)c;io();*t=(alarm_sleep_v1){.struct_size=sizeof(*t)};return 0;}
static int32_t project(void*c,points_catalog_projection*out){(void)c;io();*out=(points_catalog_projection){.struct_size=sizeof(*out)};return 0;}
static const points_service_v2 alarm={.base={.base={2,sizeof(alarm),NULL,alarm_status,alarm_step,alarm_step,alarm_ack,alarm_prepare,alarm_step},.tag=ALARM_SERVICE_DESCRIPTOR_TAG,.descriptor_version=1,.output_modes=ALARM_MODE_VISUAL},.points={POINTS_SERVICE_PROJECTION_TAG,1,project}};
static wifi_link_t wifi_status(void*c){(void)c;io();return WIFI_LINK_DOWN;}
static bool yes(void*c){(void)c;io();return true;}
static const wifi_api_v1 wifi={.api_version=1,.struct_size=sizeof(wifi),.status=wifi_status,.disconnect_checked=yes};
static bool ble_enable(void*c,bool enabled){(void)c;(void)enabled;io();return true;}
static bool ble_status(void*c,uint8_t*out){(void)c;io();*out=0;return true;}
static const portable_bluetooth_control_v1 bluetooth={.api_version=1,.struct_size=sizeof(bluetooth),.set_enabled=ble_enable,.status=ble_status};
static bool contexts_step(void*c,const contexts_policy_v1*p){(void)c;(void)p;io();return true;}
static bool contexts_status(void*c,contexts_status_v1*out){(void)c;io();*out=(contexts_status_v1){.struct_size=sizeof(*out),.state=CONTEXTS_OFF};return true;}
static bool contexts_request(void*c,uint32_t n){(void)c;(void)n;io();return true;}
static bool contexts_record(void*c,uint32_t s,uint32_t k,uint32_t i,const void*d,uint32_t n){(void)c;(void)s;(void)k;(void)i;(void)d;(void)n;io();return true;}
static bool contexts_finish(void*c,uint32_t s,uint32_t r){(void)c;(void)s;(void)r;io();return true;}
static int32_t contexts_label(void*c,uint32_t s,uint32_t i,contexts_label_v1*out){(void)c;(void)s;(void)i;(void)out;io();return 0;}
static bool contexts_claim(void*c,uint32_t s,uint32_t i,const char*n,uint32_t g){(void)c;(void)s;(void)i;(void)n;(void)g;io();return true;}
static bool contexts_result(void*c,uint32_t s,uint32_t g,uint32_t r){(void)c;(void)s;(void)g;(void)r;io();return true;}
static const contexts_service_v1 contexts={.api_version=1,.struct_size=sizeof(contexts),.step=contexts_step,.pause=yes,.status=contexts_status,.request_export=contexts_request,.begin_export=contexts_request,.export_record=contexts_record,.finish_export=contexts_finish,.label=contexts_label,.claim_preset=contexts_claim,.preset_result=contexts_result,.capture_audio=yes};
static bool broadcast_step(void*c,bool allow,const telemetry_broadcast_policy_v1*p){(void)c;(void)allow;(void)p;io();return true;}
static bool broadcast_status(void*c,telemetry_broadcast_status_v1*out){(void)c;io();*out=(telemetry_broadcast_status_v1){.struct_size=sizeof(*out),.state=TELEMETRY_BROADCAST_OFF,.settings_valid=true};return true;}
static int32_t broadcast_enumerate(void*c,uint32_t i,risc_telemetry_field_v1*out){(void)c;(void)i;(void)out;io();return 0;}
static int32_t broadcast_read(void*c,uint32_t i,int32_t*out){(void)c;(void)i;(void)out;io();return 0;}
static const telemetry_broadcast_v1 broadcast={.api_version=1,.struct_size=sizeof(broadcast),.step=broadcast_step,.pause=yes,.status=broadcast_status,.enumerate=broadcast_enumerate,.read=broadcast_read};
const void *capacity_provider(const char *name){
 if(!strcmp(name,"display.output"))return &display;
 if(!strcmp(name,"input.touch.raw"))return &touch;
 if(!strcmp(name,"input.navigation"))return &nav;
 if(!strcmp(name,"board.battery"))return &battery;
 if(!strcmp(name,"rtc.clock"))return &rtc;
 if(!strcmp(name,"alarm.service"))return &alarm;
 if(!strcmp(name,"net.wifi"))return &wifi;
 if(!strcmp(name,"bluetooth.hci"))return &bluetooth;
 if(!strcmp(name,"contexts.service"))return &contexts;
 if(!strcmp(name,"telemetry.broadcast"))return &broadcast;
 if(!strcmp(name,"usb.hid.keyboard"))return &keyboard;
 static const uint32_t dummy[]={1,8};assert(!strncmp(name,"test.padding",12)||!strcmp(name,"x4.power")||!strcmp(name,"storage.volume")||!strcmp(name,"bluetooth.sensors"));return dummy;
}
static struct {uint32_t ns;char key[32];unsigned char bytes[256];uint32_t n;} cells[128];
int32_t capacity_kv_get(void*c,uint32_t ns,const char*k,void*out,uint32_t cap,uint32_t*n){(void)c;io();*n=0;for(unsigned i=0;i<128;i++)if(cells[i].ns==ns&&!strcmp(k,cells[i].key)){*n=cells[i].n;if(cap<*n)return RISC_KEY_VALUE_BUFFER_SMALL;memcpy(out,cells[i].bytes,*n);return 0;}return RISC_KEY_VALUE_NOT_FOUND;}
int32_t capacity_kv_put(void*c,uint32_t ns,const char*k,const void*in,uint32_t n){(void)c;io();assert(n<=256&&strlen(k)<32);unsigned i;for(i=0;i<128&&cells[i].key[0]&&(cells[i].ns!=ns||strcmp(k,cells[i].key));i++);assert(i<128);cells[i].ns=ns;strcpy(cells[i].key,k);memcpy(cells[i].bytes,in,n);cells[i].n=n;return 0;}
void capacity_verify(bool retained){if(!retained)assert(!held&&!touch_sub&&!keyboard_sub);printf("peripherals frames=%u touch=%llu keyboard=%llu retained=%u\n",frames,(unsigned long long)touch_sub,(unsigned long long)keyboard_sub,retained);}
