#include <RiscStreamSessionProviderV1.h>
#include <string.h>
#ifndef FIXTURE_SLOT
#error FIXTURE_SLOT must be defined
#endif
extern void dependency_cleanup_event(unsigned, const char*);
extern void dependency_cleanup_mapping(unsigned, const void*);
extern bool dependency_cleanup_ready(unsigned);
static const risc_provider_dependency_v1* dependencies;
static size_t dependency_count;
static int mapping_marker;
static const uint32_t api[2] = {1, sizeof(api)};
static bool dependencies_valid(void) {
  for (size_t i = 0; i < dependency_count; ++i)
    if (!dependencies[i].api || *(const uint32_t*)dependencies[i].api != 1) return false;
  return true;
}
static bool start(const risc_provider_dependency_v1* deps, size_t count) {
  dependencies = deps;
  dependency_count = count;
#if FIXTURE_SLOT == 3
  if (count != 2 || strcmp(deps[0].capability_id, "cap.cleanup-safe") ||
      strcmp(deps[1].capability_id, "cap.cleanup-middle")) return false;
#elif FIXTURE_SLOT == 2
  if (count != 1 || strcmp(deps[0].capability_id, "cap.cleanup-leaf")) return false;
#else
  if (count) return false;
#endif
  dependency_cleanup_event(FIXTURE_SLOT, "start");
  dependency_cleanup_mapping(FIXTURE_SLOT, &mapping_marker);
  return dependencies_valid();
}
static bool quiesce(void) {
  if (!dependencies_valid()) __builtin_trap();
  dependency_cleanup_event(FIXTURE_SLOT, "quiesce");
  return dependency_cleanup_ready(FIXTURE_SLOT);
}
static void stop(void) {
  if (!dependencies_valid()) __builtin_trap();
  dependency_cleanup_event(FIXTURE_SLOT, "stop");
}
#if FIXTURE_SLOT == 3
static const risc_stream_provider_v1* host;
static uint32_t rx, tx;
static bool bind(const risc_stream_provider_v1* h) { host = h; return true; }
static int32_t open_session(const void* request, uint32_t size, uint32_t ms,
                            risc_provider_stream_session_v1* out) {
  (void)request;
  if (size || !ms || rx || tx) return RISC_STREAM_INVALID;
  risc_stream_endpoint_v1 e = {sizeof(e), 1, RISC_STREAM_READ, 16, 0, 0, 0};
  if (host->publish(host->context, &e, &rx) != RISC_STREAM_OK) return RISC_STREAM_RETAINED;
  e.rights = RISC_STREAM_WRITE;
  if (host->publish(host->context, &e, &tx) != RISC_STREAM_OK) return RISC_STREAM_RETAINED;
  out->session = 1;
  out->rx_endpoint = rx;
  out->tx_endpoint = tx;
  dependency_cleanup_event(FIXTURE_SLOT, "open");
  return RISC_STREAM_OK;
}
static int32_t call_session(uint64_t session, const void* request, uint32_t size,
                            uint32_t ms, void* reply, uint32_t capacity, uint32_t* count) {
  (void)session; (void)request; (void)size; (void)ms; (void)reply; (void)capacity;
  *count = 0;
  return RISC_STREAM_OK;
}
static int32_t close_session(uint64_t session, uint32_t ms) {
  if (session != 1 || !ms || !rx || !tx) return RISC_STREAM_INVALID;
  dependency_cleanup_event(FIXTURE_SLOT, "close");
  if (host->close(host->context, rx) != RISC_STREAM_OK ||
      host->close(host->context, tx) != RISC_STREAM_OK) return RISC_STREAM_RETAINED;
  rx = tx = 0;
  return RISC_STREAM_OK;
}
static const risc_stream_session_provider_v1 adapter = {
  1, sizeof(adapter), open_session, call_session, close_session
};
static const risc_driver_stream_sessions_v2 driver = {
  {{{2, sizeof(driver), "cleanup-stream", "cap.cleanup-stream", 1, api, start, stop, quiesce}, 0, bind}, 0},
  RISC_DRIVER_STREAM_SESSIONS_TAG_V1, 1, &adapter
};
#else
#if FIXTURE_SLOT == 0
#define ID "cleanup-safe"
#define CAP "cap.cleanup-safe"
#elif FIXTURE_SLOT == 1
#define ID "cleanup-leaf"
#define CAP "cap.cleanup-leaf"
#else
#define ID "cleanup-middle"
#define CAP "cap.cleanup-middle"
#endif
static const risc_driver_v2 driver = {2, sizeof(driver), ID, CAP, 1, api, start, stop, quiesce};
#endif
__attribute__((visibility("default")))
const risc_driver_v2* t5_driver_get(uint32_t abi) {
  return abi == 2 ? (const risc_driver_v2*)&driver : 0;
}
