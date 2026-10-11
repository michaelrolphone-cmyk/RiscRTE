/* Hardware one-bit SDMMC, scoped through the existing selected GPIO bank.
 * Included inside namespace RiscCpu after Port's pin reservation helpers. */
bool Port::sdmmcOpen(void* context,uint8_t clk,uint8_t cmd,uint8_t dat0,uint32_t hz,
                     uint64_t* out,risc_sdmmc_card_info_v1* info){
  if(out)*out=0;
  if(info)*info={};
  if(!context || !out || !info)return false;
  auto& g=*static_cast<Gpio*>(context);if(!g.port)return false;auto& p=*g.port;auto& s=p.sdmmc_;
  const uint64_t clockMask=pinBit(clk),commandMask=pinBit(cmd),dataMask=pinBit(dat0);
  if(!p.available() || p.sleepRetained_ || p.transferring_ || s.token || s.closing ||
     !p.hw_.sdmmcOpen || !p.hw_.sdmmcRead || !p.hw_.sdmmcWrite || !p.hw_.sdmmcSync || !p.hw_.sdmmcClose ||
     !clockMask || !commandMask || !dataMask || clk==cmd || clk==dat0 || cmd==dat0 ||
     !(g.output&clockMask) || (g.input&commandMask)!=commandMask || (g.output&commandMask)!=commandMask ||
     (g.input&dataMask)!=dataMask || (g.output&dataMask)!=dataMask ||
     (g.pullup&(commandMask|dataMask))!=(commandMask|dataMask) ||
     hz!=RISC_SDMMC_MAX_HZ || p.serial_==UINT64_MAX)return false;
  if(!p.reserve(clk,&s))return false;
  if(!p.reserve(cmd,&s)){p.unreserve(clk,&s);return false;}
  if(!p.reserve(dat0,&s)){p.unreserve(clk,&s);p.unreserve(cmd,&s);return false;}
  s.owner=&g;s.clk=clk;s.cmd=cmd;s.dat0=dat0;s.token=p.token();s.closing=true;*out=s.token;
  p.transferring_=true;
  risc_sdmmc_card_info_v1 discovered{};
  const bool opened=p.hw_.sdmmcOpen(clk,cmd,dat0,hz,&discovered);
  p.transferring_=false;
  if(opened && p.available() && discovered.struct_size==sizeof(discovered) && !discovered.reserved &&
     discovered.sector_size==RISC_SDMMC_SECTOR_BYTES && discovered.sector_count &&
     discovered.sector_count<=uint64_t(UINT32_MAX)+1u && discovered.clock_hz && discovered.clock_hz<=hz){
    s.sectors=discovered.sector_count;s.closing=false;*info=discovered;return true;
  }
  // Card absence is recoverable only after the same backend proves teardown.
  if(sdmmcRelease(context,s.token))*out=0;
  return false;
}
bool Port::sdmmcTransfer(void* context,uint64_t token,uint64_t lba,uint32_t count,void* out,const void* data){
  if(!context || !token || (bool(out)==bool(data)))return false;
  auto& g=*static_cast<Gpio*>(context);if(!g.port)return false;auto& p=*g.port;auto& s=p.sdmmc_;
  const auto address=reinterpret_cast<uintptr_t>(out?out:data);
  if(!p.available() || p.sleepRetained_ || p.transferring_ || s.owner!=&g || s.token!=token || s.closing ||
     !count || count>RISC_SDMMC_MAX_SECTORS || lba>=s.sectors || count>s.sectors-lba ||
     address>UINTPTR_MAX-size_t(count)*RISC_SDMMC_SECTOR_BYTES)return false;
  p.transferring_=true;
  const bool ok=out?p.hw_.sdmmcRead(lba,count,out):p.hw_.sdmmcWrite(lba,count,data);
  p.transferring_=false;
  if(!ok || !p.available()){s.closing=true;return false;}
  return true;
}
bool Port::sdmmcRead(void* context,uint64_t token,uint64_t lba,uint32_t count,void* out){
  return sdmmcTransfer(context,token,lba,count,out,nullptr);
}
bool Port::sdmmcWrite(void* context,uint64_t token,uint64_t lba,uint32_t count,const void* data){
  return sdmmcTransfer(context,token,lba,count,nullptr,data);
}
bool Port::sdmmcSync(void* context,uint64_t token){
  if(!context || !token)return false;
  auto& g=*static_cast<Gpio*>(context);if(!g.port)return false;auto& p=*g.port;auto& s=p.sdmmc_;
  if(!p.available() || p.sleepRetained_ || p.transferring_ || s.owner!=&g || s.token!=token || s.closing)return false;
  p.transferring_=true;const bool ok=p.hw_.sdmmcSync();p.transferring_=false;
  if(!ok || !p.available()){s.closing=true;return false;}return true;
}
bool Port::sdmmcRelease(void* context,uint64_t token){
  if(!context || !token)return false;
  auto& g=*static_cast<Gpio*>(context);if(!g.port)return false;auto& p=*g.port;auto& s=p.sdmmc_;
  if(!p.hw_.owner || !p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || p.transferring_ ||
     s.owner!=&g || s.token!=token || !p.hw_.sdmmcClose)return false;
  s.closing=true;p.transferring_=true;const bool ok=p.hw_.sdmmcClose();p.transferring_=false;
  if(!ok || !p.hw_.owner())return false;
  p.unreserve(s.clk,&s);p.unreserve(s.cmd,&s);p.unreserve(s.dat0,&s);s={};return true;
}
