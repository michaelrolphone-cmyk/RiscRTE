#include "bootstrap/Runtime.h"
#include <cassert>
#include <fstream>
#include <string>
static unsigned metadata_calls=0,metadata_fail_at=0;
namespace RiscBoot {void* metadataTestAllocate(size_t n){return ++metadata_calls==metadata_fail_at?nullptr:std::malloc(n);}}
static bool fault_mode=false,close_fault=false;
extern "C" int __real_fclose(FILE*);
extern "C" int __wrap_fclose(FILE*f){int result=__real_fclose(f);if(close_fault){close_fault=false;return EOF;}return result;}
extern "C" int volume_test_fault_mode(){return fault_mode;}
extern "C" void volume_test_arm_close_fault(){close_fault=true;}
static bool owned=true;static int phase=0;static risc_storage_volume_api_v1 saved{};
extern "C" void volume_test_owner(int n){owned=n;}
extern "C" int volume_test_phase(){return phase;}
extern "C" void volume_test_next(){phase++;}
extern "C" void volume_test_keep(risc_storage_volume_api_v1 v){saved=v;}
extern "C" void volume_test_revoked(){if(saved.refresh)assert(!saved.refresh(saved.context));}
static bool owner(){return owned;}static bool health(risc_runtime_health_v1*){return true;}static void delay(uint32_t){}static bool log(const char*){return true;}
static void write(const std::string&p,const std::string&s){std::ofstream f(p);f<<s;assert(f.good());}
int main(int argc,char**argv){assert(argc==2);std::string root=argv[1];
 write(root+"/board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 write(root+"/profile.json","private provisioning input");
 write(root+"/app.json",R"({"type":"application","id":"files","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"storage.installed-files","api":1}]})");
 auto boot=[&](unsigned api,unsigned id){write(root+"/boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"storage.installed-files","api":)")+std::to_string(api)+",\"instance_id\":"+std::to_string(id)+"}]}]}");};
 boot(1,1);{RiscBoot::Runtime r({owner,health,delay,log});assert(!r.prepare(root.c_str()));assert(!strcmp(r.error(),"invalid installed-files authority"));}
 boot(2,0);{RiscBoot::Runtime r({owner,health,delay,log});assert(!r.prepare(root.c_str()));}
 boot(1,0);for(unsigned fail=1;fail<=3;fail++){metadata_calls=0;metadata_fail_at=fail;RiscBoot::Runtime r({owner,health,delay,log});assert(!r.prepare(root.c_str()));assert(!r.prepare(root.c_str()));assert(!r.run());}
 metadata_fail_at=0;
 boot(1,0);for(unsigned repeat=0;repeat<2;repeat++){phase=0;RiscBoot::Runtime r({owner,health,delay,log});assert(r.prepare(root.c_str()));assert(r.run());assert(phase==2);volume_test_revoked();}
 fault_mode=true;{RiscBoot::Runtime r({owner,health,delay,log});assert(r.prepare(root.c_str()));assert(!r.run() && r.retained());assert(strstr(r.error(),"retention barrier"));volume_test_revoked();}
 puts("Real Runtime + actual ELF: installed-files explicit authority, owner, no inherited grants, one active volume, copied-context revocation, fresh default reload and private-file exclusion PASS");}
