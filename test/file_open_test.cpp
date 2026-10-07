#include "bootstrap/Runtime.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
using namespace RiscBoot;
namespace {
Runtime* live=nullptr;
std::string root,mode,expectedSource;
bool ownerOk=true,exitSafe=true;
unsigned visits[3]{},initCalls[3]{},finiCalls[3]{};
const t5_file_open_api_v1* stale=nullptr;
risc_runtime_capability_v1 abandoned{};
bool launchFailure(){return mode=="load-failure" || mode=="invalid-elf" || mode=="missing-entry" || mode=="init-failure" || mode=="init-failure-grant";}
constexpr uint64_t cookie=UINT64_C(0xfedcba9876543210);
bool owner(){return ownerOk;}
bool health(risc_runtime_health_v1*){return true;}
bool logLine(const char*){return true;}
void delay(uint32_t){}
bool safe(){return exitSafe;}
void write(const std::string& name,const std::string& bytes){std::ofstream(root+"/"+name)<<bytes;}
void save(const std::string& name,const JsonDocument& doc){std::string bytes;serializeJson(doc,bytes);write(name,bytes);}
JsonDocument manifest(const char* id,const char* elf,bool capability) {
  JsonDocument doc;doc["type"]="application";doc["id"]=id;doc["version"]="1.0.0";
  doc["architecture"]="xtensa-esp32s3";doc["file_name"]=elf;doc["entry"]="app_main";
  auto req=doc["requires"].to<JsonArray>();
  if(capability){auto value=req.add<JsonObject>();value["capability"]="file.open";value["api"]=1;}
  return doc;
}
void provision() {
  write("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  auto browser=manifest("browser","role-0.elf",true);save("browser.json",browser);
  auto viewer=manifest("viewer","role-1.elf",true);viewer["display_name"]="Declared Viewer";viewer["icon"]="text";
  auto types=viewer["supported_file_types"].to<JsonArray>();types.add(".txt");types.add(".md");save("viewer.json",viewer);
  auto second=manifest("second","absent.elf",true);second["supported_file_types"].to<JsonArray>().add(".txt");save("second.json",second);
  std::filesystem::remove(root+"/absent.elf");
  if(mode=="invalid-elf")write("absent.elf","not a loadable module");
  if(mode=="missing-entry")std::filesystem::copy_file(root+"/no-entry.elf",root+"/absent.elf");
  auto launcher=manifest("launcher","role-2.elf",false);save("launcher.json",launcher);
  auto rogue=manifest("rogue","rogue.elf",true);rogue["supported_file_types"].to<JsonArray>().add(".txt");save("rogue.json",rogue);
  JsonDocument boot;boot["board"]="board.json";boot["default_app"]="role-2.elf";boot["drivers"].to<JsonArray>();
  if(mode=="default-caller")boot["default_app"]="role-0.elf";
  if(mode=="default-receiver")boot["default_app"]="role-1.elf";
  auto policies=boot["app_capabilities"].to<JsonArray>();
  for(const char* name:{"browser","viewer","second","launcher"}) {
    auto policy=policies.add<JsonObject>();policy["manifest"]=std::string(name)+".json";
    auto grants=policy["grants"].to<JsonArray>();
    if(strcmp(name,"launcher")){auto grant=grants.add<JsonObject>();grant["capability"]="file.open";grant["api"]=1;grant["instance_id"]=0;}
  }
  save("boot.json",boot);
}
void negativePaths(const t5_file_open_api_v1* api) {
  const char* invalid[]={nullptr,"","/sd","/sd/","sd/book.txt","/sdx/book.txt","/sd//book.txt","/sd/./book.txt","/sd/../book.txt",
    "/sd/a/../../book.txt","/sd/book.txt/","/sd/a\\book.txt","/bootfs/book.txt","/sd/book\n.txt","/sd/b\x7f.txt","/sd/\xc0\xaf.txt","/sd/book","/sd/book.","/sd/book.unknown","/sd/dir.txt/book","/sd/file.ELF","/sd/.txt","/sd/a.abcdefghijklmnop","/sd/a.\xc0\xaf","/sd/\xed\xa0\x80.txt","/sd/\xf4\x90\x80\x80.txt","/sd/\xe2\x82.txt","/sd/\x80.txt"};
  for(const char* value:invalid) {
    t5_file_handler_t result;memset(&result,0xab,sizeof(result));
    assert(api->handler_count(value)==0 && !api->handler_get(value,0,&result));
    t5_file_handler_t zero{};assert(!memcmp(&result,&zero,sizeof(zero)));
    assert(!api->open_request(value,"viewer",cookie));
  }
  std::string oversized="/sd/"+std::string(T5_FILE_OPEN_PATH_MAX-8,'x')+".txt";
  assert(oversized.size()==T5_FILE_OPEN_PATH_MAX && api->handler_count(oversized.c_str())==0);
  assert(!api->open_request(oversized.c_str(),"viewer",cookie));
  // Put the terminating boundary against an unreadable page: no accidental
  // strlen/strcmp beyond either canonical input bound can go unnoticed.
  const size_t page=static_cast<size_t>(sysconf(_SC_PAGESIZE));
  auto* memory=static_cast<char*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
  assert(memory!=MAP_FAILED && page>=T5_FILE_OPEN_PATH_MAX);
  assert(mprotect(memory+page,page,PROT_NONE)==0);
  char* bounded=memory+page-T5_FILE_OPEN_PATH_MAX;
  memset(bounded,'x',T5_FILE_OPEN_PATH_MAX);memcpy(bounded,"/sd/",4);
  t5_file_handler_t guarded{};
  assert(api->handler_count(bounded)==0 && !api->handler_get(bounded,0,&guarded));
  assert(!api->open_request(bounded,"viewer",cookie));
  memcpy(memory+page-5,".txt",5);
  assert(api->handler_count(bounded)==2); // Exactly 511 bytes plus NUL.
  char* boundedId=memory+page-T5_FILE_HANDLER_ID_MAX;
  memset(boundedId,'v',T5_FILE_HANDLER_ID_MAX);
  assert(!api->open_request("/sd/book.txt",boundedId,cookie));
  assert(munmap(memory,page*2)==0);
  assert(api->handler_count("/sd/Books/Original.TXT")==2);
  assert(api->handler_count("/sd/caf\xc3\xa9.Md")==1);
  assert(!api->handler_get("/sd/book.txt",UINT32_MAX,nullptr));
  t5_file_handler_t out;memset(&out,0xab,sizeof(out));
  assert(!api->handler_get("/sd/book.txt",UINT32_MAX,&out));
  t5_file_handler_t zero{};assert(!memcmp(&out,&zero,sizeof(out)));
  assert(!api->open_request("/sd/book.txt","rogue",cookie));
  assert(!api->open_request("/sd/book.txt","../viewer",cookie));
  assert(!api->open_request("/sd/book.txt","/sd/role-1.elf",cookie));
  assert(!api->open_request("/sd/book.txt",nullptr,cookie));
  assert(!api->open_request("/sd/book.txt",std::string(64,'a').c_str(),cookie));
  assert(!api->open_request("/sd/book.md","second",cookie));
}
}
extern "C" int test_file_init(unsigned role) {
  assert(role<3);++initCalls[role];
  if(abandoned.api) {
    auto old=abandoned;assert(!risc_runtime_get_api(1)->release(&old));abandoned={};
  }
  if(role!=2) {
    const auto* runtime=risc_runtime_get_api(1);assert(runtime);
    risc_runtime_capability_v1 grant{};grant.struct_size=sizeof(grant);
    assert(runtime->acquire("file.open",1,0,&grant));
    const auto* api=static_cast<const t5_file_open_api_v1*>(grant.api);
    assert(!api->open_request("/sd/book.txt","viewer",cookie)); // Only app_main may request.
    char source[512];
    if(role==1 && !(mode=="default-receiver" && (visits[1]==0 || visits[1]==2)))
      assert(api->source_path_get(source,sizeof(source)) && source==expectedSource);
    if(role==1 && mode=="init-failure-grant"){abandoned=grant;return -1;}
    assert(runtime->release(&grant));
  }
  if(role==1 && mode=="init-failure")return -1;
  return 0;
}
extern "C" void test_file_fini(unsigned role) {
  ++finiCalls[role];
  if(mode=="repeat" && role!=2) {assert(stale->refresh());return;} // Revoked after fini.
  // Other successful entries explicitly release their grants.
  if(stale)assert(!stale->refresh() && stale->handler_count("/sd/book.txt")==0);
}
extern "C" void test_file_main(unsigned role,const risc_runtime_api_v1* runtime,char* source,char* id) {
  assert(role<3);++visits[role];
  risc_runtime_capability_v1 grant{};grant.struct_size=sizeof(grant);
  if(mode=="repeat" && role!=2) {
    assert(runtime->acquire("file.open",1,0,&grant));
    const auto* api=static_cast<const t5_file_open_api_v1*>(grant.api);stale=api;
    risc_runtime_capability_v1 extra{};extra.struct_size=sizeof(extra);
    assert(runtime->acquire("file.open",1,0,&extra));
    assert(runtime->release(&grant) && api->refresh());
    abandoned=extra; // Exercise automatic cleanup, not just explicit release.
    if(role==1) {
      char path[T5_FILE_OPEN_PATH_MAX+1];memset(path,'Q',sizeof(path));
      const size_t n=expectedSource.size();
      assert(!api->source_path_get(path,n) && path[0]==0 && path[n]=='Q');
      assert(api->source_path_get(path,n+1) && path==expectedSource && path[n+1]=='Q');
      assert(!api->open_request(source,"viewer",cookie)); // Self/nested open.
      assert(!api->open_take_result(nullptr,nullptr));
      assert(finiCalls[0]==visits[0]);
      return;
    }
    if(visits[0]>1) {
      assert(!api->open_request(source,id,0)); // Pending result must be consumed.
      if(visits[0]==5)assert(api->open_take_result(nullptr,nullptr));
      else {
        int32_t error=99;uint64_t actual=0;
        assert(api->open_take_result(&error,&actual));
        assert(error==(visits[0]==3?-1:0) && actual==cookie+visits[0]-2);
      }
      assert(!api->open_take_result(nullptr,nullptr));
      if(visits[0]==5)return;
    }
    expectedSource=std::string("/sd/round-")+std::to_string(visits[0])+".txt";
    strcpy(source,expectedSource.c_str());
    assert(api->open_request(source,visits[0]==2?"second":"viewer",cookie+visits[0]-1));
    memset(source,'X',strlen(source));
    assert(!api->open_request("/sd/other.txt","viewer",0));
    return;
  }
  if(role==2) {
    assert(!runtime->acquire("file.open",1,0,&grant));
    if(stale){char path[512];assert(!stale->source_path_get(path,sizeof(path)));assert(!stale->open_take_result(nullptr,nullptr));}
    if(visits[2]==1)assert(runtime->request_launch("role-0.elf"));
    return;
  }
  assert(!runtime->acquire("file.open",2,0,&grant));
  assert(!runtime->acquire("file.open",1,1,&grant));
  assert(runtime->acquire("file.open",1,0,&grant));
  const auto* api=static_cast<const t5_file_open_api_v1*>(grant.api);stale=api;
  assert(api->api_version==1 && api->struct_size==sizeof(*api));
  char path[512];
  if(role==1) {
    if(mode=="default-receiver" && !api->source_path_get(path,sizeof(path))) {
      assert(visits[1]==1 || visits[1]==3);assert(runtime->release(&grant));
      if(visits[1]==1)assert(runtime->request_launch("role-0.elf"));
      return;
    }
    assert(finiCalls[0]==1); // Caller finalized before receiver initialization.
    assert(!api->source_path_get(nullptr,512));
    assert(!api->source_path_get(path,0));
    assert(!api->source_path_get(path,expectedSource.size()));
    assert(api->source_path_get(path,sizeof(path)) && path==expectedSource);
    ownerOk=false;assert(!api->source_path_get(path,sizeof(path)));ownerOk=true;
    assert(api->source_path_get(path,sizeof(path)) && path==expectedSource);
    assert(!api->open_take_result(nullptr,nullptr));
    assert(!api->open_request("/sd/book.txt","second",cookie));
    assert(!runtime->request_launch("role-2.elf"));
    if(mode=="caller-load-failure")std::filesystem::remove(root+"/role-0.elf");
    assert(runtime->release(&grant));
    if(mode=="retained-receiver")exitSafe=false;
    return;
  }
  assert(!api->source_path_get(path,sizeof(path)) && !path[0]);
  if(visits[0]==2) {
    assert(launchFailure() || finiCalls[1]==(mode=="default-receiver"?2u:1u));
    if(mode!="discard-result") {
      int32_t error=99;uint64_t actual=0;
      ownerOk=false;assert(!api->open_take_result(&error,&actual) && error==99 && actual==0);ownerOk=true;
      assert(api->open_take_result(&error,&actual) && actual==cookie);
      assert(error==(launchFailure()?-1:0));
      assert(!api->open_take_result(&error,&actual));
    }
    assert(runtime->release(&grant));return;
  }
  assert(!api->open_take_result(nullptr,nullptr));
  assert(api->refresh());negativePaths(api);
  {
    auto candidate=manifest("browser","role-0.elf",true);candidate["version"]="1.0.1";
    candidate["supported_file_types"].to<JsonArray>().add(".txt");
    Runtime::UpdateApp update;std::string bytes;serializeJson(candidate,bytes);
    assert(live->appUpdate("browser",bytes.data(),bytes.size(),update));
    candidate["supported_file_types"][0]=".TXT";bytes.clear();serializeJson(candidate,bytes);
    assert(!live->appUpdate("browser",bytes.data(),bytes.size(),update));
    assert(api->handler_count(source)==2); // Candidate metadata never mutates the live snapshot.
  }
  t5_file_handler_t handler{};
  assert(api->handler_get(source,0,&handler) && handler.kind==0 && !strcmp(handler.app_id,"viewer"));
  assert(!strcmp(handler.display_name,"Declared Viewer") && !strcmp(handler.icon,"text"));
  assert(api->handler_get(source,1,&handler) && !strcmp(handler.app_id,"second") && !strcmp(handler.display_name,"second") && !handler.icon[0]);
  assert(!api->handler_get(source,2,&handler));
  // Metadata remains the prepared immutable copy after all source JSON changed.
  write("viewer.json","invalid");assert(api->refresh() && api->handler_count(source)==2);
  ownerOk=false;assert(!api->refresh() && !api->open_request(source,id,cookie));ownerOk=true;
  exitSafe=false;assert(!api->open_request(source,id,cookie));exitSafe=true;
  const auto copiedGrant=grant;
  assert(runtime->release(&grant));
  assert(!api->refresh() && !api->open_request(source,id,cookie));
  assert(runtime->acquire("file.open",1,0,&grant));
  auto old=copiedGrant;assert(!runtime->release(&old));
  if(mode=="queued-launch") {
    assert(runtime->request_launch("role-2.elf"));
    assert(!api->open_request(source,id,cookie));
    assert(runtime->release(&grant));return;
  }
  if(mode=="load-failure" || mode=="invalid-elf" || mode=="missing-entry")strcpy(id,"second");
  if(mode=="long-path")strcpy(source,("/sd/"+std::string(T5_FILE_OPEN_PATH_MAX-9,'x')+".txt").c_str());
  expectedSource=source;
  assert(api->open_request(source,id,cookie));
  memset(source,'X',strlen(source));memset(id,'X',strlen(id)); // No loaded pointers retained.
  assert(!api->open_request("/sd/book.txt","viewer",1));
  assert(!runtime->request_launch("role-2.elf"));
  assert(!runtime->request_launch("/sd/role-2.elf"));
  assert(runtime->release(&grant));
  assert(!api->refresh());
  if(mode=="retained-caller")exitSafe=false;
}
int main(int argc,char** argv) {
  assert(argc==2);root=argv[1];
  std::filesystem::copy_file(root+"/role-0.elf",root+"/browser-backup.elf");
  for(const char* scenario:{"success","load-failure","invalid-elf","missing-entry","init-failure","init-failure-grant","repeat","long-path","discard-result","default-caller","default-receiver","caller-load-failure","queued-launch"}) {
    mode=scenario;provision();memset(visits,0,sizeof(visits));memset(initCalls,0,sizeof(initCalls));memset(finiCalls,0,sizeof(finiCalls));
    Runtime runtime({owner,health,delay,logLine,nullptr,nullptr,safe});live=&runtime;
    assert(runtime.prepare(root.c_str()));
    std::vector<std::string> churn(128,std::string(4096,'Z'));
    assert(runtime.run());
    if(mode=="repeat") {
      assert(visits[0]==5 && visits[1]==3 && visits[2]==2);
      assert(initCalls[0]==5 && finiCalls[0]==5 && initCalls[1]==3 && finiCalls[1]==3);
      assert(!stale->refresh() && !stale->open_take_result(nullptr,nullptr));continue;
    }
    assert(visits[0]==((mode=="caller-load-failure" || mode=="queued-launch")?1u:2u));
    assert(visits[2]==((mode=="default-caller" || mode=="default-receiver")?0u:2u));
    assert(visits[1]==(mode=="default-receiver"?3u:unsigned(!launchFailure() && mode!="queued-launch")));
    if(mode=="caller-load-failure")std::filesystem::copy_file(root+"/browser-backup.elf",root+"/role-0.elf");
    assert(!stale->refresh() && !stale->open_take_result(nullptr,nullptr));
  }
  puts("File-open: declared immutable handlers, strict SD paths, explicit grants, copied handoff, fresh child-caller-default, repeated success/failure and automatic grant cleanup PASS");
  // Malformed association metadata always rejects before any ELF executes.
  for(const char* bad:{"null","{}","[1]","[\"txt\"]","[\".TXT\"]","[\".\"]","[\".a-b\"]","[\".txt\",\".txt\"]","[\".abcdefghijklmno\"]"}) {
    provision();JsonDocument viewer;assert(readJson((root+"/viewer.json").c_str(),viewer));
    JsonDocument invalid;assert(!deserializeJson(invalid,bad));viewer["supported_file_types"].set(invalid.as<JsonVariantConst>());save("viewer.json",viewer);
    Runtime runtime({owner,health,delay,logLine});assert(!runtime.prepare(root.c_str()));assert(!strcmp(runtime.error(),"invalid app file associations"));
  }
  for(const char* field:{"id","display_name","icon","supported_file_types"}) {
    provision();JsonDocument viewer;assert(readJson((root+"/viewer.json").c_str(),viewer));
    if(!strcmp(field,"supported_file_types")){auto types=viewer[field].to<JsonArray>();for(unsigned n=0;n<13;++n)types.add(".t"+std::to_string(n));}
    else viewer[field]=std::string(!strcmp(field,"id")?64:!strcmp(field,"icon")?24:96,'a');
    save("viewer.json",viewer);Runtime runtime({owner,health,delay,logLine});assert(!runtime.prepare(root.c_str()));
  }
  for(const char* field:{"api","instance_id"}) {
    provision();JsonDocument boot;assert(readJson((root+"/boot.json").c_str(),boot));
    boot["app_capabilities"][0]["grants"][0][field]=!strcmp(field,"api")?2:1;save("boot.json",boot);
    if(!strcmp(field,"api")){JsonDocument browser;assert(readJson((root+"/browser.json").c_str(),browser));browser["requires"][0]["api"]=2;save("browser.json",browser);}
    Runtime runtime({owner,health,delay,logLine});assert(!runtime.prepare(root.c_str()));assert(!strcmp(runtime.error(),"invalid file-open authority"));
  }
  puts("File-open: malformed/oversized/duplicate metadata and wrong global version/instance fail closed PASS");
  // Retention intentionally keeps the pinned test module until process exit.
  for(const char* scenario:{"retained-caller","retained-receiver"}) {
    mode=scenario;provision();memset(visits,0,sizeof(visits));memset(initCalls,0,sizeof(initCalls));memset(finiCalls,0,sizeof(finiCalls));exitSafe=true;
    Runtime runtime({owner,health,delay,logLine,nullptr,nullptr,safe});live=&runtime;
    assert(runtime.prepare(root.c_str()));assert(!runtime.run() && runtime.retained());
    assert(visits[0]==1 && visits[2]==1 && visits[1]==unsigned(mode=="retained-receiver"));
    assert(finiCalls[0]==unsigned(mode=="retained-receiver") && finiCalls[1]==0);
    assert(!stale->refresh() && !stale->open_take_result(nullptr,nullptr));
  }
  puts("File-open: failed caller/receiver retention blocks all relaunch and discards handoff authority PASS");
}
