// Shared low-level fixture for real external Watch app/driver modules.
// No renderer or application behavior is implemented here.
#pragma once
#include "ports/esp32s3/CpuPort.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
namespace WatchPeripheralModel {
struct Model {
  bool installFailure=false;
  bool pins[49]{},levels[49]{},i2c=false,spi=false,held=false,ready=false,badId=false,failSpi=false;
  uint8_t registers[2][256]{};uint64_t time=0;unsigned calls=0,rows=0,pwm=0,dateWrites=0;
  uint8_t command=0;unsigned row=0;std::vector<uint8_t> frame=std::vector<uint8_t>(240*240*2);
} model;
RiscCpu::Port* cpu=nullptr;
bool owner(){return true;}
uint64_t now(){return model.time;}
void delay(uint32_t ms){model.time+=ms;}
bool health(risc_runtime_health_v1* h){assert(++model.calls<10000);h->uptime_ms=0;return !model.ready;}
bool log(const char* line){puts(line);if(!strncmp(line,"WATCH_CLOCK ready",17))model.ready=true;return true;}
bool gpioOpen(uint8_t pin,bool out,bool initial,bool){assert(pin<49 && !model.pins[pin]);model.pins[pin]=true;model.levels[pin]=out?initial:true;return true;}
bool gpioWrite(uint8_t pin,bool value){assert(model.pins[pin]);model.levels[pin]=value;return true;}
bool gpioRead(uint8_t pin,bool* value){assert(model.pins[pin]);*value=model.levels[pin];return true;}
bool gpioPwm(uint8_t pin,uint32_t hz,uint16_t duty,uint16_t max){assert(pin==45 && hz==1000 && duty==40 && max==100);++model.pwm;return true;}
bool gpioClose(uint8_t pin){model.pins[pin]=false;return true;}
bool i2cOpen(uint8_t physical,uint8_t sda,uint8_t scl,uint32_t hz){assert(physical==0 && sda==10 && scl==11 && hz==100000 && !model.i2c);model.i2c=true;return !model.installFailure;}
bool i2cTransfer(uint8_t physical,uint8_t address,const uint8_t* tx,size_t tn,uint8_t* rx,size_t rn,uint32_t ms){
  assert(physical==0 && model.i2c && (address==0x34 || address==0x51) && tn && ms && ms<=1000);
  auto* registers=model.registers[address==0x51];unsigned reg=tx[0];assert(reg+rn<=256 && reg+tn-1<=256);
  if(tn>1){assert(!rn);for(size_t i=1;i<tn;++i){if(address==0x51 && reg>=2 && reg<=8)++model.dateWrites;registers[reg++]=tx[i];}}
  if(rn)memcpy(rx,registers+reg,rn);
  return true;
}
bool i2cClose(uint8_t physical){assert(physical==0);if(model.installFailure)return false;model.i2c=false;return true;}
bool spiOpen(uint8_t physical,int16_t sclk,int16_t mosi,int16_t miso){assert(physical==2 && sclk==18 && mosi==13 && miso==-1 && !model.spi);model.spi=true;return true;}
bool spiBegin(uint8_t physical,uint8_t cs,uint32_t hz,uint8_t mode,uint32_t ms){assert(physical==2 && cs==12 && hz==10000000 && !mode && ms && model.spi && !model.held);model.held=true;model.levels[cs]=false;return true;}
bool spiTransfer(uint8_t physical,const uint8_t* tx,uint8_t*,size_t n,uint32_t ms){
  assert(physical==2 && model.held && tx && n && ms);
  if(model.failSpi)return false;
  if(!model.levels[38]){assert(n==1);model.command=tx[0];}
  else if(model.command==0x2a){assert(n==4 && tx[0]==0 && tx[1]==0 && tx[2]==0 && tx[3]==239);}
  else if(model.command==0x2b){assert(n==4 && tx[0]==0 && tx[2]==0 && tx[1]==tx[3]);model.row=tx[1];}
  else if(model.command==0x2c){assert(n==480 && model.row<240 && model.row==model.rows);for(size_t i=0;i<n;i+=2){model.frame[model.row*480+i]=tx[i+1];model.frame[model.row*480+i+1]=tx[i];}++model.rows;}
  return true;
}
bool spiEnd(uint8_t physical,uint8_t cs,uint32_t){assert(physical==2 && cs==12 && model.held);model.held=false;model.levels[cs]=true;return true;}
bool spiClose(uint8_t physical){assert(physical==2 && !model.held);model.spi=false;return true;}
bool bind(RiscBoot::Runtime& runtime){return cpu->bind(runtime);}
void reset(bool valid,bool badId=false,bool failSpi=false){
  model=Model{};model.badId=badId;model.failSpi=failSpi;model.registers[0][3]=badId?0:0x4a;
  const uint8_t date[]={uint8_t(valid?0:0x80),0x34,0x12,0x29,2,2,0x28};memcpy(model.registers[1]+2,date,7);
}
}
