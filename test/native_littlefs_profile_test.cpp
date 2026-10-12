#include "ports/esp32s3/NativeRadioAsync.h"
#undef ESP_FAIL
#include "experimental/native_replica/CheckedLittleFsFreshInitArtifacts.h"
#include "experimental/native_replica/CheckedNvsFreshSources.h"
#include "experimental/native_replica/NvsReplicaMetadata.h"
#include "native_littlefs_profile_fixture.h"
#include <cassert>
#include <thread>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <fcntl.h>
#include <dirent.h>
using I=RiscExperimental::CheckedLittleFsFreshInitArtifacts;
using C=I::C;using A=RiscExperimental::NativeBaselineAdmission;
using S=RiscExperimental::CheckedNativeReplicaStore;
using N=RiscExperimental::CheckedNvsFreshSources;
namespace Async=RiscCpu::NativeRadioAsync;
using Bytes=std::vector<uint8_t>;
namespace {
const auto workerThread=std::this_thread::get_id();int workerIdentity;
uint64_t leaseEpoch=1,rootEpoch=1,writerEpoch=1,redirectEpoch=1;
unsigned competing=0,nested=0;bool proceed=true,armed=false;int observed=C::Ok;
unsigned callbacks=0,callbackCut=0,ioEvents=0,ioCut=0,reentries=0;std::string transition;
I*active=nullptr;
std::vector<native_lfs_fixture_operation> trace;std::vector<bool> observerTrace;
std::vector<size_t> readRequests;
std::map<std::string,std::map<std::string,Bytes>> nvs;
std::map<nvs_handle_t,std::string> nvHandles;uint32_t nextHandle=0;bool failReceipt=false;
uint32_t crc(const uint8_t*p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
void put(Bytes&p,size_t at,uint64_t n,unsigned bytes=4){for(unsigned i=0;i<bytes;++i)p[at+i]=uint8_t(n>>(8*i));}
Bytes image(){Bytes p(484);memcpy(p.data(),"RTENR03",8);put(p,8,6);put(p,16,1,8);put(p,108,484);memcpy(p.data()+384,"RTENBP1",8);put(p,392,6);put(p,396,1);put(p,476,crc(p.data()+384,92));put(p,480,crc(p.data(),480));return p;}
Bytes object(uint64_t id=2){auto p=image();Bytes out(24);memcpy(out.data(),"RTENOBJ",8);put(out,8,id,8);put(out,16,p.size());put(out,20,crc(p.data(),p.size()));out.insert(out.end(),p.begin(),p.end());return out;}
bool held(){return std::this_thread::get_id()==workerThread&&Async::sharedLease.load(std::memory_order_acquire)==2;}
void change(){
 if(transition=="lease")Async::endShared();
 else if(transition=="retained"){observed=C::Retained;Async::endShared();}
 else if(transition=="io")observed=C::Io;
 else if(transition=="cancel")proceed=false;
 else if(transition=="mount")++redirectEpoch;
 else if(transition=="epoch")++leaseEpoch;
 else if(transition=="root")++rootEpoch;
 else if(transition=="writer")++writerEpoch;
 else if(transition=="nested")++nested;
 else if(transition=="competing")++competing;
 else if(transition=="reentry"){assert(active&&active->inspectAbsent()==C::Busy&&active->inspectImportedAttempt(nullptr,0,0)==C::Busy&&active->inspectSdAttempt(nullptr,0,0)==C::Busy&&!active->exitSafe());++reentries;}
}
void callback(bool observing=false){if(armed){observerTrace.push_back(observing);if(++callbacks==callbackCut)change();}}
void io(void*,native_lfs_fixture_operation op,native_lfs_fixture_phase phase){
 if(!armed||phase!=NATIVE_LFS_AFTER||op>=NATIVE_LFS_FLASH_READ)return;
 trace.push_back(op);if(++ioEvents==ioCut)change();
}
I::Mount snapshot(){I::Mount m{};m.profile=I::PinnedProfile;m.partitionAddress=native_lfs_fixture_partition_address();m.partitionBytes=native_lfs_fixture_partition_bytes();m.encrypted=native_lfs_fixture_partition_encrypted();
 m.vfs=native_lfs_fixture_context();m.partition=native_lfs_fixture_partition();m.worker=std::this_thread::get_id()==workerThread?&workerIdentity:nullptr;
 m.mountEpoch=native_lfs_fixture_mount_generation();m.rootEpoch=rootEpoch;m.leaseEpoch=leaseEpoch;m.writerFenceEpoch=writerEpoch;m.redirectEpoch=redirectEpoch;
 m.nativeLease=Async::sharedLease.load(std::memory_order_acquire);m.competingWriters=competing;m.nestedMounts=nested;
 strcpy(m.mountPath,native_lfs_fixture_mount());strcpy(m.privateRoot,"/private");return m;
}
I::Port port(){return {nullptr,
 [](void*,I::Mount*out){callback();*out=snapshot();return native_lfs_fixture_binding_valid(out->vfs,out->partition)?int(C::Ok):int(C::Busy);},
 [](void*){callback(true);return observed;},[](void*){callback();return proceed;},
 [](void*,const void*v,const char*p,struct stat*s){assert(v==native_lfs_fixture_context()&&held());return native_lfs_fixture_stat(p,s);},
 [](void*,const void*v,const char*p){assert(v==native_lfs_fixture_context()&&held());return native_lfs_fixture_open(p,O_RDONLY);},
 [](void*,const void*v,int fd,void*p,size_t n){assert(v==native_lfs_fixture_context()&&held()&&n<=512);if(armed)readRequests.push_back(n);return native_lfs_fixture_read(fd,p,n);},
 [](void*,const void*v,int fd){assert(v==native_lfs_fixture_context()&&held());return native_lfs_fixture_close(fd);},
 [](void*,const void*v,const char*p){assert(v==native_lfs_fixture_context()&&held());return native_lfs_fixture_opendir(p);},
 [](void*,const void*v,void*d,I::Entry*out){assert(v==native_lfs_fixture_context()&&held());native_lfs_fixture_entry entry{};int rc=native_lfs_fixture_readdir(d,&entry);memcpy(out->name,entry.name,sizeof out->name);return rc;},
 [](void*,const void*v,void*d){assert(v==native_lfs_fixture_context()&&held());return native_lfs_fixture_closedir(d);}};}
void fresh(){armed=false;native_lfs_fixture_hook(nullptr,nullptr);native_lfs_fixture_clear_faults();Async::endShared();assert(native_lfs_fixture_reset()==0);assert(Async::tryShared());++leaseEpoch;rootEpoch=writerEpoch=redirectEpoch=1;competing=nested=0;proceed=true;observed=C::Ok;callbacks=callbackCut=ioEvents=ioCut=reentries=0;transition.clear();trace.clear();observerTrace.clear();readRequests.clear();native_lfs_fixture_clear_counters();native_lfs_fixture_hook(io,nullptr);nvs.clear();nvHandles.clear();failReceipt=false;}
void seed(const char*name,const Bytes&p){assert(!armed&&held());std::string path="/private/n00000006/";path+=name;assert(!native_lfs_fixture_seed(path.c_str(),p.data(),p.size()));}
int inspect(I&i,uint64_t next=1,bool attempt=true,const Bytes*given=nullptr){auto bytes=image();if(given)bytes=*given;active=&i;armed=true;const int rc=attempt?i.inspectAttempt(bytes.data(),bytes.size(),next):i.inspectAbsent();armed=false;active=nullptr;return rc;}
uint64_t operationCount(){native_lfs_fixture_counters c{};native_lfs_fixture_get_counters(&c);uint64_t n=0;for(auto v:c.calls)n+=v;return n;}
void terminal(I&i,int rc){native_lfs_fixture_counters c{};native_lfs_fixture_get_counters(&c);if(rc==C::Retained){assert(i.retained()&&!i.exitSafe());const auto before=operationCount();const auto cb=callbacks;assert(inspect(i)==C::Retained);assert(callbacks==cb&&operationCount()==before);}else assert(i.exitSafe()&&!i.retained()&&!c.live_files&&!c.live_dirs);}
void basics(){fresh();assert(!native_lfs_fixture_has_link_callback());seed(".pending",object());struct stat s{};assert(!native_lfs_fixture_stat("/private/n00000006/.pending",&s));assert(s.st_ino==0&&s.st_nlink==0&&s.st_dev==0&&s.st_ctime==0&&s.st_size==508&&S_ISREG(s.st_mode));int fd=native_lfs_fixture_open("/private/n00000006/.pending",O_RDONLY);assert(fd>=0);assert(!native_lfs_fixture_fstat(fd,&s));assert(!s.st_ino&&!s.st_nlink&&!s.st_dev&&!s.st_ctime);assert(!native_lfs_fixture_close(fd));I i(6,snapshot(),port());assert(inspect(i)==C::Ok);terminal(i,C::Ok);assert(!Async::tryShared());
 // An owner/second worker, mount manager and cleanup contender all need the
 // same real native atomic lease. Their failed acquisition causes no VFS call.
 const auto before=operationCount();unsigned excluded=0;for(unsigned k=0;k<4;++k)if(!Async::tryShared())++excluded;assert(excluded==4&&before==operationCount());
 int other=C::Ok;std::thread t([&]{other=inspect(i);});t.join();assert(other==C::Busy&&i.exitSafe());
 puts("Pinned actual ESP VFS/LittleFS: zero identity/link metadata, absent hard-link callback, actual native shared-lease exclusion and wrong-worker refusal PASS");}
void directoryMetadata(){
 // lfs_info.size is unspecified for directories. Exercise differing values
 // deterministically instead of depending on compiler stack reuse or ASan.
 struct Stats {unsigned directories=0,files=0;bool changeFile=false;};
 for(unsigned mode=0;mode<3;++mode){
  fresh();if(mode==0)assert(!native_lfs_fixture_rename("/private/n00000006","/private/saved"));
  else seed(".pending",object());
  Stats stats{};stats.changeFile=mode==2;auto p=port();p.context=&stats;
  p.stat=[](void*context,const void*v,const char*path,struct stat*out){
   assert(v==native_lfs_fixture_context()&&held());
   const int rc=native_lfs_fixture_stat(path,out);if(rc)return rc;
   auto&stats=*static_cast<Stats*>(context);
   if(S_ISDIR(out->st_mode))out->st_size=(++stats.directories&1)?off_t(-1):off_t(0x12345678);
   else if(S_ISREG(out->st_mode)&&stats.changeFile&&++stats.files==2)++out->st_size;
   return rc;
  };
  I i(6,snapshot(),p);const int expected=mode==2?C::Io:C::Ok;
  const int rc=inspect(i,1,mode!=0);assert(rc==expected);terminal(i,rc);
  assert(stats.directories>=2);if(mode==2)assert(stats.files==2);
 }
 puts("Pinned directory-size contract: missing/present inventory ignores unspecified sizes; regular-file size change refuses PASS");
}
void prefixes(){unsigned count=0;for(uint64_t id:{uint64_t(2),uint64_t(256),uint64_t(65536),uint64_t(1)<<32,uint64_t(1)<<56,UINT64_MAX-1})for(unsigned n=0;n<=508;++n){fresh();auto p=object(id);p.resize(n);seed(".pending",p);I i(6,snapshot(),port());const int rc=inspect(i,id>>1);if(rc!=C::Ok)fprintf(stderr,"pending prefix id=%llu length=%u next=%llu rc=%d\n",(unsigned long long)id,n,(unsigned long long)(id>>1),rc);assert(rc==C::Ok);terminal(i,C::Ok);++count;}printf("Actual VFS profile: %u canonical pending prefixes across six IDs including carries and maximum even ID PASS\n",count);}
void refusals(){unsigned cases=0;for(unsigned n=0;n<=508;++n){fresh();auto p=object();p.resize(n);seed(".pending",p);I i(6,snapshot(),port());assert(inspect(i,0)==C::Io);terminal(i,C::Io);++cases;}
 for(unsigned n=0;n<508;++n){fresh();auto p=object();p.resize(n);seed("replica-a.bin",p);I i(6,snapshot(),port());assert(inspect(i)==C::Io);++cases;}
 for(unsigned at=0;at<508;++at){fresh();auto p=object();p[at]^=0x80;seed("replica-a.bin",p);I i(6,snapshot(),port());assert(inspect(i)==C::Io);++cases;}
 for(const char*name:{"replica-b.bin","unknown",".hidden","replica-a.bin.alias"}){fresh();seed(name,object());I i(6,snapshot(),port());assert(inspect(i)==C::Io);++cases;}
 for(const char*name:{"replica-a.bin",".pending"}){fresh();auto p=object();p.push_back(0);seed(name,p);I i(6,snapshot(),port());assert(inspect(i)==C::Io);fresh();std::string path="/private/n00000006/";path+=name;assert(!native_lfs_fixture_mkdir(path.c_str()));I d(6,snapshot(),port());assert(inspect(d)==C::Io);cases+=2;}
 for(unsigned k=1;k<8;++k){fresh();auto p=object(0);p.resize(8+k);seed(".pending",p);I i(6,snapshot(),port());const uint64_t n=uint64_t(1)<<(8*k-1);assert(inspect(i,n)==C::Ok&&inspect(i,n-1)==C::Io);++cases;}
 for(uint64_t id:{uint64_t(0),uint64_t(1),uint64_t(3),uint64_t(4),UINT64_MAX}){fresh();seed(".pending",object(id));I i(6,snapshot(),port());assert(inspect(i)==C::Io);++cases;}
 for(unsigned at=0;at<484;++at){fresh();auto p=image();p[at]^=1;put(p,476,crc(p.data()+384,92));put(p,480,crc(p.data(),480));if(p==image())continue;I i(6,snapshot(),port());assert(inspect(i,1,true,&p)==C::Invalid);++cases;}
 fresh();I i(6,snapshot(),port());assert(inspect(i,0,false)==C::Ok);assert(inspect(i,UINT64_MAX/2+1)==C::Invalid);printf("Actual VFS profile: %u truncated/corrupt/noncanonical/unknown/type/sequence refusal cases PASS\n",cases);}
void faults(){fresh();seed("replica-a.bin",object());seed(".pending",object(4));Bytes initialFlash(native_lfs_fixture_flash_size());assert(native_lfs_fixture_snapshot(initialFlash.data(),initialFlash.size())==initialFlash.size());unsigned total=0,totalCb=0;std::vector<bool> observerBaseline;std::vector<native_lfs_fixture_operation> baseline;native_lfs_fixture_counters counts{};
 {I i(6,snapshot(),port());native_lfs_fixture_clear_counters();assert(inspect(i,2)==C::Ok);total=ioEvents;totalCb=callbacks;baseline=trace;observerBaseline=observerTrace;native_lfs_fixture_get_counters(&counts);}
 Bytes finalFlash(initialFlash.size());assert(native_lfs_fixture_snapshot(finalFlash.data(),finalFlash.size())==finalFlash.size()&&initialFlash==finalFlash);assert(!counts.calls[NATIVE_LFS_FLASH_PROG]&&!counts.calls[NATIVE_LFS_FLASH_ERASE]);
 unsigned cases=0;for(const char*kind:{"lease","retained","io","cancel","epoch","mount","root","reentry"})for(unsigned cut=1;cut<=total;++cut){fresh();seed("replica-a.bin",object());seed(".pending",object(4));transition=kind;ioCut=cut;I i(6,snapshot(),port());const int rc=inspect(i,2);if(transition=="reentry")assert(rc==C::Ok&&reentries==1);else if(transition=="io")assert(rc==C::Io);else if(transition=="retained")assert(rc==C::Retained);else if(transition=="cancel")assert(rc==C::Busy||rc==C::Ok);else {if(rc!=C::Busy&&rc!=C::Retained)fprintf(stderr,"fault %s cut %u rc %d\n",kind,cut,rc);assert(rc==C::Busy||rc==C::Retained);}terminal(i,rc);++cases;}
 for(const char*kind:{"lease","retained","io","cancel","epoch"})for(unsigned cut=1;cut<=totalCb;++cut){if(std::string(kind)=="io"&&!observerBaseline[cut-1])continue;fresh();seed("replica-a.bin",object());seed(".pending",object(4));transition=kind;callbackCut=cut;I i(6,snapshot(),port());const int rc=inspect(i,2);if(transition=="io")assert(rc==C::Io);else if(transition=="retained")assert(rc==C::Retained);else if(transition=="cancel")assert(rc==C::Busy||rc==C::Ok);else assert(rc==C::Busy||rc==C::Retained);terminal(i,rc);++cases;}
 for(unsigned op=0;op<NATIVE_LFS_OPERATION_COUNT;++op)for(uint64_t at=1;at<=counts.calls[op];++at)for(auto mode:{NATIVE_LFS_FAIL_BEFORE,NATIVE_LFS_FAIL_AFTER}){
  // A fake failed open after successful acquisition is not a valid API model.
  if((op==NATIVE_LFS_OPEN||op==NATIVE_LFS_OPENDIR)&&mode==NATIVE_LFS_FAIL_AFTER)continue;
  if(op==NATIVE_LFS_FLASH_PROG||op==NATIVE_LFS_FLASH_ERASE)continue;
  fresh();seed("replica-a.bin",object());seed(".pending",object(4));native_lfs_fixture_clear_counters();native_lfs_fixture_fault(native_lfs_fixture_operation(op),at,mode);I i(6,snapshot(),port());const int rc=inspect(i,2);if(rc!=C::Io&&rc!=C::Retained)fprintf(stderr,"op fault op=%u at=%llu mode=%u rc=%d\n",op,(unsigned long long)at,unsigned(mode),rc);assert(rc==C::Io||rc==C::Retained);terminal(i,rc);++cases;
 }
 fresh();seed("replica-a.bin",object());native_lfs_fixture_fault(NATIVE_LFS_READ,1,NATIVE_LFS_SHORT_READ);I i(6,snapshot(),port());assert(inspect(i)==C::Io);terminal(i,C::Io);
 printf("Actual VFS profile: %u operation/flash/callback failures and mount/root/lease epoch loss boundaries; checked close and retained reentry PASS\n",cases);}
// Restore cases deliberately sample grammar parts and every chunk boundary,
// rather than multiplying every byte, sequence, shape and fault combination.
// These images are supplied by the trusted caller; inspection is read-only.
uint32_t word(const Bytes&p,size_t at){uint32_t n=0;for(unsigned i=0;i<4;++i)n|=uint32_t(p[at+i])<<(8*i);return n;}
void finishRestore(Bytes&p,bool sd){if(!sd)put(p,476,crc(p.data()+384,92));put(p,p.size()-4,crc(p.data(),p.size()-4));}
Bytes restoreImage(bool sd,uint32_t bytes){
 const uint32_t header=sd?C::Header:C::HeaderV3,base=header+4;assert(bytes>=base&&bytes<=65512);
 Bytes p(bytes);memcpy(p.data(),sd?"RTENR01":"RTENR03",8);put(p,8,6);put(p,16,1);put(p,104,bytes-base);put(p,108,bytes);
 if(sd){put(p,12,1);p[48]=0x31;p[64]=0x72;}
 else{memcpy(p.data()+384,"RTENBP1",8);put(p,392,6);put(p,396,uint32_t(C::Origin::ImportedLegacy));put(p,400,bytes-base);p[408]=0x31;p[440]=0x72;put(p,472,1);}
 uint32_t at=header,count=0,remaining=bytes-base;
 while(remaining){assert(remaining>=21);uint32_t row=remaining>20+C::ValueMax?20+C::ValueMax:remaining;
  if(remaining>row&&remaining-row<21)row-=21-(remaining-row);
  char key[16]{};snprintf(key,sizeof key,"k%04u",unsigned(sd?9999-count:count));memcpy(p.data()+at,key,sizeof key);put(p,at+16,row-20);
  for(uint32_t j=20;j<row;++j)p[at+j]=uint8_t(count*17+j);
  at+=row;remaining-=row;++count;
 }
 // SD preserves its unique source order; imported maps must be strictly sorted.
 put(p,96,count);if(!sd)put(p,404,count);finishRestore(p,sd);return p;
}
Bytes restoreObject(const Bytes&p,uint64_t id=2){Bytes out(24);memcpy(out.data(),"RTENOBJ",8);put(out,8,id,8);put(out,16,p.size());put(out,20,crc(p.data(),p.size()));out.insert(out.end(),p.begin(),p.end());return out;}
int inspectRestore(I&i,const Bytes&p,bool sd,uint64_t next=1){
 native_lfs_fixture_counters before{},after{};native_lfs_fixture_get_counters(&before);
 active=&i;armed=true;const int rc=sd?i.inspectSdAttempt(p.data(),p.size(),next):i.inspectImportedAttempt(p.data(),p.size(),next);armed=false;active=nullptr;
 native_lfs_fixture_get_counters(&after);
 for(auto op:{NATIVE_LFS_WRITE,NATIVE_LFS_MKDIR,NATIVE_LFS_UNLINK,NATIVE_LFS_FSYNC,NATIVE_LFS_RENAME,NATIVE_LFS_FLASH_PROG,NATIVE_LFS_FLASH_ERASE})assert(before.calls[op]==after.calls[op]);
 return rc;
}
void restoreTerminal(I&i,int rc,const Bytes&p,bool sd){
 terminal(i,rc);if(rc!=C::Retained)return;
 const auto before=operationCount();const auto cb=callbacks;
 assert(inspectRestore(i,p,sd)==C::Retained&&inspectRestore(i,p,!sd)==C::Retained);
 assert(operationCount()==before&&callbacks==cb&&!i.exitSafe());
}
std::set<uint32_t> restoreBoundaries(const Bytes&p,bool sd){
 const uint32_t n=p.size()+24,header=(sd?C::Header:C::HeaderV3)+24;
 std::set<uint32_t> points{0,7,8,9,15,16,17,19,20,23,24,25,header-1,header,header+1,n-5,n-4,n-3,n-1,n};
 for(uint32_t at:{512u,1024u,32768u,65024u})for(uint32_t value:{at-1,at,at+1})if(value<=n)points.insert(value);
 for(uint32_t at=4096;at<=n;at+=4096)for(uint32_t value:{at-1,at,at+1})if(value<=n)points.insert(value);
 return points;
}
void restoreArtifacts(){
 unsigned accepted=0,prefixes=0,corruptions=0,refused=0;
 for(bool sd:{false,true})for(uint32_t size:{sd?132u:484u,9000u,65512u}){
  const auto p=restoreImage(sd,size),full=restoreObject(p);const uint32_t n=full.size();
  fresh();I empty(6,snapshot(),port());assert(inspectRestore(empty,p,sd,0)==C::Ok);restoreTerminal(empty,C::Ok,p,sd);
  assert(!native_lfs_fixture_rename("/private/n00000006","/private/saved"));I absent(6,snapshot(),port());assert(inspectRestore(absent,p,sd,0)==C::Ok);restoreTerminal(absent,C::Ok,p,sd);
  fresh();seed("replica-a.bin",full);seed(".pending",restoreObject(p,UINT64_MAX-1));
  Bytes before(native_lfs_fixture_flash_size()),after(before.size());assert(native_lfs_fixture_snapshot(before.data(),before.size())==before.size());
  I exact(6,snapshot(),port());assert(inspectRestore(exact,p,sd,UINT64_MAX/2)==C::Ok);restoreTerminal(exact,C::Ok,p,sd);
  assert(native_lfs_fixture_snapshot(after.data(),after.size())==after.size()&&before==after);
  std::vector<size_t> expected;for(unsigned file=0;file<2;++file){for(uint32_t at=0;at<n;at+=512)expected.push_back(n-at>512?512:n-at);expected.push_back(1);}
  assert(readRequests==expected);assert(inspectRestore(exact,p,sd,0)==C::Io);restoreTerminal(exact,C::Io,p,sd);++accepted;
  if(size<9000)continue;
  for(uint32_t length:restoreBoundaries(p,sd)){
   fresh();auto pending=full;pending.resize(length);seed(".pending",pending);I i(6,snapshot(),port());assert(inspectRestore(i,p,sd)==C::Ok);restoreTerminal(i,C::Ok,p,sd);++prefixes;
   // The same representative truncations can never be a committed bank.
   if(length!=n){fresh();seed("replica-a.bin",pending);I bank(6,snapshot(),port());assert(inspectRestore(bank,p,sd)==C::Io);restoreTerminal(bank,C::Io,p,sd);++refused;}
  }
  // Object framing, every image header/provenance region, every row's key,
  // size and value, every 512-byte seam, and the final image CRC are compared.
  std::set<uint32_t> points{0,7,8,15,16,19,20,23,24,31,32,36,40,44,64,72,88,120,124,128,132,136,n-4,n-1};
  if(!sd)for(uint32_t at:{383u,384u,391u,392u,396u,400u,404u,408u,439u,440u,471u,472u,476u,479u})points.insert(24+at);
  for(uint32_t at=sd?C::Header:C::HeaderV3;at<p.size()-4;at+=20+word(p,at+16))for(uint32_t offset:{0u,15u,16u,19u,20u,19u+word(p,at+16)})points.insert(24+at+offset);
  for(uint32_t at=512;at<n;at+=512)for(uint32_t point:{at-1,at,at+1})points.insert(point);
  unsigned which=0;for(uint32_t at:points){assert(at<n);fresh();auto bad=full;bad[at]^=0x80;
   seed((which++&1)?"replica-a.bin":".pending",bad);I i(6,snapshot(),port());assert(inspectRestore(i,p,sd)==C::Io);restoreTerminal(i,C::Io,p,sd);++corruptions;
  }
  for(const char*name:{"replica-a.bin",".pending"}){fresh();auto over=full;over.push_back(0);seed(name,over);I i(6,snapshot(),port());assert(inspectRestore(i,p,sd)==C::Io);restoreTerminal(i,C::Io,p,sd);++refused;}
  // A different canonical image with valid CRCs is still not this attempt.
  for(unsigned mode=0;mode<3;++mode){fresh();seed("replica-a.bin",full);auto other=p;other[mode==0?(sd?48:408):mode==1?(sd?64:440):(sd?148:500)]^=1;finishRestore(other,sd);I i(6,snapshot(),port());assert(inspectRestore(i,other,sd)==C::Io);restoreTerminal(i,C::Io,other,sd);++refused;}
 }
 // Representative imported/SD IDs retain the FreshEmpty prefix arithmetic.
 for(bool sd:{false,true}){const auto p=restoreImage(sd,9000);
  for(uint64_t id:{uint64_t(256),uint64_t(1)<<56,UINT64_MAX-1})for(uint32_t n:{9u,15u,16u,4097u}){fresh();auto object=restoreObject(p,id);object.resize(n);seed(".pending",object);I i(6,snapshot(),port());assert(inspectRestore(i,p,sd,id>>1)==C::Ok);restoreTerminal(i,C::Ok,p,sd);++prefixes;}
  for(uint64_t id:{uint64_t(0),uint64_t(1),uint64_t(3),uint64_t(4),UINT64_MAX}){fresh();seed(".pending",restoreObject(p,id));I i(6,snapshot(),port());assert(inspectRestore(i,p,sd)==C::Io);restoreTerminal(i,C::Io,p,sd);++refused;}
  for(const char*name:{"replica-b.bin","unknown","replica-a.bin.alias"}){fresh();seed(name,restoreObject(p));I i(6,snapshot(),port());assert(inspectRestore(i,p,sd)==C::Io);restoreTerminal(i,C::Io,p,sd);++refused;}
 }
 printf("Actual VFS imported/SD: %u exact images (including 9000/65512 bytes), %u boundary/ID pending prefixes, %u grammar/chunk corruptions and %u artifact refusals; <=512-byte reads and unchanged flash PASS\n",accepted,prefixes,corruptions,refused);
}
void restoreShapeRefusals(){
 unsigned cases=0;
 for(bool sd:{false,true}){
  const auto good=restoreImage(sd,9000);const uint32_t header=sd?C::Header:C::HeaderV3;
  auto reject=[&](const Bytes&p){fresh();I i(6,snapshot(),port());const auto before=operationCount();assert(inspectRestore(i,p,sd)==C::Invalid&&operationCount()==before);restoreTerminal(i,C::Invalid,p,sd);++cases;};
  for(uint32_t at:{0u,7u,8u,12u,16u,20u,24u,96u,100u,104u,108u,112u,127u}){auto p=good;p[at]^=1;finishRestore(p,sd);reject(p);}
  if(sd){auto p=good;memset(p.data()+48,0,16);finishRestore(p,sd);reject(p);}
  else{
   for(uint32_t at:{383u,384u,391u,392u,396u,400u,404u,472u}){auto p=good;p[at]^=1;finishRestore(p,sd);reject(p);}
   for(uint32_t at:{408u,440u}){auto p=good;memset(p.data()+at,0,32);finishRestore(p,sd);reject(p);}
   auto p=good;p[476]^=1;put(p,p.size()-4,crc(p.data(),p.size()-4));reject(p);
  }
  for(unsigned kind=0;kind<10;++kind){auto p=good;const uint32_t second=header+20+word(p,header+16);
   if(kind==0)p[header]='K';
   if(kind==1)p[header]=0;
   if(kind==2)memset(p.data()+header,'a',16);
   if(kind==3)p[header+6]='x';
   if(kind==4)memcpy(p.data()+second,p.data()+header,16);
   if(kind==5)put(p,header+16,0);
   if(kind==6)put(p,header+16,C::ValueMax+1);
   if(kind==7){put(p,96,UINT32_MAX);if(!sd)put(p,404,UINT32_MAX);}
   if(kind==8){put(p,96,0);if(!sd)put(p,404,0);}
   if(kind==9){uint32_t last=header;while(last+20+word(p,last+16)<p.size()-4)last+=20+word(p,last+16);put(p,last+16,word(p,last+16)+1);}
   finishRestore(p,sd);reject(p);
  }
  if(!sd){auto p=good;p[header]='z';finishRestore(p,sd);reject(p);}
  auto p=good;p.back()^=1;reject(p);
  p=restoreImage(sd,65512);p.push_back(0);reject(p);p.resize(header+3);reject(p);reject({});reject(image());reject(restoreImage(!sd,9000));
  // Older/newer replica shapes are not silently accepted through either path.
  for(char version:{'2','4'}){p=good;p[6]=version;finishRestore(p,sd);reject(p);}
  fresh();I i(6,snapshot(),port());const auto before=operationCount();assert(inspectRestore(i,good,sd,UINT64_MAX/2+1)==C::Invalid&&operationCount()==before);assert(inspect(i,1,true,&good)==C::Invalid&&operationCount()==before);cases+=2;
 }
 printf("Actual VFS imported/SD: %u noncanonical/unsupported/oversize inputs refuse before filesystem I/O PASS\n",cases);
}
void restoreFaults(){
 unsigned reads=0,interruptions=0,identities=0,closes=0;
 for(bool sd:{false,true}){
  const auto p=restoreImage(sd,65512),object=restoreObject(p);
  fresh();seed("replica-a.bin",object);seed(".pending",restoreObject(p,4));I baseline(6,snapshot(),port());assert(inspectRestore(baseline,p,sd,2)==C::Ok);restoreTerminal(baseline,C::Ok,p,sd);
  const auto operations=trace;const auto requests=readRequests;std::vector<unsigned> readCuts;
  for(unsigned at=0;at<operations.size();++at)if(operations[at]==NATIVE_LFS_READ)readCuts.push_back(at+1);
  assert(readCuts.size()==requests.size()&&requests.size()==258); // 128 chunks + EOF per artifact.
  // Sample data reads at each 4 KiB seam, the first/last chunk and each EOF.
  std::set<unsigned> faultReads{1,2,64,128,129,130,131,193,257,258};
  for(unsigned at=8;at<=128;at+=8)for(unsigned value:{at,at+1,129+at,130+at})faultReads.insert(value);
  for(unsigned at:faultReads)for(auto mode:{NATIVE_LFS_FAIL_BEFORE,NATIVE_LFS_FAIL_AFTER,NATIVE_LFS_SHORT_READ}){
   if(mode==NATIVE_LFS_SHORT_READ&&requests[at-1]==1)continue;
   fresh();seed("replica-a.bin",object);seed(".pending",restoreObject(p,4));native_lfs_fixture_fault(NATIVE_LFS_READ,at,mode);I i(6,snapshot(),port());const int rc=inspectRestore(i,p,sd,2);assert(rc==C::Io);restoreTerminal(i,rc,p,sd);++reads;
  }
  // A few strategically placed cuts cover the first, middle, last and EOF
  // reads in both files without a fault x callback x byte Cartesian product.
  for(unsigned request:{1u,2u,64u,128u,129u,130u,193u,257u,258u})for(const char*kind:{"lease","retained","io","cancel","epoch","root","mount","writer","nested","competing","reentry"}){
   fresh();seed("replica-a.bin",object);seed(".pending",restoreObject(p,4));transition=kind;ioCut=readCuts[request-1];I i(6,snapshot(),port());const int rc=inspectRestore(i,p,sd,2);
   assert(ioEvents>=ioCut);
   if(transition=="reentry")assert(rc==C::Ok&&reentries==1);
   else if(transition=="io")assert(rc==C::Io);
   else if(transition=="cancel")assert(rc==C::Busy);
   else{assert(rc==C::Retained&&ioEvents==ioCut);native_lfs_fixture_counters c{};native_lfs_fixture_get_counters(&c);assert(c.live_files&&c.live_dirs);}
   restoreTerminal(i,rc,p,sd);++interruptions;
  }
  for(auto op:{NATIVE_LFS_CLOSE,NATIVE_LFS_CLOSEDIR})for(auto mode:{NATIVE_LFS_FAIL_BEFORE,NATIVE_LFS_FAIL_AFTER}){
   fresh();seed("replica-a.bin",object);native_lfs_fixture_fault(op,1,mode);I i(6,snapshot(),port());const int rc=inspectRestore(i,p,sd);assert(rc==C::Retained);restoreTerminal(i,rc,p,sd);++closes;
  }
  // Zero inode/link/device are a pinned contract, not file identity. Unexpected
  // fields and changed path metadata refuse; mount identity loss retains custody.
  for(unsigned mode=0;mode<6;++mode){
   fresh();seed("replica-a.bin",object);struct State{unsigned mode,files=0;} state{mode};auto custom=port();custom.context=&state;
   custom.stat=[](void*context,const void*v,const char*path,struct stat*out){assert(v==native_lfs_fixture_context()&&held());const int rc=native_lfs_fixture_stat(path,out);if(rc)return rc;auto&s=*static_cast<State*>(context);if(S_ISREG(out->st_mode)){
    ++s.files;if(s.mode==0)out->st_ino=1;else if(s.mode==1)out->st_nlink=1;else if(s.mode==2)out->st_dev=1;
    else if(s.files==2){if(s.mode==3)++out->st_size;else if(s.mode==4)++out->st_mtime;else out->st_mode=S_IFDIR;}
   }return rc;};
   I i(6,snapshot(),custom);assert(inspectRestore(i,p,sd)==C::Io);restoreTerminal(i,C::Io,p,sd);if(mode<3)assert(readRequests.empty());else assert(state.files==2);++identities;
  }
  for(unsigned mode=0;mode<5;++mode){
   fresh();seed("replica-a.bin",object);auto custom=port();custom.context=&mode;
   custom.snapshot=[](void*context,I::Mount*out){callback();*out=snapshot();if(readRequests.size()>=2)switch(*static_cast<unsigned*>(context)){
    case 0:out->vfs=&workerIdentity;break;case 1:out->partition=&workerIdentity;break;case 2:out->worker=nullptr;break;case 3:++out->mountEpoch;break;case 4:strcpy(out->privateRoot,"/other");break;
   }return int(C::Ok);};
   I i(6,snapshot(),custom);assert(inspectRestore(i,p,sd)==C::Retained&&readRequests.size()==2);restoreTerminal(i,C::Retained,p,sd);++identities;
  }
 }
 printf("Actual VFS imported/SD: %u failed/short chunk and EOF reads, %u interruption/reentry boundaries, %u custody/metadata mismatches and %u retained-close failures PASS\n",reads,interruptions,identities,closes);
}
const uint8_t Owner[16]={'R','i','s','c','R','T','E','/','N','a','t','i','v','e','1',0};
void adapterIdentity(){
 using Adapter=RiscExperimental::FreshInitArtifactInspector;
 using Linux=RiscExperimental::CheckedFreshInitArtifacts;
 fresh();I concrete(6,snapshot(),port()),other(6,snapshot(),port());Linux linux(6,"/native/private",{});
 Adapter a(concrete),copy=a,b(other),posix(linux),empty,null(nullptr),nullLittle(static_cast<I*>(nullptr)),nullLinux(static_cast<Linux*>(nullptr));
 const auto before=operationCount();
 assert(a&&copy&&a.sameInspector(copy)&&copy.sameInspector(a));
 assert(!a.sameInspector(b)&&!b.sameInspector(a)&&!a.sameInspector(posix)&&!posix.sameInspector(a));
 assert(a.concreteAddress()==&concrete&&copy.concreteAddress()==&concrete&&a.concreteBytes()==sizeof concrete);
 assert(posix.concreteAddress()==&linux&&posix.concreteBytes()==sizeof linux);
 assert(a.namespaceId()==6&&a.boundTo(6,"/native/private")&&!a.boundTo(7,"/native/private")&&!a.boundTo(6,"/private")&&!a.boundTo(6,"/native/private/"));
 for(Adapter e:{empty,null,nullLittle,nullLinux}){assert(!e&&!e.sameInspector(e)&&!a.sameInspector(e)&&!e.sameInspector(a));assert(!e.concreteAddress()&&!e.concreteBytes()&&!e.namespaceId()&&!e.boundTo(6,"/native/private"));assert(e.exitSafe()&&!e.retained());assert(e.inspectAbsent()==C::Invalid&&e.inspectAttempt(nullptr,0,0)==C::Invalid&&e.inspectImportedAttempt(nullptr,0,0)==C::Invalid&&e.inspectSdAttempt(nullptr,0,0)==C::Invalid);}
 N sources(6,Owner,a,{nullptr,[](void*){return held();},[](void*){return ESP_OK;},[](void*){return observed;}});
 assert(sources.boundTo(6,a)&&sources.boundTo(6,copy)&&!sources.boundTo(7,copy)&&!sources.boundTo(6,b)&&!sources.boundTo(6,posix)&&!sources.boundTo(6,empty));
 assert(operationCount()==before&&sources.exitSafe());
 for(bool sd:{false,true}){fresh();const auto p=restoreImage(sd,9000);seed("replica-a.bin",restoreObject(p));I i(6,snapshot(),port());Adapter dispatch(i);active=&i;armed=true;const int rc=sd?dispatch.inspectSdAttempt(p.data(),p.size(),1):dispatch.inspectImportedAttempt(p.data(),p.size(),1);armed=false;active=nullptr;assert(rc==C::Ok&&dispatch.exitSafe()&&!dispatch.retained());restoreTerminal(i,rc,p,sd);}
 puts("Typed inspector adapter: copy identity, exact concrete ranges/root, distinct/null/type refusal, NVS source binding and imported/SD dispatch PASS");
}

}
// Exact-IDF-shaped NVS boundary model. These map operations are not an NVS
// flash/durability qualification; filesystem execution below is actual VFS/LFS.
extern "C" esp_err_t nvs_open(const char*name,nvs_open_mode_t mode,nvs_handle_t*out){assert(held());if(!nvs.count(name)&&mode==NVS_READONLY)return ESP_ERR_NVS_NOT_FOUND;nvs[name];*out=++nextHandle;nvHandles[*out]=name;return ESP_OK;}
extern "C" esp_err_t nvs_get_blob(nvs_handle_t h,const char*key,void*out,size_t*n){assert(held()&&nvHandles.count(h));auto&space=nvs.at(nvHandles.at(h));auto it=space.find(key);if(it==space.end())return ESP_ERR_NVS_NOT_FOUND;if(!out){*n=it->second.size();return ESP_OK;}if(*n<it->second.size())return ESP_ERR_NVS_INVALID_LENGTH;memcpy(out,it->second.data(),it->second.size());*n=it->second.size();return ESP_OK;}
extern "C" esp_err_t nvs_set_blob(nvs_handle_t h,const char*key,const void*p,size_t n){assert(held()&&nvHandles.count(h));auto*b=static_cast<const uint8_t*>(p);nvs.at(nvHandles.at(h))[key]=Bytes(b,b+n);if(failReceipt&&!strcmp(key,"root")){failReceipt=false;return ESP_FAIL;}return ESP_OK;}
extern "C" esp_err_t nvs_commit(nvs_handle_t h){assert(held()&&nvHandles.count(h));return ESP_OK;}
extern "C" void nvs_close(nvs_handle_t h){assert(held()&&nvHandles.erase(h));}
extern "C" esp_err_t nvs_get_used_entry_count(nvs_handle_t h,size_t*n){assert(held()&&nvHandles.count(h));*n=nvs.at(nvHandles.at(h)).size();return ESP_OK;}
struct Iterator {std::string space;std::vector<std::string> keys;size_t at=0;};
std::set<Iterator*> iterators;
extern "C" nvs_iterator_t nvs_entry_find(const char*partition,const char*space,nvs_type_t type){assert(held()&&!strcmp(partition,"nvs")&&type==NVS_TYPE_ANY);auto it=nvs.find(space);if(it==nvs.end()||it->second.empty())return nullptr;auto*p=new Iterator;p->space=space;for(auto&v:it->second)p->keys.push_back(v.first);iterators.insert(p);return p;}
extern "C" nvs_iterator_t nvs_entry_next(nvs_iterator_t it){assert(held());auto*p=static_cast<Iterator*>(it);assert(iterators.count(p));if(++p->at<p->keys.size())return p;iterators.erase(p);delete p;return nullptr;}
extern "C" void nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t*out){assert(held());auto*p=static_cast<Iterator*>(it);assert(iterators.count(p));*out={};strcpy(out->namespace_name,p->space.c_str());strcpy(out->key,p->keys.at(p->at).c_str());out->type=NVS_TYPE_BLOB;}
extern "C" void nvs_release_iterator(nvs_iterator_t it){assert(held());if(!it)return;auto*p=static_cast<Iterator*>(it);assert(iterators.erase(p));delete p;}
// Interpose only calls from the actual AppDataFiles translation unit. Its
// absolute paths must name this exact registered mount and firmware-private root.
static const char*relative(const char*p){assert(held()&&p&&!strncmp(p,"/native/private",15)&&(p[15]=='/'||!p[15]));return p+7;}
extern "C" int __wrap_stat(const char*p,struct stat*s){return native_lfs_fixture_stat(relative(p),s);}
extern "C" int __wrap_fstat(int fd,struct stat*s){assert(held()&&fd>=10000);return native_lfs_fixture_fstat(fd-10000,s);}
extern "C" int __wrap_open(const char*p,int flags,...){const int fd=native_lfs_fixture_open(relative(p),flags);return fd<0?fd:fd+10000;}
extern "C" ssize_t __wrap_read(int fd,void*p,size_t n){assert(held()&&fd>=10000);return native_lfs_fixture_read(fd-10000,p,n);}
extern "C" ssize_t __wrap_write(int fd,const void*p,size_t n){assert(held()&&fd>=10000);return native_lfs_fixture_write(fd-10000,p,n);}
extern "C" int __wrap_close(int fd){assert(held()&&fd>=10000);return native_lfs_fixture_close(fd-10000);}
extern "C" int __wrap_fsync(int fd){assert(held()&&fd>=10000);return native_lfs_fixture_fsync(fd-10000);}
extern "C" int __wrap_mkdir(const char*p,mode_t){return native_lfs_fixture_mkdir(relative(p));}
extern "C" int __wrap_unlink(const char*p){return native_lfs_fixture_unlink(relative(p));}
extern "C" int __wrap_rename(const char*from,const char*to){return native_lfs_fixture_rename(relative(from),relative(to));}
extern "C" DIR*__wrap_opendir(const char*p){return static_cast<DIR*>(native_lfs_fixture_opendir(relative(p)));}
extern "C" dirent*__wrap_readdir(DIR*d){assert(held());native_lfs_fixture_entry entry{};int rc=native_lfs_fixture_readdir(d,&entry);if(rc<=0)return nullptr;static dirent out{};out={};memcpy(out.d_name,entry.name,sizeof out.d_name);out.d_type=entry.type;return &out;}
extern "C" int __wrap_closedir(DIR*d){assert(held());return native_lfs_fixture_closedir(d);}
namespace {
struct Fixture {
 I inspector{6,snapshot(),port()};
 RiscStorage::AppDataFiles files{{nullptr,[](void*){return 0u;},[](void*){return held()&&observed==C::Ok;},malloc,free}};
 RiscExperimental::NvsReplicaMetadata metadata{{nullptr,[](void*){return held();},[](void*){return ESP_OK;}}};
 S store{6,files,metadata.api(),{nullptr,[](void*){return held();},[](void*,uint32_t*t,uint32_t*u){if(!held())return false;*t=0x80000;*u=16*4096;return true;}},C::heap(),&inspector};
 C core{6,store.backend(),S::limits(),C::heap()};
 N sources{6,Owner,inspector,{nullptr,[](void*){return held();},[](void*){return ESP_OK;},[](void*){return observed;}}};
 Fixture(){assert(files.configure("/native/private"));}
 int initialize(bool resume=false){A factory(this,core,{nullptr,[](void*){return held();},[](void*){return leaseEpoch;}},{},sources.sources(),{});C::Initialization a;int rc=resume?factory.admitFreshResume(&a):factory.admitFreshEmpty(&a);if(!rc)rc=core.initializeFreshEmpty(a);store.finishOperation();return rc;}
};
void sequence(uint64_t n){Bytes b(24);memcpy(b.data(),"RTENSEQ",8);put(b,8,n,8);put(b,20,crc(b.data(),20));nvs["rnr00000006"]["next"]=b;}
uint64_t counter(){auto&p=nvs.at("rnr00000006").at("next");uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(p[8+i])<<(8*i);return n;}
void receipt(){fresh();Fixture f;failReceipt=true;assert(f.initialize()==C::Uncertain&&!f.core.ready());const auto&p=nvs.at("rnr00000006").at("root");assert(p.size()==48&&!memcmp(p.data(),"RTENFI1",8));assert(nvHandles.empty()&&iterators.empty());}
void composition(){fresh();{Fixture f;assert(f.initialize()==C::Ok&&f.core.ready()&&f.core.baseline().origin()==C::Origin::FreshEmpty&&counter()==1);assert(f.store.exitSafe()&&f.sources.exitSafe());}
 {Fixture f;assert(f.core.load()==C::Ok&&f.core.revision()==1&&f.core.baseline().origin()==C::Origin::FreshEmpty);}
 for(unsigned n=0;n<=508;++n){receipt();sequence(8);auto p=object(16);p.resize(n);seed(".pending",p);Fixture f;assert(f.core.load()==C::Initializing);const int rc=f.initialize(true);if(rc)fprintf(stderr,"resume length %u rc %d\n",n,rc);assert(rc==C::Ok&&f.core.ready()&&counter()==9&&f.store.activeObject()==18&&f.store.exitSafe()&&f.sources.exitSafe());struct stat s{};assert(native_lfs_fixture_stat("/private/n00000006/.pending",&s)<0&&errno==ENOENT);assert(nvHandles.empty()&&iterators.empty());}
 for(unsigned mode=0;mode<7;++mode){receipt();sequence(3);if(mode==0)seed("replica-b.bin",object(6));if(mode==1)seed("unknown",{});if(mode==2)seed(".pending",object(8));if(mode==3)nvs["rte00000006"]["saved"]={1};if(mode==4)nvs["kvj00000006"]["journal"]={1};if(mode==5)nvs["kvd00000006"]["delta"]={1};if(mode==6)nvs["rnr00000006"]["unknown"]={1};const auto before=nvs;Bytes flash(native_lfs_fixture_flash_size());assert(native_lfs_fixture_snapshot(flash.data(),flash.size())==flash.size());Fixture f;assert(f.initialize(true)!=C::Ok&&!f.core.ready()&&nvs==before);Bytes after(flash.size());assert(native_lfs_fixture_snapshot(after.data(),after.size())==after.size()&&flash==after);}
 puts("Actual Core + NVS source collector/model + Store + AppDataFiles + pinned VFS/LittleFS: first creation, reload, all 509 interrupted prefix resumes with new IDs, 7 evidence-preserving source/artifact refusals PASS");}
void mutationFaults(){
 fresh();native_lfs_fixture_counters baseline{};{Fixture f;native_lfs_fixture_clear_counters();assert(f.initialize()==C::Ok);native_lfs_fixture_get_counters(&baseline);}
 unsigned cases=0,recovered=0,retained=0;
 for(auto op:{NATIVE_LFS_WRITE,NATIVE_LFS_FSYNC,NATIVE_LFS_CLOSE,NATIVE_LFS_RENAME,NATIVE_LFS_FLASH_PROG,NATIVE_LFS_FLASH_ERASE})for(uint64_t cut=1;cut<=baseline.calls[op];++cut)for(auto mode:{NATIVE_LFS_FAIL_BEFORE,NATIVE_LFS_FAIL_AFTER}){
  fresh();{Fixture f;native_lfs_fixture_clear_counters();native_lfs_fixture_fault(op,cut,mode);const int rc=f.initialize();if(rc==C::Retained){assert(!f.store.exitSafe());++retained;}else assert(rc==C::Ok||rc==C::Io||rc==C::Uncertain||rc==C::NoSpace);}
  native_lfs_fixture_clear_faults();native_lfs_fixture_hook(nullptr,nullptr);Async::endShared();const int mounted=native_lfs_fixture_reboot();if(mounted)fprintf(stderr,"reboot op %u cut %llu mode %u failed\n",unsigned(op),(unsigned long long)cut,unsigned(mode));assert(!mounted);assert(Async::tryShared());++leaseEpoch;
  {Fixture f;const int selected=f.core.load();if(selected!=C::Ok){assert(selected==C::Missing||selected==C::Initializing);const int rc=f.initialize(selected==C::Initializing);if(rc)fprintf(stderr,"recover op %u cut %llu mode %u rc %d\n",unsigned(op),(unsigned long long)cut,unsigned(mode),rc);assert(rc==C::Ok);++recovered;}assert(f.core.ready()&&f.core.revision()==1&&f.core.baseline().origin()==C::Origin::FreshEmpty&&f.core.pendingCount()==0);}
  ++cases;
 }
 printf("Actual VFS/Core composition: %u write/sync/close/rename/flash-program/erase failures; %u receipt-or-absence recoveries and %u retained operations PASS\n",cases,recovered,retained);
}
void inventories(){
 fresh();assert(!native_lfs_fixture_rename("/private/n00000006","/private/saved"));I missing(6,snapshot(),port());assert(inspect(missing,0,false)==C::Ok&&missing.exitSafe());
 fresh();assert(!native_lfs_fixture_rename("/private/n00000006","/private/saved"));I changed(6,snapshot(),port());transition="root";ioCut=2;assert(inspect(changed,0,false)==C::Busy&&changed.exitSafe());
 for(unsigned mode=0;mode<2;++mode){fresh();seed("replica-a.bin",object());seed(".pending",object(4));auto p=port();
  if(mode==0)p.readdir=[](void*,const void*,void*d,I::Entry*out){native_lfs_fixture_entry e{};int rc=native_lfs_fixture_readdir(d,&e);memcpy(out->name,e.name,sizeof out->name);return rc>0?0:rc;};
  else p.readdir=[](void*,const void*,void*d,I::Entry*out){native_lfs_fixture_entry e{};int rc=native_lfs_fixture_readdir(d,&e);if(rc>0)strcpy(out->name,"replica-a.bin");return rc;};
  I i(6,snapshot(),p);assert(inspect(i,2)==C::Io);terminal(i,C::Io);
 }
 puts("Actual VFS profile: missing namespace recheck/root epoch, early enumeration EOF and duplicate-entry refusal PASS");
}
void admission(){for(unsigned mode=0;mode<12;++mode){fresh();I::Mount m=snapshot();if(mode==0)++m.profile;if(mode==1)m.vfs=&workerIdentity;if(mode==2)m.partition=&workerIdentity;if(mode==3)++m.partitionAddress;if(mode==4)m.partitionBytes*=2;if(mode==5)m.encrypted=true;if(mode==6)strcpy(m.mountPath,"/other");if(mode==7)strcpy(m.privateRoot,"/other");if(mode==8)++m.mountEpoch;if(mode==9)++m.leaseEpoch;if(mode==10)m.nestedMounts=1;if(mode==11)m.competingWriters=1;const auto before=operationCount();I i(6,m,port());const int rc=inspect(i);assert((rc==C::Invalid||rc==C::Busy)&&i.exitSafe()&&before==operationCount());}
 fresh();const auto m=snapshot();Async::endShared();assert(native_lfs_fixture_reset()==0);assert(Async::tryShared());I stale(6,m,port());assert(inspect(stale)==C::Busy&&stale.exitSafe());puts("Explicit target admission: wrong source/profile/mount/partition/root/epoch/writer/nested binding refuses before I/O; actual remount invalidates old admission PASS");}
}
int main(){setbuf(stdout,nullptr);basics();admission();inventories();directoryMetadata();prefixes();refusals();faults();restoreArtifacts();restoreShapeRefusals();restoreFaults();adapterIdentity();composition();mutationFaults();armed=false;native_lfs_fixture_hook(nullptr,nullptr);native_lfs_fixture_clear_faults();native_lfs_fixture_shutdown();Async::endShared();assert(nvHandles.empty()&&iterators.empty());printf("Experimental profile host size=%zu, typed injection=%zu. Production profile unselected; NVS model only; no hardware/power-loss qualification.\n",sizeof(I),sizeof(RiscExperimental::FreshInitArtifactInspector));}
