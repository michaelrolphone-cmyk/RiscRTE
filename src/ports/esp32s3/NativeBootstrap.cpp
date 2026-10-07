#include "NativeBootstrap.h"
#ifdef RISC_PAIRED_BANKS
#include "NativeBankStore.h"
#include "HttpBounds.h"
#include "runtime/provisioning/Coordinator.h"
#include "runtime/provisioning/StoreFiles.h"
#include "runtime/update/PairedBank.h"
#include "bootstrap/Json.h"
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <new>
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace RiscBootstrap {
namespace {
using RiscProvision::Step;
bool sha(const void* bytes,uint32_t size,uint8_t* digest){
 mbedtls_sha256_context context;mbedtls_sha256_init(&context);
 const bool ok=mbedtls_sha256_starts_ret(&context,0)==0 &&
   mbedtls_sha256_update_ret(&context,static_cast<const uint8_t*>(bytes),size)==0 && mbedtls_sha256_finish_ret(&context,digest)==0;
 mbedtls_sha256_free(&context);return ok;
}
struct Session {
 Port port;RiscProvision::Profile profile;
 uint8_t input[RiscProvision::ProfileInputBytes]{},digest[32]{},buffer[RISC_HTTP_CHUNK_MAX]{};
 char root[256]{};uint64_t bankToken=0,httpToken=0;
 uint32_t received=0;size_t fileIndex=0;
 bool radio=false,retained=false,finished=false,historyChecked=false,timeStarted=false,identityUnavailable=false;
 Reason reason=Reason::Unchanged;
 explicit Session(const Port& p):port(p){}
 ~Session(){volatile uint8_t* p=input;for(size_t i=0;i<sizeof(input);++i)p[i]=0;}
 bool safe(){return !retained && port.hardware.owner && port.hardware.owner() && port.operationSafe && port.operationSafe();}
 bool clock(uint64_t& utc,bool& pending){
   pending=false;TimeSample sample{};
   if(!port.time.poll){reason=Reason::ClockUnavailable;return false;}
   timeStarted=true;
   const auto status=port.time.poll(port.time.context,&sample);
   if(status==TimeStatus::Pending){reason=Reason::ClockUnavailable;pending=true;return false;}
   const uint64_t now=port.hardware.now();
   if(status!=TimeStatus::Ready||!sample.max_age_ms||sample.max_age_ms>300000u||sample.sampled_monotonic_ms>now||
      now-sample.sampled_monotonic_ms>=sample.max_age_ms||sample.utc_seconds<RiscCpu::HttpBounds::FirstUtc||sample.utc_seconds>RiscCpu::HttpBounds::LastUtc){reason=Reason::ClockUnavailable;return false;}
   const uint64_t elapsed=(now-sample.sampled_monotonic_ms)/1000u;
   if(elapsed>RiscCpu::HttpBounds::LastUtc-sample.utc_seconds){reason=Reason::ClockUnavailable;return false;}
   utc=sample.utc_seconds+elapsed;return true;
 }
 bool closeHttp(){
   if(!httpToken)return true;
   const auto* http=port.hardware.httpClient;
   if(http->close(http->context,httpToken)!=RISC_HTTP_OK){retained=true;reason=Reason::CleanupRetained;return false;}
   httpToken=0;return true;
 }
 bool closeTransport(){
   if(timeStarted){
     if(port.time.stop&&!port.time.stop(port.time.context)){retained=true;reason=Reason::CleanupRetained;return false;}
     timeStarted=false;
   }
   if(!closeHttp())return false;
   if(radio){
     if(!port.hardware.radioLeave()||!port.hardware.radioIdle()){retained=true;reason=Reason::CleanupRetained;return false;}
     radio=false;
   }
   return true;
 }
 bool matches(const uint8_t* expected){
   uint8_t identity[32];memcpy(identity,expected,32);
   const auto receipt=RiscBankStore::provisionIdentity(identity);
   if(receipt==RiscBankStore::ProvisionIdentity::Match)return true;
   if(receipt==RiscBankStore::ProvisionIdentity::Different)return false;
   if(receipt==RiscBankStore::ProvisionIdentity::Unavailable){identityUnavailable=true;reason=Reason::HistoryUnavailable;return false;}
   char path[256];if(!RiscBoot::path(root,RiscProvision::StoreFiles::DigestFile,path,sizeof(path)))return false;
   FILE* file=fopen(path,"rb");if(!file)return false;
   uint8_t actual[33];const size_t size=fread(actual,1,sizeof(actual),file);bool ok=size==32&&!ferror(file)&&!memcmp(actual,expected,32);
   if(fclose(file)!=0){retained=true;reason=Reason::CleanupRetained;ok=false;}return ok;
 }
 Step connect(){
   if(identityUnavailable){reason=Reason::HistoryUnavailable;return Step::Failed;}
   reason=Reason::NetworkFailed;auto& h=port.hardware;
   if(!historyChecked){const auto history=RiscBankStore::provisionHistory(digest);
     if(history!=RiscBankStore::ProvisionHistory::Clear){reason=history==RiscBankStore::ProvisionHistory::SameAttempt?Reason::AttemptHeld:Reason::HistoryUnavailable;return Step::Failed;}
     historyChecked=true;
   }
   if(!port.time.poll){reason=Reason::ClockUnavailable;return Step::Failed;}
   if(!radio){radio=true;if(!h.radioJoin(profile.ssid,profile.password)){reason=Reason::NetworkFailed;return Step::Failed;}return Step::Pending;}
   uint8_t state=0,station[12]{},ap[12]{};int8_t rssi=0;
   if(!h.radioState(&state,&rssi)||state==0){reason=Reason::NetworkFailed;return Step::Failed;}
   if(state!=2)return Step::Pending;
   if(!h.radioAddresses(station,ap)||!(station[0]||station[1]||station[2]||station[3])){reason=Reason::NetworkFailed;return Step::Failed;}
   uint64_t utc=0;bool pending=false;if(!clock(utc,pending))return pending?Step::Pending:Step::Failed;
   return Step::Done;
 }
 Step begin(){
   reason=Reason::StageFailed;
   if(!bankToken){
     if(RiscBankStore::provisionBegin(profile,digest,port.hardware,port.keyValue,&bankToken,port.appData)!=RISC_BANK_OK){reason=Reason::StageFailed;return Step::Failed;}
   }
   risc_bank_status_v1 status{};status.struct_size=sizeof(status);
   if(RiscBankStore::provisionStep(bankToken,&status)!=RISC_BANK_OK){reason=Reason::StageFailed;return Step::Failed;}
   return status.state==RiscUpdate::Transaction::StoreStaging?Step::Done:Step::Pending;
 }
 Step download(const RiscProvision::File& file,uint32_t limit){
   reason=Reason::DownloadFailed;
   if(fileIndex>=profile.count||&file!=&profile.files[fileIndex]){reason=Reason::StageFailed;return Step::Failed;}
   const auto* http=port.hardware.httpClient;
   if(!httpToken){uint64_t utc=0;bool pending=false;if(!clock(utc,pending))return pending?Step::Pending:Step::Failed;
     risc_http_request_v1 request{sizeof(request),file.url,file.bytes,300000u,utc};
     if(http->open(http->context,&request,&httpToken)!=RISC_HTTP_OK || !httpToken){reason=Reason::DownloadFailed;return Step::Failed;}
   }
   uint32_t count=0;const uint32_t capacity=std::min<uint32_t>(limit,sizeof(buffer));
   const int32_t result=http->read(http->context,httpToken,buffer,capacity,&count);
   if(result==RISC_HTTP_AGAIN && count==0)return Step::Pending;
   if(result==RISC_HTTP_OK && count && count<=capacity && count<=file.bytes-received){
     if(RiscBankStore::provisionWrite(bankToken,fileIndex,buffer,count)!=RISC_BANK_OK){reason=Reason::StageFailed;return Step::Failed;}
     received+=count;return Step::Pending;
   }
   if(result==RISC_HTTP_EOF && count==0){
     risc_http_response_v1 info{};info.struct_size=sizeof(info);
     if(http->info(http->context,httpToken,&info)!=RISC_HTTP_OK||info.status_code!=200||received!=file.bytes||info.received_bytes!=received||
        (info.content_length!=-1 && info.content_length!=int64_t(file.bytes))){reason=Reason::DownloadFailed;return Step::Failed;}
     if(!closeHttp())return Step::Failed;
     received=0;++fileIndex;return Step::Done;
   }
   reason=Reason::DownloadFailed;return Step::Failed;
 }
 Step validate(){
   reason=Reason::StageFailed;
   if(!finished){
     // Close network before the peak PSRAM/native-ELF admission phase.
     if(!closeTransport())return Step::Failed;
     if(RiscBankStore::provisionFinish(bankToken)!=RISC_BANK_OK){reason=Reason::StageFailed;return Step::Failed;}finished=true;
   }
   risc_bank_status_v1 status{};status.struct_size=sizeof(status);
   if(RiscBankStore::provisionStep(bankToken,&status)!=RISC_BANK_OK){reason=Reason::StageFailed;return Step::Failed;}
   return status.state==RISC_BANK_READY?Step::Done:Step::Pending;
 }
 RiscProvision::Selection activate(){
   const int32_t result=RiscBankStore::provisionActivate(bankToken);
   risc_bank_status_v1 status{};status.struct_size=sizeof(status);
   if(!RiscBankStore::provisionStatus(bankToken,&status)||status.state==RISC_BANK_ACTIVATION_UNKNOWN){reason=Reason::SelectionUnknown;return RiscProvision::Selection::Unknown;}
   if(result==RISC_BANK_OK && status.state==RISC_BANK_ACTIVATED){reason=Reason::Activated;return RiscProvision::Selection::Selected;}
   reason=Reason::StageFailed;return RiscProvision::Selection::Unchanged;
 }
 Step abort(){
   if(!closeTransport())return Step::Failed;
   if(bankToken){if(RiscBankStore::provisionAbort(bankToken)!=RISC_BANK_OK){retained=true;reason=Reason::CleanupRetained;return Step::Failed;}bankToken=0;}
   return Step::Done;
 }
 RiscProvision::Backend backend(){return {this,
   [](void* c){return uint32_t(static_cast<Session*>(c)->port.hardware.now());},
   [](void* c){return static_cast<Session*>(c)->safe();},
   [](void* c){auto& s=*static_cast<Session*>(c);auto& h=s.port.hardware;if(!h.radioIdle()||!h.httpIdle()){s.retained=true;s.reason=Reason::NativeUnsafe;return Step::Failed;}return Step::Done;},
   [](void*){return RiscBankStore::bootLabel()!=nullptr;},
   [](void* c,const uint8_t* d){return static_cast<Session*>(c)->matches(d);},
   [](void* c,const char*,const char*){return static_cast<Session*>(c)->connect();},
   [](void* c){return static_cast<Session*>(c)->begin();},
   [](void* c,const RiscProvision::File& f,uint32_t n){return static_cast<Session*>(c)->download(f,n);},
   [](void* c,const RiscProvision::Profile&){return static_cast<Session*>(c)->validate();},
   [](void* c){return static_cast<Session*>(c)->closeTransport()?Step::Done:Step::Failed;},
   [](void* c,const uint8_t*){return static_cast<Session*>(c)->activate();},
   [](void* c){return static_cast<Session*>(c)->abort();}
 };}
};
Session* retainedSession=nullptr;
bool supported(const Port& p){const auto& h=p.hardware;const auto* http=h.httpClient;return h.owner&&h.owner()&&h.now&&h.sleep&&p.operationSafe&&
 h.radioJoin&&h.radioState&&h.radioAddresses&&h.radioLeave&&h.radioIdle&&h.httpIdle&&h.httpSafe&&http&&http->api_version==1&&http->struct_size>=sizeof(*http)&&http->open&&http->read&&http->info&&http->close;}
}
Result run(const Port& port,const char* root){
 if(retainedSession)return {Outcome::Stopped,retainedSession->reason};
 if(!port.hardware.owner||!port.hardware.owner()||!port.operationSafe||!port.operationSafe())return {Outcome::Stopped,Reason::NativeUnsafe};
 if(!root||root[0]!='/'||strlen(root)>=256||!supported(port))return {Outcome::Installed,Reason::InputUnavailable};
 if(!RiscBankStore::provisionAvailable())return {RiscBankStore::exitSafe()?Outcome::Installed:Outcome::Stopped,Reason::PairUnavailable};
 void* memory=heap_caps_malloc(sizeof(Session),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!memory)return {Outcome::Installed,Reason::MemoryUnavailable};
 auto* session=new(memory) Session(port);strcpy(session->root,root);
 const auto loaded=RiscProvision::loadProfile(port.input,session->input,sizeof(session->input),session->profile,session->digest,sha);
 if(loaded!=RiscProvision::InputStatus::Ready){
   const Reason reason=loaded==RiscProvision::InputStatus::Missing?Reason::NoProfile:loaded==RiscProvision::InputStatus::Invalid?Reason::InvalidProfile:Reason::InputUnavailable;
   session->~Session();free(session);return {Outcome::Installed,reason};
 }
 RiscProvision::Coordinator coordinator(session->backend(),session->profile,session->digest);
 for(;;){const auto state=coordinator.step();
   if(state==RiscProvision::State::Installed||state==RiscProvision::State::Recovery){
     const Reason reason=session->reason;session->~Session();free(session);return {Outcome::Installed,reason};
   }
   if(state==RiscProvision::State::Retained||state==RiscProvision::State::Restart||state==RiscProvision::State::SelectionUnknown){
     if(state==RiscProvision::State::Retained && !session->retained && session->reason!=Reason::Activated && session->reason!=Reason::SelectionUnknown)session->reason=Reason::NativeUnsafe;
     retainedSession=session;
     if(state==RiscProvision::State::Restart||state==RiscProvision::State::SelectionUnknown)(void)RiscBankStore::provisionRestart(session->bankToken);
     return {Outcome::Stopped,session->reason};
   }
   port.hardware.sleep(1);
 }
}
const char* reasonName(Reason reason){switch(reason){
 case Reason::NoProfile:return "profile-absent";case Reason::InvalidProfile:return "profile-invalid";
 case Reason::InputUnavailable:return "profile-input-unavailable";case Reason::MemoryUnavailable:return "profile-memory-unavailable";
 case Reason::PairUnavailable:return "pair-unavailable";case Reason::Unchanged:return "installed-profile";
 case Reason::ClockUnavailable:return "fresh-time-unavailable";case Reason::NetworkFailed:return "network-unavailable";
 case Reason::DownloadFailed:return "download-failed";case Reason::StageFailed:return "stage-failed";
 case Reason::CleanupRetained:return "cleanup-retained";case Reason::NativeUnsafe:return "native-unsafe";
 case Reason::Activated:return "activation-awaits-safe-restart";case Reason::SelectionUnknown:return "selection-unknown";
 case Reason::AttemptHeld:return "profile-attempt-held";case Reason::HistoryUnavailable:return "profile-history-unavailable";
 }return "unavailable";}
}
#endif
