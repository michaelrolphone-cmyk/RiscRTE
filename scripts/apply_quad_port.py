from pathlib import Path
import re
R=Path(__file__).resolve().parents[1]
def edit(path,old,new):
 p=R/path;s=p.read_text();assert old in s,(path,old[:100]);p.write_text(s.replace(old,new))
(R/'sdk/driver/RiscSpiQuadV1.h').write_text('''#pragma once
/* Additive raw four-lane SPI transport. The v1 SPI prefix and one-lane operations
 * remain unchanged. Quad commands/panel registers belong to provider ELFs. */
#include "GardenPlatformV1.h"
#define RISC_SPI_QUAD_TAG 0x51535049u
#define RISC_SPI_QUAD_VERSION 1u
typedef struct {
    garden_spi_v1 base;
    uint32_t quad_tag, quad_version;
    bool (*claim_quad)(void *,uint8_t sclk,uint8_t data0,uint8_t data1,
                       uint8_t data2,uint8_t data3,uint8_t cs,uint64_t *token);
    /* TX only, 1..512 bytes, copied and drained under the current begin budget.
     * After failure only end/release may run; failed cleanup keeps its token. */
    bool (*exchange_quad)(void *,uint64_t token,const uint8_t *,size_t);
} risc_spi_quad_v1;
''')
(R/'sdk/hardware/RiscDisplaySpiV2.h').write_text('''#pragma once
#include "RiscHardwareConfigV1.h"
/* display.spi config_version=2: complete v1 prefix, then two extra data pins.
 * bus.mosi/data0 and bus.miso/data1 are mandatory for this configuration. */
typedef struct {
    risc_hw_spi_display_v1 base;
    int16_t data2, data3;
} risc_hw_spi_display_v2;
''')
edit('src/bootstrap/Board.h','#include <RiscTouchI2cV2.h>','#include <RiscTouchI2cV2.h>\n#include <RiscDisplaySpiV2.h>')
edit('src/bootstrap/Board.h','      risc_hw_spi_display_v1 display;','      risc_hw_spi_display_v1 display;\n      risc_hw_spi_display_v2 displayQuad;')
edit('src/bootstrap/Board.h','    const risc_hw_i2c_touch_v1& touch()','    const risc_hw_spi_display_v1& display() const { return hardware.config_version==2?config.displayQuad.base:config.display; }\n    const risc_hw_i2c_touch_v1& touch()')
edit('src/bootstrap/Board.cpp','d->config.display.bus.instance_id','d->display().bus.instance_id')
edit('src/bootstrap/Board.cpp','''    if (!keys(c,{"bus_instance_id","width","height","offset_x","offset_y","rotation","cs","dc","reset","backlight","busy","reset_active_high","busy_active_high","backlight_active_high","power_pins","power_active_high","reset_assert_ms","reset_recovery_ms"}) || b->kind!=RISC_HW_BUS_SPI) return false;
    auto& x=d.config.display; x={}; x.struct_size=sizeof(x); x.bus=*b;''','''    const bool quad=d.hardware.config_version==2;
    if (b->kind!=RISC_HW_BUS_SPI) return false;
    if (quad) {
      if (!keys(c,{"bus_instance_id","width","height","offset_x","offset_y","rotation","cs","dc","reset","backlight","busy","reset_active_high","busy_active_high","backlight_active_high","power_pins","power_active_high","reset_assert_ms","reset_recovery_ms","data2","data3"}) || b->miso<0) return false;
      d.config.displayQuad={};
    } else {
      if (!keys(c,{"bus_instance_id","width","height","offset_x","offset_y","rotation","cs","dc","reset","backlight","busy","reset_active_high","busy_active_high","backlight_active_high","power_pins","power_active_high","reset_assert_ms","reset_recovery_ms"})) return false;
      d.config.display={};
    }
    auto& x=quad?d.config.displayQuad.base:d.config.display;
    x.struct_size=quad?sizeof(d.config.displayQuad):sizeof(x); x.bus=*b;''')
