#pragma once
#include "Installer.h"
#include <RiscProvisionRequestV1.h>
namespace RiscProvision {
constexpr uint32_t RequestHeaderBytes=160,RequestRecordBytes=RequestHeaderBytes+ProfileInputBytes+384;
struct RequestSource {
 uint32_t storeAbi=0,firmwareSize=0;
 uint8_t storeSha[32]{},firmwareSha[32]{},consumedProfileSha[32]{};
 bool receiptAvailable=false;
};
struct RequestView {
 const uint8_t* profile=nullptr;const uint8_t* time=nullptr;
 uint32_t profileSize=0,timeSize=0;RequestSource source{};
 uint8_t profileSha[32]{},requestSha[32]{},targetStoreSha[32]{};
};
using RequestHash=bool(*)(const void*,uint32_t,uint8_t*);
bool parseRequest(void*,uint32_t,RequestView&,RequestHash);
bool requestSourceMatches(const RequestView&,const RequestSource&);
bool requestConsumed(const RequestView&,const RequestSource&);
// Two caller-owned PSRAM buffers. No caller pointers survive; neither the old
// pending request nor unrelated data is ever erased. A failed final write is
// ambiguous even when set/commit reports failure.
int32_t queueRequest(InstallTransport,const risc_provision_request_v1&,const RequestSource&,
                     void* oldBytes,void* nextBytes,uint32_t capacity,RequestHash,uint8_t out[32]);
Input requestInput(RequestView&);
}
