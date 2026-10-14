#include "runtime/provisioning/Profile.h"
#include "bootstrap/Json.h"
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
using namespace RiscProvision;
static std::string encode(const JsonDocument& doc){std::string result;serializeJson(doc,result);return result;}
static std::string make(size_t count=MaxFiles,unsigned schema=4){
 JsonDocument doc;doc["schema"]="riscrte.provisioning";doc["schema_version"]=schema;
 doc["wifi"]["ssid"]="test-network";doc["wifi"]["password"]="test-only-password";
 doc["image"]["url"]="https://example.test/image.bin";doc["image"]["bytes"]=0x550000;
 doc["image"]["sha256"]=std::string(64,'a');auto files=doc["files"].to<JsonArray>();
 for(size_t i=0;i<count;++i){
  std::string name=i==0?"boot.json":i==1?"board.json":i==2?"default.elf":"a"+std::to_string(i)+".json";
  if(schema==4){auto row=files.add<JsonArray>();row.add(name);row.add(i+1);row.add(std::string(64,'b'));}
  else{auto row=files.add<JsonObject>();row["path"]=name;row["bytes"]=i+1;row["sha256"]=std::string(64,'b');}
 }
 return encode(doc);
}
int main(){
 static_assert(MaxFiles==160 && ProfileInputBytes==16384,"fixed inventory/input bounds");
 static_assert(ProfileJsonBytes==65536 && sizeof(Profile)<=100*1024,"bounded native memory");
 auto profile=std::make_unique<Profile>();const auto full=make();
 assert(full.size()<ProfileInputBytes && parseProfile(full.data(),full.size(),*profile));
 assert(profile->count==MaxFiles && profile->imageMode() && profile->downloads()==1);
 assert(profile->image.bytes==0x550000 && !strcmp(profile->download(0).url,"https://example.test/image.bin"));
 for(size_t i=0;i<MaxFiles;++i){assert(profile->files[i].bytes==i+1 && !profile->files[i].url[0]);
  for(auto byte:profile->files[i].sha256)assert(byte==0xbb);}
 const auto rejects=[&](const std::string& text){assert(!parseProfile(text.data(),text.size(),*profile));
  for(size_t i=0;i<sizeof(*profile);++i)assert(!reinterpret_cast<unsigned char*>(profile.get())[i]);};
 rejects(make(MaxFiles+1));rejects(make(2));
 // The provisioning override must not widen package/board metadata policy.
 JsonDocument doc;assert(!RiscBoot::parse(full.data(),full.size(),doc));
 assert(RiscBoot::parse(full.data(),full.size(),doc,MaxFiles));
 const auto excessive=make(MaxFiles+1);assert(!RiscBoot::parse(excessive.data(),excessive.size(),doc,MaxFiles));
 // All old image schemas and their object rows retain the same interpretation.
 auto legacy=make(128,3);assert(parseProfile(legacy.data(),legacy.size(),*profile) && profile->count==128);
 legacy=make(132,3);assert(parseProfile(legacy.data(),legacy.size(),*profile) && profile->count==132);
 auto small=make(3);
 const auto changed=[&](auto change){assert(RiscBoot::parse(small.data(),small.size(),doc));change(doc);rejects(encode(doc));};
 changed([](JsonDocument& d){d["schema_version"]=5;});
 changed([](JsonDocument& d){d["schema_version"]=3;});
 changed([](JsonDocument& d){d["extra"]=true;});
 changed([](JsonDocument& d){d["base_url"]="https://example.test/";});
 changed([](JsonDocument& d){d["image"]["extra"]=1;});
 changed([](JsonDocument& d){d["wifi"]["extra"]=1;});
 changed([](JsonDocument& d){d["files"][0].to<JsonObject>()["path"]="boot.json";});
 for(const char* raw:{"null","true","\"boot.json\"","[]","[\"boot.json\"]",
                     "[\"boot.json\",1]","[\"boot.json\",1,\"hash\",0]"}){
  auto text=small;size_t start=text.find("[[")+1,end=text.find(']',start);text.replace(start,end-start+1,raw);rejects(text);}
 for(const char* name:{"", "../boot.json", "/boot.json", "x//boot.json", "x/./boot.json", "x/../boot.json", "boot%2ejson", "other.json", "board.json", "board.json/x"})
  changed([&](JsonDocument& d){d["files"][0][0]=name;});
 changed([](JsonDocument& d){d["files"][0][0]=std::string(193,'x');});
 for(const char* raw:{"0","-1","true","\"1\"","1.5","8388609","18446744073709551616"}){
  auto text=small;const auto at=text.find(",1,")+1;text.replace(at,1,raw);rejects(text);}
 for(const std::string& hash:{std::string(63,'a'),std::string(65,'a'),std::string(64,'A'),std::string(64,'g')})
  changed([&](JsonDocument& d){d["files"][0][2]=hash;});
 changed([](JsonDocument& d){d["files"][0][2]=false;});
 changed([](JsonDocument& d){for(auto row:d["files"].as<JsonArray>())row[1]=MaxFileBytes;});
 for(const char* url:{"http://example.test/image.bin","https://u@example.test/image.bin","https://example.test/image.bin?x=1","https://example.test/%2e/image.bin"})
  changed([&](JsonDocument& d){d["image"]["url"]=url;});
 changed([](JsonDocument& d){d["image"]["bytes"]=0x550001;});
 changed([](JsonDocument& d){d["image"]["bytes"]=true;});
 changed([](JsonDocument& d){d["image"]["sha256"]=std::string(64,'A');});
 for(const auto& edit:std::vector<std::pair<std::string,std::string>>{
     {"\"schema_version\":4","\"schema_version\":4,\"schema_version\":4"},
     {"\"schema_version\":4","\"schema_versi\\u006fn\":4"},
     {"test-network","test\\u0000network"}, {"boot.json","boot\\u0000.json"},
     {"test-network",std::string("bad\xc0\xaf",5)},
     {"test-network",std::string(33,'x')},{"test-only-password",std::string(64,'x')}}){
  auto text=small;const auto at=text.find(edit.first);assert(at!=std::string::npos);text.replace(at,edit.first.size(),edit.second);rejects(text);}
 // A short hostile JSON document can request many variant allocations before
 // schema validation. The bounded allocator must fail closed and wipe output.
 std::string hostile="{\"extra\":[";
 for(size_t i=0;i<MaxFiles;++i){if(i)hostile+=',';hostile+='[';
  for(unsigned j=0;j<40;++j){if(j)hostile+=',';hostile+='0';}hostile+=']';}
 hostile+="]}";assert(hostile.size()<ProfileInputBytes);rejects(hostile);
 auto padded=full+std::string(ProfileInputBytes-full.size(),' ');
 assert(parseProfile(padded.data(),padded.size(),*profile));padded+=' ';rejects(padded);
 assert(!parseProfile(nullptr,ProfileInputBytes,*profile));
 printf("Compact profile: 160 exact tuples (%zu bytes), legacy schemas, unchanged default JSON policy, malformed rows, wiped rejection and 16 KiB bound PASS; Profile=%zu bytes, JSON heap<=%zu bytes\n",full.size(),sizeof(Profile),ProfileJsonBytes);
}