edit('src/bootstrap/Board.cpp','!pin(c["dc"],x.dc)', '!pin(c["dc"],x.dc,quad)')
edit('src/bootstrap/Board.cpp','!claim(x.dc)', '!claim(x.dc,quad)')
edit('src/bootstrap/Board.cpp','''    d.hardware.config=&x; d.hardware.config_size=sizeof(x); return true;
  }
  return false; // Extension types''','''    if (quad) {
      auto& v=d.config.displayQuad;
      if (!pin(c["data2"],v.data2) || !pin(c["data3"],v.data3) || !claim(v.data2) || !claim(v.data3)) return false;
      d.hardware.config=&v;d.hardware.config_size=sizeof(v);
    } else {d.hardware.config=&x;d.hardware.config_size=sizeof(x);}
    return true;
  }
  return false; // Extension types''')
edit('src/bootstrap/Board.cpp','(!strcmp(d.type,"radio.lora") || !strcmp(d.type,"touch.i2c"))?2:1','(!strcmp(d.type,"radio.lora") || !strcmp(d.type,"touch.i2c") || !strcmp(d.type,"display.spi"))?2:1')
edit('src/bootstrap/Runtime.cpp','(!strcmp(type,"radio.lora") || !strcmp(type,"touch.i2c"))?2:1','(!strcmp(type,"radio.lora") || !strcmp(type,"touch.i2c") || !strcmp(type,"display.spi"))?2:1')
edit('src/ports/esp32s3/CpuPort.h','#include <GardenPlatformV1.h>','#include <GardenPlatformV1.h>\n#include <RiscSpiQuadV1.h>')
edit('src/ports/esp32s3/CpuPort.h','  bool (*schedulerWait)(uint32_t)=nullptr; // owner-only, bounded, scheduler only','''  bool (*schedulerWait)(uint32_t)=nullptr; // owner-only, bounded, scheduler only
  // Additive raw quad write transport; no controller command knowledge.
  bool (*spiOpenQuad)(uint8_t,int16_t,int16_t,int16_t,int16_t,int16_t)=nullptr;
  bool (*spiTransferQuad)(uint8_t,const uint8_t*,size_t,uint32_t)=nullptr;''')
edit('src/ports/esp32s3/CpuPort.h','bool threeWire=false,ready=false,closing=false; garden_spi_v1 api{};', 'bool threeWire=false,quad=false,ready=false,closing=false; int16_t data2=-1,data3=-1; risc_spi_quad_v1 api{};')
edit('src/ports/esp32s3/CpuPort.h','struct SpiBus { uint64_t instance=0; unsigned refs=0;', 'struct SpiBus { uint64_t instance=0; unsigned refs=0; bool quad=false;')
edit('src/ports/esp32s3/CpuPort.h','static bool spiClaimImpl(void*,uint8_t,uint8_t,int8_t,uint8_t,uint64_t*,bool);','''static bool spiClaimImpl(void*,uint8_t,uint8_t,int8_t,uint8_t,uint64_t*,bool,bool=false);
  static bool spiClaimQuad(void*,uint8_t,uint8_t,uint8_t,uint8_t,uint8_t,uint8_t,uint64_t*);
  static bool spiTransferQuad(void*,uint64_t,const uint8_t*,size_t);''')
edit('src/ports/esp32s3/CpuPort.cpp','const auto& c=d.config.display;', 'const auto& c=d.display();')
edit('src/ports/esp32s3/CpuPort.cpp','d.config.display.bus','d.display().bus')
edit('src/ports/esp32s3/CpuPort.cpp','d.config.display.cs','d.display().cs')
edit('src/ports/esp32s3/CpuPort.cpp','selected.config.display','selected.display()')
edit('src/ports/esp32s3/CpuPort.cpp','''      c.api={1,sizeof(c.api),&c,spiClaim,spiBegin,spiTransfer,spiEnd,idleClocks,spiRelease,spiClaimThreeWire};''','''      c.api.base={1,sizeof(c.api.base),&c,spiClaim,spiBegin,spiTransfer,spiEnd,idleClocks,spiRelease,spiClaimThreeWire};
      if(!radio && d.hardware.config_version==2) {
        c.data2=d.config.displayQuad.data2;c.data3=d.config.displayQuad.data3;
        if(hw_.spiOpenQuad && hw_.spiTransferQuad) {
          c.api.base.struct_size=sizeof(c.api);c.api.quad_tag=RISC_SPI_QUAD_TAG;c.api.quad_version=RISC_SPI_QUAD_VERSION;
          c.api.claim_quad=spiClaimQuad;c.api.exchange_quad=spiTransferQuad;
        }
      }''')
