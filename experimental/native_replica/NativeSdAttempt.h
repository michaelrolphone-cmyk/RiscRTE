#pragma once
#include <RiscStoragePackageAsyncV1.h>
#include <RiscStorageVolumeAsyncV1.h>
#include <RiscStorageWorkerV1.h>
#include <cstdint>
#include <cstring>
namespace RiscExperimental {
// Disconnected native authority model. No application/SDK entry point or Port
// binding. Hooks model native pins, task identity and lifecycle gates; integration
// must implement them before this can protect a physical provider.
class NativeSdAttempt final {
 public:
 enum class State {Idle,Admission,OwnedSession,Closing,ReadyClosed,Unavailable,Cancelled,Failed,Released,Retained};
 enum class Call {None,Begin,Submit,Package,Collect,End};
 enum class Scope {Healthy,Unavailable,Retained};
 static constexpr int Context=-30,Lost=-31,Unsupported=-32;
 struct Binding {uint32_t namespaceId;uint64_t device,grant,scope;const risc_storage_volume_api_v1*volume;};
 struct Hooks {
  void* context;
  bool(*owner)(void*); // exact owner task, excluding ISR
  bool(*freshExclusive)(void*,const Binding&); // native construction proof, never READY
  bool(*idle)(void*); // includes all selected native jobs
  bool(*held)(void*,const Binding&);
  void(*fence)(void*,bool);
  Scope(*snapshot)(void*,uint64_t*); // only after actual completion fence
 };
 explicit NativeSdAttempt(Binding b,Hooks h):binding_(b),hooks_(h){}
 NativeSdAttempt(const NativeSdAttempt&)=delete;NativeSdAttempt&operator=(const NativeSdAttempt&)=delete;
 State state()const{return state_;} Call dispatch()const{return call_;}
 bool fenced()const{return fenced_;} bool exitSafe()const{return !fenced_&&(state_==State::Idle||state_==State::Released);}
 bool hasSession()const{return session_!=0;} uint64_t epoch()const{return epoch_;}
 uint64_t closedSequence()const{return closed_;} const Binding&binding()const{return binding_;}
 bool receiptPinned()const{return nativePin_;}
 int acquire(const risc_storage_volume_api_v1*volume){
  if(!owner()||state_!=State::Idle||call_!=Call::None)return Context;
  if(!binding_.namespaceId||!binding_.device||!binding_.grant||!binding_.scope||volume!=binding_.volume||!hooks_.freshExclusive||!hooks_.idle||!hooks_.held||!hooks_.fence||!hooks_.snapshot)return Unsupported;
  api_=risc_storage_volume_packages(volume);if(!api_||!risc_storage_volume_download(volume))return Unsupported;
  volume_=volume;
  const int observed=observe();if(observed==RISC_STORAGE_STATE_RETAINED){retain();return RISC_STORAGE_ASYNC_RETAINED;}
  if(!hooks_.freshExclusive(hooks_.context,binding_))return Unsupported;
  if(!hooks_.idle(hooks_.context)||!held())return RISC_STORAGE_ASYNC_BUSY;
  fenced_=true;hooks_.fence(hooks_.context,true);
  const int rc=begin();
  if(rc==RISC_STORAGE_ASYNC_BUSY){unfence();return rc;}
  if(rc!=RISC_STORAGE_ASYNC_OK){retain();return RISC_STORAGE_ASYNC_RETAINED;}
  state_=State::Admission;return 0;
 }
 // The fresh-only profile always runs one real REFRESH during admission. Public
 // observe may truthfully report UNAVAILABLE throughout that owned session.
 int refresh(){
  if(!owner()||call_!=Call::None||state_!=State::Admission||refreshSent_)return Context;
  risc_storage_async_request_v1 q{};q.operation=RISC_STORAGE_ASYNC_REFRESH;
  const int rc=submitImpl(q);if(!rc)refreshSent_=true;return rc;
 }
 int openChild(){
  if(!owner()||call_!=Call::None||state_!=State::ReadyClosed||nativePin_||!clientAccess()||!boundary())return Context;
  const int rc=begin();if(!rc){state_=State::OwnedSession;childOwner_=dispatchOwner_;}else if(rc!=RISC_STORAGE_ASYNC_BUSY)retain();return rc;
 }
 int submit(const risc_storage_async_request_v1&q){
  if(!owner()||call_!=Call::None||state_!=State::OwnedSession||nativePin_||!sessionAccess()||q.operation==RISC_STORAGE_ASYNC_REFRESH)return Context;
  if(q.operation!=RISC_STORAGE_ASYNC_NEXT_ENTRY&&q.operation!=RISC_STORAGE_ASYNC_CLOSE_DIRECTORY&&
     !path(q.path,q.operation==RISC_STORAGE_ASYNC_OPEN_DIRECTORY))return Context;
  if(q.operation==RISC_STORAGE_ASYNC_RENAME&&!path(q.destination,false))return Context;
  return submitImpl(q);
 }
 int package(const risc_storage_package_request_v1&q){return packageImpl(q,false);}
 private:
 // Only the typed first-checkpoint dispatch may create its exact parent.
 // Raw package callers retain the existing namespace-only path restriction.
 int packageImpl(const risc_storage_package_request_v1&q,bool firstParent){
  if(!owner()||call_!=Call::None||state_!=State::OwnedSession||operation_||nativePin_||!sessionAccess()||!live())return Context;
  if(q.operation==RISC_STORAGE_PACKAGE_VALIDATE||q.operation==RISC_STORAGE_PACKAGE_VALIDATE_DRIVER)return Context;
  const bool parent=firstParent&&dispatchOwner_&&q.operation==RISC_STORAGE_PACKAGE_MKDIR&&!std::strcmp(q.path,"/RTECFG");
  if(q.operation!=RISC_STORAGE_PACKAGE_WRITE_CHUNK&&q.operation!=RISC_STORAGE_PACKAGE_CLOSE_WRITE&&!parent&&!path(q.path,false))return Context;
  if(q.operation==RISC_STORAGE_PACKAGE_REPLACE&&!path(q.destination,false))return Context;
  if(!arm(Call::Package,&q,sizeof q))return Context;
  uint64_t token=0;const int rc=api_->submit_package(volume_->context,session_,&q,&token);
  if(!disarm())return RISC_STORAGE_ASYNC_RETAINED;
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return rc;}
  if(rc==RISC_STORAGE_ASYNC_IO){failed_=true;state_=State::Closing;}
  if(!rc){if(!token||token!=minted_){retain();return RISC_STORAGE_ASYNC_RETAINED;}operation_=token;}
  return rc;
 }
 public:
 int collect(risc_storage_async_result_v1*out){
  if(!owner()||call_!=Call::None||!out||!sessionAccess()||!session_||!operation_||state_==State::Closing||state_==State::Retained)return Context;
  if(!arm(Call::Collect))return Context;
  out->status=INT32_MIN;const int rc=api_->base.collect(volume_->context,session_,operation_,out);
  if(!disarm())return RISC_STORAGE_ASYNC_RETAINED;
  if(rc==RISC_STORAGE_ASYNC_PENDING){if(out->status!=INT32_MIN){retain();return RISC_STORAGE_ASYNC_RETAINED;}return rc;}
  // A copied BUSY is terminal; it consumed this operation token.
  if(rc==RISC_STORAGE_ASYNC_BUSY&&out->status!=RISC_STORAGE_ASYNC_BUSY){if(out->status!=INT32_MIN){retain();return RISC_STORAGE_ASYNC_RETAINED;}return rc;}
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return rc;}
  if(out->status!=rc||rc<RISC_STORAGE_ASYNC_RETAINED||rc>RISC_STORAGE_ASYNC_NOT_FOUND||out->size>sizeof(out->data)){retain();return RISC_STORAGE_ASYNC_RETAINED;}
  operation_=0;
  if(rc==RISC_STORAGE_ASYNC_IO){failed_=true;state_=State::Closing;}
  if(state_==State::Admission){refreshOk_=rc==RISC_STORAGE_ASYNC_OK;state_=State::Closing;if(!refreshOk_)failed_=true;}
  if(!held()){failed_=true;state_=State::Closing;}
  return rc;
 }
 void cancel(){
  if(!owner()||call_!=Call::None||state_==State::Retained||state_==State::Released||state_==State::Idle||nativePin_||!sessionAccess())return;
  cancelled_=true;if(session_)state_=State::Closing;else {if(boundary())state_=State::Cancelled;}
 }
 // End OK never directly permits release: observe and a fenced native snapshot
 // have precedence over cancellation, operation IO and simultaneous lease loss.
 int close(){
  if(!owner()||call_!=Call::None||!session_||state_==State::Retained||nativePin_||!sessionAccess())return Context;
  state_=State::Closing;
  if(!arm(Call::End))return Context;
  const int rc=api_->base.end(volume_->context,session_);
  if(!disarm())return RISC_STORAGE_ASYNC_RETAINED;
  if(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY)return rc;
  if(rc!=RISC_STORAGE_ASYNC_OK){retain();return RISC_STORAGE_ASYNC_RETAINED;}
  session_=operation_=0;cleanup_=false;childOwner_=nullptr;
  if(!boundary(true))return state_==State::Retained?RISC_STORAGE_ASYNC_RETAINED:Lost;
  if(closed_==UINT64_MAX){retain();return RISC_STORAGE_ASYNC_RETAINED;}++closed_;
  if(cancelled_)state_=State::Cancelled;
  else if(failed_)state_=State::Failed;
  else if(!refreshOk_)state_=State::Unavailable;
  else state_=State::ReadyClosed;
  return 0;
 }
 int release(){
  if(!owner()||call_!=Call::None||session_||nativePin_||clientPins_||state_==State::Retained||!fenced_)return Context;
  if(!boundary(false,false))return state_==State::Retained?RISC_STORAGE_ASYNC_RETAINED:Lost;
  unfence();state_=State::Released;return 0;
 }
 // Called only by the selected native Sync seam. It is consumptive, device-bound
 // and cannot remain true through nested calls or a pending operation.
 bool syncAdmission(uint64_t device){
  if(!owner()||device!=binding_.device)return false;
  if(!fenced_)return true;
  if(call_==Call::None||!credit_)return false;
  credit_=false;return true;
 }
 // Private native mailbox gate model: exact call kind, pointer/size and token.
 bool rawSubmit(uint64_t scope,const void*p,uint32_t n){
  if(scope!=binding_.scope||!rawReady()||rawSubmitted_)return false;
  if(call_==Call::Submit||call_==Call::Package){if(p!=request_||n!=requestSize_)return false;}
  else if(call_==Call::End){
   if(operation_&&!rawFinished_)return false;
   if(n!=sizeof(risc_storage_async_request_v1)||!p)return false;
   const auto&q=*static_cast<const risc_storage_async_request_v1*>(p);
   if(q.operation)return false;
   for(char c:q.path)if(c)return false;
   for(char c:q.destination)if(c)return false;
  }else return false;
  rawSubmitted_=true;return true;
 }
 void submitted(uint64_t scope,int rc,uint64_t token){if(scope==binding_.scope&&rawReady()&&rawSubmitted_&&rc==RISC_STORAGE_WORK_OK){minted_=token;if(call_==Call::End){operation_=token;cleanup_=true;}}}
 bool rawCollect(uint64_t scope,uint64_t token){
  if(scope!=binding_.scope||!rawReady()||rawFinished_||!token||token!=operation_||(call_!=Call::Collect&&(call_!=Call::End||!cleanup_)))return false;
  rawFinished_=true;return true;
 }
 bool rawCancel(uint64_t scope,uint64_t token){
  if(scope!=binding_.scope||!rawReady()||rawFinished_||call_!=Call::End||cleanup_||!token||token!=operation_)return false;
  rawFinished_=true;return true;
 }
 private:
 friend class NativeSdCompletion;friend class NativeSdSession;friend class NativeSdPreparation;
 const void*childOwner_=nullptr;const void*dispatchOwner_=nullptr;uint32_t clientPins_=0;
 bool clientAccess()const{return !clientPins_||dispatchOwner_;}
 bool sessionAccess()const{return clientAccess()&&childOwner_==dispatchOwner_;}
 Binding binding_;Hooks hooks_;const risc_storage_volume_api_v1*volume_=nullptr;const risc_storage_volume_api_v1_packages*api_=nullptr;
 State state_=State::Idle;Call call_=Call::None;uint64_t session_=0,operation_=0,epoch_=0,closed_=0,minted_=0;
 const void*request_=nullptr;uint32_t requestSize_=0;
 bool fenced_=false,credit_=false,rawSubmitted_=false,rawFinished_=false,cleanup_=false;
 bool refreshSent_=false,refreshOk_=false,failed_=false,cancelled_=false,nativePin_=false;
 bool path(const char*p,bool parents)const{
  if(!p||!std::memchr(p,0,205))return false;
  if(parents&&(!std::strcmp(p,"/")||!std::strcmp(p,"/RTECFG")))return true;
  char prefix[17]="/RTECFG/00000000";const char*hex="0123456789ABCDEF";
  for(unsigned i=0;i<8;++i)prefix[8+i]=hex[(binding_.namespaceId>>(28-4*i))&15];
  if(std::strncmp(p,prefix,16)|| (p[16]&&p[16]!='/'))return false;
  if(!p[16])return true;
  const char*component=p+17;if(!*component)return false;
  for(const char*at=component;;++at){
   if(*at=='/'||!*at){const size_t n=size_t(at-component);if(!n||(n==1&&*component=='.')||(n==2&&component[0]=='.'&&component[1]=='.'))return false;if(!*at)return true;component=at+1;}
   else if(static_cast<unsigned char>(*at)<32||*at=='\\')return false;
  }
 }
 bool owner()const{return hooks_.owner&&hooks_.owner(hooks_.context);}
 bool held()const{return hooks_.held&&hooks_.held(hooks_.context,binding_);}
 int observe()const{return api_->base.fs.state.observe(volume_->context);}
 void retain(){state_=State::Retained;if(!fenced_&&hooks_.fence){fenced_=true;hooks_.fence(hooks_.context,true);}}
 void unfence(){hooks_.fence(hooks_.context,false);fenced_=false;}
 bool live(){if(!held()){failed_=true;state_=State::Closing;return false;}return state_!=State::Retained;}
 bool boundary(bool admission=false,bool requireReady=true){
  if(observe()==RISC_STORAGE_STATE_RETAINED){retain();return false;}
  if(!hooks_.idle(hooks_.context)){retain();return false;}
  uint64_t serial=0;const Scope s=hooks_.snapshot(hooks_.context,&serial);
  if(s==Scope::Retained||!serial){retain();return false;}
  const int observed=observe();if(observed==RISC_STORAGE_STATE_RETAINED){retain();return false;}
  if(!held()||(epoch_&&epoch_!=serial)){failed_=true;state_=State::Failed;return false;}
  if(!epoch_&&admission)epoch_=serial;
  if(requireReady&&(s!=Scope::Healthy||observed!=RISC_STORAGE_STATE_READY)){state_=State::Unavailable;return false;}
  return true;
 }
 bool arm(Call c,const void*p=nullptr,uint32_t n=0){
  if(!owner()||call_!=Call::None||!fenced_)return false;
  call_=c;credit_=true;rawSubmitted_=rawFinished_=false;request_=p;requestSize_=n;minted_=0;return true;
 }
 bool disarm(){const bool consumed=!credit_;call_=Call::None;credit_=false;request_=nullptr;requestSize_=0;if(!consumed)retain();return consumed;}
 bool rawReady()const{return owner()&&fenced_&&call_!=Call::None&&!credit_;}
 int begin(){
  if(!arm(Call::Begin))return Context;
  const int rc=api_->base.begin(volume_->context,&session_);
  if(!disarm())return RISC_STORAGE_ASYNC_RETAINED;
  if(!rc&&!session_){retain();return RISC_STORAGE_ASYNC_RETAINED;}return rc;
 }
 int submitImpl(const risc_storage_async_request_v1&q){
  if(operation_||!session_||!live()||!arm(Call::Submit,&q,sizeof q))return Context;
  uint64_t token=0;const int rc=api_->base.submit(volume_->context,session_,&q,&token);
  if(!disarm())return RISC_STORAGE_ASYNC_RETAINED;
  if(rc==RISC_STORAGE_ASYNC_RETAINED){retain();return rc;}
  if(rc==RISC_STORAGE_ASYNC_IO){failed_=true;state_=State::Closing;}
  if(!rc){if(!token||token!=minted_){retain();return RISC_STORAGE_ASYNC_RETAINED;}operation_=token;}return rc;
 }
};
}
