#include "runtime/storage/PackageFiles.h"
#include <RiscStorageAsyncAdapterV1.h>
#include <cassert>
#include <cstdlib>
#include <initializer_list>
#include <cstdio>
#include <cstring>
#include <climits>
#include <unistd.h>
#include <sys/stat.h>
static bool held=false,failed=false;static unsigned task=1,owner=0;
int main(){
 char temp[]="/tmp/risc-package-XXXXXX",root[PATH_MAX];assert(mkdtemp(temp)&&realpath(temp,root));
 RiscStorage::PackageFiles files({nullptr,[](void*){if(held)return owner==task;owner=task;held=true;return true;},[](void*){assert(held&&owner==task);held=false;owner=0;},[](void*){return failed;},[](void*,uint64_t*t,uint64_t*u){*t=524288;*u=65536;return true;}});
 assert(files.configure(root));auto*v=files.api();auto*f=risc_storage_volume_fs(v);auto*e=risc_storage_volume_extension(v);void*c=v->context;
 assert(v->refresh(c)&&!held);assert(e->mkdir(c,"/Apps/example"));assert(!e->mkdir(c,"/../escape"));assert(!e->mkdir(c,"/Apps/../../escape"));
 assert(!e->mkdir(c,"/.native-settings"));uint64_t free=0;assert(files.freeBytes(&free)&&free==319488);
 auto h=v->file_open_write(c,"/Apps/example/image.elf");assert(h&&held&&!files.exitSafe());
 task=2;assert(v->file_write(c,h,"evil",4)==0);assert(!v->file_close(c,h,true));task=1;
 assert(v->file_write(c,h,"IMAGE",5)==5);assert(e->file_sync(c,h));assert(v->file_close(c,h,true)&&!held&&files.exitSafe());assert(!v->file_close(c,h,true));
 assert(!v->file_open_write(c,"/Apps/example/image.elf"));uint64_t size=0;auto read=v->file_open_read(c,"/Apps/example/image.elf",&size);assert(read!=h&&size==5);char bytes[6]{};assert(v->file_read(c,read,bytes,5)==5&&!strcmp(bytes,"IMAGE"));assert(v->file_close(c,read,false)&&!held);
 // Ordinary existing-write close(false) must preserve the stage, unlike the
 // exclusive legacy writer abort. Reopen never creates or truncates a file.
 assert(!e->file_open(c,"/Apps/example/missing",RISC_STORAGE_OPEN_WRITE));
 auto append=e->file_open(c,"/Apps/example/image.elf",RISC_STORAGE_OPEN_WRITE);assert(append);
 uint64_t length=0,offset=0;assert(e->file_info(c,append,&length,&offset)&&length==5&&offset==0);
 assert(e->file_seek(c,append,length));assert(v->file_write(c,append,"MORE",4)==4);assert(e->file_sync(c,append));
 assert(v->file_close(c,append,false));read=v->file_open_read(c,"/Apps/example/image.elf",&size);assert(read&&size==9);assert(v->file_close(c,read,false));
 risc_storage_async_adapter_v1 bridge{};bridge.volume=v;bridge.ext=e;bridge.fs=f;
 risc_storage_package_request_v1 request{};risc_storage_async_result_v1 result{};
 assert(risc_storage_package_append_at(&request,"/Apps/example/chunks",0,"ONE",3));
 assert(risc_storage_package_execute(&bridge,&request,&result)==RISC_STORAGE_ASYNC_OK);
 assert(risc_storage_package_append_at(&request,"/Apps/example/chunks",3,"TWO",3));
 assert(risc_storage_package_execute(&bridge,&request,&result)==RISC_STORAGE_ASYNC_OK);
 assert(risc_storage_package_execute(&bridge,&request,&result)==RISC_STORAGE_ASYNC_IO); // no blind retry
 read=v->file_open_read(c,"/Apps/example/chunks",&size);assert(read&&size==6);char chunks[7]{};
 assert(v->file_read(c,read,chunks,6)==6&&!strcmp(chunks,"ONETWO"));assert(v->file_close(c,read,false));
 assert(v->remove(c,"/Apps/example/chunks"));
 assert(!files.removeDirectory("/Apps/example"));assert(!files.removeDirectory("/Apps"));
 assert(e->mkdir(c,"/Drivers/.empty"));assert(files.removeDirectory("/Drivers/.empty"));
 auto stage=v->file_open_write(c,"/Apps/example/manifest.install");assert(stage);assert(v->file_write(c,stage,"NEW",3)==3);assert(v->file_close(c,stage,true));assert(f->replace_file(c,"/Apps/example/manifest.install","/Apps/example/manifest.json"));assert(f->recover_replace(c,"/Apps/example/manifest.json"));
 auto aborted=v->file_open_write(c,"/Apps/example/.partial");assert(aborted&&v->file_close(c,aborted,false));risc_storage_metadata_v1 m{sizeof(m),0,0,0};assert(f->metadata(c,"/Apps/example/.partial",&m)==RISC_STORAGE_FS_NOT_FOUND);
 auto d=v->dir_open(c,"/Apps/example");assert(d&&held);risc_storage_dirent_v1 entry{};unsigned count=0;while(v->dir_next(c,d,&entry))++count;assert(count==2&&!e->handle_error(c,d,true));assert(e->dir_close_checked(c,d)&&!held);
 char link[PATH_MAX+32];snprintf(link,sizeof(link),"%s/Apps/link",root);assert(!symlink("/tmp",link));assert(!v->file_open_write(c,"/Apps/link/escape"));assert(!unlink(link));
 assert(v->remove(c,"/Apps/example/image.elf")&&v->remove(c,"/Apps/example/manifest.json"));
 char path[PATH_MAX+32];for(const char*p:{"/Apps/example","/Apps","/Drivers","/AppData"}){snprintf(path,sizeof(path),"%s%s",root,p);assert(!rmdir(path));}assert(!rmdir(root));
 failed=true;assert(!v->refresh(c));puts("Internal package files: confined paths, real file lifecycle, atomic activation, exclusive custody, abort, stale handles and capacity reserve PASS");
}
