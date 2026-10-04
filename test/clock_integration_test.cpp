// Only the physical pins/controllers/register transfers are modeled. The actual
// Runtime, Graph, CpuPort, five external drivers, clock app and renderer execute.
#include "support/WatchPeripheralModel.h"
using namespace WatchPeripheralModel;
int main(int argc,char** argv){
  assert(argc==2);const std::string root=argv[1];
  for(unsigned scenario=0;scenario<5;++scenario){
    reset(scenario!=1,scenario==2,scenario==3);model.installFailure=scenario==4;
    RiscCpu::Port port({owner,now,delay,gpioOpen,gpioWrite,gpioRead,gpioPwm,gpioClose,i2cOpen,i2cTransfer,i2cClose,spiOpen,spiBegin,spiTransfer,spiEnd,spiClose});cpu=&port;
    RiscBoot::Runtime runtime({owner,health,delay,log,bind});
    if(!runtime.prepare(root.c_str())){fprintf(stderr,"prepare: %s\n",runtime.error());return 1;}
    assert(port.quiescent() && !model.time && !model.calls && !model.i2c && !model.spi);
    const bool ran=runtime.run();
    if(scenario<2){
      if(!ran){fprintf(stderr,"run: %s\n",runtime.error());return 1;}
      assert(model.ready && model.rows==240 && model.pwm==1);
      std::ifstream f(root+(scenario?"/unset.rgb565":"/valid.rgb565"),std::ios::binary);
      const std::vector<uint8_t> golden{std::istreambuf_iterator<char>(f),{}};
      assert(golden.size()==115200 && golden==model.frame);
    } else {assert(!ran && !model.ready && !model.rows);}
    if(scenario==4){
      assert(model.i2c && !model.spi && !model.dateWrites && !port.quiescent());
      puts("Failed native I2C install/cleanup retains physical ownership and poisons port PASS");
      continue; // Deliberately retained until restart, not reported as cleanup.
    }
    assert(!model.dateWrites && !model.i2c && !model.spi && !model.held && port.quiescent());
    for(bool pin:model.pins)assert(!pin);
    assert(model.registers[0][0x90]==0 && model.registers[0][0x93]==0 && model.registers[0][0x94]==0);
  }
  puts("Actual runtime + external GPIO/I2C/PMU/panel/RTC + clock ELF: exact valid/unset frames, no RTC time writes, PMU-ID/SPI failure rollback and zero live resources PASS");
}
