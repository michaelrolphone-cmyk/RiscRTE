#include "runtime/provisioning/Coordinator.h"
#include "runtime/provisioning/SpiffsCapacity.h"
#include "bootstrap/Runtime.h"
#include "bootstrap/Json.h"
#include <openssl/sha.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>
using namespace RiscProvision;
namespace fs=std::filesystem;
using Bytes=std::vector<uint8_t>;
static Bytes read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void write(const fs::path& p,const Bytes& b){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());assert(f.good());}
static Bytes bytes(const std::string& s){return Bytes(s.begin(),s.end());}
static std::string hash(const Bytes& b){uint8_t d[32];SHA256(b.data(),b.size(),d);std::string result;for(auto x:d){result+="0123456789abcdef"[x>>4];result+="0123456789abcdef"[x&15];}return result;}
static unsigned launches=0;
static bool owner(){return true;}static bool health(risc_runtime_health_v1* h){h->uptime_ms=5;return true;}static void delay(uint32_t){}
static bool diagnostic(const char* s){assert(!strcmp(s,"TEST default"));++launches;return true;}
struct Host {
 fs::path root;std::map<std::string,Bytes> server;
 uint8_t committed[32]{};uint32_t time=0;bool active=false,safe=true,online=true,staged=false;
 unsigned connections=0,activations=0,aborts=0,downloads=0;size_t offset=0;
 std::string failure,pending;bool ambiguous=false,afterSelectUnsafe=false,tamper=false;
 explicit Host(fs::path p):root(std::move(p)){fs::create_directories(root);}
 Step op(const char* name){if(failure==name)return Step::Failed;if(pending==name)return Step::Pending;return Step::Done;}
 Backend backend(){return {this,
 [](void* c){return static_cast<Host*>(c)->time;},[](void* c){return static_cast<Host*>(c)->safe;},
 [](void* c){auto& h=*static_cast<Host*>(c);auto s=h.op("recover");if(s==Step::Done){fs::remove_all(h.root/"stage");h.staged=false;h.offset=0;}return s;},
 [](void* c){return static_cast<Host*>(c)->active;},
 [](void* c,const uint8_t* d){return !memcmp(static_cast<Host*>(c)->committed,d,32);},
 [](void* c,const char* ssid,const char* password){auto& h=*static_cast<Host*>(c);++h.connections;assert(!strcmp(ssid,"test-network"));assert(!strcmp(password,"test-only-password"));return h.online?h.op("connect"):Step::Failed;},
 [](void* c){auto& h=*static_cast<Host*>(c);auto s=h.op("begin");if(s==Step::Done){fs::create_directories(h.root/"stage");h.staged=true;}return s;},
 [](void* c,const File& file,uint32_t limit){auto& h=*static_cast<Host*>(c);++h.downloads;assert(limit==4096 && h.staged);auto s=h.op("download");if(s!=Step::Done)return s;auto it=h.server.find(file.url);if(it==h.server.end()||it->second.size()!=file.bytes)return Step::Failed;
  const auto& b=it->second;size_t n=std::min<size_t>(limit,b.size()-h.offset);auto p=h.root/"stage"/file.path;fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary|std::ios::app);f.write(reinterpret_cast<const char*>(b.data()+h.offset),n);f.close();if(!f)return Step::Failed;h.offset+=n;if(h.offset!=b.size())return Step::Pending;h.offset=0;return Step::Done;},
 [](void* c,const Profile& p){auto& h=*static_cast<Host*>(c);auto s=h.op("validate");if(s!=Step::Done)return s;if(h.tamper)write(h.root/"stage"/"board.json",bytes("corrupt"));size_t count=0;for(auto& entry:fs::recursive_directory_iterator(h.root/"stage"))if(entry.is_regular_file())++count;if(count!=p.count)return Step::Failed;
  for(size_t i=0;i<p.count;++i){auto b=read(h.root/"stage"/p.files[i].path);uint8_t d[32];SHA256(b.data(),b.size(),d);if(b.size()!=p.files[i].bytes||memcmp(d,p.files[i].sha256,32))return Step::Failed;}
  // Real Runtime preparation validates full board/manifest/dependency policy
  // without loading any module. Host dlopen ELF fixture is executed only after
  // simulated reset, never during candidate validation.
  auto rt=std::make_unique<RiscBoot::Runtime>(RiscBoot::Port{owner,health,delay,diagnostic});return rt->prepare((h.root/"stage").c_str())?Step::Done:Step::Failed;},
 [](void* c){return static_cast<Host*>(c)->op("close");},
 [](void* c,const uint8_t* d){auto& h=*static_cast<Host*>(c);++h.activations;if(h.failure=="activate")return Selection::Unchanged;
  // Model selector commit; old store is retained for a later health decision.
  if(fs::exists(h.root/"installed")){fs::remove_all(h.root/"previous");fs::rename(h.root/"installed",h.root/"previous");}
  fs::rename(h.root/"stage",h.root/"installed");h.staged=false;h.active=true;memcpy(h.committed,d,32);if(h.afterSelectUnsafe)h.safe=false;return h.ambiguous?Selection::Unknown:Selection::Selected;},
 [](void* c){auto& h=*static_cast<Host*>(c);++h.aborts;auto s=h.op("abort");if(s==Step::Done){fs::remove_all(h.root/"stage");h.staged=false;h.offset=0;}return s;}
 };}
 void boot(){assert(active);auto rt=std::make_unique<RiscBoot::Runtime>(RiscBoot::Port{owner,health,delay,diagnostic});assert(rt->prepare((root/"installed").c_str()));assert(rt->run());}
};
static std::string profile(Host& h){
 std::string s=R"({"schema":"riscrte.provisioning","schema_version":1,"wifi":{"ssid":"test-network","password":"test-only-password"},"files":[)";bool first=true;
 for(auto& item:h.server){if(!first)s+=",";first=false;auto path=item.first.substr(strlen("https://example.test/"));s+="{\"path\":\""+path+"\",\"url\":\""+item.first+"\",\"bytes\":"+std::to_string(item.second.size())+",\"sha256\":\""+hash(item.second)+"\"}";}return s+"]}";
}
static State run(Coordinator& c){for(unsigned i=0;i<10000;++i){auto s=c.step();if(s==State::Installed||s==State::Recovery||s==State::Restart||s==State::Retained||s==State::SelectionUnknown)return s;}assert(false);return State::Retained;}
static void replace(std::string& s,const std::string& from,const std::string& to){auto i=s.find(from);assert(i!=std::string::npos);s.replace(i,from.size(),to);}
int main(int argc,char** argv){assert(argc==2);fs::path base=argv[1];Host h(base/"host");
 h.server["https://example.test/boot.json"]=bytes(R"({"board":"board.json","default_app":"default.elf","drivers":[]})");
 h.server["https://example.test/board.json"]=bytes(R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 h.server["https://example.test/default.elf"]=read(base/"default.elf");
 auto p=std::make_unique<Profile>();auto json=profile(h);uint8_t digest[32];SHA256(reinterpret_cast<const uint8_t*>(json.data()),json.size(),digest);
 assert(parseProfile(json.data(),json.size(),*p));
 // SPIFFS estimates include write churn, metadata and four reserved blocks.
 static_assert(SpiffsCapacity::filePages(1)==3 && SpiffsCapacity::filePages(251)==3,"first page");
 static_assert(SpiffsCapacity::filePages(252)==4,"next data page");
 static_assert(SpiffsCapacity::filePages(8192)==35,"coalesced flush");
 assert(SpiffsCapacity::fits(*p,0x510000));
 {auto oversized=std::make_unique<Profile>();oversized->count=3;
  for(auto& f:oversized->files)f.bytes=2*1024*1024;
  assert(!SpiffsCapacity::fits(*oversized,0x510000));
  oversized->files[0].bytes=0;assert(!SpiffsCapacity::fits(*oversized,0x510000));}
 // Strict schema, limits, duplicate keys, path/source confusion and credentials.
 for(auto change:std::vector<std::pair<std::string,std::string>>{{"\"schema_version\":1","\"schema_version\":true"},{"\"schema_version\":1","\"schema_version\":1,\"schema_version\":1"},{"\"wifi\":","\"extra\":1,\"wifi\":"},{"test-only-password","short"},{"test-network",""},{"\"path\":\"board.json\"","\"path\":\"../board.json\""},{"https://example.test/board.json","http://example.test/board.json"},{"https://example.test/board.json","https://user@example.test/board.json"},{"https://example.test/board.json","https://example.test/board.json?token=x"},{"\"path\":\"board.json\"","\"path\":\"default.elf\""},{"\"path\":\"board.json\"","\"path\":\"default.elf/sub\""},{"\"path\":\"board.json\"","\"path\":\"other.json\""}}){auto bad=json;replace(bad,change.first,change.second);assert(!parseProfile(bad.data(),bad.size(),*p));assert(p->count==0 && p->password[0]==0);}
 // Compact profiles admit a complete Watch-sized cohort within owner NVS.
 {JsonDocument doc;assert(RiscBoot::parse(json.data(),json.size(),doc));
  doc["schema_version"]=2;doc["base_url"]="https://example.test/";
  for(auto file:doc["files"].as<JsonArray>())file.as<JsonObject>().remove("url");
  const auto compact=[&](){std::string result;serializeJson(doc,result);return result;};
  auto text=compact();assert(parseProfile(text.data(),text.size(),*p));
  assert(!strcmp(p->files[0].url,"https://example.test/board.json"));
  for(size_t i=3;i<MaxFiles;++i){auto item=doc["files"].as<JsonArray>().add<JsonObject>();
   item["path"]="app"+std::to_string(i)+".json";item["bytes"]=1;item["sha256"]=std::string(64,'0');
   if(i==58){text=compact();assert(text.size()<16384 && parseProfile(text.data(),text.size(),*p) && p->count==59);}}
  text=compact();assert(parseProfile(text.data(),text.size(),*p) && p->count==MaxFiles);
  auto extra=doc["files"].as<JsonArray>().add<JsonObject>();extra["path"]="excess.json";extra["bytes"]=1;extra["sha256"]=std::string(64,'0');
  text=compact();assert(!parseProfile(text.data(),text.size(),*p));
  doc["files"].as<JsonArray>().remove(MaxFiles);
  for(const char* bad:{"https://example.test", "https://example.test/a/../", "http://example.test/", "https://user@example.test/", "https://example.test/?token=x/", "https://example.test/%2e/"}){
   doc["base_url"]=bad;text=compact();assert(!parseProfile(text.data(),text.size(),*p));assert(!p->count&&!p->password[0]);}
  doc["base_url"]="https://example.test/";doc["files"][0]["url"]="https://other.test/board.json";text=compact();assert(!parseProfile(text.data(),text.size(),*p));
  doc["files"][0].as<JsonObject>().remove("url");doc["base_url"]="https://example.test/"+std::string(365,'x')+"/";text=compact();assert(!parseProfile(text.data(),text.size(),*p));
 }
 // Numeric/hash/size limits reject before any network or storage callback.
 assert(!parseProfile(nullptr,0,*p));
 for(const char* invalid:{"0","-1","true","1.5","8388609","18446744073709551616"}){
   auto bad=json;auto at=bad.find("\"bytes\":")+8;auto end=bad.find(',',at);bad.replace(at,end-at,invalid);
   assert(!parseProfile(bad.data(),bad.size(),*p));
 }
 {auto bad=json;auto at=bad.find("\"sha256\":\"")+10;bad[at]='g';assert(!parseProfile(bad.data(),bad.size(),*p));}
 {auto bad=json;replace(bad,"test-network","test\\u0000network");assert(!parseProfile(bad.data(),bad.size(),*p));}
 {auto bad=json;replace(bad,"test-network",std::string(33,'x'));assert(!parseProfile(bad.data(),bad.size(),*p));}
 {auto bad=json;replace(bad,"test-only-password",std::string(64,'x'));assert(!parseProfile(bad.data(),bad.size(),*p));}
 {auto bad=json;auto at=bad.find("\"bytes\":");while(at!=std::string::npos){at+=8;auto end=bad.find(',',at);bad.replace(at,end-at,"8388608");at=bad.find("\"bytes\":",at);}assert(!parseProfile(bad.data(),bad.size(),*p));}
 {auto open=json;replace(open,"test-only-password","");assert(parseProfile(open.data(),open.size(),*p));assert(p->password[0]==0);}
 assert(parseProfile(json.data(),json.size(),*p));
 Coordinator first(h.backend(),*p,digest);assert(run(first)==State::Restart);assert(h.activations==1 && launches==0);h.boot();assert(launches==1);
 auto original=read(h.root/"installed"/"default.elf");
 unsigned connects=h.connections;h.online=false;Coordinator same(h.backend(),*p,digest);assert(run(same)==State::Installed && h.connections==connects);h.boot();
 uint8_t changed[32];memcpy(changed,digest,32);changed[0]^=1;
 Coordinator offline(h.backend(),*p,changed);assert(run(offline)==State::Installed && h.aborts==1);h.boot();h.online=true;
 // Every pre-selection phase fails without modifying installed default.
 for(const char* fail:{"recover","connect","begin","download","validate","close","activate"}){h.failure=fail;Coordinator c(h.backend(),*p,changed);assert(run(c)==State::Installed);assert(read(h.root/"installed"/"default.elf")==original);assert(!fs::exists(h.root/"stage"));h.boot();}
 h.failure="";h.tamper=true;Coordinator corrupt(h.backend(),*p,changed);assert(run(corrupt)==State::Installed);h.tamper=false;
 // Hash-correct but malformed full board graph is rejected by real admission.
 h.server["https://example.test/boot.json"]=bytes(R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"missing.json","instance_id":7}]})");auto malformed=profile(h);assert(parseProfile(malformed.data(),malformed.size(),*p));Coordinator badGraph(h.backend(),*p,changed);assert(run(badGraph)==State::Installed);
 h.server["https://example.test/boot.json"]=bytes(R"({"board":"board.json","default_app":"default.elf","drivers":[]})");assert(parseProfile(json.data(),json.size(),*p));
 // Interrupted stage is discarded at next boot. Installed store survives.
 Coordinator cut(h.backend(),*p,changed);while(cut.state()!=State::Download)cut.step();cut.step();assert(fs::exists(h.root/"stage"));Coordinator resume(h.backend(),*p,changed);assert(run(resume)==State::Restart);assert(fs::exists(h.root/"previous"));h.boot();
 // Cleanup failure and unsafe native state suppress installed fallback.
 h.failure="abort";h.online=false;Coordinator retained(h.backend(),*p,digest);assert(run(retained)==State::Retained);h.failure="";h.online=true;
 h.safe=false;Coordinator unsafe(h.backend(),*p,digest);assert(run(unsafe)==State::Retained);h.safe=true;
 // Overall and cleanup deadlines remain bounded across uint32 clock wrap.
 h.time=UINT32_MAX-100;h.pending="connect";Coordinator timeout(h.backend(),*p,digest);timeout.step();timeout.step();h.time+=300000;assert(timeout.step()==State::Cleanup);h.pending="abort";h.time+=30000;assert(timeout.step()==State::Retained);h.pending="";
 // Ambiguous selection is terminal, never abort/retry/rewrite after commit.
 h.ambiguous=true;unsigned aborts=h.aborts;Coordinator ambiguous(h.backend(),*p,digest);assert(run(ambiguous)==State::SelectionUnknown);assert(h.aborts==aborts);unsigned acts=h.activations;for(int i=0;i<5;++i)assert(ambiguous.step()==State::SelectionUnknown);assert(h.activations==acts);h.ambiguous=false;h.boot();
 h.afterSelectUnsafe=true;Coordinator selectionUnsafe(h.backend(),*p,changed);assert(run(selectionUnsafe)==State::Retained);assert(h.aborts==aborts);h.afterSelectUnsafe=false;h.safe=true;
 Host fresh(base/"fresh");fresh.server=h.server;fresh.online=false;Coordinator noDefault(fresh.backend(),*p,digest);assert(run(noDefault)==State::Recovery);
 puts("Provisioning parser, real graph admission/default launch, changed-profile, offline fallback, interruption, corruption, deadline, cleanup and ambiguous activation tests PASS");
}
