#include "ports/esp32s3/NativeSpiffsFiles.h"
#include <cassert>
#include <cstdio>
#include <cstring>
extern "C" {void fixture_mount();void fixture_unmount();extern int fixture_short_write,fixture_fstat_fail,fixture_close_fail,fixture_truncate_fail,fixture_disappear;}
int main(){
 fixture_mount();RiscCpu::NativeSpiffsFiles provider("/bootfs");const auto&a=*provider.api();uint32_t h=0,n=0;uint64_t size=0,at=0;
 assert(a.open(a.context,"/absent",RISC_FILES_WRITE|RISC_FILES_APPEND,&h)==RISC_FILES_NOT_FOUND&&!h);
 assert(a.open(a.context,"/absent",RISC_FILES_WRITE|RISC_FILES_TRUNCATE,&h)==RISC_FILES_NOT_FOUND&&!h);
 assert(!a.open(a.context,"/sample",RISC_FILES_READ|RISC_FILES_WRITE|RISC_FILES_CREATE,&h));
 assert(!a.write(a.context,h,"abcdef",6,&n)&&n==6);assert(!a.close(a.context,h));
 assert(a.open(a.context,"/sample",RISC_FILES_WRITE|RISC_FILES_CREATE|RISC_FILES_EXCLUSIVE,&h)==RISC_FILES_EXISTS&&!h);
 assert(!a.open(a.context,"/sample",RISC_FILES_READ|RISC_FILES_WRITE|RISC_FILES_CREATE,&h));
 assert(!a.info(a.context,h,&size,&at)&&size==6&&at==0);assert(!a.seek(a.context,h,6));
 assert(!a.truncate(a.context,h,3));assert(!a.info(a.context,h,&size,&at)&&size==3&&at==3);
 assert(a.truncate(a.context,h,4)==RISC_FILES_INVALID);assert(a.seek(a.context,h,4)==RISC_FILES_INVALID);
 assert(!a.write(a.context,h,"d",1,&n)&&n==1);assert(!a.info(a.context,h,&size,&at)&&size==4&&at==4);assert(!a.close(a.context,h));
 assert(!a.open(a.context,"/sample",RISC_FILES_READ|RISC_FILES_WRITE|RISC_FILES_TRUNCATE,&h));assert(!a.info(a.context,h,&size,&at)&&size==0&&at==0);assert(!a.close(a.context,h));
 assert(!a.open(a.context,"/sample",RISC_FILES_READ|RISC_FILES_WRITE|RISC_FILES_APPEND,&h));
 fixture_short_write=1;assert(!a.write(a.context,h,"abcdef",6,&n)&&n==2);fixture_short_write=0;
 assert(!a.seek(a.context,h,0));assert(!a.write(a.context,h,"cd",2,&n)&&n==2);
 assert(!a.info(a.context,h,&size,&at)&&size==4&&at==4);assert(!a.seek(a.context,h,0));char bytes[8]{};
 assert(!a.read(a.context,h,bytes,sizeof(bytes),&n)&&n==4&&!memcmp(bytes,"abcd",4));assert(!a.read(a.context,h,bytes,sizeof(bytes),&n)&&!n);assert(!a.close(a.context,h));
 fixture_truncate_fail=1;assert(a.open(a.context,"/sample",RISC_FILES_WRITE|RISC_FILES_TRUNCATE,&h)==RISC_FILES_IO&&!h);fixture_truncate_fail=0;
 risc_storage_files_stat_v1 st{sizeof(st)};assert(!a.stat(a.context,"/sample",&st)&&st.size==4);
 fixture_fstat_fail=1;assert(a.open(a.context,"/sample",RISC_FILES_READ,&h)==RISC_FILES_IO&&!h);fixture_fstat_fail=0;
 assert(!a.open(a.context,"/sub/item",RISC_FILES_WRITE|RISC_FILES_CREATE,&h));assert(!a.close(a.context,h));
 assert(a.mkdir(a.context,"/sub")==RISC_FILES_UNAVAILABLE);assert(a.dir_open(a.context,"/missing",&h)==RISC_FILES_UNAVAILABLE&&!h);
 assert(a.rename(a.context,"/sample","/new")==RISC_FILES_UNAVAILABLE);
 assert(!a.stat(a.context,"/",&st)&&(st.flags&RISC_FILES_DIRECTORY));
 assert(!a.dir_open(a.context,"/",&h));uint64_t cursor=0;assert(!a.dir_tell(a.context,h,&cursor)&&cursor==0);
 risc_storage_files_entry_v1 e{sizeof(e)};unsigned seen=0;int rc=0;while((rc=a.dir_next(a.context,h,&e))==RISC_FILES_OK){assert(!e.flags);if(!strcmp(e.name,"sample")){assert(e.size==4);seen|=1;}else if(!strcmp(e.name,"sub/item"))seen|=2;else assert(false);}
 assert(rc==RISC_FILES_END&&seen==3);assert(!a.dir_rewind(a.context,h));assert(!a.dir_tell(a.context,h,&cursor)&&cursor==0);assert(!a.dir_next(a.context,h,&e));assert(!a.dir_tell(a.context,h,&cursor)&&cursor==1);assert(!a.dir_seek(a.context,h,0));assert(!a.dir_tell(a.context,h,&cursor)&&cursor==0);assert(a.dir_seek(a.context,h,999)!=RISC_FILES_OK);assert(!a.dir_close(a.context,h));
 fixture_disappear=1;assert(a.open(a.context,"/sample",RISC_FILES_WRITE|RISC_FILES_CREATE,&h)==RISC_FILES_NOT_FOUND&&!h);assert(a.stat(a.context,"/sample",&st)==RISC_FILES_NOT_FOUND);
 assert(!a.remove(a.context,"/sub/item"));assert(provider.exitSafe());
 assert(!a.open(a.context,"/retained",RISC_FILES_WRITE|RISC_FILES_CREATE,&h));fixture_close_fail=1;assert(a.close(a.context,h)==RISC_FILES_RETAINED);assert(provider.retained()&&!provider.exitSafe());assert(a.stat(a.context,"/retained",&st)==RISC_FILES_RETAINED);
 fixture_unmount();puts("Pinned SPIFFS provider: creation/exclusive/race, truncate/cursor, append/short writes, EOF/errors/custody, flat root/cursors and unsupported operations PASS");
}
