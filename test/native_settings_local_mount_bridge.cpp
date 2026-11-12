#include "ports/esp32s3/NativeAppDataMount.h"
#include "native_settings_local_mount_bridge.h"
namespace NativeLocalMount {
bool prepare(const void*p){return RiscAppDataMount::prepare(static_cast<const esp_partition_t*>(p));}
bool finish(){return RiscAppDataMount::finish();}
bool retained(){return RiscAppDataMount::retained();}
bool held(){return RiscAppDataMount::held();}
bool enterAppData(){return RiscAppDataMount::enter(RiscAppDataMount::Access::AppData);}
void leave(){RiscAppDataMount::leave();}
}
