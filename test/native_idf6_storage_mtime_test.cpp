#include "native_littlefs_profile_fixture.h"
#include "experimental/native_replica/pinned_littlefs/littlefs_api.h"
#include "experimental/native_replica/pinned_littlefs/mtime32.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fcntl.h>
#include <utime.h>
extern "C" int native_lfs_fixture_utime(const char*,const struct utimbuf*);
static esp_littlefs_t* context(){return const_cast<esp_littlefs_t*>(static_cast<const esp_littlefs_t*>(native_lfs_fixture_context()));}
static std::vector<unsigned char> snapshot(){std::vector<unsigned char> b(native_lfs_fixture_flash_size());assert(native_lfs_fixture_snapshot(b.data(),b.size())==b.size());return b;}
int main(){
 static_assert(sizeof(time_t)==8,"Run the native migration test with a 64-bit libc time_t");
 assert(native_lfs_fixture_reset()==0);
 const char*path="/private/time";assert(native_lfs_fixture_seed(path,"old bytes",9)==0);
 const int64_t values[]={0,1,1700000000,INT32_MAX,-1,INT32_MIN};
 for(int64_t value:values){
  uint8_t legacy[4];assert(risc_littlefs_mtime_encode(time_t(value),legacy)==0);
  assert(lfs_setattr(context()->fs,path,'t',legacy,4)==0);
  auto old=snapshot();native_lfs_fixture_clear_counters();
  assert(native_lfs_fixture_reboot()==0);
  struct stat st{};errno=0;assert(native_lfs_fixture_stat(path,&st)==0&&st.st_mtime==value);
  int fd=native_lfs_fixture_open(path,O_RDONLY);assert(fd>=0);errno=0;assert(native_lfs_fixture_fstat(fd,&st)==0&&st.st_mtime==value);assert(native_lfs_fixture_close(fd)==0);
  assert(snapshot()==old);
  native_lfs_fixture_counters counts{};native_lfs_fixture_get_counters(&counts);assert(!counts.flash_program_bytes&&!counts.flash_erase_bytes);
  struct utimbuf times{time_t(value),time_t(value)};assert(native_lfs_fixture_utime(path,&times)==0);
  uint8_t actual[16]{};assert(lfs_getattr(context()->fs,path,'t',actual,sizeof actual)==4&&!memcmp(actual,legacy,4));
 }
 // Explicit native64 values cannot silently wrap or widen the frozen attribute.
 for(int64_t value:{int64_t(INT32_MAX)+1,int64_t(INT32_MIN)-1,INT64_MAX,INT64_MIN}){
  auto old=snapshot();native_lfs_fixture_clear_counters();struct utimbuf times{time_t(value),time_t(value)};errno=0;
  assert(native_lfs_fixture_utime(path,&times)==-1&&errno==EOVERFLOW);assert(snapshot()==old);
  native_lfs_fixture_counters counts{};native_lfs_fixture_get_counters(&counts);assert(!counts.flash_program_bytes&&!counts.flash_erase_bytes);
 }
 for(unsigned n:{0u,1u,3u,5u,8u,16u}){
  unsigned char invalid[16]{};assert(lfs_setattr(context()->fs,path,'t',invalid,n)==0);
  auto old=snapshot();struct stat st{};errno=0;assert(native_lfs_fixture_stat(path,&st)==-1&&errno==EIO);
  int fd=native_lfs_fixture_open(path,O_RDONLY);assert(fd>=0);errno=0;assert(native_lfs_fixture_fstat(fd,&st)==-1&&errno==EIO);assert(native_lfs_fixture_close(fd)==0);assert(snapshot()==old);
 }
 assert(lfs_removeattr(context()->fs,path,'t')==0);struct stat st{};errno=0;assert(native_lfs_fixture_stat(path,&st)==0&&errno==ENODATA);
 struct utimbuf times{123456789,123456789};assert(native_lfs_fixture_utime(path,&times)==0);
 // Physical read/program failures still surface at the real flash boundary.
 assert(native_lfs_fixture_reboot()==0);native_lfs_fixture_fault(NATIVE_LFS_FLASH_READ,1,NATIVE_LFS_FAIL_BEFORE);errno=0;
 assert(native_lfs_fixture_stat(path,&st)==-1&&errno==EIO);native_lfs_fixture_clear_faults();
 assert(native_lfs_fixture_reboot()==0);times.modtime++;native_lfs_fixture_fault(NATIVE_LFS_FLASH_PROG,1,NATIVE_LFS_FAIL_BEFORE);errno=0;
 assert(native_lfs_fixture_utime(path,&times)==-1&&errno==EIO);native_lfs_fixture_clear_faults();
 native_lfs_fixture_shutdown();puts("PASS: native64 VFS reads/writes signed LE32, rejects malformed/range, read-only reboot preservation, flash read/program fault propagation");
}
