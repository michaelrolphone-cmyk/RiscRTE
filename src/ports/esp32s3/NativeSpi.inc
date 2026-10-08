// Included by NativeHardware.cpp after its GPIO and clock helpers. The host
// regression includes this exact implementation against a narrow SDK shim.
struct SpiState {
  bool initialized=false,held=false,pending=false,ready=false;
  bool threeWire=false,managedMosi=false;uint32_t hz=0;uint8_t mode=0;
  int sclk=-1,mosi=-1,miso=-1;spi_device_handle_t device=nullptr;
  spi_transaction_t transaction{};
  alignas(4) uint8_t tx[512]{},rx[512]{};
} spi[2];
bool drain(SpiState& state,uint32_t ms){
  if(!state.pending)return true;
  spi_transaction_t* completed=nullptr;
  if(spi_device_get_trans_result(state.device,&completed,ticks(ms))!=ESP_OK)return false;
  if(completed!=&state.transaction)return false;
  state.pending=false;return true;
}
bool spiOpen(uint8_t physical,int16_t sclk,int16_t mosi,int16_t miso){
  auto& state=spi[physical-2];if(state.initialized)return false;
  state.sclk=sclk;state.mosi=mosi;state.miso=miso;
  spi_bus_config_t config{};config.sclk_io_num=sclk;config.mosi_io_num=mosi;config.miso_io_num=miso;
  config.quadwp_io_num=-1;config.quadhd_io_num=-1;config.max_transfer_sz=512;
  // A no-MISO bus may later opt into shared-MOSI reads. Route it through the
  // matrix from initialization, so its SDK timing model matches direction
  // changes. Ordinary claims still use their original full-duplex semantics.
  if(miso==-1)config.flags=SPICOMMON_BUSFLAG_GPIO_PINS;
  if(spi_bus_initialize(host(physical),&config,SPI_DMA_CH_AUTO)!=ESP_OK)return false;
  state.initialized=true;return true;
}
bool spiMosiDirection(uint8_t physical,SpiState& state,bool output){
  if(gpio_set_direction(static_cast<gpio_num_t>(state.mosi),output?GPIO_MODE_INPUT_OUTPUT:GPIO_MODE_INPUT)!=ESP_OK)return false;
  // IDF4 gpio_set_direction(output) replaces the matrix output with GPIO.
  // Restore the SPI signal for TX. RX selects the disabled GPIO output so
  // a previous peripheral output-enable cannot drive the shared data pad;
  // the SPI input route established by bus initialization stays connected.
  const uint32_t signal=output?(physical==2?FSPID_OUT_IDX:SPI3_D_OUT_IDX):SIG_GPIO_OUT_IDX;
  esp_rom_gpio_connect_out_signal(state.mosi,signal,false,false);
  return true;
}
bool spiBeginMode(uint8_t physical,uint8_t cs,uint32_t hz,uint8_t mode,bool threeWire){
  auto& state=spi[physical-2];if(!state.initialized || state.held || state.pending)return false;
  // In the explicit mode every attempted begin has a cleanup path, including
  // failures before CS goes low or while changing SDK device/pad state.
  if(threeWire)state.held=true;
  state.ready=false;
  if(threeWire && state.miso!=-1)return false;
  if(state.device && (state.hz!=hz || state.mode!=mode || state.threeWire!=threeWire)){
    if(spi_bus_remove_device(state.device)!=ESP_OK)return false;
    state.device=nullptr;
  }
  if(!state.device){
    spi_device_interface_config_t config{};config.clock_speed_hz=hz;config.mode=mode;config.spics_io_num=-1;config.queue_size=1;
    if(threeWire)config.flags=SPI_DEVICE_3WIRE|SPI_DEVICE_HALFDUPLEX;
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
  // IDF4 acquire_bus accepts only an unbounded wait. The CPU port owns this
  // controller exclusively and serializes every device on its owner task, so
  // no SDK bus acquisition is needed. Queued transfers retain bounded waits.
  if(!gpioWrite(cs,false))return false;
  state.held=true;state.ready=true;return true;
}
bool spiBegin(uint8_t physical,uint8_t cs,uint32_t hz,uint8_t mode,uint32_t){
  return spiBeginMode(physical,cs,hz,mode,false);
}
bool spiBeginThreeWire(uint8_t physical,uint8_t cs,uint32_t hz,uint8_t mode,uint32_t){
  return spiBeginMode(physical,cs,hz,mode,true);
}
bool spiTransfer(uint8_t physical,const uint8_t* tx,uint8_t* rx,size_t count,uint32_t ms){
  auto& state=spi[physical-2];if(!state.held || !state.ready || state.pending || !count || count>sizeof(state.tx))return false;
  if(state.threeWire && bool(tx)==bool(rx))return false;
  state.transaction={};
  if(state.threeWire){
    if(ms>8)ms=8;
    // On failure leave this transaction cleanup-only until end, including a
    // fallible pin-direction change or an uncertain queued DMA completion.
    state.ready=false;
    if(!spiMosiDirection(physical,state,tx!=nullptr))return false;
    if(tx){memcpy(state.tx,tx,count);state.transaction.length=count*8;state.transaction.tx_buffer=state.tx;}
    else {state.transaction.rxlength=count*8;state.transaction.rx_buffer=state.rx;}
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
bool spiEnd(uint8_t physical,uint8_t cs,uint32_t ms){
  auto& state=spi[physical-2];if(!state.held)return false;
  // CpuPort applies this cap even when a mode-switch failed and the cached
  // device still has legacy flags. This additional cap protects direct use.
  if(state.threeWire){state.ready=false;if(ms>8)ms=8;}
  if(!drain(state,ms) || !gpioWrite(cs,true))return false;
  state.held=false;state.ready=false;return true;
}
bool spiClose(uint8_t physical){
  auto& state=spi[physical-2];if(state.held || state.pending)return false;
  if(state.device){if(spi_bus_remove_device(state.device)!=ESP_OK)return false;
    state.device=nullptr;}
  if(state.initialized){if(spi_bus_free(host(physical))!=ESP_OK)return false;state.initialized=false;}
  for(int pin:{state.sclk,state.mosi,state.miso})if(pin>=0 && !gpioClose(pin))return false;
  state={};return true;
}
