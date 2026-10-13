"""One-time readable migration of pinned raw SPI sources. CI commits the results."""
from pathlib import Path
R=Path(__file__).resolve().parents[1]
if 'bool spiTransferFrame(' in (R/'src/ports/esp32s3/NativeSpi.inc').read_text():raise SystemExit(0)
def change(path,old,new):
 p=R/path;s=p.read_text();assert old in s,(path,old[:80]);p.write_text(s.replace(old,new))
change('sdk/driver/RiscSpiQuadV1.h','#include "GardenPlatformV1.h"','#include "GardenPlatformV1.h"\n#include "RiscSpiFrameV1.h"')
change('sdk/driver/RiscSpiQuadV1.h','} risc_spi_quad_v1;','''    /* Append-only framed TX. Requires the advertised table extent below.
     * Payload is copied, 0..512 bytes; header-only commands are permitted.
     * Failure makes the active begin cleanup-only, including late completion. */
    bool (*exchange_frame)(void *,uint64_t token,const risc_spi_frame_v1 *,const uint8_t *,size_t);
} risc_spi_quad_v1;
#define RISC_SPI_QUAD_RAW_V1_SIZE offsetof(risc_spi_quad_v1,exchange_frame)
#define RISC_SPI_QUAD_FRAME_V1_SIZE sizeof(risc_spi_quad_v1)''')
change('src/ports/esp32s3/CpuPort.h','  bool (*spiTransferQuad)(uint8_t,const uint8_t*,size_t,uint32_t)=nullptr;','''  bool (*spiTransferQuad)(uint8_t,const uint8_t*,size_t,uint32_t)=nullptr;
  bool (*spiTransferFrame)(uint8_t,const risc_spi_frame_v1*,const uint8_t*,size_t,uint32_t)=nullptr;''')
change('src/ports/esp32s3/CpuPort.h','  static bool spiTransferQuad(void*,uint64_t,const uint8_t*,size_t);','''  static bool spiTransferQuad(void*,uint64_t,const uint8_t*,size_t);
  static bool spiTransferFrame(void*,uint64_t,const risc_spi_frame_v1*,const uint8_t*,size_t);''')
change('src/ports/esp32s3/CpuPort.cpp','c.api.base.struct_size=sizeof(c.api);c.api.quad_tag=', 'c.api.base.struct_size=RISC_SPI_QUAD_RAW_V1_SIZE;c.api.quad_tag=')
change('src/ports/esp32s3/CpuPort.cpp','c.api.claim_quad=spiClaimQuad;c.api.exchange_quad=spiTransferQuad;','''c.api.claim_quad=spiClaimQuad;c.api.exchange_quad=spiTransferQuad;
          if(hw_.spiTransferFrame){c.api.exchange_frame=spiTransferFrame;c.api.base.struct_size=sizeof(c.api);}''')
change('src/ports/esp32s3/CpuPort.cpp','bool Port::spiEnd(void* context,uint64_t token){','''bool Port::spiTransferFrame(void* context,uint64_t token,const risc_spi_frame_v1* frame,const uint8_t* tx,size_t count){
  auto& c=*static_cast<Spi*>(context);auto& p=*c.port;auto& bus=p.spiBuses_[c.physical-2];
  const uint64_t now=p.hw_.now();
  if(!p.available() || !token || c.token!=token || !c.quad || !c.ready || bus.held!=&c ||
      !p.hw_.spiTransferFrame || !risc_spi_frame_valid(frame,count) || (count&&!tx) || now>=c.deadline)return false;
  uint32_t remaining=uint32_t(c.deadline-now);if(remaining>8)remaining=8;
  const bool result=p.hw_.spiTransferFrame(c.physical,frame,tx,count,remaining);
  if(!result)c.ready=false;
  return result;
}
bool Port::spiEnd(void* context,uint64_t token){''')
change('src/ports/esp32s3/NativeHardware.cpp','hardware.spiOpenQuad=spiOpenQuad;hardware.spiTransferQuad=spiTransferQuad;','hardware.spiOpenQuad=spiOpenQuad;hardware.spiTransferQuad=spiTransferQuad;hardware.spiTransferFrame=spiTransferFrame;')
change('src/ports/esp32s3/NativeSpi.inc','  spi_transaction_t transaction{};','''  spi_transaction_t transaction{};
  spi_transaction_ext_t framed{};
  bool sdkOwned=false,pollingPending=false;''')
change('src/ports/esp32s3/NativeSpi.inc','  spi_transaction_t* completed=nullptr;','''  if(state.pollingPending){
    if(spi_device_polling_end(state.device,ticks(ms))!=ESP_OK)return false;
    state.pollingPending=false;state.pending=false;return true;
  }
  spi_transaction_t* completed=nullptr;''')
