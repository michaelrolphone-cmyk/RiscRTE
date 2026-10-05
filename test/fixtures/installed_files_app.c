#include <RiscRuntimeV1.h>
#include <RiscStorageVolumeV1.h>
#include <assert.h>
#include <string.h>
extern void volume_test_owner(int);
extern void volume_test_keep(risc_storage_volume_api_v1);
extern void volume_test_revoked(void);
extern int volume_test_phase(void);
extern void volume_test_next(void);
__attribute__((visibility("default"))) void app_main(void){
 const risc_runtime_api_v1*rt=risc_runtime_get_api(1);assert(rt);volume_test_revoked();risc_runtime_capability_v1 g={.struct_size=sizeof(g)};
#ifdef CHILD
 assert(!rt->acquire("storage.installed-files",1,0,&g));volume_test_next();return;
#else
 assert(!rt->acquire("storage.installed-files",2,0,&g));assert(!rt->acquire("storage.installed-files",1,1,&g));assert(!rt->acquire("storage.volume",1,0,&g));assert(rt->acquire("storage.installed-files",1,0,&g));
 const risc_storage_volume_api_v1*v=g.api;assert(v && v->api_version==1 && !v->file_open_write && !v->file_write && !v->remove && v->refresh(v->context));
 risc_runtime_capability_v1 other={.struct_size=sizeof(other)};assert(!rt->acquire("storage.installed-files",1,0,&other));
 uint64_t size=0;bool dir=false;assert(!v->stat(v->context,"/profile.json",&size,&dir));assert(!v->stat(v->context,"/boot.json",&size,&dir));
 risc_storage_dir_t d=v->dir_open(v->context,"/");assert(d);risc_storage_dirent_v1 e;unsigned found=0;while(v->dir_next(v->context,d,&e)){assert(!strcmp(e.name,"app.json") || !strcmp(e.name,"default.elf"));found++;}assert(found==2);v->dir_close(v->context,d);
 risc_storage_file_t f=v->file_open_read(v->context,"/app.json",&size);assert(f && size);char bytes[512];assert(v->file_read(v->context,f,bytes,sizeof(bytes))==size && bytes[0]=='{');assert(v->file_close(v->context,f,true));
 volume_test_owner(0);assert(!v->refresh(v->context) && !v->dir_open(v->context,"/"));volume_test_owner(1);
 risc_storage_volume_api_v1 old=*v;assert(rt->release(&g));assert(!old.refresh(old.context));assert(rt->acquire("storage.installed-files",1,0,&g));v=g.api;assert(v->context!=old.context && !old.refresh(old.context));volume_test_keep(*v);
 if(!volume_test_phase()){volume_test_next();assert(rt->request_launch("child.elf"));}
 // Outstanding logical handles are revoked before child/default reload.
 assert(v->file_open_read(v->context,"/app.json",&size));
#endif
}
