#include "runtime/provisioning/BootstrapInput.h"
#include "ports/esp32s3/NvsBootstrapInput.h"
#include <openssl/sha.h>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cassert>
#include <cstring>
using namespace RiscProvision;
static std::map<std::string,std::string> blobs;
static esp_err_t initStatus=ESP_OK,openError=ESP_OK,getError=ESP_OK;
static bool changeSize=false;static unsigned opened=0,closed=0,gets=0,hashes=0;
namespace RiscNvs {esp_err_t initializationStatus(){return initStatus;}}
extern "C" esp_err_t nvs_open(const char* space,nvs_open_mode_t mode,nvs_handle_t* handle){assert(!strcmp(space,"rte_bootstrap")&&mode==NVS_READONLY);++opened;if(openError)return openError;*handle=1;return ESP_OK;}
extern "C" esp_err_t nvs_get_blob(nvs_handle_t handle,const char* key,void* out,size_t* size){assert(handle==1);++gets;if(getError)return getError;auto at=blobs.find(key);if(at==blobs.end())return ESP_ERR_NVS_NOT_FOUND;
 if(!out){*size=at->second.size();return ESP_OK;}assert(*size>=at->second.size());memcpy(out,at->second.data(),at->second.size());*size=at->second.size()-(changeSize?1:0);return ESP_OK;}
extern "C" void nvs_close(nvs_handle_t handle){assert(handle==1);++closed;}
extern "C" esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void*,size_t){assert(false);return ESP_FAIL;}
extern "C" esp_err_t nvs_commit(nvs_handle_t){assert(false);return ESP_FAIL;}
extern "C" esp_err_t nvs_flash_init(){assert(false);return ESP_FAIL;}
extern "C" esp_err_t nvs_flash_erase(){assert(false);return ESP_FAIL;}
static bool hash(const void* bytes,uint32_t size,uint8_t* digest){++hashes;SHA256(static_cast<const uint8_t*>(bytes),size,digest);return true;}
int main(){
 const std::string descriptor=R"({"schema":"riscrte.bootstrap","schema_version":1,"profile_key":"profile"})";
 std::string profile=R"({"schema":"riscrte.provisioning","schema_version":1,"wifi":{"ssid":"test-network","password":"test-only-password"},"files":[)";
 for(const char* name:{"boot.json","board.json","default.elf"}){if(profile.back()!='[')profile+=',';profile+="{\"path\":\""+std::string(name)+"\",\"url\":\"https://example.test/"+name+"\",\"bytes\":1,\"sha256\":\""+std::string(64,'0')+"\"}";}profile+="]}";
 auto p=std::make_unique<Profile>();std::vector<uint8_t> scratch(ProfileInputBytes);uint8_t digest[32]{};
 auto check=[&](InputStatus expected){std::fill(scratch.begin(),scratch.end(),0xab);auto result=loadProfile(RiscBootstrap::nvsInput(),scratch.data(),scratch.size(),*p,digest,hash);assert(result==expected);for(auto b:scratch)assert(b==0);if(expected!=InputStatus::Ready)assert(!p->count&&!p->password[0]);};
 check(InputStatus::Missing);blobs["descriptor"]=descriptor;check(InputStatus::Missing);blobs["profile"]=profile;check(InputStatus::Ready);
 assert(p->count==3&&!strcmp(p->ssid,"test-network")&&hashes==1);uint8_t expected[32];SHA256(reinterpret_cast<const uint8_t*>(profile.data()),profile.size(),expected);assert(!memcmp(expected,digest,32));
 for(auto bad:{std::string("{}"),descriptor.substr(0,descriptor.size()-1)+",\"utc_seconds\":123}",std::string(DescriptorBytes+1,'x')}){blobs["descriptor"]=bad;check(InputStatus::Invalid);}blobs["descriptor"]=descriptor;
 blobs["profile"]="{";check(InputStatus::Invalid);blobs["profile"]=std::string(ProfileInputBytes+1,'x');check(InputStatus::Invalid);blobs["profile"]=profile;
 changeSize=true;check(InputStatus::Invalid);changeSize=false;getError=ESP_ERR_NVS_TYPE_MISMATCH;check(InputStatus::Invalid);getError=ESP_OK;
 initStatus=ESP_ERR_NVS_NO_FREE_PAGES;unsigned before=opened;check(InputStatus::Unavailable);assert(opened==before);initStatus=ESP_OK;
 openError=ESP_ERR_NVS_NOT_FOUND;check(InputStatus::Missing);openError=ESP_FAIL;check(InputStatus::Unavailable);openError=ESP_OK;
 assert(closed+2==opened);puts("Owner NVS descriptor/profile input: read-only namespace, bounds/types, torn-size refusal, SHA identity, scratch wiping and no erase recovery PASS");
}