edit('src/ports/esp32s3/CpuPort.cpp','bool Port::spiClaimImpl(void* context,uint8_t sclk,uint8_t mosi,int8_t miso,uint8_t cs,uint64_t* out,bool threeWire){','''bool Port::spiClaimQuad(void* context,uint8_t sclk,uint8_t d0,uint8_t d1,uint8_t d2,uint8_t d3,uint8_t cs,uint64_t* out){
  if(out)*out=0;
  auto& c=*static_cast<Spi*>(context);
  if(c.data2!=d2 || c.data3!=d3 || !c.port->hw_.spiOpenQuad || !c.port->hw_.spiTransferQuad)return false;
  return spiClaimImpl(context,sclk,d0,d1,cs,out,false,true);
}
bool Port::spiClaimImpl(void* context,uint8_t sclk,uint8_t mosi,int8_t miso,uint8_t cs,uint64_t* out,bool threeWire,bool quad){''')
edit('src/ports/esp32s3/CpuPort.cpp','(threeWire && !p.hw_.spiBeginThreeWire) || (bus.refs && bus.instance!=c.bus.instance_id)', '(quad!=(c.data2>=0 && c.data3>=0)) || (threeWire && !p.hw_.spiBeginThreeWire) || (bus.refs && (bus.instance!=c.bus.instance_id || bus.quad!=quad))')
edit('src/ports/esp32s3/CpuPort.cpp','''    bool reserved=p.reserve(sclk,&bus) && p.reserve(mosi,&bus) && p.reserve(miso,&bus);
    if(!reserved || !p.hw_.spiOpen(c.physical,sclk,mosi,miso)){''','''    bool reserved=p.reserve(sclk,&bus) && p.reserve(mosi,&bus) && p.reserve(miso,&bus) &&
        (!quad || (p.reserve(c.data2,&bus) && p.reserve(c.data3,&bus)));
    if(!reserved || !(quad?p.hw_.spiOpenQuad(c.physical,sclk,mosi,miso,c.data2,c.data3):p.hw_.spiOpen(c.physical,sclk,mosi,miso))){''')
edit('src/ports/esp32s3/CpuPort.cpp','p.unreserve(miso,&bus);}', 'p.unreserve(miso,&bus);if(quad){p.unreserve(c.data2,&bus);p.unreserve(c.data3,&bus);}}')
edit('src/ports/esp32s3/CpuPort.cpp','bus.instance=c.bus.instance_id;', 'bus.instance=c.bus.instance_id;bus.quad=quad;')
edit('src/ports/esp32s3/CpuPort.cpp','++bus.refs;c.threeWire=threeWire;', '++bus.refs;c.threeWire=threeWire;c.quad=quad;')
edit('src/ports/esp32s3/CpuPort.cpp','''  if(c.threeWire){
    // Backend mode''','''  if(c.threeWire || c.quad){
    // Backend mode''')
