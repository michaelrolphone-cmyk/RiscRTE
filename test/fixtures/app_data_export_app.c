#include <RiscRuntimeV1.h>
#include <RiscAppDataExportV1.h>
#include <RiscResidentShellV1.h>
#include <assert.h>
#include <string.h>
extern int export_test_mode(void);
extern int export_test_phase(void);
extern unsigned export_test_backend_calls(void);
extern void export_test_next(void);
extern void export_test_owner(int);
extern void export_test_alloc_fail(int);
extern void export_test_broker_alloc_fail(int);
extern void export_test_fault(void);
extern void export_test_keep(int,risc_app_data_export_v1);
extern void export_test_probe(int,int);
extern void export_test_finished(int);
extern void export_test_update(void);
extern void export_test_reset_safe(int);
#if defined(RESIDENT) && !defined(CHILD)
static int32_t dispatch(void* c,const risc_resident_request_v1* q,risc_resident_reply_v1* p) {
 (void)c;(void)q;(void)p;export_test_probe(0,1);export_test_probe(1,0);return RISC_RESIDENT_OK;
}
#endif
#ifdef RESIDENT
__attribute__((visibility("default"))) const risc_resident_app_descriptor_v1_t risc_resident_app_descriptor_v1={
 1,sizeof(risc_resident_app_descriptor_v1_t),
#ifdef CHILD
 RISC_RESIDENT_ROLE_FOREGROUND,
#else
 RISC_RESIDENT_ROLE_HOST,
#endif
 0};
#endif
__attribute__((visibility("default"))) void app_main(void) {
 const risc_runtime_api_v1* rt=risc_runtime_get_api(1);assert(rt);
 const int mode=export_test_mode();
 risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)};
#ifdef CHILD
 const int role=1;
 if(mode==0 || mode==5) {assert(!rt->acquire(RISC_APP_DATA_EXPORT_CAPABILITY,1,0,&grant));export_test_probe(0,0);export_test_next();return;}
#else
 const int role=0;
 if((mode==0 || mode==5) && export_test_phase()==2){export_test_probe(0,0);return;}
#endif
 assert(!rt->acquire(RISC_APP_DATA_CAPABILITY,1,41,&grant));
 assert(!rt->acquire(RISC_APP_DATA_EXPORT_CAPABILITY,2,0,&grant));
 assert(!rt->acquire(RISC_APP_DATA_EXPORT_CAPABILITY,1,41,&grant));
 export_test_broker_alloc_fail(1);
 assert(!rt->acquire(RISC_APP_DATA_EXPORT_CAPABILITY,1,0,&grant));
 export_test_broker_alloc_fail(0);
 if(mode==4){assert(!rt->acquire(RISC_APP_DATA_EXPORT_CAPABILITY,1,0,&grant));assert(!risc_runtime_get_api(1));return;}
 if(mode==5)assert(!rt->acquire(RISC_APP_DATA_EXPORT_CAPABILITY,1,0,&grant));
 assert(rt->acquire(RISC_APP_DATA_EXPORT_CAPABILITY,1,0,&grant));
 const risc_app_data_export_v1* ex=grant.api;
 const risc_storage_volume_api_v1* v=&ex->volume.terminal.power.volume.base;
 assert(v->api_version==1 && v->struct_size==sizeof(*ex) && ex->export_tag==RISC_APP_DATA_EXPORT_TAG);
 assert(!v->remove && !ex->volume.terminal.power.volume.mkdir && !ex->volume.terminal.power.volume.rename);
 assert(!ex->volume.terminal.power.prepare_power_down && !ex->volume.terminal.power.cancel_power_down);
 assert(!risc_storage_volume_power_commit(v) && !risc_storage_volume_sleep(v));
 assert(risc_app_data_export_catalog(v)==ex);
 const unsigned before_catalog=export_test_backend_calls();
 risc_app_data_export_entry_v1 entry;
 assert(ex->entry(v->context,0,&entry)==RISC_APP_DATA_OK);
 assert(!strcmp(entry.path,"/saved/state.bin") && entry.writable==1);
 assert(!entry.reserved[0] && !entry.reserved[1] && !entry.reserved[2]);
 assert(ex->entry(v->context,1,&entry)==RISC_APP_DATA_OK);
 assert(!strcmp(entry.path,"/readonly/state.bin") && entry.writable==0);
 assert(ex->entry(v->context,2,&entry)==RISC_APP_DATA_NOT_FOUND && !entry.path[0]);
 export_test_owner(0);
 assert(ex->entry(v->context,0,&entry)==RISC_APP_DATA_CONTEXT && !entry.path[0]);
 export_test_owner(1);
 assert(ex->entry(v->context,0,0)==RISC_APP_DATA_INVALID);
 assert(export_test_backend_calls()==before_catalog);
 risc_app_data_export_v1 legacy=*ex;
 legacy.volume.terminal.power.volume.base.struct_size=offsetof(risc_app_data_export_v1,entry);
 assert(risc_app_data_export(&legacy.volume.terminal.power.volume.base)==&legacy);
 assert(!risc_app_data_export_catalog(&legacy.volume.terminal.power.volume.base));
 risc_runtime_capability_v1 duplicate={.struct_size=sizeof(duplicate)};assert(!rt->acquire(RISC_APP_DATA_EXPORT_CAPABILITY,1,0,&duplicate));
 uint32_t size=0;uint64_t revision=0;
 export_test_owner(0);assert(ex->stat_revision(v->context,"/saved/state.bin",&size,&revision)==RISC_APP_DATA_CONTEXT);export_test_owner(1);
 assert(ex->stat_revision(v->context,"/saved/state.bin",&size,&revision)==RISC_APP_DATA_OK && revision && size==4);
 assert(ex->stat_revision(v->context,"/unmapped.bin",&size,&revision)!=RISC_APP_DATA_OK);
 assert(ex->replace_revision(v->context,"/readonly/state.bin",revision,"nope",4)!=RISC_APP_DATA_OK);
 assert(ex->stat_revision(v->context,"/saved/state.bin",&size,&revision)==RISC_APP_DATA_OK);
 assert(ex->replace_revision(v->context,"/saved/state.bin",revision,"edit",4)==RISC_APP_DATA_OK);
 uint64_t previous=revision;
 assert(ex->replace_revision(v->context,"/saved/state.bin",previous,"nope",4)==RISC_APP_DATA_STALE);
 assert(ex->stat_revision(v->context,"/saved/state.bin",&size,&revision)==RISC_APP_DATA_OK && revision!=previous);
 char bytes[5]={0};export_test_alloc_fail(1);
 assert(ex->read_revision(v->context,"/saved/state.bin",revision,bytes,4,&size,&revision)==RISC_APP_DATA_IO && !bytes[0]);
 assert(ex->replace_revision(v->context,"/saved/state.bin",previous,"nope",4)==RISC_APP_DATA_IO);
 export_test_alloc_fail(0);assert(ex->stat_revision(v->context,"/saved/state.bin",&size,&revision)==RISC_APP_DATA_OK);
 assert(ex->read_revision(v->context,"/saved/state.bin",revision,bytes,4,&size,&revision)==RISC_APP_DATA_OK && !strcmp(bytes,"edit"));
 assert(!v->file_open_write(v->context,"/saved/state.bin"));
 export_test_keep(role,*ex);
