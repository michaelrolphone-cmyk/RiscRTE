// Compile separately from full Runtime fixture: no replacement Runtime methods.
#define main qualified_scope_fixture_main
#include "qualified_task_scope_fixture.cpp"
#undef main
extern "C" void v5_test_before_kernel_delete(TaskHandle_t) {}
extern "C" void integration_graph_tls(void*);
extern "C" void integration_install_tls(void*p){unsigned long k=0;CHECK(!scope_fixture_pthread_key_create(&k,integration_graph_tls));CHECK(!scope_fixture_pthread_setspecific(k,p));}
extern "C" bool integration_is_creator(){return current==&root;}
extern "C" void integration_foreign(bool yes){current=yes?&foreign:&root;}
extern "C" void integration_pump_tail(){idle_pump();}
extern "C" unsigned integration_tail_count(){return newlib_calls.load();}
extern "C" unsigned integration_create_count(){return creates.load();}
extern "C" unsigned integration_delete_count(){return deletes.load();}
extern "C" bool integration_tasks_done(){std::lock_guard<std::mutex> g(tasks_mutex);return !tasks.empty()&&std::all_of(tasks.begin(),tasks.end(),[](FakeTask*t){return t->done.load();});}
extern "C" void integration_finish_tasks(){finish();}
extern "C" void integration_hold_resident_gate(bool held){hold_before_private=held;}

