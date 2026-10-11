#include "runtime/storage/ScopedUserVolume.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
extern "C" {
const risc_storage_volume_api_v1* scoped_fatfs_create();
bool scoped_fatfs_safe(void*);
void scoped_fatfs_fail_write(uint32_t);
uint32_t scoped_fatfs_writes();
void scoped_fatfs_destroy();
}
static bool owner(void*){return true;}
static void seed(const risc_storage_volume_api_v1*v,const char*p,const char*text){auto h=v->file_open_write(v->context,p);assert(h);assert(v->file_write(v->context,h,text,strlen(text))==strlen(text));assert(v->file_close(v->context,h,true));}
static int management(const char*mode,unsigned cut){
 const bool make=!strcmp(mode,"mkdir");
 const auto*provider=scoped_fatfs_create();const auto*upstream=risc_storage_volume_extension(provider);
 assert(upstream && upstream->mkdir(nullptr,"/User") && upstream->mkdir(nullptr,"/User/Books"));
 assert(upstream->mkdir(nullptr,"/User/Books/Nested") && upstream->mkdir(nullptr,"/Private"));
 seed(provider,"/Private/settings.json","secret");seed(provider,"/User/Books/Nested/entry.txt","preserved user data");
 RiscStorage::ScopedUserVolume volume({nullptr,owner,scoped_fatfs_safe});
 assert(volume.configure(provider,"/User","User files"));risc_storage_volume_api_v1_ext ext{};assert(volume.beginExtended(&ext));
 auto&api=ext.base;
 const auto initial=scoped_fatfs_writes();if(cut)scoped_fatfs_fail_write(cut);
 const bool ok=make?ext.mkdir(api.context,"/New user directory"):ext.rename(api.context,"/Books","/Moved user directory");
 if(cut){assert(!ok && volume.retained());const auto writes=scoped_fatfs_writes();
  assert(!ext.mkdir(api.context,"/retry"));assert(!ext.rename(api.context,"/Books","/retry"));
  assert(!api.refresh(api.context) && !volume.end());assert(writes==scoped_fatfs_writes());
  puts(make?"FatFs mkdir write-cut retained without retry":"FatFs rename write-cut retained without retry");return 0;
 }
 assert(ok);printf("%s_WRITES=%u\n",make?"MKDIR":"RENAME",scoped_fatfs_writes()-initial);
 if(make){assert(ext.rename(api.context,"/Books","/New user directory/Books"));}
 const char*folder=make?"/New user directory/Books/Nested":"/Moved user directory/Nested";
 char path[192]{};snprintf(path,sizeof(path),"%s/entry.txt",folder);
 uint64_t size=0;auto file=api.file_open_read(api.context,path,&size);assert(file && size==19);
 char bytes[32]{};assert(api.file_read(api.context,file,bytes,sizeof(bytes))==19);
 assert(!strcmp(bytes,"preserved user data") && !ext.handle_error(api.context,file,false));assert(api.file_close(api.context,file,true));
 assert(ext.rename(api.context,path,"/renamed.txt"));
 auto dir=api.dir_open(api.context,"/");assert(dir);risc_storage_dirent_v1 entry{};unsigned count=0;
 while(api.dir_next(api.context,dir,&entry)){assert(strcmp(entry.name,"Private"));++count;}
 assert(count==2 && !ext.handle_error(api.context,dir,true));assert(ext.dir_close_checked(api.context,dir));
 // Actual FAT case aliases remain exclusive and must not overwrite a file.
 const auto writes=scoped_fatfs_writes();
 assert(!ext.mkdir(api.context,"/RENAMED.TXT"));
 assert(!ext.rename(api.context,make?"/New user directory":"/Moved user directory","/RENAMED.TXT"));
 assert(writes==scoped_fatfs_writes() && !volume.retained());
 assert(volume.end());assert(volume.exitSafe());
 file=provider->file_open_read(provider->context,"/Private/settings.json",&size);assert(file && size==6);
 assert(provider->file_read(provider->context,file,bytes,sizeof(bytes))==6 && !memcmp(bytes,"secret",6));
 assert(provider->file_close(provider->context,file,true));scoped_fatfs_destroy();
 puts("Scoped management with actual production FatFs: mkdir, recursive directory move, file rename, checked close and case-alias collision PASS");return 0;
}
int main(int argc,char**argv){
 if(argc>=2 && (!strcmp(argv[1],"mkdir") || !strcmp(argv[1],"rename")))return management(argv[1],argc==3?unsigned(strtoul(argv[2],nullptr,10)):0);
 const auto*provider=scoped_fatfs_create();const auto*ext=risc_storage_volume_extension(provider);
 assert(ext && ext->mkdir(nullptr,"/User") && ext->mkdir(nullptr,"/User/Books") && ext->mkdir(nullptr,"/Private"));
 seed(provider,"/Private/settings.json","secret");seed(provider,"/User/Books/already.txt","old");
 RiscStorage::ScopedUserVolume volume({nullptr,owner,scoped_fatfs_safe});
 assert(volume.configure(provider,"/User","User files"));risc_storage_volume_api_v1 api{};assert(volume.begin(&api));
 const bool fault=argc==2;const unsigned cut=fault?unsigned(std::strtoul(argv[1],nullptr,10)):0;
 const unsigned initialWrites=scoped_fatfs_writes();
 if(fault)scoped_fatfs_fail_write(cut);
 const auto writer=api.file_open_write(api.context,"/Books/a long book name.txt");
 if(!writer){assert(volume.retained());puts("FatFs stage-create fault retained");return 0;}
 const char data[]="A user-owned book.";const size_t written=api.file_write(api.context,writer,data,sizeof(data));
 if(written!=sizeof(data)){assert(volume.retained());puts("FatFs data-write fault retained");return 0;}
 if(!fault){
  uint64_t size=999;bool directory=false;
  assert(!api.stat(api.context,"/Books/a long book name.txt",&size,&directory));
  assert(!api.stat(api.context,"/../Private/settings.json",&size,&directory));
  assert(!api.stat(api.context,"/Books/~r000001.tmp",&size,&directory));
  auto d=api.dir_open(api.context,"/Books");assert(d);risc_storage_dirent_v1 entry{};unsigned n=0;
  while(api.dir_next(api.context,d,&entry)){assert(!strncmp(entry.name,"already",7));++n;}assert(n==1);api.dir_close(api.context,d);
 }
 const bool committed=api.file_close(api.context,writer,true);
 if(fault && !committed){assert(volume.retained());const auto before=scoped_fatfs_writes();assert(!api.refresh(api.context));assert(!api.file_close(api.context,writer,false));assert(!volume.end());assert(scoped_fatfs_writes()==before);puts("FatFs commit fault retained without retry");return 0;}
 assert(committed);
 std::printf("COMMIT_WRITES=%u\n",scoped_fatfs_writes()-initialWrites);
 scoped_fatfs_fail_write(0);
 uint64_t size=0;auto read=api.file_open_read(api.context,"/Books/a long book name.txt",&size);assert(read && size==sizeof(data));char bytes[128]{};
 assert(api.file_read(api.context,read,bytes,sizeof(bytes))==sizeof(data) && !memcmp(bytes,data,sizeof(data)));assert(api.file_close(api.context,read,true));
 assert(!api.file_open_write(api.context,"/Books/already.txt"));
 auto aborted=api.file_open_write(api.context,"/Books/aborted.txt");assert(aborted);assert(api.file_write(api.context,aborted,data,4)==4);auto old=api;assert(volume.end());
 assert(!old.ready(old.context));assert(volume.begin(&api));bool dir=false;assert(!api.stat(api.context,"/Books/aborted.txt",&size,&dir));
 assert(api.remove(api.context,"/Books/a long book name.txt"));assert(volume.end());assert(volume.exitSafe());
 scoped_fatfs_destroy();puts("Scoped adapter with actual production FatFs volume: isolation, staging, commit, rollback, aliases, restart PASS");
}
