#include "native_littlefs_profile_fixture.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <fcntl.h>
#include <vector>
int main(){
 assert(native_lfs_fixture_reset()==0);
 const unsigned chunk=std::getenv("PROBE_CHUNK")?unsigned(std::strtoul(std::getenv("PROBE_CHUNK"),nullptr,10)):8192;
 assert(chunk && chunk<=8192);
 const bool afterOpen=std::getenv("PROBE_FAULT_AFTER_OPEN")!=nullptr;
 if(!afterOpen)native_lfs_fixture_fault(NATIVE_LFS_FLASH_PROG,2,NATIVE_LFS_FAIL_BEFORE);
 errno=0;
 const int fd=native_lfs_fixture_open("/private/n00000006/test",O_WRONLY|O_CREAT|O_TRUNC);
 fprintf(stderr,"open returned %d errno %d; chunk %u fault_after_open %d\n",fd,errno,chunk,int(afterOpen));assert(fd>=0);
 if(afterOpen)native_lfs_fixture_fault(NATIVE_LFS_FLASH_PROG,2,NATIVE_LFS_FAIL_BEFORE);
 std::vector<unsigned char>bytes(8192,0xa5);
 for(unsigned at=0;at<bytes.size();at+=chunk){unsigned count=unsigned(bytes.size())-at;if(count>chunk)count=chunk;
  fprintf(stderr,"before write at %u size %u\n",at,count);
  errno=0; const auto result=native_lfs_fixture_write(fd,bytes.data()+at,count);fprintf(stderr,"after write at %u result %lld errno %d\n",at,(long long)result,errno);
  if(result!=count)break;
 }
 fprintf(stderr,"before close\n");int rc=native_lfs_fixture_close(fd);fprintf(stderr,"after close %d errno %d\n",rc,errno);
 fprintf(stderr,"before shutdown\n");native_lfs_fixture_shutdown();fprintf(stderr,"after shutdown\n");
}
