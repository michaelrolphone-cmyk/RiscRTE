#include "runtime/drivers/ProviderGraphV2.h"
#include "native_registry_fixture.h"
#include "support/native_registry/backend.h"
#include <esp_dlfcn.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace RuntimeProviders;
namespace {
struct Lease { unsigned slot; bool live=false, failStart=false, failQuiesce=false; uint64_t token=0; } leases[3]={{0},{1},{2}};
struct Event { unsigned slot; std::string name; };
std::vector<Event> events;
uint64_t generation;
bool begin(void* p) { auto& l=*static_cast<Lease*>(p); assert(!l.live);l.live=true;l.token=++generation;return true; }
void revoke(void* p) { auto& l=*static_cast<Lease*>(p);assert(l.live);l.live=false;events.push_back({l.slot,"revoke"}); }
unsigned count(unsigned slot,const char* name) { unsigned n=0;for(const auto& e:events) if(e.slot==slot&&e.name==name)++n;return n; }
void reset() { assert(!risc_test_native_mapping_count());events.clear();for(auto& l:leases){assert(!l.live);l.failStart=l.failQuiesce=false;} }
const registry_fixture_api* api(GraphV2& g,GrantV2 grant) { auto* p=static_cast<const registry_fixture_api*>(g.interfaceFor(grant));assert(p&&p->marker==0x51ab);return p; }
struct Fixture {
    GraphV2 graph;
    risc_hardware_device_v1 hardware{};
    RequirementV2 hardwareReq{"hardware.device",1};
    RequirementV2 serviceReq{"test.hardware",1,"registry-hardware",11};
    Fixture(const char* hardwarePath,const char* servicePath,const char* otherPath) {
        hardware.api_version=1;hardware.struct_size=sizeof(hardware);hardware.instance_id=11;
        SpecV2 h{"registry-hardware",hardwarePath,"test.hardware",1,&hardwareReq,1};h.hardware=&hardware;h.lease={&leases[0],begin,revoke};
        SpecV2 s{"registry-service",servicePath,"test.service",1,&serviceReq,1};s.lease={&leases[1],begin,revoke};
        SpecV2 o{"registry-other",otherPath,"test.other",1,&serviceReq,1};o.lease={&leases[2],begin,revoke};
        assert(graph.addVerified(h));assert(graph.addVerified(s));assert(graph.addVerified(o));
        assert(!graph.addVerified(s)); // Software singleton admission stays graph-owned.
    }
};
}
extern "C" void registry_fixture_event(unsigned slot,const char* event) { assert(slot<3);events.push_back({slot,event}); }
extern "C" int registry_fixture_control(unsigned slot,unsigned control) { assert(slot<3);return control?leases[slot].failQuiesce:leases[slot].failStart; }
extern "C" uint64_t registry_fixture_token(unsigned slot) { return leases[slot].token; }
extern "C" bool registry_fixture_authorized(unsigned slot,uint64_t token) { return token&&leases[slot].live&&leases[slot].token==token; }
int main(int argc,char** argv) {
    assert(argc==5);
    // This is the actual pre-fix call sequence. The production registry must
    // still reject the ordinary duplicate without even entering relocation.
    void* h=esp_dlopen_instance(argv[1]);assert(h);
    size_t before=risc_test_native_relocation_count();
    assert(!dlopen(argv[2],RTLD_NOW));assert(dlerror());
    assert(risc_test_native_relocation_count()==before);
    void* s=esp_dlopen_instance(argv[2]);assert(s&&s!=h);
    assert(dlsym(h,"t5_driver_get")!=dlsym(s,"t5_driver_get"));
    assert(!dlclose(s));assert(!dlclose(h));reset();
    std::puts("PASS: production registry reproduces hardware/software driver.elf collision; ordinary duplicate still rejects");
    {
        Fixture f(argv[1],argv[2],argv[3]);auto& g=f.graph;
        auto hardware=g.acquireFrom("registry-hardware","test.hardware",1,11);assert(hardware.slot);
        auto service=g.acquire("test.service",1);assert(service.slot);
        auto repeated=g.acquire("test.service",1);assert(repeated.slot);
        auto other=g.acquire("test.other",1);assert(other.slot);
        assert(risc_test_native_mapping_count()==3);
        assert(api(g,service)==api(g,repeated));assert(api(g,service)!=api(g,other));
        assert(count(1,"start")==1&&count(2,"start")==1);
        assert(api(g,service)->next()==1&&api(g,repeated)->next()==2&&api(g,other)->next()==1);
        uint64_t oldToken=leases[1].token;
        assert(g.release(service));assert(count(1,"stop")==0&&risc_test_native_mapping_count()==3);
        assert(g.release(repeated));assert(count(1,"stop")==1&&count(1,"fini")==1);
        assert(risc_test_native_mapping_count()==2&&api(g,other)->next()==2);
        auto reloaded=g.acquire("test.service",1);assert(reloaded.slot&&api(g,reloaded)->next()==1);
        assert(leases[1].token!=oldToken&&!registry_fixture_authorized(1,oldToken));
        assert(g.release(hardware));assert(risc_test_native_mapping_count()==3); // Dependent pins retain hardware.
        assert(g.release(other));assert(risc_test_native_mapping_count()==2);
        assert(g.release(reloaded));assert(!risc_test_native_mapping_count());assert(g.shutdown());
    }
    reset();
    {
        Fixture f(argv[1],argv[2],argv[3]);auto& g=f.graph;
        auto hardware=g.acquire("test.hardware",1);assert(hardware.slot);
        risc_test_native_fail_relocations(1);
        assert(!g.acquire("test.service",1).slot);
        assert(risc_test_native_mapping_count()==1&&count(1,"start")==0);
        auto retry=g.acquire("test.service",1);assert(retry.slot);
        assert(g.release(retry));assert(g.release(hardware));assert(g.shutdown());
    }
    reset();
    {
        Fixture f(argv[1],argv[2],argv[3]);auto& g=f.graph;
        leases[1].failStart=true;leases[1].failQuiesce=true;
        assert(!g.acquire("test.service",1).slot);
        uint64_t failedToken=leases[1].token;
        assert(risc_test_native_mapping_count()==2&&!leases[1].live&&leases[0].live);
        assert(count(1,"start")==1&&count(1,"stop")==0&&count(1,"fini")==0);
        assert(!g.acquire("test.service",1).slot); // Retained failure cannot reload.
        assert(!g.recoverFailedFrom("registry-service","test.service",1));
        assert(risc_test_native_mapping_count()==2&&count(1,"fini")==0);
        leases[1].failQuiesce=false;
        assert(g.recoverFailedFrom("registry-service","test.service",1));
        assert(!risc_test_native_mapping_count()&&count(1,"stop")==1&&count(1,"fini")==1);
        leases[1].failStart=false;
        auto retry=g.acquire("test.service",1);assert(retry.slot&&api(g,retry)->next()==1);
        assert(!registry_fixture_authorized(1,failedToken));
        assert(g.release(retry));assert(g.shutdown());
    }
    reset();
    {
        void* hardware=esp_dlopen_instance(argv[1]);assert(hardware);
        before=risc_test_native_relocation_count();
        assert(!esp_dlopen_instance(argv[4]));assert(dlerror()); // Corrupt same-basename image.
        assert(risc_test_native_relocation_count()==before+1&&risc_test_native_mapping_count()==1);
        assert(dlsym(hardware,"t5_driver_get"));assert(!dlclose(hardware));
    }
    reset();
    std::puts("PASS: same-basename graph ownership, software singleton, refcounts, independent state, leases, failed load retry, retained cleanup and fini");
}
