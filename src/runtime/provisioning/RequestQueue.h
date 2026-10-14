#pragma once
#include "Installer.h"
#include <RiscProvisionRequestV1.h>
namespace RiscProvision {
constexpr uint32_t RequestHeaderBytes=160,RequestReceiptBytes=288,RequestRecordBytes=RequestHeaderBytes+ProfileInputBytes+384;
enum : uint8_t { RequestReady=1,RequestActive=2 };
struct RequestSource {
 uint32_t storeAbi=0,firmwareSize=0;
 uint8_t storeSha[32]{},firmwareSha[32]{},consumedProfileSha[32]{};
 bool receiptAvailable=false;
};
struct RequestView {
 const uint8_t* profile=nullptr;const uint8_t* time=nullptr;
 uint32_t profileSize=0,timeSize=0;RequestSource source{};
 uint8_t profileSha[32]{},requestSha[32]{},targetStoreSha[32]{},timeSha[32]{},consumedStoreSha[32]{};
 bool terminal=false;
};
using RequestHash=bool(*)(const void*,uint32_t,uint8_t*);
// Private queue-owned transport. probe observes key presence without reading any
// payload bytes; erase is restricted to these queue-owned keys by the adapter.
struct RequestTransport {
 Input input;bool (*set)(void*,const char*,const void*,uint32_t);bool (*commit)(void*);
 bool (*erase)(void*,const char*);InputStatus (*probe)(void*,const char*);
};
// A present terminal is authoritative; never inspect its staged payload.
InputStatus readRequest(Input,void*,uint32_t,uint32_t*,bool* controlled=nullptr);
bool parseRequest(void*,uint32_t,RequestView&,RequestHash);
bool requestSourceMatches(const RequestView&,const RequestSource&);
bool requestConsumed(const RequestView&,const RequestSource&);
// Two caller-owned PSRAM buffers. Only a confirmed consumed request can enter
// terminal mode. Persist a separate receipt, erase its consumed/staged payload,
// freshly write/verify the next request, then remove/verify the terminal before
// reporting success. Never replace either backend value in place. Any ambiguity
// stops UNKNOWN; a retained terminal suppresses legacy bootstrap input.
int32_t queueRequest(RequestTransport,const risc_provision_request_v1&,const RequestSource&,
                     void* oldBytes,void* nextBytes,uint32_t capacity,RequestHash,uint8_t out[32]);
Input requestInput(RequestView&);
}