edit('src/ports/esp32s3/CpuPort.cpp','c.ready=p.hw_.spiBeginThreeWire(c.physical,c.cs,hz,mode,ms);', 'c.ready=c.threeWire?p.hw_.spiBeginThreeWire(c.physical,c.cs,hz,mode,ms):p.hw_.spiBegin(c.physical,c.cs,hz,mode,ms);')
edit('src/ports/esp32s3/CpuPort.cpp','if(c.threeWire && (!c.ready || bool(tx)==bool(rx)))return false;', 'if((c.threeWire || c.quad) && (!c.ready || bool(tx)==bool(rx)))return false;\n  if(c.quad && (!tx || rx))return false;')
edit('src/ports/esp32s3/CpuPort.cpp','if(c.threeWire && remaining>8)remaining=8;', 'if((c.threeWire || c.quad) && remaining>8)remaining=8;')
edit('src/ports/esp32s3/CpuPort.cpp','if(c.threeWire && !result)c.ready=false;', 'if((c.threeWire || c.quad) && !result)c.ready=false;')
edit('src/ports/esp32s3/CpuPort.cpp','bool Port::spiEnd(void* context,uint64_t token){','''bool Port::spiTransferQuad(void* context,uint64_t token,const uint8_t* tx,size_t count){
  auto& c=*static_cast<Spi*>(context);auto& p=*c.port;auto& bus=p.spiBuses_[c.physical-2];
  const uint64_t now=p.hw_.now();
  if(!p.available() || !token || c.token!=token || !c.quad || !c.ready || bus.held!=&c ||
      !tx || !count || count>512 || now>=c.deadline)return false;
  uint32_t remaining=uint32_t(c.deadline-now);if(remaining>8)remaining=8;
  const bool result=p.hw_.spiTransferQuad(c.physical,tx,count,remaining);
  if(!result)c.ready=false;
  return result;
}
bool Port::spiEnd(void* context,uint64_t token){''')
edit('src/ports/esp32s3/CpuPort.cpp','if(c.threeWire){c.ready=false;if(remaining>8)remaining=8;}', 'if(c.threeWire || c.quad){c.ready=false;if(remaining>8)remaining=8;}')
edit('src/ports/esp32s3/CpuPort.cpp','if(c.threeWire){c.closing=true;bus.closing=&c;}', 'if(c.threeWire || c.quad){c.closing=true;bus.closing=&c;}')
edit('src/ports/esp32s3/CpuPort.cpp','p.unreserve(c.bus.miso,&bus);bus.instance=0;', 'p.unreserve(c.bus.miso,&bus);if(c.quad){p.unreserve(c.data2,&bus);p.unreserve(c.data3,&bus);}bus.instance=0;bus.quad=false;')
edit('src/ports/esp32s3/CpuPort.cpp','c.token=0;c.threeWire=false;c.ready=false;', 'c.token=0;c.threeWire=false;c.quad=false;c.ready=false;')
edit('src/ports/esp32s3/NativeSpi.inc','bool threeWire=false,managedMosi=false;', 'bool threeWire=false,quad=false,managedMosi=false;')
edit('src/ports/esp32s3/NativeSpi.inc','int sclk=-1,mosi=-1,miso=-1;', 'int sclk=-1,mosi=-1,miso=-1,data2=-1,data3=-1;')
edit('src/ports/esp32s3/NativeSpi.inc','bool spiOpen(uint8_t physical,int16_t sclk,int16_t mosi,int16_t miso){', 'bool spiOpenPins(uint8_t physical,int16_t sclk,int16_t mosi,int16_t miso,int16_t data2,int16_t data3){')
edit('src/ports/esp32s3/NativeSpi.inc','state.sclk=sclk;state.mosi=mosi;state.miso=miso;', 'state.sclk=sclk;state.mosi=mosi;state.miso=miso;state.data2=data2;state.data3=data3;state.quad=data2>=0&&data3>=0;')
edit('src/ports/esp32s3/NativeSpi.inc','config.quadwp_io_num=-1;config.quadhd_io_num=-1;', 'config.quadwp_io_num=data2;config.quadhd_io_num=data3;')
edit('src/ports/esp32s3/NativeSpi.inc','if(miso==-1)config.flags=SPICOMMON_BUSFLAG_GPIO_PINS;', 'if(state.quad)config.flags=SPICOMMON_BUSFLAG_QUAD;\n  else if(miso==-1)config.flags=SPICOMMON_BUSFLAG_GPIO_PINS;')
edit('src/ports/esp32s3/NativeSpi.inc','bool spiMosiDirection(','''bool spiOpen(uint8_t physical,int16_t sclk,int16_t mosi,int16_t miso){
  return spiOpenPins(physical,sclk,mosi,miso,-1,-1);
}
bool spiOpenQuad(uint8_t physical,int16_t sclk,int16_t d0,int16_t d1,int16_t d2,int16_t d3){
  if(d0<0 || d1<0 || d2<0 || d3<0)return false;
  return spiOpenPins(physical,sclk,d0,d1,d2,d3);
}
bool spiMosiDirection(''')
edit('src/ports/esp32s3/NativeSpi.inc','if(threeWire)state.held=true;', 'if(threeWire || state.quad)state.held=true;')
edit('src/ports/esp32s3/NativeSpi.inc','if(threeWire && state.miso!=-1)return false;', 'if(threeWire && (state.miso!=-1 || state.quad))return false;')
edit('src/ports/esp32s3/NativeSpi.inc','if(threeWire)config.flags=SPI_DEVICE_3WIRE|SPI_DEVICE_HALFDUPLEX;', 'if(threeWire)config.flags=SPI_DEVICE_3WIRE|SPI_DEVICE_HALFDUPLEX;\n    else if(state.quad)config.flags=SPI_DEVICE_HALFDUPLEX;')
edit('src/ports/esp32s3/NativeSpi.inc','bool spiTransfer(uint8_t physical,const uint8_t* tx,uint8_t* rx,size_t count,uint32_t ms){', 'bool spiTransferLanes(uint8_t physical,const uint8_t* tx,uint8_t* rx,size_t count,uint32_t ms,bool quadData){')
edit('src/ports/esp32s3/NativeSpi.inc','if(state.threeWire && bool(tx)==bool(rx))return false;', 'if(state.threeWire && bool(tx)==bool(rx))return false;\n  if((quadData && !state.quad) || (state.quad && (!tx || rx)))return false;')
edit('src/ports/esp32s3/NativeSpi.inc','''  } else {
    if(tx)memcpy(state.tx,tx,count);else memset(state.tx,0xff,count);''','''  } else if(state.quad) {
    if(ms>8)ms=8;
    state.ready=false;
    memcpy(state.tx,tx,count);state.transaction.length=count*8;state.transaction.tx_buffer=state.tx;
    if(quadData)state.transaction.flags=SPI_TRANS_MODE_QIO;
  } else {
    if(tx)memcpy(state.tx,tx,count);else memset(state.tx,0xff,count);''')
