// Production parked loop, Runtime, diagnostic RX/TX and endpoint. SDK USB only
// is the finite host model; no USB hardware/IRQ or target timing is executed.
#define main existing_log_fixture_main
#include "native_log_retention_test.cpp"
#undef main
#include "ports/esp32s3/NativeSerialService.h"
#include "RuntimeFileFixture.h"
#include <fstream>
int main(int argc,char**argv){
  assert(argc==2);const std::string root=argv[1];
  std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
  std::ofstream(root+"/boot.json")<<R"({"board":"board.json","default_app":"never-start.elf","drivers":[]})";
  assert(RiscDiagnostics::prepareSerial());RiscPlatform::console.begin(115200);RiscDiagnostics::start();host=true;
  RiscBoot::RuntimeFiles unavailable;
  const RiscSerial::Namespace map[]={{12,"Unavailable SD",&unavailable,true}};
  N::Admission admission{map,1,ready};assert(N::prepare(&admission,owner,ready));
  assert(N::enableTransferMode(ready)); // paired eligibility tested by native bank adapter
  RiscBoot::Port port{owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}};
  port.appExitSafe=ready;port.providerStorageSafe=ready;
  port.serialFilesCheckpoint=N::checkpoint;port.serialFilesControlCheckpoint=N::controlCheckpoint;
  port.serialFilesShutdown=N::shutdown;port.serialFilesStop=N::stop;port.serialFilesPoll=RiscDiagnostics::poll;
  port.serialTransferRequested=N::transferRequested;port.serialTransferEnter=N::enterTransferMode;
  RiscTest::FileSources sources({root});RiscBoot::Runtime runtime(port);assert(sources.bind(runtime)&&runtime.prepare(root.c_str()));
  rx.insert(rx.end(),hello.begin(),hello.end());
  assert(runtime.run()); // early valid HELLO prevents nonexistent app from loading
  assert(runtime.nativeFilesServiceSafe()&&N::transferRequested()&&!N::transferActive()&&!N::outputSize()&&wire.empty());
  auto step=[&](){N::serviceParkedOwner(&runtime);irqDrain();};
  auto drain=[&](){for(unsigned i=0;i<400 && (N::outputSize()||!queued.empty());++i)step();assert(!N::outputSize()&&queued.empty());};
  step();drain();assert(N::transferActive()&&wire=="RTEF1 14cc1e14 1 0 HELLO 48 511 1\n");wire.clear();
  const std::string path="RTEF1 14cc1e14 2 PATH 0 2f417070732f616c61726d732f616c61726d732e656c66\n";
  assert(path.size()==71);rx.insert(rx.end(),path.begin(),path.end());
  const auto reads=rxReads;N::serviceParkedOwner(&runtime);
  printf("USB PATH rx_remaining=%zu read_calls=%u response_bytes=%zu\n",rx.size(),rxReads-reads,N::outputSize());fflush(stdout);
  assert(rx.empty()&&rxReads>=reads+71&&!N::outputSize()); // first checkpoint sees only64; second poll copies remaining7
  const auto before=rxReads;for(unsigned i=0;i<20;++i)irqDrain();assert(rxReads==before&&wire.empty());
  writeLimit=0;N::serviceParkedOwner(&runtime);assert(N::outputSize()&&wire.empty());
  writeLimit=17;drain();assert(wire=="RTEF1 14cc1e14 2 0 PATH 23\n");wire.clear();
  // Partial log line and a full TX queue do not suppress inbound request work.
  const std::string diagnostic(199,'d');writeLimit=17;RiscDiagnostics::line(diagnostic.c_str());RiscDiagnostics::poll();
  const std::string ns="RTEF1 14cc1e14 3 NS 0\n";rx.insert(rx.end(),ns.begin(),ns.end());
  for(unsigned i=0;i<500;++i)step();drain();assert(wire.find("RTEF1 14cc1e14 3 0 NS 12 ")!=std::string::npos);wire.clear();
  host=false;step();assert(N::transferActive()&&N::exitSafe());
  host=true;const std::string fresh="RTEF1 87654321 1 HELLO\n";rx.insert(rx.end(),fresh.begin(),fresh.end());
  step();drain();assert(wire=="RTEF1 87654321 1 0 HELLO 48 511 1\n");
  assert(runtime.stopNativeFiles());
  puts("Production parked owner loop PASS: early HELLO skips app, 71-byte PATH needs repeated poll/checkpoint, blocked/partial TX, diagnostic priority, disconnect/fresh SID; unavailable namespace catalogued");
}
