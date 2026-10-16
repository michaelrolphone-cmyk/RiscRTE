#include <new>
static bool failObjectAllocation=false;
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{
 if(failObjectAllocation)return nullptr;
 try{return ::operator new(n);}catch(...){return nullptr;}
}
void operator delete(void*p,const std::nothrow_t&)noexcept{::operator delete(p);}
#include "ports/esp32s3/NativeProvisionRequest.cpp"
#include <cassert>
#include <map>
#include <string>
#include <vector>
using namespace RiscProvision;
static std::map<std::string,std::vector<uint8_t>> stored;
static RequestSource source;static bool nativeReady=true,owned=true,missingNamespace=true;
static unsigned writes=0,commits=0,opens=0,closes=0,erases=0,payloadReads=0;static bool forbidPayload=false,metadataFail=false;static int failure=0;
static bool mockQuarantined=false,resourceAvailable=true,resourceHeld=false;
namespace RiscCpu {bool nativeRadioResourceTry(){if(!resourceAvailable||resourceHeld)return false;resourceHeld=true;return true;}void nativeRadioResourceEnd(){assert(resourceHeld);resourceHeld=false;}}
namespace RiscNvs {esp_err_t initializationStatus(){return mockQuarantined?ESP_FAIL:ESP_OK;}void quarantine(){mockQuarantined=true;}}
namespace RiscBankStore {bool provisionRequestSource(RequestSource&out,risc_bank_status_v1&status,bool){if(!nativeReady)return false;out=source;status={};strcpy(status.runtime_version,"0.2.23");strcpy(status.layout,"esp32s3-16mb-appdata");return true;}}
namespace RiscBoot {bool Runtime::registerPlatform(const char*,uint32_t,Scope,uint64_t,const void*){return true;}}
extern "C" esp_err_t nvs_open(const char*ns,nvs_open_mode_t mode,nvs_handle_t*out){assert(!strcmp(ns,"rte_provreq"));++opens;if(mode==NVS_READONLY&&missingNamespace)return ESP_ERR_NVS_NOT_FOUND;missingNamespace=false;*out=1;return ESP_OK;}
extern "C" esp_err_t nvs_get_blob(nvs_handle_t h,const char*k,void*out,size_t*n){assert(h==1&&!strcmp(k,"request"));assert(!forbidPayload);++payloadReads;auto at=stored.find(k);if(at==stored.end())return ESP_ERR_NVS_NOT_FOUND;if(!out){*n=at->second.size();return ESP_OK;}assert(*n>=at->second.size());memcpy(out,at->second.data(),at->second.size());*n=at->second.size();return ESP_OK;}
extern "C" esp_err_t nvs_set_blob(nvs_handle_t h,const char*k,const void*p,size_t n){assert(h==1&&!strcmp(k,"request"));assert(!stored.count(k));++writes;if(failure==1)return ESP_ERR_NVS_NOT_ENOUGH_SPACE;stored[k]={static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n};return ESP_OK;}
extern "C" esp_err_t nvs_get_u8(nvs_handle_t h,const char*k,uint8_t*out){assert(h==1&&!strcmp(k,"state"));auto at=stored.find(k);if(at==stored.end())return ESP_ERR_NVS_NOT_FOUND;if(at->second.size()!=1)return ESP_ERR_NVS_TYPE_MISMATCH;*out=at->second[0];return ESP_OK;}
extern "C" esp_err_t nvs_set_u8(nvs_handle_t h,const char*k,uint8_t value){assert(h==1&&!strcmp(k,"state"));++writes;if(failure==1)return ESP_ERR_NVS_NOT_ENOUGH_SPACE;stored[k]={value};return ESP_OK;}
extern "C" esp_err_t nvs_get_str(nvs_handle_t h,const char*k,char*out,size_t*n){assert(h==1&&!strcmp(k,"receipt"));auto at=stored.find(k);if(at==stored.end())return ESP_ERR_NVS_NOT_FOUND;if(!out){*n=at->second.size();return ESP_OK;}assert(*n>=at->second.size());memcpy(out,at->second.data(),at->second.size());*n=at->second.size();return ESP_OK;}
extern "C" esp_err_t nvs_set_str(nvs_handle_t h,const char*k,const char*p){assert(h==1&&!strcmp(k,"receipt")&&!stored.count(k));++writes;if(failure==1)return ESP_ERR_NVS_NOT_ENOUGH_SPACE;stored[k]={p,p+strlen(p)+1};return ESP_OK;}
extern "C" esp_err_t nvs_erase_key(nvs_handle_t h,const char*k){assert(h==1&&(!strcmp(k,"request")||!strcmp(k,"receipt")));++erases;return stored.erase(k)?ESP_OK:ESP_ERR_NVS_NOT_FOUND;}
static std::map<std::string,std::vector<uint8_t>>::iterator metadata;
#if RISC_NATIVE_ESP_IDF
static bool metadataInfoFail=false,metadataNextFail=false;
#endif
extern "C" esp_err_t nvs_get_used_entry_count(nvs_handle_t h,size_t*n){assert(h==1);*n=stored.size();return ESP_OK;}
static nvs_iterator_t findEntry(const char*part,const char*ns,nvs_type_t type){assert(!strcmp(part,"nvs")&&!strcmp(ns,"rte_provreq")&&type==NVS_TYPE_ANY);if(metadataFail)return nullptr;metadata=stored.begin();return metadata==stored.end()?nullptr:&metadata;}
static nvs_iterator_t nextEntry(nvs_iterator_t it){assert(it==&metadata);++metadata;return metadata==stored.end()?nullptr:&metadata;}
static void entryInfo(nvs_iterator_t it,nvs_entry_info_t*out){assert(it==&metadata);*out={};strcpy(out->namespace_name,"rte_provreq");strcpy(out->key,metadata->first.c_str());out->type=metadata->first=="state"?NVS_TYPE_U8:metadata->first=="receipt"?NVS_TYPE_STR:NVS_TYPE_BLOB;}
#if RISC_NATIVE_ESP_IDF
extern "C" esp_err_t nvs_entry_find(const char*p,const char*n,nvs_type_t t,nvs_iterator_t*out){*out=findEntry(p,n,t);return metadataFail?ESP_ERR_NO_MEM:*out?ESP_OK:ESP_ERR_NVS_NOT_FOUND;}
extern "C" esp_err_t nvs_entry_next(nvs_iterator_t*it){if(metadataNextFail)return ESP_ERR_NO_MEM;*it=nextEntry(*it);return *it?ESP_OK:ESP_ERR_NVS_NOT_FOUND;}
extern "C" esp_err_t nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t*out){if(metadataInfoFail)return ESP_ERR_INVALID_ARG;entryInfo(it,out);return ESP_OK;}
#else
extern "C" nvs_iterator_t nvs_entry_find(const char*p,const char*n,nvs_type_t t){return findEntry(p,n,t);}
extern "C" nvs_iterator_t nvs_entry_next(nvs_iterator_t it){return nextEntry(it);}
extern "C" void nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t*out){entryInfo(it,out);}
#endif
extern "C" void nvs_release_iterator(nvs_iterator_t it){assert(!it||it==&metadata);}
extern "C" esp_err_t nvs_commit(nvs_handle_t h){assert(h==1);++commits;return failure==2?ESP_FAIL:ESP_OK;}
extern "C" void nvs_close(nvs_handle_t h){assert(h==1);++closes;}
extern "C" esp_err_t nvs_flash_init(){assert(false);return ESP_FAIL;}
extern "C" esp_err_t nvs_flash_erase(){assert(false);return ESP_FAIL;}
int main(){
 std::memset(RiscProvisionRequest::bootViewStorage,0xa5,sizeof(RiscProvisionRequest::bootViewStorage));
 RiscProvisionRequest::releaseBootInput(); // An unopened workspace is never accessed.
 for(auto byte:RiscProvisionRequest::bootViewStorage)assert(byte==0xa5);
 RiscProvisionRequest::ownerTask=[](){return owned;};source.storeAbi=2;source.firmwareSize=4096;source.storeSha[0]=1;source.firmwareSha[0]=2;
 Input original{reinterpret_cast<void*>(1),nullptr};bool preserving=true;
 const auto unopened=opens;Descriptor allocationDescriptor;
 failObjectAllocation=true;auto input=RiscProvisionRequest::bootInput(original,preserving);
 assert(preserving&&!RiscProvisionRequest::bootBuffer&&loadDescriptor(input,allocationDescriptor)==InputStatus::Unavailable&&opens==unopened);
 RiscProvisionRequest::releaseBootInput();
 for(auto byte:RiscProvisionRequest::bootViewStorage)assert(byte==0xa5);
 failObjectAllocation=false;risc_test_psram_fail_after=0;
 input=RiscProvisionRequest::bootInput(original,preserving);
 assert(preserving&&RiscProvisionRequest::bootBuffer&&loadDescriptor(input,allocationDescriptor)==InputStatus::Unavailable&&opens==unopened);
 auto*retainedBuffer=RiscProvisionRequest::bootBuffer;
 input=RiscProvisionRequest::bootInput(original,preserving);
 assert(preserving&&RiscProvisionRequest::bootBuffer==retainedBuffer&&loadDescriptor(input,allocationDescriptor)==InputStatus::Unavailable&&opens==unopened);
 RiscProvisionRequest::releaseBootInput();RiscProvisionRequest::releaseBootInput();
 assert(!RiscProvisionRequest::bootBuffer&&!RiscProvisionRequest::bootView().profile&&!RiscProvisionRequest::bootView().time);
 risc_test_psram_fail_after=-1;
 input=RiscProvisionRequest::bootInput(original,preserving);assert(!preserving&&input.context==original.context);RiscProvisionRequest::releaseBootInput();
 std::string p=R"({"schema":"riscrte.provisioning","schema_version":2,"wifi":{"ssid":"test","password":"test-only"},"base_url":"https://example.test/","files":[)";for(const char*k:{"boot.json","board.json","default.elf"}){if(p.back()!='[')p+=',';p+="{\"path\":\""+std::string(k)+"\",\"bytes\":1,\"sha256\":\""+std::string(64,'0')+"\"}";}p+="]}";
 std::string t=R"({"schema":"riscrte.sntp","schema_version":1,"servers":["time.example.test"]})";
 risc_provision_request_v1 request{sizeof(request),p.data(),uint32_t(p.size()),t.data(),uint32_t(t.size()),{}};memcpy(request.expected_active_store_sha256,source.storeSha,32);uint8_t digest[32];
 owned=false;assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_UNAVAILABLE&&!writes);owned=true;
 resourceAvailable=false;auto beforeBusy=opens;assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_UNAVAILABLE&&opens==beforeBusy&&!writes);resourceAvailable=true;
 assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_OK&&writes==2);
 risc_provision_request_status_v1 status{};status.struct_size=sizeof(status);assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_PENDING&&!memcmp(status.request_sha256,digest,32));
 input=RiscProvisionRequest::bootInput(original,preserving);Descriptor descriptor;assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Ready);RiscProvisionRequest::releaseBootInput();
 uint8_t consumedProfile[32];memcpy(consumedProfile,status.profile_sha256,32);
 metadataFail=true;auto writesBeforeMetadataFailure=writes;
 assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_BLOCKED);
 input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();
 assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_UNAVAILABLE&&writes==writesBeforeMetadataFailure);metadataFail=false;
