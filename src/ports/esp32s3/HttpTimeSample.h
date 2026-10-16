#pragma once
/* Private HTTP-session-only, read-only SNTP exchange. No system SNTP service,
 * background callback, socket export or wall-clock/RTC mutation is involved. */
#include "HttpBounds.h"
#include <RiscHttpTimeV1.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
namespace RiscCpu { namespace HttpTimeSample {
constexpr uint32_t MaximumMs=5000u,MaximumRejected=8u;
constexpr uint64_t NtpOffset=UINT64_C(2208988800),Era=UINT64_C(4294967296);
inline uint64_t now(){return static_cast<uint64_t>(esp_timer_get_time()/1000);}
inline bool hostname(const char* value){
  if(!value)return false;
  unsigned label=0,n=0;
  for(;n<=RISC_HTTP_TIME_SERVER_MAX && value[n];++n){
    const unsigned char c=value[n];
    if(c=='.'){
      if(!label || value[n-1]=='-')return false;
      label=0;
    }else{
      if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-') ||
         (!label && c=='-') || ++label>63u)return false;
    }
  }
  return n && n<=RISC_HTTP_TIME_SERVER_MAX && label && value[n-1]!='-';
}
inline uint32_t word(const uint8_t* p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
inline bool timestamp(const uint8_t* p,uint64_t& seconds){
  const uint64_t raw=word(p);
  if(!raw && !word(p+4))return false; // NTP's unspecified timestamp.
  const uint64_t first=HttpBounds::FirstUtc+NtpOffset,last=HttpBounds::LastUtc+NtpOffset;
  // The allowed 2024..2099 window is shorter than one era. Exactly one of
  // era0/era1 can match, including the 2036 rollover; never guess from RTC.
  const uint64_t ntp=raw>=first?raw:raw+Era;
  if(ntp<first || ntp>last)return false;
  seconds=ntp-NtpOffset;return true;
}
struct Sample {
  enum class Phase { Resolve,Send,Receive,Done };
  char server[RISC_HTTP_TIME_SERVER_MAX+1]{};
  sockaddr_in peer{};uint8_t packet[48]{};
  int fd=-1;bool retained=false,closeAttempted=false;
  Phase phase=Phase::Resolve;uint32_t rejected=0;uint64_t started=0;
  void begin(const char* hostname,uint64_t start){std::strcpy(server,hostname);started=start;}
  bool close(){
    if(retained)return false;
    if(fd==-1)return true;
    // A failed close may already have consumed/reused the descriptor. Keep
    // custody and the descriptor value for diagnosis; never close it again.
    if(fd<0 || closeAttempted){retained=true;return false;}
    closeAttempted=true;
    if(::close(fd)!=0){retained=true;return false;}
    fd=-1;return true;
  }
  bool alive(const HttpBounds::Budget& budget)const{
    const uint64_t t=now();return budget.alive(t) && t>=started && t-started<MaximumMs;
  }
  int32_t reject(){return ++rejected>=MaximumRejected?RISC_HTTP_CLOCK:RISC_HTTP_AGAIN;}
  int32_t step(const HttpBounds::Budget& budget,uint64_t& utc){
    if(retained)return RISC_HTTP_RETAINED;
    if(!alive(budget))return RISC_HTTP_TIMEOUT;
    if(phase==Phase::Resolve){
      addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;hints.ai_protocol=IPPROTO_UDP;
      addrinfo* found=nullptr;
      // The pinned SDK resolver is synchronous and bounded by its DNS retry
      // configuration, just like HTTPS DNS. Copy one address and release all
      // resolver storage before returning. Never retain a callback or input.
      const int result=::getaddrinfo(server,"123",&hints,&found);
      const bool valid=result==0 && found && found->ai_family==AF_INET && found->ai_addr &&
        found->ai_addrlen>=sizeof(peer) && found->ai_addr->sa_family==AF_INET;
      if(valid)std::memcpy(&peer,found->ai_addr,sizeof(peer));
      if(found)::freeaddrinfo(found);
      if(!alive(budget))return RISC_HTTP_TIMEOUT;
      if(!valid)return RISC_HTTP_CLOCK;
      peer.sin_port=htons(123);
      fd=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
      if(fd<0){fd=-1;return RISC_HTTP_TRANSPORT;}
      if(::fcntl(fd,F_SETFL,O_NONBLOCK)!=0)return RISC_HTTP_TRANSPORT;
      packet[0]=0x23; // No leap warning, NTPv4 client.
      ::esp_fill_random(packet+40,8);packet[40]|=0x80;
      phase=Phase::Send;return alive(budget)?RISC_HTTP_AGAIN:RISC_HTTP_TIMEOUT;
    }
    if(phase==Phase::Send){
      const ssize_t n=::sendto(fd,packet,sizeof(packet),0,reinterpret_cast<const sockaddr*>(&peer),sizeof(peer));
      if(!alive(budget))return RISC_HTTP_TIMEOUT;
      if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))return RISC_HTTP_AGAIN;
      if(n!=sizeof(packet))return RISC_HTTP_TRANSPORT;
      phase=Phase::Receive;return RISC_HTTP_AGAIN;
    }
    if(phase!=Phase::Receive)return RISC_HTTP_CLOCK;
    uint8_t reply[49]{};sockaddr_in source{};socklen_t length=sizeof(source);
    const ssize_t n=::recvfrom(fd,reply,sizeof(reply),0,reinterpret_cast<sockaddr*>(&source),&length);
    if(!alive(budget))return RISC_HTTP_TIMEOUT;
    if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))return RISC_HTTP_AGAIN;
    if(n<0)return RISC_HTTP_TRANSPORT;
    if(length!=sizeof(source) || source.sin_family!=AF_INET || source.sin_port!=peer.sin_port ||
       source.sin_addr.s_addr!=peer.sin_addr.s_addr)return reject();
    if(n!=48)return RISC_HTTP_CLOCK;
    if(std::memcmp(reply+24,packet+40,8))return reject();
    const unsigned version=(reply[0]>>3)&7u;
    if((reply[0]&7u)!=4u || (version!=3u && version!=4u) || (reply[0]>>6)==3u ||
       !reply[1] || reply[1]>15u)return RISC_HTTP_CLOCK;
    uint64_t received=0,transmitted=0;
    if(!timestamp(reply+32,received) || !timestamp(reply+40,transmitted) || transmitted<received ||
       transmitted-received>MaximumMs/1000u ||
       (transmitted==received && word(reply+44)<word(reply+36)))return RISC_HTTP_CLOCK;
    // TLS may start only after the sampler socket has certainly been closed.
    if(!close())return RISC_HTTP_RETAINED;
    if(!alive(budget))return RISC_HTTP_TIMEOUT;
    utc=transmitted;phase=Phase::Done;return RISC_HTTP_OK;
  }
};
} }
