#include "runtime/storage/AppDataExport.h"
#include "runtime/storage/AppDataFiles.h"
#include <ArduinoJson.h>
#include EXPORT_TIMECARD_BRIDGE
#include EXPORT_TIMECARD_VALIDATION
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <unistd.h>
using RiscStorage::AppDataExport;using RiscStorage::AppDataFiles;
extern "C" {
size_t export_record_fixture(unsigned,void*,size_t);
bool export_browser_copy(const risc_storage_volume_api_v1*,const char*,const char*);
void export_browser_set_yield(void(*)(void));
bool export_browser_safe();const char*export_browser_status();
unsigned export_browser_list(const risc_storage_volume_api_v1*,const char*,char[6][128]);
unsigned export_browser_preview(const risc_storage_volume_api_v1*,const char*,void*,unsigned);
ssize_t __real_write(int,const void*,size_t);ssize_t __real_read(int,void*,size_t);
int __real_rename(const char*,const char*);int __real_close(int);int __real_fsync(int);int __real_unlink(const char*);
}
static int fault=0;static unsigned replacements=0,stats=0,reads=0,allocations=0;static bool owned=true,safe=true,allocateFail=false,afterStatUnsafe=false;
static AppDataFiles* liveFiles=nullptr;
static void raceWriter(){assert(liveFiles->replace(1,"timecard-copy.json",0,"winning owner",13)==0);}
extern "C" ssize_t __wrap_write(int fd,const void*b,size_t n){if(fault==1){fault=0;errno=ENOSPC;return -1;}return __real_write(fd,b,n);}
extern "C" ssize_t __wrap_read(int fd,void*b,size_t n){if(fault==6){fault=0;errno=EIO;return -1;}return __real_read(fd,b,n);}
extern "C" int __wrap_rename(const char*a,const char*b){if(fault==2){fault=0;errno=EIO;return -1;}int r=__real_rename(a,b);if(fault==3){fault=0;errno=EIO;return -1;}return r;}
extern "C" int __wrap_close(int fd){int r=__real_close(fd);if(fault==4){fault=0;errno=EIO;return -1;}return r;}
extern "C" int __wrap_fsync(int fd){if(fault==5){fault=0;errno=EIO;return -1;}return __real_fsync(fd);}
extern "C" int __wrap_unlink(const char*p){if(fault==7){fault=0;errno=EIO;return -1;}return __real_unlink(p);}
static void* allocate(size_t n){if(allocateFail)return nullptr;void*p=malloc(n);if(p)++allocations;return p;}
static void deallocate(void*p){if(p)--allocations;free(p);}
static const AppDataFiles::Hooks io{nullptr,[](void*){return 0u;},[](void*){return true;},malloc,free};
static const AppDataExport::Hooks hooks{nullptr,[](void*){return owned;},[](void*){return safe;},allocate,deallocate};
static RiscBoot::AppDataBackend backend(AppDataFiles&f){return {&f,
 [](void*c,uint32_t ns,const char*n,uint32_t*z,uint64_t*r){++stats;int32_t result=static_cast<AppDataFiles*>(c)->stat(ns,n,z,r);if(afterStatUnsafe){afterStatUnsafe=false;safe=false;}return result;},
 [](void*c,uint32_t ns,const char*n,uint64_t r,void*b,uint32_t cap,uint32_t*z,uint64_t*v){++reads;return static_cast<AppDataFiles*>(c)->read(ns,n,r,b,cap,z,v);},
 [](void*c,uint32_t ns,const char*n,uint64_t r,const void*b,uint32_t z){++replacements;return static_cast<AppDataFiles*>(c)->replace(ns,n,r,b,z);},
 [](void*c){return static_cast<AppDataFiles*>(c)->exitSafe();}};}
static risc_app_data_v1 ownerApi(AppDataFiles&f){return {1,sizeof(risc_app_data_v1),&f,
 [](void*c,const char*n,uint32_t*z,uint64_t*r){return static_cast<AppDataFiles*>(c)->stat(1,n,z,r);},
 [](void*c,const char*n,uint64_t r,void*b,uint32_t cap,uint32_t*z,uint64_t*v){return static_cast<AppDataFiles*>(c)->read(1,n,r,b,cap,z,v);},
 [](void*c,const char*n,uint64_t r,const void*b,uint32_t z){return static_cast<AppDataFiles*>(c)->replace(1,n,r,b,z);}};}
