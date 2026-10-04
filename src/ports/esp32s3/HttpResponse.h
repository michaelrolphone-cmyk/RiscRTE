#pragma once
#include "HttpBounds.h"
#include <http_parser.h>
#include <cstring>
namespace RiscCpu { namespace HttpResponse {
/* Adapter around the pinned SDK HTTP parser, not a second HTTP parser. Every
 * feed is <=512 wire bytes. All fields, trailers and total wire overhead have
 * independent bounds; the parser never receives an unbounded stream. */
struct Decoder {
  http_parser parser{};
  uint32_t maximum=0,received=0,wire=0,headers=0;
  int32_t status=0,error=0;
  int64_t length=-1;
  bool complete=false,headersDone=false,redirect=false,lastValue=false,haveLocation=false;
  char field[64]{},value[RISC_HTTP_URL_MAX+1]{},location[RISC_HTTP_URL_MAX+1]{};
  size_t fieldBytes=0,valueBytes=0;
  uint8_t* output=nullptr;uint32_t capacity=0,copied=0;
  explicit Decoder(uint32_t max):maximum(max){http_parser_init(&parser,HTTP_RESPONSE);parser.data=this;}
  static Decoder& self(http_parser* p){return *static_cast<Decoder*>(p->data);}
  bool header(){
    if(!fieldBytes)return true;
    if(++headers>128){error=RISC_HTTP_SIZE;return false;}
    if(!std::strcmp(field,"location")){
      if(haveLocation || !HttpBounds::url(value)){error=RISC_HTTP_INVALID;return false;}
      std::memcpy(location,value,valueBytes+1);haveLocation=true;
    }
    if(!std::strcmp(field,"content-encoding") && std::strcmp(value,"identity")){
      error=RISC_HTTP_INVALID;return false;
    }
    fieldBytes=valueBytes=0;field[0]=value[0]=0;lastValue=false;return true;
  }
  static int begin(http_parser* p){auto& s=self(p);if(s.complete){s.error=RISC_HTTP_INVALID;return 1;}return 0;}
  static int fieldPart(http_parser* p,const char* bytes,size_t n){
    auto& s=self(p);if(s.lastValue && !s.header())return 1;
    if(n>=sizeof(s.field)-s.fieldBytes){s.error=RISC_HTTP_SIZE;return 1;}
    for(size_t i=0;i<n;++i){unsigned char c=bytes[i];if(c>='A'&&c<='Z')c+=32;s.field[s.fieldBytes++]=static_cast<char>(c);}
    s.field[s.fieldBytes]=0;return 0;
  }
  static int valuePart(http_parser* p,const char* bytes,size_t n){
    auto& s=self(p);s.lastValue=true;
    if(n>=sizeof(s.value)-s.valueBytes){s.error=RISC_HTTP_SIZE;return 1;}
    std::memcpy(s.value+s.valueBytes,bytes,n);s.valueBytes+=n;s.value[s.valueBytes]=0;return 0;
  }
  static int ready(http_parser* p){
    auto& s=self(p);if(!s.header())return 1;
    if(p->nread>16384u){s.error=RISC_HTTP_SIZE;return 1;}
    s.headersDone=true;s.status=p->status_code;
    if(p->http_major!=1 || p->http_minor>1 || p->upgrade){s.error=RISC_HTTP_INVALID;return 1;}
    s.length=p->content_length==UINT64_MAX?-1:static_cast<int64_t>(p->content_length);
    if(p->content_length!=UINT64_MAX && p->content_length>s.maximum){s.error=RISC_HTTP_SIZE;return 1;}
    s.redirect=s.status==301||s.status==302||s.status==303||s.status==307||s.status==308;
    if(s.redirect){
      if(!s.haveLocation){s.error=RISC_HTTP_INVALID;return 1;}
      http_parser_pause(p,1);return 0;
    }
    if(s.status!=200){s.error=RISC_HTTP_STATUS;return 1;}
    return 0;
  }
  static int body(http_parser* p,const char* bytes,size_t n){
    auto& s=self(p);
    if(n>s.maximum-s.received || n>s.capacity-s.copied || (!s.output&&n)){
      s.error=RISC_HTTP_SIZE;return 1;
    }
    std::memcpy(s.output+s.copied,bytes,n);s.copied+=n;s.received+=n;return 0;
  }
  static int end(http_parser* p){auto& s=self(p);if(!s.header())return 1;s.complete=true;return 0;}
  bool feed(const uint8_t* bytes,uint32_t count,void* out,uint32_t cap){
    copied=0;
    if(error || count>RISC_HTTP_CHUNK_MAX || (!bytes&&count) || (!out&&cap) || cap>RISC_HTTP_CHUNK_MAX ||
       wire>maximum+65536u || count>maximum+65536u-wire){error=RISC_HTTP_SIZE;return false;}
    wire+=count;output=static_cast<uint8_t*>(out);capacity=cap;
    http_parser_settings settings{};settings.on_message_begin=begin;settings.on_header_field=fieldPart;
    settings.on_header_value=valuePart;settings.on_headers_complete=ready;settings.on_body=body;settings.on_message_complete=end;
    const size_t n=http_parser_execute(&parser,&settings,reinterpret_cast<const char*>(bytes),count);
    output=nullptr;capacity=0;
    const auto code=HTTP_PARSER_ERRNO(&parser);
    if(redirect && code==HPE_PAUSED && !error)return true;
    if(n!=count || code!=HPE_OK){if(!error)error=RISC_HTTP_TRANSPORT;return false;}
    if(!headersDone && wire>16384u){error=RISC_HTTP_SIZE;return false;}
    return !error;
  }
};
} }
