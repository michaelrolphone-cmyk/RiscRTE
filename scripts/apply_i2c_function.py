"""Apply the tested generic function descriptor without weakening physical ownership."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
p=r/'src/bootstrap/Board.h';s=p.read_text()
if '#include <RiscI2cFunctionV1.h>' in s:raise SystemExit(0)
s=s.replace('#include <RiscDisplaySpiV2.h>','#include <RiscDisplaySpiV2.h>\n#include <RiscI2cFunctionV1.h>')
s=s.replace('      tw_hw_i2c_device_v1 peripheral;','      tw_hw_i2c_device_v1 peripheral;\n      risc_hw_i2c_function_v1 i2cFunction;');p.write_text(s)
p=r/'src/bootstrap/Board.cpp';s=p.read_text()
s=s.replace('  if (!strcmp(d->type,"peripheral.i2c"))','  if (!strcmp(d->type,"peripheral.i2c-function")) return d->config.i2cFunction.bus.instance_id;\n  if (!strcmp(d->type,"peripheral.i2c"))',1)
old='bool Board::materialize(JsonObjectConst c,Device& d) {'
new=old+'''
  if (!strcmp(d.type,"peripheral.i2c-function")) {
    auto& x=d.config.i2cFunction;x={};x.struct_size=sizeof(x);
    if (!keys(c,{"parent_instance_id","function_id"}) ||
        !number(c["parent_instance_id"],1,INT32_MAX,x.parent_instance_id) ||
        x.parent_instance_id==d.hardware.instance_id ||
        !number(c["function_id"],0,UINT32_MAX,x.function_id))return false;
    d.hardware.config=&x;d.hardware.config_size=sizeof(x);return true;
  }
'''
assert old in s;s=s.replace(old,new)
old='  for (size_t i=0;i<devicesCount_;++i) {\n    const auto& d=devices_[i];\n    for (size_t j=0;j<d.bindingCount;++j)'
new='''  // Resolve function identity only after every physical device was parsed.
  // JSON order does not decide ownership. Functions cannot parent functions,
  // select another address, duplicate an existing function or claim pins.
  for (size_t i=0;i<devicesCount_;++i) {
    auto& d=devices_[i];
    if (strcmp(d.type,"peripheral.i2c-function"))continue;
    auto& x=d.config.i2cFunction;
    const auto* parent=device(x.parent_instance_id);
    if (!parent)return fail("I2C function parent missing");
    const tw_hw_i2c_device_v1* physical=nullptr;
    if (!strcmp(parent->type,"peripheral.i2c"))physical=&parent->config.peripheral;
    else if (!strcmp(parent->type,"power.axp2101"))physical=&parent->config.power.device;
    if (!physical)return fail("I2C function requires physical peripheral parent");
    for(size_t j=0;j<i;++j)if(!strcmp(devices_[j].type,"peripheral.i2c-function")) {
      const auto& other=devices_[j].config.i2cFunction;
      if(other.parent_instance_id==x.parent_instance_id && other.function_id==x.function_id)
        return fail("duplicate I2C peripheral function");
    }
    x.bus=physical->bus;x.address=physical->address;x.chip_id=physical->chip_id;
  }
  for (size_t i=0;i<devicesCount_;++i) {
    const auto& d=devices_[i];
    for (size_t j=0;j<d.bindingCount;++j)'''
assert old in s;s=s.replace(old,new);p.write_text(s)
p=r/'platformio.ini';s=p.read_text();assert 'version = 0.3.2' in s;p.write_text(s.replace('version = 0.3.2','version = 0.3.3',1))
