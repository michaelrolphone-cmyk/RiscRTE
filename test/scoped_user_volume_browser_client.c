/* Include the unchanged shared browser/controller/adapter host fixture. Its
 * original main is compiled but not run. Only the actual fbx_copy path below
 * is selected; this wrapper adds no substitute copy implementation. */
#define PORTABLE_FILE_BROWSER_CAPABILITY "storage.volume"
#define main scoped_reference_browser_main
#include SCOPED_BROWSER_FIXTURE_SOURCE
#undef main

bool scoped_browser_copy(const risc_storage_volume_api_v1 *api,const char *from,const char *to){
    assert(portable_file_browser_safe());
    fb_runtime=&test_runtime;
    return fbx_copy(api,from,api,to);
}
bool scoped_browser_copy_safe(void){return portable_file_browser_safe();}
bool scoped_browser_retry_close(void){return fbx_close_owned();}
const char *scoped_browser_copy_status(void){return fb_status;}
unsigned scoped_browser_copy_yields(void){return test_ticks;}
bool scoped_browser_storage_layout(size_t base,size_t ext,size_t checked,size_t errors,size_t rename){
    return base==sizeof(risc_storage_volume_api_v1) && ext==sizeof(risc_storage_volume_api_v1_ext) &&
        checked==offsetof(risc_storage_volume_api_v1_ext,dir_close_checked) &&
        errors==offsetof(risc_storage_volume_api_v1_ext,handle_error) &&
        rename==offsetof(risc_storage_volume_api_v1_ext,rename);
}