edit('src/ports/esp32s3/NativeSpi.inc','bool spiEnd(uint8_t physical,uint8_t cs,uint32_t ms){','''bool spiTransfer(uint8_t physical,const uint8_t* tx,uint8_t* rx,size_t count,uint32_t ms){
  return spiTransferLanes(physical,tx,rx,count,ms,false);
}
bool spiTransferQuad(uint8_t physical,const uint8_t* tx,size_t count,uint32_t ms){
  return spiTransferLanes(physical,tx,nullptr,count,ms,true);
}
bool spiEnd(uint8_t physical,uint8_t cs,uint32_t ms){''')
edit('src/ports/esp32s3/NativeSpi.inc','if(state.threeWire){state.ready=false;if(ms>8)ms=8;}', 'if(state.threeWire || state.quad){state.ready=false;if(ms>8)ms=8;}')
edit('src/ports/esp32s3/NativeSpi.inc','{state.sclk,state.mosi,state.miso}', '{state.sclk,state.mosi,state.miso,state.data2,state.data3}')
edit('src/ports/esp32s3/NativeHardware.cpp','  hardware.spiBeginThreeWire=spiBeginThreeWire;', '  hardware.spiBeginThreeWire=spiBeginThreeWire;\n  hardware.spiOpenQuad=spiOpenQuad;hardware.spiTransferQuad=spiTransferQuad;')
for p in (R/'test').glob('*.cpp'):
 s=p.read_text();n=re.sub(r'(spis_\[[^\]]+\]\.api)(?!\.base)',r'\1.base',s)
 if n!=s:p.write_text(n)
edit('test/native_spi_shim/Sdk.h','constexpr int GPIO_MODE_INPUT=','constexpr int SPI_TRANS_MODE_QIO=1<<1,SPICOMMON_BUSFLAG_QUAD=1<<5;\nconstexpr int GPIO_MODE_INPUT=')
edit('test/native_spi_shim/Sdk.h','struct spi_transaction_t {size_t length,rxlength;','struct spi_transaction_t {uint32_t flags;size_t length,rxlength;')
edit('test/native_spi_shim/Sdk.h','  } else assert(transaction->tx_buffer && transaction->rx_buffer && transaction->length>0);','''  } else if(device->config.flags&SPI_DEVICE_HALFDUPLEX) {
    assert(model.bus.quadwp_io_num>=0 && model.bus.quadhd_io_num>=0);
    assert(transaction->tx_buffer && !transaction->rx_buffer && transaction->length>0 && !transaction->rxlength);
    assert(!transaction->flags || transaction->flags==SPI_TRANS_MODE_QIO);
  } else assert(transaction->tx_buffer && transaction->rx_buffer && transaction->length>0 && !transaction->flags);''')
edit('test/native_spi_test.cpp','h.spiBeginThreeWire=NativeShim::spiBeginThreeWire;return h;', 'h.spiBeginThreeWire=NativeShim::spiBeginThreeWire;h.spiOpenQuad=NativeShim::spiOpenQuad;h.spiTransferQuad=NativeShim::spiTransferQuad;return h;')
edit('test/native_spi_test.cpp','struct LegacySpi {','#include "native_quad_test.inc"\nstruct LegacySpi {')
edit('test/native_spi_test.cpp','assert(argc==2);std::string root=argv[1];','assert(argc==2);std::string root=argv[1];quad_tests(root);')
print('Quad SPI source migration applied; run production transport tests before committing.')
