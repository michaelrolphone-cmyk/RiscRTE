#pragma once
// Shared deterministic backend adapted from native_replica_core_test.cpp.
#include <experimental/native_replica/NativeReplicaCore.h>
#include <experimental/native_replica/NativeRebaseCandidate.h>
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using C=RiscExperimental::NativeReplicaCore;
using R=RiscExperimental::NativeRebaseCandidate;
using Bytes=std::vector<uint8_t>;
struct Disk {
 struct PowerCut {};
 enum Mode{Before,After,Torn};
 Bytes root;std::map<uint64_t,Bytes> objects;uint64_t next=1;
 unsigned calls=0,cut=0,retainedRootMode=0;Mode mode=Before;bool crash=false;size_t capacity=16*1024*1024;
 bool begin(){++calls;if(cut==calls&&mode==Before&&crash)throw PowerCut{};return cut==calls&&mode==Before;}
 bool end()const{if(cut==calls&&mode!=Before&&crash)throw PowerCut{};return cut==calls&&mode!=Before;}
 void fault(unsigned n,Mode m,bool power=false){calls=0;cut=n;mode=m;crash=power;}
 C::Backend backend(){return {this,
  [](void*v,void*p,uint32_t n){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);if(d.root.empty())return int(C::Missing);if(d.root.size()!=n)return int(C::Io);memcpy(p,d.root.data(),n);if(d.retainedRootMode==3)return int(C::Retained);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint32_t n,uint64_t*id){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);size_t used=0;for(auto&e:d.objects)used+=e.second.size();if(used>d.capacity||n>d.capacity-used)return int(C::NoSpace);*id=d.next++;d.objects[*id]=Bytes(n,0xa5);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint64_t id,uint32_t at,const void*p,uint32_t n){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);auto it=d.objects.find(id);if(it==d.objects.end()||at>it->second.size()||n>it->second.size()-at)return int(C::Io);const uint32_t written=d.cut==d.calls&&d.mode==Torn?n/2:n;memcpy(it->second.data()+at,p,written);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint64_t id,uint32_t at,void*p,uint32_t n){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);auto it=d.objects.find(id);if(it==d.objects.end()||at>it->second.size()||n>it->second.size()-at)return int(C::Io);memcpy(p,it->second.data()+at,n);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint64_t id){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);if(!d.objects.count(id))return int(C::Io);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,const void*p,uint32_t n){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io); // Root is atomic even when immutable payload writes can tear.
   if(d.retainedRootMode==1)return int(C::Retained);
   if(!(d.cut==d.calls&&d.mode==Torn))d.root.assign(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n);
   if(d.retainedRootMode==2)return int(C::Retained);
   return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint64_t id){auto&d=*static_cast<Disk*>(v);if(d.begin())return;if(d.root.size()==C::RootBytes){uint64_t active=0;for(unsigned i=0;i<8;++i)active|=uint64_t(d.root[24+i])<<(8*i);assert(active!=id);}d.objects.erase(id);}
 };}
};
struct Alloc {
 void(*probe)(void*)=nullptr;void*probeContext=nullptr;bool verifyWipe=false;unsigned calls=0,fail=0;std::map<void*,size_t> live;size_t bytes=0,peak=0;
 C::Allocator api(){return {this,[](void*v,size_t n)->void*{auto&a=*static_cast<Alloc*>(v);if(a.probe)a.probe(a.probeContext);if(++a.calls==a.fail)return nullptr;void*p=malloc(n);if(p){a.live[p]=n;a.bytes+=n;if(a.bytes>a.peak)a.peak=a.bytes;}return p;},[](void*v,void*p){auto&a=*static_cast<Alloc*>(v);if(a.probe)a.probe(a.probeContext);auto i=a.live.find(p);assert(i!=a.live.end());if(a.verifyWipe)for(size_t k=0;k<i->second;++k)assert(static_cast<uint8_t*>(p)[k]==0);a.bytes-=i->second;a.live.erase(i);free(p);}};}
 void discardVolatileAfterPowerCut(){for(auto&v:live)free(v.first);live.clear();bytes=0;}
 ~Alloc(){assert(live.empty()&&bytes==0);}
};

inline C::Limits coreLimits(uint32_t bytes=65512){return {bytes,4096,1024};}
inline R::Limits receiverLimits(uint32_t bytes=65512){return {bytes,bytes,4096,1024};}
inline C::Identity identity(uint64_t gen,uint8_t lineage=1){C::Identity i{};i.known=true;i.generation=gen;i.lineage[0]=lineage;i.digest[0]=uint8_t(gen);return i;}
inline void append(Bytes&map,const char*key,const void*value,uint32_t size){const auto at=map.size();map.resize(at+20+size);assert(strlen(key)<16);memcpy(map.data()+at,key,strlen(key));for(unsigned i=0;i<4;++i)map[at+16+i]=uint8_t(size>>(i*8));memcpy(map.data()+at+20,value,size);}
inline std::string get(C&core,const char*key){char bytes[2048];uint32_t size=0;assert(core.get(key,bytes,sizeof bytes,&size)==C::Ok);return {bytes,size};}
inline std::string get(R&candidate,const char*key){char bytes[2048];uint32_t size=0;assert(candidate.get(candidate.viewRevision(),key,bytes,sizeof bytes,&size)==C::Ok);return {bytes,size};}
inline int put(C&core,const char*key,const char*value,const char*intent){C::Mutation m{key,C::Put,value,uint32_t(strlen(value))};return core.apply(core.revision(),&m,1,intent,uint32_t(strlen(intent)));}
inline R::Remote remote(const Bytes&map,uint32_t count,C::Identity base=identity(900),uint32_t ns=6){return {ns,count,uint32_t(map.size()),map.data(),base};}
inline R::Batch batch(R&candidate,uint64_t id,const char*intent,const C::Mutation*m=nullptr,uint32_t count=0){return {6,candidate.viewRevision(),candidate.frontier(),id,intent,uint32_t(strlen(intent)),m,count};}
inline Bytes nativeIntents(C&core){Bytes all;uint64_t after=0,id=0;uint8_t bytes[1024];uint32_t size=0;for(;;){const int rc=core.pending(after,&id,bytes,sizeof bytes,&size);if(rc==C::Missing)break;assert(rc==C::Ok);for(unsigned j=0;j<8;++j)all.push_back(uint8_t(id>>(j*8)));all.insert(all.end(),bytes,bytes+size);after=id;}return all;}
