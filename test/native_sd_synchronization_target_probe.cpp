#include "experimental/native_replica/NativeSdSynchronization.h"
using Sync=RiscExperimental::NativeSdSynchronization;
extern "C" unsigned char native_sd_synchronization_size[sizeof(Sync)];
unsigned char native_sd_synchronization_size[sizeof(Sync)];
int native_sd_synchronization_probe(Sync&s,RiscExperimental::NativeSdPortAttempt&a){
 int rc=s.registerBeforeActivate();if(!rc)rc=s.begin(a);if(!rc)rc=s.poll();s.cancel();
 return s.exitSafe()?rc:s.error();
}
