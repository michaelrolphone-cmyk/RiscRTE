// Included by NativeHardware.cpp after its GPIO and clock helpers. The host
// regression includes this exact implementation against a narrow SDK shim.
struct SpiState {
  bool initialized=false,held=false,pending=false,ready=false;
  bool threeWire=false,quad=false,managedMosi=false;uint32_t hz=0;uint8_t mode=0;
  int sclk=-1,mosi=-1,miso=-1,data2=-1,data3=-1;spi_device_handle_t device=nullptr;
  spi_transaction_t transaction{};
  spi_transaction_ext_t framed{};
  bool sdkOwned=false,pollingPending=false;
  alignas(4) uint8_t tx[512]{},rx[512]{};
} spi[2];
bool drain(SpiState& state,uint32_t ms){
  if(!state.pending)return true;
  if(state.pollingPending){
    if(spi_device_polling_end(state.device,ticks(ms))!=ESP_OK)return false;
    state.pollingPending=false;state.pending=false;return true;
  }
  spi_transaction_t* completed=nullptr;
  if(spi_device_get_trans_result(state.device,&completed,ticks(ms))!=ESP_OK)return false;
  if(completed!=&state.transaction)return false;
  state.pending=false;return true;
}
bool spiOpenPins(uint8_t physical,int16_t sclk,int16_t mosi,int16_t miso,int16_t data2,int16_t data3){
  auto& state=spi[physical-2];if(state.initialized)return false;
  state.sclk=sclk;state.mosi=mosi;state.miso=miso;state.data2=data2;state.data3=data3;state.quad=data2>=0&&data3>=0;
  spi_bus_config_t config{};config.sclk_io_num=sclk;config.mosi_io_num=mosi;config.miso_io_num=miso;
  config.quadwp_io_num=data2;config.quadhd_io_num=data3;config.max_transfer_sz=512;
  // A no-MISO bus may later opt into shared-MOSI reads. Route it through the
  // matrix from initialization, so its SDK timing model matches direction
  // changes. Ordinary claims still use their original full-duplex semantics.
  if(state.quad){
    config.flags=SPICOMMON_BUSFLAG_QUAD|SPICOMMON_BUSFLAG_MASTER|SPICOMMON_BUSFLAG_GPIO_PINS;
    config.data4_io_num=config.data5_io_num=config.data6_io_num=config.data7_io_num=-1;
  }
  else if(miso==-1)config.flags=SPICOMMON_BUSFLAG_GPIO_PINS;
  if(spi_bus_initialize(host(physical),&config,SPI_DMA_CH_AUTO)!=ESP_OK)return false;
  state.initialized=true;return true;
}
bool spiOpen(uint8_t physical,int16_t sclk,int16_t mosi,int16_t miso){
  return spiOpenPins(physical,sclk,mosi,miso,-1,-1);
}
bool spiOpenQuad(uint8_t physical,int16_t sclk,int16_t d0,int16_t d1,int16_t d2,int16_t d3){
  if(d0<0 || d1<0 || d2<0 || d3<0)return false;
  return spiOpenPins(physical,sclk,d0,d1,d2,d3);
}
bool spiMosiDirection(uint8_t physical,SpiState& state,bool output){
  if(gpio_set_direction(static_cast<gpio_num_t>(state.mosi),output?GPIO_MODE_INPUT_OUTPUT:GPIO_MODE_INPUT)!=ESP_OK)return false;
  // Pinned S3 gpio_ll_output_disable both disables the pad and disconnects
  // its matrix output. Do not reconnect SIG_GPIO_OUT_IDX on RX: the ROM
  // routing function enables output again, driving the GPIO latch onto MOSI.
  // Its input route (spid_in) from bus initialization remains connected.
  // Output direction setup selects GPIO, so only TX restores SPI routing.
  if(output)esp_rom_gpio_connect_out_signal(state.mosi,physical==2?FSPID_OUT_IDX:SPI3_D_OUT_IDX,false,false);
  return true;
}
bool spiBeginMode(uint8_t physical,uint8_t cs,uint32_t hz,uint8_t mode,bool threeWire){
  auto& state=spi[physical-2];if(!state.initialized || state.held || state.pending)return false;
  // In the explicit mode every attempted begin has a cleanup path, including
  // failures before CS goes low or while changing SDK device/pad state.
  if(threeWire || state.quad)state.held=true;
  state.ready=false;
  if(threeWire && (state.miso!=-1 || state.quad))return false;
  if(state.device && (state.hz!=hz || state.mode!=mode || state.threeWire!=threeWire)){
    if(state.sdkOwned){spi_device_release_bus(state.device);state.sdkOwned=false;}
    if(spi_bus_remove_device(state.device)!=ESP_OK)return false;
    state.device=nullptr;
  }
  if(!state.device){
    spi_device_interface_config_t config{};config.clock_speed_hz=hz;config.mode=mode;config.spics_io_num=-1;config.queue_size=1;
    if(threeWire)config.flags=SPI_DEVICE_3WIRE|SPI_DEVICE_HALFDUPLEX;
    else if(state.quad)config.flags=SPI_DEVICE_HALFDUPLEX;
    if(spi_bus_add_device(host(physical),&config,&state.device)!=ESP_OK)return false;
    state.hz=hz;state.mode=mode;state.threeWire=threeWire;
  }
  if(threeWire){
    state.managedMosi=true;
    if(gpio_pulldown_dis(static_cast<gpio_num_t>(state.mosi))!=ESP_OK ||
       gpio_pullup_en(static_cast<gpio_num_t>(state.mosi))!=ESP_OK || !spiMosiDirection(physical,state,false))return false;
  } else if(state.managedMosi){
    if(gpio_pullup_dis(static_cast<gpio_num_t>(state.mosi))!=ESP_OK || !spiMosiDirection(physical,state,true))return false;
  }
  // Quad claims are exclusive to this host and owner task in CpuPort. Match
  // the verified vendor's SDK ownership while CS is active; no competing SDK
  // owner to wait for. IDF4 requires portMAX_DELAY for acquire/start. Actual
  // transfers drain with an 8 ms cap; timeout retains the DMA descriptor.
  // Legacy/three-wire devices keep their existing queued transport.
  if(state.quad && !state.sdkOwned){
    if(spi_device_acquire_bus(state.device,portMAX_DELAY)!=ESP_OK)return false;
    state.sdkOwned=true;
  }
  if(!gpioWrite(cs,false))return false;
  state.held=true;state.ready=true;return true;
}
bool spiBegin(uint8_t physical,uint8_t cs,uint32_t hz,uint8_t mode,uint32_t){
  return spiBeginMode(physical,cs,hz,mode,false);
}
bool spiBeginThreeWire(uint8_t physical,uint8_t cs,uint32_t hz,uint8_t mode,uint32_t){
  return spiBeginMode(physical,cs,hz,mode,true);
}
bool spiTransferLanes(uint8_t physical,const uint8_t* tx,uint8_t* rx,size_t count,uint32_t ms,bool quadData){
  auto& state=spi[physical-2];if(!state.held || !state.ready || state.pending || !count || count>sizeof(state.tx))return false;
  if(state.threeWire && bool(tx)==bool(rx))return false;
  if((quadData && !state.quad) || (state.quad && (!tx || rx)))return false;
  state.transaction={};
  if(state.threeWire){
    if(ms>8)ms=8;
    // On failure leave this transaction cleanup-only until end, including a
    // fallible pin-direction change or an uncertain queued DMA completion.
    state.ready=false;
    if(!spiMosiDirection(physical,state,tx!=nullptr))return false;
    if(tx){memcpy(state.tx,tx,count);state.transaction.length=count*8;state.transaction.tx_buffer=state.tx;}
    else {state.transaction.rxlength=count*8;state.transaction.rx_buffer=state.rx;}
  } else if(state.quad) {
    if(ms>8)ms=8;
    state.ready=false;
    memcpy(state.tx,tx,count);state.transaction.length=count*8;state.transaction.tx_buffer=state.tx;
    if(quadData)state.transaction.flags=SPI_TRANS_MODE_QIO;
  } else {
    if(tx)memcpy(state.tx,tx,count);else memset(state.tx,0xff,count);
    state.transaction.length=count*8;state.transaction.tx_buffer=state.tx;state.transaction.rx_buffer=state.rx;
  }
  const int64_t start=esp_timer_get_time();
  if(spi_device_queue_trans(state.device,&state.transaction,ticks(ms))!=ESP_OK)return false;
  state.pending=true;
  const uint32_t elapsed=uint32_t((esp_timer_get_time()-start)/1000);
  if(!drain(state,elapsed<ms?ms-elapsed:0))return false;
  if(rx)memcpy(rx,state.rx,count);
  state.ready=true;return true;
}
bool spiTransferFrame(uint8_t physical,const risc_spi_frame_v1* frame,const uint8_t* tx,size_t count,uint32_t ms){
  if(physical<2 || physical>3 || !risc_spi_frame_valid(frame,count) || (count&&!tx) || !ms)return false;
  auto& state=spi[physical-2];
  if(!state.quad || !state.sdkOwned || !state.held || !state.ready || state.pending)return false;
  if(ms>8)ms=8;
  state.ready=false;state.framed={};
  auto& t=state.framed;
  // CO5300 QSPI command protocol (0x02 opcode + 24-bit address) requires the
  // multiline command/address flags used by Arduino_ESP32QSPI. VARIABLE_* alone
  // lets transactions complete but the panel ignores them.
  t.base.flags=SPI_TRANS_MULTILINE_CMD|SPI_TRANS_MULTILINE_ADDR|SPI_TRANS_VARIABLE_CMD|SPI_TRANS_VARIABLE_ADDR|SPI_TRANS_VARIABLE_DUMMY;
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
bool spiTransfer(uint8_t physical,const uint8_t* tx,uint8_t* rx,size_t count,uint32_t ms){
  return spiTransferLanes(physical,tx,rx,count,ms,false);
}
bool spiTransferQuad(uint8_t physical,const uint8_t* tx,size_t count,uint32_t ms){
  return spiTransferLanes(physical,tx,nullptr,count,ms,true);
}
bool spiEnd(uint8_t physical,uint8_t cs,uint32_t ms){
  auto& state=spi[physical-2];if(!state.held)return false;
  // CpuPort applies this cap even when a mode-switch failed and the cached
  // device still has legacy flags. This additional cap protects direct use.
  if(state.threeWire || state.quad){state.ready=false;if(ms>8)ms=8;}
  if(!drain(state,ms) || !gpioWrite(cs,true))return false;
  if(state.sdkOwned){spi_device_release_bus(state.device);state.sdkOwned=false;}
  state.held=false;state.ready=false;return true;
}
bool spiClose(uint8_t physical){
  auto& state=spi[physical-2];if(state.held || state.pending)return false;
  if(state.device){
    if(state.sdkOwned){spi_device_release_bus(state.device);state.sdkOwned=false;}
    if(spi_bus_remove_device(state.device)!=ESP_OK)return false;
    state.device=nullptr;}
  if(state.initialized){if(spi_bus_free(host(physical))!=ESP_OK)return false;state.initialized=false;}
  for(int pin:{state.sclk,state.mosi,state.miso,state.data2,state.data3})if(pin>=0 && !gpioClose(pin))return false;
  state={};return true;
}