change('src/ports/esp32s3/NativeSpi.inc','  if(state.quad)config.flags=SPICOMMON_BUSFLAG_QUAD;','''  if(state.quad){
    config.flags=SPICOMMON_BUSFLAG_QUAD|SPICOMMON_BUSFLAG_MASTER|SPICOMMON_BUSFLAG_GPIO_PINS;
    config.data4_io_num=config.data5_io_num=config.data6_io_num=config.data7_io_num=-1;
  }''')
change('src/ports/esp32s3/NativeSpi.inc','    if(spi_bus_remove_device(state.device)!=ESP_OK)return false;', '''    if(state.sdkOwned){spi_device_release_bus(state.device);state.sdkOwned=false;}
    if(spi_bus_remove_device(state.device)!=ESP_OK)return false;''')
change('src/ports/esp32s3/NativeSpi.inc','  // IDF4 acquire_bus accepts only an unbounded wait. The CPU port owns this\n  // controller exclusively and serializes every device on its owner task, so\n  // no SDK bus acquisition is needed. Queued transfers retain bounded waits.','''  // Quad claims are exclusive to this host and owner task in CpuPort. Match
  // the verified vendor's SDK ownership while CS is active; no competing SDK
  // owner to wait for. IDF4 requires portMAX_DELAY for acquire/start. Actual
  // transfers drain with an 8 ms cap; timeout retains the DMA descriptor.
  // Legacy/three-wire devices keep their existing queued transport.
  if(state.quad && !state.sdkOwned){
    if(spi_device_acquire_bus(state.device,portMAX_DELAY)!=ESP_OK)return false;
    state.sdkOwned=true;
  }''')
change('src/ports/esp32s3/NativeSpi.inc','bool spiTransfer(uint8_t physical,const uint8_t* tx,uint8_t* rx,size_t count,uint32_t ms){','''bool spiTransferFrame(uint8_t physical,const risc_spi_frame_v1* frame,const uint8_t* tx,size_t count,uint32_t ms){
  if(physical<2 || physical>3 || !risc_spi_frame_valid(frame,count) || (count&&!tx) || !ms)return false;
  auto& state=spi[physical-2];
  if(!state.quad || !state.sdkOwned || !state.held || !state.ready || state.pending)return false;
  if(ms>8)ms=8;
  state.ready=false;state.framed={};
  auto& t=state.framed;
  t.base.flags=SPI_TRANS_VARIABLE_CMD|SPI_TRANS_VARIABLE_ADDR|SPI_TRANS_VARIABLE_DUMMY;
  if(frame->data_lines==4)t.base.flags|=SPI_TRANS_MODE_QIO;
  t.command_bits=frame->command_bits;t.address_bits=frame->address_bits;t.dummy_bits=frame->dummy_bits;
  t.base.cmd=frame->command;t.base.addr=frame->address;t.base.length=count*8;
  if(count){memcpy(state.tx,tx,count);t.base.tx_buffer=state.tx;}
  // Hardware command/address phases and data are submitted together. Splitting
  // the prefix into an earlier data-only transfer is not equivalent here.
  // The pre-acquired exclusive SDK lease removes start-side lock contention.
  const int64_t began=esp_timer_get_time();
  if(spi_device_polling_start(state.device,&t.base,portMAX_DELAY)!=ESP_OK)return false;
  state.pending=true;state.pollingPending=true;
  const uint32_t elapsed=uint32_t((esp_timer_get_time()-began)/1000);
  if(!drain(state,elapsed<ms?ms-elapsed:0))return false;
  state.ready=true;return true;
}
bool spiTransfer(uint8_t physical,const uint8_t* tx,uint8_t* rx,size_t count,uint32_t ms){''')
change('src/ports/esp32s3/NativeSpi.inc','  if(state.device){if(spi_bus_remove_device(state.device)!=ESP_OK)return false;','''  if(state.device){
    if(state.sdkOwned){spi_device_release_bus(state.device);state.sdkOwned=false;}
    if(spi_bus_remove_device(state.device)!=ESP_OK)return false;''')
