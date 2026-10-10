/* Local C++ lifecycle/runtime support. No task, board or libc filesystem ABI. */
#include "RiscRuntimeV1.h"
#include "RiscCppRuntime.h"
#include <cstdlib>
#include <new>
#include <cstdarg>
namespace {
void(*failureHandler)();
intptr_t initializationJump[5];bool initializing;
[[noreturn]] void stop(){if(failureHandler)failureHandler();const auto* rt=risc_runtime_get_api(1);if(rt&&rt->struct_size>=RISC_RUNTIME_RETAIN_INVOCATION_V1_SIZE&&rt->retain_invocation)rt->retain_invocation();if(initializing)__builtin_longjmp(initializationJump,1);for(;;){if(rt&&rt->yield_ms)rt->yield_ms(50);}}
struct Finalizer {void(*call)(void*);void* object;void(*simple)();};
Finalizer finalizers[128];unsigned finalizerCount;
}
void* operator new(size_t n){if(void* p=malloc(n?n:1))return p;stop();}
void* operator new[](size_t n){return ::operator new(n);}
void* operator new(size_t n,const std::nothrow_t&)noexcept{return malloc(n?n:1);}
void* operator new[](size_t n,const std::nothrow_t&)noexcept{return malloc(n?n:1);}
void operator delete(void* p)noexcept{free(p);}void operator delete[](void* p)noexcept{free(p);}
void operator delete(void* p,size_t)noexcept{free(p);}void operator delete[](void* p,size_t)noexcept{free(p);}
namespace std {
const nothrow_t nothrow{};
void __throw_bad_alloc(){stop();}void __throw_length_error(const char*){stop();}void __throw_out_of_range(const char*){stop();}
void __throw_logic_error(const char*){stop();}void __throw_invalid_argument(const char*){stop();}
void __throw_out_of_range_fmt(const char*,...){stop();}void __throw_bad_function_call(){stop();}
void __throw_bad_array_new_length(){stop();}
}
extern "C" {
void risc_cpp_set_failure_handler(void(*f)()){failureHandler=f;}
void* __dso_handle=nullptr;
int __cxa_atexit(void(*f)(void*),void* object,void*){if(finalizerCount==128)stop();finalizers[finalizerCount++]={f,object,nullptr};return 0;}
int atexit(void(*f)()){if(finalizerCount==128)stop();finalizers[finalizerCount++]={nullptr,nullptr,f};return 0;}
void __cxa_pure_virtual(){stop();}
void __assert_func(const char*,int,const char*,const char*){stop();}
void abort(){stop();}
typedef void(*Initializer)();
extern Initializer __risc_cpp_init_start[],__risc_cpp_init_end[],__risc_cpp_ctor_start[],__risc_cpp_ctor_end[],__risc_cpp_dtor_start[],__risc_cpp_dtor_end[],__risc_cpp_fini_start[],__risc_cpp_fini_end[];
__attribute__((visibility("default"))) int app_module_init(){if(__builtin_setjmp(initializationJump)){initializing=false;return -1;}initializing=true;for(auto p=__risc_cpp_ctor_end;p!=__risc_cpp_ctor_start;)(*--p)();for(auto p=__risc_cpp_init_start;p!=__risc_cpp_init_end;++p)(*p)();initializing=false;return 0;}
__attribute__((visibility("default"))) void app_module_fini(){for(auto p=__risc_cpp_fini_end;p!=__risc_cpp_fini_start;)(*--p)();for(auto p=__risc_cpp_dtor_start;p!=__risc_cpp_dtor_end;++p)(*p)();while(finalizerCount){auto f=finalizers[--finalizerCount];if(f.simple)f.simple();else f.call(f.object);}}
}
