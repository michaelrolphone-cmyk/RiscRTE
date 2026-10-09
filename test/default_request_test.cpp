#include "bootstrap/Runtime.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
using namespace RiscBoot;
namespace {
std::string root,mode;
bool ownerOk=true,exitSafe=true;
unsigned visits[3]{},inits[3]{},finis[3]{},quiesces=0;
const risc_runtime_api_v1* cached=nullptr;
risc_runtime_capability_v1 abandoned{};
bool owner(){return ownerOk;}
bool health(risc_runtime_health_v1*){return true;}
bool logLine(const char*){return true;}
void delay(uint32_t){}
bool safe(){return exitSafe;}
bool retained(){return mode=="entry-retained" || mode=="fini-retained" || mode=="provider-retained" || mode=="explicit-retained";}
void save(const char* name,const JsonDocument& doc){std::string out;serializeJson(doc,out);std::ofstream(root+"/"+name)<<out;}
void setup(){
 std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
 JsonDocument boot;boot["board"]="board.json";boot["default_app"]="role-2.elf";
 if(mode=="default-caller")boot["default_app"]="role-0.elf";
 if(mode=="default-receiver")boot["default_app"]="role-1.elf";
 boot["provider_activation"]="demand";
 auto drivers=boot["drivers"].to<JsonArray>();
 if(mode=="provider-retained") {
  drivers.add<JsonObject>()["manifest"]="provider.json";
  std::ofstream(root+"/provider.json")<<R"({"type":"driver","id":"home-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"provider.elf","requires":[],"provides":[{"capability":"test.home","api":1}]})";
 }
 auto policies=boot["app_capabilities"].to<JsonArray>();
 for(unsigned role=0;role<3;++role){
  JsonDocument app;app["type"]="application";app["id"]=role==1?"viewer":role==0?"browser":"launcher";
  app["version"]="1.0.0";app["architecture"]="xtensa-esp32s3";app["file_name"]="role-"+std::to_string(role)+".elf";app["entry"]="app_main";
  auto requirements=app["requires"].to<JsonArray>();
  auto policy=policies.add<JsonObject>();std::string name="role-"+std::to_string(role)+".json";policy["manifest"]=name;
  auto grants=policy["grants"].to<JsonArray>();
  auto grant=[&](const char* cap){auto r=requirements.add<JsonObject>();r["capability"]=cap;r["api"]=1;auto g=grants.add<JsonObject>();g["capability"]=cap;g["api"]=1;g["instance_id"]=0;};
  if(role!=2)grant("file.open");
  if(role==1){app["supported_file_types"].to<JsonArray>().add(".txt");if(mode=="provider-retained")grant("test.home");}
  save(name.c_str(),app);
 }
 save("boot.json",boot);
}
}
extern "C" bool test_home_quiesce(){++quiesces;return false;}
extern "C" int test_file_init(unsigned role){
 ++inits[role];cached=risc_runtime_get_api(1);assert(cached && cached->struct_size>=RISC_RUNTIME_DEFAULT_REQUEST_V1_SIZE);
 assert(!cached->request_default()); // app_main only, including receiver init.
 if(abandoned.api){auto old=abandoned;assert(!cached->release(&old));abandoned={};}
 if(role==2 && inits[2]==2 && mode=="default-init-failure")return -1;
 return 0;
}
extern "C" void test_file_fini(unsigned role){
 ++finis[role];assert(cached && !cached->request_default());
 if(role==1 && mode=="fini-retained")exitSafe=false;
}
extern "C" void test_file_main(unsigned role,const risc_runtime_api_v1* api,char* source,char* id){
 ++visits[role];assert(visits[role]<4);
 ownerOk=false;assert(!api->request_default());ownerOk=true;
 exitSafe=false;assert(!api->request_default());exitSafe=true;
 if(role==2){if(visits[2]==1)assert(api->request_launch("role-0.elf"));return;}
 risc_runtime_capability_v1 grant{};grant.struct_size=sizeof(grant);
 assert(api->acquire("file.open",1,0,&grant));
 const auto* files=static_cast<const t5_file_open_api_v1*>(grant.api);
 char path[512]{};
 bool receiving=files->source_path_get(path,sizeof(path));
 if(role==0){
  if(visits[0]>1){assert(mode=="default-caller");assert(!files->open_take_result(nullptr,nullptr));assert(api->release(&grant));return;}
  if(mode=="ordinary"){
   assert(api->request_default());assert(!api->request_default());assert(!api->request_launch("role-1.elf"));
  }else if(mode=="queued-launch"){
   assert(api->request_launch("role-2.elf"));assert(!api->request_default());
  }else{
   assert(files->open_request(source,id,77));assert(!api->request_default());
  }
  assert(api->release(&grant));return;
 }
 if(!receiving){assert(mode=="default-receiver");assert(visits[1]==1 || visits[1]==3);assert(!files->open_take_result(nullptr,nullptr));assert(api->release(&grant));if(visits[1]==1)assert(api->request_launch("role-0.elf"));return;}
 assert(!strcmp(path,"/sd/Books/Original.TXT"));assert(finis[0]==1);
 assert(!api->request_launch("role-2.elf")); // Old request behavior remains closed.
 if(mode=="provider-retained"){
  risc_runtime_capability_v1 provider{};provider.struct_size=sizeof(provider);assert(api->acquire("test.home",1,0,&provider));
 }
 if(mode=="release-before-home")assert(api->release(&grant));
 if(mode=="default-load-failure")std::filesystem::remove(root+"/role-2.elf");
 assert(api->request_default());assert(!api->request_default());
 assert(!files->open_request(source,id,99));
 if(mode=="automatic-revoke")abandoned=grant;
 else if(mode!="release-before-home")assert(api->release(&grant));
 if(mode=="entry-retained")exitSafe=false;
 if(mode=="explicit-retained"){assert(api->retain_invocation());assert(!api->request_default());}
}
int main(int argc,char** argv){
 assert(argc==3);root=argv[1];mode=argv[2];setup();
 auto* runtime=new Runtime({owner,health,delay,logLine,nullptr,nullptr,safe});
 assert(!runtime->launchDefault());assert(runtime->prepare(root.c_str()));assert(!runtime->launchDefault());
 bool ok=runtime->run();assert(ok==!(retained() || mode=="default-load-failure" || mode=="default-init-failure"));
 assert(runtime->retained()==retained());assert(!risc_runtime_get_api(1));assert(cached && !cached->request_default());
 assert(visits[0]==(mode=="default-caller"?2u:1u));
 assert(visits[1]==(mode=="ordinary" || mode=="queued-launch"?0u:mode=="default-receiver"?3u:1u));
 assert(visits[2]==(mode=="default-caller" || mode=="default-receiver"?0u:retained() || mode=="default-load-failure" || mode=="default-init-failure"?1u:2u));
 if(mode=="entry-retained" || mode=="explicit-retained")assert(finis[1]==0);
 if(mode=="fini-retained" || mode=="provider-retained")assert(finis[1]==1);
 if(mode=="provider-retained")assert(quiesces==1);
 if(!retained())delete runtime; // Deliberately retained test images live to process exit.
 printf("Explicit default: %s PASS\n",mode.c_str());
}
