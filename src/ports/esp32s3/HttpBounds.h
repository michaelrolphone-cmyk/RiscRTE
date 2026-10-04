#pragma once
#include <RiscHttpClientV1.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace RiscCpu { namespace HttpBounds {
constexpr uint64_t FirstUtc=UINT64_C(1704067200),LastUtc=UINT64_C(4102444799);
struct Calendar {int year,month,day,hour,minute,second;};
inline bool leap(unsigned year){return year%4==0 && (year%100!=0 || year%400==0);}
inline bool calendar(uint64_t seconds,Calendar& out){
  if(seconds<FirstUtc || seconds>LastUtc+300u)return false;
  uint64_t days=seconds/86400u;unsigned year=1970;
  while(days>=(leap(year)?366u:365u)){days-=leap(year)?366u:365u;++year;}
  static const unsigned months[]={31,28,31,30,31,30,31,31,30,31,30,31};
  unsigned month=0;
  while(month<11){const unsigned n=months[month]+(month==1&&leap(year));if(days<n)break;days-=n;++month;}
  const unsigned time=seconds%86400u;
  out={static_cast<int>(year),static_cast<int>(month+1),static_cast<int>(days+1),
    static_cast<int>(time/3600u),static_cast<int>((time/60u)%60u),static_cast<int>(time%60u)};
  return true;
}
/* Reject userinfo, controls, fragments, ambiguous authority, non-443 ports and
 * an empty host. Paths/queries remain generic; update policy validates its own
 * immutable release URL separately. No redirect may downgrade authentication. */
inline bool url(const char* value){
  if(!value)return false;
  size_t size=0;while(size<=RISC_HTTP_URL_MAX && value[size])++size;
  if(size>RISC_HTTP_URL_MAX || size<9 || std::memcmp(value,"https://",8))return false;
  const char* end=value+size;const char* host=value+8;const char* authority=host;
  while(authority<end && *authority!='/' && *authority!='?')++authority;
  if(host==authority || *host=='.' || authority[-1]=='.')return false;
  const char* colon=nullptr;
  for(const char* p=host;p<authority;++p){
    const unsigned char c=static_cast<unsigned char>(*p);
    if(c==':'){if(colon) return false;colon=p;}
    else if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'))return false;
  }
  if(colon && (colon==host || authority-colon!=4 || std::memcmp(colon,":443",4)))return false;
  for(const char* p=authority;p<end;++p){const unsigned char c=static_cast<unsigned char>(*p);if(c<=0x20||c>=0x7f||c=='#'||c=='\\')return false;}
  return true;
}
inline bool request(const risc_http_request_v1* r){
  return r && r->struct_size>=sizeof(*r) && url(r->url) &&
    r->max_bytes && r->max_bytes<=RISC_HTTP_MAX_BYTES &&
    r->timeout_ms && r->timeout_ms<=300000u && r->utc_seconds>=FirstUtc && r->utc_seconds<=LastUtc;
}
class Budget {
 public:
  Budget(uint64_t now,uint32_t ms,uint32_t maximum):start_(now),progress_(now),limit_(ms),maximum_(maximum){}
  bool alive(uint64_t now)const{return now>=start_ && now-start_<limit_ && now>=progress_ && now-progress_<15000u;}
  bool accept(uint64_t now,uint32_t n){if(!alive(now)||n>maximum_-received_)return false;received_+=n;if(n)progress_=now;return true;}
  uint32_t received()const{return received_;}
  uint32_t remaining()const{return maximum_-received_;}
  uint32_t wait(uint64_t now,uint32_t cap)const{
    if(!alive(now))return 0;
    const uint64_t total=limit_-(now-start_),idle=15000u-(now-progress_);
    uint64_t n=total<idle?total:idle;if(n>cap)n=cap;return static_cast<uint32_t>(n);
  }
  void connected(uint64_t now){progress_=now;}
 private:
  uint64_t start_,progress_;uint32_t limit_,maximum_,received_=0;
};
} }
