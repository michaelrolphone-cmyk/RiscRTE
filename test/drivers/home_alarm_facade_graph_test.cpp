#include "runtime/drivers/ProviderGraphV2.h"
#include "AlarmServiceFacadeV2.h"
#include "AlarmClockChangeV1.h"
#include "PointsServiceProjection.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace RuntimeProviders;
static unsigned starts,stops,callbacks,quiesces;static bool fault;
extern "C" void home_alarm_lifecycle(unsigned event){if(event==1)++starts;else if(event==2)++stops;else if(event==3)++callbacks;else if(event==4)++quiesces;else assert(false);}
extern "C" int home_alarm_fault(void){return fault;}
int main(int argc,char**argv){
 assert(argc==6);const char*mode=argv[1];unsigned version=unsigned(std::strtoul(argv[2],nullptr,10));
 const bool missing=!std::strcmp(mode,"missing"),wrong=!std::strcmp(mode,"wrong-api"),duplicate=!std::strcmp(mode,"duplicate"),malformed=!std::strcmp(mode,"malformed"),retained=!std::strcmp(mode,"retained");
 const RequirementV2 needs[]={{ALARM_SERVICE_CAPABILITY,wrong?3u:version}};
 GraphV2 graph;
 if(!missing)assert(graph.addVerified({"alarm-owner",argv[3],ALARM_SERVICE_CAPABILITY,version,nullptr,0}));
 if(duplicate)assert(graph.addVerified({"alarm-alternate",argv[5],ALARM_SERVICE_CAPABILITY,version,nullptr,0}));
 assert(graph.addVerified({"alarm-facade",argv[4],ALARM_SERVICE_FACADE_CAPABILITY,2,needs,1}));
 assert(!graph.acquire(ALARM_SERVICE_FACADE_CAPABILITY,1).slot);
 if(missing||wrong||duplicate){
  assert(!graph.acquire(ALARM_SERVICE_FACADE_CAPABILITY,2).slot);
  assert(starts==0&&callbacks==0&&stops==0&&graph.shutdown());
  puts("Actual facade graph rejects missing/wrong/ambiguous owner without dispatch PASS");return 0;
 }
 auto oldGrant=graph.acquire(ALARM_SERVICE_CAPABILITY,version);assert(oldGrant.slot);
 const auto*old=static_cast<const alarm_service_v1*>(graph.interfaceFor(oldGrant));assert(old&&starts==1&&!stops);
 auto newGrant=graph.acquire(ALARM_SERVICE_FACADE_CAPABILITY,2);
 if(malformed){
  assert(!newGrant.slot&&graph.interfaceFor(oldGrant)==old&&!stops);
  assert(old->step(old->context)==ALARM_OK);
  assert(graph.release(oldGrant)&&graph.shutdown()&&stops==1);
  puts("Malformed owner descriptor refuses facade without invalidating installed consumer PASS");return 0;
 }
 assert(newGrant.slot&&starts==1&&!stops);
 const auto*f=static_cast<const alarm_service_v1*>(graph.interfaceFor(newGrant));
 const auto*d=alarm_service_descriptor(f);assert(d&&f->context==old->context);
 assert(f->status==old->status&&f->step==old->step&&f->refresh==old->refresh&&f->acknowledge==old->acknowledge&&f->prepare_sleep==old->prepare_sleep&&f->stop_only==old->stop_only);
 assert(d->output_modes==(version==1?ALARM_MODE_BOTH:ALARM_MODE_VIBRATE));
 assert(old->step(old->context)==ALARM_OK&&f->step(f->context)==ALARM_OK);
 alarm_status_v1 copied={};assert(f->status(f->context,&copied)==ALARM_OK&&copied.snapshot==2);
 if(version==1){const auto*c=alarm_clock_change_control(old);assert(c&&d->resume_sleep==c->sleep.resume_sleep);assert(c->clock_changed(old->context,1234)==ALARM_OK);assert(f->status(f->context,&copied)==ALARM_OK&&copied.snapshot==102);}
 else{const auto*o=alarm_service_descriptor(old);assert(o&&d->resume_sleep==o->resume_sleep);points_catalog_projection p={};p.struct_size=sizeof(p);assert(points_service_project(f,&p)==ALARM_OK);}
 alarm_sleep_v1 ticket={};assert(f->prepare_sleep(f->context,&ticket)==ALARM_OK&&alarm_service_resume(f,&ticket)==ALARM_OK);
 assert(!graph.shutdown());
 if(retained){
  fault=true;assert(f->status(f->context,&copied)==ALARM_RETAINED);
  unsigned at=callbacks;
  assert(graph.release(newGrant)&&!graph.interfaceFor(newGrant)&&!stops);
  assert(!graph.release(oldGrant)&&!graph.interfaceFor(oldGrant)&&stops==0);
  assert(callbacks==at&&quiesces);
  assert(graph.holdsGrant(oldGrant)&&graph.liveGrants()==1&&!graph.shutdown()&&callbacks==at);
  assert(graph.release(oldGrant)&&!graph.holdsGrant(oldGrant)&&!graph.liveGrants());
  assert(graph.shutdown()&&stops==1&&callbacks==at);
  puts("Retained original owner stays mapped after both consumer grants revoke; retry quiescence does not dispatch callbacks PASS");return 0;
 }
 assert(graph.release(oldGrant)&&!graph.interfaceFor(oldGrant)&&!stops);
 assert(f->step(f->context)==ALARM_OK);assert(starts==1);
 auto replacement=graph.acquire(ALARM_SERVICE_CAPABILITY,version);assert(replacement.slot&&replacement.generation!=oldGrant.generation&&graph.interfaceFor(replacement)==old);
 assert(graph.release(newGrant)&&!graph.interfaceFor(newGrant)&&!stops);
 assert(old->step(old->context)==ALARM_OK);
 assert(graph.release(replacement)&&graph.shutdown()&&starts==1&&stops==1);
 puts("Original alarm consumer and actual facade share one owner/callback/context; exact lifetime and stale grants PASS");
}
