#pragma once
#include "CheckedSdReplicaReader.h"
#include "CheckedLegacySettingsReader.h"
#include <new>
namespace RiscExperimental {
// Explicit background observation only. A closed report is historical evidence,
// never a reusable create/adopt permit or a per-launch readiness scan.
class CheckedSdSettingsCatalog {
 public:
 using C=NativeReplicaCore;using R=CheckedSdReplicaReader;using L=CheckedLegacySettingsReader;using Digest=R::Digest;using Guard=R::Guard;
 enum class State {Idle,Running,Verified,Done,Failed,Cancelled,Retained};
 enum class Reason {None,MediaUnavailable,CustodyLost,Allocation,ReadFailure,Retained};
 enum class Directory {Unknown,Absent,Empty,Populated,Incomplete,Unreadable,Invalid};
 enum class Content {NotChecked,Missing,Valid,Invalid,Unreadable,CapacityExceeded};
 static constexpr size_t entryStorageBytes(){return sizeof(Entry);}
 static constexpr size_t childStorageBytes(){return sizeof(R)>sizeof(L)?sizeof(R):sizeof(L);}
 struct Limits {uint32_t entries=128,nameBytes=8192;};
 // Identity and source digests are sensitive profile-derived evidence: never
 // log/export fingerprints of real settings in diagnostics or build artifacts.
 struct Slot {Content content=Content::NotChecked;C::Identity identity{};L::Format format=L::Format::None;uint64_t generation=0;uint32_t count=0,mapBytes=0,sourceBytes=0;uint8_t sourceDigest[32]{};};
 struct Artifacts {uint32_t selectedPages=0,orphanPages=0,staging=0,unknown=0,directories=0,backups=0;};
 class Report {
  public:
  ~Report(){reset();}
  bool valid()const{return valid_;}uint32_t namespaceId()const{return valid_?namespace_:0;}
  const Slot*slot(unsigned i)const{return valid_&&i<3?slots_+i:nullptr;}
  Directory initialDirectory()const{return before_;}Directory finalDirectory()const{return after_;}
  bool stableInventory()const{return valid_&&stable_;}bool vacant()const{return valid_&&vacant_;}
  uint32_t initialEntries()const{return initialEntries_;}uint32_t finalEntries()const{return finalEntries_;}
  uint8_t initialBackups()const{return initialBackups_;}uint8_t finalBackups()const{return finalBackups_;}uint8_t recovered()const{return recovered_;}
  Artifacts artifacts()const{return artifacts_;}
  void reset(){C::wipe(this,sizeof *this);}
  private:friend class CheckedSdSettingsCatalog;
  bool valid_=false,stable_=false,vacant_=false;uint32_t namespace_=0,initialEntries_=0,finalEntries_=0;Directory before_=Directory::Unknown,after_=Directory::Unknown;
  uint8_t initialBackups_=0,finalBackups_=0,recovered_=0;Slot slots_[3]{};Artifacts artifacts_{};
 };
 CheckedSdSettingsCatalog(const C&core,Digest digest,Guard guard,C::Allocator allocator=C::heap())
  :core_(core),hash_(digest),guard_(guard),alloc_(allocator){}
 ~CheckedSdSettingsCatalog(){destroyChild();releaseEntries();wipeScratch();}
 CheckedSdSettingsCatalog(const CheckedSdSettingsCatalog&)=delete;CheckedSdSettingsCatalog&operator=(const CheckedSdSettingsCatalog&)=delete;
 State state()const{return state_;}Reason reason()const{return reason_;}int error()const{return error_;}
 bool exitSafe()const{return !session_&&!child_&&(state_==State::Idle||state_==State::Done||state_==State::Failed||state_==State::Cancelled);}
 int start(const risc_storage_volume_api_v1*volume,Limits limits){
  if(entered_)return C::Busy;
  EntryLock lock(entered_);
  if(state_!=State::Idle||!core_.namespaceId()||core_.namespaceId()>INT32_MAX||!limits.entries||limits.entries>256||!limits.nameBytes||limits.nameBytes>32768||!guard_.held||!guard_.worker||!alloc_.allocate||!alloc_.release||!hash_.begin||!hash_.update||!hash_.finish||!risc_storage_volume_download(volume))return C::Invalid;
  const auto*ext=risc_storage_volume_extension(volume);if(!volume->dir_open||!volume->dir_next||!ext->dir_close_checked||!ext->handle_error)return C::Invalid;
  reason_=Reason::None;error_=0;if(!guard_.worker(guard_.context))return C::Busy;if(!guard_.held(guard_.context)){reason_=Reason::CustodyLost;return C::Conflict;}
  limits_=limits;entryBytes_=2*limits.entries*sizeof(Entry);entries_=static_cast<Entry*>(alloc_.allocate(alloc_.context,entryBytes_));if(!entries_){reason_=Reason::Allocation;return C::NoMemory;}std::memset(entries_,0,entryBytes_);
  volume_=volume;api_=risc_storage_volume_packages(volume);report_.namespace_=core_.namespaceId();std::snprintf(namespaceName_,sizeof namespaceName_,"%08X",unsigned(core_.namespaceId()));std::snprintf(directory_,sizeof directory_,"/RTECFG/%s",namespaceName_);
  state_=State::Running;phase_=BeginScan;return C::Ok;
 }
 int start(const risc_storage_volume_api_v1*volume){return start(volume,Limits{});}
 void cancel(){if(entered_)return;EntryLock lock(entered_);if(state_==State::Verified){if(observe()==RISC_STORAGE_STATE_RETAINED){retain();return;}finish(State::Cancelled,C::Ok);return;}if(state_==State::Running){cancelled_=true;if(child_)cancelChild();else phase_=EndScan;}}
 int take(Report*out){
  if(entered_)return C::Busy;
  EntryLock lock(entered_);if(!out||out->valid())return C::Invalid;if(state_!=State::Verified)return C::Stale;
  const int observed=observe();if(observed==RISC_STORAGE_STATE_RETAINED){retain();return C::Retained;}
  if(!guard_.held(guard_.context)){reason_=Reason::CustodyLost;finish(State::Failed,C::Conflict);return C::Conflict;}if(observed!=RISC_STORAGE_STATE_READY){reason_=Reason::MediaUnavailable;finish(State::Failed,C::Io);return C::Io;}
  *out=report_;out->valid_=true;report_.reset();wipeScratch();state_=State::Done;return C::Ok;
 }
 void step(){
  if(entered_||state_!=State::Running)return;
  EntryLock lock(entered_);
  const int observed=observe();if(observed==RISC_STORAGE_STATE_RETAINED){retain();return;}
  if(!cancelled_&&!error_){if(!guard_.held(guard_.context))fail(C::Conflict,Reason::CustodyLost);else if(observed!=RISC_STORAGE_STATE_READY)fail(C::Io,Reason::MediaUnavailable);}
  if(child_){stepChild();return;}
  if(phase_==EndScan){
   if(session_){const int rc=api_->base.end(volume_->context,session_);if(wait(rc))return;if(rc!=RISC_STORAGE_ASYNC_OK){retain();return;}session_=operation_=0;}
   if(observe()==RISC_STORAGE_STATE_RETAINED){retain();return;}
   if(cancelled_||error_){finish(cancelled_?State::Cancelled:State::Failed,error_);return;}
   if(scan_==0){phase_=StartChild;return;}prepareReport();return;
  }
  if(phase_==Summarize){summarizeStep();return;}
  if(phase_==StartChild){startChild();return;}
  if(phase_==BeginScan){const int rc=api_->base.begin(volume_->context,&session_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}if(rc){fail(C::Io,Reason::ReadFailure);return;}phase_=RefreshScan;return;}
  if(!operation_){submit();return;}
  result_.status=INT32_MIN;const int rc=api_->base.collect(volume_->context,session_,operation_,&result_);
  if(rc==RISC_STORAGE_ASYNC_PENDING||(rc==RISC_STORAGE_ASYNC_BUSY&&result_.status!=RISC_STORAGE_ASYNC_BUSY))return;
  operation_=0;
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}
  auto&s=scans_[scan_];
  if(phase_==OpenScan&&rc==RISC_STORAGE_ASYNC_NOT_FOUND){if(s.level<2){++s.level;s.count=s.names=s.work=0;phase_=OpenScan;}else{s.status=Directory::Unreadable;phase_=EndScan;}C::wipe(&result_,sizeof result_);return;}
  if(phase_==NextScan&&rc==RISC_STORAGE_ASYNC_END){s.status=s.level?Directory::Absent:s.count?Directory::Populated:Directory::Empty;phase_=EndScan;C::wipe(&result_,sizeof result_);return;}
  if(rc!=RISC_STORAGE_ASYNC_OK){s.status=rc==RISC_STORAGE_ASYNC_INVALID?Directory::Invalid:Directory::Unreadable;phase_=EndScan;C::wipe(&result_,sizeof result_);return;}
  if(phase_==RefreshScan)phase_=OpenScan;else if(phase_==OpenScan)phase_=NextScan;else if(phase_==NextScan)admitEntry();else fail(C::Invalid,Reason::ReadFailure);
  C::wipe(&result_,sizeof result_);
 }
 private:
 struct EntryLock {bool&v;explicit EntryLock(bool&b):v(b){v=true;}~EntryLock(){v=false;}};
 struct Entry {char name[128];uint64_t size;uint8_t directory;};
 struct Scan {Directory status=Directory::Unknown;uint32_t count=0,names=0,work=0;unsigned level=0;};
 enum Phase {BeginScan,RefreshScan,OpenScan,NextScan,EndScan,StartChild,Child,Summarize};
 const C&core_;Digest hash_;Guard guard_;C::Allocator alloc_;Limits limits_{};Report report_;Scan scans_[2]{};Entry*entries_=nullptr;size_t entryBytes_=0;
 const risc_storage_volume_api_v1*volume_=nullptr;const risc_storage_volume_api_v1_packages*api_=nullptr;uint64_t session_=0,operation_=0;
 State state_=State::Idle;Reason reason_=Reason::None;Phase phase_=BeginScan;int error_=0;bool entered_=false,cancelled_=false;unsigned scan_=0,source_=0,child_=0;
 unsigned summaryStage_=0,summaryScan_=0;uint32_t summaryAt_=0;char pagePrefix_[2][96]{};
 char namespaceName_[9]{},directory_[32]{};risc_storage_async_result_v1 result_{};
 alignas(R) alignas(L) uint8_t childBytes_[sizeof(R)>sizeof(L)?sizeof(R):sizeof(L)]{};
 R*reader(){return reinterpret_cast<R*>(childBytes_);}L*legacy(){return reinterpret_cast<L*>(childBytes_);}
 int observe()const{return api_->base.fs.state.observe(volume_->context);}
 static bool wait(int rc){return rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY;}
 static const char*name(unsigned n){return n==0?"replica.index":n==1?"settings.index":"settings.kv";}
 static bool foldEqual(const char*a,const char*b){for(;*a&&*b;++a,++b){unsigned x=uint8_t(*a),y=uint8_t(*b);if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return false;}return !*a&&!*b;}
 Entry*list(unsigned scan){return entries_+scan*limits_.entries;}const Entry*list(unsigned scan)const{return entries_+scan*limits_.entries;}
 void releaseEntries(){if(entries_){C::wipe(entries_,entryBytes_);alloc_.release(alloc_.context,entries_);entries_=nullptr;entryBytes_=0;}}
 void destroyChild(){if(child_==1)reader()->~R();else if(child_==2)legacy()->~L();child_=0;C::wipe(childBytes_,sizeof childBytes_);}
 void cancelChild(){if(child_==1)reader()->cancel();else if(child_==2)legacy()->cancel();}
 void wipeScratch(){C::wipe(&result_,sizeof result_);C::wipe(pagePrefix_,sizeof pagePrefix_);}
 void retain(){state_=State::Retained;reason_=Reason::Retained;error_=C::Retained;wipeScratch();}
 void fail(int rc,Reason reason){error_=rc;reason_=reason;if(child_)cancelChild();else phase_=EndScan;}
 void finish(State state,int rc){destroyChild();releaseEntries();report_.reset();wipeScratch();state_=state;error_=rc;}
 void submit(){risc_storage_async_request_v1 q{};q.operation=phase_==RefreshScan?RISC_STORAGE_ASYNC_REFRESH:phase_==OpenScan?RISC_STORAGE_ASYNC_OPEN_DIRECTORY:RISC_STORAGE_ASYNC_NEXT_ENTRY;const unsigned level=scans_[scan_].level;std::strcpy(q.path,level==0?directory_:level==1?"/RTECFG":"/");const int rc=api_->base.submit(volume_->context,session_,&q,&operation_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}if(rc){scans_[scan_].status=Directory::Unreadable;phase_=EndScan;}}
 void admitEntry(){
  auto&s=scans_[scan_];const auto&e=result_.entry;const char*end=static_cast<const char*>(std::memchr(e.name,0,sizeof e.name));
  if(!end||end==e.name||e.is_directory>1||std::strchr(e.name,'/')||std::strchr(e.name,'\\')){s.status=Directory::Invalid;phase_=EndScan;return;}
  const uint32_t n=uint32_t(end-e.name)+1;
  if(++s.work>limits_.entries||n>limits_.nameBytes-s.names){s.status=Directory::Incomplete;phase_=EndScan;return;}s.names+=n;
  // Exact dots are traversal metadata, never children; count them as scan work.
  if(!std::strcmp(e.name,".")||!std::strcmp(e.name,"..")){if(!e.is_directory){s.status=Directory::Invalid;phase_=EndScan;}return;}
  for(uint32_t i=0;i<s.count;++i)if(foldEqual(list(scan_)[i].name,e.name)){s.status=Directory::Invalid;phase_=EndScan;return;}
  auto&out=list(scan_)[s.count++];std::memcpy(out.name,e.name,n);out.size=e.size;out.directory=e.is_directory;
  if(s.level&&foldEqual(e.name,s.level==1?namespaceName_:"RTECFG")){s.status=e.is_directory?Directory::Unreadable:Directory::Invalid;phase_=EndScan;}
 }
 void startChild(){
  if(!guard_.worker(guard_.context))return;
  if(source_==0){new(childBytes_)R(core_,hash_,guard_,alloc_);child_=1;}else{new(childBytes_)L(core_,hash_,guard_,alloc_);child_=2;}
  int rc=child_==1?reader()->start(volume_):legacy()->start(volume_,source_==1?L::Source::PagedV2:L::Source::SingleFile);
  if(rc==C::Busy){destroyChild();return;}
  if(rc){destroyChild();fail(rc,rc==C::NoMemory?Reason::Allocation:Reason::ReadFailure);return;}phase_=Child;
 }
 void stepChild(){
  bool running=false,retained=false,verified=false,safe=false;
  if(child_==1){reader()->step();running=reader()->state()==R::State::Running;retained=reader()->state()==R::State::Retained;verified=reader()->state()==R::State::Verified;safe=reader()->exitSafe();}
  else{legacy()->step();running=legacy()->state()==L::State::Running;retained=legacy()->state()==L::State::Retained;verified=legacy()->state()==L::State::Verified;safe=legacy()->exitSafe();}
  if(retained){retain();return;}if(running)return;
  if(observe()==RISC_STORAGE_STATE_RETAINED){retain();return;}
  if(cancelled_||error_){if(verified){cancelChild();safe=child_==1?reader()->exitSafe():legacy()->exitSafe();}if(!safe){retain();return;}finish(cancelled_?State::Cancelled:State::Failed,error_);return;}
  auto&slot=report_.slots_[source_];
  if(verified){
   int rc;if(child_==1){R::Candidate candidate;rc=reader()->take(&candidate);if(!rc){slot.content=Content::Valid;slot.identity=candidate.identity();slot.generation=slot.identity.generation;slot.count=candidate.count();slot.mapBytes=candidate.mapSize();slot.sourceBytes=slot.mapBytes;}}
   else{L::Candidate candidate;rc=legacy()->take(&candidate);if(!rc){slot.content=Content::Valid;slot.format=candidate.format();slot.generation=candidate.generation();slot.count=candidate.count();slot.mapBytes=candidate.mapSize();slot.sourceBytes=candidate.sourceSize();std::memcpy(slot.sourceDigest,candidate.sourceDigest(),32);}}
   if(rc==C::Retained){retain();return;}if(rc){destroyChild();fail(rc,rc==C::Conflict?Reason::CustodyLost:Reason::MediaUnavailable);return;}
  }else if(child_==1){const auto why=reader()->reason();slot.content=why==R::Reason::CheckpointMissing?Content::Missing:why==R::Reason::InvalidContent?Content::Invalid:why==R::Reason::Capacity?Content::CapacityExceeded:Content::Unreadable;}
  else {const auto why=legacy()->reason();slot.content=why==L::Reason::SourceMissing?Content::Missing:(why==L::Reason::InvalidContent||why==L::Reason::SourceChanged)?Content::Invalid:why==L::Reason::Capacity?Content::CapacityExceeded:Content::Unreadable;}
  destroyChild();if(++source_<3)phase_=StartChild;else{scan_=1;phase_=BeginScan;}
 }
 static bool complete(Directory d){return d==Directory::Absent||d==Directory::Empty||d==Directory::Populated;}
 const Entry*find(unsigned scan,const char*name)const{if(scans_[scan].level)return nullptr;for(uint32_t i=0;i<scans_[scan].count;++i)if(!std::strcmp(list(scan)[i].name,name))return list(scan)+i;return nullptr;}
 static bool same(const Entry&a,const Entry&b){return !std::strcmp(a.name,b.name)&&a.size==b.size&&a.directory==b.directory;}
 static bool sameObject(const Entry&a,const Entry&b){return a.size==b.size&&a.directory==b.directory&&!a.directory;}
 uint8_t backupMask(unsigned scan)const{uint8_t mask=0;for(unsigned n=0;n<3;++n){char b[48];std::snprintf(b,sizeof b,"%s.risc-replace",name(n));if(find(scan,b))mask|=uint8_t(1u<<n);}return mask;}
 bool recovered(unsigned n)const{if(!complete(scans_[0].status)||!complete(scans_[1].status)||scans_[0].level||scans_[1].level)return false;char b[48];std::snprintf(b,sizeof b,"%s.risc-replace",name(n));const Entry*old=find(0,b),*now=find(1,name(n));return old&&now&&!find(0,name(n))&&!find(1,b)&&sameObject(*old,*now);}
 bool selectedPage(const Entry&e)const{
  if(e.directory)return false;
  for(unsigned source=0;source<2;++source){const Slot&s=report_.slots_[source];if(s.content!=Content::Valid)continue;const uint32_t bytes=source?s.sourceBytes:s.mapBytes;const size_t prefix=std::strlen(pagePrefix_[source]);if(std::strlen(e.name)!=prefix+8||std::memcmp(e.name,pagePrefix_[source],prefix))continue;
   uint32_t page=0;bool valid=true;for(unsigned i=0;i<8;++i){const unsigned c=uint8_t(e.name[prefix+i]);if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))){valid=false;break;}page=page*16+(c<='9'?c-'0':c-'a'+10);}const uint64_t at=uint64_t(page)*4096;
   if(valid&&at<bytes){const uint32_t left=bytes-uint32_t(at);if(e.size==(left>4096?4096:left))return true;}
  }return false;
 }
 void prepareReport(){
  report_.before_=scans_[0].status;report_.after_=scans_[1].status;report_.initialEntries_=scans_[0].level?0:scans_[0].count;report_.finalEntries_=scans_[1].level?0:scans_[1].count;report_.initialBackups_=backupMask(0);report_.finalBackups_=backupMask(1);
  report_.stable_=complete(scans_[0].status)&&complete(scans_[1].status);
  if(scans_[0].status==Directory::Absent||scans_[1].status==Directory::Absent)report_.stable_=scans_[0].status==Directory::Absent&&scans_[1].status==Directory::Absent;
  for(unsigned n=0;n<3;++n){if(recovered(n))report_.recovered_|=uint8_t(1u<<n);if(report_.slots_[n].content==Content::Missing&&(find(0,name(n))||find(1,name(n))))report_.slots_[n].content=Content::Unreadable;}
  for(unsigned source=0;source<2;++source){const Slot&s=report_.slots_[source];if(s.content!=Content::Valid)continue;
   if(source)std::snprintf(pagePrefix_[source],sizeof pagePrefix_[source],"kv-%08x%08x-",unsigned(s.generation>>32),unsigned(s.generation));
   else{char digest[65];for(unsigned i=0;i<32;++i)std::snprintf(digest+2*i,3,"%02x",unsigned(s.identity.digest[i]));std::snprintf(pagePrefix_[source],sizeof pagePrefix_[source],"nr-%08x%08x-%s-",unsigned(s.generation>>32),unsigned(s.generation),digest);C::wipe(digest,sizeof digest);}
  }phase_=Summarize;
 }
 // At most eight inventory comparisons/classifications per poll. Comparisons
 // scan the fixed native entry table; no card input controls unbounded work.
 void summarizeStep(){
  for(unsigned work=0;work<8;++work){
   if(summaryStage_==0){
    if(summaryScan_==2){summaryStage_=1;summaryAt_=0;continue;}
    if(scans_[summaryScan_].level||summaryAt_==scans_[summaryScan_].count){++summaryScan_;summaryAt_=0;continue;}
    const auto&e=list(summaryScan_)[summaryAt_++];const Entry*other=find(1-summaryScan_,e.name);if(other&&same(e,*other))continue;
    bool allowed=false;for(unsigned n=0;n<3;++n)if(report_.recovered_&(1u<<n)){char b[48];std::snprintf(b,sizeof b,"%s.risc-replace",name(n));if(!std::strcmp(e.name,summaryScan_?name(n):b))allowed=true;}if(!allowed)report_.stable_=false;
   }else{
    if(scans_[1].level||summaryAt_==scans_[1].count){report_.vacant_=report_.stable_&&(report_.before_==Directory::Absent||report_.before_==Directory::Empty)&&(report_.after_==Directory::Absent||report_.after_==Directory::Empty);for(const auto&s:report_.slots_)if(s.content!=Content::Missing)report_.vacant_=false;releaseEntries();wipeScratch();state_=State::Verified;return;}
    const auto&e=list(1)[summaryAt_++];bool source=false;for(unsigned n=0;n<3;++n)if(!std::strcmp(e.name,name(n)))source=true;if(source&&!e.directory)continue;
    auto&a=report_.artifacts_;if(e.directory){++a.directories;continue;}if(selectedPage(e)){++a.selectedPages;continue;}const size_t n=std::strlen(e.name);if(n>=13&&!std::strcmp(e.name+n-13,".risc-replace")){++a.backups;continue;}
    if(!std::strncmp(e.name,"nr-",3)||!std::strncmp(e.name,"kv-",3))++a.orphanPages;else if(!std::strcmp(e.name,".replica.next")||!std::strcmp(e.name,".settings.index.next")||!std::strcmp(e.name,".settings.next"))++a.staging;else ++a.unknown;
   }
  }
 }
};
}
