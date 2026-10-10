/* Host fixture drives production Home startup/handoff functions. */
#include <assert.h>
#define app_main capacity_original_home_main
#ifndef CAPACITY_HOME_SOURCE
#error "The fixture runner must select CAPACITY_HOME_SOURCE"
#endif
#include CAPACITY_HOME_SOURCE
#undef app_main
extern void capacity_home_adapter_debug(void);
extern void capacity_phase(const char *);
extern const char *capacity_client(void);
const t5_app_manifest_t portable_catalog[]={{.compatible=false}};
const unsigned portable_catalog_count=0;
__attribute__((visibility("default"))) void app_main(void) {
 bool started=sparse_boot();capacity_phase("home-started");capacity_home_adapter_debug();fprintf(stderr,"home started=%u desk_retained=%u promoted=%u ready=%u sleep_retained=%u\n",started,desk_retained,desk_promoted,portable_desk_adapter_ready(),portable_app_sleep_retained());assert(started);
 close_clock();assert(!desk_retained);capacity_phase("home-before-handoff");
 int result=portable_resident_run_foreground(capacity_client());
 capacity_phase("home-after-handoff");
 if(result<0){assert(portable_app_sleep_retained());return;}
 assert(result==1);open_clock();capacity_phase("home-restored");
}
