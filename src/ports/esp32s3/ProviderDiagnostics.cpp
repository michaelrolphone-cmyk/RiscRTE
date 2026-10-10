#include "ProviderDiagnostics.h"
#include "SleepDiagnostics.h"

extern "C" const uint32_t risc_provider_diagnostic_build_abi_v1=RISC_DIAGNOSTIC_ADAPTER?1u:0u;

extern "C" uint32_t risc_provider_diagnostic_abi_v1(void){
  return risc_provider_diagnostic_build_abi_v1;
}

#if RISC_DIAGNOSTIC_ADAPTER
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace {
constexpr size_t scanLimit=256,renderLimit=255;
constexpr unsigned conversionLimit=32;
constexpr int fieldLimit=64;
bool active=false;

struct Guard {
  Guard(){active=true;}
  ~Guard(){active=false;}
};

bool ready(){return RiscDiagnostics::providerDiagnosticReady() && !active;}

struct Record {
  char bytes[renderLimit+1]{};
  size_t size=0;
  bool truncated=false;
  void append(char byte){
    if(size<renderLimit)bytes[size++]=byte;
    else truncated=true;
  }
  void repeat(char byte,size_t count){while(count--)append(byte);}
  void append(const char* text,size_t count){
    for(size_t i=0;i<count;++i)append(text[i]);
  }
  int emit(){
    if(truncated){
      bytes[renderLimit-3]='.';bytes[renderLimit-2]='.';bytes[renderLimit-1]='.';
    }else if(size && bytes[size-1]=='\n'){
      --size;
      if(size && bytes[size-1]=='\r')--size;
    }
    for(size_t i=0;i<size;++i){
      const auto c=static_cast<unsigned char>(bytes[i]);
      if(c=='\r' || c=='\n' || c=='\t')bytes[i]=' ';
      else if(c<32 || c==127)bytes[i]='?';
    }
    bytes[size]='\0';
    RiscDiagnostics::line(bytes);
    return static_cast<int>(size);
  }
};

enum class Length {none,hh,h,l,ll,z,t,j};
struct Spec {
  bool left=false,plus=false,space=false,alternate=false,zero=false;
  bool widthArg=false,precisionArg=false;
  int width=0,precision=-1;
  Length length=Length::none;
  char conversion=0;
};
bool digit(char c){return c>='0' && c<='9';}
bool field(const char* format,size_t& at,int& value){
  value=0;
  while(digit(format[at])){
    value=value*10+(format[at++]-'0');
    if(value>fieldLimit)return false;
  }
  return true;
}
// The format has already passed the bounded NUL scan. This parser reads no
// arguments, so unsupported conversions reject before any %s can be read.
bool parse(const char* format,size_t& at,Spec& s){
  if(format[at]=='%'){s.conversion='%';++at;return true;}
  bool flags=true;
  while(flags){
    switch(format[at]){
      case '-':s.left=true;break;
      case '+':s.plus=true;break;
      case ' ':s.space=true;break;
      case '#':s.alternate=true;break;
      case '0':s.zero=true;break;
      default:flags=false;continue;
    }
    ++at;
  }
  if(format[at]=='*'){s.widthArg=true;++at;}
  else if(!field(format,at,s.width))return false;
  if(format[at]=='.'){
    ++at;
    if(format[at]=='*'){s.precisionArg=true;++at;}
    else if(!field(format,at,s.precision))return false;
  }
  switch(format[at]){
    case 'h':++at;s.length=Length::h;
      if(format[at]=='h'){++at;s.length=Length::hh;}break;
    case 'l':++at;s.length=Length::l;
      if(format[at]=='l'){++at;s.length=Length::ll;}break;
    case 'z':++at;s.length=Length::z;break;
    case 't':++at;s.length=Length::t;break;
    case 'j':++at;s.length=Length::j;break;
    default:break;
  }
  s.conversion=format[at];
  if(!s.conversion)return false;
  ++at;
  switch(s.conversion){
    case 'd':case 'i':case 'u':case 'o':case 'x':case 'X':return true;
    case 'c':
      return s.length==Length::none && !s.plus && !s.space &&
             !s.alternate && !s.zero && s.precision<0 && !s.precisionArg;
    case 's':
      return s.length==Length::none && !s.plus && !s.space &&
             !s.alternate && !s.zero;
    case 'p':return s.length==Length::none && !s.plus && !s.space;
    default:return false;
  }
}

bool validate(const char* input,char (&format)[scanLimit]){
  if(!input)return false;
  size_t length=0;
  // Retain a bounded local copy for every later parser pass. Neither a caller
  // changing its buffer nor a later scan can extend the accepted format.
  while(length<scanLimit){
    format[length]=input[length];
    if(!format[length])break;
    ++length;
  }
  if(length==scanLimit)return false;
  unsigned conversions=0;
  for(size_t at=0;at<length;){
    if(format[at++]!='%')continue;
    if(++conversions>conversionLimit)return false;
    Spec s;
    if(!parse(format,at,s))return false;
  }
  return true;
}

bool arguments(Spec& s,va_list& args){
  if(s.widthArg){
    const int width=va_arg(args,int);
    if(width < -fieldLimit || width > fieldLimit)return false;
    s.left=s.left || width<0;
    s.width=width<0?-width:width;
  }
  if(s.precisionArg){
    const int precision=va_arg(args,int);
    if(precision < -fieldLimit || precision > fieldLimit)return false;
    s.precision=precision<0?-1:precision;
  }
  return true;
}

intmax_t signedValue(Length length,va_list& args){
  switch(length){
    case Length::hh:return static_cast<signed char>(va_arg(args,int));
    case Length::h:return static_cast<short>(va_arg(args,int));
    case Length::l:return va_arg(args,long);
    case Length::ll:return va_arg(args,long long);
    case Length::z:return va_arg(args,typename std::make_signed<size_t>::type);
    case Length::t:return va_arg(args,ptrdiff_t);
    case Length::j:return va_arg(args,intmax_t);
    default:return va_arg(args,int);
  }
}
uintmax_t unsignedValue(Length length,va_list& args){
  switch(length){
    case Length::hh:return static_cast<unsigned char>(va_arg(args,unsigned int));
    case Length::h:return static_cast<unsigned short>(va_arg(args,unsigned int));
    case Length::l:return va_arg(args,unsigned long);
    case Length::ll:return va_arg(args,unsigned long long);
    case Length::z:return va_arg(args,size_t);
    case Length::t:return va_arg(args,typename std::make_unsigned<ptrdiff_t>::type);
    case Length::j:return va_arg(args,uintmax_t);
    default:return va_arg(args,unsigned int);
  }
}

// Preflight all dynamic fields without dereferencing string/pointer arguments.
// A later invalid field therefore cannot leak a prefix or touch an earlier %s.
bool preflight(const char* format,va_list& args){
  for(size_t at=0;format[at];){
    if(format[at++]!='%')continue;
    Spec s;
    if(!parse(format,at,s) || !arguments(s,args))return false;
    switch(s.conversion){
      case '%':break;
      case 'd':case 'i':(void)signedValue(s.length,args);break;
      case 'u':case 'o':case 'x':case 'X':(void)unsignedValue(s.length,args);break;
      case 's':(void)va_arg(args,const char*);break;
      case 'p':(void)va_arg(args,void*);break;
      case 'c':(void)va_arg(args,int);break;
      default:return false;
    }
  }
  return true;
}

void integer(Record& record,const Spec& s,uintmax_t value,bool negative){
  const bool pointer=s.conversion=='p';
  const bool signedNumber=s.conversion=='d' || s.conversion=='i';
  const unsigned base=s.conversion=='o'?8:
                      (s.conversion=='x' || s.conversion=='X' || pointer?16:10);
  const char* alphabet=s.conversion=='X'?"0123456789ABCDEF":"0123456789abcdef";
  char reversed[sizeof(uintmax_t)*8];
  size_t digits=0;
  const uintmax_t original=value;
  if(value || s.precision!=0 || pointer){
    do{reversed[digits++]=alphabet[value%base];value/=base;}while(value);
  }
  size_t zeros=s.precision>static_cast<int>(digits)?size_t(s.precision)-digits:0;
  char prefix[2];size_t prefixSize=0;
  if(signedNumber){
    if(negative)prefix[prefixSize++]='-';
    else if(s.plus)prefix[prefixSize++]='+';
    else if(s.space)prefix[prefixSize++]=' ';
  }else if(pointer || (s.alternate && base==16 && original)){
    prefix[prefixSize++]='0';prefix[prefixSize++]=s.conversion=='X'?'X':'x';
  }
  if(s.alternate && base==8 && !zeros && (!digits || reversed[digits-1]!='0'))++zeros;
  const size_t length=prefixSize+zeros+digits;
  size_t padding=s.width>static_cast<int>(length)?size_t(s.width)-length:0;
  if(s.zero && !s.left && s.precision<0){zeros+=padding;padding=0;}
  if(!s.left)record.repeat(' ',padding);
  record.append(prefix,prefixSize);
  record.repeat('0',zeros);
  while(digits)record.append(reversed[--digits]);
  if(s.left)record.repeat(' ',padding);
}

void textValue(Record& record,const Spec& s,const char* text){
  if(!text)text="(null)";
  const size_t limit=s.precision<0?scanLimit:size_t(s.precision);
  size_t count=0;
  while(count<limit && text[count])++count;
  const size_t padding=s.width>static_cast<int>(count)?size_t(s.width)-count:0;
  if(!s.left)record.repeat(' ',padding);
  record.append(text,count);
  if(s.left)record.repeat(' ',padding);
}

void render(Record& record,const char* format,va_list& args){
  for(size_t at=0;format[at];){
    if(format[at]!='%'){record.append(format[at++]);continue;}
    ++at;Spec s;
    (void)parse(format,at,s);(void)arguments(s,args);
    switch(s.conversion){
      case '%':record.append('%');break;
      case 'd':case 'i':{
        const intmax_t value=signedValue(s.length,args);
        const uintmax_t magnitude=value<0?uintmax_t(0)-uintmax_t(value):uintmax_t(value);
        integer(record,s,magnitude,value<0);break;
      }
      case 'u':case 'o':case 'x':case 'X':integer(record,s,unsignedValue(s.length,args),false);break;
      case 'p':integer(record,s,reinterpret_cast<uintptr_t>(va_arg(args,void*)),false);break;
      case 's':textValue(record,s,va_arg(args,const char*));break;
      case 'c':{
        const char value=static_cast<unsigned char>(va_arg(args,int));
        const size_t padding=s.width>1?size_t(s.width)-1:0;
        if(!s.left)record.repeat(' ',padding);
        record.append(value);
        if(s.left)record.repeat(' ',padding);
        break;
      }
      default:break; // validate/preflight excluded every other conversion.
    }
  }
}
}

extern "C" int risc_provider_diagnostic_printf(const char* format,...){
  if(!ready())return -1;
  Guard guard;
  char ownedFormat[scanLimit];
  if(!validate(format,ownedFormat))return -1;
  va_list args;va_start(args,format);
  va_list checked;va_copy(checked,args);
  const bool valid=preflight(ownedFormat,checked);
  va_end(checked);
  if(!valid){va_end(args);return -1;}
  Record record;
  render(record,ownedFormat,args);
  va_end(args);
  return record.emit();
}

extern "C" int risc_provider_diagnostic_puts(const char* text){
  if(!ready())return -1;
  Guard guard;
  if(!text)return -1;
  Record record;Spec s;
  textValue(record,s,text);
  return record.emit();
}

extern "C" int risc_provider_diagnostic_putchar(int value){
  if(!ready())return -1;
  Guard guard;
  const auto byte=static_cast<unsigned char>(value);
  Record record;record.append(static_cast<char>(byte));
  record.emit();
  return byte;
}
#else
extern "C" int risc_provider_diagnostic_printf(const char*,...){return -1;}
extern "C" int risc_provider_diagnostic_puts(const char*){return -1;}
extern "C" int risc_provider_diagnostic_putchar(int){return -1;}
#endif
