#include "experimental/native_replica/CheckedFirstSdCheckpoint.h"
using First=RiscExperimental::CheckedFirstSdCheckpoint;
void first_checkpoint_target_probe(First&first,const risc_storage_volume_api_v1*volume){
 (void)first.start(volume);first.step();(void)first.prepareNative();
 (void)first.acknowledgeNative();first.cancel();(void)first.exitSafe();
}
extern "C" {
extern const unsigned char first_checkpoint_layout[sizeof(First)];
const unsigned char first_checkpoint_layout[sizeof(First)]={};
}
