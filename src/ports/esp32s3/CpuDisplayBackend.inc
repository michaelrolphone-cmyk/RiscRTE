// Generic exclusive native display broker. Hardware-specific code is supplied
// only by an explicit device build, never selected by an app or boot filename.
bool Port::displayOpen(void* context,uint64_t* out){
 if(out)*out=0;
 auto& c=*static_cast<Display*>(context);auto& p=*c.port;
 if(!out || !p.available() || p.sleepRetained_ || p.transferring_ || c.token || p.spiBuses_[c.physical-2].refs)return false;
 const auto& d=c.device->config.displayQuad;const auto& b=d.base;
 const int16_t pins[]={b.bus.sclk,b.bus.mosi,b.bus.miso,d.data2,d.data3,b.cs,b.reset};
 for(auto pin:pins)if(!p.reserve(pin,&c)){for(auto old:pins)p.unreserve(old,&c);return false;}
 const uint64_t token=p.token();if(!token){for(auto pin:pins)p.unreserve(pin,&c);return false;}
 c.token=token;c.closing=true;*out=token;
 p.transferring_=true;bool ok=p.hw_.displayBackend->open(c.device->hardware,c.physical);p.transferring_=false;
 if(ok)c.closing=false;
 else if(displayClose(context,token))*out=0;
 return ok;
}
bool Port::displayRow(void* context,uint64_t token,uint32_t y,const uint16_t* pixels,size_t count){
 auto& c=*static_cast<Display*>(context);auto& p=*c.port;
 if(!p.available() || p.sleepRetained_ || p.transferring_ || !token || token!=c.token || c.closing ||
    !pixels || count!=c.device->display().width || y>=c.device->display().height)return false;
 p.transferring_=true;bool ok=p.hw_.displayBackend->row(y,pixels,count);p.transferring_=false;
 if(!ok)c.closing=true;return ok;
}
bool Port::displayBrightness(void* context,uint64_t token,uint8_t value){
 auto& c=*static_cast<Display*>(context);auto& p=*c.port;
 if(!p.available() || p.sleepRetained_ || p.transferring_ || !token || token!=c.token || c.closing)return false;
 p.transferring_=true;bool ok=p.hw_.displayBackend->brightness(value);p.transferring_=false;
 if(!ok)c.closing=true;return ok;
}
bool Port::displayClose(void* context,uint64_t token){
 auto& c=*static_cast<Display*>(context);auto& p=*c.port;
 if(!p.hw_.owner || !p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || p.transferring_ || !token || token!=c.token)return false;
 c.closing=true;p.transferring_=true;bool ok=p.hw_.displayBackend->close();p.transferring_=false;
 if(!ok)return false;
 const auto& d=c.device->config.displayQuad;const auto& b=d.base;
 for(int16_t pin:{b.bus.sclk,b.bus.mosi,b.bus.miso,d.data2,d.data3,b.cs,b.reset})p.unreserve(pin,&c);
 c.token=0;c.closing=false;return true;
}
