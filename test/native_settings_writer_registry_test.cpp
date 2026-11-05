#include "ports/esp32s3/NativeSettingsWriterRegistry.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
using RiscCpu::NativeSettingsLifecycle::WriterRegistry;
template<size_t B,size_t I>void boundaries(){
 WriterRegistry<B,I> r;
 for(size_t i=0;i<I;++i){const auto name="identity-"+std::to_string(i);assert(r.add(name.c_str(),i%2?6:1));}
 assert(r.identityCount()==I&&r.bindingCount()==I);
 const char*stable=r.app(0);
 for(size_t i=I;i<B;++i){const size_t id=i-I;const auto name="identity-"+std::to_string(id);assert(r.add(name.c_str(),id%2?1:6));}
 assert(r.bindingCount()==B&&r.identityCount()==I&&stable==r.app(0));
 assert(!r.add("overflow",1)&&r.bindingCount()==B);
 assert(!r.add("identity-0",1)&&r.bindingCount()==B);
 for(size_t i=0;i<B;++i){const size_t id=i<I?i:i-I;const auto name="identity-"+std::to_string(id);assert(!std::strcmp(r.app(i),name.c_str()));assert(r.nameSpace(i)==(i<I?(id%2?6u:1u):(id%2?1u:6u)));}
 assert(!r.app(B)&&r.nameSpace(B)==0);
}
int main(){
 boundaries<1,1>();boundaries<2,1>();boundaries<7,6>();boundaries<32,16>();boundaries<32,32>();
 WriterRegistry<4,2> r;char source[96]="stable";
 assert(r.add(source,6));const char*p=r.app(0);std::strcpy(source,"changed");
 assert(!std::strcmp(p,"stable"));assert(r.add("stable",1)&&r.app(1)==p);
 assert(!r.add("stable",1)&&r.bindingCount()==2&&r.identityCount()==1);
 const std::string longest(63,'a');assert(r.add(longest.c_str(),1));assert(r.add(longest.c_str(),6));
 assert(r.identityCount()==2&&r.bindingCount()==4);
 WriterRegistry<3,1> unique;
 assert(unique.add("one",1));assert(!unique.add("two",6));assert(unique.bindingCount()==1&&unique.identityCount()==1);
 assert(unique.add("one",6));assert(!unique.add("one",6));
 for(const char*bad:{"","a-","_a","A","a/b","a.b"}){WriterRegistry<1,1> x;assert(!x.add(bad,1)&&!x.bindingCount());}
 for(size_t length:{size_t(64),size_t(95),size_t(96)}){WriterRegistry<1,1>x;const std::string name(length,'a');assert(!x.add(name.c_str(),1)&&!x.bindingCount());}
 WriterRegistry<2,2>x;assert(!x.add(nullptr,1)&&!x.add("ok",0)&&!x.add("ok",3)&&!x.add("ok",7)&&!x.bindingCount());
 printf("Writer identities: all capacities, stable copies, ordered two-namespace bindings, duplicate/invalid refusal passed; sizeof previous6=%zu successor7/6=%zu default32=%zu\n",sizeof(WriterRegistry<6,6>),sizeof(WriterRegistry<7,6>),sizeof(WriterRegistry<32,32>));
}
