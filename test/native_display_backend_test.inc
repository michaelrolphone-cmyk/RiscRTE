static unsigned displayCalls=0;static bool displayOpenOk=true,displayCloseOk=true;
static const DisplayBackend displayBackend={
 [](const risc_hardware_device_v1&,uint8_t p){return p==2;},
 [](const risc_hardware_device_v1&,uint8_t){++displayCalls;return displayOpenOk;},
 [](uint32_t,const uint16_t*,size_t){++displayCalls;return true;},
 [](uint8_t){++displayCalls;return true;},
 [](){++displayCalls;return displayCloseOk;}
};
static void display_backend_tests(const std::string& root){
 for(bool success:{true,false}){
  model={};displayCalls=0;displayOpenOk=success;displayCloseOk=true;
  Port p(hardware());p.hw_.displayBackend=&displayBackend;current=&p;stage_quad(root);
  std::ifstream in(root+"/display.json");std::string manifest((std::istreambuf_iterator<char>(in)),{});
  const auto at=manifest.find("spi.bus");manifest.replace(at,7,"platform.display.backend");
  std::ofstream(root+"/display.json")<<manifest;
  RiscBoot::Runtime r(runtime());assert(r.prepare(root.c_str()));assert(!displayCalls && !model.calls && !p.spiCount_);
  auto& a=p.display_.api;uint64_t token=0;uint16_t row[410]{};
  model.owner=false;assert(!a.open(a.context,&token) && !token && !displayCalls);model.owner=true;
  assert(p.reserve(6,&r));assert(!a.open(a.context,&token) && !displayCalls);p.unreserve(6,&r);
  for(unsigned pin:{4,5,7,8,11,12})assert(!p.pins_[pin].owner);
  displayCloseOk=false;
  assert(a.open(a.context,&token)==success && token);
  for(unsigned pin:{4,5,6,7,8,11,12})assert(p.pins_[pin].owner==&p.display_);
  assert(!p.quiescent() && !p.restartResourcesSafe());
  assert(!a.row(a.context,token+1,0,row,410));assert(!a.row(a.context,token,502,row,410));
  assert(!a.row(a.context,token,0,row,409));
  if(success){assert(a.row(a.context,token,0,row,410));assert(a.brightness(a.context,token,80));}
  else assert(!a.row(a.context,token,0,row,410));
  assert(!a.close(a.context,token));assert(!p.providerStorageSafe());
  assert(!a.row(a.context,token,0,row,410));
  displayCloseOk=true;assert(a.close(a.context,token));assert(p.quiescent());
 }
 puts("Native display backend: no admission I/O, scoped pins, owner/token/row bounds, failed-open custody and cleanup retry PASS");
}
