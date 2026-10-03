#include "bootstrap/Runtime.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
using namespace RiscBoot;
static bool owner(){return true;}
static bool health(risc_runtime_health_v1*){return false;}
static void delay(uint32_t){}
static bool diagnostic(const char*){return true;}
static void save(const std::string& path,const JsonDocument& d){std::ofstream f(path);serializeJson(d,f);}
int main(int argc,char** argv) {
  assert(argc==3); std::string fixtures=argv[1],temp=argv[2]; unsigned profiles=0;
  for(const auto& file:std::filesystem::directory_iterator(fixtures+"/hardware")) {
    JsonDocument board;assert(readJson(file.path().c_str(),board));
    Board materialized;assert(materialized.load(board.as<JsonObjectConst>()));
    assert(materialized.bus(103)->controller==0 && materialized.physicalController(103)==2);
    assert(materialized.bus(104)->controller==1 && materialized.physicalController(104)==3);
    assert(materialized.device(4)->config.power.device.struct_size==sizeof(tw_hw_axp2101_v1));
    assert(materialized.device(5)->config.display.reset==-1 && !materialized.device(5)->config.display.reset_assert_ms);
    assert(materialized.device(15) && materialized.device(16)); // WiFi/BLE are separate family resources.
    JsonDocument boot;boot["board"]="board.json";boot["default_app"]="default.elf";
    auto selected=boot["drivers"].to<JsonArray>();
    Runtime runtime({owner,health,delay,diagnostic});
    static const uint32_t rawHeader[]={1,8}; // Admission only; never activated here.
    assert(runtime.registerPlatform("platform.clock",1,Runtime::Scope::Global,0,rawHeader));
    for(JsonObjectConst device:board["devices"].as<JsonArrayConst>()) {
      bool matched=false;
      for(const auto& directory:std::filesystem::directory_iterator(fixtures+"/drivers")) {
        JsonDocument manifest;assert(readJson((directory.path()/"manifest.json").c_str(),manifest));
        bool compatible=false;
        for(JsonObjectConst entry:manifest["hardware_compatibility"].as<JsonArrayConst>())
          if(eq(entry["compatible"],device["compatible"].as<const char*>()) && eq(entry["config_type"],device["config_type"].as<const char*>())) compatible=true;
        if(!compatible) continue;
        assert(!matched);matched=true;
        const std::string folder=directory.path().filename().string();
        std::filesystem::create_directories(temp+"/"+folder);save(temp+"/"+folder+"/manifest.json",manifest);
        auto item=selected.add<JsonObject>();item["manifest"]=folder+"/manifest.json";item["instance_id"]=device["instance_id"];
        for(JsonObjectConst req:manifest["requires"].as<JsonArrayConst>()) {
          const char* cap=req["capability"];
          if((!strncmp(cap,"platform.",9) || !strcmp(cap,"spi.bus")) && strcmp(cap,"platform.clock"))
            assert(runtime.registerPlatform(cap,1,Runtime::Scope::Device,device["instance_id"].as<uint64_t>(),rawHeader));
        }
      }
      assert(matched);
    }
    save(temp+"/board.json",board);save(temp+"/boot.json",boot);
    if(!runtime.prepare(temp.c_str())) {fprintf(stderr,"%s: %s\n",file.path().c_str(),runtime.error());return 1;}
    {Runtime absent({owner,health,delay,diagnostic});assert(!absent.prepare(temp.c_str()));}
    // Missing metadata is not inferred from a board name/controller value.
    JsonDocument changed=board;changed["buses"][2].remove("controller_namespace");changed["buses"][2].remove("physical_controller");
    {Board b;assert(!b.load(changed.as<JsonObjectConst>()));}
    // Explicit legacy override is supported and a conflicting one is rejected.
    JsonDocument port;port["cpu_compatible"]="espressif,esp32-s3";
    auto m=port["controller_mappings"].to<JsonArray>().add<JsonObject>();m["bus_instance_id"]=103;m["controller_namespace"]="riscrte.logical";m["physical_controller"]=2;
    {Board b;assert(b.port(port.as<JsonObjectConst>()) && b.load(changed.as<JsonObjectConst>()));}
    port["controller_mappings"][0]["physical_controller"]=3;
    {Board b;assert(b.port(port.as<JsonObjectConst>()) && !b.load(board.as<JsonObjectConst>()));}
    changed=board;changed["buses"][3]["physical_controller"]=2;
    {Board b;assert(!b.load(changed.as<JsonObjectConst>()));}
    changed=board;changed["buses"][0]["pins"]["sclk"]=4;
    {Board b;assert(!b.load(changed.as<JsonObjectConst>()));}
    changed=board;changed["devices"][4]["config"]["reset_assert_ms"]=1;
    {Board b;assert(!b.load(changed.as<JsonObjectConst>()));}
    changed=board;changed["devices"][4]["config"]["reset"]=42;
    {Board b;assert(!b.load(changed.as<JsonObjectConst>()));}
    changed=board;changed["devices"][4]["config"]["reset"]=42;changed["devices"][4]["config"]["reset_assert_ms"]=1;changed["devices"][4]["config"]["reset_recovery_ms"]=1;
    {Board b;assert(b.load(changed.as<JsonObjectConst>()));}
    {Board b;assert(b.reservePin(44) && !b.load(board.as<JsonObjectConst>()));} // Console policy is port-owned.
    ++profiles;
  }
  assert(profiles==8);puts("Eight pinned Watch profiles/manifests: typed layouts, exact DAG admission, explicit namespaces and resource conflicts PASS");
}
