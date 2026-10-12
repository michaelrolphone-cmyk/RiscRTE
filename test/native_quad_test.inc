// The actual Board -> CpuPort -> NativeSpi implementation, not a replacement transport.
static void stage_quad(const std::string& root,unsigned physical=2) {
  std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"generic-quad","revision":"unspecified","buses":[{"instance_id":100,"kind":"spi","controller_namespace":"riscrte.logical","controller":0,"physical_controller":)"<<physical<<R"(,"frequency_hz":40000000,"mode":0,"pins":{"sclk":11,"mosi":4,"miso":5}}],"devices":[{"instance_id":7,"chip":{"vendor":"test","model":"quad","revision":"unspecified"},"compatible":"test,quad","config_type":"display.spi","config_version":2,"config":{"bus_instance_id":100,"cs":12,"dc":-1,"reset":8,"backlight":-1,"busy":13,"width":410,"height":502,"offset_x":22,"offset_y":0,"rotation":0,"reset_active_high":false,"busy_active_high":true,"backlight_active_high":true,"power_pins":[],"power_active_high":[],"reset_assert_ms":200,"reset_recovery_ms":200,"data2":6,"data3":7}}]})";
  std::ofstream(root+"/boot.json")<<R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"display.json","instance_id":7}]})";
  std::ofstream(root+"/display.json")<<R"({"type":"driver","id":"quad-display","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"display.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.gpio","api":1},{"capability":"spi.bus","api":1},{"capability":"platform.clock","api":1}],"provides":[{"capability":"test.display","api":1}],"hardware_compatibility":[{"compatible":"test,quad","revisions":["unspecified"],"config_type":"display.spi","config_version":2}]})";
}
struct QuadFixture {
  Port p;RiscBoot::Runtime r;
  QuadFixture(const std::string& root,unsigned physical=2,bool supported=true):p(hardware()),r(runtime()){
    model={};for(auto& state:NativeShim::spi)state={};stage_quad(root,physical);current=&p;
    if(!supported)p.hw_.spiOpenQuad=nullptr;
    if(!r.prepare(root.c_str())){std::fprintf(stderr,"quad prepare: %s\n",r.error());assert(false);}
    assert(!model.calls && p.spiCount_==1);
  }
  ~QuadFixture(){assert(p.quiescent() && !model.initialized && !model.device.live && !model.pending);}
};
static void quad_tests(const std::string& root) {
  for(unsigned physical:{2u,3u}) {
    QuadFixture f(root,physical);auto& q=f.p.spis_[0].api;auto& a=q.base;auto& g=f.p.gpios_[0].api.base;
    assert(a.struct_size==sizeof(q)&&q.quad_tag==RISC_SPI_QUAD_TAG&&q.quad_version==1&&q.claim_quad&&q.exchange_quad);
    const auto& device=*f.r.board().device(7);assert(device.hardware.config_size==sizeof(risc_hw_spi_display_v2));
    assert(device.display().width==410&&device.display().height==502&&device.config.displayQuad.data2==6);
    uint64_t t=0;for(unsigned pin:{4,5,6,7,11,12})assert(!g.claim(g.context,pin,true,false,false,&t)&&!t);
    assert(!a.claim(a.context,11,4,5,12,&t)&&!model.calls);assert(!a.claim_three_wire(a.context,11,4,12,&t));
    assert(!q.claim_quad(a.context,11,4,5,6,8,12,&t));assert(!q.claim_quad(a.context,11,4,5,6,7,13,&t));
    assert(q.claim_quad(a.context,11,4,5,6,7,12,&t)&&t);
    assert(model.bus.quadwp_io_num==6&&model.bus.quadhd_io_num==7&&model.bus.flags==NativeShim::SPICOMMON_BUSFLAG_QUAD);
    assert(!a.begin(a.context,t,40000001,0,20));assert(a.begin(a.context,t,40000000,0,20));
    assert(model.device.config.flags==NativeShim::SPI_DEVICE_HALFDUPLEX&&!model.level[12]);
    uint8_t command[]={0x32,0,0x2c,0},pixels[513]{};
    assert(a.exchange(a.context,t,command,nullptr,4));assert(NativeShim::spi[physical-2].transaction.flags==0);
    assert(q.exchange_quad(a.context,t,pixels,512));assert(NativeShim::spi[physical-2].transaction.flags==NativeShim::SPI_TRANS_MODE_QIO);
    assert(model.queueWait<=9&&model.drainWait<=9&&!model.pending&&!model.level[12]);
    assert(!q.exchange_quad(a.context,t,pixels,513));assert(!q.exchange_quad(a.context,t,nullptr,4));
    assert(!a.exchange(a.context,t,pixels,pixels,1));assert(!a.release(a.context,t));assert(a.end(a.context,t));
    assert(model.level[12]&&a.release(a.context,t));
    for(unsigned pin:{4,5,6,7,11,12})assert(!f.p.pins_[pin].owner);
  }
  {QuadFixture f(root,2,false);auto& q=f.p.spis_[0].api;assert(q.base.struct_size==sizeof(garden_spi_v1)&&!q.claim_quad&&!q.exchange_quad);}
  // A timed-out queued transfer keeps the DMA buffer and CS until end proves drain.
  for(Fault fault:{Fault::Add,Fault::CsLow,Fault::Queue,Fault::Drain,Fault::CsHigh,Fault::Free,Fault::ClosePin}) {
    QuadFixture f(root);auto& q=f.p.spis_[0].api;auto& a=q.base;uint64_t t=0;uint8_t bytes[512]{};
    assert(q.claim_quad(a.context,11,4,5,6,7,12,&t));
    if(fault==Fault::Add||fault==Fault::CsLow){model.fault=fault;assert(!a.begin(a.context,t,2000000,0,100));}
    else {
      assert(a.begin(a.context,t,2000000,0,100));
      if(fault==Fault::Queue||fault==Fault::Drain){model.fault=fault;assert(!q.exchange_quad(a.context,t,bytes,512));
        assert(!q.exchange_quad(a.context,t,bytes,1));assert(!a.exchange(a.context,t,bytes,nullptr,1));assert(!a.release(a.context,t));}
      else {assert(q.exchange_quad(a.context,t,bytes,512));if(fault==Fault::CsHigh){model.fault=fault;assert(!a.end(a.context,t));}}
    }
    model.fault=Fault::None;assert(a.end(a.context,t));
    if(fault==Fault::Free||fault==Fault::ClosePin){model.fault=fault;model.faultPin=6;assert(!a.release(a.context,t));assert(!a.begin(a.context,t,2000000,0,20));model.fault=Fault::None;}
    assert(a.release(a.context,t));
  }
  std::puts("Native quad SPI: scoped four-pin binding, one/four-lane transfers, deadlines and cleanup PASS");
}
