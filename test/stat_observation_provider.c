/* New host fixture: exact released provider and unchanged FatFs on RAM media. */
#define main existing_storage_main
#include WAVE_STAT_TEST_SOURCE
#undef main
static unsigned observed_,metadata_,ready_;
static int inject_;
static int32_t (*original_observe_)(void*);
static int32_t (*original_metadata_)(void*,const char*,risc_storage_metadata_v1*);
static bool (*original_ready_)(void*);
static int32_t counted_observe(void*c){++observed_;return original_observe_(c);}
static int32_t counted_metadata(void*c,const char*p,risc_storage_metadata_v1*out){++metadata_;int32_t rc=original_metadata_(c,p,out);if(inject_)io_failed=true;return rc;}
static bool counted_ready(void*c){++ready_;return original_ready_(c);}
const risc_storage_volume_api_v1 *probe_start(void){
 make_disk();risc_provider_dependency_v1 deps[5];memcpy(deps,deps_,sizeof(deps_));deps[4]=(risc_provider_dependency_v1){RISC_STORAGE_WORKER_CAPABILITY,1,&worker_};
 const risc_driver_v2*d=t5_driver_get(2);assert(start(deps,5));const risc_storage_volume_api_v1*v=d->capability;assert(v->refresh(NULL));
 const risc_storage_volume_api_v1_ext*x=risc_storage_volume_extension(v);assert(x&&x->mkdir(NULL,"/Apps")&&x->mkdir(NULL,"/Apps/alarms"));risc_storage_file_t f=v->file_open_write(NULL,"/Apps/alarms/alarms.elf");assert(f&&v->file_close(NULL,f,true));
 original_observe_=filesystem_api.state.observe;filesystem_api.state.observe=counted_observe;
 original_metadata_=filesystem_api.metadata;filesystem_api.metadata=counted_metadata;
 original_ready_=state_api.prepared.base.sleep.terminal.power.volume.base.ready;state_api.prepared.base.sleep.terminal.power.volume.base.ready=counted_ready;
 return v;
}
void probe_mode(const char*m){
 if(!strcmp(m,"active-session")){uint64_t s=0;assert(async_begin_checked(NULL,&s)==0&&s);}
 else if(!strcmp(m,"sleep-prepared"))assert(prepare_sleep(NULL));
 else if(!strcmp(m,"owner-refused"))owner_=false;
 else if(!strcmp(m,"io-unavailable"))io_failed=true;
 else if(!strcmp(m,"provider-retained"))poisoned=true;
 else if(!strcmp(m,"post-unavailable")||!strcmp(m,"post-failed-unavailable"))inject_=1;
 observed_=metadata_=ready_=calls_=reads_=writes_=0;
}
void probe_counts(unsigned*out){out[0]=observed_;out[1]=metadata_;out[2]=ready_;out[3]=calls_;out[4]=reads_;out[5]=writes_;}
