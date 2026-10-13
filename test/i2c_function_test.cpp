#include "bootstrap/Board.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
using namespace RiscBoot;
static const char *fixture=R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"multi-function-test","revision":"unspecified","buses":[{"instance_id":101,"kind":"i2c","controller_namespace":"esp32.peripheral","controller":0,"physical_controller":0,"frequency_hz":400000,"mode":0,"pins":{"sda":15,"scl":14}}],"devices":[{"instance_id":13,"chip":{"vendor":"test","model":"function","revision":"unspecified"},"compatible":"test,function","config_type":"peripheral.i2c-function","config_version":1,"config":{"parent_instance_id":4,"function_id":2},"bindings":{}},{"instance_id":4,"chip":{"vendor":"test","model":"multifunction","revision":"unspecified"},"compatible":"test,multifunction","config_type":"peripheral.i2c","config_version":1,"config":{"bus_instance_id":101,"address":52,"chip_id":74,"irq":10,"irq_active_high":true,"irq_pull_up":false},"bindings":{}}]})";
static void fresh(JsonDocument& doc){assert(parse(fixture,strlen(fixture),doc));}
static bool load(JsonDocument& doc){auto b=std::make_unique<Board>();return b->load(doc.as<JsonObjectConst>());}
int main(){
 JsonDocument doc;
 for(unsigned reverse=0;reverse<2;reverse++){
  fresh(doc);if(reverse){JsonDocument tmp;tmp.set(doc["devices"][0]);doc["devices"][0].set(doc["devices"][1]);doc["devices"][1].set(tmp);}
  auto b=std::make_unique<Board>();assert(b->load(doc.as<JsonObjectConst>()));
  const auto* d=b->device(13);assert(d&&d->hardware.config_size==sizeof(risc_hw_i2c_function_v1));
  const auto& f=d->config.i2cFunction;assert(f.struct_size==sizeof(f)&&f.parent_instance_id==4&&f.function_id==2&&f.address==52&&f.chip_id==74&&!f.reserved);
  assert(b->deviceBus(13)==101&&f.bus.sda==15&&f.bus.scl==14);
 }
 for(unsigned id:{0u,13u,999u}){fresh(doc);doc["devices"][0]["config"]["parent_instance_id"]=id;assert(!load(doc));}
 fresh(doc);doc["devices"][0]["config"]["address"]=53;assert(!load(doc));
 fresh(doc);auto duplicate=doc["devices"].as<JsonArray>().add<JsonObject>();duplicate.set(doc["devices"][0]);duplicate["instance_id"]=14;assert(!load(doc));
 duplicate["config"]["function_id"]=3;assert(load(doc));
 doc["devices"][0]["config"]["parent_instance_id"]=14;assert(!load(doc));
 fresh(doc);doc["devices"][1]["config_type"]="controller.i2c";doc["devices"][1]["config"].as<JsonObject>().clear();doc["devices"][1]["config"]["bus_instance_id"]=101;assert(!load(doc));
 fresh(doc);auto second=doc["devices"].as<JsonArray>().add<JsonObject>();second.set(doc["devices"][1]);second["instance_id"]=14;second["config"]["irq"]=-1;assert(!load(doc));
 puts("Generic I2C function: order-independent parent/bus/address, distinct functions, missing/self/function parents and duplicate physical address rejection PASS");
}
