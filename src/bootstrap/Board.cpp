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
      if (!strcmp(devices_[i].type,"radio.integrated") && devices_[i].config.radio.unit==x.unit) return false;
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
  uint64_t busId=0;
  if (!number(c["bus_instance_id"],1,INT32_MAX,busId)) return false;
  const auto* b=bus(busId); if (!b) return false;
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
        !pin(c["reset"],x.reset) || !pin(c["irq"],x.irq) || !claim(x.reset) || !claim(x.irq) ||
        !flag(c["reset_active_high"],x.reset_active_high) || !flag(c["irq_active_high"],x.irq_active_high) || !flag(c["irq_pull_up"],x.irq_pull_up) ||
        !number(c["reset_assert_ms"],1,500,x.reset_assert_ms) || !number(c["reset_recovery_ms"],1,500,x.reset_recovery_ms)) return false;
    for (size_t i=0;i<devicesCount_;++i) if (!strcmp(devices_[i].type,"touch.i2c") &&
        devices_[i].config.touch.bus.instance_id==busId && devices_[i].config.touch.address==x.address) return false;
    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  if (!strcmp(d.type,"display.spi")) {
    if (!keys(c,{"bus_instance_id","width","height","offset_x","offset_y","rotation","cs","dc","reset","backlight","busy","reset_active_high","busy_active_high","backlight_active_high","power_pins","power_active_high","reset_assert_ms","reset_recovery_ms"}) || b->kind!=RISC_HW_BUS_SPI) return false;
    auto& x=d.config.display; x={}; x.struct_size=sizeof(x); x.bus=*b;
    if (!number(c["width"],1,4096,x.width) || !number(c["height"],1,4096,x.height) ||
        !number(c["offset_x"],0,4095,x.offset_x) || !number(c["offset_y"],0,4095,x.offset_y) || !number(c["rotation"],0,3,x.rotation) ||
        !pin(c["cs"],x.cs) || !pin(c["dc"],x.dc) || !pin(c["reset"],x.reset) || !pin(c["backlight"],x.backlight,true) || !pin(c["busy"],x.busy,true) ||
        !claim(x.cs) || !claim(x.dc) || !claim(x.reset) || !claim(x.backlight,true) || !claim(x.busy,true) ||
        !flag(c["reset_active_high"],x.reset_active_high) || !flag(c["busy_active_high"],x.busy_active_high) || !flag(c["backlight_active_high"],x.backlight_active_high) ||
        !number(c["reset_assert_ms"],1,500,x.reset_assert_ms) || !number(c["reset_recovery_ms"],1,500,x.reset_recovery_ms)) return false;
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
    if (!keys(record,{"instance_id","kind","controller","frequency_hz","mode","pins"})) return fail("invalid bus fields");
    auto& b=buses_[busesCount_]; b.struct_size=sizeof(b); b.sclk=b.mosi=b.miso=b.sda=b.scl=-1;
    if (!number(record["instance_id"],1,INT32_MAX,b.instance_id) || bus(b.instance_id) ||
        !number(record["frequency_hz"],1,10000000,b.frequency_hz) || !number(record["mode"],0,3,b.mode)) return fail("invalid bus identity/timing");
    JsonObjectConst p=record["pins"];
    if (eq(record["kind"],"spi")) {
      b.kind=RISC_HW_BUS_SPI;
      if (!number(record["controller"],2,3,b.controller) || !keys(p,{"sclk","mosi","miso"}) ||
          !pin(p["sclk"],b.sclk) || !pin(p["mosi"],b.mosi) || !pin(p["miso"],b.miso,true) ||
          !claim(b.sclk) || !claim(b.mosi) || !claim(b.miso,true)) return fail("invalid/conflicting SPI bus");
    } else if (eq(record["kind"],"i2c")) {
      b.kind=RISC_HW_BUS_I2C;
      if (!number(record["controller"],0,1,b.controller) || b.mode || b.frequency_hz>1000000 || !keys(p,{"sda","scl"}) ||
          !pin(p["sda"],b.sda) || !pin(p["scl"],b.scl) || !claim(b.sda) || !claim(b.scl)) return fail("invalid/conflicting I2C bus");
    } else return fail("unknown bus kind");
    for (size_t i=0;i<busesCount_;++i) if (buses_[i].kind==b.kind && buses_[i].controller==b.controller) return fail("duplicate controller");
    ++busesCount_;
  }
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
