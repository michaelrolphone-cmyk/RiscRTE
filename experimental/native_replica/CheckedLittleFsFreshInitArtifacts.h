#pragma once
#include "CheckedFreshInitArtifacts.h"
#include <sys/stat.h>
#include <cstddef>
namespace RiscExperimental {
// Explicit native-only profile. This is NOT selected by ESP_PLATFORM and has
// no production mount adapter. Every operation uses one captured VFS context;
// an admitted port must also fence POSIX writers, mount changes and cleanup.
class CheckedLittleFsFreshInitArtifacts final {
 public:
 using C=NativeReplicaCore;
 static constexpr uint32_t PinnedProfile=0x41873c20u;
 struct Mount {
  uint32_t profile=0,partitionAddress=0,partitionBytes=0;
  const void*vfs=nullptr;const void*partition=nullptr;const void*worker=nullptr;const void*coordinator=nullptr;
  uint64_t mountEpoch=0,rootEpoch=0,leaseEpoch=0,custodyEpoch=0,writerFenceEpoch=0,redirectEpoch=0;
  uint32_t nameSpace=0,nativeLease=0,competingWriters=0,nestedMounts=0;
  bool encrypted=false;
  char mountPath[32]{},privateRoot[96]{};
 };
 struct Entry {char name[256]{};};
 struct Port {
  void*context=nullptr;
  // A coherent native snapshot, derived from the registered VFS/partition and
  // actual worker/lease. No scalar "authorized" or caller-set success flag.
  int(*snapshot)(void*,Mount*)=nullptr;
  int(*observe)(void*)=nullptr;
  bool(*cooperate)(void*)=nullptr;
  int(*stat)(void*,const void*,const char*,struct stat*)=nullptr;
  int(*open)(void*,const void*,const char*)=nullptr;
  ptrdiff_t(*read)(void*,const void*,int,void*,size_t)=nullptr;
  int(*close)(void*,const void*,int)=nullptr;
  void*(*opendir)(void*,const void*,const char*)=nullptr;
  int(*readdir)(void*,const void*,void*,Entry*)=nullptr; // 1 row, 0 EOF, -1 error
  int(*closedir)(void*,const void*,void*)=nullptr;
 };
 // Immutable mount/partition/private-root values are construction-bound before
 // worker registration. Dynamic worker/coordinator/namespace/lease/custody
 // values in bound are deliberately NOT construction authority. Each inspection
 // obtains and latches them from two matching live port snapshots, then checks
 // them across every operation. Private-root/all-writer fencing needs port proof.
 CheckedLittleFsFreshInitArtifacts(uint32_t ns,const Mount&bound,Port port)
  :ns_(ns),bound_(bound),port_(port),encoding_(ns,nullptr,{}){}
 CheckedLittleFsFreshInitArtifacts(const CheckedLittleFsFreshInitArtifacts&)=delete;
 CheckedLittleFsFreshInitArtifacts&operator=(const CheckedLittleFsFreshInitArtifacts&)=delete;
 // No destructor cleanup: uncertain file/directory handles and enclosing native
 // mount/lease custody must remain alive. Failed close is never retried.
 uint32_t namespaceId()const{return ns_;}
 // Store paths include the registered mount; VFS callbacks receive paths
 // relative to that exact mount. This comparison is RAM-only, not admission.
 bool boundTo(uint32_t ns,const char*root)const{
  if(ns_!=ns||!root||!validBinding(bound_))return false;
  char full[sizeof bound_.mountPath+sizeof bound_.privateRoot]{};
  std::snprintf(full,sizeof full,"%s%s",bound_.mountPath,bound_.privateRoot);
  return !std::strcmp(root,full);
 }
 bool retained()const{return retained_;}
 bool exitSafe()const{return !entered_&&!retained_&&!handles();}
 int inspectAbsent(){return inspect(nullptr,0,0,Attempt::Absent);}
 int inspectAttempt(const uint8_t*p,uint32_t n,uint64_t next){return inspect(p,n,next,Attempt::Fresh);}
 int inspectImportedAttempt(const uint8_t*p,uint32_t n,uint64_t next){return inspect(p,n,next,Attempt::Imported);}
 int inspectSdAttempt(const uint8_t*p,uint32_t n,uint64_t next){return inspect(p,n,next,Attempt::Sd);}
 private:
 uint32_t ns_;Mount bound_;Port port_;CheckedFreshInitArtifacts encoding_;
 bool entered_=false,retained_=false,captured_=false;int fd_=-1;void*dir_=nullptr;
 struct Custody {const void*worker=nullptr;const void*coordinator=nullptr;uint64_t leaseEpoch=0,custodyEpoch=0;};
 Custody custody_{};
 using Canonical=CheckedFreshInitArtifacts::Canonical;
 using Attempt=CheckedFreshInitArtifacts::Attempt;
 struct Enter {bool&v;explicit Enter(bool&b):v(b){v=true;}~Enter(){v=false;}};
 struct Wipe {uint8_t*p;size_t n;~Wipe(){C::wipe(p,n);}};
 bool handles()const{return fd_>=0||dir_;}
 int retain(){retained_=true;return C::Retained;}
 static bool path(const char*p,size_t cap){
  const void*end=std::memchr(p,0,cap);if(!end||p[0]!='/'||p[1]==0)return false;
  const size_t n=static_cast<const char*>(end)-p;if(p[n-1]=='/')return false;
  for(size_t at=1;at<n;){size_t e=at;while(e<n&&p[e]!='/')++e;const size_t k=e-at;
   if(!k||(k==1&&p[at]=='.')||(k==2&&p[at]=='.'&&p[at+1]=='.'))return false;
   at=e+1;
  }return true;
 }
 static bool validBinding(const Mount&m){
  return m.profile==PinnedProfile&&m.vfs&&m.partition&&m.partitionBytes==0x80000&&
   m.partitionAddress&&m.partitionAddress%4096==0&&!m.encrypted&&m.mountEpoch&&m.rootEpoch&&
   m.writerFenceEpoch&&m.redirectEpoch&&
   !m.competingWriters&&!m.nestedMounts&&path(m.mountPath,sizeof m.mountPath)&&path(m.privateRoot,sizeof m.privateRoot);
 }
 static bool sameBinding(const Mount&a,const Mount&b){
  return validBinding(b)&&a.profile==b.profile&&a.partitionAddress==b.partitionAddress&&a.partitionBytes==b.partitionBytes&&
   a.vfs==b.vfs&&a.partition==b.partition&&a.mountEpoch==b.mountEpoch&&
   a.rootEpoch==b.rootEpoch&&a.writerFenceEpoch==b.writerFenceEpoch&&
   a.redirectEpoch==b.redirectEpoch&&!std::strcmp(a.mountPath,b.mountPath)&&!std::strcmp(a.privateRoot,b.privateRoot);
 }
 bool validCustody(const Mount&m)const{return m.nameSpace==ns_&&m.worker&&m.coordinator&&m.leaseEpoch&&m.custodyEpoch&&m.nativeLease==2;}
 static bool sameCustody(const Mount&a,const Mount&b){return a.worker==b.worker&&a.coordinator==b.coordinator&&a.leaseEpoch==b.leaseEpoch&&a.custodyEpoch==b.custodyEpoch;}
 bool accepted(const Mount&m)const{return !captured_||(custody_.worker==m.worker&&custody_.coordinator==m.coordinator&&custody_.leaseEpoch==m.leaseEpoch&&custody_.custodyEpoch==m.custodyEpoch);}
 bool configured()const{return validBinding(bound_)&&ns_&&ns_<=INT32_MAX&&port_.snapshot&&port_.observe&&port_.cooperate&&port_.stat&&port_.open&&port_.read&&port_.close&&port_.opendir&&port_.readdir&&port_.closedir;}
 int check(bool yield=false){
  if(retained_)return C::Retained;
  int rc=port_.observe(port_.context);if(rc==C::Retained)return retain();
  Mount first{},last{};const int a=port_.snapshot(port_.context,&first);
  int middle=port_.observe(port_.context);if(a==C::Retained||middle==C::Retained)return retain();
  const int b=port_.snapshot(port_.context,&last);
  if(b==C::Retained)return retain();
  if(a||b||!sameBinding(bound_,first)||!sameBinding(bound_,last)||!validCustody(first)||!validCustody(last)||!sameCustody(first,last)||!accepted(first)||!accepted(last)){
   if(port_.observe(port_.context)==C::Retained)return retain();
   return handles()?retain():int(C::Busy);
  }
  if(rc||middle)return rc?rc:middle;
  if(!captured_){custody_={first.worker,first.coordinator,first.leaseEpoch,first.custodyEpoch};captured_=true;}
  if(!yield)return C::Ok;
  const bool proceed=port_.cooperate(port_.context);rc=check();return rc?rc:proceed?int(C::Ok):int(C::Busy);
 }
 int cleanup(int rc){
  if(handles()){
   int state=check();if(state==C::Retained)return state;if(!rc)rc=state;
   if(fd_>=0){if(port_.close(port_.context,bound_.vfs,fd_))return retain();
  fd_=-1;state=check();if(state==C::Retained)return state;if(!rc)rc=state;}
   if(dir_){if(port_.closedir(port_.context,bound_.vfs,dir_))return retain();dir_=nullptr;state=check();if(state==C::Retained)return state;if(!rc)rc=state;}
  }
  const int state=check();return state==C::Retained?state:rc?rc:state;
 }
 // Inode/link/device are required to be the pinned VFS's zero values. They
 // supply NO object identity. Nonaliasing is solely the admitted no-link VFS +
 // exact mount/private-root lifetime + complete native all-writer exclusion.
 // Pinned lfs_info.size is defined only for regular files. ESP stat copies
 // that field for directories too, where it is unwritten stack data. Never
 // validate or compare directory st_size; it provides no custody evidence.
 static bool metadata(const struct stat&s){return s.st_dev==0&&s.st_ino==0&&s.st_nlink==0&&
  (S_ISDIR(s.st_mode)||(S_ISREG(s.st_mode)&&s.st_size>=0));}
 static bool sameStat(const struct stat&a,const struct stat&b){return metadata(a)&&metadata(b)&&a.st_mode==b.st_mode&&
  (!S_ISREG(a.st_mode)||a.st_size==b.st_size)&&a.st_mtime==b.st_mtime;}
 int stat(const char*p,struct stat*out){
  int rc=check();if(rc)return rc;errno=0;const int result=port_.stat(port_.context,bound_.vfs,p,out),error=errno;
  // Pinned stat reports success even if its mtime attribute read fails.
  // Missing optional mtime is ENODATA; any other freshly set errno refuses.
  rc=check(true);return rc?rc:result?(error==ENOENT?int(C::Missing):int(C::Io)):
   (error&&error!=ENODATA)||!metadata(*out)?int(C::Io):int(C::Ok);
 }
 int file(const char*p,const struct stat&seen,const Canonical&expected,uint64_t next,bool full){
  if(!S_ISREG(seen.st_mode)||uint64_t(seen.st_size)>expected.bytes+24||(full&&uint64_t(seen.st_size)!=expected.bytes+24)||!next)return C::Io;
  int rc=check();if(rc)return rc;fd_=port_.open(port_.context,bound_.vfs,p);rc=check(true);if(rc)return rc;if(fd_<0)return C::Io;
  uint8_t object[512]{};Wipe wipe{object,sizeof object};const uint32_t n=uint32_t(seen.st_size);
  for(uint32_t at=0;at<n;){const uint32_t chunk=n-at>sizeof object?sizeof object:n-at;
   const ptrdiff_t got=port_.read(port_.context,bound_.vfs,fd_,object,chunk);rc=check(true);if(rc)return rc;
   if(got!=ptrdiff_t(chunk)||!encoding_.prefix(object,at,chunk,expected,next))return C::Io;
   at+=chunk;
  }
  uint8_t extra=0;const ptrdiff_t end=port_.read(port_.context,bound_.vfs,fd_,&extra,1);rc=check(true);if(rc)return rc;if(end)return C::Io;
  if(port_.close(port_.context,bound_.vfs,fd_))return retain();
  fd_=-1;
  rc=check(true);if(rc)return rc;struct stat current{};rc=stat(p,&current);if(rc)return rc==C::Missing?int(C::Io):rc;
  return sameStat(seen,current)?int(C::Ok):int(C::Io);
 }
 int inventory(const Canonical&expected,uint64_t next,bool attempt){
  char component[sizeof bound_.privateRoot]{};std::strcpy(component,bound_.privateRoot);struct stat root{};
  for(size_t i=1;;++i)if(!component[i]||component[i]=='/'){
   const char saved=component[i];component[i]=0;const int rc=stat(component,&root);component[i]=saved;
   if(rc)return rc==C::Missing?int(C::Io):rc;
   if(!S_ISDIR(root.st_mode))return C::Io;
   if(!saved)break;
  }
  char folder[112]{};std::snprintf(folder,sizeof folder,"%s/n%08x",bound_.privateRoot,unsigned(ns_));
  struct stat directory{};int rc=stat(folder,&directory);
  if(rc==C::Missing){rc=stat(folder,&directory);if(rc!=C::Missing)return rc?rc:int(C::Io);struct stat now{};rc=stat(bound_.privateRoot,&now);return rc?rc:sameStat(root,now)?int(C::Ok):int(C::Io);}
  if(rc)return rc;
  if(!S_ISDIR(directory.st_mode))return C::Io;
  rc=check();if(rc)return rc;dir_=port_.opendir(port_.context,bound_.vfs,folder);rc=check(true);if(rc)return rc;if(!dir_)return C::Io;
  unsigned mask=0,visited=0;bool dot=false,dotdot=false;
  for(;;){Entry entry{};const int got=port_.readdir(port_.context,bound_.vfs,dir_,&entry);rc=check(true);if(rc)return rc;if(got<0)return C::Io;if(!got)break;
   if(got!=1||++visited>5||!std::memchr(entry.name,0,sizeof entry.name))return C::Io;
   if(!std::strcmp(entry.name,".")){if(dot)return C::Io;dot=true;continue;}if(!std::strcmp(entry.name,"..")){if(dotdot)return C::Io;dotdot=true;continue;}
   const unsigned bit=!std::strcmp(entry.name,"replica-a.bin")?1:!std::strcmp(entry.name,"replica-b.bin")?2:!std::strcmp(entry.name,".pending")?4:0;
   if(!bit||(mask&bit))return C::Io;
   mask|=bit;
  }
  const char*names[]={"replica-a.bin","replica-b.bin",".pending"};
  for(unsigned i=0;i<3;++i){char p[144]{};std::snprintf(p,sizeof p,"%s/%s",folder,names[i]);struct stat seen{};rc=stat(p,&seen);
   if(rc==C::Missing){if(mask&(1u<<i))return C::Io;continue;}if(rc)return rc;
   if(!(mask&(1u<<i))||!attempt||i==1)return C::Io;
   rc=file(p,seen,expected,next,i==0);if(rc)return rc;
  }
  struct stat current{};rc=stat(folder,&current);if(rc)return rc==C::Missing?int(C::Io):rc;if(!sameStat(directory,current))return C::Io;
  rc=stat(bound_.privateRoot,&current);return rc?rc:sameStat(root,current)?int(C::Ok):int(C::Io);
 }
 int inspect(const uint8_t*p,uint32_t n,uint64_t next,Attempt mode){
  if(retained_)return C::Retained;
  if(entered_)return C::Busy;
  Enter enter(entered_);captured_=false;
  if(!configured())return C::Invalid;
  int rc=check();if(rc)return rc;
  const bool attempt=mode!=Attempt::Absent;
  if(attempt&&(!p||next>UINT64_MAX/2))return C::Invalid;
  Canonical expected{};
  if(mode==Attempt::Imported||mode==Attempt::Sd){
   if(mode==Attempt::Imported?!encoding_.validImported(p,n):!encoding_.validSd(p,n))return C::Invalid;
   expected.imported=p;expected.bytes=n;expected.object=encoding_.crc(p,n);
  }else{
   expected=encoding_.canonical();
   if(mode==Attempt::Fresh){if(n!=CheckedFreshInitArtifacts::ImageBytes)return C::Invalid;
    for(uint32_t at=0;at<n;++at)if(p[at]!=encoding_.expectedAt(at+24,expected))return C::Invalid;
   }
  }
  return cleanup(inventory(expected,next,attempt));
 }
};
}
