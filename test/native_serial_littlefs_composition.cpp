// Host-only composition of the actual native mount fence and pinned LittleFS.
#include "ports/esp32s3/NativeAppDataMount.h"
#include "native_littlefs_profile_fixture.h"
#include <cassert>
#include <cstring>
#include <fcntl.h>
namespace M=RiscAppDataMount;
static bool radio;
extern "C" void native_lfs_fixture_select_appdata(void);
extern "C" void* xTaskGetCurrentTaskHandle(){static int owner;return &owner;}
extern "C" bool native_mount_dram(const void*){return true;}
namespace RiscCpu {
bool nativeBackgroundResourceTry(){if(radio)return false;radio=true;return true;}
void nativeBackgroundResourceEnd(){assert(radio);radio=false;}
}
extern "C" void serial_littlefs_start(){
  assert(native_lfs_fixture_reset()==0);
  assert(native_lfs_fixture_mkdir("/n00000001")==0);
  assert(native_lfs_fixture_seed("/n00000001/original","app sibling",11)==0);
  assert(native_lfs_fixture_mkdir("/.native-settings")==0);
  assert(native_lfs_fixture_seed("/.native-settings/original","settings sibling",16)==0);
  native_lfs_fixture_shutdown();native_lfs_fixture_select_appdata();
  assert(M::prepare(static_cast<const esp_partition_t*>(native_lfs_fixture_partition())));
  assert(native_lfs_fixture_reboot()==0&&M::finish());
}
extern "C" void serial_littlefs_preserved(){
  native_lfs_fixture_counters counts{};native_lfs_fixture_get_counters(&counts);
  assert(!counts.live_files&&!counts.live_dirs&&!M::held()&&!M::retained()&&!radio);
  for(unsigned i=0;i<2;++i){
    M::Guard guard(i?M::Access::Settings:M::Access::AppData);assert(guard.held);
    int fd=native_lfs_fixture_open(i?"/.native-settings/original":"/n00000001/original",O_RDONLY);assert(fd>=0);
    char bytes[32]{};const char*expected=i?"settings sibling":"app sibling";
    auto n=native_lfs_fixture_read(fd,bytes,sizeof(bytes));assert(n==static_cast<int>(strlen(expected))&&!memcmp(bytes,expected,n));
    assert(native_lfs_fixture_close(fd)==0);
  }
}
extern "C" void serial_littlefs_stop(){serial_littlefs_preserved();native_lfs_fixture_shutdown();}
