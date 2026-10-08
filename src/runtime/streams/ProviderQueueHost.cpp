#include "ProviderQueueHost.h"
#include "runtime/RuntimeLimits.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>

// Byte-ring copying and provider rights follow Reader's StreamRuntime and
// NativeStreamBridge.p4.inc. This bounded host retains no backend/ELF callbacks,
// record queues, resource adapters, pipes, or app-selected owner identities.
namespace RuntimeStreams {
namespace {
constexpr uint32_t Bytes = 1;
constexpr uint32_t Active = 1, Revoked = 2, Retained = 3;
constexpr uint32_t MaxContextGeneration = UINT32_MAX >> 2;
struct Context {
  // Generation and authority state share one lock-free word. A stale revoke
  // cannot fence a later occupant between checking its ID and clearing access.
  std::atomic<uint32_t> gate{0};
};
struct Queue {
  uint32_t id = 0;
  uint64_t context = 0;
  uint32_t rights = 0, capacity = 0, head = 0, used = 0, high = 0;
  int32_t terminal = 0;
  uint64_t read = 0, written = 0;
  uint64_t session = 0, reservedLease = 0;
  uint32_t reservedConsumer = 0;
  bool reservationRevoked = false, closed = false;
  uint64_t grantLease = 0;
  uint32_t grantConsumer = 0, grantRights = 0;
  std::unique_ptr<uint8_t[]> bytes;
};
struct Registry {
  std::atomic_flag mutex = ATOMIC_FLAG_INIT;
  std::array<Context, RiscLimits::Providers> contexts{};
  // Metadata-only Runtime candidates and boot without stream providers pay no
  // queue metadata allocation. Once needed, this fixed allocation persists for
  // process lifetime, including every retained provider's uncertain cleanup.
  using Queues = std::array<Queue, ProviderQueueEndpoints>;
  std::unique_ptr<Queues> queues;
  uint32_t generation = 0, endpoint = 0;
  size_t allocated = 0;
#ifdef RISC_STREAM_HOST_TESTING
  bool failAllocation = false;
  unsigned failGrant = 0;
#endif
};
Registry registry;
static_assert(ATOMIC_INT_LOCK_FREE == 2, "Provider authority requires lock-free 32-bit atomics");
struct Lock {
  bool held = !registry.mutex.test_and_set(std::memory_order_acquire);
  ~Lock() { if (held) registry.mutex.clear(std::memory_order_release); }
};
Context* contextSlot(uint64_t context) {
  const uint32_t slot = static_cast<uint32_t>(context);
  const uint64_t generation = context >> 32;
  if (!slot || slot > registry.contexts.size() || !generation ||
      generation > MaxContextGeneration) return nullptr;
  return &registry.contexts[slot - 1];
}
uint32_t contextGate(uint64_t context, uint32_t state) {
  return (static_cast<uint32_t>(context >> 32) << 2) | state;
}
bool contextIs(uint64_t context, uint32_t state) {
  const auto* slot = contextSlot(context);
  return slot && slot->gate.load(std::memory_order_acquire) == contextGate(context, state);
}
Queue* queue(uint32_t endpoint) {
  if (endpoint && registry.queues)
    for (auto& q : *registry.queues) if (q.id == endpoint) return &q;
  return nullptr;
}
int32_t ownedQueue(uint64_t context, uint32_t endpoint, Queue** out) {
  *out = nullptr;
  if (!context || !endpoint) return RISC_STREAM_INVALID;
  if (!contextIs(context, Active)) return RISC_STREAM_CLOSED;
  auto* q = queue(endpoint);
  if (!q) return RISC_STREAM_CLOSED;
  if (q->context != context) return RISC_STREAM_DENIED;
  if (q->closed) return RISC_STREAM_CLOSED;
  *out = q;
  return RISC_STREAM_OK;
}
int32_t grantedQueue(uint64_t context, uint64_t lease, uint32_t consumer,
                     uint32_t endpoint, uint32_t right, Queue** out) {
  if (!lease || !consumer) return RISC_STREAM_INVALID;
  const auto result = ownedQueue(context, endpoint, out);
  if (result != RISC_STREAM_OK) return result;
  const auto& q = **out;
  if (!q.grantLease || (q.session && q.reservationRevoked)) return RISC_STREAM_CLOSED;
  if (q.grantLease != lease || q.grantConsumer != consumer ||
      (q.grantRights & right) != right) return RISC_STREAM_DENIED;
  return RISC_STREAM_OK;
}
void saturate(uint64_t& value, uint32_t amount) {
  value = UINT64_MAX - value < amount ? UINT64_MAX : value + amount;
}
int32_t transfer(Queue& q, bool reading, void* data, uint32_t requested, uint32_t* count) {
  if (!requested) return RISC_STREAM_OK;
  if (q.terminal < 0) return q.terminal;
  if (!reading && q.terminal) return RISC_STREAM_CLOSED;
  const uint32_t n = std::min(std::min(requested, uint32_t(RISC_STREAM_CHUNK_V1)),
                            reading ? q.used : q.capacity - q.used);
  if (!n) return reading && q.terminal ? q.terminal : RISC_STREAM_AGAIN;
  const uint32_t start = reading ? q.head : (q.head + q.used) % q.capacity;
  const uint32_t first = std::min(n, q.capacity - start);
  if (reading) {
    std::memcpy(data, q.bytes.get() + start, first);
    if (n > first) std::memcpy(static_cast<uint8_t*>(data) + first, q.bytes.get(), n - first);
    q.head = (q.head + n) % q.capacity;
    q.used -= n;
    saturate(q.read, n);
  } else {
    std::memcpy(q.bytes.get() + start, data, first);
    if (n > first) std::memcpy(q.bytes.get(), static_cast<const uint8_t*>(data) + first, n - first);
    q.used += n;
    q.high = std::max(q.high, q.used);
    saturate(q.written, n);
  }
  *count = n;
  return RISC_STREAM_OK;
}
void clearGrant(Queue& q) {
  q.grantLease = 0;
  q.grantConsumer = q.grantRights = 0;
}
void destroy(Queue& q) {
  registry.allocated -= q.capacity;
  q = Queue{};
}
int32_t publish(uint64_t context, const risc_stream_endpoint_v1* spec, uint32_t* out) {
  if (out) *out = 0;
  if (!out || !spec || spec->struct_size < sizeof(*spec) || !context ||
      !spec->rights || (spec->rights & ~(RISC_STREAM_READ | RISC_STREAM_WRITE)))
    return RISC_STREAM_INVALID;
  if (spec->kind != Bytes) return RISC_STREAM_UNSUPPORTED;
  if (!spec->byte_capacity || spec->byte_capacity > RISC_STREAM_BUFFER_MAX_V1 ||
      spec->schema || spec->max_record || spec->record_capacity) return RISC_STREAM_INVALID;
  Lock lock;
  if (!lock.held) return RISC_STREAM_BUSY;
  if (!contextIs(context, Active)) return RISC_STREAM_CLOSED;
  size_t owned = 0;
  Queue* available = nullptr;
  for (auto& q : *registry.queues) {
    if (q.id && q.context == context) ++owned;
    if (!q.id && !available) available = &q;
  }
  if (owned >= ProviderQueuesPerContext || !available || registry.endpoint == UINT32_MAX ||
      spec->byte_capacity > ProviderQueueBytes - registry.allocated) return RISC_STREAM_LIMIT;
#ifdef RISC_STREAM_HOST_TESTING
  if (registry.failAllocation) { registry.failAllocation = false; return RISC_STREAM_LIMIT; }
#endif
  std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[spec->byte_capacity]);
  if (!bytes) return RISC_STREAM_LIMIT;
  // Revoke is lock-free and may have arrived during allocation.
  if (!contextIs(context, Active)) return RISC_STREAM_CLOSED;
  auto& q = *available;
  q.id = ++registry.endpoint;
  q.context = context;
  q.rights = spec->rights;
  q.capacity = spec->byte_capacity;
  q.bytes = std::move(bytes);
  registry.allocated += q.capacity;
  *out = q.id;
  return RISC_STREAM_OK;
}
int32_t providerTransfer(uint64_t context, uint32_t endpoint, void* data,
                         uint32_t size, uint32_t* count, bool reading) {
  if (count) *count = 0;
  if (!count || (!data && size)) return RISC_STREAM_INVALID;
  Lock lock;
  if (!lock.held) return RISC_STREAM_BUSY;
  Queue* q;
  const auto result = ownedQueue(context, endpoint, &q);
  if (result != RISC_STREAM_OK) return result;
  // The provider produces the consumer's READ queue and drains its WRITE queue.
  if (!(q->rights & (reading ? RISC_STREAM_WRITE : RISC_STREAM_READ))) return RISC_STREAM_DENIED;
  return transfer(*q, reading, data, size, count);
}
int32_t produce(uint64_t context, uint32_t endpoint, const void* data, uint32_t size, uint32_t* count) {
  return providerTransfer(context, endpoint, const_cast<void*>(data), size, count, false);
}
int32_t consume(uint64_t context, uint32_t endpoint, void* data, uint32_t size, uint32_t* count) {
  return providerTransfer(context, endpoint, data, size, count, true);
}
int32_t produceRecord(uint64_t, uint32_t, const void*, uint32_t) { return RISC_STREAM_UNSUPPORTED; }
int32_t consumeRecord(uint64_t, uint32_t, void*, uint32_t, uint32_t* count) {
  if (count) *count = 0;
  return RISC_STREAM_UNSUPPORTED;
}
int32_t finish(uint64_t context, uint32_t endpoint, int32_t terminal) {
  if (terminal != RISC_STREAM_EOF && (terminal >= 0 || terminal < RISC_STREAM_RETAINED))
    return RISC_STREAM_INVALID;
  Lock lock;
  if (!lock.held) return RISC_STREAM_BUSY;
  Queue* q;
  const auto result = ownedQueue(context, endpoint, &q);
  if (result != RISC_STREAM_OK) return result;
  if (q->terminal < 0 && terminal!=RISC_STREAM_RETAINED) return q->terminal;
  q->terminal = terminal;
  if(terminal==RISC_STREAM_RETAINED) {
    // A terminal custody failure is visible to graph/runtime before another
    // provider poll or app operation, even when nobody drains this endpoint.
    auto* slot=contextSlot(context);uint32_t expected=contextGate(context,Active);
    if(!slot->gate.compare_exchange_strong(expected,contextGate(context,Retained),std::memory_order_acq_rel) &&
       expected==contextGate(context,Revoked))
      (void)slot->gate.compare_exchange_strong(expected,contextGate(context,Retained),std::memory_order_acq_rel);
  }
  return RISC_STREAM_OK;
}
int32_t closeEndpoint(uint64_t context, uint32_t endpoint) {
  Lock lock;
  if (!lock.held) return RISC_STREAM_BUSY;
  Queue* q;
  const auto result = ownedQueue(context, endpoint, &q);
  if (result != RISC_STREAM_OK) return result;
  if (q->session) {
    // The adapter may still fail physical close. Keep the original bytes and
    // slot until Runtime confirms that close or Module confirms quiescence.
    q->closed = q->reservationRevoked = true;
    clearGrant(*q);
  } else {
    destroy(*q);
  }
  return RISC_STREAM_OK;
}
bool open(risc_stream_provider_v1* out) {
  if (!out) return false;
  *out = {};
  Lock lock;
  if (!lock.held || registry.generation == MaxContextGeneration) return false;
  for (size_t i = 0; i < registry.contexts.size(); ++i) {
    auto& slot = registry.contexts[i];
    if (slot.gate.load(std::memory_order_acquire)) continue;
    if (!registry.queues) {
#ifdef RISC_STREAM_HOST_TESTING
      if (registry.failAllocation) { registry.failAllocation = false; return false; }
#endif
      registry.queues.reset(new (std::nothrow) Registry::Queues);
      if (!registry.queues) return false;
    }
    const uint64_t context = (uint64_t(++registry.generation) << 32) | (i + 1);
    slot.gate.store(contextGate(context, Active), std::memory_order_release);
    *out = {RISC_STREAM_PROVIDER_API_V1, sizeof(*out), context, &publish, &produce,
            &consume, &produceRecord, &consumeRecord, &finish, &closeEndpoint};
    return true;
  }
  return false;
}
bool revokeChecked(uint64_t context) {
  auto* slot = contextSlot(context);
  if (!slot) return false;
  uint32_t expected = contextGate(context, Active);
  if (!slot->gate.compare_exchange_strong(expected, contextGate(context, Revoked),
                                         std::memory_order_acq_rel) &&
      expected != contextGate(context, Revoked)) return false;
  // New access is already dead even when a provider worker currently owns the
  // registry. False makes ModuleV2 retain its exact mapping and retry no cleanup.
  Lock lock;
  if (!lock.held) return false;
  for (auto& q : *registry.queues) if (q.id && q.context == context) {
    clearGrant(q);
    q.reservationRevoked = true;
  }
  return true;
}
bool closeChecked(uint64_t context) {
  Lock lock;
  if (!lock.held || !contextIs(context, Revoked)) return false;
  for (auto& q : *registry.queues) if (q.id && q.context == context) destroy(q);
  contextSlot(context)->gate.store(0, std::memory_order_release);
  return true;
}
void revoke(uint64_t context) { (void)revokeChecked(context); }
void close(uint64_t context) { (void)closeChecked(context); }
bool grant(uint64_t context, uint64_t lease, uint32_t consumer, uint32_t endpoint, uint32_t rights) {
  if (!lease || !consumer || !rights || (rights & ~(RISC_STREAM_READ | RISC_STREAM_WRITE))) return false;
  Lock lock;
  if (!lock.held) return false;
#ifdef RISC_STREAM_HOST_TESTING
  if (registry.failGrant && --registry.failGrant == 0) return false;
#endif
  Queue* q;
  if (ownedQueue(context, endpoint, &q) != RISC_STREAM_OK || (q->rights & rights) != rights) return false;
  if (q->session && (q->reservationRevoked || q->reservedLease != lease ||
                    q->reservedConsumer != consumer || rights != q->rights)) return false;
  if (q->grantLease && (q->grantLease != lease || q->grantConsumer != consumer ||
                       q->grantRights != rights)) return false;
  q->grantLease = lease;
  q->grantConsumer = consumer;
  q->grantRights = rights;
  return true;
}
bool revokeGrantChecked(uint64_t context, uint64_t lease) {
  return revokeProviderStreamGrant(context, lease) == RISC_STREAM_OK;
}
void revokeGrant(uint64_t context, uint64_t lease) { (void)revokeGrantChecked(context, lease); }
bool safe(uint64_t context) { return contextIs(context,Active) || contextIs(context,Revoked); }
const RuntimeProviders::StreamHostV1 host = {
  &open, &revoke, &close, &grant, &revokeGrant, nullptr,
  &revokeChecked, &closeChecked, &revokeGrantChecked, &safe
};
bool pairArguments(uint64_t context, uint64_t lease, uint32_t consumer,
                   uint64_t session, uint32_t rx, uint32_t tx) {
  return context && lease && consumer && session && rx && tx && rx != tx;
}
bool reservationMatches(const Queue& q, uint64_t context, uint64_t lease,
                        uint32_t consumer, uint64_t session) {
  return q.context == context && q.session == session && q.reservedLease == lease &&
         q.reservedConsumer == consumer;
}
int32_t clientTransfer(uint64_t context, uint64_t lease, uint32_t consumer, uint32_t endpoint,
                       void* data, uint32_t size, uint32_t* count, bool reading) {
  if (count) *count = 0;
  if (!count || (!data && size)) return RISC_STREAM_INVALID;
  Lock lock;
  if (!lock.held) return RISC_STREAM_BUSY;
  Queue* q = nullptr;
  const auto result = grantedQueue(context, lease, consumer, endpoint,
                                   reading ? RISC_STREAM_READ : RISC_STREAM_WRITE, &q);
  return result == RISC_STREAM_OK ? transfer(*q, reading, data, size, count) : result;
}
}  // namespace

