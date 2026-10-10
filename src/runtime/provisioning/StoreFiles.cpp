#include "StoreFiles.h"
#include "bootstrap/Json.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <algorithm>
#include <cstring>
namespace RiscProvision {
bool StoreFiles::checkpoint(){return !retained_ && io_.checkpoint(io_.context) && uint32_t(io_.now(io_.context)-started_)<300000u;}
bool StoreFiles::filename(const char* name,char (&out)[256]) const{return RiscBoot::path(root_,name,out,sizeof(out));}
bool StoreFiles::close(){
 // Abort never flushes newly buffered payload into the inactive store. A closed
 // incomplete write cannot be resumed with fopen("wb") and a stale hash/offset.
 if(stream_ && begun_ && profile_ && index_<profile_->count && received_!=profile_->files[index_].bytes)failed_=true;
 if(buffered_){buffered_=0;failed_=true;}
 if(stream_){FILE* file=stream_;stream_=nullptr;if(fclose(file))retained_=true;}
 return !retained_;
}
bool StoreFiles::inventory(const char* relative,unsigned depth,bool erase,size_t& count){
 if(depth>10 || !checkpoint())return false;
 char folder[256];if(*relative){if(!filename(relative,folder))return false;}else strcpy(folder,root_);
 DIR* dir=opendir(folder);if(!dir)return false;
 bool ok=true;
 for(;;){
  errno=0;auto* item=readdir(dir);if(!item){if(errno)ok=false;break;}
  if(!strcmp(item->d_name,".")||!strcmp(item->d_name,".."))continue;
  char name[193],full[256];int n=snprintf(name,sizeof(name),"%s%s%s",relative,*relative?"/":"",item->d_name);
  if(n<=0 || size_t(n)>=sizeof(name) || !filename(name,full) || !checkpoint()){ok=false;break;}
  struct stat info{};
#ifdef ESP_PLATFORM
  // SPIFFS has flat slash-containing names, no directories or symlinks.
  if(stat(full,&info)){ok=false;break;}
#else
  // Host fixtures must not follow a symlink outside their private staging root.
  if(lstat(full,&info)){ok=false;break;}
#endif
  if(S_ISDIR(info.st_mode)){
#ifdef ESP_PLATFORM
   ok=false;break;
#else
   if(!inventory(name,depth+1,erase,count)){ok=false;break;}
   // Empty host directories are scaffolding, not SPIFFS store objects.
   continue;
#endif
  }
  if(!S_ISREG(info.st_mode)||++count>MaxFiles+1){ok=false;break;}
  if(erase){if(remove(full)){ok=false;break;}continue;}
  bool known=false;
  for(size_t i=0;i<profile_->count;++i)if(!strcmp(name,profile_->files[i].path)){known=true;break;}
  if(!known && !(finished_ && !strcmp(name,DigestFile))){ok=false;break;}
 }
 if(closedir(dir)){retained_=true;ok=false;}
 return ok;
}
bool StoreFiles::begin(const char* root,const Profile& p,const uint8_t (&digest)[32],uint32_t capacity){
 if(begun_||failed_||!root||root[0]!='/'||strlen(root)>=sizeof(root_)||p.count<3||p.count>MaxFiles ||
    !io_.now||!io_.checkpoint||!io_.hashBegin||!io_.hashAdd||!io_.hashEnd||!io_.admit)return false;
 started_=io_.now(io_.context);strcpy(root_,root);profile_=&p;memcpy(digest_,digest,32);
 uint32_t total=32; // profile identity is committed inside the selected store
 for(size_t i=0;i<p.count;++i){char full[256];const auto& f=p.files[i];
  if(!filename(f.path,full)||!strcmp(f.path,DigestFile)||!f.bytes||f.bytes>MaxFileBytes||total>capacity||f.bytes>capacity-total){failed_=true;return false;}
  total+=f.bytes;
 }
 size_t count=0;if(!checkpoint()||!inventory("",0,true,count)){failed_=true;return false;}
 count=0;if(!inventory("",0,false,count)||count){failed_=true;return false;}
 begun_=true;return true;
}
bool StoreFiles::write(size_t file,const void* bytes,uint32_t size){
 if(!begun_||failed_||finished_||file!=index_||index_>=profile_->count||!bytes||!size||size>ChunkBytes||!checkpoint())return false;
 const auto& expected=profile_->files[index_];
 if(size>expected.bytes-received_){failed_=true;return false;}
 if(!stream_){char path[256];if(!filename(expected.path,path)||!io_.hashBegin(io_.context)){failed_=true;return false;}
  stream_=fopen(path,"wb");if(!stream_){failed_=true;return false;}
  if(setvbuf(stream_,nullptr,_IONBF,0)){failed_=true;return false;}}
 // HTTPS can return small/irregular chunks. Coalescing into one existing
 // two-sector buffer bounds SPIFFS header/index churn and GC at high fill.
 if(!io_.hashAdd(io_.context,bytes,size)){failed_=true;return false;}
 const auto* input=static_cast<const uint8_t*>(bytes);uint32_t consumed=0;
 while(consumed<size){
  const uint32_t amount=std::min<uint32_t>(size-consumed,sizeof(buffer_)-buffered_);
  memcpy(buffer_+buffered_,input+consumed,amount);buffered_+=amount;consumed+=amount;
  if(buffered_==sizeof(buffer_)){
   if(fwrite(buffer_,1,buffered_,stream_)!=buffered_ || !checkpoint()){failed_=true;return false;}
   buffered_=0;
  }
 }
 received_+=size;
 if(received_==expected.bytes){
  if(buffered_){
   if(fwrite(buffer_,1,buffered_,stream_)!=buffered_ || !checkpoint()){failed_=true;return false;}
   buffered_=0;
  }
  uint8_t hash[32];bool ok=io_.hashEnd(io_.context,hash)&&!memcmp(hash,expected.sha256,32);
  if(!close()||!ok){failed_=true;return false;}++index_;received_=0;}
 return true;
}
bool StoreFiles::verify(const File& f){
 char path[256];if(!filename(f.path,path)||!checkpoint()||!io_.hashBegin(io_.context))return false;
 stream_=fopen(path,"rb");if(!stream_)return false;
 bool ok=true;uint32_t read=0;
 while(read<f.bytes){uint32_t n=std::min<uint32_t>(ChunkBytes,f.bytes-read);
  if(fread(buffer_,1,n,stream_)!=n||!io_.hashAdd(io_.context,buffer_,n)||!checkpoint()){ok=false;break;}read+=n;}
 uint8_t hash[32];ok=ok&&fgetc(stream_)==EOF&&!ferror(stream_)&&io_.hashEnd(io_.context,hash)&&!memcmp(hash,f.sha256,32);
 return close()&&ok;
}
bool StoreFiles::verifyImage(const char* root,const Profile& p,uint32_t capacity){
 if(begun_||failed_||!p.imageMode()||!root||root[0]!='/'||strlen(root)>=sizeof(root_)||p.count<3||p.count>MaxFiles ||
    !io_.now||!io_.checkpoint||!io_.hashBegin||!io_.hashAdd||!io_.hashEnd||!io_.admit)return false;
 started_=io_.now(io_.context);strcpy(root_,root);profile_=&p;
 uint32_t total=0;
 for(size_t i=0;i<p.count;++i){char full[256];const auto& f=p.files[i];
  if(!filename(f.path,full)||!strcmp(f.path,DigestFile)||!f.bytes||f.bytes>MaxFileBytes||total>capacity||f.bytes>capacity-total){failed_=true;return false;}
  total+=f.bytes;
 }
 // Leave begun_ false while reading so close() cannot confuse a readback with
 // an incomplete download. This path never opens a file for writing/removal.
 size_t count=0;bool ok=checkpoint()&&inventory("",0,false,count)&&count==p.count;
 for(size_t i=0;ok&&i<p.count;++i)ok=verify(p.files[i]);
 if(ok)ok=io_.admit(io_.context,root_,p)&&checkpoint();
 count=0;ok=ok&&inventory("",0,false,count)&&count==p.count&&checkpoint();
 begun_=true;finished_=ok;failed_=!ok;index_=p.count;return ok;
}
bool StoreFiles::finish(){
 if(!begun_||failed_||finished_||stream_||index_!=profile_->count||!checkpoint())return false;
 size_t count=0;bool ok=inventory("",0,false,count)&&count==profile_->count;
 for(size_t i=0;ok&&i<profile_->count;++i)ok=verify(profile_->files[i]);
 if(ok)ok=io_.admit(io_.context,root_,*profile_)&&checkpoint();
 if(!ok){failed_=true;return false;}
 char path[256];if(!filename(DigestFile,path)){failed_=true;return false;}
 stream_=fopen(path,"wb");if(!stream_){failed_=true;return false;}
 ok=fwrite(digest_,1,32,stream_)==32;if(!close())ok=false;
 if(ok){stream_=fopen(path,"rb");ok=stream_&&fread(buffer_,1,33,stream_)==32&&!ferror(stream_)&&!memcmp(buffer_,digest_,32);if(!close())ok=false;}
 // Metadata writes can invoke filesystem GC. Recheck payloads after it too.
 for(size_t i=0;ok&&i<profile_->count;++i)ok=verify(profile_->files[i]);
 finished_=ok;count=0;ok=ok&&inventory("",0,false,count)&&count==profile_->count+1&&checkpoint();
 if(!ok){failed_=true;finished_=false;}return ok;
}
}
