/* Host-only seam: unchanged pinned asynchronous Waveshare driver/FatFs and its
 * RAM-card peripheral harness. No host mounts, device files, or physical I/O. */
#define t5_driver_get actual_wave_driver_get
#define main unused_wave_storage_main
#include WAVE_SERIAL_TEST_SOURCE
#undef main
#undef t5_driver_get

static unsigned graph_starts,graph_quiesces,graph_stops;
extern void wave_serial_graph_loaded(unsigned (*counter)(unsigned));
extern void wave_serial_graph_unloaded(unsigned (*counter)(unsigned));
/* The real graph independently maps the provider ELF. Platform mocks remain
 * owned by the initial host fixture; the graph's driver owns its own FatFs,
 * handle table and lifecycle. Report each independently mapped driver exactly. */
static void owner_io(void){assert(owner_&&!working_&&!job_);}
static bool owner_open(void*c,risc_sdmmc_card_info_v1*i){owner_io();return work_open(c,i);}
static bool owner_read(void*c,uint64_t l,uint32_t n,void*b){owner_io();return work_read(c,l,n,b);}
static bool owner_write(void*c,uint64_t l,uint32_t n,const void*b){owner_io();return work_write(c,l,n,b);}
static bool owner_sync(void*c){owner_io();return work_sync(c);}
static bool owner_close(void*c){owner_io();return work_close(c);}

static const risc_storage_volume_api_v1 *volume(void){return actual_wave_driver_get(2)->capability;}

/* Prepare synthetic sibling content before the actual Runtime graph starts.
 * Leave the real provider cold and unmounted; Runtime owns the explicitly
 * selected prepareVolume refresh before the app starts. */
__attribute__((visibility("default"))) void wave_serial_seed(void){
    work_io_.open=owner_open;work_io_.read=owner_read;work_io_.write=owner_write;work_io_.sync=owner_sync;work_io_.close=owner_close;
    make_disk();hardware_.instance_id=12;
    risc_provider_dependency_v1 deps[5];memcpy(deps,deps_,sizeof(deps_));
    deps[4]=(risc_provider_dependency_v1){RISC_STORAGE_WORKER_CAPABILITY,1,&worker_};
    const risc_storage_volume_api_v1*v=volume();
    assert(start(deps,5)&&!opened_&&v->refresh(NULL)&&v->ready(NULL));
    const risc_storage_volume_api_v1_ext*x=risc_storage_volume_extension(v);assert(x);
    assert(x->mkdir(NULL,"/Books")&&x->mkdir(NULL,"/Books/nested"));
    uint8_t sibling[193];for(unsigned i=0;i<sizeof(sibling);++i)sibling[i]=(uint8_t)(i*37u+11u);
    uint32_t h=v->file_open_write(NULL,"/Books/sibling.bin");assert(h);
    assert(v->file_write(NULL,h,sibling,sizeof(sibling))==sizeof(sibling)&&v->file_close(NULL,h,true));
    h=v->file_open_write(NULL,"/Books/nested/keep.txt");assert(h);
    assert(v->file_write(NULL,h,"nested sibling",14)==14&&v->file_close(NULL,h,true));
    h=v->file_open_write(NULL,"/unrelated.txt");assert(h);
    assert(v->file_write(NULL,h,"root sibling",12)==12&&v->file_close(NULL,h,true));
    clean();assert(!async_adapter.session&&!worker_scope_claimed);
    calls_=reads_=writes_=0;
}
__attribute__((visibility("default"))) const void *wave_serial_platform(unsigned index){
    switch(index){case 0:return &gpio_;case 1:return &clock_;case 2:return &sync_api_;case 3:return &worker_;default:abort();}
}
__attribute__((visibility("default"))) void wave_serial_fault(unsigned which){
    switch(which){case 0:missing_=true;break;case 1:fail_unlock_=true;break;case 2:fail_release_=true;break;case 3:cut_=writes_+1;break;default:abort();}
}
__attribute__((visibility("default"))) unsigned wave_serial_counter(unsigned which){
    switch(which){case 0:return calls_;case 1:return reads_;case 2:return writes_;case 3:return graph_starts;
    case 4:return graph_quiesces;case 5:return graph_stops;case 6:return has_handles();case 7:return opened_;
    case 8:return selected_;case 9:return created_;case 10:return locked_;case 11:return async_adapter.session!=0;
    case 12:return observe_state(NULL);default:abort();}
}
static bool graph_start(const risc_provider_dependency_v1*d,size_t n){
    const risc_hardware_device_v1*h=tw_dep(d,n,"hardware.device",sizeof(*h));
    assert(h&&h->instance_id==12&&n==5);++graph_starts;
    unsigned before=calls_;const bool ok=start(d,n);assert(ok&&calls_==before&&!mounted&&!opened_);wave_serial_graph_loaded(wave_serial_counter);return ok;
}
static bool graph_quiesce(void){++graph_quiesces;return quiesce();}
static void graph_stop(void){++graph_stops;stop();}
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t abi){
    const risc_driver_v2*real=actual_wave_driver_get(abi);if(!real)return NULL;
    static risc_driver_v2 wrapper;wrapper=*real;wrapper.start=graph_start;wrapper.quiesce=graph_quiesce;wrapper.stop=graph_stop;return &wrapper;
}
__attribute__((destructor)) static void fixture_unloaded(void){
    assert(!opened_&&!selected_&&!created_&&!locked_&&!has_handles()&&!async_adapter.session);
    if(graph_starts)wave_serial_graph_unloaded(wave_serial_counter);
    free(image_);image_=NULL;
}
