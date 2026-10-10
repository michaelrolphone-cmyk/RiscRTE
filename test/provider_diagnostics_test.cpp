#include "ports/esp32s3/ProviderDiagnostics.h"
#include "ports/esp32s3/SleepDiagnostics.h"
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#if RISC_DIAGNOSTIC_ADAPTER
namespace {
bool owner=true,isr=false,outputting=false,draining=false,discard=false,reenter=false;
unsigned calls=0;
std::vector<std::string> records;
const char* badPointer(){return reinterpret_cast<const char*>(uintptr_t(1));}

template<typename... Args>
void expect(const std::string& expected,const char* format,Args... args){
  const auto before=calls;
  const int result=risc_provider_diagnostic_printf(format,args...);
  assert(result==static_cast<int>(expected.size()));
  assert(calls==before+1);
  assert(records.back()==expected);
}
template<typename... Args>
void reject(const char* format,Args... args){
  const auto before=calls;
  assert(risc_provider_diagnostic_printf(format,args...)==-1);
  assert(calls==before);
}
template<typename... Args>
void compare(const char* format,Args... args){
  char expected[256];
  const int size=std::snprintf(expected,sizeof(expected),format,args...);
  assert(size>=0 && size<256);
  expect(expected,format,args...);
}
void contextRejection(){
  const auto before=calls;
  assert(risc_provider_diagnostic_printf(badPointer())==-1);
  assert(risc_provider_diagnostic_printf("%s",badPointer())==-1);
  assert(risc_provider_diagnostic_puts(badPointer())==-1);
  assert(risc_provider_diagnostic_putchar('x')==-1);
  assert(calls==before);
}
}

namespace RiscDiagnostics {
bool providerDiagnosticReady(){return owner && !isr && !outputting && !draining;}
void line(const char* text){
  ++calls;
  // Deliberately leave the readiness hook true to test the adapter's guard
  // independently of the real sink's own output/drain reentry checks.
  if(reenter){
    reenter=false;
    contextRejection();
  }
  if(!discard)records.emplace_back(text);
}
}

static void integers(){
  expect("-42 17 19 17 ff FF","%d %i %u %o %x %X",-42,17,19u,15u,255u,255u);
  for(const char* format:{"%d","%+d","% d","%08d","%-8d","%8.4d","%+08d",
                          "% 08d","%-08d","%08.4d","%.0d","%+.0d","% .0d",
                          "%#d","%+ d","%--++ 008.4d"}){
    for(int value:{INT_MIN,-123,-1,0,1,123,INT_MAX})compare(format,value);
  }
  for(const char* format:{"%u","%o","%x","%X","%#x","%#X","%#o","%#08x",
                          "%#08o","%#.0o","%#.0x","%#.3o","%#8.3o","%#-8.3x",
                          "%#08.3x","%+u","% u","%.0u","%.64x","%64u"}){
    for(unsigned value:{0u,1u,7u,8u,16u,0xffu,UINT_MAX})compare(format,value);
  }
  compare("%hhd %hd %ld %lld %jd",static_cast<int>(SCHAR_MIN),
          static_cast<int>(SHRT_MIN),LONG_MIN,LLONG_MIN,INTMAX_MIN);
  compare("%hhu %hu %lu %llu %ju",static_cast<unsigned>(UCHAR_MAX),
          static_cast<unsigned>(USHRT_MAX),ULONG_MAX,ULLONG_MAX,UINTMAX_MAX);
  compare("%hhd %hd %hhu %hu",257,65537,257u,65537u);
  using SignedSize=typename std::make_signed<size_t>::type;
  using UnsignedDifference=typename std::make_unsigned<ptrdiff_t>::type;
  compare("%zd %zi %zu",std::numeric_limits<SignedSize>::min(),SignedSize(-1),SIZE_MAX);
  compare("%td %ti %tu",PTRDIFF_MIN,ptrdiff_t(-1),std::numeric_limits<UnsignedDifference>::max());
  compare("%#llx %#jo %#zx %#tx",ULLONG_MAX,UINTMAX_MAX,SIZE_MAX,
          std::numeric_limits<UnsignedDifference>::max());
  compare("%*.*d",8,4,-42);
  compare("%*.*d",-8,4,42);
  compare("%0*.*d",8,-1,42);
  compare("%*.*d",64,64,-42);
  compare("%*.*d",-64,-64,42);
  expect("0x0 0x12ab","%p %p",static_cast<void*>(nullptr),reinterpret_cast<void*>(uintptr_t(0x12ab)));
  expect("0x00002a","%08p",reinterpret_cast<void*>(uintptr_t(0x2a)));
  expect("  0x002a","%8.4p",reinterpret_cast<void*>(uintptr_t(0x2a)));
  expect("0x2a    ","%-8p",reinterpret_cast<void*>(uintptr_t(0x2a)));
  expect("0x0","%#.0p",static_cast<void*>(nullptr));
}

