// Test-owner RAM discard must not turn retained or dirty handles into a
// successful close, flush, retry or flash mutation. This is a host model only.
#include "native_littlefs_profile_fixture.h"
#include <cassert>
#include <cstdio>
#include <fcntl.h>
#include <vector>
int main(){
 assert(native_lfs_fixture_reset()==0);
 std::vector<unsigned char> payload(100,0x5a),before(native_lfs_fixture_flash_size()),after(before.size());
 int fd=native_lfs_fixture_open("/private/n00000006/dirty",O_WRONLY|O_CREAT);assert(fd>=0);
 assert(native_lfs_fixture_write(fd,payload.data(),payload.size())==100);
 void* dir=native_lfs_fixture_opendir("/private/n00000006");assert(dir);
 assert(native_lfs_fixture_snapshot(before.data(),before.size())==before.size());
 native_lfs_fixture_clear_counters();
 native_lfs_fixture_shutdown();
 assert(native_lfs_fixture_snapshot(after.data(),after.size())==after.size()&&before==after);
 native_lfs_fixture_counters c{};native_lfs_fixture_get_counters(&c);
 for(auto count:c.calls)assert(count==0);
 assert(!c.live_files&&!c.live_dirs&&!c.registered);
 assert(native_lfs_fixture_restore(before.data(),before.size())==0);
 fd=native_lfs_fixture_open("/private/n00000006/dirty",O_RDONLY);assert(fd>=0);
 native_lfs_fixture_fault(NATIVE_LFS_CLOSE,1,NATIVE_LFS_FAIL_BEFORE);assert(native_lfs_fixture_close(fd)<0);
 native_lfs_fixture_get_counters(&c);assert(c.live_files==1);
 assert(native_lfs_fixture_reboot()==0);
 native_lfs_fixture_get_counters(&c);assert(c.live_files==0&&c.registered);
 native_lfs_fixture_shutdown();puts("retained and dirty RAM-discard reboot smoke PASS");
}
