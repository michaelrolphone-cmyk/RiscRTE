/* Test driver calls the unmodified selected Points controller functions. */
#include <assert.h>
#define app_main capacity_original_points_main
#ifndef CAPACITY_POINTS_SOURCE
#error "The fixture runner must select CAPACITY_POINTS_SOURCE"
#endif
#include CAPACITY_POINTS_SOURCE
#undef app_main
extern void capacity_phase(const char *);
extern void capacity_control(unsigned);
extern const char *capacity_mode(void);
extern const char *capacity_points_expected(void);
extern bool capacity_hardware(void);
const t5_app_manifest_t portable_catalog[]={{.compatible=false}};
const unsigned portable_catalog_count=0;
static void settle_text(void){for(unsigned i=0;i<6;i++){pc_name_step();assert(name_client.active&&!name_client.retained);}}
static void neutral(void){t5_app_input_t input={0};assert(app->poll(&input,20));}
__attribute__((visibility("default"))) void app_main(void) {
 app=t5_app_get_api(1);assert(app);paper=paper_presentation_get();nova=paper?NULL:springboard_presentation_get();
 assert(paper);acquired=0;ready=pc_open();assert(ready);pc_load();assert(pc_live()&&editor.store.loaded&&clock_valid&&service_valid);
 notice="";return_page=PC_EDIT;pc_page(PC_LIST);dirty=true;pc_draw();assert(portable_paper_frame_drain());
 capacity_phase("points-initialized");
 assert(points_editor_begin_type(&editor,0));pc_page(PC_CUSTOM);strcpy(editor.type.name,"Original");
 pc_custom_action(0);assert(name_client.active&&page==PC_NAME);capacity_phase("points-modal-open");
 assert(!portable_app_before_launch("default.elf")&&!pc_back());capacity_phase("points-handoff-fenced");settle_text();
 capacity_control(1);pc_name_step();assert(name_client.active&&!strcmp(name_result.text,capacity_points_expected()));capacity_phase("points-modal-edited");
 const char *mode=capacity_mode();
 if(!strcmp(mode,"pending-close")&&!capacity_hardware()) {
  capacity_control(4);pc_name_step();assert(!pc_name_close());assert(name_client.active&&name_client.closing);capacity_phase("points-close-pending");
  assert(!pc_name_close());capacity_control(5);assert(pc_name_close());pc_page(PC_CUSTOM);
 }else if(!strcmp(mode,"pending-close")) {
  capacity_control(4);capacity_control(2);pc_name_step();capacity_phase("points-close-pending");
  assert(name_client.active);pc_name_step();assert(name_client.active);capacity_control(5);
 }else {settle_text();if(!strcmp(mode,"retained-close"))capacity_control(9);capacity_control(!strcmp(mode,"cancelled")?3:!strcmp(mode,"back")?6:2);}
 for(unsigned i=0;i<12&&name_client.active&&!name_client.retained;i++)pc_name_step();
 if(!strcmp(mode,"retained-close")){assert(name_client.retained&&retained);capacity_phase("points-modal-retained");return;}
 assert(!name_client.active&&!name_client.acquired&&!name_client.suspended&&!retained);
 assert(!strcmp(editor.type.name,(!strcmp(mode,"cancelled")||!strcmp(mode,"back"))?"Original":(!strcmp(mode,"pending-close")&&!capacity_hardware())?"Original":capacity_points_expected()));
 capacity_phase("points-modal-closed");neutral();if(!strcmp(mode,"handoff")){assert(page==PC_CUSTOM);assert(!pc_back()&&page==PC_TYPES);assert(!pc_back()&&page==PC_LIST);assert(pc_back());capacity_phase("points-handoff-requested");}pc_close();assert(!retained);capacity_phase("points-cleanup");
}
