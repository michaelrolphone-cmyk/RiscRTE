// Dedicated native host primitives for run_provider_diagnostic_composition_test.
// This deliberately does not link the target UART/USB driver or execute Xtensa.
#include "provider_diagnostic_native_shim/Fixture.h"
#include "ports/esp32s3/NativePlatform.h"
#include <algorithm>
#include <cassert>
#include <cstring>
extern "C" uint32_t esp_get_free_heap_size(void){return 1024u*1024u;}
extern "C" int xPortInIsrContext(void){return diagnosticIsr;}
namespace RiscPlatform {
Console console;
void Console::begin(unsigned baud){assert(baud==115200);installed_=true;}
void Console::end(){installed_=false;}
Console::operator bool()const{return installed_ && ProviderDiagnosticTest::console.connected;}
int Console::available(){return installed_?0:-1;}
int Console::read(){return -1;}
int Console::availableForWrite()const{
  return installed_?int(std::min<size_t>(ProviderDiagnosticTest::console.space,128)):0;
}
size_t Console::write(const uint8_t* bytes,size_t size){
  auto& state=ProviderDiagnosticTest::console;
  assert(installed_ && state.connected && bytes && size<=64 && size<=state.space);
  ++state.writes;state.output.append(reinterpret_cast<const char*>(bytes),size);return size;
}
size_t Console::println(const char* text){
  if(!text)return 0;
  const size_t size=std::strlen(text);assert(size<=63);
  const auto written=write(reinterpret_cast<const uint8_t*>(text),size);
  const uint8_t newline='\n';return written+write(&newline,1);
}
size_t Console::setTxBufferSize(size_t size){txBytes_=size;return size;}
}
