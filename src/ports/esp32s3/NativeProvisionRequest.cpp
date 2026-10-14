#include "NativeProvisionRequest.h"
#if RISC_NATIVE_PROVISION_REQUEST
#ifndef RISC_PAIRED_BANKS
#error Provision requests require existing paired storage
#endif
#include "NativeBankStore.h"
#include "NvsBootstrapInput.h"
#include "runtime/provisioning/RequestQueue.h"
#include <RiscProvisionRequestV1.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <cstring>
#include <new>
namespace RiscProvisionRequest {namespace {
using namespace RiscProvision;
bool(*ownerTask)()=nullptr;
bool requestUnknown=false;
bool owner(){return ownerTask&&ownerTask();}
bool hash(const void*p,uint32_t n,uint8_t*out){
 mbedtls_sha256_context h;mbedtls_sha256_init(&h);
 bool ok=mbedtls_sha256_starts_ret(&h,0)==0&&mbedtls_sha256_update_ret(&h,static_cast<const uint8_t*>(p),n)==0&&mbedtls_sha256_finish_ret(&h,out)==0;
 mbedtls_sha256_free(&h);return ok;
}
struct Buffer {uint8_t*bytes=nullptr;Buffer(){bytes=static_cast<uint8_t*>(heap_caps_malloc(RequestRecordBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));}
 ~Buffer(){if(bytes){volatile uint8_t*p=bytes;for(uint32_t i=0;i<RequestRecordBytes;++i)p[i]=0;free(bytes);}}};
InputStatus read(void*context,const char*key,void*out,uint32_t cap,uint32_t*actual){
 if(actual)*actual=0;
 if(!key||!out||!actual)return InputStatus::Invalid;
 nvs_handle_t h=*static_cast<nvs_handle_t*>(context);size_t n=0;auto rc=nvs_get_blob(h,key,nullptr,&n);
 if(rc==ESP_ERR_NVS_NOT_FOUND)return InputStatus::Missing;
 if(rc!=ESP_OK||!n||n>cap)return InputStatus::Invalid;
 size_t expected=n;rc=nvs_get_blob(h,key,out,&n);if(rc!=ESP_OK||n!=expected)return InputStatus::Invalid;
 *actual=uint32_t(n);return InputStatus::Ready;
}
InputStatus load(Buffer&b,uint32_t&n){
 n=0;if(!b.bytes||RiscNvs::initializationStatus()!=ESP_OK)return InputStatus::Unavailable;
 nvs_handle_t h=0;auto rc=nvs_open("rte_provreq",NVS_READONLY,&h);
 if(rc==ESP_ERR_NVS_NOT_FOUND)return InputStatus::Missing;
 if(rc!=ESP_OK)return InputStatus::Unavailable;
 auto status=read(&h,"request",b.bytes,RequestRecordBytes,&n);nvs_close(h);return status;
}
int32_t status(void*,risc_provision_request_status_v1*out){
 if(!owner()||!out||out->struct_size<sizeof(*out))return RISC_PROVISION_REQUEST_UNAVAILABLE;
 RequestSource source;risc_bank_status_v1 bank{};
 if(!RiscBankStore::provisionRequestSource(source,bank,true))return RISC_PROVISION_REQUEST_UNAVAILABLE;
 *out={};out->struct_size=sizeof(*out);out->store_abi=source.storeAbi;out->firmware_size=source.firmwareSize;
 memcpy(out->active_store_sha256,source.storeSha,32);memcpy(out->firmware_sha256,source.firmwareSha,32);
 memcpy(out->runtime_version,bank.runtime_version,sizeof(out->runtime_version));memcpy(out->layout,bank.layout,sizeof(out->layout));
 Buffer b;if(!b.bytes)return RISC_PROVISION_REQUEST_UNAVAILABLE;
 uint32_t n=0;auto result=load(b,n);
 if(result==InputStatus::Missing){requestUnknown=false;return RISC_PROVISION_REQUEST_OK;}
 RequestView request;if(result!=InputStatus::Ready||!parseRequest(b.bytes,n,request,hash)){out->state=RISC_PROVISION_REQUEST_BLOCKED;return RISC_PROVISION_REQUEST_OK;}
 requestUnknown=false;
 memcpy(out->request_sha256,request.requestSha,32);memcpy(out->profile_sha256,request.profileSha,32);
 memcpy(out->target_store_sha256,request.targetStoreSha,32);
 out->state=requestConsumed(request,source)?RISC_PROVISION_REQUEST_CONSUMED:requestSourceMatches(request,source)?RISC_PROVISION_REQUEST_PENDING:RISC_PROVISION_REQUEST_BLOCKED;
 return RISC_PROVISION_REQUEST_OK;
}
int32_t queue(void*,const risc_provision_request_v1*q,uint8_t*out){
 if(out)memset(out,0,32);
 if(!owner()||!q||!out||RiscNvs::initializationStatus()!=ESP_OK)return RISC_PROVISION_REQUEST_UNAVAILABLE;
 if(requestUnknown)return RISC_PROVISION_REQUEST_UNKNOWN;
 if(q->struct_size<sizeof(*q)||!q->time_size||!validInstallInput(q->profile_json,q->profile_size,q->time_json,q->time_size))return RISC_PROVISION_REQUEST_INVALID;
 RequestSource source;risc_bank_status_v1 bank{};
 if(!RiscBankStore::provisionRequestSource(source,bank,true))return RISC_PROVISION_REQUEST_UNAVAILABLE;
 Buffer old,next;if(!old.bytes||!next.bytes)return RISC_PROVISION_REQUEST_UNAVAILABLE;
 nvs_handle_t h=0;if(nvs_open("rte_provreq",NVS_READWRITE,&h)!=ESP_OK)return RISC_PROVISION_REQUEST_UNAVAILABLE;
 InstallTransport transport{{&h,read},[](void*c,const char*k,const void*p,uint32_t n){return owner()&&nvs_set_blob(*static_cast<nvs_handle_t*>(c),k,p,n)==ESP_OK;},[](void*c){return owner()&&nvs_commit(*static_cast<nvs_handle_t*>(c))==ESP_OK;}};
 int32_t result=queueRequest(transport,*q,source,old.bytes,next.bytes,RequestRecordBytes,hash,out);nvs_close(h);
 if(result==RISC_PROVISION_REQUEST_UNKNOWN)requestUnknown=true;
 return result;
}
const risc_provision_request_api_v1 api{1,sizeof(api),nullptr,status,queue};
const risc_provision_request_api_v1 admissionApi{1,sizeof(admissionApi),nullptr,
 [](void*,risc_provision_request_status_v1*)->int32_t{return RISC_PROVISION_REQUEST_UNAVAILABLE;},
 [](void*,const risc_provision_request_v1*,uint8_t*)->int32_t{return RISC_PROVISION_REQUEST_UNAVAILABLE;}};
Buffer*bootBuffer=nullptr;RequestView bootView;
Input unavailable(){return {nullptr,[](void*,const char*,void*,uint32_t,uint32_t*n){if(n)*n=0;return InputStatus::Unavailable;}};}
}
bool bind(RiscBoot::Runtime&r,bool(*own)()){ownerTask=own;return owner()&&r.registerPlatform(RISC_PROVISION_REQUEST_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&api);}
bool bindAdmission(RiscBoot::Runtime&r){return r.registerPlatform(RISC_PROVISION_REQUEST_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&admissionApi);}
Input bootInput(Input original,bool&preserving){
 preserving=true;if(bootBuffer)return unavailable();
 bootBuffer=new(std::nothrow) Buffer;if(!bootBuffer)return unavailable();
 uint32_t n=0;auto result=load(*bootBuffer,n);
 if(result==InputStatus::Missing){releaseBootInput();preserving=false;return original;}
 RequestSource source;risc_bank_status_v1 bank{};
 if(result!=InputStatus::Ready||!parseRequest(bootBuffer->bytes,n,bootView,hash)||
    !RiscBankStore::provisionRequestSource(source,bank,false)||
    (!requestConsumed(bootView,source)&&!requestSourceMatches(bootView,source)))return unavailable();
 return requestInput(bootView);
}
void releaseBootInput(){delete bootBuffer;bootBuffer=nullptr;bootView={};}
}
#endif
