#include "runtime/storage/ScopedUserVolume.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
extern "C" {
const risc_storage_volume_api_v1* scoped_fatfs_create();
bool scoped_fatfs_safe(void*);
void scoped_fatfs_fail_write(uint32_t);
uint32_t scoped_fatfs_writes();
void scoped_fatfs_destroy();
bool scoped_browser_copy(const risc_storage_volume_api_v1*,const char*,const char*);
bool scoped_browser_copy_safe();
bool scoped_browser_retry_close();
}
static bool owner(void*){return true;}
int main(int argc,char**argv){
 const unsigned cut=argc==2?unsigned(strtoul(argv[1],nullptr,10)):0;
 const auto*provider=scoped_fatfs_create();const auto*ext=risc_storage_volume_extension(provider);
 assert(ext->mkdir(provider->context,"/User") && ext->mkdir(provider->context,"/User/Copies"));
 std::string content(5000,'x');for(size_t i=0;i<content.size();++i)content[i]=char(i%251);
 auto seed=provider->file_open_write(provider->context,"/User/source.bin");assert(seed);
 for(size_t offset=0;offset<content.size();){size_t got=provider->file_write(provider->context,seed,content.data()+offset,content.size()-offset);assert(got);offset+=got;}
 assert(provider->file_close(provider->context,seed,true));
 RiscStorage::ScopedUserVolume volume({nullptr,owner,scoped_fatfs_safe});assert(volume.configure(provider,"/User","User files"));
 risc_storage_volume_api_v1_ext scoped{};assert(volume.beginExtended(&scoped));auto&a=scoped.base;
 const auto before=scoped_fatfs_writes();if(cut)scoped_fatfs_fail_write(cut);
 const bool copied=scoped_browser_copy(&a,"/source.bin","/Copies/copy.bin");
 if(cut){
  assert(!copied && volume.retained());const auto stopped=scoped_fatfs_writes();
  if(!scoped_browser_copy_safe())assert(!scoped_browser_retry_close());
  assert(!volume.end() && !a.ready(a.context));assert(stopped==scoped_fatfs_writes());
  printf("Actual browser/FatFs copy write cut %u retained without retry PASS\n",cut);return 0;
 }
 assert(copied && scoped_browser_copy_safe() && !volume.retained());
 printf("COPY_WRITES=%u\n",scoped_fatfs_writes()-before);
 for(const char*path:{"/source.bin","/Copies/copy.bin"}){
  uint64_t size=0;auto file=a.file_open_read(a.context,path,&size);assert(file && size==content.size());
  std::string read(size,'\0');size_t offset=0;
  while(offset<size){const size_t n=a.file_read(a.context,file,&read[offset],size-offset);assert(n && n<=512);offset+=n;}
  assert(read==content && a.file_close(a.context,file,true));
 }
 const auto writes=scoped_fatfs_writes();assert(!scoped_browser_copy(&a,"/source.bin","/Copies/COPY.BIN"));
 assert(writes==scoped_fatfs_writes() && scoped_browser_copy_safe());
 assert(volume.end() && volume.exitSafe());scoped_fatfs_destroy();
 puts("Actual shared browser + scoped volume + unchanged production FatFs: same-volume copy and case-alias no-overwrite PASS");
}
