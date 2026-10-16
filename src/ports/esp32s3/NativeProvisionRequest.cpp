#include "NativeProvisionRequest.h"
#if RISC_NATIVE_PROVISION_REQUEST
#ifndef RISC_PAIRED_BANKS
#error Provision requests require existing paired storage
#endif
#include "NativeBankStore.h"
#include "NvsBootstrapInput.h"
#include "runtime/provisioning/RequestQueue.h"
#include "NvsProvisionRequestStorage.h"
#include "NativeRadioResources.h"
#include <RiscProvisionRequestV1.h>
#include <esp_heap_caps.h>
#include "runtime/crypto/Sha256.h"
#include <cstring>
#include <new>
#include <type_traits>
#if RISC_NATIVE_ESP_IDF && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#include <esp_attr.h>
#endif
namespace RiscProvisionRequest {namespace {
using namespace RiscProvision;
bool(*ownerTask)()=nullptr;
bool requestUnknown=false;
bool owner(){return ownerTask&&ownerTask();}
bool hash(const void*p,uint32_t n,uint8_t*out){
 return RiscCrypto::Sha256::digest(static_cast<const uint8_t*>(p),n,out);
}
struct Buffer {uint8_t*bytes=nullptr;Buffer(){bytes=static_cast<uint8_t*>(heap_caps_malloc(RequestRecordBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));}
 ~Buffer(){if(bytes){volatile uint8_t*p=bytes;for(uint32_t i=0;i<RequestRecordBytes;++i)p[i]=0;free(bytes);}}};
InputStatus read(void*context,const char*key,void*out,uint32_t cap,uint32_t*actual){
 return RiscProvisionRequestStorage::read(*static_cast<nvs_handle_t*>(context),key,out,cap,actual);
}
InputStatus load(Buffer&b,uint32_t&n){
 n=0;if(!b.bytes||RiscNvs::initializationStatus()!=ESP_OK)return InputStatus::Unavailable;
 nvs_handle_t h=0;auto rc=nvs_open("rte_provreq",NVS_READONLY,&h);
 if(rc==ESP_ERR_NVS_NOT_FOUND)return InputStatus::Missing;
 if(rc!=ESP_OK)return InputStatus::Unavailable;
 auto status=readRequest({&h,read},b.bytes,RequestRecordBytes,&n);nvs_close(h);return status;
}
int32_t status(void*,risc_provision_request_status_v1*out){
 if(!owner()||!out||out->struct_size<sizeof(*out))return RISC_PROVISION_REQUEST_UNAVAILABLE;
 // A returned NVS mutation error can leave the live IDF page cache unsafe to
 // read, even when raw flash recovers correctly on boot. Do not reconcile
 // through that instance. Only ordinary boot resets this process-local fence.
 if(requestUnknown){*out={};out->struct_size=sizeof(*out);out->state=RISC_PROVISION_REQUEST_BLOCKED;return RISC_PROVISION_REQUEST_UNKNOWN;}
 RequestSource source;risc_bank_status_v1 bank{};
 if(!RiscBankStore::provisionRequestSource(source,bank,true))return RISC_PROVISION_REQUEST_UNAVAILABLE;
 *out={};out->struct_size=sizeof(*out);out->store_abi=source.storeAbi;out->firmware_size=source.firmwareSize;
 memcpy(out->active_store_sha256,source.storeSha,32);memcpy(out->firmware_sha256,source.firmwareSha,32);
 memcpy(out->runtime_version,bank.runtime_version,sizeof(out->runtime_version));memcpy(out->layout,bank.layout,sizeof(out->layout));
 Buffer b;if(!b.bytes)return RISC_PROVISION_REQUEST_UNAVAILABLE;
 uint32_t n=0;auto result=load(b,n);
 if(result==InputStatus::Missing)return RISC_PROVISION_REQUEST_OK;
 RequestView request;if(result!=InputStatus::Ready||!parseRequest(b.bytes,n,request,hash)){out->state=RISC_PROVISION_REQUEST_BLOCKED;return RISC_PROVISION_REQUEST_OK;}
 memcpy(out->request_sha256,request.requestSha,32);memcpy(out->profile_sha256,request.profileSha,32);
 memcpy(out->target_store_sha256,request.targetStoreSha,32);
 out->state=requestConsumed(request,source)?RISC_PROVISION_REQUEST_CONSUMED:requestSourceMatches(request,source)?RISC_PROVISION_REQUEST_PENDING:RISC_PROVISION_REQUEST_BLOCKED;
 return RISC_PROVISION_REQUEST_OK;
}
int32_t queue(void*,const risc_provision_request_v1*q,uint8_t*out){
 if(out)memset(out,0,32);
 if(!owner()||!q||!out)return RISC_PROVISION_REQUEST_UNAVAILABLE;
 if(requestUnknown)return RISC_PROVISION_REQUEST_UNKNOWN;
 RiscCpu::NativeRadioResourceGuard lease;
 if(!lease.held||RiscNvs::initializationStatus()!=ESP_OK)return RISC_PROVISION_REQUEST_UNAVAILABLE;
 if(q->struct_size<sizeof(*q)||!q->time_size||!validInstallInput(q->profile_json,q->profile_size,q->time_json,q->time_size))return RISC_PROVISION_REQUEST_INVALID;
 RequestSource source;risc_bank_status_v1 bank{};
 if(!RiscBankStore::provisionRequestSource(source,bank,true))return RISC_PROVISION_REQUEST_UNAVAILABLE;
 Buffer old,next;if(!old.bytes||!next.bytes)return RISC_PROVISION_REQUEST_UNAVAILABLE;
 nvs_handle_t h=0;if(nvs_open("rte_provreq",NVS_READWRITE,&h)!=ESP_OK)return RISC_PROVISION_REQUEST_UNAVAILABLE;
 RequestTransport transport{{&h,read},
  [](void*c,const char*k,const void*p,uint32_t n){return owner()&&RiscProvisionRequestStorage::set(*static_cast<nvs_handle_t*>(c),k,p,n);},
  [](void*c){return owner()&&nvs_commit(*static_cast<nvs_handle_t*>(c))==ESP_OK;},
  [](void*c,const char*k){return owner()&&RiscProvisionRequestStorage::erase(*static_cast<nvs_handle_t*>(c),k);},
  [](void*c,const char*k){return owner()?RiscProvisionRequestStorage::probe(*static_cast<nvs_handle_t*>(c),k):InputStatus::Unavailable;}};
 int32_t result=queueRequest(transport,*q,source,old.bytes,next.bytes,RequestRecordBytes,hash,out);nvs_close(h);
 if(result==RISC_PROVISION_REQUEST_UNKNOWN){requestUnknown=true;RiscNvs::quarantine();}
 return result;
}
const risc_provision_request_api_v1 api{1,sizeof(api),nullptr,status,queue};
const risc_provision_request_api_v1 admissionApi{1,sizeof(admissionApi),nullptr,
 [](void*,risc_provision_request_status_v1*)->int32_t{return RISC_PROVISION_REQUEST_UNAVAILABLE;},
 [](void*,const risc_provision_request_v1*,uint8_t*)->int32_t{return RISC_PROVISION_REQUEST_UNAVAILABLE;}};
Buffer*bootBuffer=nullptr;
static_assert(std::is_trivially_destructible<RequestView>::value,
 "Reused provision boot metadata must not require destruction");
// Boot-owner-only parsed metadata. The SDK initializes PSRAM before app_main;
// construct after acquiring bootBuffer and never access after its release.
// NVS custody/owner gates stay internal, including the quarantine fence.
alignas(RequestView)
#if RISC_NATIVE_ESP_IDF && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#if !CONFIG_SPIRAM_BOOT_INIT
#error "External provision boot metadata requires SDK PSRAM boot initialization"
#endif
EXT_RAM_BSS_ATTR
#endif
unsigned char bootViewStorage[sizeof(RequestView)];
RequestView& bootView(){return *std::launder(reinterpret_cast<RequestView*>(bootViewStorage));}
Input unavailable(){return {nullptr,[](void*,const char*,void*,uint32_t,uint32_t*n){if(n)*n=0;return InputStatus::Unavailable;}};}
}
bool bind(RiscBoot::Runtime&r,bool(*own)()){ownerTask=own;return owner()&&r.registerPlatform(RISC_PROVISION_REQUEST_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&api);}
bool bindAdmission(RiscBoot::Runtime&r){return r.registerPlatform(RISC_PROVISION_REQUEST_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&admissionApi);}
Input bootInput(Input original,bool&preserving){
 preserving=true;if(requestUnknown||bootBuffer)return unavailable();
 bootBuffer=new(std::nothrow) Buffer;if(!bootBuffer)return unavailable();
 new (bootViewStorage) RequestView;
 uint32_t n=0;auto result=load(*bootBuffer,n);
 if(result==InputStatus::Missing){releaseBootInput();preserving=false;return original;}
 RequestSource source;risc_bank_status_v1 bank{};
 if(result!=InputStatus::Ready||!parseRequest(bootBuffer->bytes,n,bootView(),hash)||
    !RiscBankStore::provisionRequestSource(source,bank,false)||
    (!requestConsumed(bootView(),source)&&!requestSourceMatches(bootView(),source)))return unavailable();
 return requestInput(bootView());
}
void releaseBootInput(){if(bootBuffer)bootView()={};delete bootBuffer;bootBuffer=nullptr;}
}
#endif
