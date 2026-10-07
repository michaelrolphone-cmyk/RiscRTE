#include "ports/esp32s3/OwnerNvsInstaller.cpp"
#include "runtime/provisioning/Installer.h"
#include <map>
#include <string>
#include <vector>
#include <cassert>
#include <cstring>
#include <iostream>
static std::map<std::string,std::string> blobs;
static unsigned operations=0,cut=0,opened=0;static bool spaceExists=false;static bool after=false,allowed=true;static esp_err_t initialization=ESP_OK;
static bool fail(){return ++operations==cut;}
namespace RiscNvs {esp_err_t initializationStatus(){return initialization;}}
extern "C" esp_err_t nvs_open(const char* space,nvs_open_mode_t mode,nvs_handle_t* out){assert(!strcmp(space,"rte_bootstrap")&&mode==NVS_READWRITE);++opened;spaceExists=true;*out=1;return ESP_OK;}
extern "C" esp_err_t nvs_get_blob(nvs_handle_t,const char* key,void* out,size_t* size){if(fail())return ESP_FAIL;auto at=blobs.find(key);if(at==blobs.end())return ESP_ERR_NVS_NOT_FOUND;if(!out){*size=at->second.size();return ESP_OK;}assert(*size>=at->second.size());memcpy(out,at->second.data(),at->second.size());*size=at->second.size();return ESP_OK;}
extern "C" esp_err_t nvs_set_blob(nvs_handle_t,const char* key,const void* bytes,size_t size){bool fault=fail();if(!fault||after)blobs[key]=std::string(static_cast<const char*>(bytes),size);return fault?ESP_FAIL:ESP_OK;}
extern "C" esp_err_t nvs_commit(nvs_handle_t){return fail()?ESP_FAIL:ESP_OK;}
extern "C" void nvs_close(nvs_handle_t){}
extern "C" esp_err_t nvs_flash_init(){assert(false);return ESP_FAIL;}
extern "C" esp_err_t nvs_flash_erase(){assert(false);return ESP_FAIL;}
static std::string profile(const char* ssid){std::string s=std::string(R"({"schema":"riscrte.provisioning","schema_version":1,"wifi":{"ssid":")")+ssid+R"(","password":"dummy-password"},"files":[)";bool first=true;for(auto p:{"boot.json","board.json","default.elf"}){if(!first)s+=',';first=false;s+=std::string("{\"path\":\"")+p+"\",\"url\":\"https://example.invalid/"+p+"\",\"bytes\":1,\"sha256\":\""+std::string(64,'0')+"\"}";}return s+"]}";}
int main(){using namespace RiscProvision;const auto a=profile("dummy-a"),b=profile("dummy-b");const std::string time=R"({"schema":"riscrte.sntp","schema_version":1,"servers":["time.example.invalid"]})";
 auto install=[&](const std::string& p,const std::string& t){std::vector<unsigned char> scratch(ProfileInputBytes,0x88);auto result=RiscBootstrap::installOwnerNvs(p.data(),p.size(),t.data(),t.size(),scratch.data(),scratch.size(),[](){return allowed;});for(auto c:scratch)assert(!c);return result;};
 assert(RiscBootstrap::installOwnerNvs(a.data(),a.size(),time.data(),time.size(),nullptr,ProfileInputBytes,[](){return true;})==InstallResult::Unavailable);assert(!spaceExists&&!opened&&blobs.empty());
 assert(install("{",time)==InstallResult::InvalidInput);assert(!spaceExists&&!opened&&blobs.empty());
 assert(install(a,"{")==InstallResult::InvalidInput);assert(!spaceExists&&!opened&&blobs.empty());
 blobs["foreign-owner-key"]="preserve";assert(install(a,time)==InstallResult::Installed);const auto original=blobs;operations=0;assert(install(a,time)==InstallResult::Unchanged);assert(blobs==original);
 operations=0;assert(install(b,"")==InstallResult::Installed);unsigned maximum=operations;assert(blobs["foreign-owner-key"]=="preserve");
 for(unsigned failure=1;failure<=maximum+1;++failure)for(bool persisted:{false,true}){
   blobs=original;cut=failure;after=persisted;operations=0;auto result=install(b,"");cut=0;
   assert(blobs["foreign-owner-key"]=="preserve");
   const auto& d=blobs["descriptor"];bool selected=d.find("install_p1")!=std::string::npos;
   if(selected){assert(blobs["install_p1"]==b);assert(d.find("\"time_key\":\"\"")!=std::string::npos);}
   else {assert(d==original.at("descriptor")&&blobs["install_p0"]==a&&blobs["install_t0"]==time);}
   if(result==InstallResult::Installed)assert(selected);
 }
 blobs={{"profile",a},{"time",time},{"foreign-owner-key","preserve"},{"descriptor",R"({"schema":"riscrte.bootstrap","schema_version":1,"profile_key":"profile"})"}};
 assert(install(b,time)==InstallResult::Installed);assert(blobs["profile"]==a&&blobs["time"]==time&&blobs["foreign-owner-key"]=="preserve");
 blobs={{"install_p0","foreign"}};assert(install(b,time)==InstallResult::InvalidExisting);assert(blobs.size()==1);
 blobs={{"descriptor","torn"},{"foreign-owner-key","preserve"}};auto saved=blobs;assert(install(b,time)==InstallResult::InvalidExisting);assert(blobs==saved);
 blobs=original;assert(install("{",time)==InstallResult::InvalidInput);assert(blobs==original);
 assert(install(b,R"({"schema":"riscrte.sntp","schema_version":1,"servers":[],"utc":123})")==InstallResult::InvalidInput);assert(blobs==original);
 initialization=ESP_ERR_NVS_NO_FREE_PAGES;assert(install(b,time)==InstallResult::Unavailable);assert(blobs==original);initialization=ESP_OK;
 allowed=false;assert(install(b,time)==InstallResult::Unavailable);assert(blobs==original);
 std::cout<<"Owner NVS installer: native adapter, legacy input, fault cuts, selection uncertainty, collision refusal and unrelated-key preservation PASS\n";
}
