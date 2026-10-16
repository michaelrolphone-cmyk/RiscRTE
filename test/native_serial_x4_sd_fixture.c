/* Actual pinned X4 source + shared FatFs. Only typed platform dependencies,
 * RAM block media and lifecycle counters are modeled. No legacy GPIO path. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static uint32_t fixture_cycles(void){static uint32_t cycles;return cycles+=8;}
#define X4PRO_SD_CYCLE_COUNT() fixture_cycles()
#define X4PRO_SD_ASYNC 1
#define t5_driver_get actual_x4_driver_get
#include X4_SERIAL_DRIVER_SOURCE
#undef t5_driver_get
static unsigned graph_starts,graph_quiesces,graph_stops;
extern void x4_serial_graph_loaded(unsigned(*)(unsigned));
extern unsigned x4_serial_transport_calls(void);
extern void x4_serial_graph_unloaded(unsigned(*)(unsigned));
static unsigned char* image;
static const unsigned sectors=131072;
static bool locked,created,scope,bound,opened,missing,fail_unlock,fail_release,fail_write;
static unsigned io_calls,reads,writes;
static uint64_t now;
static void owner_io(void){assert(locked&&scope&&bound&&!async_adapter.session);}
static bool owner(void*c){(void)c;return true;}
static bool create(void*c,uint64_t*out){(void)c;assert(!created);created=true;*out=1;return true;}
static bool take(void*c,uint64_t token){(void)c;assert(created&&token==1);if(locked)return false;locked=true;return true;}
static bool unlock(void*c,uint64_t token){(void)c;assert(created&&locked&&token==1);if(fail_unlock)return false;locked=false;return true;}
static bool destroy(void*c,uint64_t token){(void)c;assert(created&&!locked&&token==1);created=false;return true;}
static const risc_provider_sync_api_v1 sync_fixture={1,sizeof(sync_fixture),NULL,owner,create,take,unlock,destroy};
static uint64_t clock_now(void*c){(void)c;return now;}
static void clock_delay(void*c,uint32_t ms){(void)c;owner_io();now+=ms;}
static const risc_platform_clock_api_v1 clock_fixture={1,sizeof(clock_fixture),NULL,clock_now,clock_delay};
static bool no_gpio_claim(void*c,uint8_t p,bool o,bool i,bool u,uint64_t*t){(void)c;(void)p;(void)o;(void)i;(void)u;(void)t;abort();}
static bool no_gpio_write(void*c,uint64_t t,bool v){(void)c;(void)t;(void)v;abort();}
static bool no_gpio_read(void*c,uint64_t t,bool*v){(void)c;(void)t;(void)v;abort();}
static bool no_gpio_release(void*c,uint64_t t){(void)c;(void)t;abort();}
static int32_t no_gpio_hold(void*c,uint64_t t,bool v){(void)c;(void)t;(void)v;abort();}
static bool no_native_open(void*c,uint8_t k,uint8_t m,uint8_t d,uint32_t hz,uint64_t*t,risc_sdmmc_card_info_v1*i){(void)c;(void)k;(void)m;(void)d;(void)hz;(void)t;(void)i;abort();}
static bool no_native_read(void*c,uint64_t t,uint64_t l,uint32_t n,void*b){(void)c;(void)t;(void)l;(void)n;(void)b;abort();}
static bool no_native_write(void*c,uint64_t t,uint64_t l,uint32_t n,const void*b){(void)c;(void)t;(void)l;(void)n;(void)b;abort();}
static bool no_native_token(void*c,uint64_t t){(void)c;(void)t;abort();}
static const risc_gpio_sdmmc_api_v1 gpio_fixture={
 .base={.api_version=1,.struct_size=sizeof(gpio_fixture),.claim=no_gpio_claim,.write=no_gpio_write,.read=no_gpio_read,.release=no_gpio_release,.deep_sleep_hold=no_gpio_hold},
 .sdmmc_tag=RISC_GPIO_SDMMC_TAG_V1,.sdmmc_version=1,
 .sdmmc={1,sizeof(risc_sdmmc_host_api_v1),NULL,no_native_open,no_native_read,no_native_write,no_native_token,no_native_token}};
static bool on_worker(void*c){(void)c;return false;}
static bool power(void*c,bool high){(void)c;(void)high;owner_io();++io_calls;return true;}
static bool open_card(void*c,risc_sdmmc_card_info_v1*out){(void)c;owner_io();assert(!opened);++io_calls;if(missing)return false;opened=true;*out=(risc_sdmmc_card_info_v1){sizeof(*out),512,sectors,20000000,0};return true;}
static bool read_card(void*c,uint64_t lba,uint32_t n,void*out){(void)c;owner_io();assert(opened&&n&&n<=8&&lba+n<=sectors);++io_calls;++reads;memcpy(out,image+lba*512,n*512);return true;}
static bool write_card(void*c,uint64_t lba,uint32_t n,const void*in){(void)c;owner_io();assert(opened&&n&&n<=8&&lba+n<=sectors);++io_calls;++writes;if(fail_write)return false;memcpy(image+lba*512,in,n*512);return true;}
static bool sync_card_fixture(void*c){(void)c;owner_io();assert(opened);++io_calls;return true;}
static bool close_card(void*c){(void)c;owner_io();++io_calls;opened=false;return true;}
static const risc_storage_work_io_v1 io_fixture={NULL,on_worker,clock_now,power,clock_delay,open_card,read_card,write_card,sync_card_fixture,close_card};
static int32_t claim_scope(void*c,const risc_storage_work_scope_v1*s){(void)c;assert(created&&!scope&&s->power==5&&s->clk==41&&s->cmd==42&&s->dat0==40);scope=true;return 0;}
static int32_t bind_worker(void*c,risc_storage_work_execute_v1 f,void*x){(void)c;(void)x;assert(scope&&!bound&&f);bound=true;return 0;}
static int32_t submit(void*c,const void*p,uint32_t n,uint64_t*t){(void)c;(void)p;(void)n;(void)t;abort();}
static int32_t collect(void*c,uint64_t t,void*p,uint32_t n,uint32_t*s,int32_t*r){(void)c;(void)t;(void)p;(void)n;(void)s;(void)r;abort();}
static int32_t cancel(void*c,uint64_t t){(void)c;(void)t;abort();}
static int32_t unbind_worker(void*c){(void)c;assert(scope&&bound&&!opened);bound=false;return 0;}
static int32_t release_scope(void*c){(void)c;assert(scope&&!bound&&!opened);if(fail_release)return RISC_STORAGE_WORK_RETAINED;scope=false;return 0;}
static const risc_storage_worker_v1 worker_fixture={1,sizeof(worker_fixture),NULL,bind_worker,submit,collect,cancel,unbind_worker,claim_scope,release_scope,&io_fixture};
static int32_t diagnostic(void*c,uint32_t slot,char*p,uint32_t n,uint32_t*w,uint64_t*s,uint32_t*r){(void)c;(void)slot;assert(n);*p=0;*w=0;*s=0;*r=0;return 0;}
static const risc_diagnostic_source_api_v1 diagnostic_fixture={1,sizeof(diagnostic_fixture),NULL,diagnostic};
static bool power_ready_fixture(void*c){(void)c;return true;}
static const x4_power_ready_api_v1 power_fixture={1,sizeof(power_fixture),NULL,power_ready_fixture};
static risc_hw_gpio_bank_v1 config_fixture={sizeof(config_fixture),4,1,1,0,{5,41,42,40,0,0,0,0},0,0,0};
static const risc_hardware_device_v1 hardware_fixture={1,sizeof(hardware_fixture),9,"xteink,x4-pro-sd-native1","unspecified","gpio.bank",1,sizeof(config_fixture),&config_fixture};
static void put16(unsigned char*p,uint16_t v){p[0]=v;p[1]=v>>8;}
static void put32(unsigned char*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=v>>(i*8);}
static void make_disk(void){
 image=calloc(sectors,512);assert(image);unsigned char*b=image;b[0]=0xeb;b[1]=0x58;b[2]=0x90;memcpy(b+3,"MSDOS5.0",8);put16(b+11,512);b[13]=1;put16(b+14,32);b[16]=2;b[21]=0xf8;
 put32(b+32,sectors);put32(b+36,1024);put32(b+44,2);put16(b+48,1);b[66]=0x29;memcpy(b+82,"FAT32   ",8);b[510]=0x55;b[511]=0xaa;
 for(unsigned f=0;f<2;++f){unsigned char*p=image+(32+f*1024)*512;put32(p,0xffffff8);put32(p+4,0xffffffff);put32(p+8,0xfffffff);}
}
__attribute__((visibility("default"))) void x4_serial_seed(void){
 make_disk();const risc_storage_volume_api_v1*v=actual_x4_driver_get(2)->capability;
 const risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hardware_fixture},{"platform.gpio",1,&gpio_fixture.base},{"platform.clock",1,&clock_fixture},{"platform.sync",1,&sync_fixture},{"board.power.ready",1,&power_fixture},{RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,&diagnostic_fixture},{RISC_STORAGE_WORKER_CAPABILITY,1,&worker_fixture}};
 assert(start(deps,7)&&!mounted&&v->refresh(NULL)&&v->ready(NULL));
 const risc_storage_volume_api_v1_ext*x=risc_storage_volume_extension(v);assert(x&&x->mkdir(NULL,"/Books")&&x->mkdir(NULL,"/Books/nested"));
 unsigned char sibling[193];for(unsigned i=0;i<sizeof(sibling);++i)sibling[i]=(unsigned char)(i*37u+11u);
 uint32_t h=v->file_open_write(NULL,"/Books/sibling.bin");assert(h&&v->file_write(NULL,h,sibling,sizeof(sibling))==sizeof(sibling)&&v->file_close(NULL,h,true));
 h=v->file_open_write(NULL,"/Books/nested/keep.txt");assert(h&&v->file_write(NULL,h,"nested sibling",14)==14&&v->file_close(NULL,h,true));
 h=v->file_open_write(NULL,"/unrelated.txt");assert(h&&v->file_write(NULL,h,"root sibling",12)==12&&v->file_close(NULL,h,true));
 assert(quiesce());stop();assert(!scope&&!bound&&!opened&&!locked&&!created);io_calls=reads=writes=0;
}
__attribute__((visibility("default"))) const void*x4_serial_platform(unsigned i){
 switch(i){case 0:return &gpio_fixture.base;case 1:return &clock_fixture;case 2:return &sync_fixture;case 3:return &worker_fixture;case 4:return &diagnostic_fixture;case 5:return &power_fixture;default:abort();}
}
__attribute__((visibility("default"))) void x4_serial_fault(unsigned i){switch(i){case 0:missing=true;break;case 1:fail_unlock=true;break;case 2:fail_release=true;break;case 3:fail_write=true;break;default:abort();}}
__attribute__((visibility("default"))) unsigned x4_serial_counter(unsigned i){
 switch(i){case 0:return io_calls;case 1:return reads;case 2:return writes;case 3:return graph_starts;case 4:return graph_quiesces;case 5:return graph_stops;case 6:return has_handles();case 7:return opened;case 8:return worker_scope_claimed;case 9:return operation_mutex!=0;case 10:return locked;case 11:return async_adapter.session!=0;case 12:return observe_state(NULL);default:abort();}
}
static bool graph_start(const risc_provider_dependency_v1*d,size_t n){
 assert(n==7);const risc_hardware_device_v1*h=NULL;for(size_t i=0;i<n;++i)if(!strcmp(d[i].capability_id,"hardware.device"))h=d[i].api;assert(h&&h->instance_id==9);++graph_starts;
 const unsigned before=x4_serial_transport_calls();const bool ok=start(d,n);assert(ok&&x4_serial_transport_calls()==before&&!mounted);x4_serial_graph_loaded(x4_serial_counter);return ok;
}
static bool graph_quiesce(void){++graph_quiesces;return quiesce();}
static void graph_stop(void){++graph_stops;stop();}
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t abi){
 const risc_driver_v2*real=actual_x4_driver_get(abi);if(!real)return NULL;
 /* Preserve the full actual diagnostic/stream/poll/service descriptor suffix. */
 static risc_driver_service_v2 wrapper;assert(real->struct_size==sizeof(wrapper));memcpy(&wrapper,real,sizeof(wrapper));
 risc_driver_v2*base=&wrapper.poll.streams.driver;base->start=graph_start;base->quiesce=graph_quiesce;base->stop=graph_stop;return base;
}
__attribute__((destructor)) static void unloaded(void){
 assert(!opened&&!scope&&!bound&&!locked&&!created&&!has_handles());if(graph_starts)x4_serial_graph_unloaded(x4_serial_counter);free(image);
}
