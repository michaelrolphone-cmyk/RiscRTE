#include "bootstrap/Board.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
using namespace RiscBoot;
int main(int argc,char** argv) {
  for(int i=1;i<argc;++i) {
    JsonDocument doc;assert(readJson(argv[i],doc));
    auto board=std::make_unique<Board>();
    if(!board->load(doc.as<JsonObjectConst>())) { fprintf(stderr,"%s: %s\n",argv[i],board->error());return 1; }
    printf("Shared board manifest materialization PASS: %s\n",board->identity()->board_id);
  }
  const char* fixture=R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[{"instance_id":1,"kind":"spi","controller_namespace":"esp32.peripheral","controller":2,"frequency_hz":10000000,"mode":0,"pins":{"sclk":4,"mosi":5,"miso":6}}],"devices":[{"instance_id":7,"chip":{"vendor":"test","model":"sd","revision":"unspecified"},"compatible":"test,sd","config_type":"storage.sd-spi","config_version":1,"config":{"bus_instance_id":1,"cs":7,"detect":-1,"write_protect":-1,"detect_active_high":true,"write_protect_active_high":false}}]})";
  JsonDocument doc;assert(parse(fixture,strlen(fixture),doc));
  { Board board;assert(board.load(doc.as<JsonObjectConst>()));
    const auto* hw=board.device(7);assert(hw && hw->hardware.config_size==sizeof(risc_hw_sd_spi_v1));
    assert(hw->config.sd.bus.instance_id==1 && hw->config.sd.bus.sclk==4 && hw->config.sd.cs==7);
  }
  // SPI authorization is typed and explicit, not a global bus-clock lift.
  for(unsigned hz:{1u,10000000u,40000000u}) {
    assert(parse(fixture,strlen(fixture),doc));doc["buses"][0]["frequency_hz"]=hz;
    Board b;assert(b.load(doc.as<JsonObjectConst>()));
    assert(b.bus(1)->frequency_hz==hz && b.device(7)->config.sd.bus.frequency_hz==hz);
  }
  for(unsigned hz:{0u,40000001u,UINT32_MAX}) {
    assert(parse(fixture,strlen(fixture),doc));doc["buses"][0]["frequency_hz"]=hz;
    Board b;assert(!b.load(doc.as<JsonObjectConst>()));
  }
  for(unsigned hz:{400000u,1000000u,1000001u,40000000u}) {
    assert(parse(fixture,strlen(fixture),doc));doc["devices"].as<JsonArray>().clear();
    auto bus=doc["buses"][0].as<JsonObject>();bus["kind"]="i2c";bus["controller"]=0;bus["frequency_hz"]=hz;
    bus["pins"].as<JsonObject>().clear();bus["pins"]["sda"]=4;bus["pins"]["scl"]=5;
    Board b;assert(b.load(doc.as<JsonObjectConst>())==(hz<=1000000));
  }
  assert(parse(fixture,strlen(fixture),doc));
  // A CS may not alias shared bus signals; missing mandatory pin stays missing.
  doc["devices"][0]["config"]["cs"]=4;
  {Board b;assert(!b.load(doc.as<JsonObjectConst>()));}
  assert(parse(fixture,strlen(fixture),doc));doc["buses"][0]["pins"].remove("sclk");
  {Board b;assert(!b.load(doc.as<JsonObjectConst>()));}
  assert(parse(fixture,strlen(fixture),doc));doc["devices"][0]["config"]["detect_active_high"]="true";
  {Board b;assert(!b.load(doc.as<JsonObjectConst>()));}
  assert(parse(fixture,strlen(fixture),doc));doc["devices"][0]["config_type"]="audio.i2s";
  {Board b;assert(!b.load(doc.as<JsonObjectConst>()));}
  assert(parse(fixture,strlen(fixture),doc));doc["buses"].as<JsonArray>().add(doc["buses"][0]);
  {Board b;assert(!b.load(doc.as<JsonObjectConst>()));}
  // Decoded binding compaction must not truncate or narrow admission bounds.
  for(uint64_t id:{UINT64_C(1),UINT64_C(2147483647)}){
    assert(parse(fixture,strlen(fixture),doc));
    auto other=doc["devices"].as<JsonArray>().add<JsonObject>();other.set(doc["devices"][0]);
    // Bus uses ID1; choose a different bus so binding target1 is still valid.
    doc["buses"][0]["instance_id"]=2;doc["devices"][0]["config"]["bus_instance_id"]=2;
    other["instance_id"]=id;other["config"]["bus_instance_id"]=2;other["config"]["cs"]=8;
    doc["devices"][0]["bindings"]["test.peer"]=id;
    Board b;assert(b.load(doc.as<JsonObjectConst>()));
    assert(b.device(7)->bindings[0].instance==id && b.device(id)->hardware.instance_id==id);
  }
  for(uint64_t bad:{UINT64_C(0),UINT64_C(2147483648),UINT64_C(4294967295),UINT64_C(4294967297),UINT64_MAX}){
    assert(parse(fixture,strlen(fixture),doc));doc["devices"][0]["bindings"]["test.peer"]=bad;
    Board b;assert(!b.load(doc.as<JsonObjectConst>()));
  }
  for(const char* bad:{"-1","1.5","\"7\""}){
    assert(parse(fixture,strlen(fixture),doc));JsonDocument invalid;std::string wrapped=std::string("{\"value\":")+bad+"}";assert(parse(wrapped.data(),wrapped.size(),invalid));
    doc["devices"][0]["bindings"]["test.peer"].set(invalid["value"]);
    Board b;assert(!b.load(doc.as<JsonObjectConst>()));
  }
  assert(!utf8("\xc0\x80",2));assert(!utf8("\xed\xa0\x80",3));assert(utf8("\xe2\x82\xac",3));
  puts("Board conflicts, absent pins, strict types, unsupported extensions and UTF-8 PASS");
}