static std::string bytes(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static auto inventory(const std::string&root){std::map<std::string,std::string>r;for(auto&e:std::filesystem::recursive_directory_iterator(root))if(e.is_regular_file())r[e.path().lexically_relative(root).string()]=bytes(e.path());return r;}
static std::string history(){std::string s="{\"days\":[";for(unsigned i=0;i<400;++i){char row[100];unsigned date=(2025+i/336)*10000+(i%336/28+1)*100+i%28+1;snprintf(row,sizeof(row),"%s{\"d\":%u,\"in\":480,\"out\":1020}",i?",":"",date);s+=row;}s+="]}";s.resize(TCP_APPDATA_MAX,' ');assert(tcp_validate_json(s.data(),s.size()));return s;}
static std::vector<AppDataExport::Entry> entries(const char*path){JsonDocument doc;std::ifstream f(path);assert(!deserializeJson(doc,f));std::vector<AppDataExport::Entry> v;
 for(auto row:doc["files"].as<JsonArrayConst>()){AppDataExport::Entry e{};strcpy(e.owner,row["owner"]);e.nameSpace=row["namespace"];strcpy(e.name,row["name"]);strcpy(e.path,row["path"]);e.writable=!strcmp(row["access"],"read-write");v.push_back(e);}return v;}
static std::string readExport(const risc_app_data_export_v1&a,const char*path){void*c=a.volume.terminal.power.volume.base.context;uint32_t z=0,n=0;uint64_t r=0,v=0;assert(a.stat_revision(c,path,&z,&r)==0);std::string out(z,'!');assert(a.read_revision(c,path,r,out.data(),z,&n,&v)==0&&n==z&&r==v);return out;}
int main(int argc,char**argv){assert(argc==4);const std::string root=argv[1],mode=argv[3];auto map=entries(argv[2]);
 std::filesystem::create_directory(root+"/appdata");std::filesystem::create_directory(root+"/bootfs");{std::ofstream f(root+"/bootfs/app.elf");f<<"installed sentinel";}{std::ofstream f(root+"/nvs-sentinel");f<<"private settings sentinel";}
 AppDataFiles files(io);if(mode!="missing_mount")assert(files.configure((root+"/appdata").c_str()));auto b=backend(files);AppDataExport volume(hooks);risc_app_data_export_v1 a{};
 assert(AppDataExport::validEntries(map.data(),map.size()));assert(volume.configure(&b,map.data(),map.size(),"Watch saved files"));
 if(mode=="missing_mount"){memset(&a,0x51,sizeof(a));auto before=a;assert(!volume.begin(&a)&&!memcmp(&a,&before,sizeof(a))&&!volume.retained());assert(files.configure((root+"/appdata").c_str()));assert(volume.begin(&a));assert(volume.end());puts("app-data export missing mount, absent file and retry PASS");return 0;}
 auto owner=ownerApi(files);tcp_appdata ownerState{};assert(tcp_appdata_bind(&ownerState,&owner));assert(!tcp_appdata_exists(&ownerState,TCP_APPDATA_PATH));const std::string original=history();assert(tcp_appdata_write(&ownerState,TCP_APPDATA_PATH,original.data(),original.size()));
 std::vector<unsigned char> record(65536);size_t count=export_record_fixture(0,record.data(),record.size());assert(files.replace(2,"spectrum-events-a.sqt",0,record.data(),count)==0);const std::string audio(reinterpret_cast<char*>(record.data()),count);
 count=export_record_fixture(2,record.data(),record.size());assert(files.replace(3,"rf-events-a.rft",0,record.data(),count)==0);
 const auto before=inventory(root);unsigned mutations=replacements;assert(volume.begin(&a));auto&v=a.volume.terminal.power.volume.base;void*c=v.context;
 assert(!v.remove&&!a.volume.terminal.power.volume.rename&&!a.volume.terminal.power.volume.mkdir);
 assert(risc_app_data_export(&v)==&a&&inventory(root)==before&&replacements==mutations);
 if(mode=="normal"){
  char names[6][128]{};assert(export_browser_list(&v,"/",names)==4);assert(!strcmp(names[0],"audio_spectrum"));assert(export_browser_list(&v,"/timecard",names)==1&&!strcmp(names[0],"timecard.json"));
  char preview[256];unsigned n=export_browser_preview(&v,"/timecard/timecard.json",preview,sizeof(preview));assert(n&&!memcmp(preview,original.data(),n));assert(readExport(a,"/audio_spectrum/spectrum-events-a.sqt")==audio);assert(inventory(root)==before);
  assert(export_browser_copy(&v,"/timecard/timecard.json","/timecard/timecard-copy.json")&&export_browser_safe());assert(bytes(root+"/appdata/n00000001/timecard-copy.json")==original);
  auto after=inventory(root);assert(after.size()==before.size()+1);for(auto&kv:before)assert(after[kv.first]==kv.second);
  mutations=replacements;assert(!export_browser_copy(&v,"/timecard/timecard.json","/timecard/timecard-copy.json")&&export_browser_safe()&&replacements==mutations);
 }else if(mode=="stale"){
  uint32_t z=0;uint64_t rev=0;assert(a.stat_revision(c,"/timecard/timecard.json",&z,&rev)==0);
  assert(tcp_appdata_exists(&ownerState,TCP_APPDATA_PATH));std::string edited="{\"days\":[{\"d\":20261005,\"in\":555}]}";assert(tcp_appdata_write(&ownerState,TCP_APPDATA_PATH,edited.data(),edited.size()));
  assert(a.replace_revision(c,"/timecard/timecard.json",rev,original.data(),original.size())==RISC_APP_DATA_STALE);assert(readExport(a,"/timecard/timecard.json")==edited);
  assert(a.stat_revision(c,"/timecard/timecard.json",&z,&rev)==0);assert(a.replace_revision(c,"/timecard/timecard.json",rev,original.data(),original.size())==0);
  assert(!tcp_appdata_write(&ownerState,TCP_APPDATA_PATH,edited.data(),edited.size())&&ownerState.status==RISC_APP_DATA_STALE);
 }else if(mode=="unknown_before"||mode=="unknown_after"){
  uint32_t z=0;uint64_t rev=0;assert(a.stat_revision(c,"/timecard/timecard.json",&z,&rev)==0);const char next[]="{\"days\":[]}";fault=mode=="unknown_before"?2:3;
  assert(a.replace_revision(c,"/timecard/timecard.json",rev,next,sizeof(next)-1)==RISC_APP_DATA_COMMIT_UNKNOWN);
  assert(a.replace_revision(c,"/timecard/timecard.json",rev,original.data(),original.size())==RISC_APP_DATA_STALE);
  assert(readExport(a,"/timecard/timecard.json")== (mode=="unknown_before"?original:std::string(next)));
  assert(a.stat_revision(c,"/timecard/timecard.json",&z,&rev)==0);assert(a.replace_revision(c,"/timecard/timecard.json",rev,original.data(),original.size())==0);
 }else if(mode=="copy_full"||mode=="copy_unknown_before"||mode=="copy_unknown_after"||mode=="copy_sync"){
  fault=mode=="copy_full"?1:mode=="copy_unknown_before"?2:mode=="copy_unknown_after"?3:5;
  mutations=replacements;assert(!export_browser_copy(&v,"/timecard/timecard.json","/timecard/timecard-copy.json"));assert(export_browser_safe()&&!volume.retained()&&replacements==mutations+1);
  if(mode=="copy_unknown_after"){assert(strstr(export_browser_status(),"unknown"));assert(readExport(a,"/timecard/timecard-copy.json")==original);assert(!export_browser_copy(&v,"/timecard/timecard.json","/timecard/timecard-copy.json")&&replacements==mutations+1);}
  else{assert(!std::filesystem::exists(root+"/appdata/n00000001/timecard-copy.json"));assert(export_browser_copy(&v,"/timecard/timecard.json","/timecard/timecard-copy.json"));}
 }else if(mode=="quota"){
  std::string large(65536,'q');assert(files.replace(1,"quota.bin",0,large.data(),large.size())==0);
  assert(!export_browser_copy(&v,"/timecard/timecard.json","/timecard/timecard-copy.json")&&export_browser_safe());
  uint32_t z;uint64_t r;assert(files.stat(1,"quota.bin",&z,&r)==0&&files.replace(1,"quota.bin",r,nullptr,0)==0);
  assert(export_browser_copy(&v,"/timecard/timecard.json","/timecard/timecard-copy.json")&&export_browser_safe());
 }else if(mode=="race"){
  liveFiles=&files;export_browser_set_yield(raceWriter);mutations=replacements;
  assert(!export_browser_copy(&v,"/timecard/timecard.json","/timecard/timecard-copy.json")&&export_browser_safe());
  assert(readExport(a,"/timecard/timecard-copy.json")=="winning owner"&&replacements==mutations+1);
  assert(!export_browser_copy(&v,"/timecard/timecard.json","/timecard/timecard-copy.json")&&replacements==mutations+1);
 }else if(mode=="gate"){
  uint32_t z;uint64_t r;safe=false;auto calls=stats+reads+replacements;
  assert(a.stat_revision(c,"/timecard/timecard.json",&z,&r)==RISC_APP_DATA_UNAVAILABLE&&!volume.retained());assert(calls==stats+reads+replacements&&!v.ready(c));
  safe=true;assert(readExport(a,"/timecard/timecard.json")==original);
 }else if(mode=="post_stat_custody"){
  auto priorReads=reads;afterStatUnsafe=true;uint64_t z=0;assert(!v.file_open_read(c,"/timecard/timecard.json",&z)&&volume.retained()&&reads==priorReads);
  safe=true;assert(!volume.end()&&!v.ready(c));puts("export post-dispatch custody loss fences next callback PASS");fflush(stdout);std::_Exit(0);
 }else if(mode=="slots"){
  std::vector<std::unique_ptr<AppDataExport>>others;
  for(unsigned i=0;i<3;++i){others.emplace_back(new AppDataExport(hooks));risc_app_data_export_v1 extra{};assert(others.back()->configure(&b,map.data(),map.size(),"Other")&&others.back()->begin(&extra));}
  AppDataExport fifth(hooks);risc_app_data_export_v1 extra{},unchanged{};assert(fifth.configure(&b,map.data(),map.size(),"Fifth")&&!fifth.begin(&extra)&&!memcmp(&extra,&unchanged,sizeof(extra)));
  assert(others.back()->end());assert(fifth.begin(&extra)&&fifth.end());for(auto&other:others)assert(other->end());
 }else if(mode=="read_fault"){
  uint32_t z;uint64_t r;assert(a.stat_revision(c,"/timecard/timecard.json",&z,&r)==0);std::vector<char>out(z,'!');fault=6;
  assert(a.read_revision(c,"/timecard/timecard.json",r,out.data(),z,&z,&r)==RISC_APP_DATA_IO);for(char ch:out)assert(ch=='!');assert(readExport(a,"/timecard/timecard.json")==original);
 }else if(mode=="owner"){
  owned=false;auto calls=stats+reads+replacements;uint32_t z;uint64_t r;assert(a.stat_revision(c,"/timecard/timecard.json",&z,&r)==RISC_APP_DATA_CONTEXT);assert(!v.dir_open(c,"/")&&!volume.end());assert(stats+reads+replacements==calls);owned=true;
 }else if(mode=="allocation"){
  allocateFail=true;uint64_t z=0;assert(!v.file_open_read(c,"/timecard/timecard.json",&z));assert(!v.file_open_write(c,"/timecard/timecard-copy.json"));allocateFail=false;assert(readExport(a,"/timecard/timecard.json")==original);
 }else if(mode=="retained"||mode=="cleanup_retained"){
  uint32_t z;uint64_t r;assert(a.stat_revision(c,"/timecard/timecard.json",&z,&r)==0);fault=mode=="retained"?4:7;assert(a.replace_revision(c,"/timecard/timecard.json",r,original.data(),original.size())==RISC_APP_DATA_RETAINED&&volume.retained());
  unsigned calls=stats+reads+replacements;assert(!volume.end()&&!v.ready(c));assert(a.replace_revision(c,"/timecard/timecard.json",r,"x",1)==RISC_APP_DATA_RETAINED&&calls==stats+reads+replacements);puts("export native close retention PASS");fflush(stdout);std::_Exit(0);
 }else if(mode=="generic"){
  assert(volume.end());auto generic=map[0];strcpy(generic.owner,"unrelated.owner");generic.nameSpace=177;strcpy(generic.name,"record.raw");strcpy(generic.path,"/Alpha/Sub/renamed.bin");assert(files.replace(177,generic.name,0,"opaque",6)==0);
  AppDataExport other(hooks);assert(other.configure(&b,&generic,1,"Other device")&&other.begin(&a));assert(readExport(a,generic.path)=="opaque");assert(other.end());
 }else if(mode=="invalid"){
  for(const char*path:{"relative","/","/a/../b","/a//b","/.pending","/a/","/a\\b","/a/%2e"}){auto bad=map;strcpy(bad[0].path,path);assert(!AppDataExport::validEntries(bad.data(),bad.size()));}
  auto bad=map;bad[1]=bad[0];assert(!AppDataExport::validEntries(bad.data(),bad.size()));bad=map;strcpy(bad[1].path,"/timecard/timecard.json/child");assert(!AppDataExport::validEntries(bad.data(),bad.size()));
  uint64_t z;bool d;assert(!v.stat(c,"/appdata/n00000001/timecard.json",&z,&d));assert(!v.stat(c,"/bootfs/app.elf",&z,&d));
 }else assert(false);
 assert(volume.end()&&volume.exitSafe());uint32_t z=1;uint64_t r=1;assert(a.stat_revision(c,"/timecard/timecard.json",&z,&r)==RISC_APP_DATA_CONTEXT&&!z&&!r);assert(!allocations);
 assert(bytes(root+"/bootfs/app.elf")=="installed sentinel"&&bytes(root+"/nvs-sentinel")=="private settings sentinel");
 printf("app-data export real backend/browser %s PASS\n",mode.c_str());
}
