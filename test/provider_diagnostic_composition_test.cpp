// Real graph/module/loader/sink composition. Only target primitives and final
// Xtensa instruction entry are simulated. No controller code is executed.
#include "runtime/drivers/DeviceProviderExecutorV2.h"
#include "runtime/drivers/NativeProviderPolicySnapshotV1.h"
#include "runtime/drivers/NativeProviderPolicyValidationV1.h"
#include "ports/esp32s3/ProviderDiagnostics.h"
#include "ports/esp32s3/SleepDiagnostics.h"
#include "private/esp_privileged_os_cpu.h"
#include "private/elf_platform.h"
#include <Arduino.h>
#ifdef TEST_CONTROLLER_POLICY_HEADER
#include TEST_CONTROLLER_POLICY_HEADER
#endif
#include <openssl/evp.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>
using namespace RuntimeProviders;
using RuntimePackages::DeviceProviderExecutorV2;
namespace {
std::set<void*> allocations;
unsigned relocated=0,starts=0,stops=0,observed=0,drains=0;
std::string last;
bool inject=false,failArch=false;
const char* const fixtureImports[]={"memcpy","printf","putchar","puts","xTaskGetTickCount"};
const char* const* currentImports=fixtureImports;
size_t currentImportCount=5;
const uint8_t* activeBytes=nullptr;
using Printf=int(*)(const char*,...);
using Puts=int(*)(const char*);
using Putchar=int(*)(int);
Printf boundPrintf=nullptr;Puts boundPuts=nullptr;Putchar boundPutchar=nullptr;
int capability=42;
bool start(const risc_provider_dependency_v1*,size_t n){
 assert(!n && boundPrintf && boundPuts && boundPutchar);++starts;
 assert(boundPrintf("controller stage=%s rc=%d\n","start",17)==28);
 assert(last=="controller stage=start rc=17");
 assert(boundPuts("captured")==8 && last=="captured");
 assert(boundPutchar('x')=='x' && last=="x");return true;
}
bool quiesce(){return true;}void stop(){++stops;}
const risc_driver_v2* get(uint32_t abi){
 assert(abi==RISC_PROVIDER_DRIVER_ABI_V2);
 static risc_driver_v2 descriptor{};
 descriptor.abi_version=RISC_PROVIDER_DRIVER_ABI_V2;descriptor.struct_size=sizeof(descriptor);
 descriptor.driver_id="fixture-diagnostic";descriptor.capability_id="cap.diagnostic";descriptor.capability_api=1;
 descriptor.capability=&capability;descriptor.start=start;descriptor.stop=stop;descriptor.quiesce=quiesce;
 return &descriptor;
}
std::vector<uint8_t> read(const char* path){
 FILE* f=std::fopen(path,"rb");assert(f);assert(!std::fseek(f,0,SEEK_END));long n=std::ftell(f);assert(n>0);std::rewind(f);
 std::vector<uint8_t> bytes(size_t(n),0);assert(std::fread(bytes.data(),1,bytes.size(),f)==bytes.size());assert(!std::fclose(f));return bytes;
}
NativeProviderPolicyV1 policy(const std::vector<uint8_t>& bytes){
 NativeProviderPolicyV1 p;p.relativeElfPath="diagnostics.elf";p.driverId="fixture-diagnostic";p.version="1.0.0";
 p.capability="cap.diagnostic";p.api=p.osCpuAbi=p.diagnosticAbi=1;p.elfLength=bytes.size();p.imports=fixtureImports;p.importCount=5;
 assert(nativeProviderDigest(bytes.data(),bytes.size(),p.sha256));return p;
}
void scopes(){
 const char* diagnostics[]={"printf","putchar","puts"};
 for(const char* name:diagnostics){assert(!elf_find_sym(name));assert(!esp_elf_privileged_selected_import_supported_v1(name));
  assert(esp_elf_privileged_selected_import_supported_with_diagnostics_v1(name,1));
  assert(!esp_elf_privileged_selected_import_supported_with_diagnostics_v1(name,0));
  assert(!esp_elf_privileged_selected_import_supported_with_diagnostics_v1(name,2));}
 assert(esp_elf_privileged_os_cpu_begin_v1());
 for(const char* name:diagnostics)assert(!elf_find_sym(name));
 assert(esp_elf_privileged_os_cpu_end_v1());
 assert(!esp_elf_privileged_os_cpu_begin_selected_v1(diagnostics,3));
 assert(!esp_elf_privileged_os_cpu_begin_selected_diagnostics_v1(diagnostics,3,2));
 assert(esp_elf_privileged_os_cpu_begin_selected_diagnostics_v1(diagnostics,3,1));
 for(const char* name:diagnostics)assert(!elf_find_sym(name));
 esp_elf_t module{};assert(esp_elf_privileged_os_cpu_authorize_relocation_v1(&module));
 assert(esp_elf_privileged_os_cpu_relocation_enter_v1(&module));
 assert(elf_find_sym("printf")==reinterpret_cast<uintptr_t>(&risc_provider_diagnostic_printf));
 assert(elf_find_sym("puts")==reinterpret_cast<uintptr_t>(&risc_provider_diagnostic_puts));
 assert(elf_find_sym("putchar")==reinterpret_cast<uintptr_t>(&risc_provider_diagnostic_putchar));
 assert(!elf_find_sym("memcpy"));
 task=reinterpret_cast<void*>(2);for(const char* name:diagnostics)assert(!elf_find_sym(name));
 assert(!esp_elf_privileged_os_cpu_end_v1());task=reinterpret_cast<void*>(1);
 assert(esp_elf_privileged_os_cpu_relocation_leave_v1(&module));
 for(const char* name:diagnostics)assert(!elf_find_sym(name));
 assert(esp_elf_privileged_os_cpu_end_v1());
}
void compose(std::vector<uint8_t>& bytes){
 auto p=policy(bytes);assert(nativeProviderImageValid(p,bytes.data(),bytes.size()));
 auto invalid=p;invalid.diagnosticAbi=0;assert(!nativeProviderPolicyValid(invalid));invalid.diagnosticAbi=2;assert(!nativeProviderPolicyValid(invalid));
 auto* snapshot=NativeProviderPolicySnapshotV1::capture({&p,1});assert(snapshot && snapshot->entries[0].diagnosticAbi==1);
 p.diagnosticAbi=0;assert(snapshot->entries[0].diagnosticAbi==1);p.diagnosticAbi=1;NativeProviderPolicySnapshotV1::destroy(snapshot);
 SpecV2 spec{p.driverId,nullptr,p.capability,1,nullptr,0};spec.requiredOsCpuAbi=1;
 spec.verifiedElfBytes=bytes.data();spec.verifiedElfLength=bytes.size();spec.declaredImports=fixtureImports;spec.declaredImportCount=5;
 std::memcpy(spec.contentSha256,p.sha256,32);
 {GraphV2 graph;assert(!graph.addVerified(spec));assert(!DeviceProviderExecutorV2::registerNativePolicy(graph,spec,invalid));
 assert(DeviceProviderExecutorV2::registerNativePolicy(graph,spec,p));p.diagnosticAbi=0;
 activeBytes=bytes.data();inject=true;auto grant=graph.acquire("cap.diagnostic",1);inject=false;
 assert(grant.slot && graph.interfaceFor(grant)==&capability && starts==1 && relocated==5);
 assert(graph.release(grant) && graph.shutdown() && stops==1);}
 assert(allocations.empty());
 esp_elf_t module{};
 assert(esp_elf_relocate_privileged_selected_v1(&module,bytes.data(),bytes.size(),fixtureImports,5)==-EINVAL);
 assert(esp_elf_relocate_privileged_selected_diagnostics_v1(&module,bytes.data(),bytes.size(),fixtureImports,5,2)==-EINVAL);
 failArch=inject=true;assert(esp_elf_relocate_privileged_selected_diagnostics_v1(&module,bytes.data(),bytes.size(),fixtureImports,5,1)==-ENOSYS);
 failArch=inject=false;assert(allocations.empty() && !esp_elf_privileged_os_cpu_scope_owned_v1());
}
void controller(const char* path){
 auto bytes=read(path);uint8_t digest[32];assert(nativeProviderDigest(bytes.data(),bytes.size(),digest));
 char digestText[65];for(size_t i=0;i<32;++i)std::snprintf(digestText+2*i,3,"%02x",digest[i]);
 assert(!std::strcmp(digestText,"d0864938e573ac933fefc106991cfb033ec9599cb09902a518c53ffcaf57926c"));
 assert(esp_elf_validate_file(bytes.data(),bytes.size()));
 const auto* h=reinterpret_cast<const elf32_hdr_t*>(bytes.data());
 const auto* sections=reinterpret_cast<const elf32_shdr_t*>(bytes.data()+h->shoff);
 std::set<std::string> names;
 for(unsigned i=0;i<h->shnum;++i){const auto& sh=sections[i];if(sh.type!=SHT_SYNSYM && sh.type!=SHT_SYMTAB)continue;
  const auto* symbols=reinterpret_cast<const elf32_sym_t*>(bytes.data()+sh.offset);
  const char* strings=reinterpret_cast<const char*>(bytes.data()+sections[sh.link].offset);
  for(size_t n=0;n<sh.size/sizeof(*symbols);++n)if(symbols[n].shndx==SHN_UNDEF && symbols[n].name)names.emplace(strings+symbols[n].name);}
 std::vector<const char*> imports;for(const auto& name:names)imports.push_back(name.c_str());assert(imports.size()==47);
 auto p=policy(bytes);p.imports=imports.data();p.importCount=imports.size();
 p.relativeElfPath="driver.elf";p.driverId="usb-controller-esp32s3";p.version="0.1.24";p.capability="usb.controller";
 const NativeProviderRequirementV1 requirements[]={{"board.power.vbus",1},{"platform.usb.phy.resource",1}};
 p.requirements=requirements;p.requirementCount=2;
#ifdef TEST_CONTROLLER_POLICY_HEADER
 const auto selected=selectedNativeProviderPoliciesV1();assert(selected.count==1 && selected.entries);
 p=selected.entries[0];
 assert(!std::strcmp(p.relativeElfPath,"driver.elf") && !std::strcmp(p.driverId,"usb-controller-esp32s3") &&
        !std::strcmp(p.version,"0.1.24") && !std::strcmp(p.capability,"usb.controller") && p.diagnosticAbi==1);
 assert(p.requirementCount==2 && !std::strcmp(p.requirements[0].capability,"board.power.vbus") &&
        !std::strcmp(p.requirements[1].capability,"platform.usb.phy.resource"));
#endif
 assert(nativeProviderImageValid(p,bytes.data(),bytes.size()));p.diagnosticAbi=0;assert(!nativeProviderImageValid(p,bytes.data(),bytes.size()));
 esp_elf_t module{};p.diagnosticAbi=1;currentImports=p.imports;currentImportCount=p.importCount;activeBytes=bytes.data();inject=true;
 assert(!esp_elf_relocate_privileged_selected_diagnostics_v1(&module,bytes.data(),bytes.size(),p.imports,p.importCount,p.diagnosticAbi));
 inject=false;esp_elf_deinit(&module);assert(allocations.empty());
 std::puts("Actual pinned controller: 47 exact imports resolve in explicit diagnostic ABI 1; ABI 0 rejects; Xtensa execution not tested");
}
}
extern "C" void* diagnostic_test_task(){return task;}
extern "C" void risc_native_diagnostic_observer(const char* text){++observed;last=text;}
extern "C" void risc_native_diagnostic_drain(){++drains;}
extern "C" int mbedtls_sha256_ret(const unsigned char* bytes,size_t size,unsigned char digest[32],int mode){unsigned n=0;assert(!mode);return EVP_Digest(bytes,size,digest,&n,EVP_sha256(),nullptr)==1 && n==32?0:-1;}
extern "C" void* heap_caps_malloc(size_t n,unsigned){void* p=std::malloc(n);if(p)assert(allocations.insert(p).second);return p;}
extern "C" void heap_caps_free(void* p){if(p){assert(allocations.erase(p)==1);std::free(p);}}
extern "C" void* esp_elf_malloc(uint32_t n,bool){return heap_caps_malloc(n,0);}
extern "C" void esp_elf_free(void* p){heap_caps_free(p);}
extern "C" int esp_elf_arch_flush(esp_elf_t* elf){
 for(unsigned i=0;i<elf->num;++i)if(!std::strcmp(elf->symtab[i].name,"t5_driver_get"))elf->symtab[i].addr=reinterpret_cast<void*>(&get);
 return 0;
}
extern "C" int esp_elf_arch_relocate(esp_elf_t* elf,const elf32_rela_t*,const elf32_sym_t*,uint32_t){
 ++relocated;if(!inject)return 0;
 assert(esp_elf_privileged_os_cpu_scope_owned_v1());
 for(size_t i=0;i<currentImportCount;++i)assert(elf_find_sym(currentImports[i]));
 boundPrintf=reinterpret_cast<Printf>(elf_find_sym("printf"));boundPuts=reinterpret_cast<Puts>(elf_find_sym("puts"));boundPutchar=reinterpret_cast<Putchar>(elf_find_sym("putchar"));
 assert(boundPrintf==&risc_provider_diagnostic_printf && boundPuts==&risc_provider_diagnostic_puts && boundPutchar==&risc_provider_diagnostic_putchar);
 assert(!elf_find_sym("fputs") && !elf_find_sym("risc_provider_diagnostic_printf"));
 assert(!esp_elf_privileged_os_cpu_begin_selected_diagnostics_v1(currentImports,currentImportCount,1));
 assert(!esp_elf_privileged_os_cpu_end_v1());assert(esp_elf_relocate(elf,activeBytes)==-EPERM);
 task=reinterpret_cast<void*>(2);assert(!elf_find_sym("printf") && !elf_find_sym("putchar") && !elf_find_sym("puts"));task=reinterpret_cast<void*>(1);
 return failArch?-ENOSYS:0;
}
int main(int argc,char** argv){
 assert(argc==2 || argc==3);RiscDiagnostics::start();Serial.space=0;
 scopes();auto bytes=read(argv[1]);compose(bytes);assert(observed==3 && drains==3 && Serial.output.empty());
 if(argc==3)controller(argv[2]);
 assert(allocations.empty());
 std::puts("Diagnostic composition: real owned graph/module/loader/formatter/sink, exact ABI policy and isolated resolution PASS");
}