change('test/native_spi_shim/Sdk.h','SPI_TRANS_MODE_QIO=1<<1,SPICOMMON_BUSFLAG_QUAD=1<<5;','''SPI_TRANS_MODE_QIO=1<<1,SPICOMMON_BUSFLAG_QUAD=1<<5;
constexpr int SPICOMMON_BUSFLAG_MASTER=1<<0,SPI_TRANS_VARIABLE_CMD=1<<5,SPI_TRANS_VARIABLE_ADDR=1<<6,SPI_TRANS_VARIABLE_DUMMY=1<<7;
constexpr TickType_t portMAX_DELAY=UINT32_MAX;''')
change('test/native_spi_shim/Sdk.h','quadhd_io_num,max_transfer_sz;', 'quadhd_io_num,max_transfer_sz,data4_io_num,data5_io_num,data6_io_num,data7_io_num;')
change('test/native_spi_shim/Sdk.h','struct spi_transaction_t {uint32_t flags;', 'struct spi_transaction_t {uint32_t flags;uint16_t cmd;uint64_t addr;')
change('test/native_spi_shim/Sdk.h','struct Device {','struct spi_transaction_ext_t {spi_transaction_t base;uint8_t command_bits,address_bits,dummy_bits;};\nstruct Device {')
change('test/native_spi_shim/Sdk.h','Free,ClosePin};','Free,ClosePin,Acquire,PollStart,PollEnd};')
change('test/native_spi_shim/Sdk.h','  bool owner=true,initialized=false,','  bool sdkOwned=false,polling=false;unsigned polls=0;\n  bool owner=true,initialized=false,')
change('test/native_spi_shim/Sdk.h','  assert(device==&model.device && device->live && !model.pending);++model.removes;', '  assert(device==&model.device && device->live && !model.pending && !model.sdkOwned);++model.removes;')
change('test/native_spi_shim/Sdk.h','#include "ports/esp32s3/NativeSpi.inc"','''inline int spi_device_acquire_bus(spi_device_handle_t device,TickType_t wait){
  assert(device==&model.device && device->live && !model.sdkOwned && !model.pending && wait==portMAX_DELAY);
  if(fails(Fault::Acquire))return ESP_FAIL;
  model.sdkOwned=true;return ESP_OK;
}
inline void spi_device_release_bus(spi_device_handle_t device){
  assert(device==&model.device && model.sdkOwned && !model.pending);model.sdkOwned=false;
}
inline int spi_device_polling_start(spi_device_handle_t device,spi_transaction_t* transaction,TickType_t wait){
  assert(device==&model.device && model.sdkOwned && !model.pending && wait==portMAX_DELAY);
  if(fails(Fault::PollStart))return ESP_FAIL;
  assert(device->config.flags==SPI_DEVICE_HALFDUPLEX && !transaction->rx_buffer && !transaction->rxlength);
  const auto& f=*reinterpret_cast<const spi_transaction_ext_t*>(transaction);
  assert((transaction->flags & ~SPI_TRANS_MODE_QIO)==(SPI_TRANS_VARIABLE_CMD|SPI_TRANS_VARIABLE_ADDR|SPI_TRANS_VARIABLE_DUMMY));
  assert(f.command_bits<=16 && f.address_bits<=64 && f.dummy_bits<=32);
  assert(transaction->length || f.command_bits || f.address_bits);
  assert(bool(transaction->length)==bool(transaction->tx_buffer));
  model.pending=transaction;model.polling=true;model.polls++;return ESP_OK;
}
inline int spi_device_polling_end(spi_device_handle_t device,TickType_t wait){
  assert(device==&model.device && model.sdkOwned && model.pending && model.polling && wait<=9);
  model.drainWait=wait;
  if(fails(Fault::PollEnd))return ESP_FAIL;
  model.pending=nullptr;model.polling=false;return ESP_OK;
}
#include "ports/esp32s3/NativeSpi.inc"''')
change('test/native_spi_test.cpp','h.spiTransferQuad=NativeShim::spiTransferQuad;return h;', 'h.spiTransferQuad=NativeShim::spiTransferQuad;h.spiTransferFrame=NativeShim::spiTransferFrame;return h;')
change('test/native_quad_test.inc','model.bus.flags==NativeShim::SPICOMMON_BUSFLAG_QUAD', 'model.bus.flags==(NativeShim::SPICOMMON_BUSFLAG_QUAD|NativeShim::SPICOMMON_BUSFLAG_MASTER|NativeShim::SPICOMMON_BUSFLAG_GPIO_PINS)')
change('test/native_spi_test.cpp','#include "native_quad_test.inc"','#include "native_quad_test.inc"\n#include "native_frame_test.inc"')
change('test/native_spi_test.cpp','quad_tests(root);','quad_tests(root);frame_tests(root);')
change('src/ports/esp32s3/NativeSpi.inc','  state.held=false;state.ready=false;return true;','  if(state.sdkOwned){spi_device_release_bus(state.device);state.sdkOwned=false;}\n  state.held=false;state.ready=false;return true;')
