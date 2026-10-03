#include "bootstrap/Board.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace RiscBoot;
int main(int argc,char** argv) {
  for(int i=1;i<argc;++i) {
    JsonDocument doc;assert(readJson(argv[i],doc));
    auto board=std::make_unique<Board>();
    if(!board->load(doc.as<JsonObjectConst>())) { fprintf(stderr,"%s: %s\n",argv[i],board->error());return 1; }
    printf("Shared board manifest materialization PASS: %s\n",board->identity()->board_id);
  }
  const char* fixture=R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[{"instance_id":1,"kind":"spi","controller":2,"frequency_hz":10000000,"mode":0,"pins":{"sclk":4,"mosi":5,"miso":6}}],"devices":[{"instance_id":7,"chip":{"vendor":"test","model":"sd","revision":"unspecified"},"compatible":"test,sd","config_type":"storage.sd-spi","config_version":1,"config":{"bus_instance_id":1,"cs":7,"detect":-1,"write_protect":-1,"detect_active_high":true,"write_protect_active_high":false}}]})";
  JsonDocument doc;assert(parse(fixture,strlen(fixture),doc));
  { Board board;assert(board.load(doc.as<JsonObjectConst>()));
    const auto* hw=board.device(7);assert(hw && hw->hardware.config_size==sizeof(risc_hw_sd_spi_v1));
    assert(hw->config.sd.bus.instance_id==1 && hw->config.sd.bus.sclk==4 && hw->config.sd.cs==7);
  }
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
  assert(!utf8("\xc0\x80",2));assert(!utf8("\xed\xa0\x80",3));assert(utf8("\xe2\x82\xac",3));
  puts("Board conflicts, absent pins, strict types, unsupported extensions and UTF-8 PASS");
}
