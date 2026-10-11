#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstdio>
using namespace RuntimeProviders;
static GraphV2* graph;
static unsigned calls,starts,stops,seen[2];
static bool safe=true;
static uint32_t clockMs(){return 0;}
static void service(unsigned i,uint32_t budget){
 assert(budget==1000);++calls;++seen[i];assert(graph->lifecycleBusy());
 const unsigned before=calls;graph->service(1000);graph->poll(clockMs,nullptr);
 assert(calls==before && !graph->acquire("test.service",1).slot && !graph->shutdown());
}
static bool start(unsigned){++starts;const unsigned before=calls;graph->service(1000);assert(calls==before);return true;}
static bool quiesce(unsigned){return safe;}
static void stop(unsigned){++stops;}
struct Control{void(*service)(unsigned,uint32_t);bool(*start)(unsigned);bool(*quiesce)(unsigned);void(*stop)(unsigned);};
int main(int argc,char**argv){
 assert(argc==5);GraphV2 g;graph=&g;const Control control={service,start,quiesce,stop};
 RequirementV2 req={"test.service-control",1,nullptr,0,&control};
 assert(g.addVerified({"service-0",argv[1],"test.service",1,&req,1}));
 assert(g.addVerified({"service-1",argv[2],"test.service",1,&req,1}));
 g.service(1000);assert(!calls);
 auto a=g.acquireFrom("service-0","test.service",1);auto b=g.acquireFrom("service-1","test.service",1);assert(a.slot&&b.slot&&starts==2);
 g.service(0);g.service(1001);assert(!calls);
 g.poll(clockMs,nullptr);assert(!calls); // 8 ms polling does not dispatch synchronous I/O.
 g.service(1000);assert(calls==1&&seen[0]==1);g.service(1000);assert(calls==2&&seen[1]==1);
 safe=false;assert(!g.release(a));g.service(1000);assert(calls==2); // Retained custody fences all dependent work.
 safe=true;assert(g.release(a)&&g.release(b)&&g.shutdown());g.service(1000);assert(calls==2&&stops==2);
 GraphV2 bad;graph=&bad;assert(bad.addVerified({"service-0",argv[3],"test.service",1,&req,1}));
 assert(!bad.acquireFrom("service-0","test.service",1).slot);assert(bad.shutdown());
 GraphV2 unrelated;graph=&unrelated;assert(unrelated.addVerified({"service-0",argv[4],"test.service",1,&req,1}));
 auto c=unrelated.acquireFrom("service-0","test.service",1);assert(c.slot);unrelated.service(1000);assert(calls==2);assert(unrelated.release(c)&&unrelated.shutdown());
 puts("Provider service tagged/size/version admission, one callback, no poll crossover, lifecycle recursion and revocation fences PASS");
}
