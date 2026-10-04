#include "bootstrap/Runtime.h"
#include "bootstrap/KeyValueGeneration.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
static std::string root;static bool owned=true;static int phase=0;static risc_key_value_v1 saved{};
extern "C" void test_kv_owner(int v){owned=v;}
extern "C" int test_kv_phase(){return phase;}
extern "C" void test_kv_next(){++phase;}
extern "C" void test_kv_keep(risc_key_value_v1 v){saved=v;}
extern "C" void test_kv_revoked(){if(saved.get){uint32_t size=777;char data=42;assert(saved.get(saved.context,"mode",&data,1,&size)==RISC_KEY_VALUE_CONTEXT && !size && data==42);assert(saved.put(saved.context,"mode",&data,1)==RISC_KEY_VALUE_CONTEXT);}}
static bool owner(){return owned;}
static bool health(risc_runtime_health_v1*){return true;}
static bool log(const char*){return true;}
static void delay(uint32_t){}
static std::string keyPath(uint32_t id,const char* key){return root+"/kv-"+std::to_string(id)+"-"+key;}
static int32_t get(void*,uint32_t id,const char* key,void* data,uint32_t cap,uint32_t* size){
 assert(id==1 || id==2);*size=0;
 if(!strcmp(key,"partial")){memset(data,0,cap);*size=4;return RISC_KEY_VALUE_IO;}
 if(!strcmp(key,"oversize")){*size=65;return RISC_KEY_VALUE_OK;}
 if(!strcmp(key,"empty"))return RISC_KEY_VALUE_OK;
 if(!strcmp(key,"badstatus"))return 777;
 std::ifstream f(keyPath(id,key),std::ios::binary);if(!f)return RISC_KEY_VALUE_NOT_FOUND;
 std::string value{std::istreambuf_iterator<char>(f),{}};*size=value.size();if(*size>cap)return RISC_KEY_VALUE_BUFFER_SMALL;
 memcpy(data,value.data(),*size);return 0;
}
static int32_t put(void*,uint32_t id,const char* key,const void* data,uint32_t size){
 assert(id==1 || id==2);if(!strcmp(key,"fail"))return 77;
 std::ofstream f(keyPath(id,key),std::ios::binary);f.write(static_cast<const char*>(data),size);return f?0:RISC_KEY_VALUE_IO;
}
static const RiscBoot::KeyValueBackend backend{nullptr,get,put};
static void file(const char* name,const std::string& content){std::ofstream(root+"/"+name)<<content;}
static std::string manifest(const char* id,const char* elf,const char* req){return std::string("{\"type\":\"application\",\"id\":\"")+id+"\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"file_name\":\""+elf+"\",\"entry\":\"app_main\",\"requires\":"+req+"}";}
static std::string policy(const char* name,unsigned instance){return std::string("{\"manifest\":\"")+name+"\",\"grants\":[{\"capability\":\"storage.key-value\",\"api\":1,\"instance_id\":"+std::to_string(instance)+"}]}";}
static void boot(const std::string& policies){file("boot.json","{\"board\":\"board.json\",\"default_app\":\"default.elf\",\"drivers\":[],\"app_capabilities\":["+policies+"]}");}
static void child(const char* executable,const char* mode){pid_t pid=fork();assert(pid>=0);if(!pid){execl(executable,executable,root.c_str(),mode,static_cast<char*>(nullptr));_exit(99);}int status;assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0);}
int main(int argc,char** argv){
 uintptr_t gen=0;assert(RiscBoot::nextKeyValueContext(gen) && gen==1);
 gen=UINTPTR_MAX-1;assert(RiscBoot::nextKeyValueContext(gen) && gen==UINTPTR_MAX);assert(!RiscBoot::nextKeyValueContext(gen) && gen==UINTPTR_MAX);
 assert(argc==2 || argc==3);root=argv[1];
 if(argc==3){phase=std::string(argv[2])=="wake"?2:0;
  for(unsigned repeat=0;repeat<2;++repeat){RiscBoot::Runtime runtime({owner,health,delay,log,nullptr,&backend});assert(runtime.prepare(root.c_str()));assert(runtime.run());assert(phase==2);test_kv_revoked();}
  puts("Real runtime + dynamic app key-value grants/handoff/fresh boot PASS");return 0;}
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 const char* req=R"([{"capability":"storage.key-value","api":1}])";
 file("app.json",manifest("kv-default","default.elf",req));file("child.json",manifest("kv-child","child.elf",req));file("isolated.json",manifest("kv-isolated","isolated.elf",req));
 boot(policy("app.json",0));{RiscBoot::Runtime r({owner,health,delay,log,nullptr,&backend});assert(!r.prepare(root.c_str()));}
 boot(policy("app.json",1));{RiscBoot::Runtime r({owner,health,delay,log});assert(!r.prepare(root.c_str()));}
 file("app.json",manifest("kv-default","default.elf","[]"));{RiscBoot::Runtime r({owner,health,delay,log,nullptr,&backend});assert(!r.prepare(root.c_str()));}
 file("app.json",manifest("kv-default","default.elf",R"([{"capability":"storage.key-value","api":2}])"));{RiscBoot::Runtime r({owner,health,delay,log,nullptr,&backend});assert(!r.prepare(root.c_str()));}
 file("app.json",manifest("kv-default","default.elf",req));
 boot(policy("app.json",1)+","+policy("child.json",1)+","+policy("isolated.json",2));
 child(argv[0],"cold");child(argv[0],"wake");
 file("boot.json",R"({"board":"board.json","default_app":"nopolicy.elf","drivers":[]})");
 {RiscBoot::Runtime r({owner,health,delay,log,nullptr,&backend});assert(r.prepare(root.c_str()));assert(r.run());}
 puts("Capability policy denial, owner/live-generation enforcement, namespace isolation, bounds/faults and persistent fresh-process boot PASS");
}