#ifdef RESIDENT
 risc_resident_client_v1 resident={.struct_size=sizeof(resident)};assert(rt->resident_shell(&resident));
#ifdef CHILD
 export_test_probe(0,0);export_test_probe(1,1);
 risc_resident_request_v1 req={sizeof(req),RISC_RESIDENT_CHECKPOINT_CONTROLS,0,0};risc_resident_reply_v1 reply={sizeof(reply),0};
 assert(resident.checkpoint(resident.invocation,&req,&reply)==RISC_RESIDENT_OK);
 export_test_probe(0,0);export_test_probe(1,1);
 if(mode==3){export_test_fault();assert(ex->stat_revision(v->context,"/saved/state.bin",&size,&revision)==RISC_APP_DATA_RETAINED);return;}
#else
 const risc_resident_callbacks_v1 callbacks={1,sizeof(callbacks),0,dispatch,0,0};assert(resident.register_shell(resident.invocation,&callbacks)==RISC_RESIDENT_OK);
 risc_resident_result_v1 result={.struct_size=sizeof(result)};
 assert(resident.run_foreground(resident.invocation,"child.elf",&result)==(mode==3?RISC_RESIDENT_RETAINED:RISC_RESIDENT_OK));
 export_test_probe(1,0);export_test_probe(0,mode!=3);
 if(mode==3){assert(!rt->release(&grant));return;}
#endif
#else
 if(mode==1){export_test_fault();assert(ex->stat_revision(v->context,"/saved/state.bin",&size,&revision)==RISC_APP_DATA_RETAINED);export_test_reset_safe(0);assert(!rt->release(&grant));rt->yield_ms(1);export_test_probe(role,0);return;}
 export_test_update();
#endif
 const risc_app_data_export_v1 old=*ex;
 assert(rt->release(&grant));assert(old.stat_revision(old.volume.terminal.power.volume.base.context,"/saved/state.bin",&size,&revision)==RISC_APP_DATA_CONTEXT);
 export_test_broker_alloc_fail(1);
 assert(!rt->acquire(RISC_APP_DATA_EXPORT_CAPABILITY,1,0,&grant));
 export_test_broker_alloc_fail(0);
 assert(rt->acquire(RISC_APP_DATA_EXPORT_CAPABILITY,1,0,&grant));ex=grant.api;v=&ex->volume.terminal.power.volume.base;
 assert(v->context!=old.volume.terminal.power.volume.base.context);
 assert(old.stat_revision(old.volume.terminal.power.volume.base.context,"/saved/state.bin",&size,&revision)==RISC_APP_DATA_CONTEXT);
 export_test_keep(role,*ex);
 // Runtime revocation closes these invocation-owned memory snapshots and dirs.
 uint64_t snapshotSize=0;assert(v->file_open_read(v->context,"/saved/state.bin",&snapshotSize) && snapshotSize==4);
 assert(v->dir_open(v->context,"/saved"));
#ifndef RESIDENT
#ifndef CHILD
 export_test_next();assert(rt->request_launch("child.elf"));
#endif
#endif
}
__attribute__((visibility("default"))) int app_module_init(void){return 0;}
__attribute__((visibility("default"))) void app_module_fini(void){
#ifdef CHILD
 export_test_finished(1);
#else
 export_test_finished(0);
#endif
}
