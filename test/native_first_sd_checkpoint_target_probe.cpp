#include "experimental/native_replica/CheckedFirstSdCheckpoint.h"
using First=RiscExperimental::CheckedFirstSdCheckpoint;
void first_checkpoint_target_probe(First&first,const risc_storage_volume_api_v1*volume){
 (void)first.start(volume);first.step();(void)first.prepareNative();
 (void)first.acknowledgeNative();first.cancel();(void)first.exitSafe();
}
void first_checkpoint_native_target_probe(First&first,RiscExperimental::NativeSdAttempt&attempt){
 (void)first.prepareNative(attempt);(void)first.preparationWorkerStep();(void)first.collectPreparation();
 first.step();(void)first.issueNativeArm();(void)first.nativeArmWorkerStep();(void)first.collectNativeArm();
 first.step();(void)first.issueNativeCompletion();(void)first.nativeCompletionWorkerStep();(void)first.collectNativeCompletion();
 first.cancel();(void)first.state();(void)first.reason();(void)first.error();(void)first.exitSafe();
}
extern "C" {
extern const unsigned char first_checkpoint_layout[sizeof(First)];
const unsigned char first_checkpoint_layout[sizeof(First)]={};
}
