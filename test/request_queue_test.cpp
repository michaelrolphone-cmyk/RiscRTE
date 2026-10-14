#include "runtime/provisioning/RequestQueue.h"
#include <openssl/sha.h>
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
using namespace RiscProvision;
namespace {
std::map<std::string,std::vector<uint8_t>> blobs;unsigned sets,commits;int fault;
bool sha(const void*p,uint32_t n,uint8_t*out){return SHA256(static_cast<const uint8_t*>(p),n,out)!=nullptr;}
InputStatus read(void*,const char*k,void*out,uint32_t cap,uint32_t*n){*n=0;auto i=blobs.find(k);if(i==blobs.end())return InputStatus::Missing;if(i->second.size()>cap)return InputStatus::Invalid;memcpy(out,i->second.data(),i->second.size());*n=i->second.size();return InputStatus::Ready;}
InstallTransport transport{{nullptr,read},[](void*,const char*k,const void*p,uint32_t n){++sets;if(fault==1)return false;blobs[k]=std::vector<uint8_t>(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n);if(fault==2)return false;return true;},[](void*){++commits;return fault!=3;}};
std::string profile(){std::string p=R"({"schema":"riscrte.provisioning","schema_version":2,"wifi":{"ssid":"test","password":"test-only"},"base_url":"https://example.test/","files":[)";for(const char*k:{"boot.json","board.json","default.elf"}){if(p.back()!='[')p+=',';p+="{\"path\":\""+std::string(k)+"\",\"bytes\":1,\"sha256\":\""+std::string(64,'0')+"\"}";}return p+"]}";}
}
int main(){
 std::string p=profile(),time=R"({"schema":"riscrte.sntp","schema_version":1,"servers":["time.example.test"]})";
 RequestSource source;source.storeAbi=2;source.firmwareSize=4096;source.storeSha[0]=1;source.firmwareSha[0]=2;
 risc_provision_request_v1 q{sizeof(q),p.data(),uint32_t(p.size()),time.data(),uint32_t(time.size()),{}};memcpy(q.expected_active_store_sha256,source.storeSha,32);
 std::vector<uint8_t> old(RequestRecordBytes),next(RequestRecordBytes);uint8_t out[32];
 auto queue=[&](){return queueRequest(transport,q,source,old.data(),next.data(),RequestRecordBytes,sha,out);};
 assert(queue()==RISC_PROVISION_REQUEST_OK&&sets==1&&commits==1);auto initial=blobs["request"];uint8_t identity[32];memcpy(identity,out,32);
 assert(queue()==RISC_PROVISION_REQUEST_OK&&sets==1&&!memcmp(identity,out,32));
 RequestView v;assert(parseRequest(initial.data(),initial.size(),v,sha)&&requestSourceMatches(v,source)&&!requestConsumed(v,source));
 auto input=requestInput(v);Descriptor d;assert(loadDescriptor(input,d)==InputStatus::Ready&&!strcmp(d.profileKey,"profile")&&!strcmp(d.timeKey,"time"));
 auto parsed=std::make_unique<Profile>();std::vector<uint8_t>scratch(ProfileInputBytes);uint8_t hash[32];assert(loadProfile(input,scratch.data(),scratch.size(),*parsed,hash,sha)==InputStatus::Ready&&!memcmp(hash,v.profileSha,32));for(auto b:scratch)assert(!b);
 for(size_t i=0;i<initial.size();++i){auto bad=initial;bad[i]^=1;RequestView rejected;assert(!parseRequest(bad.data(),bad.size(),rejected,sha));}
 q.expected_active_store_sha256[0]^=1;assert(queue()==RISC_PROVISION_REQUEST_STALE&&sets==1);q.expected_active_store_sha256[0]^=1;
 auto replacement=p;replacement[replacement.find("test-only")]='b';q.profile_json=replacement.data();
 assert(queue()==RISC_PROVISION_REQUEST_BUSY&&blobs["request"]==initial&&sets==1);
 source.receiptAvailable=true;memcpy(source.consumedProfileSha,v.profileSha,32);assert(requestConsumed(v,source));
 for(int mode=1;mode<=3;++mode){blobs["request"]=initial;fault=mode;assert(queue()==RISC_PROVISION_REQUEST_UNKNOWN);if(mode==1)assert(blobs["request"]==initial);else{auto bytes=blobs["request"];RequestView accepted;assert(parseRequest(bytes.data(),bytes.size(),accepted,sha));} }
 fault=0;blobs["request"]=initial;assert(queue()==RISC_PROVISION_REQUEST_OK);assert(queue()==RISC_PROVISION_REQUEST_OK);
 blobs["request"]={1,2,3};unsigned before=sets;assert(queue()==RISC_PROVISION_REQUEST_INVALID&&sets==before&&blobs["request"].size()==3);
 blobs.clear();q.time_size=0;assert(queue()==RISC_PROVISION_REQUEST_INVALID&&sets==before);
 q.time_size=time.size();
 // Image target digest is public status metadata, separate from private profile identity.
 std::string imageProfile=p;auto schema=imageProfile.find("schema_version\":2");assert(schema!=std::string::npos);imageProfile.replace(schema,strlen("schema_version\":2"),"schema_version\":3");
 auto base=imageProfile.find("\"base_url\":\"https://example.test/\"");assert(base!=std::string::npos);
 imageProfile.replace(base,strlen("\"base_url\":\"https://example.test/\""),"\"image\":{\"url\":\"https://example.test/store.bin\",\"bytes\":4096,\"sha256\":\""+std::string(64,'a')+"\"}");
 q.profile_json=imageProfile.data();q.profile_size=imageProfile.size();assert(queue()==RISC_PROVISION_REQUEST_OK);
 auto imageBytes=blobs["request"];RequestView imageView;assert(parseRequest(imageBytes.data(),imageBytes.size(),imageView,sha));for(auto b:imageView.targetStoreSha)assert(b==0xaa);
 q.profile_size=ProfileInputBytes+1;before=sets;assert(queue()==RISC_PROVISION_REQUEST_INVALID&&sets==before);
 puts("Next-boot request queue: bounded copied input, every-byte corruption, native source binding, idempotent replay, pending refusal, consumed replacement, write/commit ambiguity, no erase PASS");
}
