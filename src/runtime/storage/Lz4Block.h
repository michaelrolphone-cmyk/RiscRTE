#pragma once
#include <stddef.h>
#include <stdint.h>
namespace RiscStorage {
// One raw LZ4 block, not an LZ4 frame. Input and output lengths are trusted
// envelope bounds, never inferred from compressed bytes. No allocation or I/O
// ownership: the caller retains both buffers/session on every result.
class Lz4Block final {
 public:
  enum Status : int32_t { Ok=0, Invalid=-1, Io=-2, Retained=-9 };
  struct Hooks {
    void* context;
    Status (*read)(void*,uint8_t*,size_t,size_t*);
    bool (*safe)(void*);
    Status (*checkpoint)(void*,size_t); // absolute produced byte count
  };
  static constexpr size_t InputBufferBytes=128, CheckpointBytes=256;
  static Status decode(Hooks hooks,size_t inputBytes,uint8_t* output,
                       size_t outputBytes,size_t capacity,size_t* written) {
    if(written)*written=0;
    if(!written || !hooks.read || !hooks.safe || !hooks.checkpoint ||
       (!output && outputBytes) || outputBytes>capacity || !inputBytes)return Invalid;
    Lz4Block decoder(hooks,inputBytes,output,outputBytes);
    const Status result=decoder.run();*written=decoder.produced_;
    return result;
  }
 private:
  Hooks hooks_;
  size_t remaining_,available_=0,cursor_=0,produced_=0,expected_,checked_=0;
  uint8_t* output_;
  uint8_t input_[InputBufferBytes];
  Status status_=Ok;
  Lz4Block(Hooks h,size_t n,uint8_t* p,size_t size):
    hooks_(h),remaining_(n),expected_(size),output_(p) {}
  bool live() {
    if(status_!=Ok)return false;
    if(!hooks_.safe(hooks_.context)){status_=Retained;return false;}
    return true;
  }
  bool result(Status value) {
    if(!live())return false;
    if(value==Ok)return true;
    status_=(value==Invalid || value==Io)?value:Retained;
    return false;
  }
  bool checkpoint() {
    if(!live())return false;
    const Status rc=hooks_.checkpoint(hooks_.context,produced_);
    if(!result(rc))return false;
    checked_=produced_;return true;
  }
  size_t left()const{return remaining_+(available_-cursor_);}
  bool byte(uint8_t& value) {
    if(cursor_==available_) {
      if(!remaining_){status_=Invalid;return false;}
      if(!checkpoint())return false;
      size_t wanted=remaining_<sizeof(input_)?remaining_:sizeof(input_),actual=0;
      if(!live())return false;
      const Status rc=hooks_.read(hooks_.context,input_,wanted,&actual);
      if(!result(rc))return false;
      if(!actual || actual>wanted){status_=Io;return false;}
      remaining_-=actual;available_=actual;cursor_=0;
    }
    value=input_[cursor_++];return true;
  }
  bool length(size_t base,size_t limit,size_t& value) {
    value=base;
    if(value>limit){status_=Invalid;return false;}
    if(base!=15)return true;
    uint8_t extra;
    do {
      if(!byte(extra))return false;
      if(size_t(extra)>limit-value){status_=Invalid;return false;}
      value+=extra;
    } while(extra==255);
    return true;
  }
  bool progress() {
    return produced_-checked_<CheckpointBytes || checkpoint();
  }
  Status run() {
    if(!checkpoint())return status_;
    for(;;) {
      uint8_t token;
      if(!byte(token))return status_;
      size_t literals;
      if(!length(token>>4,expected_-produced_,literals))return status_;
      if(literals>left())return Invalid;
      for(size_t i=0;i<literals;++i) {
        uint8_t value;if(!byte(value))return status_;
        output_[produced_++]=value;
        if(!progress())return status_;
      }
      if(!left()) {
        if(produced_!=expected_)return Invalid;
        return checkpoint()?Ok:status_;
      }
      // Output exhausted before input is an explicit trailing-data refusal.
      if(produced_==expected_)return Invalid;
      uint8_t lo,hi;
      if(!byte(lo)||!byte(hi))return status_;
      const size_t distance=size_t(lo)|(size_t(hi)<<8);
      if(!distance || distance>produced_)return Invalid;
      const size_t room=expected_-produced_;
      if(room<4)return Invalid;
      size_t match;
      if(!length(token&15,room-4,match))return status_;
      match+=4; // bounded above before addition, including SIZE_MAX envelopes
      for(size_t i=0;i<match;++i) {
        output_[produced_]=output_[produced_-distance];++produced_;
        if(!progress())return status_;
      }
      // A final literal sequence is required; a match alone is not a block end.
      if(!left())return Invalid;
    }
  }
};
}
