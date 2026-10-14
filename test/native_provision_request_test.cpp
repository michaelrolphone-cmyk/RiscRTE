#include "ports/esp32s3/NativeProvisionRequest.cpp"
#include <cassert>
#include <map>
#include <string>
#include <vector>
using namespace RiscProvision;
static std::map<std::string,std::vector<uint8_t>> stored;
static RequestSource source;static bool nativeReady=true,owned=true,missingNamespace=true;
static unsigned writes=0,commits=0,opens=0,closes=0;static int failure=0;
namespace RiscNvs {esp_err_t initializationStatus(){return ESP_OK;}}
namespace RiscBankStore {bool provisionRequestSource(RequestSource&out,risc_bank_status_v1&status,bool){if(!nativeReady)return false;out=source;status={};strcpy(status.runtime_version,"0.2.23");strcpy(status.layout,"esp32s3-16mb-appdata");return true;}}
namespace RiscBoot {bool Runtime::registerPlatform(const char*,uint32_t,Scope,uint64_t,const void*){return true;}}
extern "C" esp_err_t nvs_open(const char*ns,nvs_open_mode_t mode,nvs_handle_t*out){assert(!strcmp(ns,"rte_provreq"));++opens;if(mode==NVS_READONLY&&missingNamespace)return ESP_ERR_NVS_NOT_FOUND;missingNamespace=false;*out=1;return ESP_OK;}
extern "C" esp_err_t nvs_get_blob(nvs_handle_t h,const char*k,void*out,size_t*n){assert(h==1);auto at=stored.find(k);if(at==stored.end())return ESP_ERR_NVS_NOT_FOUND;if(!out){*n=at->second.size();return ESP_OK;}assert(*n>=at->second.size());memcpy(out,at->second.data(),at->second.size());*n=at->second.size();return ESP_OK;}
extern "C" esp_err_t nvs_set_blob(nvs_handle_t h,const char*k,const void*p,size_t n){assert(h==1&&!strcmp(k,"request"));++writes;if(failure==1)return ESP_ERR_NVS_NOT_ENOUGH_SPACE;stored[k]={static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n};return ESP_OK;}
extern "C" esp_err_t nvs_commit(nvs_handle_t h){assert(h==1);++commits;return failure==2?ESP_FAIL:ESP_OK;}
extern "C" void nvs_close(nvs_handle_t h){assert(h==1);++closes;}
extern "C" esp_err_t nvs_flash_init(){assert(false);return ESP_FAIL;}
extern "C" esp_err_t nvs_flash_erase(){assert(false);return ESP_FAIL;}
int main(){
 RiscProvisionRequest::ownerTask=[](){return owned;};source.storeAbi=2;source.firmwareSize=4096;source.storeSha[0]=1;source.firmwareSha[0]=2;
 Input original{reinterpret_cast<void*>(1),nullptr};bool preserving=true;auto input=RiscProvisionRequest::bootInput(original,preserving);assert(!preserving&&input.context==original.context);RiscProvisionRequest::releaseBootInput();
 std::string p=R"({"schema":"riscrte.provisioning","schema_version":2,"wifi":{"ssid":"test","password":"test-only"},"base_url":"https://example.test/","files":[)";for(const char*k:{"boot.json","board.json","default.elf"}){if(p.back()!='[')p+=',';p+="{\"path\":\""+std::string(k)+"\",\"bytes\":1,\"sha256\":\""+std::string(64,'0')+"\"}";}p+="]}";
 std::string t=R"({"schema":"riscrte.sntp","schema_version":1,"servers":["time.example.test"]})";
 risc_provision_request_v1 request{sizeof(request),p.data(),uint32_t(p.size()),t.data(),uint32_t(t.size()),{}};memcpy(request.expected_active_store_sha256,source.storeSha,32);uint8_t digest[32];
 owned=false;assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_UNAVAILABLE&&!writes);owned=true;
 assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_OK&&writes==1);
 risc_provision_request_status_v1 status{};status.struct_size=sizeof(status);assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_PENDING&&!memcmp(status.request_sha256,digest,32));
 input=RiscProvisionRequest::bootInput(original,preserving);Descriptor descriptor;assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Ready);RiscProvisionRequest::releaseBootInput();
 source.storeSha[0]=3;input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();
 source.receiptAvailable=true;memcpy(source.consumedProfileSha,status.profile_sha256,32);assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_CONSUMED);
 input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Ready);RiscProvisionRequest::releaseBootInput();
 nativeReady=false;input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();nativeReady=true;
 auto originalBlob=stored["request"];stored["request"][0]^=1;input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();
 assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_BLOCKED);stored["request"]=originalBlob;
 request.expected_active_store_sha256[0]=3;p[p.find("test-only")]='b';request.profile_json=p.data();failure=1;assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_UNKNOWN&&stored["request"]==originalBlob);
 failure=0;assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_UNKNOWN);
 assert(RiscProvisionRequest::status(nullptr,&status)==0);risc_test_psram_fail_after=0;assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_UNAVAILABLE);risc_test_psram_fail_after=-1;
 // The public target digest is derived from the copied immutable image profile.
 std::string imageProfile=p;auto schema=imageProfile.find("schema_version\":2");assert(schema!=std::string::npos);
 imageProfile.replace(schema,strlen("schema_version\":2"),"schema_version\":3");
 auto base=imageProfile.find("\"base_url\":\"https://example.test/\"");assert(base!=std::string::npos);
 imageProfile.replace(base,strlen("\"base_url\":\"https://example.test/\""),"\"image\":{\"url\":\"https://example.test/store.bin\",\"bytes\":4096,\"sha256\":\""+std::string(64,'a')+"\"}");
 request.profile_json=imageProfile.data();request.profile_size=imageProfile.size();failure=2;
 assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_UNKNOWN);
 unsigned before=writes;failure=0;
 assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_UNKNOWN&&writes==before);
 assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_PENDING);
 for(auto byte:status.target_store_sha256)assert(byte==0xaa);
 assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_OK&&writes==before);
 assert(!memcmp(digest,status.request_sha256,32));
 puts("Native next-boot bridge: isolated NVS, explicit owner, source/receipt/target-image status, preserving-only input, stale/corrupt/pending fallback suppression, no format/erase, NVS-full preservation and commit-unknown reconciliation PASS");
}
