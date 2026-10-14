// Compile-only probe of the actual private binding and registry layout. This
// object is never linked into firmware. Target SDK headers provide the TCB ABI.
#include "../src/ports/esp32s3/NativeSettingsWorker.cpp"
namespace W=RiscCpu::NativeSettingsWorker;
extern "C" {
unsigned char native_settings_binding_size[sizeof(W::Binding)];
unsigned char native_settings_binding_alignment[alignof(W::Binding)];
unsigned char native_settings_binding_allocation_size[sizeof(W::Binding)+alignof(W::Binding)-1];
unsigned char native_settings_registry_size[sizeof(W::Registry)];
unsigned char native_settings_two_bindings_stack_size[W::MaxBindings*W::StackBytes];
unsigned char native_settings_two_bindings_task_control_size[W::MaxBindings*sizeof(StaticTask_t)];
unsigned char native_settings_two_bindings_permanent_size[W::MaxBindings*(sizeof(W::Binding)+alignof(W::Binding)-1)+sizeof(W::Registry)];
}
