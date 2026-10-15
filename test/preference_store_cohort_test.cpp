#define main atomic_cohort_fixture_main
#include "atomic_key_value_cohort_test.cpp"
#undef main
static void replaceCapability(const std::string& cap,bool availabilityEnabled){
 for(const char* id:names){
  const std::string path=root+"/"+id+".json";JsonDocument doc;std::ifstream in(path);assert(!deserializeJson(doc,in));
  doc["requires"][0]["capability"]=cap;
  if(!availabilityEnabled)doc["requires"].as<JsonArray>().remove(1);
  std::ofstream out(path);serializeJson(doc,out);
 }
 const std::string path=root+"/boot.json";JsonDocument doc;std::ifstream in(path);assert(!deserializeJson(doc,in));
 for(JsonObject p:doc["app_capabilities"].as<JsonArray>()){p["grants"][0]["capability"]=cap;if(!availabilityEnabled)p["grants"].as<JsonArray>().remove(1);}
 std::ofstream out(path);serializeJson(doc,out);
}
int main(int argc,char** argv){
 assert(argc==2);const std::string base=argv[1];root=base+"/old";std::filesystem::create_directories(root);store();
 const auto old=root;root=base+"/new";std::filesystem::create_directories(root);store();replaceCapability("storage.preferences",true);const auto next=root;
 Disk d1,d6;Alloc allocation;C c1(1,d1.backend(),limits(),allocation.api()),c6(6,d6.backend(),limits(),allocation.api());assert(!c1.initialize(nullptr,0,{})&&!c6.initialize(nullptr,0,{}));
 Gate g1,g6;A a1(c1,g1.api()),a6(c6,g6.api());one=&a1;six=&a6;
 R atomic({owner,health,delay,logLine,bind});assert(atomic.prepare(old.c_str()));
 {R candidate({});assert(atomic.validateCohort(candidate,next.c_str(),admit,nullptr));}
 // A facade cannot replace registered native authority with another app ID.
 {JsonDocument doc;std::ifstream in(next+"/paper_clock.json");assert(!deserializeJson(doc,in));doc["id"]="intruder";std::ofstream out(next+"/paper_clock.json");serializeJson(doc,out);}
 {R candidate({});assert(!atomic.validateCohort(candidate,next.c_str(),admit,nullptr));}
 root=old;store();replaceCapability("storage.key-value",false);
 root=next;store();replaceCapability("storage.preferences",false);
 const RiscBoot::KeyValueBackend backend{nullptr,
  [](void*,uint32_t,const char*,void*,uint32_t,uint32_t*)->int32_t{assert(false);return -1;},
  [](void*,uint32_t,const char*,const void*,uint32_t)->int32_t{assert(false);return -1;},2048};
 R legacy({owner,health,delay,logLine,nullptr,&backend});assert(legacy.prepare(old.c_str()));
 {R candidate({});assert(legacy.validateCohort(candidate,next.c_str(),admit,nullptr));}
 // Even without native registration, namespace ownership cannot be reassigned.
 {JsonDocument doc;std::ifstream in(next+"/paper_clock.json");assert(!deserializeJson(doc,in));doc["id"]="intruder";std::ofstream out(next+"/paper_clock.json");serializeJson(doc,out);}
 {R candidate({});assert(!legacy.validateCohort(candidate,next.c_str(),admit,nullptr));}
 puts("Preferences preserving cohort: same-owner legacy/native facade transition, exact namespace ownership, no backend I/O PASS");
}
