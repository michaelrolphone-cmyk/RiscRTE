#pragma once
#include "runtime/drivers/ProviderModuleV2.h"
#include <RiscStreamClientV1.h>
#include <cstddef>
#include <cstdint>

// Trusted Runtime operations. None of these functions is an ELF export.
// A queue's provider context never substitutes for the consumer's graph lease.
namespace RuntimeStreams {
constexpr size_t ProviderQueueEndpoints = 32;
constexpr size_t ProviderQueuesPerContext = 4;
constexpr size_t ProviderQueueBytes = 32 * 1024;

const RuntimeProviders::StreamHostV1* runtimeProviderStreamHost();

// Atomically validate and reserve both queues before GraphV2 grants either.
// A reservation requires a new session and exact RX READ-only / TX WRITE-only
// byte queues. Existing reservations and terminal queues fail as contract faults.
int32_t reserveEndpointPair(uint64_t context, uint64_t lease, uint32_t consumer,
                           uint64_t session, uint32_t rx, uint32_t tx);
// After checked physical close, retire the exact reserved queues. Provider-side
// close only revokes a reserved queue, retaining its bytes until this call.
// Missing endpoints are harmless; a mismatching live endpoint is never changed.
int32_t releaseEndpointPair(uint64_t context, uint64_t lease, uint32_t consumer,
                           uint64_t session, uint32_t rx, uint32_t tx);
// Revoke consumer authority but retain queue bytes and the reservation.
int32_t revokeProviderStreamGrant(uint64_t context, uint64_t lease);

int32_t providerStreamRead(uint64_t context, uint64_t lease, uint32_t consumer,
                          uint32_t endpoint, void* data, uint32_t capacity,
                          uint32_t* count);
int32_t providerStreamWrite(uint64_t context, uint64_t lease, uint32_t consumer,
                           uint32_t endpoint, const void* data, uint32_t length,
                           uint32_t* count);
int32_t providerStreamInfo(uint64_t context, uint64_t lease, uint32_t consumer,
                          uint32_t endpoint, risc_stream_client_info_v1* out);

#ifdef RISC_STREAM_HOST_TESTING
namespace Testing {
bool lockRegistry();
void unlockRegistry();
size_t allocatedBytes();
void failNextAllocation();
// Fail exactly the nth following host grant attempt, once (0 disables).
void failGrantNumber(unsigned n);
// Monotonic test-only advancement, never resetting or reusing an issued ID.
bool advanceIssuers(uint32_t contextGeneration, uint32_t endpoint);
bool setCounters(uint64_t context, uint32_t endpoint, uint64_t read, uint64_t written);
}
#endif
}  // namespace RuntimeStreams