static void stringsAndRecords(){
  expect("plain","plain");
  expect("","%s","");
  expect("","");
  expect("100%","100%%");
  compare("[%8.3s][%-8s][%c][%3c][%-3c]","abcdef","abc",'A','B','C');
  compare("[%*.*s]",-8,3,"abcdef");
  expect("(null)","%s",static_cast<const char*>(nullptr));
  expect("(nu","%.3s",static_cast<const char*>(nullptr));
  expect("","%.0s",badPointer());
  expect("","%.*s",0,badPointer());
  const char exact[3]={'a','b','c'};
  expect("abc","%.3s",exact);
  expect("hello","hello\n");
  expect("hello","%s","hello\r\n");
  expect("a b c d?e?f","a\nb\rc\td%ce%cf",0,127);
  expect("x?y","x%cy",1);
  expect("x y","x%cy",'\n');
  expect("x y","x%cy",'\r');
  expect("\xc3\xa9","%s","\xc3\xa9");
  expect(" ","\n\n");
  const auto before=calls;
  assert(risc_provider_diagnostic_puts("puts\r\n")==4);
  assert(records.back()=="puts");
  assert(risc_provider_diagnostic_puts("")==0);
  assert(records.back().empty());
  assert(risc_provider_diagnostic_puts(nullptr)==-1);
  assert(calls==before+2);
  const auto start=records.size();
  assert(risc_provider_diagnostic_putchar('a')=='a');
  assert(risc_provider_diagnostic_putchar('b')=='b');
  assert(risc_provider_diagnostic_putchar('\n')=='\n');
  assert(risc_provider_diagnostic_putchar('\r')=='\r');
  assert(risc_provider_diagnostic_putchar('\t')=='\t');
  assert(risc_provider_diagnostic_putchar(0)==0);
  assert(risc_provider_diagnostic_putchar(127)==127);
  assert(risc_provider_diagnostic_putchar(0x141)=='A');
  assert(risc_provider_diagnostic_putchar(-1)==UCHAR_MAX);
  assert(records.size()==start+9);
  assert(records[start]=="a" && records[start+1]=="b" && records[start+2].empty());
  assert(records[start+3]==" " && records[start+4]==" ");
  assert(records[start+5]=="?" && records[start+6]=="?" && records[start+7]=="A");
  assert(records[start+8]==std::string(1,static_cast<char>(UCHAR_MAX)));
}

static void limitsAndRejections(){
  char boundaryFormat[256];std::memset(boundaryFormat,'f',sizeof(boundaryFormat));
  reject(boundaryFormat);
  boundaryFormat[255]=0;
  expect(std::string(255,'f'),boundaryFormat);
  char boundaryString[256];std::memset(boundaryString,'s',sizeof(boundaryString));
  expect(std::string(252,'s')+"...","%s",boundaryString);
  assert(risc_provider_diagnostic_puts(boundaryString)==255);
  assert(records.back()==std::string(252,'s')+"...");
  boundaryString[255]=0;
  expect(std::string(255,'s'),"%s",boundaryString);
  expect(std::string(252,'s')+"...","%s!",boundaryString);
  expect(std::string(252,' ')+"...","%64s%64s%64s%64s","","","","");
  expect(std::string(64,'s'),"%.64s",boundaryString);
  std::string format;
  for(unsigned i=0;i<32;++i)format+="%%";
  expect(std::string(32,'%'),format.c_str());
  format+="%%";reject(format.c_str());
  for(const char* malformed:{"%","%h","%ll","%j","%.","%*","%**d","%q",
                             "%f","%F","%e","%E","%g","%G","%a","%A","%n",
                             "%S","%C","%m","%ls","%lc","%Ld","%llld","%hs",
                             "%1$d","%*1$d","%.*1$s","%.1$s","%5%","%+s","%05s",
                             "%#c","%.1c","%.*c","%+p","%lp","%65d","%.65s",
                             "%2147483647d","%.99999999999999999d","%9999999999999999u"}){
    reject(malformed);
  }
  reject(nullptr);
  reject("prefix %s %n",badPointer(),reinterpret_cast<int*>(uintptr_t(1)));
  reject("prefix %s %f",badPointer(),1.0);
  for(int field:{65,-65,INT_MAX,INT_MIN}){
    reject("prefix %s %*d",badPointer(),field,1);
    reject("prefix %s %.*d",badPointer(),field,1);
    reject("%*s",field,badPointer());
    reject("%.*s",field,badPointer());
  }
  // A rejected call releases the guard and does not retain any prefix.
  expect("next","next");
}

static void admissionAndLoss(){
  owner=false;contextRejection();owner=true;
  isr=true;contextRejection();isr=false;
  outputting=true;contextRejection();outputting=false;
  draining=true;contextRejection();draining=false;
  reenter=true;expect("outer","outer");
  assert(!reenter);
  const auto before=records.size();const auto callCount=calls;
  discard=true;
  assert(risc_provider_diagnostic_printf("dropped %d",123)==11);
  assert(risc_provider_diagnostic_puts("dropped")==7);
  assert(risc_provider_diagnostic_putchar('z')=='z');
  assert(records.size()==before && calls==callCount+3);
  discard=false;
  expect("after loss","after loss");
}
#endif

int main(){
#if RISC_DIAGNOSTIC_ADAPTER
  assert(risc_provider_diagnostic_build_abi_v1==1);
  assert(risc_provider_diagnostic_abi_v1()==1);
  integers();stringsAndRecords();limitsAndRejections();admissionAndLoss();
#else
  // No definitions of line/ready are linked into this profile. Even malformed
  // addresses must be harmless when the underlying adapter is unavailable.
  assert(risc_provider_diagnostic_build_abi_v1==0);
  assert(risc_provider_diagnostic_abi_v1()==0);
  const auto invalid=reinterpret_cast<const char*>(uintptr_t(1));
  assert(risc_provider_diagnostic_printf(invalid)==-1);
  assert(risc_provider_diagnostic_printf("%s",invalid)==-1);
  assert(risc_provider_diagnostic_puts(invalid)==-1);
  assert(risc_provider_diagnostic_putchar('x')==-1);
#endif
}
