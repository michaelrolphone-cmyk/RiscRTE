// Ownership and wire semantics are different invariants. These execute the
// actual CpuPort and NativeSpi code, with only IDF calls represented by the shim.
static void lease_tests(const std::string& root) {
  for(unsigned physical:{2u,3u}) {
    QuadFixture f(root,physical);auto& q=f.p.spis_[0].api;auto& a=q.base;
    uint64_t token=0;assert(q.claim_quad(a.context,11,4,5,6,7,12,&token));
    assert(!model.sdkOwned && !model.acquires);
    risc_spi_frame_v1 frame={sizeof(frame),2,8,24,0x5100,0,1,0};uint8_t value=0xd0;
    for(unsigned n=0;n<64;++n) {
      assert(a.begin(a.context,token,40000000,0,20));
      assert(q.exchange_frame(a.context,token,&frame,&value,1));
      assert(model.commandBits==8 && model.addressBits==24 && model.dummyBits==0);
      assert(model.commandLines==1 && model.addressLines==1 && model.dataLines==1);
      assert(a.end(a.context,token));
      assert(model.level[12] && model.sdkOwned && !model.pending);
      assert(model.acquires==1 && model.releases==0);
      assert(!NativeShim::spi[physical-2].held && NativeShim::spi[physical-2].sdkOwned);
    }
    assert(a.begin(a.context,token,20000000,0,20));
    assert(model.acquires==2 && model.releases==1 && model.removes==1);
    for(uint8_t lines:{uint8_t(1),uint8_t(4)}) {
      frame={sizeof(frame),0x1234,16,64,UINT64_C(0x123456789abcdef0),31,lines,0};
      assert(q.exchange_frame(a.context,token,&frame,&value,1));
      assert(model.commandBits==16 && model.addressBits==64 && model.dummyBits==31);
      assert(model.commandLines==1 && model.addressLines==1 && model.dataLines==lines);
      frame={sizeof(frame),0,0,0,0,0,lines,0};
      assert(q.exchange_frame(a.context,token,&frame,&value,1));
      assert(!model.commandBits && !model.addressBits && !model.dummyBits);
    }
    assert(a.end(a.context,token));assert(model.sdkOwned && model.releases==1);
    assert(a.release(a.context,token));
    assert(!model.sdkOwned && model.releases==2 && model.acquires==2);
  }
  // Failed removal must never double-release the SDK lease on cleanup retry.
  for(Fault failure:{Fault::Remove,Fault::Free,Fault::ClosePin}) {
    QuadFixture f(root);auto& q=f.p.spis_[0].api;auto& a=q.base;uint64_t token=0;
    assert(q.claim_quad(a.context,11,4,5,6,7,12,&token));assert(a.begin(a.context,token,40000000,0,20));
    assert(a.end(a.context,token));assert(model.sdkOwned);
    model.fault=failure;model.faultPin=6;
    assert(!a.release(a.context,token));assert(model.releases==1 && !model.sdkOwned);
    assert(!a.begin(a.context,token,40000000,0,20));
    model.fault=Fault::None;assert(a.release(a.context,token));assert(model.releases==1);
  }
  // A failed polling drain retains CS, data and the SDK lease until complete.
  {
    QuadFixture f(root);auto& q=f.p.spis_[0].api;auto& a=q.base;uint64_t token=0;
    assert(q.claim_quad(a.context,11,4,5,6,7,12,&token));assert(a.begin(a.context,token,40000000,0,20));
    uint8_t data[512]{};risc_spi_frame_v1 frame={sizeof(frame),0x32,8,24,0x3c00,0,4,0};
    model.fault=Fault::PollEnd;assert(!q.exchange_frame(a.context,token,&frame,data,sizeof(data)));
    assert(!a.end(a.context,token) && !a.release(a.context,token));
    assert(model.pending && model.sdkOwned && !model.level[12] && !model.releases);
    model.fault=Fault::None;assert(a.end(a.context,token));
    assert(!model.pending && model.sdkOwned && model.level[12] && !model.releases);
    assert(a.release(a.context,token));assert(model.releases==1);
  }
  std::puts("Native quad lease: 64 CS cycles/one lease, explicit frame lengths, reconfigure, teardown retry and retained-DMA cleanup PASS");
}
