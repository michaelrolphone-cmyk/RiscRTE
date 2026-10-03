#include "runtime/drivers/ProviderGraphV2.h"
#include "ports/esp32s3/CooperativeDelay.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace RuntimeProviders;
struct Event {unsigned slot;uint32_t budget;};
static std::vector<Event> events;
static uint32_t now=0,cost=0;
static unsigned waits=0,stops[6]{};
static bool startOk[6]={true,true,true,true,true,true};
static bool quiesceOk[6]={true,true,true,true,true,true};
static bool nested=false,overrun=false;
static GraphV2* running=nullptr;
static uint32_t clockMs(){return now;}
static void wait(){++waits;}
static void work(unsigned slot,uint32_t ms){
  events.push_back({slot,ms});
  if(nested)running->poll(clockMs,wait);
  now += overrun ? 12 : (cost<ms?cost:ms);
}
struct Control {void(*poll)(unsigned,uint32_t);bool(*start)(unsigned);bool(*quiesce)(unsigned);void(*stop)(unsigned);};
static const Control control={work,[](unsigned i){return startOk[i];},[](unsigned i){return quiesceOk[i];},[](unsigned i){++stops[i];}};
int main(int argc,char**argv){
  assert(argc==7);
  assert(RiscCpu::cooperativeDelayTicks(0,1000)==1);
  assert(RiscCpu::cooperativeDelayTicks(1,1000)==1);
  assert(RiscCpu::cooperativeDelayTicks(20,1000)==20);
  assert(RiscCpu::cooperativeDelayTicks(50,1000)==50);
  assert(RiscCpu::cooperativeDelayTicks(1,100)==1);
  assert(RiscCpu::cooperativeDelayTicks(11,100)==2);
  assert(RiscCpu::cooperativeDelayTicks(20,100)==2);
  assert(RiscCpu::cooperativeDelayTicks(50,100)==5);
  GraphV2 graph;running=&graph;
  graph.poll(clockMs,wait);assert(waits==0);
  const RequirementV2 req={"test.poll-control",1,nullptr,0,&control};
  char ids[6][16];GrantV2 grants[6];
  for(unsigned i=0;i<6;++i){snprintf(ids[i],sizeof(ids[i]),"poll-%u",i);assert(graph.addVerified({ids[i],argv[i+1],"test.poll",1,&req,1}));}
  graph.poll(clockMs,wait);assert(events.empty() && waits==0); // Admitted, idle.
  for(unsigned i=0;i<6;++i){grants[i]=graph.acquireFrom(ids[i],"test.poll",1);assert(grants[i].slot);}
  // Expensive cooperative providers consume exactly 8 + 2 ms, never 8 + 8.
  cost=8;graph.poll(clockMs,wait);
  assert(events.size()==2 && events[0].slot==0 && events[0].budget==8 && events[1].slot==1 && events[1].budget==2 && now==10 && waits==1);
  events.clear();graph.poll(clockMs,wait);
  assert(events.size()==2 && events[0].slot==2 && events[1].slot==3 && now==20);
  events.clear();graph.poll(clockMs,wait);
  assert(events.size()==2 && events[0].slot==4 && events[1].slot==5 && now==30); // No starvation.
  // Cheap work keeps the call cap; nested polling dispatches nothing.
  events.clear();cost=0;nested=true;waits=0;graph.poll(clockMs,wait);
  assert(events.size()==4 && events.front().slot==0 && events.back().slot==3 && waits==1);
  nested=false;events.clear();graph.poll(clockMs,nullptr);
  assert(events.size()==4 && events.front().slot==4 && waits==1); // Caller owns wait.
  // Broken cooperative callback cannot be preempted, but starts no later work.
  events.clear();overrun=true;graph.poll(clockMs,wait);
  assert(events.size()==1 && events.front().slot==2);overrun=false;
  // Unsigned elapsed arithmetic preserves deadline across millis rollover.
  events.clear();now=UINT32_MAX-3;cost=8;graph.poll(clockMs,wait);
  assert(events.size()==2 && events[0].slot==3 && events[1].slot==4 && now==6);
  // Revoked failed-quiescence modules never poll; pointer and grant retained.
  quiesceOk[5]=false;assert(!graph.release(grants[5]));assert(!graph.interfaceFor(grants[5]));
  events.clear();cost=0;graph.poll(clockMs,wait);
  for(const auto& e:events)assert(e.slot!=5);
  assert(stops[5]==0 && !graph.acquireFrom(ids[5],"test.poll",1).slot);
  quiesceOk[5]=true;assert(graph.release(grants[5]) && stops[5]==1);
  grants[5]=graph.acquireFrom(ids[5],"test.poll",1);assert(grants[5].slot);
  for(auto g:grants)assert(graph.release(g));
  assert(graph.shutdown());events.clear();graph.poll(clockMs,wait);assert(events.empty());
  // Partial failed start also retains until successful targeted cleanup.
  startOk[0]=false;quiesceOk[0]=false;
  assert(!graph.acquireFrom(ids[0],"test.poll",1).slot);
  events.clear();graph.poll(clockMs,wait);assert(events.empty());
  assert(!graph.recoverFailedFrom(ids[0],"test.poll",1));
  quiesceOk[0]=true;assert(graph.recoverFailedFrom(ids[0],"test.poll",1));
  startOk[0]=true;auto retry=graph.acquireFrom(ids[0],"test.poll",1);assert(retry.slot);
  assert(graph.release(retry) && graph.shutdown());
  puts("Actual graph scheduling: 8/10ms budgets, fairness, one/optional wait, idle/reentry/overrun/wrap, revocation and failed-start retry PASS");
}
