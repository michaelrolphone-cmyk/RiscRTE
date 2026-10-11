#pragma once
// Synthetic source FACT collector for host qualification. Metadata access uses
// the actual checked NVS adapter and private artifacts use the checked inspector.
// The fixture's legacy inventory is explicit test state, not a product collector.
#include "native_replica_checked_fixture.h"
#include "experimental/native_replica/NativeBaselineAdmission.h"
namespace NativeFixture {
struct FreshNativeFacts {
 using A=RiscExperimental::NativeBaselineAdmission;
 RiscExperimental::CheckedFreshInitArtifacts inspector;
 N metadata{{nullptr,[](void*){return onWorker&&lease;},[](void*){return initialized?ESP_OK:ESP_ERR_NVS_NOT_INITIALIZED;}}};
 explicit FreshNativeFacts():inspector(6,rootPath.c_str(),{nullptr,[](void*){return onWorker&&lease;},cooperate,[](void*){return int(C::Ok);}}){}
 static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 static uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
 static void put(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
 static uint32_t crc(const uint8_t*p,uint32_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
 static Bytes canonical(){Bytes p(484);memcpy(p.data(),"RTENR03",8);put(p.data()+8,6);p[16]=1;put(p.data()+108,p.size());auto*b=p.data()+384;memcpy(b,"RTENBP1",8);put(b+8,6);put(b+12,1);put(b+92,crc(b,92));put(p.data()+480,crc(p.data(),480));return p;}
 int unknownMetadata()const{auto it=nvs.find("rnr00000006");if(it!=nvs.end())for(auto&kv:it->second)if(kv.first!="root"&&kv.first!="next")return C::Conflict;return C::Ok;}
 int absent(){
  if(inspector.retained())return C::Retained;
  int rc=unknownMetadata();if(rc)return rc;
  auto api=metadata.api();uint8_t root[48]{},next[24]{};
  rc=api.read(api.context,6,"root",root,sizeof root);if(rc!=C::Missing)return rc?rc:int(C::Conflict);
  rc=api.read(api.context,6,"next",next,sizeof next);if(rc!=C::Missing)return rc?rc:int(C::Conflict);
  return inspector.inspectAbsent();
 }
 int resume(const C::FreshInitReceipt&receipt){
  if(inspector.retained())return C::Retained;
  if(!receipt.valid()||receipt.namespaceId()!=6)return C::Conflict;
  int rc=unknownMetadata();if(rc)return rc;
  auto api=metadata.api();uint8_t root[48]{},next[24]{};
  rc=api.read(api.context,6,"root",root,sizeof root);if(rc)return rc;
  if(memcmp(root,"RTENFI1",8)||word(root+8)!=6||memcmp(root+12,receipt.fingerprint(),32)||word(root+44)!=crc(root,44))return C::Conflict;
  rc=api.read(api.context,6,"next",next,sizeof next);if(rc==C::Missing)return inspector.inspectAbsent();if(rc)return rc;
  if(memcmp(next,"RTENSEQ",8)||word(next+16)||!wide(next+8)||wide(next+8)>UINT64_MAX/2||word(next+20)!=crc(next,20))return C::Io;
  const auto image=canonical();return inspector.inspectAttempt(image.data(),image.size(),wide(next+8));
 }
 bool hasReceipt()const{auto it=nvs.find("rnr00000006");return it!=nvs.end()&&it->second.count("root")&&it->second.at("root").size()==48&&!memcmp(it->second.at("root").data(),"RTENFI1",8);}
};
}
