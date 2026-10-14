#include "runtime/provisioning/PreservingPolicy.h"
#include "runtime/provisioning/Profile.h"
#include "bootstrap/Json.h"
#include "runtime/RuntimeLimits.h"
#include "runtime/update/PairedBank.h"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>

namespace fs=std::filesystem;
namespace {
struct Fault {
  std::string path;
  FILE* stream=nullptr;
  unsigned matches=0,opens=0,closes=0,reads=0,allocations=0;
  bool open=false,close=false,read=false,buffer=false,parser=false,closed=false;
} fault;
bool checking=false;
unsigned allOpens=0,checks=0;
std::string bytes(const fs::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
void save(const fs::path& path,const std::string& content){std::ofstream output(path,std::ios::binary);output<<content;assert(output.good());}
void save(const fs::path& path,const JsonDocument& doc){std::string content;serializeJson(doc,content);save(path,content);}
template<class F>void modify(const fs::path& root,const char* name,F change){
  JsonDocument doc;const std::string content=bytes(root/name);
  assert(RiscBoot::parse(content.data(),content.size(),doc));change(doc);save(root/name,doc);
}
std::map<std::string,std::string> snapshot(const fs::path& root){
  std::map<std::string,std::string> result;
  for(const auto& entry:fs::recursive_directory_iterator(root))if(entry.is_regular_file())
    result.emplace(entry.path().lexically_relative(root).string(),bytes(entry.path()));
  return result;
}
void arm(const fs::path& path){assert(!fault.stream);fault={};fault.path=path.string();}
void verify(const fs::path& installed,const fs::path& candidate,bool expected,bool& retained){
  const auto oldBefore=snapshot(installed),newBefore=snapshot(candidate);
  checking=true;
  const bool accepted=RiscProvision::preservingPolicy(installed.c_str(),candidate.c_str(),retained);
  checking=false;
  if(accepted!=expected)fprintf(stderr,"check %u: expected %d, got %d; fault %s\n",checks,expected,accepted,fault.path.c_str());
  assert(accepted==expected);++checks;
  assert(snapshot(installed)==oldBefore && snapshot(candidate)==newBefore);
}
void verify(const fs::path& installed,const fs::path& candidate,bool expected){
  bool retained=false;verify(installed,candidate,expected,retained);assert(!retained);
}
const char* boot=R"({"board":"board.json","default_app":"app.elf","provider_activation":"demand-retained","drivers":[{"manifest":"probe.json","instance_id":7,"key_value":[{"namespace":9,"key":"owned"}],"image_source":{"volume_instance":8,"path":"Drivers/probe.elf","internal_fallback":true}}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"storage.app-data","api":1,"namespace":42}],"app_data_export":{"namespace":42},"catalog_authority":{"allowed":true}}],"resident_shell":{"host":"app.elf","foreground":["app.elf"]},"installation":{"namespace_range":{"first":100,"count":20},"media":{"volume_instance":8}},"port":{"features":["retained-wake","iq"]}})";
const char* board=R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"rev1","buses":[],"devices":[{"instance_id":7,"chip":{"vendor":"test","model":"probe","revision":"rev1"},"compatible":"test,probe","config_type":"gpio.bank","config_version":1,"config":{"pins":[1],"output":true}}]})";
const char* driver=R"({"type":"driver","id":"probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"probe.elf","requires":[{"capability":"hardware.device","api":1}],"provides":[{"capability":"test.probe","api":1}],"hardware_compatibility":[{"compatible":"test,probe","revisions":["rev1"],"config_type":"gpio.bank","config_version":1}],"description":"provider","display_name":"Probe","notes":"unchanged","status":"software","physical_verification":false})";
const char* app=R"({"type":"application","id":"app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"app.elf","entry":"app_main","runtime_lifecycle":"foreground","requires":[{"capability":"storage.app-data","api":1}],"description":"app","display_name":"Application","icon":"A","optional":["clock"],"supported_file_types":["text/plain"],"launcher_visible":true,"min_firmware_version":"0.1.1"})";
void store(const fs::path& root){
  fs::remove_all(root);fs::create_directories(root);save(root/"boot.json",boot);save(root/"board.json",board);
  save(root/"probe.json",driver);save(root/"app.json",app);
  JsonDocument cohort;cohort["schema"]="riscrte.cohort";cohort["schema_version"]=1;
  cohort["product"]="test-product";cohort["version"]="1.0.0";cohort["runtime_version"]="0.1.99";
  cohort["source_repo"]="owner/product";cohort["source_revision"]=std::string(40,'a');
  cohort["layout"]=RiscUpdate::Layout;cohort["store_abi"]=RiscUpdate::StoreAbi;
  cohort["firmware_size"]=128;cohort["firmware_sha256"]=std::string(64,'b');save(root/"cohort.json",cohort);
  // Deliberately not executable: this guard must never inspect or execute ELF.
  save(root/"app.elf","not an elf");save(root/"probe.elf","not a provider");
}
void successes(const fs::path& old,const fs::path& next){
  store(old);store(next);arm(old/"unused");verify(old,next,true);
  // Exact profile hashes identify content: unchanged-version ELF bytes may change.
  save(next/"app.elf","same-version different app image");save(next/"probe.elf","same-version different provider image");verify(old,next,true);
  // Whitespace and top-level key order are not policy changes.
  save(next/"app.json",std::string("\n \t")+app+"\n");verify(old,next,true);
  modify(next,"boot.json",[](JsonDocument& doc){JsonDocument reordered;for(JsonPairConst item:doc.as<JsonObjectConst>())if(strcmp(item.key().c_str(),"board"))reordered[item.key().c_str()]=item.value();reordered["board"]="board.json";doc=std::move(reordered);});
  verify(old,next,true);
  for(const char* name:{"app.json","probe.json","cohort.json"})modify(next,name,[](JsonDocument& doc){doc["version"]="1.0.1";});
  modify(next,"cohort.json",[](JsonDocument& doc){doc["source_revision"]=std::string(40,'c');});
  save(next/"app.elf","different app image");save(next/"probe.elf","different provider image");verify(old,next,true);
  store(old);store(next);
  modify(next,"boot.json",[](JsonDocument& doc){doc["drivers"].as<JsonArray>().add(doc["drivers"][0]);});
  save(old/"boot.json",bytes(next/"boot.json"));verify(old,next,true);
  puts("Preserving policy: identical/reordered JSON, version increases, shared provider selection and opaque image bytes PASS");
}
void policyChanges(const fs::path& old,const fs::path& next){
  store(old);
  auto reject=[&](const char* name,auto change){store(next);modify(next,name,change);verify(old,next,false);};
  reject("boot.json",[](JsonDocument& d){d["default_app"]="other.elf";});
  reject("boot.json",[](JsonDocument& d){d["provider_activation"]="eager";});
  reject("boot.json",[](JsonDocument& d){d["drivers"][0]["instance_id"]=8;});
  reject("boot.json",[](JsonDocument& d){d["drivers"][0]["key_value"][0]["namespace"]=10;});
  reject("boot.json",[](JsonDocument& d){d["drivers"][0]["image_source"]["internal_fallback"]=false;});
  reject("boot.json",[](JsonDocument& d){d["drivers"].to<JsonArray>();});
  reject("boot.json",[](JsonDocument& d){d["drivers"].as<JsonArray>().add<JsonObject>()["manifest"]="extra.json";});
  reject("boot.json",[](JsonDocument& d){d["app_capabilities"].to<JsonArray>();});
  reject("boot.json",[](JsonDocument& d){d["app_capabilities"][0]["grants"][0]["namespace"]=43;});
  reject("boot.json",[](JsonDocument& d){d["app_capabilities"][0]["grants"][0]["api"]=2;});
  reject("boot.json",[](JsonDocument& d){d["app_capabilities"][0]["app_data_export"]["namespace"]=43;});
  reject("boot.json",[](JsonDocument& d){d["app_capabilities"][0]["catalog_authority"]["allowed"]=false;});
  reject("boot.json",[](JsonDocument& d){d.remove("resident_shell");});
  reject("boot.json",[](JsonDocument& d){d["port"]["features"].as<JsonArray>().remove(0);});
  reject("boot.json",[](JsonDocument& d){d["installation"]["namespace_range"]["count"]=21;});
  reject("boot.json",[](JsonDocument& d){d["installation"]["media"]["volume_instance"]=9;});
  reject("boot.json",[](JsonDocument& d){d["installation"]["new_authority"]=true;});
  reject("boot.json",[](JsonDocument& d){d["port"]["features"][0]="iq";d["port"]["features"][1]="retained-wake";});
  reject("board.json",[](JsonDocument& d){d["devices"][0]["config"]["pins"][0]=2;});
  reject("board.json",[](JsonDocument& d){d["devices"][0]["config"]["output"]=1;});
  reject("board.json",[](JsonDocument& d){d["devices"][0]["instance_id"]="7";});
  store(next);std::string floating=board;const size_t offset=floating.find("\"instance_id\":7");assert(offset!=std::string::npos);
  floating.replace(offset,strlen("\"instance_id\":7"),"\"instance_id\":7.0");save(next/"board.json",floating);verify(old,next,false);
  reject("board.json",[](JsonDocument& d){d["new_authority"]=nullptr;});
  // Board comparison follows boot.board, never an assumed board.json path.
  store(old);store(next);for(const auto& root:{old,next}){fs::rename(root/"board.json",root/"selected.json");modify(root,"boot.json",[](JsonDocument& d){d["board"]="selected.json";});save(root/"board.json","unselected");}
  verify(old,next,true);modify(next,"selected.json",[](JsonDocument& d){d["revision"]="rev2";});verify(old,next,false);
  puts("Preserving policy: grants/namespaces, selections, resident/features, media/installation, board and type mutations rejected PASS");
}
void contentPaths(const fs::path& old,const fs::path& next){
  for(const char* selection:{"drivers","app_capabilities"}){
    const bool provider=!strcmp(selection,"drivers");
    const std::string prefix=provider?"Drivers/probe/":"Apps/app/";
    const std::string first=prefix+"image-"+std::string(64,'a')+".elf";
    const std::string second=prefix+"image-"+std::string(64,'b')+".elf";
    auto reset=[&](){
      store(old);store(next);
      for(const auto& root:{old,next})modify(root,"boot.json",[&](JsonDocument& d){
        auto source=d[selection][0]["image_source"].to<JsonObject>();
        source["volume_instance"]=8;source["path"]=first;source["internal_fallback"]=true;
      });
    };
    reset();verify(old,next,true);
    modify(next,"boot.json",[&](JsonDocument& d){d[selection][0]["image_source"]["path"]=second;});verify(old,next,true);
    modify(next,provider?"probe.json":"app.json",[](JsonDocument& d){d["version"]="2.0.0";});verify(old,next,true);
    const std::string invalid[]={
      prefix+"image-"+std::string(64,'A')+".elf",prefix+"image-"+std::string(63,'b')+".elf",
      prefix+"image-"+std::string(65,'b')+".elf",prefix+"image-"+std::string(64,'g')+".elf",
      prefix+std::string(64,'b')+".elf",second+"?ignored",second+"/extra.elf",
      "Other/"+second,"/"+second,"../"+second,prefix+"subdir/"+"image-"+std::string(64,'b')+".elf",
      (provider?"Drivers/other/":"Apps/other/")+std::string("image-")+std::string(64,'b')+".elf",
      (provider?"Apps/probe/":"Drivers/app/")+std::string("image-")+std::string(64,'b')+".elf"
    };
    for(const auto& bad:invalid){reset();modify(next,"boot.json",[&](JsonDocument& d){d[selection][0]["image_source"]["path"]=bad;});verify(old,next,false);}
    reset();modify(old,"boot.json",[&](JsonDocument& d){d[selection][0]["image_source"]["path"]="ordinary.elf";});verify(old,next,false);
    for(unsigned mutation=0;mutation<6;++mutation){
      reset();modify(next,"boot.json",[&](JsonDocument& d){
        auto source=d[selection][0]["image_source"].as<JsonObject>();source["path"]=second;
        if(mutation==0)source["volume_instance"]=9;
        if(mutation==1)source["internal_fallback"]=false;
        if(mutation==2)source["new_authority"]=true;
        if(mutation==3)source.remove("internal_fallback");
        if(mutation==4)source.remove("path");
        if(mutation==5)d[selection][0].as<JsonObject>().remove("image_source");
      });verify(old,next,false);
    }
    reset();modify(old,"boot.json",[&](JsonDocument& d){d[selection][0].as<JsonObject>().remove("image_source");});verify(old,next,false);
    reset();modify(next,provider?"probe.json":"app.json",[](JsonDocument& d){d["id"]="other";d["version"]="2.0.0";});verify(old,next,false);
    for(bool absent:{false,true}){
      reset();for(const auto& root:{old,next})modify(root,"boot.json",[&](JsonDocument& d){
        if(absent)d[selection][0]["image_source"].as<JsonObject>().remove("internal_fallback");
        else d[selection][0]["image_source"]["internal_fallback"]=false;
      });verify(old,next,true);
      modify(next,"boot.json",[&](JsonDocument& d){d[selection][0]["image_source"]["path"]=second;});verify(old,next,false);
    }
  }
  puts("Preserving policy: identity-bound SD content paths only; media authority/fallback/source presence frozen PASS");
}
void manifestChanges(const fs::path& old,const fs::path& next){
  store(old);
  for(const char* name:{"app.json","probe.json"}){
    for(const char* version:{"0.9.9","1.0","01.0.0","1.0.0-beta","4294967296.0.0","1.0.0.0",""}){
      store(next);modify(next,name,[&](JsonDocument& d){d["version"]=version;});verify(old,next,false);
    }
    for(const char* field:{"description","display_name","icon","notes","status","physical_verification","file_name","entry","runtime_lifecycle","architecture","id","part","legacy_manual_only","min_firmware_version","optional","supported_file_types","launcher_visible","new_authority"}){
      for(const char* version:{"1.0.0","1.0.1"}){
        store(next);modify(next,name,[&](JsonDocument& d){d["version"]=version;d[field]="changed";});verify(old,next,false);
      }
    }
    for(const char* field:{"requires","type","file_name","description"}){
      store(next);modify(next,name,[&](JsonDocument& d){d["version"]="1.0.1";d.remove(field);});verify(old,next,false);
    }
    store(next);modify(next,name,[](JsonDocument& d){d["version"]="1.0.1";d["requires"][0]["api"]=2;});verify(old,next,false);
  }
  for(const char* field:{"provides","hardware_compatibility","driver_abi"}){
    store(next);modify(next,"probe.json",[&](JsonDocument& d){d["version"]="2.0.0";d.remove(field);});verify(old,next,false);
  }
  store(next);modify(next,"probe.json",[](JsonDocument& d){d["version"]="2.0.0";d["hardware_compatibility"][0]["config_version"]=2;});verify(old,next,false);
  // The exclusion is top-level only: a nested version cannot hide a change.
  for(const auto& root:{old,next})modify(root,"app.json",[](JsonDocument& d){d["unknown"]["version"]="1.0.0";});
  modify(next,"app.json",[](JsonDocument& d){d["version"]="1.0.1";d["unknown"]["version"]="2.0.0";});verify(old,next,false);
  puts("Preserving policy: canonical monotonic versions; all semantic and cosmetic manifest changes rejected PASS");
}
void cohortChanges(const fs::path& old,const fs::path& next){
  store(old);
  for(const char* field:{"product","source_repo","runtime_version","source_revision","layout","store_abi","firmware_size","firmware_sha256","schema","schema_version","unknown"}){
    store(next);modify(next,"cohort.json",[&](JsonDocument& d){d[field]="changed";});verify(old,next,false);
  }
  for(const char* version:{"0.9.9","1.0","01.0.0","1.0.0-beta","4294967296.0.0"}){
    store(next);modify(next,"cohort.json",[&](JsonDocument& d){d["version"]=version;});verify(old,next,false);
  }
  store(next);modify(next,"cohort.json",[](JsonDocument& d){d["source_revision"]=std::string(40,'c');});verify(old,next,false);
  for(const char* field:{"product","source_repo","runtime_version","layout","store_abi","firmware_size","firmware_sha256"}){
    store(next);modify(next,"cohort.json",[&](JsonDocument& d){d["version"]="2.0.0";
      if(!strcmp(field,"product"))d[field]="other-product";
      else if(!strcmp(field,"source_repo"))d[field]="other/product";
      else if(!strcmp(field,"runtime_version"))d[field]="0.2.0";
      else if(!strcmp(field,"firmware_size"))d[field]=129;
      else if(!strcmp(field,"firmware_sha256"))d[field]=std::string(64,'c');
      else d[field]="changed";
    });verify(old,next,false);
  }
  puts("Preserving policy: cohort downgrade/identity/native changes and revision-only substitutions rejected PASS");
}
void malformed(const fs::path& old,const fs::path& next){
  for(const char* name:{"boot.json","board.json","app.json","probe.json","cohort.json"})for(bool installed:{false,true}){
    for(const std::string& bad:{std::string("{"),std::string("[]"),std::string("{\"version\":\"1.0.0\",\"version\":\"2.0.0\"}"),std::string(65537,' ')}){
      store(old);store(next);save((installed?old:next)/name,bad);verify(old,next,false);
    }
    store(old);store(next);fs::remove((installed?old:next)/name);verify(old,next,false);
  }
  for(const char* relative:{"../board.json","/board.json","./board.json","a//board.json","a\\board.json"}){
    store(old);store(next);for(const auto& root:{old,next})modify(root,"boot.json",[&](JsonDocument& d){d["board"]=relative;});verify(old,next,false);
  }
  for(const char* selections:{"drivers","app_capabilities"}){
    store(old);store(next);for(const auto& root:{old,next})modify(root,"boot.json",[&](JsonDocument& d){d[selections][0]["manifest"]="../app.json";});verify(old,next,false);
    store(old);store(next);for(const auto& root:{old,next})modify(root,"boot.json",[&](JsonDocument& d){d[selections]=nullptr;});verify(old,next,false);
    store(old);store(next);for(const auto& root:{old,next})modify(root,"boot.json",[&](JsonDocument& d){auto a=d[selections].as<JsonArray>();const size_t maximum=!strcmp(selections,"drivers")?RiscLimits::Providers:RiscLimits::Apps;while(a.size()<=maximum)a.add(a[0]);});verify(old,next,false);
  }
  bool retained=false;const unsigned before=allOpens;checking=true;
  assert(!RiscProvision::preservingPolicy(nullptr,next.c_str(),retained));
  assert(!RiscProvision::preservingPolicy(old.c_str(),nullptr,retained));
  assert(!RiscProvision::preservingPolicy("relative",next.c_str(),retained));
  assert(!RiscProvision::preservingPolicy(old.c_str(),"",retained));
  assert(!RiscProvision::preservingPolicy(std::string(257,'/').c_str(),next.c_str(),retained));
  checking=false;assert(allOpens==before && !retained);
  puts("Preserving policy: malformed/missing/oversized/duplicate JSON, invalid paths, selection types and bounds rejected PASS");
}
void faults(const fs::path& old,const fs::path& next){
  store(old);store(next);
  for(const auto& root:{old,next})for(const char* name:{"boot.json","board.json","app.json","probe.json","cohort.json"}){
    for(unsigned failure=0;failure<5;++failure)for(bool close:{false,true}){
      arm(root/name);fault.open=failure==0;fault.read=failure==1;fault.buffer=failure==2;fault.parser=failure==3;fault.close=close;
      bool retained=false;verify(old,next,failure==4 && !close,retained);
      assert(fault.matches==1 && fault.opens==(fault.open?0u:1u) && fault.closes==fault.opens);
      assert(!fault.stream && retained==(close && !fault.open));
      if(fault.buffer)assert(!fault.reads);
      if(retained){const unsigned opened=allOpens;verify(old,next,false,retained);assert(retained && allOpens==opened && fault.closes==1);}
    }
  }
  arm(old/"unused");bool retained=true;const unsigned opened=allOpens;verify(old,next,false,retained);assert(retained && opened==allOpens);
  arm(old/"unused");verify(old,next,true);
  puts("Preserving policy: installed/candidate metadata open/read/buffer/parser OOM/close matrix, sticky retention and single close PASS");
}
void sourceBindings(const fs::path& root){
  auto profile=std::make_unique<RiscProvision::Profile>();
  auto reset=[&](){
    arm(root/"unused");store(root);profile->clear();profile->count=3;
    const char* inventory[]={"boot.json","drivers/probe.elf","apps/app.elf"};
    for(size_t i=0;i<3;++i){strcpy(profile->files[i].path,inventory[i]);profile->files[i].bytes=32;memset(profile->files[i].sha256,i==1?0xaa:0xbb,32);}
    fs::create_directory(root/"drivers");fs::create_directory(root/"apps");
    fs::rename(root/"probe.json",root/"drivers/probe.json");fs::rename(root/"app.json",root/"apps/app.json");
    modify(root,"boot.json",[](JsonDocument& d){
      d["drivers"][0]["manifest"]="drivers/probe.json";d["app_capabilities"][0]["manifest"]="apps/app.json";
      for(const char* selection:{"drivers","app_capabilities"}){
        const bool provider=!strcmp(selection,"drivers");auto source=d[selection][0]["image_source"].to<JsonObject>();
        source["volume_instance"]=8;source["internal_fallback"]=true;
        source["path"]=(provider?"Drivers/probe/image-":"Apps/app/image-")+std::string(64,provider?'a':'b')+".elf";
      }
    });
  };
  auto checked=[&](bool expected,bool& retained){
    const auto before=snapshot(root);checking=true;
    const bool accepted=RiscProvision::preservingImageSources(root.c_str(),*profile,retained);
    checking=false;if(accepted!=expected)fprintf(stderr,"source check %u: expected %d, got %d\n",checks,expected,accepted);
    assert(accepted==expected);++checks;assert(snapshot(root)==before);
  };
  auto verifySource=[&](bool expected){bool retained=false;checked(expected,retained);assert(!retained);};
  reset();verifySource(true);
  for(size_t i:{size_t(1),size_t(2)}){
    reset();profile->files[i].sha256[31]^=1;verifySource(false);
    reset();strcpy(profile->files[i].path,"different.elf");verifySource(false);
    reset();profile->files[i].bytes=0;verifySource(false);
    reset();profile->files[i].bytes=RiscProvision::MaxFileBytes+1;verifySource(false);
    reset();profile->files[3]=profile->files[i];profile->count=4;verifySource(false);
    reset();memset(profile->files[i].path,'a',sizeof(profile->files[i].path));verifySource(false);
  }
  reset();profile->count=0;verifySource(false);
  reset();profile->count=RiscProvision::MaxFiles+1;verifySource(false);
  reset();profile->count=2;verifySource(false);
  for(const char* selection:{"drivers","app_capabilities"}){
    const bool provider=!strcmp(selection,"drivers");const char* manifest=provider?"drivers/probe.json":"apps/app.json";
    const std::string prefix=provider?"Drivers/probe/image-":"Apps/app/image-";
    reset();modify(root,"boot.json",[&](JsonDocument& d){d[selection][0]["image_source"]["path"]=prefix+std::string(64,'c')+".elf";});verifySource(false);
    memset(profile->files[provider?1:2].sha256,0xcc,32);verifySource(true);
    reset();modify(root,"boot.json",[&](JsonDocument& d){d[selection][0]["image_source"]["path"]="legacy.elf";});verifySource(false);
    reset();modify(root,"boot.json",[&](JsonDocument& d){d[selection][0]["image_source"]["internal_fallback"]=1;});verifySource(false);
    reset();modify(root,"boot.json",[&](JsonDocument& d){d[selection][0]["image_source"]["path"]=prefix+std::string(64,'A')+".elf";});verifySource(false);
    reset();modify(root,manifest,[](JsonDocument& d){d["id"]="other";});verifySource(false);
    reset();modify(root,manifest,[](JsonDocument& d){d["file_name"]="other.elf";});verifySource(false);
    reset();modify(root,manifest,[](JsonDocument& d){d["file_name"]="../other.elf";});verifySource(false);
    reset();modify(root,manifest,[](JsonDocument& d){d["type"]="unexpected";});verifySource(false);
    reset();fs::remove(root/manifest);verifySource(false);
    for(bool absent:{false,true}){
      reset();modify(root,"boot.json",[&](JsonDocument& d){
        if(absent)d[selection][0]["image_source"].as<JsonObject>().remove("internal_fallback");
        else d[selection][0]["image_source"]["internal_fallback"]=false;
        d[selection][0]["image_source"]["path"]="unchanged-legacy-source.elf";
      });strcpy(profile->files[provider?1:2].path,"unrelated.elf");verifySource(true);
    }
    reset();modify(root,"boot.json",[&](JsonDocument& d){d[selection][0].as<JsonObject>().remove("image_source");});verifySource(true);
  }
  reset();
  for(const char* name:{"boot.json","drivers/probe.json","apps/app.json"})for(unsigned failure=0;failure<5;++failure)for(bool close:{false,true}){
    arm(root/name);fault.open=failure==0;fault.read=failure==1;fault.buffer=failure==2;fault.parser=failure==3;fault.close=close;
    bool retained=false;checked(failure==4 && !close,retained);
    assert(fault.matches==1 && fault.opens==(fault.open?0u:1u) && fault.closes==fault.opens);
    assert(!fault.stream && retained==(close && !fault.open));
    if(retained){const unsigned opened=allOpens;checked(false,retained);assert(retained && allOpens==opened && fault.closes==1);}
  }
  arm(root/"unused");bool retained=true;const unsigned opened=allOpens;checked(false,retained);assert(retained && allOpens==opened);
  puts("Preserving SD sources: selected identity and nested fallback inventory hash binding, legacy exclusions, fault matrix and sticky retention PASS");
}
}
extern "C" FILE* __real_fopen(const char*,const char*);
extern "C" int __real_fclose(FILE*);
extern "C" size_t __real_fread(void*,size_t,size_t,FILE*);
extern "C" int __real_feof(FILE*);
extern "C" int __real_ferror(FILE*);
extern "C" void* __real_malloc(size_t);
extern "C" FILE* __wrap_fopen(const char* path,const char* mode){
  if(checking){assert(!strcmp(mode,"rb"));assert(!strstr(path,".elf"));++allOpens;}
  const bool selected=checking && fault.path==path;
  if(selected){++fault.matches;if(fault.open){errno=EIO;return nullptr;}}
  FILE* stream=__real_fopen(path,mode);
  if(selected && stream){assert(!fault.stream);fault.stream=stream;++fault.opens;}
  return stream;
}
extern "C" int __wrap_fclose(FILE* stream){
  const bool selected=checking && stream==fault.stream;
  if(selected){++fault.closes;fault.stream=nullptr;fault.closed=true;}
  const int result=__real_fclose(stream);
  if(selected && fault.close){errno=EIO;return EOF;}return result;
}
extern "C" size_t __wrap_fread(void* data,size_t size,size_t count,FILE* stream){
  if(checking && stream==fault.stream){++fault.reads;if(fault.read){errno=EIO;return 0;}}
  return __real_fread(data,size,count,stream);
}
extern "C" int __wrap_feof(FILE* stream){return checking && stream==fault.stream && fault.read?1:__real_feof(stream);}
extern "C" int __wrap_ferror(FILE* stream){return checking && stream==fault.stream && fault.read?1:__real_ferror(stream);}
extern "C" void* __wrap_malloc(size_t size){
  if(checking && fault.stream && size==65537){++fault.allocations;if(fault.buffer)return nullptr;}
  if(checking && fault.closed && fault.parser)return nullptr;
  return __real_malloc(size);
}
int main(int argc,char** argv){
  setvbuf(stdout,nullptr,_IONBF,0);
  assert(argc==2);const fs::path root=argv[1],old=root/"installed",next=root/"candidate";
  successes(old,next);policyChanges(old,next);contentPaths(old,next);manifestChanges(old,next);cohortChanges(old,next);malformed(old,next);faults(old,next);sourceBindings(next);
  printf("Preserving policy: %u checks; every input store unchanged; metadata-only reads; no module loading PASS\n",checks);
}
