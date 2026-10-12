// Negative qualification probe for the exact pinned vendor sources.
// Expected result: SIGABRT at littlefs/lfs.c:262 in lfs_bd_prog. This is not
// part of the passing inspector/composition suite. Do not suppress assertions.
#include "native_littlefs_profile_fixture.h"
#include <cassert>
#include <cstdio>
#include <fcntl.h>
#include <vector>
int main(){
 assert(native_lfs_fixture_reset()==0);
 native_lfs_fixture_fault(NATIVE_LFS_FLASH_PROG,2,NATIVE_LFS_FAIL_BEFORE);
 const int fd=native_lfs_fixture_open("/private/n00000006/test",O_WRONLY|O_CREAT|O_TRUNC);
 fprintf(stderr,"program fault #2: actual VFS open returned %d; now write 8192 bytes\n",fd);
 assert(fd>=0);std::vector<unsigned char> bytes(8192,0xa5);
 (void)native_lfs_fixture_write(fd,bytes.data(),bytes.size());
 (void)native_lfs_fixture_close(fd);native_lfs_fixture_shutdown();
}
