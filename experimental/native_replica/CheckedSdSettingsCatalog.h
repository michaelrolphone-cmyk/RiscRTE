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
 // Nonhistorical inventory remains bounded separately. Restore history uses a
 // fixed window and repeated directory sweeps; this bounds total returned entries
 // (including skipped history) across both scans, not just retained descriptors.
 static constexpr uint32_t RestoreWorkMax=1048576,HistoryWindow=64;
 struct Limits {uint32_t entries=128,nameBytes=8192,restoreWork=RestoreWorkMax;};
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
  :core_(core),hash_(digest),guard_(guard),alloc_(allocator),namespace_(core.namespaceId()),prep_(this,preparationWorker,prepareCallback){}
 ~CheckedSdSettingsCatalog(){destroyChild();releaseEntries();wipeScratch();}
 CheckedSdSettingsCatalog(const CheckedSdSettingsCatalog&)=delete;CheckedSdSettingsCatalog&operator=(const CheckedSdSettingsCatalog&)=delete;
 State state()const{return prep_.retained()?State::Retained:prep_.pending()?State::Running:state_;}Reason reason()const{return prep_.retained()?Reason::Retained:prep_.pending()?Reason::None:reason_;}int error()const{return prep_.retained()?C::Retained:prep_.pending()?C::Busy:error_;}
 bool exitSafe()const{return !prep_.pending()&&!io_.pinned()&&!session_&&!child_&&(state_==State::Idle||state_==State::Done||state_==State::Failed||state_==State::Cancelled);}
 // Capture core framing on the native worker, then transfer ownership through
 // collection before owner scans or child construction can begin.
 int prepareNative(NativeSdAttempt&attempt,Limits limits){return prepareMode(attempt,limits,false);}
 int prepareNative(NativeSdAttempt&attempt){return prepareNative(attempt,Limits{});}
 // Opt-in complete-map retention for automatic native restore. Ordinary reports
 // remain observation-only. No caller-provided report can mint this capability.
 int prepareRestoreNative(NativeSdAttempt&attempt,Limits limits){return prepareMode(attempt,limits,true);}
 int prepareRestoreNative(NativeSdAttempt&attempt){return prepareRestoreNative(attempt,Limits{});}
 // Synchronization verifies a known base without granting restore or mutation
 // authority. Writer-owned target bytes are checked independently at mutation.
 int prepareSynchronizationNative(NativeSdAttempt&attempt,Limits limits){return prepareMode(attempt,limits,true,true);}
 int prepareSynchronizationNative(NativeSdAttempt&attempt){return prepareSynchronizationNative(attempt,Limits{});}
 int takeRestoreCandidate(R::Candidate*out){
  if(prep_.pending()||(io_.native()&&!io_.owner()))return C::Busy;
  if(entered_)return C::Busy;
  EntryLock lock(entered_);if(!out||out->valid_||out->bytes_||!restore_||!io_.native())return C::Invalid;
  if(state_!=State::Verified)return C::Stale;
  const int custody=checkCustody();if(custody==C::Busy)return custody;if(custody==C::Retained){retain();return custody;}
  if(custody){finish(State::Failed,custody);return error_;}
  const auto&a=report_.artifacts_;const auto&s=report_.slots_[0];
  const uint32_t pages=(s.mapBytes+4095)/4096;
  const uint32_t expected=pages+1+restoreHistorical_+allowedStage_+allowedBackup_;
  const uint8_t backups=uint8_t((allowedBackup_||restoreCandidate_.sourceBackup_)?1:0);
  if(!report_.stable_||report_.before_!=Directory::Populated||report_.after_!=Directory::Populated||s.content!=Content::Valid||report_.slots_[1].content!=Content::Missing||report_.slots_[2].content!=Content::Missing||a.orphanPages!=restoreHistorical_||a.staging!=allowedStage_||a.unknown||a.directories||a.backups!=allowedBackup_||report_.initialBackups_!=backups||report_.finalBackups_!=backups||report_.recovered_||a.selectedPages!=pages||report_.initialEntries_!=expected||report_.finalEntries_!=expected||!restoreCandidate_.valid_||!artifactValid_){finish(State::Failed,C::Conflict);return error_;}
  const int released=io_.release();if(released){if(released==C::Retained)retain();else finish(State::Failed,released);return error_;}
  restoreCandidate_.sourceCore_=&core_;restoreCandidate_.sourceAttempt_=io_.attempt();restoreCandidate_.sourceEpoch_=io_.attempt()->epoch();restoreCandidate_.sourceClosed_=io_.attempt()->closedSequence();restoreCandidate_.restoreQualified_=!synchronize_;restoreCandidate_.transferTo(*out);
  report_.reset();wipeScratch();state_=State::Done;return C::Ok;
 }
 bool preparationWorkerStep(){return prep_.workerStep();}
 int collectPreparation(){
  if(!io_.native()||!io_.owner())return C::Invalid;
  if(prep_.retained()){retain();return C::Retained;}
  if(!prep_.pending())return C::Stale;
  const int rc=prep_.collect();if(rc==C::Busy&&prep_.pending())return rc;
  if(rc==C::Retained){retain();return rc;}if(rc){finish(rc==C::Stale?State::Cancelled:State::Failed,rc);return error_;}return C::Ok;
 }
 int start(const risc_storage_volume_api_v1*volume,Limits limits){
  if(io_.native()&&!prep_.workerClaimed())return C::Busy;
  if(entered_)return C::Busy;
  EntryLock lock(entered_);
  if(state_!=State::Idle||!core_.namespaceId()||core_.namespaceId()>INT32_MAX||!limits.entries||limits.entries>256||!limits.nameBytes||limits.nameBytes>32768||(restore_&&(!limits.restoreWork||limits.restoreWork>RestoreWorkMax))||!guard_.held||!guard_.worker||!alloc_.allocate||!alloc_.release||!hash_.begin||!hash_.update||!hash_.finish||!risc_storage_volume_download(volume))return C::Invalid;
  const auto*ext=risc_storage_volume_extension(volume);if(!volume->dir_open||!volume->dir_next||!ext->dir_close_checked||!ext->handle_error)return C::Invalid;
  reason_=Reason::None;error_=0;if(!guard_.worker(guard_.context))return C::Busy;if(!prep_.workerClaimed()&&!guard_.held(guard_.context)){reason_=Reason::CustodyLost;return C::Conflict;}
  limits_=limits;entryBytes_=(2*limits.entries+(restore_?HistoryWindow:0))*sizeof(Entry);entries_=static_cast<Entry*>(alloc_.allocate(alloc_.context,entryBytes_));if(!entries_){reason_=Reason::Allocation;return C::NoMemory;}std::memset(entries_,0,entryBytes_);
  volume_=volume;api_=risc_storage_volume_packages(volume);report_.namespace_=core_.namespaceId();std::snprintf(namespaceName_,sizeof namespaceName_,"%08X",unsigned(core_.namespaceId()));std::snprintf(directory_,sizeof directory_,"/RTECFG/%s",namespaceName_);
  state_=State::Running;restorePreflight_=restore_;phase_=restore_?StartChild:BeginScan;return C::Ok;
 }
 int start(const risc_storage_volume_api_v1*volume){return start(volume,Limits{});}
 void cancel(){if(io_.native()&&!io_.owner())return;
  if(prep_.pending()){prep_.cancel();return;}if(entered_)return;EntryLock lock(entered_);if(state_==State::Verified){if(retainedCustody()){retain();return;}finish(State::Cancelled,C::Ok);return;}if(state_==State::Running){cancelled_=true;if(child_)cancelChild();else phase_=EndScan;}}
 int take(Report*out){
  if(prep_.pending()||(io_.native()&&!io_.owner()))return C::Busy;
  if(entered_)return C::Busy;
  EntryLock lock(entered_);if(!out||out->valid())return C::Invalid;if(state_!=State::Verified)return C::Stale;
  const int custody=checkCustody();if(custody==C::Busy)return custody;if(custody==C::Retained){retain();return custody;}
  if(custody){reason_=custody==C::Conflict?Reason::CustodyLost:Reason::MediaUnavailable;finish(State::Failed,custody);return error_;}
  if(io_.pinned()){const int released=io_.release();if(released){if(released==C::Retained)retain();else finish(State::Failed,released);return error_;}}
  *out=report_;out->valid_=true;report_.reset();restoreCandidate_.reset();wipeScratch();state_=State::Done;return C::Ok;
 }
 void step(){
  if(prep_.pending()||(io_.native()&&!io_.owner()))return;
  if(entered_||state_!=State::Running)return;
  EntryLock lock(entered_);
  // A child has its own exact session. Let it drain/check custody; the catalog
  // must neither observe the provider nor stall on its sibling-session Busy.
  if(io_.native()&&child_){stepChild();return;}
  const int custody=checkCustody(!cancelled_&&!error_);if(custody==C::Busy)return;if(custody==C::Retained){retain();return;}
  if(!cancelled_&&!error_&&custody)fail(custody,custody==C::Conflict?Reason::CustodyLost:Reason::MediaUnavailable);
  if(child_){stepChild();return;}
  if(cancelled_)phase_=EndScan;
  if(phase_==EndScan){
   if(session_){const int rc=io_.native()?io_.end():api_->base.end(volume_->context,session_);if(wait(rc))return;if(rc!=RISC_STORAGE_ASYNC_OK&&(!io_.native()||rc!=NativeSdAttempt::Lost)){retain();return;}session_=operation_=0;if(rc==NativeSdAttempt::Lost){const int lost=io_.custody();if(lost==C::Retained){retain();return;}fail(lost==C::Io?C::Io:C::Conflict,lost==C::Io?Reason::MediaUnavailable:Reason::CustodyLost);}}
   if(retainedCustody()){retain();return;}
   if(cancelled_||error_){finish(cancelled_?State::Cancelled:State::Failed,error_);return;}
   if(restore_&&!scans_[scan_].level&&complete(scans_[scan_].status)&&!historyDone_){if(!completeHistorySweep())return;phase_=SummarizeHistory;return;}
   if(scan_==0){phase_=StartChild;return;}
   if(restore_&&!artifactsDone_){artifact_=0;phase_=BeginArtifacts;return;}
   prepareReport();return;
  }
  if(phase_==Summarize){summarizeStep();return;}
  if(phase_==SummarizeHistory){summarizeHistory();return;}
  if(phase_==BeginArtifacts||phase_==ReadArtifact||phase_==EndArtifacts){stepArtifacts();return;}
  if(phase_==StartChild){startChild();return;}
  if(phase_==BeginScan){const int rc=io_.native()?io_.begin(&session_):api_->base.begin(volume_->context,&session_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}if(rc){fail(C::Io,Reason::ReadFailure);return;}phase_=io_.native()?OpenScan:RefreshScan;return;}
  if(!operation_){submit();return;}
  result_.status=INT32_MIN;const int rc=io_.native()?io_.collect(&result_):api_->base.collect(volume_->context,session_,operation_,&result_);
  if(rc==RISC_STORAGE_ASYNC_PENDING||(rc==RISC_STORAGE_ASYNC_BUSY&&result_.status!=RISC_STORAGE_ASYNC_BUSY))return;
  operation_=0;
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}
  auto&s=scans_[scan_];
  if(phase_==OpenScan&&rc==RISC_STORAGE_ASYNC_NOT_FOUND){if(s.level<2){++s.level;s.count=s.names=s.work=0;phase_=OpenScan;}else{s.status=Directory::Unreadable;phase_=EndScan;}C::wipe(&result_,sizeof result_);return;}
  if(phase_==NextScan&&rc==RISC_STORAGE_ASYNC_END){s.status=s.level?Directory::Absent:(s.count||historyPassCount_)?Directory::Populated:Directory::Empty;phase_=EndScan;C::wipe(&result_,sizeof result_);return;}
  if(rc!=RISC_STORAGE_ASYNC_OK){s.status=rc==RISC_STORAGE_ASYNC_INVALID?Directory::Invalid:Directory::Unreadable;phase_=EndScan;C::wipe(&result_,sizeof result_);return;}
  if(phase_==RefreshScan)phase_=OpenScan;else if(phase_==OpenScan)phase_=NextScan;else if(phase_==NextScan)admitEntry();else fail(C::Invalid,Reason::ReadFailure);
  C::wipe(&result_,sizeof result_);
  // A collected native directory result has surrendered its operation token.
  // Queue at most one next entry now, rather than spending a scheduler turn on
  // that submission alone. This never executes provider work or loops through
  // entries; BUSY retries on a later owner poll and cancellation still drains
  // the single accepted operation before checked close/release.
  if(io_.native()&&phase_==NextScan&&!cancelled_&&!error_)submit();
 }
 private:
 int prepareMode(NativeSdAttempt&attempt,Limits limits,bool restore,bool synchronize=false){
  if(prep_.pending()||io_.native()||state_!=State::Idle)return C::Invalid;
  const int rc=io_.bind(attempt,NativeSdSession::Role::Catalog,namespace_);if(rc)return rc;
  // Publish the mode and bounds before the release-fenced worker queue. A
  // rejected bind must not change an already admitted catalog's behavior.
  limits_=limits;restore_=restore;synchronize_=synchronize;const int queued=prep_.begin(io_);if(queued){restore_=synchronize_=false;const int released=io_.release();return released==C::Retained?released:queued;}return C::Ok;
 }
 struct EntryLock {bool&v;explicit EntryLock(bool&b):v(b){v=true;}~EntryLock(){v=false;}};
 struct Entry {char name[128];uint64_t size;uint8_t directory;};
 struct Scan {Directory status=Directory::Unknown;uint32_t count=0,names=0,work=0,history=0;unsigned level=0;uint8_t historyDigest[32]{},historySum[32]{},historyXor[32]{};};
 enum Phase {BeginScan,RefreshScan,OpenScan,NextScan,EndScan,StartChild,Child,Summarize,SummarizeHistory,BeginArtifacts,ReadArtifact,EndArtifacts};
 bool restore_=false,restorePreflight_=false;uint32_t restoreHistorical_=0;R::Candidate restoreCandidate_;
 bool synchronize_=false,artifactValid_=true,artifactsDone_=false;unsigned artifact_=0;
 uint32_t allowedStage_=0,allowedBackup_=0;
 uint32_t restoreWork_=0,historySweep_=0,historyCount_=0,historyAt_=0,historyPassCount_=0,historyEmitted_=0;
 bool historyMore_=false,historyDone_=false;char historyCursor_[94]{};uint8_t historySeen_[32]{},historySum_[32]{},historyXor_[32]{};
 const C&core_;Digest hash_;Guard guard_;C::Allocator alloc_;Limits limits_{};Report report_;Scan scans_[2]{};Entry*entries_=nullptr;size_t entryBytes_=0;
 const risc_storage_volume_api_v1*volume_=nullptr;const risc_storage_volume_api_v1_packages*api_=nullptr;uint64_t session_=0,operation_=0;
 uint32_t namespace_=0,nativeCapacity_=0,nativeHeader_=0;NativeSdSession io_;NativeSdPreparation prep_;
 State state_=State::Idle;Reason reason_=Reason::None;Phase phase_=BeginScan;int error_=0;bool entered_=false,cancelled_=false;unsigned scan_=0,source_=0,child_=0;
 unsigned summaryStage_=0,summaryScan_=0;uint32_t summaryAt_=0;char pagePrefix_[2][96]{};
 char namespaceName_[9]{},directory_[32]{};risc_storage_async_result_v1 result_{};
 alignas(R) alignas(L) uint8_t childBytes_[sizeof(R)>sizeof(L)?sizeof(R):sizeof(L)]{};
 R*reader(){return reinterpret_cast<R*>(childBytes_);}L*legacy(){return reinterpret_cast<L*>(childBytes_);}
 static bool preparationWorker(void*p){auto&self=*static_cast<CheckedSdSettingsCatalog*>(p);return self.guard_.worker&&self.guard_.worker(self.guard_.context);}
 static int prepareCallback(void*p){auto&self=*static_cast<CheckedSdSettingsCatalog*>(p);self.nativeCapacity_=self.core_.imageCapacity();self.nativeHeader_=self.core_.headerSize()?self.core_.headerSize():C::Header;
  if(self.synchronize_&&(!self.core_.ready()||!self.core_.identity().known))return C::Uncertain;
  return self.start(self.io_.attempt()->binding().volume,self.limits_);
 }
 int checkCustody(bool checkLease=true){
  if(io_.native())return io_.custody();
  const int observed=api_->base.fs.state.observe(volume_->context);if(observed==RISC_STORAGE_STATE_RETAINED)return C::Retained;
  if(!checkLease)return C::Ok;
  if(!guard_.held(guard_.context))return C::Conflict;
  return observed==RISC_STORAGE_STATE_READY?C::Ok:C::Io;
 }
 bool retainedCustody(){return io_.native()?io_.custody()==C::Retained:api_->base.fs.state.observe(volume_->context)==RISC_STORAGE_STATE_RETAINED;}
 static bool wait(int rc){return rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY;}
 static const char*name(unsigned n){return n==0?"replica.index":n==1?"settings.index":"settings.kv";}
 static bool foldEqual(const char*a,const char*b){for(;*a&&*b;++a,++b){unsigned x=uint8_t(*a),y=uint8_t(*b);if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return false;}return !*a&&!*b;}
 Entry*list(unsigned scan){return entries_+scan*limits_.entries;}const Entry*list(unsigned scan)const{return entries_+scan*limits_.entries;}
 void releaseEntries(){if(entries_){C::wipe(entries_,entryBytes_);alloc_.release(alloc_.context,entries_);entries_=nullptr;entryBytes_=0;}}
 void destroyChild(){if(child_==1)reader()->~R();else if(child_==2)legacy()->~L();child_=0;C::wipe(childBytes_,sizeof childBytes_);}
 void cancelChild(){if(child_==1)reader()->cancel();else if(child_==2)legacy()->cancel();}
 void wipeScratch(){C::wipe(&result_,sizeof result_);C::wipe(pagePrefix_,sizeof pagePrefix_);C::wipe(historyCursor_,sizeof historyCursor_);C::wipe(historySeen_,sizeof historySeen_);C::wipe(historySum_,sizeof historySum_);C::wipe(historyXor_,sizeof historyXor_);for(auto&s:scans_){C::wipe(s.historyDigest,sizeof s.historyDigest);C::wipe(s.historySum,sizeof s.historySum);C::wipe(s.historyXor,sizeof s.historyXor);}}
 void retain(){state_=State::Retained;reason_=Reason::Retained;error_=C::Retained;wipeScratch();}
 void fail(int rc,Reason reason){error_=rc;reason_=reason;if(child_)cancelChild();else phase_=EndScan;}
 void finish(State state,int rc){if(io_.pinned()){const int released=io_.release();if(released==C::Retained){retain();return;}if(released){state=State::Failed;rc=released;}}destroyChild();releaseEntries();report_.reset();restoreCandidate_.reset();wipeScratch();state_=state;error_=rc;}
 void submit(){risc_storage_async_request_v1 q{};q.operation=phase_==RefreshScan?RISC_STORAGE_ASYNC_REFRESH:phase_==OpenScan?RISC_STORAGE_ASYNC_OPEN_DIRECTORY:RISC_STORAGE_ASYNC_NEXT_ENTRY;const unsigned level=scans_[scan_].level;std::strcpy(q.path,level==0?directory_:level==1?"/RTECFG":"/");const int rc=io_.native()?io_.submit(q,&operation_):api_->base.submit(volume_->context,session_,&q,&operation_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}if(rc){scans_[scan_].status=Directory::Unreadable;phase_=EndScan;}}
 // Only the next fixed lexicographic window is retained. Every history name is
 // revisited until emitted, so an exact duplicate is rejected in the sweep in
 // which that name belongs to the window, even if earlier sweeps evicted it.
 Entry*historyWindow(){return entries_+2*limits_.entries;}
 bool descriptorDigest(const Entry&e,uint8_t*out){
  static const char domain[]="RTE SD history descriptor v1";uint8_t size[8];for(unsigned i=0;i<8;++i)size[i]=uint8_t(e.size>>(8*i));
  return hash_.begin(hash_.context)&&hash_.update(hash_.context,domain,sizeof domain)&&hash_.update(hash_.context,e.name,93)&&hash_.update(hash_.context,size,sizeof size)&&hash_.finish(hash_.context,out);
 }
 void admitHistory(const Entry&e){
  uint8_t digest[32]{};if(!descriptorDigest(e,digest)){fail(C::Io,Reason::ReadFailure);return;}
  unsigned carry=0;for(unsigned i=0;i<32;++i){historyXor_[i]^=digest[i];carry+=unsigned(historySum_[i])+digest[i];historySum_[i]=uint8_t(carry);carry>>=8;}C::wipe(digest,sizeof digest);++historyPassCount_;
  if(std::strcmp(e.name,historyCursor_)<=0)return;
  Entry*window=historyWindow();uint32_t at=0;while(at<historyCount_&&std::strcmp(window[at].name,e.name)<0)++at;
  if(at<historyCount_&&!std::strcmp(window[at].name,e.name)){scans_[scan_].status=Directory::Invalid;phase_=EndScan;return;}
  if(historyCount_==HistoryWindow){historyMore_=true;if(at==HistoryWindow)return;}else ++historyCount_;
  for(uint32_t i=historyCount_-1;i>at;--i)window[i]=window[i-1];
  window[at]=e;
 }
 void admitEntry(){
  auto&s=scans_[scan_];const auto&e=result_.entry;const char*end=static_cast<const char*>(std::memchr(e.name,0,sizeof e.name));
  if(!end||end==e.name||e.is_directory>1||std::strchr(e.name,'/')||std::strchr(e.name,'\\')){s.status=Directory::Invalid;phase_=EndScan;return;}
  if(restore_){if(restoreWork_>=limits_.restoreWork){s.status=Directory::Incomplete;phase_=EndScan;return;}++restoreWork_;}
  const uint32_t n=uint32_t(end-e.name)+1;Entry entry{};std::memcpy(entry.name,e.name,n);entry.size=e.size;entry.directory=e.is_directory;
  if(!s.level&&historicalPage(entry)){admitHistory(entry);return;}
  if(++s.work>limits_.entries||n>limits_.nameBytes-s.names){s.status=Directory::Incomplete;phase_=EndScan;return;}s.names+=n;
  // Exact dots are traversal metadata, never children; count them as scan work.
  if(!std::strcmp(e.name,".")||!std::strcmp(e.name,"..")){if(!e.is_directory){s.status=Directory::Invalid;phase_=EndScan;}return;}
  if(restore_&&historySweep_&&!s.level){
   uint32_t i=0;while(i<s.count&&std::strcmp(list(scan_)[i].name,e.name))++i;
   if(i==s.count||!same(list(scan_)[i],entry)||(historySeen_[i/8]&(1u<<(i%8)))){s.status=Directory::Invalid;phase_=EndScan;return;}historySeen_[i/8]|=uint8_t(1u<<(i%8));return;
  }
  for(uint32_t i=0;i<s.count;++i)if(foldEqual(list(scan_)[i].name,e.name)){s.status=Directory::Invalid;phase_=EndScan;return;}
  list(scan_)[s.count++]=entry;
  if(s.level&&foldEqual(e.name,s.level==1?namespaceName_:"RTECFG")){s.status=e.is_directory?Directory::Unreadable:Directory::Invalid;phase_=EndScan;}
 }
 // Each traversal must have the same complete history multiset commitment and
 // exactly the retained ordinary entries. XOR + 256-bit modular sum + count are
 // order-independent commitments, not duplicate proofs; exact duplicate checks
 // above and emitted-count equality below are still mandatory. Custody remains
 // held throughout every sweep, checked close, reader and owner handoff.
 bool completeHistorySweep(){
  auto&s=scans_[scan_];bool stable=true;
  if(!historySweep_){s.history=historyPassCount_;std::memcpy(s.historySum,historySum_,32);std::memcpy(s.historyXor,historyXor_,32);}
  else {stable=s.history==historyPassCount_&&!std::memcmp(s.historySum,historySum_,32)&&!std::memcmp(s.historyXor,historyXor_,32);for(uint32_t i=0;i<s.count;++i)if(!(historySeen_[i/8]&(1u<<(i%8))))stable=false;}
  if(!stable){s.status=Directory::Invalid;historyDone_=true;phase_=EndScan;return false;}return true;
 }
 void resetHistorySweep(){
  historyCount_=historyAt_=historyPassCount_=0;historyMore_=false;C::wipe(historyWindow(),HistoryWindow*sizeof(Entry));C::wipe(historySeen_,sizeof historySeen_);C::wipe(historySum_,sizeof historySum_);C::wipe(historyXor_,sizeof historyXor_);scans_[scan_].work=scans_[scan_].names=0;
 }
 void resetHistoryScan(){historySweep_=historyEmitted_=0;historyDone_=false;C::wipe(historyCursor_,sizeof historyCursor_);resetHistorySweep();}
 void summarizeHistory(){
  auto&s=scans_[scan_];
  for(unsigned work=0;work<8&&historyAt_<historyCount_;++work){
   const Entry&e=historyWindow()[historyAt_++];uint8_t digest[32]{};if(!descriptorDigest(e,digest)){fail(C::Io,Reason::ReadFailure);return;}
   static const char domain[]="RTE SD sorted history v1";uint8_t count[4];const uint32_t emitted=++historyEmitted_;for(unsigned i=0;i<4;++i)count[i]=uint8_t(emitted>>(8*i));
   const bool ok=hash_.begin(hash_.context)&&hash_.update(hash_.context,domain,sizeof domain)&&hash_.update(hash_.context,s.historyDigest,32)&&hash_.update(hash_.context,count,sizeof count)&&hash_.update(hash_.context,digest,32)&&hash_.finish(hash_.context,s.historyDigest);C::wipe(digest,sizeof digest);if(!ok){fail(C::Io,Reason::ReadFailure);return;}
  }
  if(historyAt_<historyCount_)return;
  if(historyMore_){if(!historyCount_){s.status=Directory::Invalid;historyDone_=true;phase_=EndScan;return;}std::memcpy(historyCursor_,historyWindow()[historyCount_-1].name,sizeof historyCursor_);++historySweep_;resetHistorySweep();phase_=BeginScan;return;}
  if(historyEmitted_!=s.history)s.status=Directory::Invalid;
  historyDone_=true;phase_=EndScan;
 }
 void startChild(){
  if(!io_.native()&&!guard_.worker(guard_.context))return;
  if(source_==0){if(io_.native())new(childBytes_)R(core_,namespace_,nativeCapacity_,nativeHeader_,hash_,guard_,alloc_);else new(childBytes_)R(core_,hash_,guard_,alloc_);child_=1;reader()->verificationOnly_=synchronize_;}
  else{if(io_.native())new(childBytes_)L(core_,namespace_,nativeCapacity_,nativeHeader_,hash_,guard_,alloc_);else new(childBytes_)L(core_,hash_,guard_,alloc_);child_=2;}
  const auto source=source_==1?L::Source::PagedV2:L::Source::SingleFile;
  const int rc=io_.native()?(child_==1?reader()->startPreparedNative(*io_.attempt(),io_,!restore_,restore_):legacy()->startPreparedNative(*io_.attempt(),source,io_,!restore_)):(child_==1?reader()->start(volume_):legacy()->start(volume_,source));
  if(rc==C::Retained){retain();return;}
  if(rc==C::Busy){destroyChild();return;}
  if(rc){destroyChild();fail(rc,rc==C::NoMemory?Reason::Allocation:Reason::ReadFailure);return;}phase_=Child;
 }
 void stepChild(){
  bool running=false,retained=false,verified=false,safe=false;
  if(child_==1){reader()->step();running=reader()->state()==R::State::Running;retained=reader()->state()==R::State::Retained;verified=reader()->state()==R::State::Verified;safe=reader()->exitSafe();}
  else{legacy()->step();running=legacy()->state()==L::State::Running;retained=legacy()->state()==L::State::Retained;verified=legacy()->state()==L::State::Verified;safe=legacy()->exitSafe();}
  if(retained){retain();return;}if(running)return;
  if(retainedCustody()){retain();return;}
  if(io_.native()){const int custody=checkCustody();if(custody==C::Busy)return;if(custody){if(custody==C::Retained){retain();return;}error_=custody;reason_=custody==C::Conflict?Reason::CustodyLost:Reason::MediaUnavailable;}}
  if(cancelled_||error_){if(verified){cancelChild();safe=child_==1?reader()->exitSafe():legacy()->exitSafe();}if(!safe){retain();return;}finish(cancelled_?State::Cancelled:State::Failed,error_);return;}
  auto&slot=report_.slots_[source_];
  if(verified){
   int rc;if(child_==1){R::Candidate candidate;rc=reader()->take(&candidate);if(!rc){slot.content=Content::Valid;slot.identity=candidate.identity();slot.generation=slot.identity.generation;slot.count=candidate.count();slot.mapBytes=candidate.mapSize();slot.sourceBytes=slot.mapBytes;if(restore_){if(restorePreflight_)candidate.transferTo(restoreCandidate_);else if(!sameRestoreCandidate(candidate)){fail(C::Conflict,Reason::ReadFailure);}}}}
   else{L::Candidate candidate;rc=legacy()->take(&candidate);if(!rc){slot.content=Content::Valid;slot.format=candidate.format();slot.generation=candidate.generation();slot.count=candidate.count();slot.mapBytes=candidate.mapSize();slot.sourceBytes=candidate.sourceSize();std::memcpy(slot.sourceDigest,candidate.sourceDigest(),32);}}
   if(rc==C::Retained){retain();return;}if(rc){destroyChild();fail(rc,rc==C::Conflict?Reason::CustodyLost:Reason::MediaUnavailable);return;}
  }else if(child_==1){const auto why=reader()->reason();slot.content=why==R::Reason::CheckpointMissing?Content::Missing:why==R::Reason::InvalidContent?Content::Invalid:why==R::Reason::Capacity?Content::CapacityExceeded:Content::Unreadable;}
  else {const auto why=legacy()->reason();slot.content=why==L::Reason::SourceMissing?Content::Missing:(why==L::Reason::InvalidContent||why==L::Reason::SourceChanged)?Content::Invalid:why==L::Reason::Capacity?Content::CapacityExceeded:Content::Unreadable;}
  destroyChild();if(error_){phase_=EndScan;return;}if(++source_<3)phase_=StartChild;else{
   source_=0;if(restorePreflight_){restorePreflight_=false;scan_=0;}else scan_=1;
   if(restore_)resetHistoryScan();
   phase_=BeginScan;
  }
 }
 bool sameRestoreCandidate(const R::Candidate&candidate)const{
  const auto&a=restoreCandidate_;const auto&b=candidate;
  return a.valid_&&b.valid_&&a.sourceBackup_==b.sourceBackup_&&a.namespace_==b.namespace_&&a.count_==b.count_&&a.size_==b.size_&&a.identity_.known==b.identity_.known&&a.identity_.generation==b.identity_.generation&&!std::memcmp(a.identity_.lineage,b.identity_.lineage,16)&&!std::memcmp(a.identity_.digest,b.identity_.digest,32)&&(!a.size_||!std::memcmp(a.bytes_,b.bytes_,a.size_));
 }
 static bool complete(Directory d){return d==Directory::Absent||d==Directory::Empty||d==Directory::Populated;}
 const Entry*find(unsigned scan,const char*name)const{if(scans_[scan].level)return nullptr;for(uint32_t i=0;i<scans_[scan].count;++i)if(!std::strcmp(list(scan)[i].name,name))return list(scan)+i;return nullptr;}
 static bool same(const Entry&a,const Entry&b){return !std::strcmp(a.name,b.name)&&a.size==b.size&&a.directory==b.directory;}
 static bool sameObject(const Entry&a,const Entry&b){return a.size==b.size&&a.directory==b.directory&&!a.directory;}
 static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 static uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
 static uint32_t crc(const uint8_t*p,uint32_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
 static bool sameIdentity(const C::Identity&a,const C::Identity&b){return a.known&&b.known&&a.generation==b.generation&&!std::memcmp(a.lineage,b.lineage,16)&&!std::memcmp(a.digest,b.digest,32);}
 // Compare every available byte of a partial write with the fields fixed by
 // the writer protocol. A complete staging index also needs framing/CRC, but
 // never becomes a selected root or supplies restored map bytes.
 bool artifactIndex(const uint8_t*p,uint32_t n,const Entry&entry)const{
  constexpr uint32_t size=CheckedSdReplicaCheckpoint::IndexBytes;
  const bool backup=!std::strcmp(entry.name,"replica.index.risc-replace");
  if(n>size||(backup&&n!=size)||!restoreCandidate_.valid_)return false;
  const auto&id=restoreCandidate_.identity_;auto other=id;bool named=false;
  if(backup){if(!id.generation)return false;--other.generation;}
  else if(std::strcmp(entry.name,".replica.next")){
   if(!SdReplicaStage::parseName(entry.name,&other)||std::memcmp(other.lineage,id.lineage,16))return false;
   named=true;
  }
  uint8_t expected[size];CheckedSdReplicaCheckpoint::makeIndex(expected,namespace_,0,0,other);
  // Legacy staging has no target-bound name. Once preserved across a newer
  // publication its generation can be older/current/newer; it never selects a
  // map. Its namespace, available lineage, grammar and CRC remain mandatory.
  bool valid=true;for(uint32_t i=0;i<n;++i){const bool fixed=i<12||(i>=20&&i<24)||(i>=32&&i<48)||((backup||named)&&i>=24&&i<32)||(named&&i>=48&&i<80);if(fixed&&p[i]!=expected[i])valid=false;}
  C::wipe(expected,sizeof expected);
  if(n>=16&&word(p+12)>CheckedSdReplicaCheckpoint::MapMax)valid=false;
  if(n>=20&&(word(p+16)>word(p+12)/21||(!word(p+16)&&word(p+12))))valid=false;
  if(n==size&&word(p+80)!=crc(p,80))valid=false;
  return valid;
 }
 static bool stagedName(const char*n){C::Identity id{};return !std::strcmp(n,".replica.next")||SdReplicaStage::parseName(n,&id);}
 // Artifact reads share the catalog's exact copied session and lease. No
 // recover/delete/replace request is issued, even after malformed evidence.
 // Staging descriptors retain the fixed ordinary-entry bound and consume one
 // copied small-file read each; page history keeps its separate sorted window.
 void stepArtifacts(){
  if(phase_==EndArtifacts){
   if(session_){const int rc=io_.end();if(wait(rc))return;if(rc!=RISC_STORAGE_ASYNC_OK){if(rc==NativeSdAttempt::Lost){session_=operation_=0;fail(C::Conflict,Reason::CustodyLost);}else retain();return;}session_=operation_=0;}
   if(cancelled_||error_){phase_=EndScan;return;}artifactsDone_=true;prepareReport();return;
  }
  if(phase_==BeginArtifacts){
   if(!complete(scans_[0].status)||!complete(scans_[1].status)||scans_[0].level||scans_[1].level||!restoreCandidate_.valid_){artifactValid_=false;phase_=EndArtifacts;return;}
   if(restoreCandidate_.sourceBackup_&&(find(0,"replica.index")||find(1,"replica.index"))){artifactValid_=false;phase_=EndArtifacts;return;}
   for(unsigned work=0;work<8&&artifact_<scans_[1].count;++work){const Entry&e=list(1)[artifact_];if(stagedName(e.name)||(!restoreCandidate_.sourceBackup_&&!std::strcmp(e.name,"replica.index.risc-replace")))break;++artifact_;}
   if(artifact_==scans_[1].count){phase_=EndArtifacts;return;}
   const Entry&e=list(1)[artifact_];if(!stagedName(e.name)&&(restoreCandidate_.sourceBackup_||std::strcmp(e.name,"replica.index.risc-replace")))return;
   if(!session_){const int rc=io_.begin(&session_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}if(rc){fail(C::Io,Reason::ReadFailure);return;}}
   phase_=ReadArtifact;return;
  }
  const Entry&e=list(1)[artifact_];
  if(!operation_){risc_storage_async_request_v1 q{};q.operation=RISC_STORAGE_ASYNC_READ_SMALL_FILE;std::snprintf(q.path,sizeof q.path,"%s/%s",directory_,e.name);const int rc=io_.submit(q,&operation_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}if(rc)fail(C::Io,Reason::ReadFailure);return;}
  result_.status=INT32_MIN;const int rc=io_.collect(&result_);if(rc==RISC_STORAGE_ASYNC_PENDING||(rc==RISC_STORAGE_ASYNC_BUSY&&result_.status!=RISC_STORAGE_ASYNC_BUSY))return;operation_=0;
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return;}
  const bool stage=stagedName(e.name);
  // INVALID alone also means oversized; stable directory size zero is required.
  const bool emptyStage=stage&&!e.directory&&!e.size&&rc==RISC_STORAGE_ASYNC_INVALID&&!result_.size;
  if((rc&&!emptyStage)||e.directory||e.size!=result_.size||!artifactIndex(result_.data,result_.size,e))artifactValid_=false;
  else if(stage)++allowedStage_;else ++allowedBackup_;
  C::wipe(&result_,sizeof result_);++artifact_;phase_=BeginArtifacts;
 }
 uint8_t backupMask(unsigned scan)const{uint8_t mask=0;for(unsigned n=0;n<3;++n){char b[48];std::snprintf(b,sizeof b,"%s.risc-replace",name(n));if(find(scan,b))mask|=uint8_t(1u<<n);}return mask;}
 bool recovered(unsigned n)const{if(!complete(scans_[0].status)||!complete(scans_[1].status)||scans_[0].level||scans_[1].level)return false;char b[48];std::snprintf(b,sizeof b,"%s.risc-replace",name(n));const Entry*old=find(0,b),*now=find(1,name(n));return old&&now&&!find(0,name(n))&&!find(1,b)&&sameObject(*old,*now);}
 bool selectedPage(const Entry&e)const{
  if(e.directory)return false;
  for(unsigned source=0;source<2;++source){const Slot&s=report_.slots_[source];if(s.content!=Content::Valid)continue;const uint32_t bytes=source?s.sourceBytes:s.mapBytes;const size_t prefix=std::strlen(pagePrefix_[source]);if(std::strlen(e.name)!=prefix+8||std::memcmp(e.name,pagePrefix_[source],prefix))continue;
   uint32_t page=0;bool valid=true;for(unsigned i=0;i<8;++i){const unsigned c=uint8_t(e.name[prefix+i]);if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))){valid=false;break;}page=page*16+(c<='9'?c-'0':c-'a'+10);}const uint64_t at=uint64_t(page)*4096;
   if(valid&&at<bytes){const uint32_t left=bytes-uint32_t(at);if(e.size==(left>4096?4096:left))return true;}
  }return false;
 }
 // RTENSD1 has one committed index. Unselected canonical page names have no
 // independent selection authority, at any generation. A cut can leave empty
 // or partial pages; preserve them without inferring lineage/content validity.
 // This read-only classification NEVER grants overwrite or deletion authority.
 bool historicalPage(const Entry&e)const{
  if(!restore_||e.directory||report_.slots_[0].content!=Content::Valid||std::strlen(e.name)!=93||std::memcmp(e.name,"nr-",3)||e.name[19]!='-'||e.name[84]!='-'||e.size>4096)return false;
  uint64_t generation=0;uint32_t page=0;
  for(unsigned i=3;i<93;++i){if(i==19||i==84)continue;const unsigned c=uint8_t(e.name[i]);if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;const unsigned digit=c<='9'?c-'0':c-'a'+10;if(i<19)generation=(generation<<4)|digit;else if(i>84)page=(page<<4)|digit;}
  if(uint64_t(page)*4096+e.size>CheckedSdReplicaCheckpoint::MapMax)return false;
  const auto&selected=report_.slots_[0].identity;if(generation!=selected.generation)return true;
  const char*hex="0123456789abcdef";for(unsigned i=0;i<32;++i)if(e.name[20+2*i]!=hex[selected.digest[i]>>4]||e.name[21+2*i]!=hex[selected.digest[i]&15])return true;
  // An impossible page index/size in the selected set is not an ignored orphan.
  return false;
 }
 void prepareReport(){
  report_.before_=scans_[0].status;report_.after_=scans_[1].status;report_.initialEntries_=scans_[0].level?0:scans_[0].count+scans_[0].history;report_.finalEntries_=scans_[1].level?0:scans_[1].count+scans_[1].history;report_.initialBackups_=backupMask(0);report_.finalBackups_=backupMask(1);
  report_.stable_=complete(scans_[0].status)&&complete(scans_[1].status);
  if(restore_){restoreHistorical_=scans_[1].history;report_.artifacts_.orphanPages=restoreHistorical_;if(scans_[0].history!=scans_[1].history||std::memcmp(scans_[0].historyDigest,scans_[1].historyDigest,32))report_.stable_=false;}
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
    const auto&e=list(1)[summaryAt_++];bool source=restore_&&restoreCandidate_.sourceBackup_&&!std::strcmp(e.name,"replica.index.risc-replace");for(unsigned n=0;n<3;++n)if(!std::strcmp(e.name,name(n)))source=true;if(source&&!e.directory)continue;
    auto&a=report_.artifacts_;if(e.directory){++a.directories;continue;}if(selectedPage(e)){++a.selectedPages;continue;}const size_t n=std::strlen(e.name);if(n>=13&&!std::strcmp(e.name+n-13,".risc-replace")){++a.backups;continue;}
    if(!std::strncmp(e.name,"nr-",3)||!std::strncmp(e.name,"kv-",3)){++a.orphanPages;if(historicalPage(e))++restoreHistorical_;}else if(stagedName(e.name)||!std::strcmp(e.name,".settings.index.next")||!std::strcmp(e.name,".settings.next"))++a.staging;else ++a.unknown;
   }
  }
 }
};
}