const RuntimeProviders::StreamHostV1* runtimeProviderStreamHost() { return &host; }

int32_t reserveEndpointPair(uint64_t context, uint64_t lease, uint32_t consumer,
                           uint64_t session, uint32_t rx, uint32_t tx) {
  if (!pairArguments(context, lease, consumer, session, rx, tx)) return RISC_STREAM_INVALID;
  Lock lock;
  if (!lock.held) return RISC_STREAM_BUSY;
  if (!contextIs(context, Active)) return RISC_STREAM_DENIED;
  auto* r = queue(rx);
  auto* t = queue(tx);
  if (!r || !t) return RISC_STREAM_INVALID;
  if (r->context != context || t->context != context) return RISC_STREAM_DENIED;
  if (r->rights != RISC_STREAM_READ || t->rights != RISC_STREAM_WRITE ||
      r->terminal || t->terminal || r->closed || t->closed) return RISC_STREAM_INVALID;
  if (r->session || t->session || r->grantLease || t->grantLease) return RISC_STREAM_DENIED;
  for (const auto& q : *registry.queues)
    if (q.id && q.context == context && q.session == session) return RISC_STREAM_DENIED;
  for (auto* q : {r, t}) {
    q->session = session;
    q->reservedLease = lease;
    q->reservedConsumer = consumer;
    q->reservationRevoked = false;
  }
  return RISC_STREAM_OK;
}
int32_t releaseEndpointPair(uint64_t context, uint64_t lease, uint32_t consumer,
                           uint64_t session, uint32_t rx, uint32_t tx) {
  if (!pairArguments(context, lease, consumer, session, rx, tx)) return RISC_STREAM_INVALID;
  Lock lock;
  if (!lock.held) return RISC_STREAM_BUSY;
  if (!contextIs(context, Active) && !contextIs(context, Revoked)) return RISC_STREAM_CLOSED;
  auto* r = queue(rx);
  auto* t = queue(tx);
  for (auto* q : {r, t}) if (q && !reservationMatches(*q, context, lease, consumer, session))
    return RISC_STREAM_DENIED;
  for (auto* q : {r, t}) if (q && q->grantLease) return RISC_STREAM_BUSY;
  for (auto* q : {r, t}) if (q) destroy(*q);
  return RISC_STREAM_OK;
}
int32_t revokeProviderStreamGrant(uint64_t context, uint64_t lease) {
  if (!context || !lease) return RISC_STREAM_INVALID;
  Lock lock;
  if (!lock.held) return RISC_STREAM_BUSY;
  if (!contextIs(context, Active) && !contextIs(context, Revoked)) return RISC_STREAM_CLOSED;
  for (auto& q : *registry.queues) if (q.id && q.context == context) {
    if (q.grantLease == lease) clearGrant(q);
    if (q.reservedLease == lease) q.reservationRevoked = true;
  }
  return RISC_STREAM_OK;
}
int32_t providerStreamRead(uint64_t context, uint64_t lease, uint32_t consumer,
                          uint32_t endpoint, void* data, uint32_t capacity, uint32_t* count) {
  return clientTransfer(context, lease, consumer, endpoint, data, capacity, count, true);
}
int32_t providerStreamWrite(uint64_t context, uint64_t lease, uint32_t consumer,
                           uint32_t endpoint, const void* data, uint32_t length, uint32_t* count) {
  return clientTransfer(context, lease, consumer, endpoint, const_cast<void*>(data), length, count, false);
}
int32_t providerStreamInfo(uint64_t context, uint64_t lease, uint32_t consumer,
                          uint32_t endpoint, risc_stream_client_info_v1* out) {
  if (!out || out->struct_size < sizeof(*out)) return RISC_STREAM_INVALID;
  *out = {};
  out->struct_size = sizeof(*out);
  Lock lock;
  if (!lock.held) return RISC_STREAM_BUSY;
  Queue* q = nullptr;
  const auto result = grantedQueue(context, lease, consumer, endpoint, 0, &q);
  if (result != RISC_STREAM_OK) return result;
  *out = {sizeof(*out), q->grantRights, q->capacity, q->used, q->high,
          q->terminal, q->read, q->written};
  return RISC_STREAM_OK;
}
#ifdef RISC_STREAM_HOST_TESTING
namespace Testing {
bool lockRegistry() { return !registry.mutex.test_and_set(std::memory_order_acquire); }
void unlockRegistry() { registry.mutex.clear(std::memory_order_release); }
size_t allocatedBytes() { Lock lock; return lock.held ? registry.allocated : SIZE_MAX; }
bool metadataAllocated() { Lock lock; return lock.held && bool(registry.queues); }
void failNextAllocation() { Lock lock; if (lock.held) registry.failAllocation = true; }
void failGrantNumber(unsigned n) { Lock lock; if (lock.held) registry.failGrant = n; }
bool advanceIssuers(uint32_t contextGeneration, uint32_t endpoint) {
  Lock lock;
  if (!lock.held || contextGeneration < registry.generation || endpoint < registry.endpoint ||
      contextGeneration > MaxContextGeneration) return false;
  registry.generation = contextGeneration;
  registry.endpoint = endpoint;
  return true;
}
bool setCounters(uint64_t context, uint32_t endpoint, uint64_t read, uint64_t written) {
  Lock lock;
  Queue* q;
  if (!lock.held || ownedQueue(context, endpoint, &q) != RISC_STREAM_OK) return false;
  q->read = read;
  q->written = written;
  return true;
}
}  // namespace Testing
#endif
}  // namespace RuntimeStreams
