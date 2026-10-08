#include "Coordinator.h"
#include <cstring>
namespace RiscProvision {
Coordinator::Coordinator(Backend b,const Profile& p,const uint8_t (&digest)[32]):io_(b),profile_(p){memcpy(digest_,digest,32);}
bool Coordinator::complete() const{return state_==State::Installed || state_==State::Recovery || state_==State::Restart || state_==State::Retained || state_==State::SelectionUnknown;}
void Coordinator::fallback(){state_=io_.installed(io_.context)?State::Installed:State::Recovery;}
State Coordinator::step(){
  if(complete())return state_;
  if(!io_.now||!io_.safe||!io_.recover||!io_.installed||!io_.matches||!io_.connect||!io_.begin||!io_.download||!io_.validate||!io_.close||!io_.activate||!io_.abort){state_=State::Retained;return state_;}
  void* c=io_.context;
  if(!io_.safe(c)){state_=State::Retained;return state_;}
  uint32_t now=io_.now(c);
  if(state_==State::Initial){started_=now;state_=State::Recover;}
  if(state_==State::Cleanup){
    if(uint32_t(now-cleanupStarted_)>=30000){state_=State::Retained;return state_;}
    Step result=io_.abort(c);
    if(!io_.safe(c)||result==Step::Failed || uint32_t(io_.now(c)-cleanupStarted_)>=30000)state_=State::Retained;
    else if(result==Step::Done)fallback();
    return state_;
  }
  if(uint32_t(now-started_)>=300000){cleanupStarted_=now;state_=State::Cleanup;return state_;}
  Step result=Step::Pending;State next=state_;
  switch(state_){
    case State::Recover:
      result=io_.recover(c);
      if(result==Step::Done){
        if(io_.installed(c) && io_.matches(c,digest_)){next=State::Installed;}
        else if(profile_.count<3 || profile_.count>MaxFiles){fallback();return state_;}
        else next=State::Connect;
      }
      break;
    case State::Connect:result=io_.connect(c,profile_.ssid,profile_.password);next=State::Stage;break;
    case State::Stage:result=io_.begin(c);next=State::Download;break;
    case State::Download:
      result=io_.download(c,profile_.download(file_),ChunkBytes);
      if(result==Step::Done){++file_;next=file_==profile_.downloads()?State::Validate:State::Download;}
      break;
    case State::Validate:result=io_.validate(c,profile_);next=State::Close;break;
    case State::Close:result=io_.close(c);next=State::Activate;break;
    case State::Activate:{
      auto selected=io_.activate(c,digest_);
      // After selection, never abort/rewrite even if safety/deadline changed.
      if(selected==Selection::Unknown)state_=State::SelectionUnknown;
      else if(selected==Selection::Selected)state_=io_.safe(c)?State::Restart:State::Retained;
      else {cleanupStarted_=io_.now(c);state_=State::Cleanup;}
      return state_;
    }
    default:state_=State::Retained;return state_;
  }
  if(!io_.safe(c)){state_=State::Retained;return state_;}
  if(result==Step::Failed || uint32_t(io_.now(c)-started_)>=300000){cleanupStarted_=io_.now(c);state_=State::Cleanup;}
  else if(result==Step::Done)state_=next;
  return state_;
}
}
