#include <RiscRuntimeV1.h>
#include <RiscAppDataV1.h>
#include <assert.h>
#include <string.h>
extern void shared_test_clock(const risc_runtime_api_v1*);
extern void shared_test_keep(risc_app_data_v1);
extern void shared_test_step(void);
extern unsigned shared_test_steps(void);
__attribute__((visibility("default"))) void app_main(void){
 if(shared_test_steps()==2)return;
 const risc_runtime_api_v1*rt=risc_runtime_get_api(1);assert(rt);shared_test_clock(rt);
 risc_runtime_capability_v1 g={.struct_size=sizeof(g)},other={.struct_size=sizeof(other)};
#ifdef SHARED_CHILD
 const unsigned ns=64;
#else
 const unsigned ns=63;
#endif
 assert(!rt->acquire(RISC_SHARED_DATA_CAPABILITY,1,6,&g));
 assert(rt->acquire(RISC_SHARED_DATA_CAPABILITY,1,3,&g));const risc_app_data_v1*f=g.api;
 uint32_t n=99;uint64_t v=99;char bytes[8]={0};
 assert(f->stat(f->context,"other.cfp",&n,&v)==RISC_APP_DATA_CONTEXT && !n && !v);
 assert(f->read(f->context,"../data",0,bytes,sizeof(bytes),&n,&v)==RISC_APP_DATA_CONTEXT);
 assert(f->replace(f->context,"other.cfp",0,"bad",3)==RISC_APP_DATA_CONTEXT);
 assert(!rt->acquire(RISC_APP_DATA_CAPABILITY,1,ns,&other));
 assert(other.struct_size==sizeof(other)&&!other.api&&!other.slot&&!other.generation);
#ifdef SHARED_CHILD
 assert(f->stat(f->context,"context-fingerprints.cfp",&n,&v)==RISC_APP_DATA_OK && n==4 && v);
 assert(f->read(f->context,"context-fingerprints.cfp",v,bytes,sizeof(bytes),&n,&v)==RISC_APP_DATA_OK && n==4 && !memcmp(bytes,"test",4));
#else
 assert(f->replace(f->context,"context-fingerprints.cfp",0,"test",4)==RISC_APP_DATA_OK);
 assert(f->replace(f->context,"context-fingerprints.cfp",0,"lost",4)==RISC_APP_DATA_STALE);
#endif
 risc_app_data_v1 stale=*f;shared_test_keep(stale);assert(rt->release(&g));
 assert(stale.stat(stale.context,"context-fingerprints.cfp",&n,&v)==RISC_APP_DATA_CONTEXT);
 assert(rt->acquire(RISC_APP_DATA_CAPABILITY,1,ns,&other));f=other.api;
 assert(f->stat(f->context,"context-fingerprints.cfp",&n,&v)==RISC_APP_DATA_NOT_FOUND);
 assert(rt->release(&other));
 /* Spectrum's operation-scoped private table must leave the native slot free
  * for the next shared fingerprint operation, while old contexts stay dead. */
 for(unsigned i=0;i<3;i++){
  g=(risc_runtime_capability_v1){.struct_size=sizeof(g)};
  assert(rt->acquire(RISC_SHARED_DATA_CAPABILITY,1,3,&g));f=g.api;
  assert(f->stat(f->context,"context-fingerprints.cfp",&n,&v)==RISC_APP_DATA_OK&&n==4);
  assert(stale.stat(stale.context,"context-fingerprints.cfp",&n,&v)==RISC_APP_DATA_CONTEXT);
  assert(rt->release(&g));
  other=(risc_runtime_capability_v1){.struct_size=sizeof(other)};
  assert(rt->acquire(RISC_APP_DATA_CAPABILITY,1,ns,&other));f=other.api;
  assert(f->stat(f->context,"context-fingerprints.cfp",&n,&v)==RISC_APP_DATA_NOT_FOUND);
  assert(rt->release(&other));
 }
 shared_test_step();
#ifndef SHARED_CHILD
 assert(rt->request_launch("child.elf"));
#endif
}
