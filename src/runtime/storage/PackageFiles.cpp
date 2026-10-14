#include "PackageFiles.h"
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <climits>
namespace RiscStorage {
PackageFiles::Call::Call(PackageFiles&s):s(s),ok(false){
 if(s.retained()||!s.configured_)return;
 // enter attests the current task even when it already holds handle custody.
 if(!s.hooks_.enter(s.hooks_.context))return;
 s.custody_=true;++s.depth_;errno=0;ok=true;
}
PackageFiles::Call::~Call(){if(ok){if(errno==EIO||s.hooks_.retained(s.hooks_.context))s.retained_=true;if(--s.depth_==0&&!s.file_&&!s.directory_&&!s.retained_){s.hooks_.leave(s.hooks_.context);s.custody_=false;}}}
bool PackageFiles::fault(bool success){if(errno==EIO)retained_=true;return success&&!retained();}
uint32_t PackageFiles::next(){if(serial_==UINT32_MAX)return 0;return ++serial_;}
bool PackageFiles::path(const char*p,char*out,size_t capacity){
 if(!p||*p!='/'||strlen(p)>204)return false;
 for(const char*s=p+1;*s;){const char*e=strchr(s,'/');size_t n=e?size_t(e-s):strlen(s);
  if(!n||(n==1&&*s=='.')||(n==2&&!memcmp(s,"..",2)))return false;
  for(size_t i=0;i<n;++i)if(static_cast<unsigned char>(s[i])<32||s[i]=='\\'||s[i]==127)return false;
  if(!e)break;s=e+1;if(!*s)return false;
 }
 if(strcmp(p,"/")&&strncmp(p,"/Apps/",6)&&strcmp(p,"/Apps")&&strncmp(p,"/Drivers/",9)&&strcmp(p,"/Drivers")&&strncmp(p,"/AppData/",9)&&strcmp(p,"/AppData"))return false;
 int n=snprintf(out,capacity,"%s%s",root_,!strcmp(p,"/")?"":p);if(n<0||size_t(n)>=capacity)return false;
 // LittleFS has no symlinks. Host verification rejects symlink components too.
#ifndef ESP_PLATFORM
 char check[512];strcpy(check,out);for(char*c=check+1;;++c){if(*c&&*c!='/')continue;char saved=*c;*c=0;struct stat st{};if(!lstat(check,&st)&&S_ISLNK(st.st_mode))return false;*c=saved;if(!saved)break;}
#endif
 return true;
}
bool PackageFiles::mutation(const char*p){return p&&strcmp(p,"/")&&strcmp(p,"/Apps")&&strcmp(p,"/Drivers")&&strcmp(p,"/AppData");}
bool PackageFiles::roots(){
 const char*names[]={"","/Apps","/Drivers","/AppData"};
 for(const char*n:names){char p[512];snprintf(p,sizeof(p),"%s%s",root_,n);struct stat st{};
  errno=0;if(stat(p,&st)){if(errno!=ENOENT||mkdir(p,0700))return fault(false);errno=0;if(stat(p,&st))return fault(false);}
  if(!S_ISDIR(st.st_mode))return false;
 }return true;
}
int32_t PackageFiles::metadata(const char*p,risc_storage_metadata_v1*out){
 Call call(*this);char full[512];if(!call.ok||!out||out->struct_size<sizeof(*out)||!path(p,full,sizeof(full)))return RISC_STORAGE_FS_INVALID;
 struct stat st{};errno=0;if(stat(full,&st)){int e=errno;fault(false);return e==ENOENT?RISC_STORAGE_FS_NOT_FOUND:RISC_STORAGE_FS_IO;}
 if(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode))return RISC_STORAGE_FS_INVALID;
 *out={sizeof(*out),S_ISDIR(st.st_mode)?uint32_t(RISC_STORAGE_DIRECTORY):0u,uint64_t(st.st_size),0};return RISC_STORAGE_FS_OK;
}
uint32_t PackageFiles::open(const char*p,bool write,uint64_t*size){
 if(size)*size=0;Call call(*this);char full[512];if(!call.ok||file_||!path(p,full,sizeof(full))||(write&&!mutation(p))||serial_==UINT32_MAX)return 0;
 int fd=::open(full,write?(O_WRONLY|O_CREAT|O_EXCL):O_RDONLY,0600);if(fd<0){fault(false);return 0;}
 // Publish custody before any further fallible operation.
 descriptor_=fd;file_=next();writing_=write;error_=0;if(write)strcpy(writePath_,full);
 struct stat st{};if(fstat(fd,&st)||!S_ISREG(st.st_mode)){error_=1;close(file_,false);return 0;}
 if(size)*size=uint64_t(st.st_size);return file_;
}
bool PackageFiles::close(uint32_t token,bool commit){
 Call call(*this);if(!call.ok||!token||token!=file_)return false;
 if(writing_&&commit&&fsync(descriptor_)){fault(false);return false;}
 if(::close(descriptor_)){retained_=true;return false;}
 descriptor_=-1;file_=0;
 if(writing_&&!commit&&unlink(writePath_)){if(errno!=ENOENT){retained_=true;return false;}}
 writing_=false;writePath_[0]=0;return true;
}
bool PackageFiles::freeBytes(uint64_t*out){
 if(out)*out=0;Call call(*this);if(!call.ok||!out||file_||directory_)return false;
 uint64_t total=0,used=0;if(!hooks_.usage(hooks_.context,&total,&used)||used>total)return fault(false);
 // Leave two maximum catalog copies plus metadata for protected state.
 *out=total-used>139264?total-used-139264:0;return true;
}
bool PackageFiles::configure(const char*root){
 if(configured_||!root||root[0]!='/'||strlen(root)>=sizeof(root_)||strstr(root,"..")||root[strlen(root)-1]=='/'||!hooks_.enter||!hooks_.leave||!hooks_.retained||!hooks_.usage)return false;
 strcpy(root_,root);configured_=true;auto&v=*const_cast<risc_storage_volume_api_v1*>(api());auto&e=api_.state.prepared.base.sleep.terminal.power.volume;
 v={1,sizeof(api_),this,
 [](void*c){auto&s=*static_cast<PackageFiles*>(c);Call call(s);return call.ok&&s.roots();},
 [](void*c){auto&s=*static_cast<PackageFiles*>(c);Call call(s);struct stat st{};return call.ok&&!stat(s.root_,&st)&&S_ISDIR(st.st_mode);},
 [](void*,char*out,size_t n){if(!out||n<18)return false;strcpy(out,"Internal packages");return true;},
 [](void*c,const char*p,uint64_t*n,bool*d){risc_storage_metadata_v1 m{sizeof(m),0,0,0};auto&s=*static_cast<PackageFiles*>(c);if(!n||!d||s.metadata(p,&m)!=0)return false;*n=m.size;*d=(m.flags&RISC_STORAGE_DIRECTORY)!=0;return true;},
 [](void*c,const char*p)->uint32_t{auto&s=*static_cast<PackageFiles*>(c);Call call(s);char full[512];if(!call.ok||s.directory_||s.serial_==UINT32_MAX||!s.path(p,full,sizeof(full)))return 0;DIR*d=opendir(full);if(!d){s.fault(false);return 0;}s.directory_=d;s.dirToken_=s.next();s.dirError_=0;strcpy(s.directoryPath_,full);return s.dirToken_;},
 [](void*c,uint32_t token,risc_storage_dirent_v1*out){auto&s=*static_cast<PackageFiles*>(c);Call call(s);if(!call.ok||!out||!token||token!=s.dirToken_||!s.directory_)return false;
  for(;;){errno=0;auto*entry=readdir(static_cast<DIR*>(s.directory_));if(!entry){if(errno)s.dirError_=1;s.fault(!errno);return false;}if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
   if(strlen(entry->d_name)>=sizeof(out->name)){s.dirError_=1;return false;}char full[768];snprintf(full,sizeof(full),"%s/%s",s.directoryPath_,entry->d_name);struct stat st{};if(stat(full,&st)){s.dirError_=1;s.fault(false);return false;}*out={};strcpy(out->name,entry->d_name);out->size=uint64_t(st.st_size);out->is_directory=S_ISDIR(st.st_mode);return true;}},
 [](void*c,uint32_t token){auto&s=*static_cast<PackageFiles*>(c);(void)s.api_.state.prepared.base.sleep.terminal.power.volume.dir_close_checked(c,token);},
 [](void*c,const char*p,uint64_t*n){return static_cast<PackageFiles*>(c)->open(p,false,n);},
 [](void*c,uint32_t token,void*b,size_t n)->size_t{auto&s=*static_cast<PackageFiles*>(c);Call call(s);if(!call.ok||!b||!token||token!=s.file_||s.writing_||n>4096)return 0;ssize_t got=::read(s.descriptor_,b,n);if(got<0){s.error_=1;s.fault(false);return 0;}return size_t(got);},
 [](void*c,const char*p){return static_cast<PackageFiles*>(c)->open(p,true,nullptr);},
 [](void*c,uint32_t token,const void*b,size_t n)->size_t{auto&s=*static_cast<PackageFiles*>(c);Call call(s);if(!call.ok||!b||!token||token!=s.file_||!s.writing_||n>4096)return 0;ssize_t got=::write(s.descriptor_,b,n);if(got<0||size_t(got)!=n){s.error_=1;s.fault(false);}return got<0?0:size_t(got);},
 [](void*c,uint32_t t,bool commit){return static_cast<PackageFiles*>(c)->close(t,commit);},
 [](void*c,const char*p){auto&s=*static_cast<PackageFiles*>(c);Call call(s);char full[512];return call.ok&&!s.file_&&!s.directory_&&s.mutation(p)&&s.path(p,full,sizeof(full))&&s.fault(!unlink(full));},
 [](void*,char*out,size_t n){if(!out||n<15)return false;strcpy(out,"package volume");return true;}};
 e.file_sync=[](void*c,uint32_t t){auto&s=*static_cast<PackageFiles*>(c);Call call(s);return call.ok&&t&&t==s.file_&&s.fault(!fsync(s.descriptor_));};
 e.file_seek=[](void*c,uint32_t t,uint64_t offset){auto&s=*static_cast<PackageFiles*>(c);Call call(s);struct stat st{};return call.ok&&t&&t==s.file_&&offset<=INT32_MAX&&!fstat(s.descriptor_,&st)&&offset<=uint64_t(st.st_size)&&s.fault(lseek(s.descriptor_,off_t(offset),SEEK_SET)>=0);};
 e.file_info=[](void*c,uint32_t t,uint64_t*n,uint64_t*o){auto&s=*static_cast<PackageFiles*>(c);Call call(s);struct stat st{};if(!call.ok||!n||!o||!t||t!=s.file_||fstat(s.descriptor_,&st))return false;off_t at=lseek(s.descriptor_,0,SEEK_CUR);if(at<0)return s.fault(false);*n=uint64_t(st.st_size);*o=uint64_t(at);return true;};
 e.dir_close_checked=[](void*c,uint32_t t){auto&s=*static_cast<PackageFiles*>(c);Call call(s);if(!call.ok||!t||t!=s.dirToken_||!s.directory_)return false;if(closedir(static_cast<DIR*>(s.directory_))){s.retained_=true;return false;}s.directory_=nullptr;s.dirToken_=0;return true;};
 e.handle_error=[](void*c,uint32_t t,bool dir)->uint32_t{auto&s=*static_cast<PackageFiles*>(c);return s.retained()?1:dir?(t&&t==s.dirToken_?s.dirError_:1):(t&&t==s.file_?s.error_:1);};
 e.mkdir=[](void*c,const char*p){auto&s=*static_cast<PackageFiles*>(c);Call call(s);char full[512];return call.ok&&s.path(p,full,sizeof(full))&&s.fault(!mkdir(full,0700));};
 e.rename=[](void*c,const char*a,const char*b){auto&s=*static_cast<PackageFiles*>(c);Call call(s);char from[512],to[512];struct stat st{};if(!call.ok||s.file_||s.directory_||!s.mutation(a)||!s.mutation(b)||!s.path(a,from,sizeof(from))||!s.path(b,to,sizeof(to)))return false;errno=0;if(!stat(to,&st)||errno!=ENOENT)return false;errno=0;return s.fault(!rename(from,to));};
 api_.state.state_tag=RISC_STORAGE_STATE_TAG;api_.state.state_version=1;api_.state.observe=[](void*c)->int32_t{auto&s=*static_cast<PackageFiles*>(c);return s.retained()?RISC_STORAGE_STATE_RETAINED:s.configured_?RISC_STORAGE_STATE_READY:RISC_STORAGE_STATE_UNAVAILABLE;};
 api_.fs_tag=RISC_STORAGE_FS_TAG;api_.fs_version=1;
 api_.dir_tell=[](void*c,uint32_t t,uint64_t*out){auto&s=*static_cast<PackageFiles*>(c);Call call(s);if(!call.ok||!out||!t||t!=s.dirToken_||!s.directory_)return false;long pos=telldir(static_cast<DIR*>(s.directory_));if(pos<0)return s.fault(false);*out=uint64_t(pos);return true;};
 api_.dir_seek=[](void*c,uint32_t t,uint64_t pos){auto&s=*static_cast<PackageFiles*>(c);Call call(s);if(!call.ok||!t||t!=s.dirToken_||!s.directory_||pos>LONG_MAX)return false;seekdir(static_cast<DIR*>(s.directory_),long(pos));return s.fault(!errno);};
 e.dir_rewind=[](void*c,uint32_t t){auto&s=*static_cast<PackageFiles*>(c);return s.api_.dir_seek(c,t,0);};
 api_.metadata=[](void*c,const char*p,risc_storage_metadata_v1*m){return static_cast<PackageFiles*>(c)->metadata(p,m);};
 api_.file_truncate=[](void*c,uint32_t t,uint64_t n){auto&s=*static_cast<PackageFiles*>(c);Call call(s);struct stat st{};return call.ok&&t&&t==s.file_&&s.writing_&&n<=INT32_MAX&&!fstat(s.descriptor_,&st)&&n<=uint64_t(st.st_size)&&s.fault(!ftruncate(s.descriptor_,off_t(n)));};
 // LittleFS rename replaces atomically. No rename-away gap or rollback filename.
 api_.replace_file=[](void*c,const char*a,const char*b){auto&s=*static_cast<PackageFiles*>(c);Call call(s);char from[512],to[512];if(!call.ok||s.file_||s.directory_||!s.mutation(a)||!s.mutation(b)||!s.path(a,from,sizeof(from))||!s.path(b,to,sizeof(to)))return false;const char*x=strrchr(a,'/'),*y=strrchr(b,'/');if(x-a!=y-b||strncmp(a,b,size_t(x-a)))return false;return s.fault(!rename(from,to));};
 api_.recover_replace=[](void*c,const char*p){auto&s=*static_cast<PackageFiles*>(c);Call call(s);char full[512];return call.ok&&!s.file_&&!s.directory_&&s.mutation(p)&&s.path(p,full,sizeof(full));};
 return true;
}
}
