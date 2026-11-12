#include "native_radio_async_sdk_fixture.inc"
#include "ports/esp32s3/NativeRadioAsync.h"
#ifdef NDEBUG
#error Radio diagnostic tests require active assertions
#endif
namespace Async=RiscCpu::NativeRadioAsync;
int main(){
 assert(Async::allocateBuffers());
#if RISC_STAGE_LOGS
 Async::workerId=1;
 for(unsigned n=0;n<10000;++n){
  for(uint32_t stage=ApInfo;stage<=IpInfo;++stage){
   Async::capture(stage,0,true);Async::capture(stage,stage==NetifUp?1:0,false);
  }
 }
 assert(Async::recordWrite==6&&Async::recordRead==0&&Async::lost==0);
 // Changed failures and recovery survive even with no owner draining.
 Async::capture(ApInfo,0,true);Async::capture(ApInfo,12303,false);
 Async::capture(ApInfo,0,true);Async::capture(ApInfo,0,false);
 assert(Async::recordWrite==8&&Async::lost==0);
 Async::drain();assert(stageHas("code=12303")&&!stageHas("records_lost="));
 // A repeated status call that stalls still exposes its begin edge once.
 now=200000;Async::capture(ApInfo,0,true);now=300001;
 Async::drain();assert(stageCount("async latest")==1&&stageHas("stage=14 edge=begin"));
 Async::drain();assert(stageCount("async latest")==1);
 Async::capture(ApInfo,0,false);
 // A new operation does not inherit the previous operation's suppression.
 Async::workerId=2;const auto before=Async::recordWrite.load();
 Async::capture(ApInfo,0,true);Async::capture(ApInfo,0,false);
 assert(Async::recordWrite==before+2);
 Async::drain();assert(stageHas("op=2 stage=14 edge=begin"));
 // Distinct lifecycle events are never coalesced; saturation stays explicit.
 for(unsigned n=0;n<Async::RecordCount+3;++n)Async::capture(Init,n,false);
 assert(Async::lost==3);Async::drain();assert(stageHas("records_lost=3"));
#endif
 puts("PASS radio diagnostics: 60000 unchanged edges, changes, recovery, stalled calls, new operations and explicit overflow");
}
