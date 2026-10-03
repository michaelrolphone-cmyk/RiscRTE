#include "Board.h"
namespace RiscBoot {
namespace {
bool num(JsonVariantConst v,int64_t lo,int64_t hi,int64_t& n) { return integer(v,lo,hi,n); }
template<class T> bool number(JsonVariantConst v,int64_t lo,int64_t hi,T& out) {
  int64_t n; if (!num(v,lo,hi,n)) return false; out=static_cast<T>(n); return true;
}
bool flag(JsonVariantConst v,uint8_t& out) {
  if (!v.is<bool>()) return false;
  out=v.as<bool>()?1:0; return true;
}
bool pin(JsonVariantConst v,int16_t& out,bool optional=false) { return number(v,optional?-1:0,48,out); }
}
bool Board::reservePin(int p) { if (loaded_ || p<0 || p>48) return false; pins_[p]=true; return true; }
bool Board::claim(int16_t p,bool optional) {
  if (p==-1 && optional) return true;
  if (p<0 || p>48 || pins_[p]) return false;
  pins_[p]=true; return true;
}
const Board::Device* Board::device(uint64_t id) const {
  for (size_t i=0;i<devicesCount_;++i) if (devices_[i].hardware.instance_id==id) return &devices_[i];
  return nullptr;
}
const risc_hw_bus_v1* Board::bus(uint64_t id) const {
  for (size_t i=0;i<busesCount_;++i) if (buses_[i].instance_id==id) return &buses_[i];
  return nullptr;
}
bool Board::port(JsonObjectConst p) {
  if (loaded_ || portSet_ || !keys(p,{"cpu_compatible","controller_mappings"}) ||
      !eq(p["cpu_compatible"],"espressif,esp32-s3") ||
      !p["controller_mappings"].is<JsonArrayConst>() || p["controller_mappings"].size()>MaxBuses) return fail("invalid CPU port declaration");
  portSet_=true;
  for (JsonObjectConst m:p["controller_mappings"].as<JsonArrayConst>()) {
    auto& entry=mappings_[mappingCount_];
    if (!keys(m,{"bus_instance_id","controller_namespace","physical_controller"}) ||
        !number(m["bus_instance_id"],1,INT32_MAX,entry.bus) ||
        !number(m["physical_controller"],0,3,entry.physical) ||
        (!eq(m["controller_namespace"],"esp32.peripheral") && !eq(m["controller_namespace"],"riscrte.logical"))) return fail("invalid port mapping");
    entry.logical=eq(m["controller_namespace"],"riscrte.logical");
    for (size_t i=0;i<mappingCount_;++i) if (mappings_[i].bus==entry.bus) return fail("duplicate port mapping");
    ++mappingCount_;
  }
  return true;
}
int Board::physicalController(uint64_t id) const {
  for (size_t i=0;i<busesCount_;++i) if (buses_[i].instance_id==id) return physical_[i];
  return -1;
}
uint64_t Board::deviceBus(uint64_t id) const {
  const auto* d=device(id); if (!d) return 0;
  if (!strcmp(d->type,"controller.i2c")) return d->config.i2cController.bus.instance_id;
  if (!strcmp(d->type,"peripheral.i2c")) return d->config.peripheral.bus.instance_id;
  if (!strcmp(d->type,"power.axp2101")) return d->config.power.device.bus.instance_id;
  if (!strcmp(d->type,"display.spi")) return d->config.display.bus.instance_id;
  if (!strcmp(d->type,"touch.i2c")) return d->config.touch.bus.instance_id;
  if (!strcmp(d->type,"storage.sd-spi")) return d->config.sd.bus.instance_id;
  if (!strcmp(d->type,"radio.lora")) return d->config.lora.bus.instance_id;
  return 0;
}
bool Board::address(uint64_t busId,uint8_t value) {
  if (addressCount_==MaxDevices) return false;
  for (size_t i=0;i<addressCount_;++i) if (addresses_[i].bus==busId && addresses_[i].value==value) return false;
  addresses_[addressCount_++]={busId,value}; return true;
}
bool Board::i2cDevice(JsonObjectConst c,tw_hw_i2c_device_v1& x) {
  uint64_t id;
  if (!keys(c,{"bus_instance_id","address","chip_id","irq","irq_active_high","irq_pull_up"}) ||
      !number(c["bus_instance_id"],1,INT32_MAX,id)) return false;
  const auto* b=bus(id); if (!b || b->kind!=RISC_HW_BUS_I2C) return false;
  x={}; x.struct_size=sizeof(x); x.bus=*b;
  return number(c["address"],8,119,x.address) && address(id,x.address) && number(c["chip_id"],0,255,x.chip_id) &&
    pin(c["irq"],x.irq,true) && claim(x.irq,true) && flag(c["irq_active_high"],x.irq_active_high) && flag(c["irq_pull_up"],x.irq_pull_up);
}
bool Board::materialize(JsonObjectConst c,Device& d) {
  if (!strcmp(d.type,"gpio.bank")) {
    if (!keys(c,{"pins","active_high","pull_up","debounce_us","long_press_us","click_min_us"})) return false;
    auto& x=d.config.gpio; x={}; x.struct_size=sizeof(x);
    if (!c["pins"].is<JsonArrayConst>()) return false;
    JsonArrayConst a=c["pins"]; if (!a.size() || a.size()>8) return false;
    x.count=a.size(); for (size_t i=0;i<a.size();++i) if (!pin(a[i],x.pins[i]) || !claim(x.pins[i])) return false;
    if (!flag(c["active_high"],x.active_high) || !flag(c["pull_up"],x.pull_up) ||
        !number(c["debounce_us"],0,10000000,x.debounce_us) || !number(c["long_press_us"],0,60000000,x.long_press_us) ||
        !number(c["click_min_us"],0,60000000,x.click_min_us)) return false;
    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  if (!strcmp(d.type,"pixel.ws2812")) {
    if (!keys(c,{"pin","count","order"})) return false;
    auto& x=d.config.pixel; x={}; x.struct_size=sizeof(x);
    if (!pin(c["pin"],x.pin) || !claim(x.pin) || !number(c["count"],1,16,x.count) || !number(c["order"],0,1,x.order)) return false;
    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  if (!strcmp(d.type,"radio.integrated")) {
    if (!keys(c,{"unit","features"})) return false;
    auto& x=d.config.radio; x={}; x.struct_size=sizeof(x);
    if (!number(c["unit"],0,0,x.unit) || !number(c["features"],1,3,x.features)) return false;
    for (size_t i=0;i<devicesCount_;++i)
      if (!strcmp(devices_[i].type,"radio.integrated") && devices_[i].config.radio.unit==x.unit && !strcmp(devices_[i].compatible,d.compatible)) return false;
    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  if (!strcmp(d.type,"input.quadrature")) {
    if (!keys(c,{"a","b","pull_up","edges_per_detent","direction","debounce_us","button_instance_id"})) return false;
    auto& x=d.config.quadrature; x={}; x.struct_size=sizeof(x);
    if (!pin(c["a"],x.a) || !pin(c["b"],x.b) || !claim(x.a) || !claim(x.b) || !flag(c["pull_up"],x.pull_up) ||
        !number(c["edges_per_detent"],1,16,x.edges_per_detent) || !number(c["direction"],-1,1,x.direction) || !x.direction ||
        !number(c["debounce_us"],1,1000000,x.debounce_us) || !number(c["button_instance_id"],1,INT32_MAX,x.button_instance_id)) return false;
    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  if (!strcmp(d.type,"controller.gpio")) {
    auto& x=d.config.gpioController; x={}; x.struct_size=sizeof(x);
    if (!keys(c,{"unit","features"}) || !number(c["unit"],0,0,x.unit) || !number(c["features"],0,0,x.features)) return false;
    for (size_t i=0;i<devicesCount_;++i) if (!strcmp(devices_[i].type,d.type)) return false;
    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  if (!strcmp(d.type,"audio.i2s")) {
    auto& x=d.config.audio; x={}; x.struct_size=sizeof(x);
    if (!keys(c,{"controller","pdm_rx","bclk","ws","data"}) || !number(c["controller"],0,1,x.controller) || !flag(c["pdm_rx"],x.pdm_rx) ||
        !pin(c["bclk"],x.bclk) || !pin(c["data"],x.data) || !pin(c["ws"],x.ws,true) ||
        (x.pdm_rx ? (x.controller!=0 || x.ws!=-1) : x.ws<0) || !claim(x.bclk) || !claim(x.ws,true) || !claim(x.data)) return false;
    for (size_t i=0;i<devicesCount_;++i) if (!strcmp(devices_[i].type,d.type) && devices_[i].config.audio.controller==x.controller) return false;
    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  if (!strcmp(d.type,"peripheral.i2c")) {
    auto& x=d.config.peripheral; if (!i2cDevice(c,x)) return false;
    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  if (!strcmp(d.type,"power.axp2101")) {
    auto& x=d.config.power; x={};
    if (!keys(c,{"device","charge_ma","rails"}) || !i2cDevice(c["device"],x.device) ||
        !number(c["charge_ma"],0,65535,x.charge_ma) || !c["rails"].is<JsonArrayConst>() || c["rails"].size()>4) return false;
    x.device.struct_size=sizeof(x);
    for (JsonObjectConst rail:c["rails"].as<JsonArrayConst>()) {
      auto& r=x.rails[x.rail_count];
      if (!keys(rail,{"id","millivolts"}) || !number(rail["id"],1,255,r.id) || !number(rail["millivolts"],1,65535,r.millivolts)) return false;
      for (size_t i=0;i<x.rail_count;++i) if (x.rails[i].id==r.id) return false;
      ++x.rail_count;
    }
    // Electrical authorization/charge limits remain in the trusted port and PMU driver.
    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  uint64_t busId=0;
  if (!number(c["bus_instance_id"],1,INT32_MAX,busId)) return false;
  const auto* b=bus(busId); if (!b) return false;
  if (!strcmp(d.type,"controller.i2c")) {
    auto& x=d.config.i2cController; x={}; x.struct_size=sizeof(x); x.bus=*b;
    if (!keys(c,{"bus_instance_id"}) || b->kind!=RISC_HW_BUS_I2C) return false;
    for (size_t i=0;i<devicesCount_;++i) if (!strcmp(devices_[i].type,d.type) && devices_[i].config.i2cController.bus.instance_id==busId) return false;
    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  if (!strcmp(d.type,"radio.lora")) {
    auto& x=d.config.lora; x={}; x.struct_size=sizeof(x); x.bus=*b;
    if (b->kind!=RISC_HW_BUS_SPI || !keys(c,{"bus_instance_id","cs","reset","busy","irq","minimum_hz","maximum_hz","tcxo_voltage","reset_active_high","busy_active_high","irq_active_high"}) ||
        !pin(c["cs"],x.cs) || !pin(c["reset"],x.reset) || !pin(c["busy"],x.busy) || !pin(c["irq"],x.irq) ||
        !claim(x.cs) || !claim(x.reset) || !claim(x.busy) || !claim(x.irq) ||
        !number(c["minimum_hz"],1,UINT32_MAX,x.minimum_hz) || !number(c["maximum_hz"],x.minimum_hz,UINT32_MAX,x.maximum_hz) ||
        !number(c["tcxo_voltage"],0,7,x.tcxo_voltage) || !flag(c["reset_active_high"],x.reset_active_high) ||
        !flag(c["busy_active_high"],x.busy_active_high) || !flag(c["irq_active_high"],x.irq_active_high)) return false;
    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  if (!strcmp(d.type,"storage.sd-spi")) {
    if (!keys(c,{"bus_instance_id","cs","detect","write_protect","detect_active_high","write_protect_active_high"}) || b->kind!=RISC_HW_BUS_SPI) return false;
    auto& x=d.config.sd; x={}; x.struct_size=sizeof(x); x.bus=*b;
    if (!pin(c["cs"],x.cs) || !pin(c["detect"],x.detect,true) || !pin(c["write_protect"],x.write_protect,true) ||
        !claim(x.cs) || !claim(x.detect,true) || !claim(x.write_protect,true) ||
        !flag(c["detect_active_high"],x.detect_active_high) || !flag(c["write_protect_active_high"],x.write_protect_active_high)) return false;
    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  if (!strcmp(d.type,"touch.i2c")) {
    if (!keys(c,{"bus_instance_id","width","height","address","reset_active_high","irq_active_high","irq_pull_up","reset","irq","reset_assert_ms","reset_recovery_ms"}) || b->kind!=RISC_HW_BUS_I2C) return false;
    auto& x=d.config.touch; x={}; x.struct_size=sizeof(x); x.bus=*b;
    if (!number(c["width"],1,4096,x.width) || !number(c["height"],1,4096,x.height) || !number(c["address"],8,119,x.address) ||
        !pin(c["reset"],x.reset,true) || !pin(c["irq"],x.irq) || !claim(x.reset,true) || !claim(x.irq) ||
        !flag(c["reset_active_high"],x.reset_active_high) || !flag(c["irq_active_high"],x.irq_active_high) || !flag(c["irq_pull_up"],x.irq_pull_up) ||
        !number(c["reset_assert_ms"],x.reset<0?0:1,x.reset<0?0:500,x.reset_assert_ms) || !number(c["reset_recovery_ms"],x.reset<0?0:1,x.reset<0?0:500,x.reset_recovery_ms)) return false;
    if (!address(busId,x.address)) return false;
    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  if (!strcmp(d.type,"display.spi")) {
    if (!keys(c,{"bus_instance_id","width","height","offset_x","offset_y","rotation","cs","dc","reset","backlight","busy","reset_active_high","busy_active_high","backlight_active_high","power_pins","power_active_high","reset_assert_ms","reset_recovery_ms"}) || b->kind!=RISC_HW_BUS_SPI) return false;
    auto& x=d.config.display; x={}; x.struct_size=sizeof(x); x.bus=*b;
    if (!number(c["width"],1,4096,x.width) || !number(c["height"],1,4096,x.height) ||
        !number(c["offset_x"],0,4095,x.offset_x) || !number(c["offset_y"],0,4095,x.offset_y) || !number(c["rotation"],0,3,x.rotation) ||
        !pin(c["cs"],x.cs) || !pin(c["dc"],x.dc) || !pin(c["reset"],x.reset,true) || !pin(c["backlight"],x.backlight,true) || !pin(c["busy"],x.busy,true) ||
        !claim(x.cs) || !claim(x.dc) || !claim(x.reset,true) || !claim(x.backlight,true) || !claim(x.busy,true) ||
        !flag(c["reset_active_high"],x.reset_active_high) || !flag(c["busy_active_high"],x.busy_active_high) || !flag(c["backlight_active_high"],x.backlight_active_high) ||
        !number(c["reset_assert_ms"],x.reset<0?0:1,x.reset<0?0:500,x.reset_assert_ms) || !number(c["reset_recovery_ms"],x.reset<0?0:1,x.reset<0?0:500,x.reset_recovery_ms)) return false;
    if (!c["power_pins"].is<JsonArrayConst>() || !c["power_active_high"].is<JsonArrayConst>()) return false;
    JsonArrayConst pins=c["power_pins"], high=c["power_active_high"];
    if (pins.size()>4 || pins.size()!=high.size()) return false;
    x.power_count=pins.size();
    for (size_t i=0;i<pins.size();++i) if (!pin(pins[i],x.power_pins[i]) || !claim(x.power_pins[i]) || !flag(high[i],x.power_active_high[i])) return false;
    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  return false; // Extension types require their exact registered materializer.
}
bool Board::load(JsonObjectConst root) {
  if (loaded_) return fail("board object is single-use");
  loaded_=true;
  if (!keys(root,{"schema","schema_version","board_id","revision","buses","devices"}) ||
      !eq(root["schema"],"riscrte.board-hardware") || !root["schema_version"].is<unsigned>() || root["schema_version"].as<unsigned>()!=1 ||
      !text(root["board_id"],boardId_,sizeof(boardId_)) || !text(root["revision"],revision_,sizeof(revision_)) ||
      !root["buses"].is<JsonArrayConst>() || !root["devices"].is<JsonArrayConst>()) return fail("invalid board envelope");
  JsonArrayConst buses=root["buses"], devices=root["devices"];
  if (buses.size()>MaxBuses || devices.size()>MaxDevices) return fail("board bounds exceeded");
  for (JsonObjectConst record : buses) {
    if (!keys(record,{"instance_id","kind","controller","frequency_hz","mode","pins"},{"controller_namespace","physical_controller"})) return fail("invalid bus fields");
    auto& b=buses_[busesCount_]; b.struct_size=sizeof(b); b.sclk=b.mosi=b.miso=b.sda=b.scl=-1;
    if (!number(record["instance_id"],1,INT32_MAX,b.instance_id) || bus(b.instance_id) ||
        !number(record["frequency_hz"],1,10000000,b.frequency_hz) || !number(record["mode"],0,3,b.mode)) return fail("invalid bus identity/timing");
    bool declared=false, logical=false; uint32_t physical=0;
    for (size_t i=0;i<mappingCount_;++i) if (mappings_[i].bus==b.instance_id) {
      declared=true; logical=mappings_[i].logical; physical=mappings_[i].physical;
    }
    if (!record["controller_namespace"].isNull()) {
      const bool inLogical=eq(record["controller_namespace"],"riscrte.logical");
      uint32_t inPhysical=0;
      if ((!inLogical && !eq(record["controller_namespace"],"esp32.peripheral")) ||
          !number(record[inLogical || !record["physical_controller"].isNull()?"physical_controller":"controller"],0,3,inPhysical) ||
          (declared && (logical!=inLogical || physical!=inPhysical))) return fail("conflicting controller mapping");
      declared=true; logical=inLogical; physical=inPhysical;
    } else if (!record["physical_controller"].isNull()) return fail("physical controller needs namespace");
    if (!declared || !number(record["controller"],0,3,b.controller) || (!logical && physical!=b.controller)) return fail("explicit controller mapping required");
    physical_[busesCount_]=physical;
    JsonObjectConst p=record["pins"];
    if (eq(record["kind"],"spi")) {
      b.kind=RISC_HW_BUS_SPI;
      if ((logical ? b.controller>1 : b.controller<2) || physical<2 || physical>3 || !keys(p,{"sclk","mosi","miso"},{"sda","scl"}) ||
          (!p["sda"].isUnbound() && (!p["sda"].is<int>() || p["sda"].as<int>()!=-1)) || (!p["scl"].isUnbound() && (!p["scl"].is<int>() || p["scl"].as<int>()!=-1)) ||
          !pin(p["sclk"],b.sclk) || !pin(p["mosi"],b.mosi) || !pin(p["miso"],b.miso,true) ||
          !claim(b.sclk) || !claim(b.mosi) || !claim(b.miso,true)) return fail("invalid/conflicting SPI bus");
    } else if (eq(record["kind"],"i2c")) {
      b.kind=RISC_HW_BUS_I2C;
      if (b.controller>1 || physical>1 || b.mode || b.frequency_hz>1000000 || !keys(p,{"sda","scl"},{"sclk","mosi","miso"}) ||
          (!p["sclk"].isUnbound() && (!p["sclk"].is<int>() || p["sclk"].as<int>()!=-1)) ||
          (!p["mosi"].isUnbound() && (!p["mosi"].is<int>() || p["mosi"].as<int>()!=-1)) ||
          (!p["miso"].isUnbound() && (!p["miso"].is<int>() || p["miso"].as<int>()!=-1)) ||
          !pin(p["sda"],b.sda) || !pin(p["scl"],b.scl) || !claim(b.sda) || !claim(b.scl)) return fail("invalid/conflicting I2C bus");
    } else return fail("unknown bus kind");
    for (size_t i=0;i<busesCount_;++i) if (buses_[i].kind==b.kind && (buses_[i].controller==b.controller || physical_[i]==physical)) return fail("duplicate controller");
    ++busesCount_;
  }
  for (size_t i=0;i<mappingCount_;++i) if (!bus(mappings_[i].bus)) return fail("unused port mapping");
  for (JsonObjectConst record : devices) {
    if (!keys(record,{"instance_id","chip","compatible","config_type","config_version","config"},{"bindings"})) return fail("invalid device fields");
    auto& d=devices_[devicesCount_]; auto& h=d.hardware;
    h.api_version=1; h.struct_size=sizeof(h); h.compatible=d.compatible; h.revision=d.revision; h.config_type=d.type;
    JsonObjectConst chip=record["chip"]; char vendor[96]{}, model[96]{};
    if (!number(record["instance_id"],1,INT32_MAX,h.instance_id) || device(h.instance_id) ||
        !keys(chip,{"vendor","model","revision"}) || !text(chip["vendor"],vendor,sizeof(vendor)) || !text(chip["model"],model,sizeof(model)) ||
        !text(chip["revision"],d.revision,sizeof(d.revision)) || !text(record["compatible"],d.compatible,sizeof(d.compatible)) ||
        !text(record["config_type"],d.type,sizeof(d.type)) || !number(record["config_version"],1,1,h.config_version) ||
        !materialize(record["config"],d)) return fail("invalid/unsupported/conflicting device config");
    if (!record["bindings"].isNull()) {
      if (!record["bindings"].is<JsonObjectConst>() || record["bindings"].size()>16) return fail("invalid bindings");
      for (JsonPairConst pair : record["bindings"].as<JsonObjectConst>()) {
        auto& binding=d.bindings[d.bindingCount++];
        const char* name=pair.key().c_str(); if (!*name || strlen(name)>=sizeof(binding.capability)) return fail("invalid binding name");
        strcpy(binding.capability,name);
        if (!number(pair.value(),1,INT32_MAX,binding.instance) || binding.instance==h.instance_id) return fail("invalid binding target");
      }
    }
    ++devicesCount_;
  }
  for (size_t i=0;i<devicesCount_;++i) {
    const auto& d=devices_[i];
    for (size_t j=0;j<d.bindingCount;++j) if (!device(d.bindings[j].instance)) return fail("missing binding instance");
    if (!strcmp(d.type,"input.quadrature")) {
      bool found=false; for (size_t j=0;j<d.bindingCount;++j) if (!strcmp(d.bindings[j].capability,"input.button") && d.bindings[j].instance==d.config.quadrature.button_instance_id) found=true;
      if (!found) return fail("quadrature button binding missing");
    }
  }
  identity_={2,sizeof(identity_),boardId_,revision_}; return true;
}
}
