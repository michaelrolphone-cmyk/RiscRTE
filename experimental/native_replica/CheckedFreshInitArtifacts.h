#pragma once
#include "NativeReplicaCore.h"
#include <cerrno>
#include <climits>
#include <cstdio>
#if defined(__linux__) && !defined(ESP_PLATFORM)
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace RiscExperimental {
// Read-only, worker/lease-only inspection of one firmware-private namespace.
// The lease excludes every writer, renamer and cleanup owner, including changes
// to the provisioned root/ancestors. No app-supplied filename or cleanup API.
// Linux has qualified no-follow/identity/link metadata in the focused host tests.
// ESP (and other unqualified VFSes) fails closed: stat does not prove nonaliasing,
// and host checks/target compilation are NOT target filesystem qualification.
class CheckedFreshInitArtifacts {
 public:
 using C=NativeReplicaCore;
 struct Hooks {
  void*context;
  bool(*workerWithLease)(void*);
  bool(*cooperate)(void*);
  int(*observe)(void*); // Core status; Retained has lifetime precedence.
 };
 CheckedFreshInitArtifacts(uint32_t ns,const char*root,Hooks hooks):ns_(ns),hooks_(hooks){
  if(root&&root[0]=='/'&&std::strlen(root)<sizeof root_)std::strcpy(root_,root);
 }
 CheckedFreshInitArtifacts(const CheckedFreshInitArtifacts&)=delete;
 CheckedFreshInitArtifacts&operator=(const CheckedFreshInitArtifacts&)=delete;
 // Intentionally no destructor cleanup: failed close/lost lease keeps custody.
 uint32_t namespaceId()const{return ns_;}
 bool retained()const{return retained_;}
 bool exitSafe()const{return !entered_&&!retained_&&!handles();}
 int inspectAbsent(){return inspect(nullptr,0,0,Attempt::Absent);}
 // persistedNext==0 denotes an absent sequence. Caller separately validates the
 // exact RTENSEQ record and receipt/source inventory. No artifact may then exist.
 int inspectAttempt(const uint8_t*canonicalImage,uint32_t imageBytes,uint64_t persistedNext){return inspect(canonicalImage,imageBytes,persistedNext,Attempt::Fresh);}
 // Imported recovery is tied to the caller's complete, checked initial image.
 // Source selection, digest derivation and sequence validation remain the bound
 // admission/store's responsibility; this method grants no recovery authority.
 int inspectImportedAttempt(const uint8_t*canonicalImage,uint32_t imageBytes,uint64_t persistedNext){return inspect(canonicalImage,imageBytes,persistedNext,Attempt::Imported);}
 // Separate known-SD grammar; no legacy archive or map sorting is inferred.
 int inspectSdAttempt(const uint8_t*image,uint32_t bytes,uint64_t next){return inspect(image,bytes,next,Attempt::Sd);}
 private:
 enum class Attempt {Absent,Fresh,Imported,Sd};
 static constexpr uint32_t ImageBytes=484,MaxImageBytes=65512,ChunkBytes=4096;
 uint32_t ns_;Hooks hooks_;char root_[160]{};bool entered_=false,retained_=false;
#if defined(__linux__) && !defined(ESP_PLATFORM)
 int rootFd_=-1,fileFd_=-1,openingDir_=-1;DIR*dir_=nullptr;
#endif
 struct Entry {bool&v;explicit Entry(bool&b):v(b){v=true;}~Entry(){v=false;}};
 struct Wipe {uint8_t*p;size_t n;~Wipe(){C::wipe(p,n);}};
 bool handles()const{
#if defined(__linux__) && !defined(ESP_PLATFORM)
  return rootFd_>=0||fileFd_>=0||openingDir_>=0||dir_;
#else
  return false;
#endif
 }
 int retain(){retained_=true;return C::Retained;}
 int check(bool yield=false){
  if(retained_)return C::Retained;
  const int observed=hooks_.observe(hooks_.context);if(observed==C::Retained)return retain();
  const bool lease=hooks_.workerWithLease(hooks_.context);
  const int after=hooks_.observe(hooks_.context);if(after==C::Retained)return retain();
  const bool stillHeld=hooks_.workerWithLease(hooks_.context);
  if(!lease||!stillHeld){if(hooks_.observe(hooks_.context)==C::Retained)return retain();return handles()?retain():C::Busy;}
  if(observed)return observed;
  if(after)return after;
  if(!yield)return C::Ok;
  const bool proceed=hooks_.cooperate(hooks_.context);
  const int rc=check();return rc?rc:proceed?C::Ok:C::Busy;
 }
 // Ordinary errors/cancellation still close under the lease. Retained or lost
 // lease with any handle suppresses ALL subsequent I/O, including other closes.
#if defined(__linux__) && !defined(ESP_PLATFORM)
 int cleanup(int rc){
  if(handles()){
   int state=check();if(state==C::Retained)return state;if(!rc)rc=state;
   if(fileFd_>=0){if(::close(fileFd_))return retain();fileFd_=-1;state=check();if(state==C::Retained)return state;if(!rc)rc=state;}
   if(dir_){if(::closedir(dir_))return retain();dir_=nullptr;state=check();if(state==C::Retained)return state;if(!rc)rc=state;}
   if(openingDir_>=0){if(::close(openingDir_))return retain();openingDir_=-1;state=check();if(state==C::Retained)return state;if(!rc)rc=state;}
   if(rootFd_>=0){if(::close(rootFd_))return retain();rootFd_=-1;state=check();if(state==C::Retained)return state;if(!rc)rc=state;}
  }
  const int state=check();return state==C::Retained?state:rc?rc:state;
 }
#endif
 struct Canonical {uint32_t baseline=0,image=0,object=0,bytes=ImageBytes;const uint8_t*imported=nullptr;};
 static uint8_t byte(uint32_t word,unsigned at){return uint8_t(word>>(8*at));}
 uint8_t expectedAt(uint32_t at,const Canonical&c)const{
  if(at<8)return uint8_t("RTENOBJ"[at]);
  if(at>=16&&at<20)return byte(c.bytes,at-16);
  if(at>=20&&at<24)return byte(c.object,at-20);
  if(at>=24&&c.imported)return c.imported[at-24];
  if(at>=24&&at<32)return uint8_t("RTENR03"[at-24]);
  if(at>=32&&at<36)return byte(ns_,at-32);
  if(at==40)return 1;
  if(at>=132&&at<136)return byte(ImageBytes,at-132);
  if(at>=408&&at<416)return uint8_t("RTENBP1"[at-408]);
  if(at>=416&&at<420)return byte(ns_,at-416);
  if(at==420)return 1;
  if(at>=500&&at<504)return byte(c.baseline,at-500);
  if(at>=504&&at<508)return byte(c.image,at-504);
  return 0;
 }
 uint32_t expectedCrc(uint32_t start,uint32_t n,const Canonical&expected)const{
  uint32_t c=~0u;for(uint32_t i=0;i<n;++i){c^=expectedAt(start+i,expected);for(unsigned bit=0;bit<8;++bit)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;
 }
 Canonical canonical()const{
  static_assert(C::HeaderV3==480&&C::HeaderV2==384&&C::ProvenanceBytes==96,"Initial v3 schema changed");
  Canonical c{};c.baseline=expectedCrc(408,92,c);c.image=expectedCrc(24,480,c);c.object=expectedCrc(24,ImageBytes,c);return c;
 }
 bool prefix(const uint8_t*p,uint32_t at,uint32_t n,const Canonical&expected,uint64_t next)const{
  if(at>expected.bytes+24||n>expected.bytes+24-at||!next||next>UINT64_MAX/2)return false;
  for(uint32_t i=0;i<n;++i)if((at+i<8||at+i>=16)&&p[i]!=expectedAt(at+i,expected))return false;
  if(at)return true; // Only the first chunk contains the object ID.
  const unsigned bytes=n<=8?0:n>=16?8:unsigned(n-8);if(!bytes)return true;
  uint64_t low=0;for(unsigned i=0;i<bytes;++i)low|=uint64_t(p[8+i])<<(8*i);
  if(low&1)return false;
  // Lowest nonzero even ID with these little-endian low bytes. A partial zero
  // ID can be completed in a higher byte; a complete zero ID cannot. No range
  // enumeration, overflow, or shift by 64 even at the last counter value.
  if(low<2){if(bytes==8)return false;low=uint64_t(1)<<(8*bytes);}
  return low<=(next<<1);
 }
 bool validRootPath()const{
  const size_t n=std::strlen(root_);if(n<2||root_[n-1]=='/')return false;
  for(size_t at=1;at<n;){size_t end=at;while(end<n&&root_[end]!='/')++end;const size_t count=end-at;
   if(!count||(count==1&&root_[at]=='.')||(count==2&&root_[at]=='.'&&root_[at+1]=='.'))return false;
   at=end+1;
  }
  return true;
 }
 static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 static bool zero(const uint8_t*p,uint32_t n){while(n--)if(*p++)return false;return true;}
 static uint32_t crc(const uint8_t*p,uint32_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned bit=0;bit<8;++bit)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
 bool validImported(const uint8_t*p,uint32_t n)const{
  if(!p||n<ImageBytes||n>MaxImageBytes)return false;
  const uint32_t bytes=n-ImageBytes,count=word(p+96);const auto*b=p+C::HeaderV2;
  if(std::memcmp(p,"RTENR03",8)||word(p+8)!=ns_||!zero(p+12,4)||word(p+16)!=1||!zero(p+20,76)||word(p+100)||
     word(p+104)!=bytes||word(p+108)!=n||!zero(p+112,C::HeaderV2-112)||
     std::memcmp(b,"RTENBP1",8)||word(b+8)!=ns_||word(b+12)!=uint32_t(C::Origin::ImportedLegacy)||
     word(b+16)!=bytes||word(b+20)!=count||zero(b+24,32)||zero(b+56,32)||word(b+88)!=1||
     word(b+92)!=crc(b,92)||word(p+n-4)!=crc(p,n-4)||(!bytes)!=(!count)||uint64_t(count)*21>bytes)return false;
  uint32_t at=C::HeaderV3;const uint32_t end=n-4;const uint8_t*previous=nullptr;
  for(uint32_t i=0;i<count;++i){
   if(at>end||end-at<21)return false;
   const uint8_t*key=p+at;unsigned length=0;
   while(length<16&&key[length]){const uint8_t c=key[length];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;++length;}
   if(!length||length==16||!zero(key+length,16-length)||(previous&&std::memcmp(previous,key,16)>=0))return false;
   const uint32_t size=word(key+16);if(!size||size>C::ValueMax||size>end-at-20)return false;
   previous=key;at+=20+size;
  }
  return at==end;
 }
 bool validSd(const uint8_t*p,uint32_t n)const{
  if(!p||n<C::Header+4||n>MaxImageBytes)return false;
  const uint32_t bytes=n-C::Header-4,count=word(p+96);
  if(std::memcmp(p,"RTENR01",8)||word(p+8)!=ns_||word(p+12)!=1||word(p+16)!=1||!zero(p+20,20)||zero(p+48,16)||word(p+100)||word(p+104)!=bytes||word(p+108)!=n||!zero(p+112,16)||word(p+n-4)!=crc(p,n-4)||(!bytes)!=(!count)||uint64_t(count)*21>bytes)return false;
  uint32_t at=C::Header;
  for(uint32_t i=0;i<count;++i){
   if(at>n-4||n-4-at<21)return false;
   const uint8_t*key=p+at;unsigned length=0;
   while(length<16&&key[length]){const uint8_t c=key[length];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;++length;}
   if(!length||length==16||!zero(key+length,16-length))return false;
   const uint32_t size=word(key+16);if(!size||size>C::ValueMax||size>n-4-at-20)return false;
   for(uint32_t before=C::Header;before<at;before+=20+word(p+before+16))if(!std::memcmp(p+before,key,16))return false;
   at+=20+size;
  }
  return at==n-4;
 }
 int inspect(const uint8_t*image,uint32_t bytes,uint64_t next,Attempt mode){
  if(retained_)return C::Retained;
  if(entered_)return C::Busy;
  Entry entry(entered_);
  if(!hooks_.observe||!hooks_.workerWithLease||!hooks_.cooperate)return C::Invalid;
  int rc=check();if(rc)return rc;
  const bool attempt=mode!=Attempt::Absent;
  if(!ns_||ns_>INT32_MAX||!validRootPath()||(attempt&&(!image||next>UINT64_MAX/2)))return C::Invalid;
  Canonical expected{};
  if(mode==Attempt::Imported||mode==Attempt::Sd){if(mode==Attempt::Imported?!validImported(image,bytes):!validSd(image,bytes))return C::Invalid;expected.imported=image;expected.bytes=bytes;expected.object=crc(image,bytes);}
  else{expected=canonical();if(mode==Attempt::Fresh){if(bytes!=ImageBytes)return C::Invalid;for(uint32_t i=0;i<ImageBytes;++i)if(image[i]!=expectedAt(i+24,expected))return C::Invalid;}}
#if defined(__linux__) && !defined(ESP_PLATFORM)
  rc=inventory(expected,next,attempt);return cleanup(rc);
#else
  // No fallback to stat/O_NOFOLLOW=0 or assumed st_ino/st_nlink support.
  return C::Io;
#endif
 }
#if defined(__linux__) && !defined(ESP_PLATFORM)
 static bool same(const struct stat&a,const struct stat&b){
  return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_nlink==b.st_nlink&&a.st_size==b.st_size&&
   a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
 }
 int lookup(int fd,const char*name,struct stat*out){
  int rc=check();if(rc)return rc;
  const int got=::fstatat(fd,name,out,AT_SYMLINK_NOFOLLOW),error=errno;
  rc=check(true);return rc?rc:got?(error==ENOENT?C::Missing:C::Io):C::Ok;
 }
 int inspectFile(const char*name,const struct stat&seen,const Canonical&expected,uint64_t next,bool full){
  if(!S_ISREG(seen.st_mode)||seen.st_nlink!=1||seen.st_size<0||uint64_t(seen.st_size)>expected.bytes+24||(full&&seen.st_size!=expected.bytes+24)||!next)return C::Io;
  int rc=check();if(rc)return rc;
  fileFd_=::openat(::dirfd(dir_),name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);const int opened=fileFd_;
  rc=check(true);if(rc)return rc;if(opened<0)return C::Io;
  struct stat before{},after{};int result=::fstat(fileFd_,&before);rc=check(true);if(rc)return rc;
  if(result||!same(before,seen))return C::Io;
  uint8_t object[ChunkBytes]{};Wipe wipe{object,sizeof object};const uint32_t n=uint32_t(seen.st_size);
  for(uint32_t at=0;at<n;){const uint32_t chunk=n-at>sizeof object?sizeof object:n-at;
   const ssize_t got=::read(fileFd_,object,chunk);rc=check(true);if(rc)return rc;
   if(got!=ssize_t(chunk)||!prefix(object,at,chunk,expected,next))return C::Io;
   at+=chunk;
  }
  uint8_t extra=0;const ssize_t end=::read(fileFd_,&extra,1);rc=check(true);if(rc)return rc;if(end)return C::Io;
  result=::fstat(fileFd_,&after);rc=check(true);if(rc)return rc;if(result||!same(before,after))return C::Io;
  if(::close(fileFd_))return retain();
  fileFd_=-1;rc=check(true);if(rc)return rc;
  struct stat current{};rc=lookup(::dirfd(dir_),name,&current);if(rc)return rc==C::Missing?C::Io:rc;
  return same(before,current)?C::Ok:C::Io;
 }
 int stableRoot(const struct stat&root){
  struct stat current{};int result=::fstat(rootFd_,&current);int rc=check();if(rc)return rc;
  if(result||!same(root,current))return C::Io;
  result=::lstat(root_,&current);rc=check();if(rc)return rc;
  return !result&&same(root,current)?C::Ok:C::Io;
 }
 int inventory(const Canonical&expected,uint64_t next,bool attempt){
  char component[sizeof root_]{};std::strcpy(component,root_);struct stat root{};
  for(size_t i=1;;++i)if(!component[i]||component[i]=='/'){
   const char saved=component[i];component[i]=0;int result=::lstat(component,&root);component[i]=saved;
   int rc=check(true);if(rc)return rc;if(result||!S_ISDIR(root.st_mode))return C::Io;if(!saved)break;
  }
  rootFd_=::open(root_,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);int rc=check(true);if(rc)return rc;if(rootFd_<0)return C::Io;
  struct stat openedRoot{};int result=::fstat(rootFd_,&openedRoot);rc=check(true);if(rc)return rc;if(result||!same(root,openedRoot))return C::Io;
  char space[16]{};std::snprintf(space,sizeof space,"n%08x",unsigned(ns_));struct stat folder{};
  rc=lookup(rootFd_,space,&folder);
  if(rc==C::Missing){
   // The first missing lookup cooperated. Recheck under the same lease, then
   // fence both the open root and its path before reporting checked absence.
   rc=lookup(rootFd_,space,&folder);if(rc!=C::Missing)return rc?rc:C::Io;
   return stableRoot(root);
  }
  if(rc)return rc;
  if(!S_ISDIR(folder.st_mode))return C::Io;
  openingDir_=::openat(rootFd_,space,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);rc=check(true);if(rc)return rc;if(openingDir_<0)return C::Io;
  struct stat opened{};result=::fstat(openingDir_,&opened);rc=check(true);if(rc)return rc;if(result||!same(folder,opened))return C::Io;
  dir_=::fdopendir(openingDir_);if(dir_)openingDir_=-1;rc=check(true);if(rc)return rc;if(!dir_)return C::Io;
  unsigned mask=0,visited=0;bool dot=false,dotdot=false;
  for(;;){
   errno=0;dirent*e=::readdir(dir_);const int error=errno;rc=check(true);if(rc)return rc;
   if(!e){if(error)return C::Io;break;}
   if(++visited>5)return C::Io;
   if(!std::strcmp(e->d_name,".")){if(dot)return C::Io;dot=true;continue;}
   if(!std::strcmp(e->d_name,"..")){if(dotdot)return C::Io;dotdot=true;continue;}
   unsigned bit=!std::strcmp(e->d_name,"replica-a.bin")?1:!std::strcmp(e->d_name,"replica-b.bin")?2:!std::strcmp(e->d_name,".pending")?4:0;
   if(!bit||(mask&bit))return C::Io;
   mask|=bit;
  }
  const char*names[]={"replica-a.bin","replica-b.bin",".pending"};
  for(unsigned i=0;i<3;++i){struct stat file{};rc=lookup(::dirfd(dir_),names[i],&file);
   if(rc==C::Missing){if(mask&(1u<<i))return C::Io;continue;}
   if(rc)return rc;
   if(!(mask&(1u<<i))||!attempt||i==1)return C::Io;
   rc=inspectFile(names[i],file,expected,next,i==0);if(rc)return rc;
  }
  struct stat current{};result=::fstat(::dirfd(dir_),&current);rc=check(true);if(rc)return rc;if(result||!same(folder,current))return C::Io;
  rc=lookup(rootFd_,space,&current);if(rc)return rc==C::Missing?C::Io:rc;if(!same(folder,current))return C::Io;
  return stableRoot(root);
 }
#endif
};
}
