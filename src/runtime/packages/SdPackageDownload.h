#pragma once
#include <RiscStoragePackageAsyncV1.h>
#include <RiscHttpClientV1.h>
#include <cstring>
#include <cstdio>
namespace RiscPackages {
// One immutable image, borrowed provider tables pinned until terminal. This
// coordinator never changes boot selection or grants. Its durable caller may
// activate a complete admitted graph only after every selected image is Ready.
class SdPackageDownload final {
 public:
  struct Record {char url[RISC_HTTP_URL_MAX+1]{},path[193]{};uint32_t size=0;uint8_t sha256[32]{};bool driver=false;};
  enum class State {Idle,Journal,Refresh,Recover,Existing,Directory,RemovePartial,OpenFile,OpenHttp,ReadHttp,Write,CloseHttp,CloseFile,Validate,Publish,Verify,Ending,Ready,Failed,Cancelled,Retained};
  bool start(const risc_storage_volume_api_v1*v,const risc_http_client_v1*h,Record&r,uint64_t utc,bool durable=false){
   if(state_!=State::Idle||!risc_storage_volume_download(v)||!h||h->api_version!=1||h->struct_size<sizeof(*h)||!h->open||!h->read||!h->info||!h->close||
      !r.size||r.size>2097152u||!std::memchr(r.url,0,sizeof(r.url))||std::strncmp(r.url,"https://",8)||
      !std::memchr(r.path,0,sizeof(r.path))||r.path[0]!='/'||std::strstr(r.path,"..")||std::strchr(r.path,'\\')||
      (r.driver?std::strncmp(r.path,"/Drivers/",9):std::strncmp(r.path,"/Apps/",6)))return false;
   const char*leaf=std::strrchr(r.path,'/');if(!leaf||std::strlen(leaf)!=75||std::strncmp(leaf,"/image-",7)||std::strcmp(leaf+71,".elf"))return false;
   static const char hex[]="0123456789abcdef";
   for(unsigned i=0;i<32;++i)if(leaf[7+i*2]!=hex[r.sha256[i]>>4]||leaf[8+i*2]!=hex[r.sha256[i]&15])return false;
   if(std::snprintf(partial_,sizeof(partial_),"%s.part",r.path)>=int(sizeof(partial_)))return false;
   record_=&r;volume_=v;http_=h;api_=risc_storage_volume_packages(v);utc_=utc;
   if(api_->base.begin(v->context,&session_)!=RISC_STORAGE_ASYNC_OK)return false;
   state_=durable?State::Refresh:State::Journal;return true;
  }
  State state()const{return state_;}
  bool journalCommitted(){if(state_!=State::Journal)return false;state_=State::Refresh;return true;}
  void cancel(){if(terminal()||state_==State::Idle)return;cancelled_=true;state_=State::Ending;}
  void step(){
   if(terminal()||state_==State::Idle||state_==State::Journal)return;
   if(state_==State::Ending){
    if(httpHandle_){const int rc=http_->close(http_->context,httpHandle_);if(rc!=RISC_HTTP_OK){state_=State::Retained;return;}httpHandle_=0;}
    const int rc=api_->base.end(volume_->context,session_);if(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY)return;
    if(rc!=RISC_STORAGE_ASYNC_OK){state_=State::Retained;return;}
    session_=0;state_=cancelled_?State::Cancelled:failed_?State::Failed:State::Ready;return;
   }
   if(state_==State::OpenHttp){
    risc_http_request_v1 request{sizeof(request),record_->url,record_->size,300000,utc_};
    int rc=http_->open(http_->context,&request,&httpHandle_);if(rc==RISC_HTTP_BUSY)return;
    if(rc!=RISC_HTTP_OK){fail(rc==RISC_HTTP_RETAINED);return;}state_=State::ReadHttp;return;
   }
   if(state_==State::ReadHttp){
    uint32_t n=0;int rc=http_->read(http_->context,httpHandle_,request_.data,RISC_HTTP_CHUNK_MAX,&n);
    if(rc==RISC_HTTP_AGAIN)return;
    if(rc==RISC_HTTP_EOF){
     risc_http_response_v1 response{sizeof(response),0,0,0};
     if(received_!=record_->size||n||http_->info(http_->context,httpHandle_,&response)!=RISC_HTTP_OK||response.status_code!=200||
        response.received_bytes!=received_||(response.content_length!=-1&&response.content_length!=received_)){fail(false);return;}
     state_=State::CloseHttp;return;
    }
    if(rc!=RISC_HTTP_OK||!n||n>RISC_HTTP_CHUNK_MAX||n>record_->size-received_){fail(rc==RISC_HTTP_RETAINED);return;}
    request_.size=n;received_+=n;state_=State::Write;return;
   }
   if(state_==State::CloseHttp){if(http_->close(http_->context,httpHandle_)!=RISC_HTTP_OK){state_=State::Retained;return;}httpHandle_=0;state_=State::CloseFile;return;}
   if(!operation_){
    int rc;
    if(state_==State::Refresh||state_==State::RemovePartial){
     risc_storage_async_request_v1 q{};q.operation=state_==State::Refresh?RISC_STORAGE_ASYNC_REFRESH:RISC_STORAGE_ASYNC_REMOVE_FILE;
     std::strcpy(q.path,partial_);rc=api_->base.submit(volume_->context,session_,&q,&operation_);
    }else{
     const auto size=request_.size;
     // Keep the single copied HTTP chunk until its worker submission completes.
     request_.operation=state_==State::Recover?RISC_STORAGE_PACKAGE_RECOVER:state_==State::Directory?RISC_STORAGE_PACKAGE_MKDIR:state_==State::OpenFile?RISC_STORAGE_PACKAGE_OPEN_WRITE:
       state_==State::Write?RISC_STORAGE_PACKAGE_WRITE_CHUNK:state_==State::CloseFile?RISC_STORAGE_PACKAGE_CLOSE_WRITE:
       (state_==State::Validate||state_==State::Verify||state_==State::Existing)?(record_->driver?RISC_STORAGE_PACKAGE_VALIDATE_DRIVER:RISC_STORAGE_PACKAGE_VALIDATE):RISC_STORAGE_PACKAGE_REPLACE;
     request_.size=state_==State::Write?size:record_->size;
     std::memcpy(request_.sha256,record_->sha256,32);std::strcpy(request_.path,(state_==State::Verify||state_==State::Recover||state_==State::Existing)?record_->path:partial_);
     request_.destination[0]=0;
     if(state_==State::Directory){
      const char*next=std::strchr(record_->path+directory_+1,'/');
      if(!next){state_=State::RemovePartial;return;}
      directory_=size_t(next-record_->path);std::memcpy(request_.path,record_->path,directory_);request_.path[directory_]=0;
     }
     if(state_==State::Publish)std::strcpy(request_.destination,record_->path);
     rc=api_->submit_package(volume_->context,session_,&request_,&operation_);
    }
    if(rc==RISC_STORAGE_ASYNC_BUSY){if(state_==State::Directory)directory_=previousDirectory_;return;}
    if(rc!=RISC_STORAGE_ASYNC_OK){fail(rc==RISC_STORAGE_ASYNC_RETAINED);return;}
    if(state_==State::Directory)previousDirectory_=directory_;return;
   }
   int rc=api_->base.collect(volume_->context,session_,operation_,&result_);
   if(rc==RISC_STORAGE_ASYNC_BUSY||rc==RISC_STORAGE_ASYNC_PENDING)return;
   operation_=0;
   // A failed atomic replacement may have published the exact image. The
   // immutable destination is checked on resume; no graph selects it yet.
   if(state_==State::Existing && rc!=RISC_STORAGE_ASYNC_RETAINED){state_=rc==RISC_STORAGE_ASYNC_OK?State::Ending:State::Directory;return;}
   if(rc!=RISC_STORAGE_ASYNC_OK){fail(rc==RISC_STORAGE_ASYNC_RETAINED);return;}
   switch(state_){
    case State::Refresh:state_=State::Recover;break;
    case State::Recover:state_=State::Existing;break;
    case State::Directory:break;
    case State::RemovePartial:state_=State::OpenFile;break;
    case State::OpenFile:state_=State::OpenHttp;break;
    case State::Write:state_=State::ReadHttp;break;
    case State::CloseFile:state_=State::Validate;break;
    case State::Validate:state_=State::Publish;break;
    case State::Publish:state_=State::Verify;break;
    case State::Verify:state_=State::Ending;break;
    default:fail(false);break;
   }
  }
 private:
  bool terminal()const{return state_==State::Ready||state_==State::Failed||state_==State::Cancelled||state_==State::Retained;}
  void fail(bool retained){failed_=true;state_=retained?State::Retained:State::Ending;}
  State state_=State::Idle;Record*record_=nullptr;const risc_storage_volume_api_v1*volume_=nullptr;
  const risc_http_client_v1*http_=nullptr;const risc_storage_volume_api_v1_packages*api_=nullptr;
  uint64_t session_=0,operation_=0,httpHandle_=0,utc_=0;uint32_t received_=0;size_t directory_=0,previousDirectory_=0;
  bool failed_=false,cancelled_=false;char partial_[205]{};
  risc_storage_package_request_v1 request_{};risc_storage_async_result_v1 result_{};
};
}
