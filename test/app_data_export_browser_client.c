/* Execute the actual shared browser enumeration, preview and copy paths. */
#define PORTABLE_FILE_BROWSER_CAPABILITY "storage.app-data.export"
#define main export_reference_browser_main
#include EXPORT_BROWSER_FIXTURE_SOURCE
#undef main
static void (*export_yield_hook)(void);
static void export_yield(uint32_t n){test_yield(n);if(export_yield_hook){void(*hook)(void)=export_yield_hook;export_yield_hook=NULL;hook();}}
void export_browser_set_yield(void(*hook)(void)){export_yield_hook=hook;}
bool export_browser_copy(const risc_storage_volume_api_v1 *api,const char *from,const char *to){
 assert(portable_file_browser_safe());static risc_runtime_api_v1 runtime;runtime=test_runtime;runtime.yield_ms=export_yield;fb_runtime=&runtime;return fbx_copy(api,from,api,to);
}
bool export_browser_safe(void){return portable_file_browser_safe();}
const char* export_browser_status(void){return fb_status;}
unsigned export_browser_list(const risc_storage_volume_api_v1*api,const char*path,char names[6][128]){
 assert(portable_file_browser_safe());fb_runtime=&test_runtime;fb_volume=api;fb_mode=FB_LIST;fb_page_rows=6;fb_query[0]=0;strcpy(fb_path,path);
 assert(fb_load(0));for(unsigned i=0;i<fb_count;++i)strcpy(names[i],fb_rows[i].name);return fb_count;
}
unsigned export_browser_preview(const risc_storage_volume_api_v1*api,const char*path,void*out,unsigned capacity){
 fb_runtime=&test_runtime;fb_volume=api;strcpy(fb_file_path,path);fb_offset=0;
 bool directory=false;assert(api->stat(api->context,path,&fb_size,&directory)&&!directory);
 assert(fb_preview_read()&&fb_preview_count<=capacity);memcpy(out,fb_preview,fb_preview_count);return fb_preview_count;
}
