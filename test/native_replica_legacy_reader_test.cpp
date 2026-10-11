#include "native_replica_sd_fixture.h"
#include "experimental/native_replica/CheckedLegacySettingsReader.h"
using L=RiscExperimental::CheckedLegacySettingsReader;
static uint32_t crc(const uint8_t*p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
static void seal(Bytes&b){put32(b.data()+b.size()-4,crc(b.data(),b.size()-4));}
static Bytes legacy(unsigned count,bool v1=false,unsigned value=32){
 Bytes b(v1?128+84*count:132+(20+value)*count);memcpy(b.data(),v1?"RISCKV1":"RISCKV2",8);put32(b.data()+8,6);put32(b.data()+16,7);strcpy(reinterpret_cast<char*>(b.data()+24),"shared-00000006");put32(b.data()+120,count);if(!v1)put32(b.data()+124,b.size());
 for(unsigned i=0;i<count;++i){auto at=(v1?124:128)+i*(v1?84:20+value);char key[16];snprintf(key,sizeof key,"k%05u",i);strcpy(reinterpret_cast<char*>(b.data()+at),key);put32(b.data()+at+16,value);memset(b.data()+at+20,'v',value);}seal(b);return b;
}
static std::string selected(L::Source s){return std::string("/RTECFG/00000006/")+(s==L::Source::PagedV2?"settings.index":"settings.kv");}
static std::string legacyPage(unsigned page){char p[96];snprintf(p,sizeof p,"/RTECFG/00000006/kv-0000000000000007-%08x",page);return p;}
static void storeLegacy(Files&f,const Bytes&b,L::Source source){if(source==L::Source::SingleFile){f[selected(source)]=b;return;}Bytes index(28);memcpy(index.data(),"RTEKVP2",8);put32(index.data()+8,b.size());put32(index.data()+12,7);put32(index.data()+20,crc(b.data(),b.size()));seal(index);f[selected(source)]=index;for(size_t at=0;at<b.size();at+=4096)f[legacyPage(at/4096)]={b.begin()+at,b.begin()+std::min(at+4096,b.size())};}
struct WipingAlloc {
 bool fail=false;unsigned allocations=0;size_t peak=0,live=0;std::map<void*,size_t> pointers;
 C::Allocator api(){return {this,[](void*p,size_t n)->void*{auto&a=*static_cast<WipingAlloc*>(p);++a.allocations;if(a.fail)return nullptr;auto*out=malloc(n);if(out){a.pointers[out]=n;a.live+=n;a.peak=std::max(a.peak,a.live);memset(out,0xa7,n);}return out;},[](void*p,void*out){auto&a=*static_cast<WipingAlloc*>(p);size_t n=a.pointers.at(out);for(size_t i=0;i<n;++i)assert(static_cast<uint8_t*>(out)[i]==0);a.live-=n;a.pointers.erase(out);free(out);}};}
 ~WipingAlloc(){assert(!live&&pointers.empty());}
};
static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
static Bytes canonical(const Bytes&b){bool v1=!memcmp(b.data(),"RISCKV1",8);Bytes out;unsigned at=v1?124:128;for(unsigned i=0;i<word(b.data()+120);++i){unsigned n=word(b.data()+at+16),dst=out.size();out.resize(dst+20+n);strcpy(reinterpret_cast<char*>(out.data()+dst),reinterpret_cast<const char*>(b.data()+at));put32(out.data()+dst+16,n);memcpy(out.data()+dst+20,b.data()+at+20,n);at+=v1?84:20+n;}return out;}
static Bytes fingerprint(const Files&f,L::Source source,const Bytes&b){Bytes in={'R','T','E','L','E','G','A','C','Y','S','R','C',1,0,0,0};for(uint32_t n:{6u,uint32_t(source),!memcmp(b.data(),"RISCKV1",8)?1u:2u,source==L::Source::PagedV2?28u:0u,uint32_t(b.size())}){auto at=in.size();in.resize(at+4);put32(in.data()+at,n);}if(source==L::Source::PagedV2){const auto&i=f.at(selected(source));in.insert(in.end(),i.begin(),i.end());}in.insert(in.end(),b.begin(),b.end());Bytes out(32);unsigned n=0;assert(EVP_Digest(in.data(),in.size(),out.data(),&n,EVP_sha256(),nullptr)==1&&n==32);return out;}
static void noWrites(const Sd&s){for(auto&op:s.operations)assert(op!="open-write"&&op!="write"&&op!="remove"&&op!="sync"&&op.find("replace-")!=0);assert(!s.mutations&&!s.indexReplacements);}
static void nativeUnchanged(C&c,Native&n,unsigned calls){assert(n.calls==calls&&c.pendingCount()==1&&get(c,"edited")=="local");}
static void readGood(Files&f,L::Source source,const Bytes&b,unsigned nativeCapacity=65512){Native n;C c(6,n.api(),{nativeCapacity,4096,1024},C::heap());const Files before=f;Sd s(f);Hash h;WipingAlloc a;L::Candidate out;const Bytes want=canonical(b),sha=fingerprint(f,source,b);
 {L l(c,h.api(),s.guard(),a.api());assert(l.start(s.volume(),source)==0);assert(l.take(&out)==C::Stale);for(unsigned i=0;i<5;++i)s.tick(l,false);assert(s.io==0);s.run(l);assert(l.state()==L::State::Verified&&s.ends&&!l.exitSafe());assert(l.take(&out)==0&&out.valid()&&l.exitSafe());assert(out.namespaceId()==6&&out.count()==word(b.data()+120)&&out.mapSize()==want.size()&&out.sourceSize()==b.size()&&out.generation()==7&&out.source()==source);assert(out.format()==(!memcmp(b.data(),"RISCKV1",8)?L::Format::V1:L::Format::V2));assert(!memcmp(out.sourceDigest(),sha.data(),32));}
 assert((want.empty()||!memcmp(out.map(),want.data(),want.size()))&&f==before&&n.calls==0);noWrites(s);out.reset();assert(!a.live);
}
static void basics(){
 for(auto source:{L::Source::PagedV2,L::Source::SingleFile}){Files f;Bytes b=legacy(source==L::Source::PagedV2?1025:16,source==L::Source::SingleFile);storeLegacy(f,b,source);readGood(f,source,b);}
 for(bool v1:{false,true}){for(unsigned count:{0u,1u,16u}){Files f;Bytes b=legacy(count,v1,v1?64:128);const unsigned start=v1?124:128; // Production accepts padding; exact fingerprint must keep it.
   b[119]=0x9d;for(unsigned i=0;i<count;++i)b[start+i*(v1?84:148)+15]=0xa7;seal(b);storeLegacy(f,b,L::Source::SingleFile);readGood(f,L::Source::SingleFile,b);}}
 // V1 fixed tails need not fit the canonical native map budget.
 {Files f;auto b=legacy(16,true,1);for(unsigned i=0;i<16;++i)memset(b.data()+124+i*84+21,0xcf,63);seal(b);storeLegacy(f,b,L::Source::SingleFile);readGood(f,L::Source::SingleFile,b,C::Header+4+16*21);}
 // Logical order need not be sorted. A 2048-byte value is preserved exactly.
 {Files f;auto b=legacy(2,false,2048);std::swap_ranges(b.begin()+128,b.begin()+144,b.begin()+2196);seal(b);storeLegacy(f,b,L::Source::PagedV2);readGood(f,L::Source::PagedV2,b);}
 // Exact source sizes 4096/4097/8192 split the CRC or value across pages.
 for(unsigned total:{4096u,4097u,8192u}){Bytes b=legacy(4,false,2000);unsigned mapBytes=total-132,first=mapBytes/4-20;unsigned at=128;Bytes exact(total);memcpy(exact.data(),b.data(),128);put32(exact.data()+124,total);for(unsigned i=0;i<4;++i){unsigned n=i==3?total-4-at-20:first;assert(n&&n<=2048);char k[16];snprintf(k,sizeof k,"p%u",i);strcpy(reinterpret_cast<char*>(exact.data()+at),k);put32(exact.data()+at+16,n);memset(exact.data()+at+20,'x',n);at+=20+n;}assert(at==total-4);seal(exact);Files f;storeLegacy(f,exact,L::Source::PagedV2);readGood(f,L::Source::PagedV2,exact);}
 puts("Legacy reader: 1025-key V2, V1 fixed tails, permitted padding, empty/unsorted/max values, page/CRC boundary framing, owned canonical map and exact-source SHA PASS");
}
static void malformed(){unsigned cases=0;
 for(unsigned mode=0;mode<45;++mode){Native n;Files f;seed(n,f);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);const unsigned calls=n.calls;const bool v1=mode>=30;auto source=v1?L::Source::SingleFile:L::Source::PagedV2;Bytes b=legacy(v1?2:100,v1);const unsigned at=v1?124:128;L::Reason want=L::Reason::InvalidContent;
  switch(mode){
   case 0:b[0]='X';break;case 1:b[7]=1;break;case 2:put32(b.data()+8,7);break;case 3:put32(b.data()+12,1);break;
   case 4:b[24]='X';break;case 5:memset(b.data()+24,'a',96);break;case 6:put32(b.data()+16,8);break;
   case 7:put32(b.data()+120,99);break;case 8:put32(b.data()+120,101);break;case 9:put32(b.data()+120,UINT32_MAX);break;
   case 10:put32(b.data()+124,b.size()-1);break;case 11:b[at]=0;break;case 12:b[at]='A';break;case 13:memset(b.data()+at,'a',16);break;
   case 14:put32(b.data()+at+16,0);break;case 15:put32(b.data()+at+16,2049);break;case 16:put32(b.data()+at+16,UINT32_MAX);break;
   case 17:memcpy(b.data()+at+52,b.data()+at,16);b[at+52+15]=0xaa;break; // duplicate logical name, different padding
   case 18:b.pop_back();put32(b.data()+124,b.size());break;case 19:b.insert(b.end()-4,0);put32(b.data()+124,b.size());break;
   case 30:put32(b.data()+12,1);break;case 31:put32(b.data()+12,2);break;case 32:put32(b.data()+120,17);break;
   case 33:put32(b.data()+at+16,0);break;case 34:put32(b.data()+at+16,65);break;case 35:put32(b.data()+at+16,UINT32_MAX);break;
   case 36:memcpy(b.data()+at+84,b.data()+at,16);b[at+84+15]=0xaa;break;case 37:memset(b.data()+at,'z',16);break;
   case 38:b[at]='/';break;case 39:b[24]='X';break;case 40:put32(b.data()+8,9);break;case 41:b.pop_back();break;
   case 42:b.push_back(0);break;case 43:b[0]='X';break;case 44:b=legacy(0,true);put32(b.data()+12,1);break;
  }
  seal(b);
  if(mode==20){b.back()^=1;}storeLegacy(f,b,source);
  if(mode==21)f[selected(source)][0]='X';
  if(mode==22)f[selected(source)].back()^=1;
  if(mode==23){put32(f[selected(source)].data()+8,UINT32_MAX);seal(f[selected(source)]);want=L::Reason::Capacity;}
  if(mode==24){put32(f[selected(source)].data()+20,123);seal(f[selected(source)]);}
  if(mode==25)f[legacyPage(0)].pop_back();
  if(mode==26)f[legacyPage(0)].push_back(0);
  if(mode==27)f.erase(legacyPage(1));
  if(mode==28)f[selected(source)].push_back(0);
  if(mode==29){put32(f[selected(source)].data()+8,128);seal(f[selected(source)]);}
  const Files before=f;Sd s(f);Hash h;WipingAlloc a;L l(c,h.api(),s.guard(),a.api());assert(l.start(s.volume(),source)==0);s.run(l);if(l.state()!=L::State::Failed||l.reason()!=want)fprintf(stderr,"malformed mode %u state %d reason %d expected %d\n",mode,int(l.state()),int(l.reason()),int(want));assert(l.state()==L::State::Failed&&l.reason()==want&&l.exitSafe());L::Candidate out;assert(l.take(&out)==C::Stale&&!out.valid()&&!a.live);nativeUnchanged(c,n,calls);assert(f==before);noWrites(s);++cases;
 }
 printf("Legacy reader: %u recomputed-CRC malformed namespace/owner/header/row/anchor/duplicate/count/length/page/index cases fail closed PASS\n",cases);
}
static void capacityAndSelection(){
 for(unsigned mode=0;mode<9;++mode){Native n;Files f;Bytes b=legacy(16,true,1);storeLegacy(f,b,L::Source::SingleFile);const auto source=mode==0?L::Source::PagedV2:L::Source::SingleFile;
  unsigned cap=C::Header+4+(mode==1?335:336);C c(6,n.api(),{cap,4096,1024},C::heap());Sd s(f);Hash h;WipingAlloc a;
  if(mode==2)s.available=false;
  if(mode==3)s.lease=false;
  if(mode==4)a.fail=true;
  if(mode==5)s.fault=2;
  if(mode==6)s.terminalBusy=true;
  if(mode==7)s.nativeWorker=false;
  if(mode==8)h.broken=true;
  L l(c,h.api(),s.guard(),a.api());int rc=l.start(s.volume(),source);
  if(mode==3){assert(rc==C::Conflict&&l.reason()==L::Reason::CustodyLost);continue;}if(mode==4){assert(rc==C::NoMemory&&s.io==0);continue;}if(mode==7){assert(rc==C::Busy&&s.io==0);continue;}assert(rc==0);s.run(l);assert(l.state()==L::State::Failed&&l.exitSafe());
  if(mode==0)assert(l.reason()==L::Reason::SourceMissing);
  if(mode==1)assert(l.reason()==L::Reason::Capacity);
  if(mode==2)assert(l.reason()==L::Reason::MediaUnavailable);
  if(mode==5||mode==6||mode==8){assert(l.reason()==L::Reason::ReadFailure);}assert(!n.calls);noWrites(s);
 }
 // Both sources can exist; select exactly one, never fallback on invalid/missing.
 {Files f;auto a=legacy(3,false),b=legacy(2,true);storeLegacy(f,a,L::Source::PagedV2);storeLegacy(f,b,L::Source::SingleFile);readGood(f,L::Source::PagedV2,a);readGood(f,L::Source::SingleFile,b);f[selected(L::Source::PagedV2)][0]^=1;readGood(f,L::Source::SingleFile,b);}
 // Same V2 bytes in two containers must have distinct source fingerprints.
 {Files f;auto b=legacy(2,false);storeLegacy(f,b,L::Source::PagedV2);storeLegacy(f,b,L::Source::SingleFile);assert(fingerprint(f,L::Source::PagedV2,b)!=fingerprint(f,L::Source::SingleFile,b));}
 for(auto source:{L::Source::PagedV2,L::Source::SingleFile}){Files f;auto b=legacy(0,source==L::Source::SingleFile);storeLegacy(f,b,source);readGood(f,source,b,C::Header+4);}
 puts("Legacy reader: exact native capacity, fixed V1 overhead, independent source selection/no fallback, missing/unavailable/unreadable and allocator/hash/worker admission PASS");
}
static unsigned completeRead(Native&n,Files&f,L::Source source){C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;L l(c,h.api(),s.guard());assert(l.start(s.volume(),source)==0);unsigned steps=s.run(l);assert(l.state()==L::State::Verified);L::Candidate out;assert(l.take(&out)==0);return steps;}
static void boundaries(){unsigned cases=0;for(auto source:{L::Source::PagedV2,L::Source::SingleFile}){Native initial;Files original;seed(initial,original);storeLegacy(original,legacy(source==L::Source::PagedV2?100:16,source==L::Source::SingleFile),source);unsigned steps=completeRead(initial,original,source);
 for(bool queued:{false,true})for(unsigned mode=0;mode<4;++mode)for(unsigned cut=0;cut<=steps;++cut){Native n=initial;Files f=original;{C c(6,n.api(),limits(),C::heap());assert(c.load()==0);const unsigned calls=n.calls;Sd s(f);Hash h;WipingAlloc a;L l(c,h.api(),s.guard(),a.api());assert(l.start(s.volume(),source)==0);for(unsigned i=0;i<cut&&l.state()==L::State::Running;++i)s.tick(l);if(queued&&l.state()==L::State::Running)s.tick(l,false);
  if(mode==0){l.cancel();s.run(l);assert(l.state()==L::State::Cancelled&&l.exitSafe());}
  if(mode==1||mode==2){if(mode==1)s.available=false;else s.lease=false;if(l.state()==L::State::Verified){L::Candidate out;assert(l.take(&out)!=0&&!out.valid());}else s.run(l);assert(l.state()==L::State::Failed&&l.exitSafe());}
  nativeUnchanged(c,n,calls);assert(f==original);noWrites(s);
 }unchanged(n);completeRead(n,f,source);++cases;}}
 printf("Legacy reader: %u queued/completed worker cancel/removal/export/restart boundaries preserve all native intents and source bytes PASS\n",cases);
}
static void faults(){unsigned cases=0;for(auto source:{L::Source::PagedV2,L::Source::SingleFile})for(bool backup:{false,true}){Native initial;Files original;seed(initial,original);storeLegacy(original,legacy(source==L::Source::PagedV2?100:16,source==L::Source::SingleFile),source);Files sourceFiles=original;const std::string path=selected(source);if(backup){sourceFiles[path+".risc-replace"]=sourceFiles.at(path);sourceFiles.erase(path);}unsigned io=0;
 {Files f=sourceFiles;C c(6,initial.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;L l(c,h.api(),s.guard());assert(l.start(s.volume(),source)==0);s.run(l);assert(l.state()==L::State::Verified&&f==original);io=s.io;l.cancel();}
 for(bool crash:{false,true})for(bool after:{false,true})for(unsigned cut=1;cut<=io;++cut){Native n=initial;Files f=sourceFiles;{C c(6,n.api(),limits(),C::heap());assert(c.load()==0);const unsigned calls=n.calls;Sd s(f);s.fault=cut;s.crash=crash;s.after=after;Hash h;WipingAlloc a;L l(c,h.api(),s.guard(),a.api());assert(l.start(s.volume(),source)==0);try{s.run(l);}catch(const Sd::Cut&){}assert(s.hit);nativeUnchanged(c,n,calls);assert(f==sourceFiles||f==original);noWrites(s);
   if(l.state()==L::State::Retained){auto before=s.io;l.cancel();for(unsigned i=0;i<5;++i)s.tick(l,false);L::Candidate out;assert(l.state()==L::State::Retained&&!l.exitSafe()&&l.take(&out)==C::Stale&&!out.valid()&&s.io==before);}
  }unchanged(n);completeRead(n,f,source);assert(f==original);++cases;
 }
 // Existing destination plus backup is read without invoking recovery/deletion.
 {Files f=original;f[path+".risc-replace"]={1,2,3};const auto before=f;completeRead(initial,f,source);assert(f==before);}
 }
 printf("Legacy reader: %u reached before/after read/recovery/close faults and power cuts; backup-only recovery and sticky retained custody PASS\n",cases);
}

struct SourceRace {
 static SourceRace*active;Sd&sd;L::Source source;Bytes replacement;std::string watched;unsigned occurrence=1,seen=0;bool fired=false,revoke=false,onEnd=false;
 bool(*close)(void*,uint32_t,bool)=nullptr;int32_t(*end)(void*,uint64_t)=nullptr;
 SourceRace(Sd&s,L::Source kind,Bytes b):sd(s),source(kind),replacement(std::move(b)){assert(!active);active=this;auto&v=*const_cast<risc_storage_volume_api_v1*>(s.adapter.volume);close=v.file_close;v.file_close=[](void*p,uint32_t h,bool commit){auto&r=*active;const std::string path=r.sd.opened;bool ok=r.close(p,h,commit);if(!r.onEnd&&path==r.watched&&++r.seen==r.occurrence)r.fire();return ok;};end=s.download.base.base.end;s.download.base.base.end=[](void*p,uint64_t token){auto&r=*active;int32_t rc=r.end(p,token);if(r.onEnd&&rc==RISC_STORAGE_ASYNC_OK)r.fire();return rc;};}
 void fire(){assert(!fired);fired=true;storeLegacy(sd.files,replacement,source);if(revoke)sd.lease=false;}
 ~SourceRace(){active=nullptr;}
};
SourceRace*SourceRace::active=nullptr;
static void sourceRaces(){unsigned cases=0;
 for(auto source:{L::Source::PagedV2,L::Source::SingleFile})for(unsigned mode=0;mode<4;++mode){Native n;Files f;seed(n,f);const Bytes original=legacy(source==L::Source::PagedV2?100:16,source==L::Source::SingleFile);storeLegacy(f,original,source);Bytes changed=original;changed[(source==L::Source::PagedV2?128:124)+20]='X';seal(changed);
  // CRC32(image including its own CRC) is a fixed residue: identical indexes
  // alone cannot detect this valid same-generation replacement.
  if(source==L::Source::PagedV2){Files other;storeLegacy(other,changed,source);assert(other[selected(source)]==f[selected(source)]);}
  C c(6,n.api(),limits(),C::heap());assert(c.load()==0);const unsigned calls=n.calls;Sd s(f);Hash h;SourceRace race(s,source,changed);race.watched=source==L::Source::PagedV2?legacyPage(1):selected(source);race.occurrence=mode==1?2:1;race.onEnd=mode==2;race.revoke=mode>=1;
  if(mode==3){race.watched=selected(source);race.revoke=false;if(source==L::Source::PagedV2){put32(race.replacement.data()+16,8);seal(race.replacement);}}
  WipingAlloc a;L l(c,h.api(),s.guard(),a.api());assert(l.start(s.volume(),source)==0);s.run(l);assert(race.fired);L::Candidate out;assert(l.take(&out)!=0&&!out.valid());assert(l.state()==L::State::Failed&&l.exitSafe());assert(l.reason()==L::Reason::SourceChanged||l.reason()==L::Reason::CustodyLost||l.reason()==L::Reason::InvalidContent);nativeUnchanged(c,n,calls);noWrites(s);++cases;
 }
 // Candidate bytes stay historical and owned after successful close/transfer.
 for(unsigned mode=0;mode<4;++mode){Native n;Files f;Bytes b=legacy(2);storeLegacy(f,b,L::Source::PagedV2);C c(6,n.api(),limits(),C::heap());Sd s(f);Hash h;WipingAlloc a;L l(c,h.api(),s.guard(),a.api());assert(l.start(s.volume(),L::Source::PagedV2)==0);s.run(l);assert(l.state()==L::State::Verified);L::Candidate out;
  if(mode==0){assert(l.take(&out)==0);const auto wanted=canonical(b);f.clear();s.lease=false;s.available=false;assert(!memcmp(out.map(),wanted.data(),wanted.size()));}
  else {if(mode==1)s.available=false;if(mode==2)s.lease=false;if(mode==3)s.retained=true;assert(l.take(&out)!=0&&!out.valid());if(mode==3){l.cancel();assert(l.state()==L::State::Retained&&!l.exitSafe());}else assert(l.state()==L::State::Failed&&l.exitSafe());}++cases;
 }
 printf("Legacy reader: %u source races including unchanged-index replacement, final reread/close lease loss, late removal/export/retention and post-transfer ownership PASS\n",cases);
}
struct ReentryAlloc {
 WipingAlloc base;L*job=nullptr;const risc_storage_volume_api_v1*volume=nullptr;unsigned attempts=0;
 C::Allocator api(){return {this,[](void*p,size_t n)->void*{auto&a=*static_cast<ReentryAlloc*>(p);++a.attempts;assert(a.job->start(a.volume,L::Source::PagedV2)==C::Busy);L::Candidate out;assert(a.job->take(&out)==C::Busy&&!out.valid());a.job->step();a.job->cancel();const auto b=a.base.api();return b.allocate(b.context,n);},[](void*p,void*out){auto&a=*static_cast<ReentryAlloc*>(p);const auto b=a.base.api();b.release(b.context,out);}};}
};
struct FaultHash {
 Hash base;unsigned calls=0,failAt=0;bool hit=false;L*job=nullptr;
 bool admit(){++calls;if(job){L::Candidate out;assert(job->take(&out)==C::Busy);job->step();job->cancel();}if(calls==failAt){hit=true;return false;}return true;}
 L::Digest api(){return {this,[](void*p){auto&h=*static_cast<FaultHash*>(p);const auto a=h.base.api();return h.admit()&&a.begin(a.context);},[](void*p,const void*b,uint32_t n){auto&h=*static_cast<FaultHash*>(p);const auto a=h.base.api();return h.admit()&&a.update(a.context,b,n);},[](void*p,uint8_t*out){auto&h=*static_cast<FaultHash*>(p);const auto a=h.base.api();return h.admit()&&a.finish(a.context,out);}};}
};
static void edgeCases(){
 // Admission is retryable; a failed lease cannot leave a stale error reason.
 {Native n;Files f;auto b=legacy(1);storeLegacy(f,b,L::Source::PagedV2);C c(6,n.api(),limits(),C::heap());Sd s(f);Hash h;L l(c,h.api(),s.guard());s.lease=false;assert(l.start(s.volume(),L::Source::PagedV2)==C::Conflict&&l.reason()==L::Reason::CustodyLost);s.lease=true;assert(l.start(s.volume(),L::Source::PagedV2)==0&&l.reason()==L::Reason::None);s.run(l);L::Candidate out;assert(l.take(&out)==0&&l.state()==L::State::Done&&l.reason()==L::Reason::None&&l.error()==C::Ok);}
 // Retention outranks simultaneous lease loss, including cancellation after
 // checked close; neither route can release a retained provider's grants.
 for(bool cancel:{false,true}){Native n;Files f;auto b=legacy(1);storeLegacy(f,b,L::Source::PagedV2);C c(6,n.api(),limits(),C::heap());Sd s(f);Hash h;L l(c,h.api(),s.guard());assert(l.start(s.volume(),L::Source::PagedV2)==0);s.run(l);assert(l.state()==L::State::Verified);s.lease=false;s.retained=true;L::Candidate out;const unsigned io=s.io;if(cancel)l.cancel();else assert(l.take(&out)==C::Retained);assert(!out.valid()&&l.state()==L::State::Retained&&!l.exitSafe()&&l.reason()==L::Reason::Retained);l.cancel();l.step();assert(s.io==io&&l.state()==L::State::Retained&&!l.exitSafe());}
 // Reader admission/hash callbacks cannot recursively mutate the same job.
 Native n;Files f;auto b=legacy(100);storeLegacy(f,b,L::Source::PagedV2);const Files original=f;C c(6,n.api(),limits(),C::heap());unsigned hashCalls=0;
 {Sd s(f);FaultHash h;ReentryAlloc a;L l(c,h.api(),s.guard(),a.api());a.job=&l;a.volume=s.volume();h.job=&l;assert(l.start(s.volume(),L::Source::PagedV2)==0);s.run(l);L::Candidate out;assert(l.take(&out)==0&&a.attempts==1);hashCalls=h.calls;}
 for(unsigned fail=1;fail<=hashCalls;++fail){Sd s(f);FaultHash h;h.failAt=fail;WipingAlloc a;L l(c,h.api(),s.guard(),a.api());assert(l.start(s.volume(),L::Source::PagedV2)==0);s.run(l);assert(h.hit&&l.state()==L::State::Failed&&l.reason()==L::Reason::ReadFailure&&l.exitSafe()&&!a.live&&f==original);}
 for(auto source:{L::Source::PagedV2,L::Source::SingleFile}){
  // Corrupt source retained after missing-only recovery; no new defaults/fallback.
  Files bad;auto bytes=legacy(0,source==L::Source::SingleFile);storeLegacy(bad,bytes,source);const auto path=selected(source);bad[path][0]='X';const auto corrupt=bad[path];bad[path+".risc-replace"]=bad[path];bad.erase(path);Sd s(bad);Hash h;L l(c,h.api(),s.guard());assert(l.start(s.volume(),source)==0);s.run(l);assert(l.state()==L::State::Failed&&l.reason()==L::Reason::InvalidContent&&bad[path]==corrupt&&!bad.count(path+".risc-replace"));noWrites(s);
 }
 // Any padding/tail change alters exact provenance despite identical output map.
 {Bytes a=legacy(2,true,1),b=a;b[119]^=1;b[139]^=1;b[145]^=1;seal(b);assert(canonical(a)==canonical(b));Files fa,fb;storeLegacy(fa,a,L::Source::SingleFile);storeLegacy(fb,b,L::Source::SingleFile);assert(fingerprint(fa,L::Source::SingleFile,a)!=fingerprint(fb,L::Source::SingleFile,b));readGood(fb,L::Source::SingleFile,b);}
 // V2 single-file framing at exact worker limit is supported; one over is invalid.
 {Bytes b=legacy(2,false,1962);assert(b.size()==4096);Files f;storeLegacy(f,b,L::Source::SingleFile);readGood(f,L::Source::SingleFile,b);f[selected(L::Source::SingleFile)].push_back(0);Sd s(f);Hash h;L l(c,h.api(),s.guard());assert(l.start(s.volume(),L::Source::SingleFile)==0);s.run(l);assert(l.state()==L::State::Failed&&l.reason()==L::Reason::InvalidContent);}
 assert(!n.calls);printf("Legacy reader: %u digest failure sites, reentrant callbacks, corrupt recovery, padding provenance and exact 4096-byte single-file limit PASS\n",hashCalls);
}
static void dynamicEnvelope(){
 Native n;Files f;C c(6,n.api(),{512,4096,1024},C::heap());auto base=identity({},0);assert(c.initialize(nullptr,0,base)==0);Sd s(f);Hash h;WipingAlloc a;
 L before(c,h.api(),s.guard(),a.api()); // Construct while v1 is still selected.
 C::Snapshot snapshot;assert(c.captureSnapshot(&snapshot)==0);auto target=identity({},0,8);uint8_t incarnation[16]={9};C::ExportMarker marker;assert(c.prepareExport(snapshot,target,incarnation,&marker)==0&&c.headerSize()==C::HeaderV2);
 auto tooBig=legacy(3);storeLegacy(f,tooBig,L::Source::PagedV2);unsigned calls=n.calls;assert(before.start(s.volume(),L::Source::PagedV2)==0);s.run(before);assert(before.state()==L::State::Failed&&before.reason()==L::Reason::Capacity&&a.peak==512-C::HeaderV2-4+132&&n.calls==calls&&c.exportPrepared());
 auto exact=legacy(1,false,104);storeLegacy(f,exact,L::Source::PagedV2);Hash h2;L after(c,h2.api(),s.guard());assert(after.start(s.volume(),L::Source::PagedV2)==0);s.run(after);L::Candidate out;assert(after.take(&out)==0&&out.mapSize()==124&&n.calls==calls&&c.exportPrepared());
 puts("Legacy reader: start-time RTENR02 envelope after preconstructed-v1 upgrade, exact v2 capacity and untouched active export marker PASS");
}
struct RunningCancel {
 static RunningCancel*active;Sd&sd;L*job=nullptr;bool fired=false;size_t(*read)(void*,uint32_t,void*,size_t)=nullptr;
 explicit RunningCancel(Sd&s):sd(s){assert(!active);active=this;auto&v=*const_cast<risc_storage_volume_api_v1*>(s.adapter.volume);read=v.file_read;v.file_read=[](void*p,uint32_t h,void*out,size_t n){auto&r=*active;if(!r.fired){r.fired=true;r.job->cancel();for(unsigned i=0;i<3;++i){const unsigned io=r.sd.io;r.job->step();L::Candidate candidate;assert(r.job->state()==L::State::Running&&!r.job->exitSafe()&&r.job->take(&candidate)==C::Stale&&!candidate.valid()&&r.sd.io==io);}}return r.read(p,h,out,n);};}
 ~RunningCancel(){active=nullptr;}
};
RunningCancel*RunningCancel::active=nullptr;
struct BusyFlow {
 static BusyFlow*active;unsigned begins=0,submits=0,ends=0;
 int32_t(*begin)(void*,uint64_t*)=nullptr;int32_t(*submit)(void*,uint64_t,const risc_storage_async_request_v1*,uint64_t*)=nullptr;int32_t(*end)(void*,uint64_t)=nullptr;
 explicit BusyFlow(Sd&s){assert(!active);active=this;auto&a=s.download.base.base;begin=a.begin;submit=a.submit;end=a.end;a.begin=[](void*p,uint64_t*t){auto&b=*active;return ++b.begins<=3?int32_t(RISC_STORAGE_ASYNC_BUSY):b.begin(p,t);};a.submit=[](void*p,uint64_t t,const risc_storage_async_request_v1*q,uint64_t*op){auto&b=*active;return ++b.submits<=3?int32_t(RISC_STORAGE_ASYNC_BUSY):b.submit(p,t,q,op);};a.end=[](void*p,uint64_t t){auto&b=*active;++b.ends;if(b.ends<=4)return int32_t(b.ends%2?RISC_STORAGE_ASYNC_PENDING:RISC_STORAGE_ASYNC_BUSY);return b.end(p,t);};}
 ~BusyFlow(){active=nullptr;}
};
BusyFlow*BusyFlow::active=nullptr;
static void activeWorkerCustody(){
 for(auto source:{L::Source::PagedV2,L::Source::SingleFile}){Native n;Files f;auto b=legacy(2,source==L::Source::SingleFile);storeLegacy(f,b,source);const Files before=f;C c(6,n.api(),limits(),C::heap());
  {Sd s(f);Hash h;RunningCancel running(s);L l(c,h.api(),s.guard());running.job=&l;assert(l.start(s.volume(),source)==0);s.run(l);assert(running.fired&&s.ends>=4&&l.state()==L::State::Cancelled&&l.exitSafe()&&!n.calls&&f==before);noWrites(s);}
  {Sd s(f);Hash h;BusyFlow busy(s);L l(c,h.api(),s.guard());assert(l.start(s.volume(),source)==0);while(l.state()==L::State::Running){L::Candidate out;assert(l.take(&out)==C::Stale&&!out.valid()&&!l.exitSafe());s.tick(l);}assert(l.state()==L::State::Verified&&busy.begins>=4&&busy.submits>=4&&busy.ends>=5);L::Candidate out;assert(l.take(&out)==0&&l.exitSafe()&&!n.calls&&f==before);noWrites(s);}
 }
 puts("Legacy reader: actual running-worker cancellation waits through repeated pending close; transient begin/submit BUSY and closing BUSY/PENDING preserve custody PASS");
}
int main(){setbuf(stdout,nullptr);basics();malformed();capacityAndSelection();boundaries();faults();sourceRaces();edgeCases();dynamicEnvelope();activeWorkerCustody();puts("DISCONNECTED LEGACY READER ONLY: no migration, adoption, defaults, native writes, cleanup, public ABI or hardware claim");}
