#include "runtime/streams/ProviderQueueHost.h"
#include "runtime/RuntimeLimits.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>

using namespace RuntimeStreams;
namespace {
const auto* host = runtimeProviderStreamHost();
constexpr uint64_t Lease = (uint64_t(7) << 32) | 3;
constexpr uint32_t Consumer = 19;
risc_stream_provider_v1 openProvider() {
  risc_stream_provider_v1 p{};
  assert(host->open(&p));
  assert(p.context && p.api_version == 1 && p.struct_size == sizeof(p));
  return p;
}
void closeProvider(const risc_stream_provider_v1& p) {
  assert(host->revokeChecked(p.context));
  assert(host->closeChecked(p.context));
}
risc_stream_endpoint_v1 spec(uint32_t rights, uint32_t capacity = 513) {
  return {sizeof(risc_stream_endpoint_v1), 1, rights, capacity, nullptr, 0, 0};
}
uint32_t publish(const risc_stream_provider_v1& p, uint32_t rights, uint32_t capacity = 513) {
  auto s = spec(rights, capacity);
  uint32_t endpoint = 0;
  assert(p.publish(p.context, &s, &endpoint) == RISC_STREAM_OK && endpoint);
  return endpoint;
}
void grant(const risc_stream_provider_v1& p, uint32_t rx, uint32_t tx,
           uint64_t lease = Lease, uint32_t consumer = Consumer, uint64_t session = 101) {
  assert(reserveEndpointPair(p.context, lease, consumer, session, rx, tx) == RISC_STREAM_OK);
  assert(host->grant(p.context, lease, consumer, rx, RISC_STREAM_READ));
  assert(host->grant(p.context, lease, consumer, tx, RISC_STREAM_WRITE));
}
risc_stream_client_info_v1 info(const risc_stream_provider_v1& p, uint32_t endpoint) {
  risc_stream_client_info_v1 out{};
  out.struct_size = sizeof(out);
  assert(providerStreamInfo(p.context, Lease, Consumer, endpoint, &out) == RISC_STREAM_OK);
  return out;
}
void lazyMetadata() {
  assert(!Testing::metadataAllocated() && Testing::allocatedBytes() == 0);
  Testing::failNextAllocation();
  risc_stream_provider_v1 rejected{};
  assert(!host->open(&rejected) && !rejected.context);
  assert(!Testing::metadataAllocated() && Testing::allocatedBytes() == 0);
  auto p = openProvider();
  assert((p.context >> 32) == 1); // Failed allocation consumed no identity.
  assert(Testing::metadataAllocated() && Testing::allocatedBytes() == 0);
  closeProvider(p);
  assert(Testing::metadataAllocated() && Testing::allocatedBytes() == 0);
}
void transfers() {
  auto p = openProvider();
  const auto rx = publish(p, RISC_STREAM_READ), tx = publish(p, RISC_STREAM_WRITE);
  grant(p, rx, tx);
  std::array<uint8_t, 512> input{}, output{};
  for (size_t i = 0; i < input.size(); ++i) input[i] = static_cast<uint8_t>(i * 13);
  uint32_t count = 9;
  assert(providerStreamRead(p.context, Lease, Consumer, rx, output.data(), 1, &count) == RISC_STREAM_AGAIN && !count);
  for (uint32_t request : {0u, 1u, 511u, 512u, 513u, UINT32_MAX}) {
    assert(p.produce(p.context, rx, input.data(), request, &count) == RISC_STREAM_OK);
    const auto n = std::min(request, 512u);
    assert(count == n);
    assert(providerStreamRead(p.context, Lease, Consumer, rx, output.data(), request, &count) == RISC_STREAM_OK && count == n);
    assert(std::equal(input.begin(), input.begin() + n, output.begin()));
    assert(providerStreamWrite(p.context, Lease, Consumer, tx, input.data(), request, &count) == RISC_STREAM_OK && count == n);
    assert(p.consume(p.context, tx, output.data(), request, &count) == RISC_STREAM_OK && count == n);
    assert(std::equal(input.begin(), input.begin() + n, output.begin()));
  }
  assert(providerStreamWrite(p.context, Lease, Consumer, tx, input.data(), 512, &count) == RISC_STREAM_OK && count == 512);
  assert(providerStreamWrite(p.context, Lease, Consumer, tx, input.data(), 512, &count) == RISC_STREAM_OK && count == 1);
  assert(providerStreamWrite(p.context, Lease, Consumer, tx, input.data(), 1, &count) == RISC_STREAM_AGAIN && !count);
  assert(p.consume(p.context, tx, output.data(), UINT32_MAX, &count) == RISC_STREAM_OK && count == 512);
  assert(p.consume(p.context, tx, output.data(), UINT32_MAX, &count) == RISC_STREAM_OK && count == 1);
  auto stats = info(p, tx);
  assert(stats.capacity == 513 && stats.high_water == 513 && stats.buffered == 0);
  assert(stats.bytes_read == stats.bytes_written && stats.rights == RISC_STREAM_WRITE);

  // Uneven alternating operations cross the physical ring end in both copies.
  std::deque<uint8_t> expected;
  uint32_t rng = 997;
  uint64_t produced = 0, consumed = 0;
  for (unsigned step = 0; step < 4000; ++step) {
    rng = rng * 1664525u + 1013904223u;
    const uint32_t request = (rng >> 7) % 700;
    if (rng & 1) {
      for (size_t j = 0; j < input.size(); ++j) input[j] = static_cast<uint8_t>(produced + j);
      const auto result = p.produce(p.context, rx, input.data(), request, &count);
      const auto n = std::min({request, 512u, uint32_t(513 - expected.size())});
      assert(count == n && result == (request && !n ? RISC_STREAM_AGAIN : RISC_STREAM_OK));
      expected.insert(expected.end(), input.begin(), input.begin() + n);
      produced += n;
    } else {
      const auto result = providerStreamRead(p.context, Lease, Consumer, rx, output.data(), request, &count);
      const auto n = std::min({request, 512u, uint32_t(expected.size())});
      assert(count == n && result == (request && !n ? RISC_STREAM_AGAIN : RISC_STREAM_OK));
      for (uint32_t j = 0; j < n; ++j) { assert(output[j] == expected.front()); expected.pop_front(); }
      consumed += n;
    }
  }
  assert(produced > 100000 && consumed > 100000);
  assert(info(p, rx).buffered == expected.size());
  closeProvider(p);
}
void malformedAndAuthority() {
  auto p = openProvider(), other = openProvider();
  const auto rx = publish(p, RISC_STREAM_READ), tx = publish(p, RISC_STREAM_WRITE);
  const auto otherRx = publish(other, RISC_STREAM_READ), otherTx = publish(other, RISC_STREAM_WRITE);
  uint8_t byte = 0;
  uint32_t count = 77;
  assert(reserveEndpointPair(p.context, Lease, Consumer, 1, rx, rx) == RISC_STREAM_INVALID);
  assert(reserveEndpointPair(p.context, Lease, Consumer, 1, rx, otherTx) == RISC_STREAM_DENIED);
  assert(reserveEndpointPair(p.context, Lease, Consumer, 1, tx, rx) == RISC_STREAM_INVALID);
  assert(reserveEndpointPair(p.context, Lease, Consumer, 1, rx, UINT32_MAX) == RISC_STREAM_INVALID);
  grant(p, rx, tx);
  grant(other, otherRx, otherTx);
  assert(reserveEndpointPair(p.context, Lease + 1, Consumer + 1, 102, rx, tx) == RISC_STREAM_DENIED);
  assert(!host->grant(p.context, Lease + 1, Consumer, rx, RISC_STREAM_READ));
  assert(!host->grant(p.context, Lease, Consumer + 1, rx, RISC_STREAM_READ));
  assert(!host->grant(p.context, Lease, Consumer, rx, RISC_STREAM_WRITE));
  assert(!host->grant(p.context, Lease, Consumer, otherRx, RISC_STREAM_READ));
  assert(providerStreamRead(p.context, Lease + 1, Consumer, rx, &byte, 1, &count) == RISC_STREAM_DENIED && !count);
  count = 77;
  assert(providerStreamRead(p.context, Lease, Consumer + 1, rx, &byte, 1, &count) == RISC_STREAM_DENIED && !count);
  assert(providerStreamRead(p.context, Lease, Consumer, otherRx, &byte, 1, &count) == RISC_STREAM_DENIED && !count);
  assert(providerStreamRead(p.context, Lease, Consumer, tx, &byte, 1, &count) == RISC_STREAM_DENIED && !count);
  assert(providerStreamWrite(p.context, Lease, Consumer, rx, &byte, 1, &count) == RISC_STREAM_DENIED && !count);
  assert(p.produce(p.context, tx, &byte, 1, &count) == RISC_STREAM_DENIED && !count);
  assert(p.consume(p.context, rx, &byte, 1, &count) == RISC_STREAM_DENIED && !count);
  assert(p.produce(other.context, rx, &byte, 1, &count) == RISC_STREAM_DENIED && !count);
  assert(p.produce(p.context, rx, nullptr, 1, &count) == RISC_STREAM_INVALID && !count);
  assert(p.produce(p.context, rx, nullptr, 0, &count) == RISC_STREAM_OK && !count);
  assert(p.consume(p.context, tx, nullptr, 0, &count) == RISC_STREAM_OK && !count);
  assert(providerStreamRead(p.context, Lease, Consumer, rx, nullptr, 0, &count) == RISC_STREAM_OK && !count);
  assert(providerStreamWrite(p.context, Lease, Consumer, tx, nullptr, 0, &count) == RISC_STREAM_OK && !count);
  assert(providerStreamRead(p.context, Lease, Consumer, rx, &byte, 1, nullptr) == RISC_STREAM_INVALID);
  assert(providerStreamWrite(p.context, Lease, Consumer, tx, nullptr, 1, &count) == RISC_STREAM_INVALID && !count);
  assert(p.produce_record(p.context, rx, &byte, 1) == RISC_STREAM_UNSUPPORTED);
  count = 77;
  assert(p.consume_record(p.context, rx, &byte, 1, &count) == RISC_STREAM_UNSUPPORTED && !count);
  assert(releaseEndpointPair(p.context, Lease, Consumer, 101, rx, tx) == RISC_STREAM_BUSY);
  assert(revokeProviderStreamGrant(other.context, Lease) == RISC_STREAM_OK);
  assert(p.produce(p.context, rx, &byte, 1, &count) == RISC_STREAM_OK && count == 1);
  assert(providerStreamRead(p.context, Lease, Consumer, rx, &byte, 1, &count) == RISC_STREAM_OK && count == 1);
  assert(revokeProviderStreamGrant(p.context, Lease) == RISC_STREAM_OK);
  assert(providerStreamRead(p.context, Lease, Consumer, rx, &byte, 1, &count) == RISC_STREAM_CLOSED && !count);
  assert(!host->grant(p.context, Lease, Consumer, rx, RISC_STREAM_READ));
  assert(releaseEndpointPair(p.context, Lease, Consumer, 999, rx, tx) == RISC_STREAM_DENIED);
  assert(p.close(p.context, rx) == RISC_STREAM_OK);
  assert(p.close(p.context, tx) == RISC_STREAM_OK);
  assert(releaseEndpointPair(p.context, Lease, Consumer, 101, rx, tx) == RISC_STREAM_OK);
  assert(releaseEndpointPair(p.context, Lease, Consumer, 101, rx, tx) == RISC_STREAM_OK);
  const auto newRx = publish(p, RISC_STREAM_READ);
  assert(newRx != rx && newRx != tx);
  assert(p.produce(p.context, rx, &byte, 1, &count) == RISC_STREAM_CLOSED && !count);
  closeProvider(p);
  auto replacement = openProvider();
  assert(replacement.context != p.context && !host->revokeChecked(p.context));
  publish(replacement, RISC_STREAM_READ);
  closeProvider(replacement);
  closeProvider(other);
}
void terminalsAndCounters() {
  auto p = openProvider();
  const auto rx = publish(p, RISC_STREAM_READ), tx = publish(p, RISC_STREAM_WRITE);
  grant(p, rx, tx);
  std::array<uint8_t, 8> data{};
  uint32_t count;
  assert(p.finish(p.context, rx, RISC_STREAM_AGAIN) == RISC_STREAM_INVALID);
  assert(p.finish(p.context, rx, RISC_STREAM_OK) == RISC_STREAM_INVALID);
  assert(p.finish(p.context, rx, -100) == RISC_STREAM_INVALID);
  assert(Testing::setCounters(p.context, rx, UINT64_MAX - 1, UINT64_MAX - 1));
  assert(p.produce(p.context, rx, data.data(), 8, &count) == RISC_STREAM_OK && count == 8);
  assert(p.finish(p.context, rx, RISC_STREAM_EOF) == RISC_STREAM_OK);
  assert(p.produce(p.context, rx, data.data(), 8, &count) == RISC_STREAM_CLOSED && !count);
  assert(providerStreamRead(p.context, Lease, Consumer, rx, data.data(), 3, &count) == RISC_STREAM_OK && count == 3);
  assert(providerStreamRead(p.context, Lease, Consumer, rx, data.data(), 8, &count) == RISC_STREAM_OK && count == 5);
  assert(providerStreamRead(p.context, Lease, Consumer, rx, data.data(), 8, &count) == RISC_STREAM_EOF && !count);
  auto stats = info(p, rx);
  assert(stats.terminal == RISC_STREAM_EOF && stats.bytes_read == UINT64_MAX && stats.bytes_written == UINT64_MAX);
  assert(providerStreamRead(p.context, Lease, Consumer, rx, nullptr, 0, &count) == RISC_STREAM_OK && !count);
  assert(providerStreamWrite(p.context, Lease, Consumer, tx, data.data(), 8, &count) == RISC_STREAM_OK && count == 8);
  assert(p.finish(p.context, tx, RISC_STREAM_DISCONNECTED) == RISC_STREAM_OK);
  assert(p.consume(p.context, tx, data.data(), 8, &count) == RISC_STREAM_DISCONNECTED && !count);
  assert(providerStreamWrite(p.context, Lease, Consumer, tx, data.data(), 8, &count) == RISC_STREAM_DISCONNECTED && !count);
  assert(info(p, tx).buffered == 8 && info(p, tx).terminal == RISC_STREAM_DISCONNECTED);
  assert(p.finish(p.context, tx, RISC_STREAM_EOF) == RISC_STREAM_DISCONNECTED);
  assert(p.finish(p.context, rx, RISC_STREAM_IO) == RISC_STREAM_OK);
  assert(providerStreamRead(p.context, Lease, Consumer, rx, data.data(), 8, &count) == RISC_STREAM_IO && !count);
  closeProvider(p);
}
void sessionIsolationAndRetention() {
  auto p = openProvider();
  const auto rx1 = publish(p, RISC_STREAM_READ), tx1 = publish(p, RISC_STREAM_WRITE);
  const auto rx2 = publish(p, RISC_STREAM_READ), tx2 = publish(p, RISC_STREAM_WRITE);
  grant(p, rx1, tx1);
  assert(reserveEndpointPair(p.context, Lease + 1, Consumer, 101, rx2, tx2) == RISC_STREAM_DENIED);
  grant(p, rx2, tx2, Lease + 1, Consumer, 102);
  uint8_t byte = 8;
  uint32_t count = 77;
  assert(providerStreamWrite(p.context, Lease, Consumer, tx2, &byte, 1, &count) == RISC_STREAM_DENIED && !count);
  assert(providerStreamWrite(p.context, Lease + 1, Consumer, tx1, &byte, 1, &count) == RISC_STREAM_DENIED && !count);
  assert(p.produce(p.context, rx1, &byte, 1, &count) == RISC_STREAM_OK && count == 1);
  assert(p.finish(p.context, rx1, RISC_STREAM_IO) == RISC_STREAM_OK);
  assert(providerStreamRead(p.context, Lease, Consumer, rx1, &byte, 1, &count) == RISC_STREAM_IO && !count);
  assert(info(p, rx1).buffered == 1); // Negative terminal never drains queued RX.
  const auto before = Testing::allocatedBytes();
  assert(revokeProviderStreamGrant(p.context, Lease) == RISC_STREAM_OK);
  assert(p.close(p.context, rx1) == RISC_STREAM_OK);
  assert(p.close(p.context, tx1) == RISC_STREAM_OK);
  assert(Testing::allocatedBytes() == before); // Uncertain physical close retains all bytes.
  assert(providerStreamWrite(p.context, Lease + 1, Consumer, tx2, &byte, 1, &count) == RISC_STREAM_OK && count == 1);
  assert(releaseEndpointPair(p.context, Lease, Consumer, 101, rx1, tx2) == RISC_STREAM_DENIED);
  assert(Testing::allocatedBytes() == before);
  assert(releaseEndpointPair(p.context, Lease, Consumer, 101, rx1, tx1) == RISC_STREAM_OK);
  assert(Testing::allocatedBytes() == before - 2 * 513);
  assert(providerStreamWrite(p.context, Lease + 1, Consumer, tx2, &byte, 1, &count) == RISC_STREAM_OK && count == 1);
  closeProvider(p);
  assert(Testing::allocatedBytes() == 0);

  // Production grant path fault injection preserves provisional reservation
  // custody until the broker's checked rollback has closed the physical session.
  for (unsigned fail = 1; fail <= 2; ++fail) {
    p = openProvider();
    const auto rx = publish(p, RISC_STREAM_READ), tx = publish(p, RISC_STREAM_WRITE);
    assert(reserveEndpointPair(p.context, Lease, Consumer, 101, rx, tx) == RISC_STREAM_OK);
    Testing::failGrantNumber(fail);
    const bool first = host->grant(p.context, Lease, Consumer, rx, RISC_STREAM_READ);
    if (fail == 1) assert(!first);
    else {
      assert(first);
      assert(!host->grant(p.context, Lease, Consumer, tx, RISC_STREAM_WRITE));
    }
    assert(revokeProviderStreamGrant(p.context, Lease) == RISC_STREAM_OK);
    assert(p.close(p.context, rx) == RISC_STREAM_OK && p.close(p.context, tx) == RISC_STREAM_OK);
    assert(Testing::allocatedBytes() == 2 * 513);
    assert(releaseEndpointPair(p.context, Lease, Consumer, 101, rx, tx) == RISC_STREAM_OK);
    assert(Testing::allocatedBytes() == 0);
    closeProvider(p);
  }
}
void limitsAndValidation() {
  auto p = openProvider();
  uint32_t out = 77;
  auto s = spec(RISC_STREAM_READ);
  assert(p.publish(p.context, nullptr, &out) == RISC_STREAM_INVALID && !out);
  assert(p.publish(p.context, &s, nullptr) == RISC_STREAM_INVALID);
  for (auto size : {0u, 4097u, UINT32_MAX}) {
    s.byte_capacity = size;
    assert(p.publish(p.context, &s, &out) == RISC_STREAM_INVALID && !out);
  }
  s = spec(0);
  assert(p.publish(p.context, &s, &out) == RISC_STREAM_INVALID && !out);
  s = spec(4);
  assert(p.publish(p.context, &s, &out) == RISC_STREAM_INVALID && !out);
  s = spec(RISC_STREAM_READ); s.kind = 2;
  assert(p.publish(p.context, &s, &out) == RISC_STREAM_UNSUPPORTED && !out);
  s = spec(RISC_STREAM_READ); s.schema = "ignored-but-invalid";
  assert(p.publish(p.context, &s, &out) == RISC_STREAM_INVALID && !out);
  s = spec(RISC_STREAM_READ); s.struct_size--;
  assert(p.publish(p.context, &s, &out) == RISC_STREAM_INVALID && !out);
  s = spec(RISC_STREAM_READ);
  Testing::failNextAllocation();
  assert(p.publish(p.context, &s, &out) == RISC_STREAM_LIMIT && !out);
  assert(Testing::allocatedBytes() == 0);
  for (size_t i = 0; i < ProviderQueuesPerContext; ++i) publish(p, RISC_STREAM_READ, 1);
  assert(p.publish(p.context, &s, &out) == RISC_STREAM_LIMIT && !out);
  closeProvider(p);

  std::vector<risc_stream_provider_v1> contexts;
  for (size_t i = 0; i < RiscLimits::Providers; ++i) contexts.push_back(openProvider());
  risc_stream_provider_v1 overflow{};
  assert(!host->open(&overflow) && !overflow.context);
  assert(contexts.size() > 16); // Both X4 (17) and ordinary host (24).
  for (size_t i = 0; i < ProviderQueueEndpoints; ++i)
    publish(contexts[i / ProviderQueuesPerContext], RISC_STREAM_READ, 1);
  assert(contexts.back().publish(contexts.back().context, &s, &out) == RISC_STREAM_LIMIT && !out);
  assert(Testing::allocatedBytes() == ProviderQueueEndpoints);
  for (const auto& context : contexts) closeProvider(context);
  assert(Testing::allocatedBytes() == 0);

  contexts.clear();
  contexts.push_back(openProvider()); contexts.push_back(openProvider()); contexts.push_back(openProvider());
  for (unsigned i = 0; i < 8; ++i) publish(contexts[i / 4], RISC_STREAM_READ, 4096);
  assert(Testing::allocatedBytes() == ProviderQueueBytes);
  s = spec(RISC_STREAM_READ, 1);
  assert(contexts[2].publish(contexts[2].context, &s, &out) == RISC_STREAM_LIMIT && !out);
  closeProvider(contexts[0]);
  publish(contexts[2], RISC_STREAM_READ, 4096);
  closeProvider(contexts[1]); closeProvider(contexts[2]);
  assert(Testing::allocatedBytes() == 0);
}
void contentionAndRevocation() {
  auto p = openProvider();
  const auto rx = publish(p, RISC_STREAM_READ), tx = publish(p, RISC_STREAM_WRITE);
  grant(p, rx, tx);
  const auto allocation = Testing::allocatedBytes();
  uint32_t count = 77, endpoint = 77;
  uint8_t byte = 0;
  auto s = spec(RISC_STREAM_READ);
  assert(!host->closeChecked(p.context)); // No destruction before owner revoke.
  assert(Testing::lockRegistry());
  assert(p.publish(p.context, &s, &endpoint) == RISC_STREAM_BUSY && !endpoint);
  assert(p.produce(p.context, rx, &byte, 1, &count) == RISC_STREAM_BUSY && !count);
  count = 77;
  assert(providerStreamRead(p.context, Lease, Consumer, rx, &byte, 1, &count) == RISC_STREAM_BUSY && !count);
  assert(p.finish(p.context, rx, RISC_STREAM_EOF) == RISC_STREAM_BUSY);
  assert(p.close(p.context, rx) == RISC_STREAM_BUSY);
  assert(!host->grant(p.context, Lease, Consumer, rx, RISC_STREAM_READ));
  assert(revokeProviderStreamGrant(p.context, Lease) == RISC_STREAM_BUSY);
  assert(!host->revokeChecked(p.context)); // Fences without waiting on this lock.
  assert(!host->closeChecked(p.context));
  Testing::unlockRegistry();
  assert(Testing::allocatedBytes() == allocation);
  assert(p.produce(p.context, rx, &byte, 1, &count) == RISC_STREAM_CLOSED && !count);
  assert(providerStreamRead(p.context, Lease, Consumer, rx, &byte, 1, &count) == RISC_STREAM_CLOSED && !count);
  assert(p.close(p.context, rx) == RISC_STREAM_CLOSED);
  assert(Testing::allocatedBytes() == allocation);
  assert(host->revokeChecked(p.context));
  assert(host->closeChecked(p.context));
  assert(Testing::allocatedBytes() == 0);
}
void nonwrappingIds() {
  assert(Testing::advanceIssuers((UINT32_MAX >> 2) - 1, UINT32_MAX - 1));
  auto p = openProvider();
  assert((p.context >> 32) == (UINT32_MAX >> 2));
  assert(publish(p, RISC_STREAM_READ) == UINT32_MAX);
  auto s = spec(RISC_STREAM_READ);
  uint32_t out = 77;
  assert(p.publish(p.context, &s, &out) == RISC_STREAM_LIMIT && !out);
  assert(p.close(p.context, UINT32_MAX) == RISC_STREAM_OK);
  assert(p.publish(p.context, &s, &out) == RISC_STREAM_LIMIT && !out);
  closeProvider(p);
  risc_stream_provider_v1 later{};
  assert(!host->open(&later) && !later.context);
  assert(Testing::allocatedBytes() == 0);
}
}
int main() {
  lazyMetadata();
  transfers();
  malformedAndAuthority();
  terminalsAndCounters();
  sessionIsolationAndRetention();
  limitsAndValidation();
  contentionAndRevocation();
  nonwrappingIds();
  std::puts("provider queue host: passed");
}
