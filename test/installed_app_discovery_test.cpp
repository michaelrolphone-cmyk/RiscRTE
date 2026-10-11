#include "runtime/packages/InstalledAppDiscovery.h"
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
using Scan=RiscPackages::InstalledAppDiscovery;
struct Fixture {
 risc_storage_volume_api_v1_ext v{};std::string mode,json=std::string(1024,'x');
 unsigned next=0,read=0,closes=0,dircloses=0,admissions=0;bool dir=false,file=false;
 Fixture(const char*m):mode(m){auto&b=v.base;b.api_version=1;b.struct_size=sizeof(v);b.context=this;
  b.ready=[](void*c){return static_cast<Fixture*>(c)->mode!="absent";};
  b.dir_open=[](void*c,const char*p)->uint32_t{auto&f=*static_cast<Fixture*>(c);assert(!strcmp(p,"/Apps"));return f.dir=true;};
  b.dir_next=[](void*c,uint32_t,risc_storage_dirent_v1*e){auto&f=*static_cast<Fixture*>(c);assert(f.dir);
   if(f.next++)return false;strcpy(e->name,"new-app");e->is_directory=1;return true;};
  b.file_open_read=[](void*c,const char*p,uint64_t*n)->uint32_t{auto&f=*static_cast<Fixture*>(c);assert(!strcmp(p,"/Apps/new-app/manifest.json"));
   if(f.mode=="missing")return 0;*n=f.mode=="oversized"?4097:f.json.size();f.file=true;return 1;};
  b.file_read=[](void*c,uint32_t,void*p,size_t n)->size_t{auto&f=*static_cast<Fixture*>(c);assert(f.file&&n<=512);
   if(f.mode=="truncated")return 0;n=n>97?97:n;memcpy(p,f.json.data()+f.read,n);f.read+=n;return n;};
  b.file_close=[](void*c,uint32_t,bool){auto&f=*static_cast<Fixture*>(c);++f.closes;
   if(f.mode=="close-retained")return false;f.file=false;return true;};
  v.dir_close_checked=[](void*c,uint32_t){auto&f=*static_cast<Fixture*>(c);++f.dircloses;
   if(f.mode=="directory-retained")return false;f.dir=false;return true;};
  v.handle_error=[](void*c,uint32_t,bool dir)->uint32_t{auto&f=*static_cast<Fixture*>(c);return f.mode==(dir?"directory-error":"read-error")?1:0;};
 }
 static bool admit(void*c,const char*p,const char*j){auto&f=*static_cast<Fixture*>(c);assert(!f.file&&f.closes==1);assert(!strcmp(p,"Apps/new-app/manifest.json"));assert(strlen(j)==f.json.size());++f.admissions;return f.mode!="rejected";}
};
int main(){
 for(const char*mode:{"normal","absent","missing","oversized","truncated","close-retained","directory-retained","directory-error","read-error","rejected","cancel"}){
  Fixture f(mode);Scan scan;scan.step();bool started=scan.start(&f.v.base,Fixture::admit,&f);
  if(f.mode=="absent"){assert(!started&&!f.dir);continue;}assert(started);
  unsigned steps=0;while(scan.state()!=Scan::State::Complete&&scan.state()!=Scan::State::Failed&&scan.state()!=Scan::State::Retained){assert(++steps<100);scan.step();if(f.mode=="cancel"&&scan.state()==Scan::State::Read)scan.cancel();}
  bool retained=f.mode=="close-retained"||f.mode=="directory-retained";
  bool complete=f.mode=="normal"||f.mode=="rejected";
  assert(scan.state()==(retained?Scan::State::Retained:complete?Scan::State::Complete:Scan::State::Failed));
  if(retained){unsigned closes=f.closes,dirs=f.dircloses;scan.cancel();scan.step();assert(f.closes==closes&&f.dircloses==dirs);}
  else assert(!f.dir&&!f.file);
  assert(scan.admitted()==(f.mode=="normal"||f.mode=="directory-error"||f.mode=="directory-retained"?1u:0u));
  // A complete scan result is essential before publishing/removing catalog rows.
  printf("discovery %s PASS\n",mode);
 }
}
