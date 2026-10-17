#include "Files.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
namespace RiscSerial {
namespace {
bool number(const char*s,uint64_t&out,unsigned base=10){
 if(!s||!*s)return false;
 out=0;for(;*s;++s){unsigned v=*s>='0'&&*s<='9'?unsigned(*s-'0'):*s>='a'&&*s<='f'?unsigned(*s-'a'+10):99;
  if(v>=base || out>(UINT64_MAX-v)/base)return false;
  out=out*base+v;
 }return true;
}
bool unhex(const char*s,uint8_t*out,size_t&n){
 n=0;if(!s || !*s)return false;
 for(size_t i=0;s[i];i+=2){if(!s[i+1]||n==Files::Chunk)return false;uint64_t v=0;char b[3]{s[i],s[i+1],0};if(!number(b,v,16))return false;out[n++]=uint8_t(v);}return true;
}
void hex(const uint8_t*in,size_t n,char*out){static const char digits[]="0123456789abcdef";for(size_t i=0;i<n;++i){out[2*i]=digits[in[i]>>4];out[2*i+1]=digits[in[i]&15];}out[2*n]=0;}
uint32_t crc32(uint32_t crc,const uint8_t*p,size_t n){while(n--){crc^=*p++;for(unsigned i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}return crc;}
}
bool Files::bind(const Namespace*map,size_t count,Hooks hooks){
 if(bound_ || !hooks.owner || !hooks.safe || !hooks.millis || !hooks.owner() || count>NamespaceMax || (count&&!map))return false;
 for(size_t i=0;i<count;++i){const auto&n=map[i];if(!n.id||!n.files||!n.label||!*n.label||strnlen(n.label,64)>=64)return false;
  for(size_t j=0;j<i;++j)if(map[j].id==n.id)return false;
 }
 namespaces_=map;count_=count;hooks_=hooks;bound_=true;return true;
}
const Namespace* Files::find(uint64_t id)const{for(size_t i=0;i<count_;++i)if(namespaces_[i].id==id)return &namespaces_[i];return nullptr;}
bool Files::custodySafe()const{if(retained_)return false;for(size_t i=0;i<count_;++i)if(namespaces_[i].files->retained())return false;return true;}
bool Files::exitSafe()const{return !calling_&&!pending()&&custodySafe();}
void Files::checkRetention(){if(!custodySafe())retained_=true;}
void Files::input(uint8_t value){
 if(!bound_ || requestReady_)return;
 if(value=='\n'){
  if(!discard_ && requestLength_ && !strncmp(request_,"RTEF1 ",6)){request_[requestLength_]=0;requestReady_=true;}
  else requestLength_=0;
  discard_=false;return;
 }
 if(discard_)return;
 if(value<32 || value>126 || requestLength_+1>=Line){discard_=true;requestLength_=0;return;}
 request_[requestLength_++]=char(value);request_[requestLength_]=0;
 if(!handle_ || (requestLength_>=6 && !strncmp(request_,"RTEF1 ",6)))touched_=hooks_.millis();
}
void Files::consumeOutput(size_t n){if(n>outputSize())n=outputSize();sent_+=n;if(n)touched_=hooks_.millis();}
void Files::disconnect(){
 // An idle transport has only protocol RAM to discard. Do not manufacture a
 // pending cleanup barrier after Light wake: Hybrid may enter Deep before the
 // next application checkpoint, even when no host ever connected.
 // A live handle or reentrant dispatch still needs the settled owner boundary.
 // In particular, an in-flight open may not have installed its handle yet.
 cancel_=handle_ || calling_;
 if(!cancel_)resetSession();
 requestReady_=false;requestLength_=0;discard_=false;responseLength_=sent_=0;
}
void Files::clearTransfer(){selected_=nullptr;handle_=0;size_=offset_=0;crc_=0xffffffffu;writing_=failed_=false;}
void Files::resetSession(){session_=sequence_=0;pathLength_=0;path_[0]=0;previous_[0]=cached_[0]=0;}
int32_t Files::close(){
 if(!handle_)return 0;
 checkRetention();if(retained_)return RISC_FILES_RETAINED;
 int32_t rc=selected_->files->close(handle_);
 if(rc!=RISC_FILES_OK){selected_->files->retainCustody();retained_=true;return RISC_FILES_RETAINED;}
 clearTransfer();return 0;
}
void Files::reply(int32_t rc,const char*format,...){
 int n=snprintf(response_,Line,"RTEF1 %08x %u %d",unsigned(replySession_),unsigned(replySequence_),int(rc));
 if(n<0||size_t(n)>=Line-2){retained_=true;return;}
 size_t used=size_t(n);
 if(format){response_[used++]=' ';va_list args;va_start(args,format);n=vsnprintf(response_+used,Line-used,format,args);va_end(args);
  if(n<0||size_t(n)>=Line-used-1){retained_=true;return;}used+=size_t(n);}
 response_[used++]='\n';response_[used]=0;responseLength_=used;sent_=0;
}
void Files::checkpoint(){
 if(!bound_ || calling_ || !hooks_.owner())return;
 checkRetention();
 const bool cancelling=cancel_ || (pending() && uint32_t(hooks_.millis()-touched_)>=TimeoutMs);
 // Discarding protocol RAM needs no provider readiness. A live token still
 // requires its exact safety gate before cleanup; terminal custody never calls.
 if(!retained_ && !(cancelling && !handle_) && !hooks_.safe())return;
 calling_=true;
 if(cancelling){
  (void)close();resetSession();cancel_=false;requestReady_=false;requestLength_=0;responseLength_=sent_=0;
 }else if(requestReady_ && !outputSize()){
  command();requestReady_=false;requestLength_=0;
 }
 calling_=false;
}
bool Files::shutdown(){disconnect();checkpoint();return exitSafe();}
void Files::command(){
 // Parse a private copy so an exact duplicate can replay its cached response
 // without repeating any open/truncate/write/read/sync/close operation.
 char parsed[Line];memcpy(parsed,request_,requestLength_+1);char*words[8]{};unsigned count=0;
 char*p=parsed;
 while(*p && count<8){words[count++]=p;char*space=strchr(p,' ');if(!space)break;*space=0;p=space+1;if(!*p)return;}
 if(count<4 || count==8 || strcmp(words[0],"RTEF1"))return;
 uint64_t sid=0,seq=0;if(strlen(words[1])!=8||!number(words[1],sid,16)||!sid||!number(words[2],seq)||!seq||seq>UINT32_MAX)return;
 replySession_=uint32_t(sid);replySequence_=uint32_t(seq);
 const char*cmd=words[3];
 if(session_==sid && sequence_==seq){
  if(!strcmp(previous_,request_)){strcpy(response_,cached_);responseLength_=strlen(response_);sent_=0;}
  else reply(-10);
  return;
 }
 if(!strcmp(cmd,"HELLO") && count==4 && seq==1 && sid!=session_){
  if(handle_ || retained_){reply(retained_?RISC_FILES_RETAINED:RISC_FILES_BUSY);return;}
  resetSession();session_=uint32_t(sid);
 }else if(session_!=sid || sequence_==UINT32_MAX || seq!=uint64_t(sequence_)+1){reply(-10);return;}
 sequence_=uint32_t(seq);strcpy(previous_,request_);touched_=hooks_.millis();
 auto finish=[&](){strcpy(cached_,response_);};
 if(retained_){reply(RISC_FILES_RETAINED);finish();return;}
 uint64_t a=0,b=0;uint8_t bytes[Chunk];size_t n=0;
 if(!strcmp(cmd,"HELLO") && count==4){reply(0,"HELLO %u %u %u",unsigned(Chunk),unsigned(RISC_STORAGE_FILES_PATH_MAX-1),unsigned(count_));}
 else if(!strcmp(cmd,"NS") && count==5 && number(words[4],a)){
  if(a>=count_)reply(RISC_FILES_NOT_FOUND);else {const auto&ns=namespaces_[a];char label[128];hex(reinterpret_cast<const uint8_t*>(ns.label),strlen(ns.label),label);reply(0,"NS %llu %u %s",(unsigned long long)ns.id,unsigned(ns.writable),label);}
 }else if(!strcmp(cmd,"ABORT") && count==4){int32_t rc=close();pathLength_=0;path_[0]=0;reply(rc,"ABORT");}
 else if(handle_){
  if(!strcmp(cmd,"WRITE") && count==6 && writing_ && !failed_ && number(words[4],a) && a==offset_ && unhex(words[5],bytes,n) && n<=size_-offset_){
   uint32_t written=0;int32_t rc=selected_->files->write(handle_,bytes,uint32_t(n),&written);crc_=crc32(crc_,bytes,written);offset_+=written;
   if(rc!=0 || !written){failed_=true;if(!rc)rc=RISC_FILES_IO;}checkRetention();reply(retained_?RISC_FILES_RETAINED:rc,"WRITE %llu",(unsigned long long)offset_);
  }else if(!strcmp(cmd,"READ") && count==5 && !writing_ && !failed_ && number(words[4],a) && a==offset_){
   uint32_t got=0;const uint32_t wanted=uint32_t(size_-offset_>Chunk?Chunk:size_-offset_);
   int32_t rc=selected_->files->read(handle_,bytes,wanted,&got);crc_=crc32(crc_,bytes,got);offset_+=got;
   if(rc || (wanted&&!got)){failed_=true;if(!rc)rc=RISC_FILES_IO;}checkRetention();char data[Chunk*2+1];hex(bytes,got,data);
   if(got)reply(retained_?RISC_FILES_RETAINED:rc,"READ %llu %s",(unsigned long long)offset_,data);else reply(retained_?RISC_FILES_RETAINED:rc,"READ %llu",(unsigned long long)offset_);
  }else if(!strcmp(cmd,"COMMIT") && count==5 && writing_ && !failed_ && offset_==size_ && strlen(words[4])==8 && number(words[4],a,16)){
   if(uint32_t(a)!=(crc_^0xffffffffu)){failed_=true;reply(-11,"CRC");}
   else{int32_t rc=selected_->files->sync(handle_);checkRetention();if(!rc)rc=close();else failed_=true;reply(retained_?RISC_FILES_RETAINED:rc,"COMMIT");}
  }else if(!strcmp(cmd,"CLOSE") && count==4 && !writing_ && !failed_ && offset_==size_){uint32_t crc=crc_^0xffffffffu;int32_t rc=close();reply(rc,"CLOSE %08x",unsigned(crc));}
  else reply(RISC_FILES_INVALID);
 }else if(!strcmp(cmd,"PATH") && count==6 && number(words[4],a) && unhex(words[5],bytes,n) && !memchr(bytes,0,n) && (a==0||a==pathLength_) && a+n<sizeof(path_)){
  memcpy(path_+a,bytes,n);pathLength_=size_t(a)+n;path_[pathLength_]=0;reply(0,"PATH %u",unsigned(pathLength_));
 }else if((!strcmp(cmd,"GET") || !strcmp(cmd,"PUT") || !strcmp(cmd,"STAT") || !strcmp(cmd,"MKDIR")) && count>=5 && number(words[4],a) && RiscStorage::validPath(path_) && strcmp(path_,"/")){
  const auto*ns=find(a);if(!ns)reply(RISC_FILES_NOT_FOUND);
  else if(!strcmp(cmd,"GET") && count==5){
   selected_=ns;int32_t rc=ns->files->open(path_,RISC_FILES_READ,&handle_);checkRetention();
   if(!rc){rc=ns->files->info(handle_,&size_,&offset_);checkRetention();if(!rc && offset_)rc=RISC_FILES_IO;if(rc){failed_=true;}else writing_=false;}
   if(!handle_)clearTransfer();
   reply(retained_?RISC_FILES_RETAINED:rc,"GET %llu",(unsigned long long)size_);
  }else if(!strcmp(cmd,"PUT") && count==7 && (!strcmp(words[5],"create") || !strcmp(words[5],"replace")) && number(words[6],b)){
   if(!ns->writable)reply(RISC_FILES_DENIED);
   else{selected_=ns;const uint32_t flags=RISC_FILES_WRITE|(!strcmp(words[5],"create")?(RISC_FILES_CREATE|RISC_FILES_EXCLUSIVE):RISC_FILES_TRUNCATE);
    int32_t rc=ns->files->open(path_,flags,&handle_);checkRetention();if(!rc){size_=b;offset_=0;crc_=0xffffffffu;writing_=true;failed_=false;}if(!handle_)clearTransfer();reply(retained_?RISC_FILES_RETAINED:rc,"PUT");}
  }else if(!strcmp(cmd,"STAT") && count==5){risc_storage_files_stat_v1 st{sizeof(st),0,0,0};int32_t rc=ns->files->stat(path_,&st);checkRetention();reply(retained_?RISC_FILES_RETAINED:rc,"STAT %u %llu",unsigned(st.flags),(unsigned long long)st.size);}
  else if(!strcmp(cmd,"MKDIR") && count==5){int32_t rc=ns->writable?ns->files->mkdir(path_):int32_t(RISC_FILES_DENIED);checkRetention();reply(retained_?RISC_FILES_RETAINED:rc,"MKDIR");}
  else reply(RISC_FILES_INVALID);
 }else reply(RISC_FILES_INVALID);
 finish();
}
}
