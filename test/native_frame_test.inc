// Exercise the real capability -> CpuPort -> native SDK descriptor path.
static void frame_tests(const std::string& root) {
  for(unsigned physical:{2u,3u}) {
    QuadFixture f(root,physical);auto& q=f.p.spis_[0].api;auto& a=q.base;
    assert(a.struct_size==RISC_SPI_QUAD_FRAME_V1_SIZE && q.exchange_frame);
    uint64_t t=0;assert(q.claim_quad(a.context,11,4,5,6,7,12,&t));
    assert(a.begin(a.context,t,40000000,0,100));
    assert(model.sdkOwned && !model.level[12]);
    risc_spi_frame_v1 command={sizeof(command),0x02,8,24,0x1100,0,1,0};
    assert(q.exchange_frame(a.context,t,&command,nullptr,0));
    auto& wire=NativeShim::spi[physical-2].framed;
    assert(wire.base.cmd==2 && wire.base.addr==0x1100 && model.commandBits==8 && model.addressBits==24);
    assert(!wire.base.length && !wire.base.tx_buffer && !wire.base.rx_buffer);
    uint8_t param=0x55;
    command.address=0x3a00;assert(q.exchange_frame(a.context,t,&command,&param,1));
    assert(wire.base.length==8 && NativeShim::payload(wire.base)[0]==0x55);
    uint8_t data[512];memset(data,0xa5,sizeof(data));
    risc_spi_frame_v1 pixels={sizeof(pixels),0x32,8,24,0x3c00,0,4,0};
    assert(q.exchange_frame(a.context,t,&pixels,data,sizeof(data)));
    assert(wire.base.flags&NativeShim::SPI_TRANS_MODE_QIO);
    assert(wire.base.cmd==0x32 && wire.base.addr==0x3c00 && wire.base.length==4096);
    assert(wire.base.tx_buffer!=data && !memcmp(wire.base.tx_buffer,data,512));
    pixels.command=0;pixels.command_bits=0;pixels.address=0;pixels.address_bits=0;
    assert(q.exchange_frame(a.context,t,&pixels,data,308));
    assert(!model.commandBits && !model.addressBits && wire.base.length==2464 && !model.level[12]);
    unsigned polls=model.polls;
    assert(!q.exchange_frame(a.context,t,nullptr,data,1));
    assert(!q.exchange_frame(a.context,t,&pixels,nullptr,1));
    assert(!q.exchange_frame(a.context,t,&pixels,data,513));
    assert(!q.exchange_frame(a.context,t,&pixels,nullptr,0));
    assert(!q.exchange_frame(a.context,t+1,&pixels,data,1));
    pixels.command_bits=17;assert(!q.exchange_frame(a.context,t,&pixels,data,1));pixels.command_bits=0;
    pixels.address_bits=65;assert(!q.exchange_frame(a.context,t,&pixels,data,1));pixels.address_bits=0;
    pixels.dummy_bits=33;assert(!q.exchange_frame(a.context,t,&pixels,data,1));pixels.dummy_bits=0;
    pixels.reserved=1;assert(!q.exchange_frame(a.context,t,&pixels,data,1));pixels.reserved=0;
    pixels.data_lines=2;assert(!q.exchange_frame(a.context,t,&pixels,data,1));pixels.data_lines=4;
    pixels.command=1;assert(!q.exchange_frame(a.context,t,&pixels,data,1));pixels.command=0;
    pixels.address=1;assert(!q.exchange_frame(a.context,t,&pixels,data,1));pixels.address=0;
    assert(model.polls==polls);
    model.owner=false;assert(!q.exchange_frame(a.context,t,&pixels,data,1));model.owner=true;
    model.us=200000;assert(!q.exchange_frame(a.context,t,&pixels,data,1));
    assert(a.end(a.context,t));assert(model.level[12]);assert(a.release(a.context,t));assert(!model.sdkOwned);
  }
  for(Fault fault:{Fault::Acquire,Fault::PollStart,Fault::PollEnd}) {
    QuadFixture f(root);auto& q=f.p.spis_[0].api;auto& a=q.base;uint64_t t=0;
    assert(q.claim_quad(a.context,11,4,5,6,7,12,&t));
    if(fault==Fault::Acquire){model.fault=fault;assert(!a.begin(a.context,t,40000000,0,100));}
    else {
      assert(a.begin(a.context,t,40000000,0,100));
      risc_spi_frame_v1 frame={sizeof(frame),0x32,8,24,0x3c00,0,4,0};uint8_t data[512]{};
      model.fault=fault;assert(!q.exchange_frame(a.context,t,&frame,data,512));
      assert(!q.exchange_frame(a.context,t,&frame,data,1));assert(!a.release(a.context,t));
      if(fault==Fault::PollEnd){
        assert(model.pending && model.polling && !model.level[12]);
        auto& state=NativeShim::spi[0];assert(state.pending && state.pollingPending && state.framed.base.tx_buffer==state.tx);
        memset(data,0xff,sizeof(data));assert(state.tx[0]==0);
        assert(!a.end(a.context,t) && model.pending && !model.level[12]);
      }
    }
    model.fault=Fault::None;assert(a.end(a.context,t));assert(a.release(a.context,t));assert(!model.sdkOwned);
  }
  {
    Port p(hardware());p.hw_.spiTransferFrame=nullptr;model={};for(auto& s:NativeShim::spi)s={};
    stage_quad(root);current=&p;RiscBoot::Runtime r(runtime());assert(r.prepare(root.c_str()));
    assert(p.spis_[0].api.base.struct_size==RISC_SPI_QUAD_RAW_V1_SIZE && !p.spis_[0].api.exchange_frame);
  }
  std::puts("Native framed SPI: real header/data descriptors, continuation CS, host2/3, extents, deadlines and retained polling DMA PASS");
}