#if RISC_NATIVE_ESP_IDF
 for(bool* error:{&metadataInfoFail,&metadataNextFail}){
  *error=true;
  assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_BLOCKED);
  input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();
  assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_UNAVAILABLE&&writes==writesBeforeMetadataFailure);
  *error=false;
 }
#endif
 source.storeSha[0]=3;input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();
 source.receiptAvailable=true;memcpy(source.consumedProfileSha,consumedProfile,32);assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_CONSUMED);
 input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Ready);RiscProvisionRequest::releaseBootInput();
 nativeReady=false;input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();nativeReady=true;
 auto originalBlob=stored["request"];stored["request"][0]^=1;input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();
 assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_BLOCKED);stored["request"]=originalBlob;
 request.expected_active_store_sha256[0]=3;p[p.find("test-only")]='b';request.profile_json=p.data();failure=1;assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_UNKNOWN&&stored["request"]==originalBlob);
 failure=0;assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_UNKNOWN);
 assert(mockQuarantined);auto guardedOpens=opens;
 assert(RiscProvisionRequest::status(nullptr,&status)==RISC_PROVISION_REQUEST_UNKNOWN&&opens==guardedOpens);
 input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable&&opens==guardedOpens);RiscProvisionRequest::releaseBootInput();
 // Test-only process restart. Real-IDF tests separately reinitialize raw flash.
 RiscProvisionRequest::requestUnknown=false;mockQuarantined=false;
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
 assert(mockQuarantined);guardedOpens=opens;assert(RiscProvisionRequest::status(nullptr,&status)==RISC_PROVISION_REQUEST_UNKNOWN&&opens==guardedOpens);
 assert(RiscProvisionRequest::status(nullptr,&status)==RISC_PROVISION_REQUEST_UNKNOWN&&opens==guardedOpens);
 input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable&&opens==guardedOpens);RiscProvisionRequest::releaseBootInput();
 RiscProvisionRequest::requestUnknown=false;mockQuarantined=false; // ordinary-boot simulation only
 // The failed compaction commit can already have durably written its terminal
 // receipt. Reconciliation reports the old request as consumed and does not
 // expose either its removed credentials or the legacy bootstrap input.
 assert(stored["receipt"].size()==RiscProvisionRequestStorage::ControlChars&&stored["request"]==originalBlob);
 auto terminal=stored["receipt"];forbidPayload=true;
 assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_CONSUMED);
 for(auto byte:status.target_store_sha256)assert(byte==0);
 input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();
 auto confirmed=source;
 for(unsigned mode=0;mode<4;++mode){source=confirmed;
  if(mode==0)source.storeSha[31]^=1;else if(mode==1)source.receiptAvailable=false;
  else if(mode==2)source.consumedProfileSha[31]^=1;else source.firmwareSha[31]^=1;
  assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_BLOCKED);
  input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();
 }
 source=confirmed;nativeReady=false;
 assert(RiscProvisionRequest::status(nullptr,&status)==RISC_PROVISION_REQUEST_UNAVAILABLE);
 input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();nativeReady=true;
 assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_CONSUMED);
 forbidPayload=false;assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_OK&&writes==before+3);
 assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_PENDING);
 for(auto byte:status.target_store_sha256)assert(byte==0xaa);
 assert(!memcmp(digest,status.request_sha256,32));
 assert(RiscProvisionRequest::queue(nullptr,&request,digest)==RISC_PROVISION_REQUEST_OK&&writes==before+3);
 // A retained terminal masks even an unreadable staged payload at every
 // read-only entry point. Corrupt/unsupported terminal input also fails closed.
 auto pending=stored["request"];stored["receipt"]=terminal;stored["request"]={1,2,3};stored["state"]={RequestActive};forbidPayload=true;
 assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_CONSUMED);
 input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();
 stored["receipt"][0]='z';assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_BLOCKED);
 input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();
 stored["receipt"]=terminal;stored["receipt"].pop_back();assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_BLOCKED);
 input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();
 // A self-erased terminal cannot expose staged bytes while ACTIVE.
 stored.erase("receipt");assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_BLOCKED);
 input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();
 forbidPayload=false;stored["state"]={RequestReady};stored["request"]=pending;
 assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_PENDING);
 stored.erase("request");assert(RiscProvisionRequest::status(nullptr,&status)==0&&status.state==RISC_PROVISION_REQUEST_BLOCKED);
 input=RiscProvisionRequest::bootInput(original,preserving);assert(preserving&&loadDescriptor(input,descriptor)==InputStatus::Unavailable);RiscProvisionRequest::releaseBootInput();
 puts("Native next-boot bridge: isolated NVS, explicit owner, source/receipt/target-image status, preserving-only input, stale/corrupt/pending fallback suppression, no format/unrelated erase, terminal-first marker fencing, NVS-full preservation and same-boot UNKNOWN fence and ordinary-boot reconciliation PASS");
}
