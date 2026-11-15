#include "private/esp_cpu_task_scope_v1.h"
#include "private/esp_cpu_task_stack_v1.h"
#include "esp_freertos_hooks.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);std::fflush(stderr);std::_Exit(1);} } while(0)
using Scope=esp_cpu_task_scope_t;
using Token=esp_cpu_scoped_task_t;
using Publication=esp_cpu_scoped_task_publication_t;
using Report=esp_cpu_retirement_report_t;
constexpr auto OK=ESP_CPU_RETIREMENT_OK,PENDING=ESP_CPU_RETIREMENT_PENDING,INVALID=ESP_CPU_RETIREMENT_INVALID;
constexpr auto CONTEXT=ESP_CPU_RETIREMENT_CONTEXT,STALE=ESP_CPU_RETIREMENT_STALE,EXHAUSTED=ESP_CPU_RETIREMENT_EXHAUSTED,RETAINED=ESP_CPU_RETIREMENT_RETAINED;
extern "C" {
int scope_fixture_pthread_key_create(unsigned long *,void(*)(void *));
int scope_fixture_pthread_key_delete(unsigned long);
int scope_fixture_pthread_setspecific(unsigned long,const void *);
void *scope_fixture_pthread_getspecific(unsigned long);
}
struct KernelExit {};
struct ExpectedPark {};
struct FakeTask {
    std::thread worker;
    std::atomic<bool> stop{false},done{false},reaped{false},reaping{false},deferred{false};
    void *tls=nullptr;
    TlsDeleteCallbackFunction_t destructor=nullptr;
    unsigned sets=0;
    int core=0;
};
static FakeTask root,foreign,idle0,idle1;
static thread_local FakeTask *current=&root;
static thread_local int current_core=0;
static thread_local bool isr=false;
static std::recursive_mutex critical;
static std::mutex tasks_mutex,events_mutex;
static std::vector<FakeTask*> tasks;
static std::vector<std::string> events;
static esp_freertos_idle_cb_t hooks[2]{};
static std::atomic<unsigned> creates{0},deletes{0},self_deletes{0},private_entries{0},private_destructors{0},newlib_calls{0},parks{0};
static std::atomic<int> alloc_count{0},fail_alloc{-1};
static std::atomic<bool> release_entry{false},entry_ready{false},tls_gate{false},newlib_gate{false},tls_reached{false},newlib_reached{false};
static bool fail_create=false,defer_delete=false,ignore_proxy=false,mutate_input=false,hold_create=false,wrong_identity=false;
static bool expect_park=false;
static std::atomic<bool> hold_before_private{false},child_gate_waiting{false};
static std::atomic<bool> hold_nested_create{false},nested_create_entered{false},release_nested_create{false};
static std::atomic<unsigned> root_delays{0};
static unsigned park_after=30;
static Scope access_other{};
static std::atomic<bool> access_ready{false},access_delete_gate{false},access_after_delete{false};
static bool access_race=false;
static uint32_t expected_stack_bytes=4096;
static esp_cpu_task_stack_layout_v1_t current_layout{0x1000,0x1011,0x1020,0x1033,48,160,16};
static bool layout_query_ok=true;
extern "C" bool esp_cpu_task_stack_layout_current_v1(esp_cpu_task_stack_layout_v1_t *out){if(!layout_query_ok)return false;CHECK(out);*out=current_layout;return true;}
static FakeTask *reuse_task=nullptr;
static Publication publication{};
static Scope scope{};
static char input_name[16]="copied-task";
static esp_cpu_scoped_task_config_t *input_config=nullptr;
static void event(const char *s){std::lock_guard<std::mutex> g(events_mutex);events.emplace_back(s);}
template<class F> static void wait_until(F f){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!f()){CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
struct Context {FakeTask *prior;int core;Context(FakeTask *p,int n=0):prior(current),core(current_core){current=p;current_core=n;}~Context(){current=prior;current_core=core;}};
extern "C" void scope_test_enter(portMUX_TYPE*){critical.lock();}
extern "C" void scope_test_exit(portMUX_TYPE*){critical.unlock();}
extern "C" BaseType_t xPortInIsrContext(void){return isr;}
extern "C" BaseType_t xPortGetCoreID(void){return current_core;}
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(void){return current;}
extern "C" TaskHandle_t xTaskGetIdleTaskHandleForCore(BaseType_t n){return n==0?&idle0:n==1?&idle1:nullptr;}
extern "C" esp_err_t esp_register_freertos_idle_hook_for_cpu(esp_freertos_idle_cb_t cb,UBaseType_t n){CHECK(n<2&&!hooks[n]);hooks[n]=cb;return ESP_OK;}
extern "C" void *scope_test_malloc(size_t n){int index=alloc_count.fetch_add(1);return index==fail_alloc.load()?nullptr:std::malloc(n);}
extern "C" void *scope_test_calloc(size_t n,size_t s){int index=alloc_count.fetch_add(1);return index==fail_alloc.load()?nullptr:std::calloc(n,s);}
extern "C" void scope_test_free(void *p){std::free(p);}
extern "C" void *pvTaskGetThreadLocalStoragePointer(TaskHandle_t h,BaseType_t n){CHECK(n==0);FakeTask *t=h?static_cast<FakeTask*>(h):current;std::lock_guard<std::recursive_mutex> g(critical);CHECK(t&&!t->reaped);return t->tls;}
extern "C" void vTaskSetThreadLocalStoragePointerAndDelCallback(TaskHandle_t h,BaseType_t n,void *data,TlsDeleteCallbackFunction_t cb){
    CHECK(n==0);FakeTask *t=h?static_cast<FakeTask*>(h):current;std::lock_guard<std::recursive_mutex> g(critical);CHECK(t&&!t->reaped);
    ++t->sets;
    if(ignore_proxy&&t->sets==2)return; // deliberate lost callback write: must never lead to false ACK
    t->tls=data;t->destructor=cb;
}
extern "C" void vTaskSetThreadLocalStoragePointer(TaskHandle_t h,BaseType_t n,void *data){vTaskSetThreadLocalStoragePointerAndDelCallback(h,n,data,nullptr);}
static Report inspect(Token t,esp_cpu_retirement_result_t expected=PENDING){Report r{};CHECK(esp_cpu_task_scope_poll(t,&r)==expected);return r;}
static void cleanup(FakeTask *t,bool idle){
    CHECK(t->done&&!t->reaped);if(t->worker.joinable())t->worker.join();
    event("kernel-TLS-enter");
    TlsDeleteCallbackFunction_t cb;void *data;
    {std::lock_guard<std::recursive_mutex> g(critical);cb=t->destructor;data=t->tls;}
    if(cb)cb(0,data);
    event("kernel-TLS-return");
    newlib_reached=true;
    while(newlib_gate.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    event("newlib-private-FILE-callback");++newlib_calls;
    event("newlib-return");event("stack-free");event("TCB-free");t->reaped=true;
    if(idle){event("idle-hook");CHECK(hooks[current_core]());}
}
static void idle_pump(){
    std::vector<FakeTask*> snapshot;{std::lock_guard<std::mutex> g(tasks_mutex);snapshot=tasks;}
    for(auto *t:snapshot)if(t->done&&t->deferred&&!t->reaped&&!t->reaping.exchange(true)){
        Context ctx(&idle1,1);cleanup(t,true);
    }
}
extern "C" void vTaskDelay(TickType_t ticks){
    CHECK(ticks==1);
    if(current!=&root&&current!=&foreign&&current!=&idle0&&current!=&idle1&&current->stop)throw KernelExit{};
    if(current!=&root&&current!=&foreign&&current!=&idle0&&current!=&idle1){
        if(hold_before_private)child_gate_waiting=true;
        while(hold_before_private.load()&&!current->stop)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(current!=&root&&current!=&foreign&&current!=&idle0&&current!=&idle1&&current->stop)throw KernelExit{};
    if(current==&root){++root_delays;idle_pump();if(expect_park&&++parks>=park_after)throw ExpectedPark{};}
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
extern "C" void v5_test_before_kernel_delete(TaskHandle_t);
extern "C" void vTaskDelete(TaskHandle_t h){
    v5_test_before_kernel_delete(h);
    ++deletes;
    if(h&&access_race){access_delete_gate=true;wait_until([]{return access_after_delete.load();});}
    if(!h){CHECK(current!=&root&&current!=&foreign&&current!=&idle0&&current!=&idle1);++self_deletes;current->deferred=true;current->stop=true;throw KernelExit{};}
    auto *t=static_cast<FakeTask*>(h);CHECK(t!=current&&!t->reaped);
    t->deferred=defer_delete;t->stop=true;
    wait_until([&]{return t->done.load();});
    if(!defer_delete){CHECK(!t->reaping.exchange(true));cleanup(t,false);}
    event("vTaskDelete-return");
}
extern "C" BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry,const char *name,uint32_t depth,void *arg,UBaseType_t priority,TaskHandle_t *out,BaseType_t cpu){
    ++creates;CHECK(entry&&out&&name&&depth==expected_stack_bytes&&priority==3&&cpu==-1);
    CHECK(std::strcmp(name,"net-owner-v5")==0);
    if(hold_nested_create&&current!=&root){nested_create_entered=true;while(!release_nested_create)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    if(fail_create)return 0;
    FakeTask *t=reuse_task?reuse_task:new FakeTask;
    if(reuse_task){CHECK(t->reaped&&!t->worker.joinable());t->stop=false;t->done=false;t->reaped=false;t->reaping=false;t->deferred=false;t->tls=nullptr;t->destructor=nullptr;t->sets=0;reuse_task=nullptr;}
    else {std::lock_guard<std::mutex> g(tasks_mutex);tasks.push_back(t);}
    t->core=1;*out=wrong_identity?&foreign:t;
    if(mutate_input){CHECK(input_config);std::strcpy(input_name,"changed");input_config->entry=nullptr;input_config->argument=&foreign;input_config->priority=99;}
    t->worker=std::thread([=]{Context context(t,1);try{entry(arg);CHECK(false);}catch(const KernelExit&){t->done=true;}});
    if(hold_create){wait_until([&]{std::lock_guard<std::recursive_mutex> g(critical);return t->sets>=2;});if(hold_before_private)wait_until([]{return child_gate_waiting.load();});CHECK(private_entries==0);CHECK(publication.ticket.serial==0);}
    return pdPASS;
}
static Scope open(unsigned budget=8){Scope result{};CHECK(esp_cpu_task_scope_open(budget,&result)==OK);return result;}
static void private_destructor(void *value){
    CHECK(value==reinterpret_cast<void*>(1)||value==reinterpret_cast<void*>(2));
    ++private_destructors;event(value==reinterpret_cast<void*>(1)?"private-destructor-1":"private-destructor-2");
    tls_reached=true;while(tls_gate.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
static unsigned long keys[2]{};
static unsigned long escape_key{},escaped_key{};
static std::atomic<unsigned> escaped_calls{0},escape_calls{0};
static void escaped_destructor(void*){++escaped_calls;}
static void escape_destructor(void*){
    CHECK(current==&root); // direct cleanup executes on the deleting task
    ++escape_calls;
    CHECK(scope_fixture_pthread_setspecific(escaped_key,reinterpret_cast<void*>(3))==0);
}
static void entry_escape(void*){
    CHECK(publication.ticket.serial&&publication.kernel_identity==current);
    ++private_entries;
    CHECK(scope_fixture_pthread_key_create(&escaped_key,escaped_destructor)==0);
    CHECK(scope_fixture_pthread_key_create(&escape_key,escape_destructor)==0);
    CHECK(scope_fixture_pthread_setspecific(escape_key,reinterpret_cast<void*>(4))==0);
    entry_ready=true;
    for(;;)vTaskDelay(1);
}
static void entry_wait(void *arg){
    CHECK(arg==&publication);CHECK(publication.ticket.serial&&publication.kernel_identity==current);++private_entries;entry_ready=true;
    while(!release_entry.load())vTaskDelay(1);
}
static void entry_tls(void *arg){
    CHECK(arg==&publication);CHECK(publication.ticket.serial&&publication.kernel_identity==current);++private_entries;
    CHECK(scope_fixture_pthread_key_create(&keys[0],private_destructor)==0);
    CHECK(scope_fixture_pthread_key_create(&keys[1],private_destructor)==0);
    CHECK(scope_fixture_pthread_setspecific(keys[0],reinterpret_cast<void*>(1))==0);
    CHECK(scope_fixture_pthread_setspecific(keys[1],reinterpret_cast<void*>(2))==0);
    CHECK(scope_fixture_pthread_getspecific(keys[0])==reinterpret_cast<void*>(1));entry_ready=true;
    while(!release_entry.load())vTaskDelay(1);
}
static void entry_self(void *arg){CHECK(arg==&publication);CHECK(publication.ticket.serial);++private_entries;esp_cpu_task_scope_exit_current(scope);CHECK(false);}
static void entry_self_join(void *arg){CHECK(arg==&publication);CHECK(publication.ticket.serial);++private_entries;(void)esp_cpu_task_scope_delete_join(publication.ticket);CHECK(false);}
struct DeleterArgument {Publication self{};Token target{};std::atomic<bool> *launch;std::atomic<unsigned> *returned;};
static void entry_deleter(void *arg){auto *d=static_cast<DeleterArgument*>(arg);CHECK(d->self.ticket.serial&&d->self.kernel_identity==current);while(!d->launch->load())vTaskDelay(1);CHECK(esp_cpu_task_scope_delete_join(d->target)==OK);++*d->returned;}
static std::atomic<bool> nested_scope_refused{false},nested_create_returned{false},fresh_create_refused{false};
static Publication grandchild{};
static void entry_nested_scope(void*){Scope result{999};CHECK(esp_cpu_task_scope_open(1,&result)==CONTEXT);CHECK(result.id==999);nested_scope_refused=true;}
static void entry_grandchild(void*){CHECK(grandchild.ticket.serial&&grandchild.kernel_identity==current);for(;;)vTaskDelay(1);}
static void entry_nested_create(void*){
    esp_cpu_scoped_task_config_t c{entry_grandchild,nullptr,input_name,4096,1024,3,1};
    CHECK(esp_cpu_task_scope_create(scope,&c,&grandchild)==OK);nested_create_returned=true;
    Publication refused{};CHECK(esp_cpu_task_scope_create(scope,&c,&refused)==CONTEXT);fresh_create_refused=true;
}
static esp_cpu_scoped_task_config_t config(esp_cpu_scoped_entry_t entry=entry_wait){return {entry,&publication,input_name,4096,1024,3,1};}
static void create(esp_cpu_scoped_entry_t entry=entry_wait){auto c=config(entry);CHECK(esp_cpu_task_scope_create(scope,&c,&publication)==OK);}
static void await_ack(){for(unsigned n=0;n<3000;++n){Report r{};auto rc=esp_cpu_task_scope_poll(publication.ticket,&r);if(rc==OK)return;CHECK(rc==PENDING);vTaskDelay(1);}CHECK(false);}
static void finish(){idle_pump();std::lock_guard<std::mutex> g(tasks_mutex);for(auto *t:tasks)CHECK(t->done&&t->reaped&&!t->worker.joinable());}
#include "stack_floor_cases.h"

static esp_cpu_task_scope_current_v1_t access_sentinel(){return {&foreign,{777,888},true};}
static void unchanged_current(const esp_cpu_task_scope_current_v1_t &v){CHECK(v.identity==&foreign&&v.ticket.scope==777&&v.ticket.serial==888&&v.creator);}
static void access_refusal(Scope s,esp_cpu_retirement_result_t expected){
    auto v=access_sentinel();Token t{777,888};
    CHECK(esp_cpu_task_scope_current_v1(s,&v)==expected);unchanged_current(v);
    CHECK(esp_cpu_task_scope_resolve_v1(s,&foreign,&t)==expected);CHECK(t.scope==777&&t.serial==888);
}
static void entry_access(void*){
    esp_cpu_task_scope_current_v1_t v{};Token t{};
    CHECK(esp_cpu_task_scope_current_v1(scope,&v)==OK);
    CHECK(!v.creator&&v.identity==current&&v.ticket.scope==publication.ticket.scope&&v.ticket.serial==publication.ticket.serial);
    CHECK(esp_cpu_task_scope_resolve_v1(scope,current,&t)==OK);
    CHECK(t.scope==v.ticket.scope&&t.serial==v.ticket.serial);
    access_refusal(access_other,CONTEXT);access_ready=true;
    if(access_race){
        while(!access_delete_gate.load())std::this_thread::yield();
        access_refusal(scope,CONTEXT);access_after_delete=true;
    }
    for(;;)vTaskDelay(1);
}
static void run_access(const std::string &name){
    if(name=="access-invalid"){
        auto v=access_sentinel();Token t{777,888};scope=open();
        CHECK(esp_cpu_task_scope_current_v1({0},&v)==INVALID);unchanged_current(v);
        CHECK(esp_cpu_task_scope_current_v1(scope,nullptr)==INVALID);
        CHECK(esp_cpu_task_scope_resolve_v1({0},&root,&t)==INVALID);
        CHECK(esp_cpu_task_scope_resolve_v1(scope,nullptr,&t)==INVALID);
        CHECK(esp_cpu_task_scope_resolve_v1(scope,&root,nullptr)==INVALID);
        CHECK(t.scope==777&&t.serial==888);access_refusal({UINT64_MAX},STALE);
        CHECK(esp_cpu_task_scope_close(scope)==OK);return;
    }
    scope=open();
    if(name=="access-creator"){
        esp_cpu_task_scope_current_v1_t v{};CHECK(esp_cpu_task_scope_current_v1(scope,&v)==OK);
        CHECK(v.creator&&v.identity==&root&&v.ticket.scope==0&&v.ticket.serial==0);
        Token t{777,888};CHECK(esp_cpu_task_scope_resolve_v1(scope,&root,&t)==STALE);
        CHECK(t.scope==777&&t.serial==888);CHECK(esp_cpu_task_scope_resolve_v1(scope,&foreign,&t)==STALE);
        CHECK(t.scope==777&&t.serial==888);
    }else if(name=="access-foreign"){
        Context c(&foreign);access_refusal(scope,CONTEXT);
    }else if(name=="access-isr"){
        isr=true;access_refusal(scope,CONTEXT);isr=false;
    }else if(name=="access-closed"){
        CHECK(esp_cpu_task_scope_close(scope)==OK);access_refusal(scope,STALE);return;
    }else if(name=="access-child"||name=="access-delete-race"){
        access_other=open();access_race=name=="access-delete-race";create(entry_access);
        wait_until([]{return access_ready.load();});CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==OK);
        if(access_race)CHECK(access_after_delete);CHECK(esp_cpu_task_scope_close(access_other)==OK);finish();
    }else if(name=="access-tombstone"){
        create();wait_until([]{return entry_ready.load();});Token original=publication.ticket;
        auto identity=publication.kernel_identity;CHECK(esp_cpu_task_scope_delete_join(original)==OK);
        CHECK(static_cast<FakeTask*>(identity)->reaped);
        Token result{};CHECK(esp_cpu_task_scope_resolve_v1(scope,identity,&result)==OK);
        CHECK(result.scope==original.scope&&result.serial==original.serial);
        unsigned prior_deletes=deletes;CHECK(esp_cpu_task_scope_delete_join(result)==OK);CHECK(deletes==prior_deletes);
        // An unreported reuse of this raw address is not discoverable here.
        // Resolution stays an old token, never authority for a new task.
        CHECK(esp_cpu_task_scope_resolve_v1(scope,identity,&result)==OK);
        CHECK(result.scope==original.scope&&result.serial==original.serial);finish();
    }else if(name=="access-retained"||name=="access-known-reuse"){
        if(name=="access-known-reuse"){
            create();wait_until([]{return entry_ready.load();});
            CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==OK);
            reuse_task=static_cast<FakeTask*>(publication.kernel_identity);
        }else wrong_identity=true;
        auto c=config();Publication refused{};CHECK(esp_cpu_task_scope_create(scope,&c,&refused)==RETAINED);
        access_refusal(scope,RETAINED);
        Token t{777,888};CHECK(esp_cpu_task_scope_resolve_v1(scope,publication.kernel_identity?publication.kernel_identity:&foreign,&t)==RETAINED);
        CHECK(t.scope==777&&t.serial==888);
        wait_until([]{std::lock_guard<std::mutex> g(tasks_mutex);return tasks[0]->done.load();});idle_pump();finish();
        CHECK(esp_cpu_task_scope_close(scope)==RETAINED);return;
    }else CHECK(false);
    CHECK(esp_cpu_task_scope_close(scope)==OK);
}

static void run(const std::string &name){
    if(name.rfind("access-",0)==0){run_access(name);return;}
    if(name.rfind("floor-",0)==0){run_floor_case(name);return;}
    if(name=="invalid-config"){
        scope=open();auto c=config();Publication out{{77,88},&foreign};
        CHECK(esp_cpu_task_scope_create(scope,nullptr,&out)==INVALID);CHECK(esp_cpu_task_scope_create(scope,&c,nullptr)==INVALID);
        c.entry=nullptr;CHECK(esp_cpu_task_scope_create(scope,&c,&out)==INVALID);c=config();c.stack_bytes=0;CHECK(esp_cpu_task_scope_create(scope,&c,&out)==INVALID);
        c=config();c.priority=25;CHECK(esp_cpu_task_scope_create(scope,&c,&out)==INVALID);c=config();c.core=2;CHECK(esp_cpu_task_scope_create(scope,&c,&out)==INVALID);
        c=config();c.name="";CHECK(esp_cpu_task_scope_create(scope,&c,&out)==INVALID);c.name="this-name-is-too-long";CHECK(esp_cpu_task_scope_create(scope,&c,&out)==INVALID);
        CHECK(creates==0&&out.ticket.serial==88&&out.kernel_identity==&foreign);CHECK(esp_cpu_task_scope_close(scope)==OK);
    }else if(name=="stack-bytes-zero"||name=="stack-bytes-one"||name=="stack-bytes-max"||name=="stack-bytes-nearmax"){
        scope=open();auto c=config();Publication out{{77,88},&foreign};
        expected_stack_bytes=name=="stack-bytes-zero"?0:name=="stack-bytes-one"?1:name=="stack-bytes-max"?UINT32_MAX:UINT32_MAX-1;
        c.stack_bytes=expected_stack_bytes;fail_create=true;
        CHECK(esp_cpu_task_scope_create(scope,&c,&out)==(expected_stack_bytes>1?EXHAUSTED:INVALID));
        CHECK(creates==(expected_stack_bytes>1?1u:0u)&&deletes==0&&private_entries==0);
        CHECK(out.ticket.scope==77&&out.ticket.serial==88&&out.kernel_identity==&foreign);
        CHECK(c.stack_bytes==expected_stack_bytes);CHECK(esp_cpu_task_scope_close(scope)==OK);
    }else if(name=="stack-zero-budget"||name=="stack-floor-minus-one"||name=="stack-exact-floor"||name=="stack-malformed-layout"||name=="stack-overflow-layout"||name=="stack-query-failure"){
        scope=open(1);auto c=config();Publication out{{77,88},&foreign};uint32_t minimum=0;
        CHECK(esp_cpu_task_stack_floor_v1(&current_layout,c.working_stack_bytes,&minimum));
        if(name=="stack-zero-budget")c.working_stack_bytes=0;
        if(name=="stack-floor-minus-one")c.stack_bytes=minimum-1;
        if(name=="stack-exact-floor")c.stack_bytes=minimum;
        if(name=="stack-malformed-layout")current_layout.alignment=3;
        if(name=="stack-overflow-layout")current_layout.coprocessor_bytes=UINT32_MAX;
        if(name=="stack-query-failure")layout_query_ok=false;
        fail_create=true;expected_stack_bytes=c.stack_bytes;bool accepted=name=="stack-exact-floor";
        CHECK(esp_cpu_task_scope_create(scope,&c,&out)==(accepted?EXHAUSTED:INVALID));
        CHECK(creates==(accepted?1u:0u)&&deletes==0&&private_entries==0);
        CHECK(out.ticket.scope==77&&out.ticket.serial==88&&out.kernel_identity==&foreign);
        if(!accepted){ // rejected validation must not consume the single ticket
            current_layout={0x1000,0x1011,0x1020,0x1033,48,160,16};layout_query_ok=true;c=config();expected_stack_bytes=4096;
            CHECK(esp_cpu_task_scope_create(scope,&c,&out)==EXHAUSTED);CHECK(creates==1);
        }
        CHECK(esp_cpu_task_scope_close(scope)==OK);
    }else if(name=="foreign-context"){
        scope=open();auto c=config();Publication out{};Context foreign_ctx(&foreign);
        CHECK(esp_cpu_task_scope_create(scope,&c,&out)==CONTEXT);CHECK(esp_cpu_task_scope_close(scope)==CONTEXT);CHECK(creates==0);
    }else if(name=="isr-context"){
        scope=open();isr=true;auto c=config();Publication out{};CHECK(esp_cpu_task_scope_create(scope,&c,&out)==CONTEXT);CHECK(esp_cpu_task_scope_delete_join({scope.id,1})==CONTEXT);CHECK(creates==0);isr=false;CHECK(esp_cpu_task_scope_close(scope)==OK);
    }else if(name=="key-allocation-failure"){
        fail_alloc=0;Scope out{99};CHECK(esp_cpu_task_scope_open(1,&out)==EXHAUSTED);CHECK(out.id==99&&creates==0);fail_alloc=-1;scope=open();CHECK(esp_cpu_task_scope_close(scope)==OK);
    }else if(name=="kernel-create-failure"||name=="lifetime-budget"){
        scope=open(1);fail_create=true;auto c=config();Publication out{{77,88},&foreign};CHECK(esp_cpu_task_scope_create(scope,&c,&out)==EXHAUSTED);CHECK(out.ticket.serial==88&&creates==1&&deletes==0);
        if(name=="lifetime-budget"){fail_create=false;CHECK(esp_cpu_task_scope_create(scope,&c,&out)==EXHAUSTED);CHECK(creates==1);}
        CHECK(esp_cpu_task_scope_close(scope)==OK);
    }else if(name=="tls-list-allocation-failure"||name=="tls-value-allocation-failure"){
        scope=open();alloc_count=0;fail_alloc=name=="tls-list-allocation-failure"?0:1;auto c=config();Publication out{{77,88},&foreign};
        CHECK(esp_cpu_task_scope_create(scope,&c,&out)==EXHAUSTED);CHECK(out.ticket.serial==88&&private_entries==0&&self_deletes==1);CHECK(esp_cpu_task_scope_close(scope)==OK);finish();
    }else if(name=="publication-and-copy"){
        scope=open();hold_create=true;mutate_input=true;auto c=config();input_config=&c;CHECK(esp_cpu_task_scope_create(scope,&c,&publication)==OK);wait_until([]{return entry_ready.load();});CHECK(private_entries==1);CHECK(c.entry==nullptr&&std::strcmp(input_name,"changed")==0);
        CHECK(esp_cpu_task_scope_close(scope)==PENDING);CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==OK);CHECK(esp_cpu_task_scope_close(scope)==OK);finish();
    }else if(name=="published-ready-delete"){
        scope=open();hold_before_private=true;hold_create=true;create();CHECK(private_entries==0);CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==OK);CHECK(private_entries==0&&deletes==1&&self_deletes==0);hold_before_private=false;CHECK(esp_cpu_task_scope_close(scope)==OK);finish();
    }else if(name=="duplicate-delete-join"){
        scope=open();create();wait_until([]{return entry_ready.load();});std::atomic<bool> launch{false};std::atomic<unsigned> returned{0};
        DeleterArgument a{{},publication.ticket,&launch,&returned},b{{},publication.ticket,&launch,&returned};
        auto c=config(entry_deleter);c.argument=&a;CHECK(esp_cpu_task_scope_create(scope,&c,&a.self)==OK);c.argument=&b;CHECK(esp_cpu_task_scope_create(scope,&c,&b.self)==OK);
        launch=true;for(unsigned n=0;returned<2&&n<3000;++n)vTaskDelay(1);CHECK(returned==2);
        for(unsigned n=0;n<3000;++n){vTaskDelay(1);if(esp_cpu_task_scope_close(scope)==OK)break;if(n==2999)CHECK(false);}
        CHECK(deletes==3&&self_deletes==2);finish();
    }else if(name=="nested-scope-refusal"){
        scope=open();create(entry_nested_scope);await_ack();CHECK(nested_scope_refused);CHECK(esp_cpu_task_scope_close(scope)==OK);finish();
    }else if(name=="delete-drains-active-create"){
        scope=open();hold_nested_create=true;create(entry_nested_create);wait_until([]{return nested_create_entered.load();});unsigned delays=root_delays;
        std::thread witness([&]{Context c(&foreign);wait_until([&]{return root_delays.load()>delays;});CHECK(deletes==0&&creates==2);release_nested_create=true;});
        CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==OK);witness.join();CHECK(nested_create_returned&&fresh_create_refused);hold_nested_create=false;
        CHECK(esp_cpu_task_scope_delete_join(grandchild.ticket)==OK);CHECK(esp_cpu_task_scope_close(scope)==OK);finish();
    }else if(name=="delete-drains-active-join"){
        scope=open();create();wait_until([]{return entry_ready.load();});std::atomic<bool> launch{false};std::atomic<unsigned> returned{0};
        DeleterArgument d{{},publication.ticket,&launch,&returned};auto c=config(entry_deleter);c.argument=&d;CHECK(esp_cpu_task_scope_create(scope,&c,&d.self)==OK);
        newlib_gate=true;launch=true;wait_until([]{return newlib_reached.load();});unsigned delays=root_delays;
        std::thread witness([&]{Context ctx(&foreign);wait_until([&]{return root_delays.load()>delays;});CHECK(deletes==1&&returned==0);newlib_gate=false;});
        CHECK(esp_cpu_task_scope_delete_join(d.self.ticket)==OK);witness.join();CHECK(returned==1&&deletes==2);CHECK(esp_cpu_task_scope_close(scope)==OK);finish();
    }else if(name=="self-exit-wins-delete-race"){
        scope=open();release_entry=true;create();wait_until([]{std::lock_guard<std::mutex> g(tasks_mutex);return tasks[0]->done.load();});CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==OK);CHECK(deletes==1&&self_deletes==1);CHECK(esp_cpu_task_scope_close(scope)==OK);finish();
    }else if(name=="normal-return"||name=="explicit-self-exit"||name=="self-delete-join"){
        scope=open();release_entry=true;create(name=="normal-return"?entry_wait:name=="explicit-self-exit"?entry_self:entry_self_join);await_ack();CHECK(self_deletes==1&&private_entries==1);CHECK(esp_cpu_task_scope_close(scope)==OK);finish();
    }else if(name=="direct-delete"||name=="deferred-delete"||name=="all-pthread-destructors"){
        scope=open();defer_delete=name=="deferred-delete";create(name=="all-pthread-destructors"?entry_tls:entry_wait);wait_until([]{return entry_ready.load();});CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==OK);
        auto r=inspect(publication.ticket,OK);CHECK(r.tls_returned&&r.kernel_tail_complete&&r.kernel_task_ownership_retired&&!r.direct_delete_inflight);CHECK(deletes==1&&self_deletes==0);
        if(name=="all-pthread-destructors"){CHECK(private_destructors==2);CHECK(std::find(events.begin(),events.end(),"private-destructor-2")<std::find(events.begin(),events.end(),"newlib-private-FILE-callback"));}
        CHECK(esp_cpu_task_scope_close(scope)==OK);finish();
    }else if(name=="tls-stall"||name=="newlib-stall"){
        scope=open();create(entry_tls);wait_until([]{return entry_ready.load();});bool tls=name=="tls-stall";tls_gate=tls;newlib_gate=!tls;
        std::thread witness([&]{Context foreign_ctx(&foreign);wait_until([&]{return tls?tls_reached.load():newlib_reached.load();});Report r{};CHECK(esp_cpu_task_scope_poll(publication.ticket,&r)==PENDING);CHECK(!r.kernel_tail_complete&&!r.kernel_task_ownership_retired);CHECK(tls?!r.tls_returned:r.tls_returned);tls_gate=false;newlib_gate=false;});
        CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==OK);witness.join();CHECK(private_destructors==2&&newlib_calls==1);CHECK(esp_cpu_task_scope_close(scope)==OK);finish();
    }else if(name=="forged-token"||name=="cross-scope-token"){
        scope=open();create();wait_until([]{return entry_ready.load();});Token forged=publication.ticket;if(name=="forged-token")++forged.serial;else forged.scope=open().id;
        CHECK(esp_cpu_task_scope_delete_join(forged)==STALE);CHECK(deletes==0);CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==OK);CHECK(esp_cpu_task_scope_close(scope)==OK);finish();
    }else if(name=="foreign-delete"){
        scope=open();create();wait_until([]{return entry_ready.load();});{Context c(&foreign);CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==CONTEXT);}CHECK(deletes==0);CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==OK);CHECK(esp_cpu_task_scope_close(scope)==OK);finish();
    }else if(name=="void-refusal-parks"){
        scope=open();expect_park=true;try{esp_cpu_task_scope_delete_or_park({scope.id,999});CHECK(false);}catch(const ExpectedPark&){}CHECK(deletes==0&&parks>=park_after);expect_park=false;CHECK(esp_cpu_task_scope_close(scope)==OK);
    }else if(name=="lost-proxy-never-acks"){
        scope=open();ignore_proxy=true;create();wait_until([]{return entry_ready.load();});expect_park=true;
        try{(void)esp_cpu_task_scope_delete_join(publication.ticket);CHECK(false);}catch(const ExpectedPark&){}
        expect_park=false;auto r=inspect(publication.ticket);CHECK(!r.tls_returned&&!r.kernel_tail_complete);CHECK(deletes==1);CHECK(esp_cpu_task_scope_close(scope)==PENDING);finish();
    }else if(name=="wrong-created-identity"){
        scope=open();wrong_identity=true;auto c=config();Publication out{{77,88},&foreign};CHECK(esp_cpu_task_scope_create(scope,&c,&out)==RETAINED);CHECK(out.ticket.serial==88&&private_entries==0);CHECK(esp_cpu_task_scope_close(scope)==RETAINED);wait_until([]{std::lock_guard<std::mutex> g(tasks_mutex);return tasks[0]->done.load();});idle_pump();finish();
    }else if(name=="closed-scope"){
        scope=open();create();wait_until([]{return entry_ready.load();});CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==OK);CHECK(esp_cpu_task_scope_close(scope)==OK);CHECK(esp_cpu_task_scope_close(scope)==STALE);CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==STALE);auto c=config();Publication out{};CHECK(esp_cpu_task_scope_create(scope,&c,&out)==STALE);CHECK(creates==1&&deletes==1);finish();
    }else if(name=="reused-identity-retained"){
        scope=open();create();wait_until([]{return entry_ready.load();});CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==OK);reuse_task=static_cast<FakeTask*>(publication.kernel_identity);entry_ready=false;auto c=config();Publication out{{77,88},&foreign};CHECK(esp_cpu_task_scope_create(scope,&c,&out)==RETAINED);CHECK(out.ticket.serial==88&&private_entries==1);CHECK(esp_cpu_task_scope_close(scope)==RETAINED);wait_until([]{std::lock_guard<std::mutex> g(tasks_mutex);return tasks[0]->done.load();});idle_pump();finish();
    }else if(name=="callback-escape-outside-contract"){
        scope=open();CHECK(root.tls==nullptr);create(entry_escape);wait_until([]{return entry_ready.load();});
        CHECK(esp_cpu_task_scope_delete_join(publication.ticket)==OK);
        auto r=inspect(publication.ticket,OK);CHECK(r.tls_returned&&r.kernel_tail_complete);
        CHECK(escape_calls==1&&escaped_calls==0&&root.tls&&root.destructor);
        CHECK(scope_fixture_pthread_getspecific(escaped_key)==reinterpret_cast<void*>(3));
        CHECK(esp_cpu_task_scope_close(scope)==OK);finish();
        // Negative demonstration: target ACK/close does NOT remove this foreign
        // executor's newly installed private callback. Leave it visible until
        // process exit; this behavior is expressly outside admitted custody.
        CHECK(root.tls&&root.destructor&&escaped_calls==0);
    }else if(name=="void-isr-fail-stop"){
        isr=true;std::puts("EXPECTED_ISR_FAIL_STOP");std::fflush(stdout);esp_cpu_task_scope_delete_or_park({1,1});CHECK(false);
    }else CHECK(false);
}
int main(int argc,char**argv){CHECK(argc==2);run(argv[1]);std::printf("PASS %s\n",argv[1]);return 0;}
