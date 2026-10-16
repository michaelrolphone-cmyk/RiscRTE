#include "runtime/storage/FileSourceFiles.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
namespace {
bool owner=true,safe=true;
bool own(){return owner;}bool ready(){return safe;}
struct Provider {
 risc_storage_files_v1 api{};std::vector<unsigned char> bytes;bool exists=false,opened=false;
 uint32_t generation=0,token=0;uint64_t offset=0;unsigned calls=0,closes=0,removes=0;
 int32_t openResult=0,readResult=0,writeResult=0,closeResult=0,removeResult=0;
 bool shortRead=false,oversize=false,unsafeInfo=false;
 static Provider& self(void*c){return *static_cast<Provider*>(c);}
 Provider(){api={1,sizeof(api),this,
  [](void*c,const char*p,uint32_t f,uint32_t*h)->int32_t{auto&s=self(c);++s.calls;assert(!strcmp(p,"/.risc-file-source"));*h=0;if(s.openResult)return s.openResult;if(s.opened)return RISC_FILES_BUSY;if(f&RISC_FILES_CREATE){if(s.exists)return RISC_FILES_EXISTS;s.exists=true;s.bytes.clear();}if(!s.exists)return RISC_FILES_NOT_FOUND;s.opened=true;s.offset=0;*h=s.token=++s.generation;return 0;},
  [](void*c,uint32_t h)->int32_t{auto&s=self(c);++s.calls;++s.closes;assert(s.opened&&h==s.token);if(s.closeResult)return s.closeResult;s.opened=false;return 0;},
  [](void*c,uint32_t h,void*b,uint32_t n,uint32_t*a)->int32_t{auto&s=self(c);++s.calls;assert(s.opened&&h==s.token);*a=std::min<uint64_t>(n,s.bytes.size()-s.offset);if(s.shortRead&&*a)--*a;memcpy(b,s.bytes.data()+s.offset,*a);s.offset+=*a;if(s.oversize)*a=n+1;return s.readResult;},
  [](void*c,uint32_t h,const void*b,uint32_t n,uint32_t*a)->int32_t{auto&s=self(c);++s.calls;assert(s.opened&&h==s.token);*a=0;if(s.writeResult)return s.writeResult;s.bytes.insert(s.bytes.end(),static_cast<const unsigned char*>(b),static_cast<const unsigned char*>(b)+n);s.offset+=n;*a=n;return 0;},
  [](void*c,uint32_t h,uint64_t o)->int32_t{auto&s=self(c);++s.calls;assert(s.opened&&h==s.token);if(o>s.bytes.size())return RISC_FILES_INVALID;s.offset=o;return 0;},
  [](void*c,uint32_t h,uint64_t*n,uint64_t*o)->int32_t{auto&s=self(c);++s.calls;assert(s.opened&&h==s.token);*n=s.bytes.size();*o=s.offset;if(s.unsafeInfo)safe=false;return 0;},
  [](void*,uint32_t)->int32_t{return RISC_FILES_UNAVAILABLE;},
  [](void*,uint32_t,uint64_t)->int32_t{return RISC_FILES_UNAVAILABLE;},
  [](void*,const char*,risc_storage_files_stat_v1*)->int32_t{return RISC_FILES_UNAVAILABLE;},
  [](void*,const char*)->int32_t{return RISC_FILES_UNAVAILABLE;},
  [](void*,const char*,const char*)->int32_t{return RISC_FILES_UNAVAILABLE;},
  [](void*c,const char*p)->int32_t{auto&s=self(c);++s.calls;++s.removes;assert(!strcmp(p,"/.risc-file-source"));if(s.removeResult)return s.removeResult;if(!s.exists)return RISC_FILES_NOT_FOUND;s.exists=false;s.bytes.clear();return 0;},
  [](void*,const char*,uint32_t*)->int32_t{return RISC_FILES_UNAVAILABLE;},
  [](void*,uint32_t,risc_storage_files_entry_v1*)->int32_t{return RISC_FILES_UNAVAILABLE;},
  [](void*,uint32_t)->int32_t{return RISC_FILES_UNAVAILABLE;},
  [](void*,uint32_t)->int32_t{return RISC_FILES_UNAVAILABLE;},
  [](void*,uint32_t,uint64_t*)->int32_t{return RISC_FILES_UNAVAILABLE;},
  [](void*,uint32_t,uint64_t)->int32_t{return RISC_FILES_UNAVAILABLE;}};}
};
struct Fixture {
 Provider provider;RiscBoot::RuntimeFiles session;RiscStorage::FileSourceFiles source{{nullptr,[](void*){return owner;},[](void*){return safe;}}};
 Fixture(){assert(session.bind(&provider.api,own,ready));assert(source.configure(&session,"/.risc-file-source"));}
 void sealed(){uint32_t n=0;assert(source.begin(8)==0);assert(source.write("01234567",8,&n)==0&&n==8);assert(source.finish()==0);}
 void frozen(){auto count=provider.calls;uint32_t n=99;char b=42;assert(source.discard()==RISC_FILE_SOURCE_RETAINED);assert(source.begin(1)==RISC_FILE_SOURCE_RETAINED);assert(source.write(&b,1,&n)==RISC_FILE_SOURCE_RETAINED&&!n);assert(source.finish()==RISC_FILE_SOURCE_RETAINED);assert(source.readAt(0,&b,1,&n)==RISC_FILE_SOURCE_RETAINED&&!n&&b==42);assert(count==provider.calls&&!source.exitSafe()&&session.retained());}
};
}
int main(){
 {RiscStorage::FileSourceFiles f({nullptr,[](void*){return true;},[](void*){return true;}});RiscBoot::RuntimeFiles empty;assert(!f.configure(nullptr,"/.risc-file-source")&&!f.configure(&empty,"/.risc-file-source"));assert(f.begin(8)==RISC_FILE_SOURCE_UNAVAILABLE);}
 {Provider p;RiscBoot::RuntimeFiles session;assert(session.bind(&p.api,own,ready));RiscStorage::FileSourceFiles f({nullptr,[](void*){return owner;},[](void*){return safe;}});assert(!f.configure(&session,"/foreign")&&!f.configure(&session,"/../.risc-file-source")&&!p.calls);}
 {Fixture f;const auto calls=f.provider.calls;assert(!f.source.configure(&f.session,"/other"));assert(calls==f.provider.calls);f.sealed();uint32_t n=0;char b[8]{};assert(f.source.readAt(0,b,8,&n)==0&&n==8&&!memcmp(b,"01234567",8));assert(f.source.discard()==0&&f.source.exitSafe()&&!f.provider.exists);}
 for(int result:{int(RISC_FILES_UNAVAILABLE),int(RISC_FILES_NO_SPACE),123456}){Fixture f;f.provider.openResult=result;assert(f.source.begin(8)==(result==RISC_FILES_UNAVAILABLE?RISC_FILE_SOURCE_UNAVAILABLE:result==RISC_FILES_NO_SPACE?RISC_FILE_SOURCE_NO_SPACE:RISC_FILE_SOURCE_IO));assert(!f.provider.exists&&f.source.exitSafe());}
 {Fixture f;assert(f.source.begin(8)==0);f.provider.writeResult=RISC_FILES_NO_SPACE;uint32_t n=1;assert(f.source.write("01234567",8,&n)==RISC_FILE_SOURCE_NO_SPACE&&!n);assert(f.source.discard()==0);}
 // Semantic integrity faults retain the session before any close or cleanup.
 for(unsigned fault=0;fault<6;++fault){Fixture f;f.sealed();if(fault==0)f.provider.bytes.push_back('x');if(fault==1)f.provider.shortRead=true;if(fault==2)f.provider.oversize=true;if(fault==3)f.provider.closeResult=RISC_FILES_IO;if(fault==4)f.provider.unsafeInfo=true;if(fault==5)f.provider.readResult=RISC_FILES_RETAINED;const auto closes=f.provider.closes;std::array<char,8> out;out.fill(42);uint32_t n=99;assert(f.source.readAt(0,out.data(),8,&n)==RISC_FILE_SOURCE_RETAINED&&!n);for(auto c:out)assert(c==42);safe=true;assert(f.provider.closes==closes+(fault==3));f.frozen();}
 {Fixture f;assert(f.source.begin(8)==0);f.provider.bytes.push_back('x');auto closes=f.provider.closes;uint32_t n=99;assert(f.source.write("01234567",8,&n)==RISC_FILE_SOURCE_RETAINED&&!n&&f.provider.closes==closes);f.frozen();}
 {Fixture f;f.sealed();f.provider.removeResult=RISC_FILES_RETAINED;assert(f.source.discard()==RISC_FILE_SOURCE_RETAINED);f.frozen();}
 puts("Generic scratch provider: private namespace, absent provider, NO_SPACE, unknown result, changed-size, short read, malformed count, failed close, lost custody and no retry PASS");
}
