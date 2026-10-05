#include "runtime/provisioning/Maintenance.h"
#include <openssl/sha.h>
#include <map>
#include <string>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstring>
using namespace RiscProvision;
struct Model {
 std::map<std::string,std::string> blobs{{"unrelated","keep"}};std::vector<std::string> replies;uint32_t now=1,writes=0,installs=0;bool server=false;
 InstallResult run(const void* p,uint32_t n,const void* t,uint32_t size){++installs;std::vector<uint8_t> scratch(ProfileInputBytes);
 InstallTransport io{{this,[](void* c,const char* k,void* out,uint32_t cap,uint32_t* size){auto& m=*static_cast<Model*>(c);auto at=m.blobs.find(k);if(at==m.blobs.end())return InputStatus::Missing;if(at->second.size()>cap)return InputStatus::Invalid;memcpy(out,at->second.data(),at->second.size());*size=at->second.size();return InputStatus::Ready;}},
 [](void* c,const char* k,const void* p,uint32_t n){auto& m=*static_cast<Model*>(c);++m.writes;m.blobs[k]=std::string(static_cast<const char*>(p),n);return true;},[](void*){return true;}};
 return install(io,p,n,t,size,scratch.data(),scratch.size());}
 MaintenancePort port(){return {this,[](void* c){return static_cast<Model*>(c)->now;},[](void*){return 0x12345678u;},
 [](void*,const void* p,uint32_t n,uint8_t* out){SHA256(static_cast<const uint8_t*>(p),n,out);return true;},
 [](void* c,const void* p,uint32_t n,const void* t,uint32_t size){return static_cast<Model*>(c)->run(p,n,t,size);},
 [](void* c,const char* line){auto& m=*static_cast<Model*>(c);m.replies.emplace_back(line);if(m.server)std::cout<<line<<std::endl;},"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};}
};
int main(int argc,char**){Model model;model.server=argc>1;std::vector<uint8_t> buffer(ProfileInputBytes+384);Maintenance endpoint(model.port(),buffer.data(),buffer.size());
 auto feed=[&](const std::string& text){for(unsigned char c:text)endpoint.feed(c);};
 if(model.server){char c;while(std::cin.get(c))endpoint.feed(uint8_t(c));assert(model.blobs["unrelated"]=="keep");return 0;}
 feed("INSTALL 12345678 1 0 "+std::string(64,'0')+"\n");assert(!model.installs&&model.replies.back()=="RTE_INSTALL INVALID");
 feed("HELLO\n");model.now+=30001;endpoint.poll();assert(model.replies.back()=="RTE_INSTALL TIMEOUT");
 feed("HELLO\nINSTALL 12345678 1 0 "+std::string(64,'0')+"\nx");assert(!model.installs&&model.replies.back()=="RTE_INSTALL CORRUPT");
 feed("HELLO\nINSTALL 00000000 1 0 "+std::string(64,'0')+"\n");assert(!model.installs&&model.replies.back()=="RTE_INSTALL INVALID");
 feed("HELLO\nINSTALL 12345678 16385 0 "+std::string(64,'0')+"\n");assert(!model.installs&&model.replies.back()=="RTE_INSTALL INVALID");
 feed("HELLO\nINSTALL 12345678 8 0 "+std::string(64,'0')+"\nsecret");model.now+=30001;endpoint.poll();for(auto c:buffer)assert(!c);assert(!model.writes);
 feed(std::string(220,'x')+"\n");assert(model.replies.back()=="RTE_INSTALL INVALID");
 std::cout<<"Maintenance framing: identity, nonce, bounds, corruption, timeout and payload wiping PASS\n";
}
