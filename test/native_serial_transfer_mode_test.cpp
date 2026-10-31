// Dedicated-mode qualification uses production Runtime, NativeSerialFiles and
// Files. The boot predicate and provider are explicit host fixture inputs.
#ifndef RISC_TEST_TRANSFER_MODE
#define RISC_TEST_TRANSFER_MODE 1
#endif
#if RISC_TEST_TRANSFER_MODE
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main storage_files_adapter_fixture_main
#include "storage_files_adapter_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/NativeSerialFiles.h"
#include "RuntimeFileFixture.h"
#include <fstream>
#include <unistd.h>

namespace N = RiscCpu::NativeSerialFiles;
static GenericProvider provider;
static RiscBoot::RuntimeFiles files;
static RiscBoot::Runtime* runtime;
static const risc_runtime_api_v1* app;
static std::string mode;
static unsigned dispatches, controls, shutdowns, stops, inits, entries, finis, confirmations;
static unsigned childEntries, polls, transferEntries;
static bool appStackLive, confirmedBoot, pendingPair, storageReady = true;
static bool failShutdown, lateArmed, lateInjected;
static uint32_t nowMs;
static unsigned sid = 0x12345678, sequence = 1;
static bool owner() { return true; }
static bool safe() { return true; }
static bool exitSafe() { return N::custodySafe(); }
static bool servicesReady() { return storageReady && N::exitSafe(); }
static bool eligible() { return confirmedBoot && !pendingPair; }
extern "C" int64_t esp_timer_get_time() { return int64_t(nowMs) * 1000; }
extern "C" bool xPortInIsrContext() { return false; }

static std::string frame(unsigned session, unsigned seq, const char* body) {
  char prefix[64]; snprintf(prefix, sizeof(prefix), "RTEF1 %08x %u ", session, seq);
  return std::string(prefix) + body + "\n";
}
static void input(const std::string& bytes) { for (unsigned char b : bytes) N::input(b); }
static std::string drain() {
  std::string out;
  while (N::outputSize()) {
    const size_t size = N::outputSize(); out.append(N::output(), size); N::consumeOutput(size);
  }
  return out;
}
static bool helloSuccess(const std::string& out) { return out.find(" 0 HELLO ") != std::string::npos; }
static bool checkpoint() {
  assert(!runtime->active() && !risc_runtime_get_api(1)); ++dispatches;
  printf("DISPATCH storage app_stack_live=%u app_fini=%u mode_active=%u\n", appStackLive, finis, N::transferActive());
  return N::checkpoint();
}
static bool controlCheckpoint() {
  assert(!runtime->active() && !risc_runtime_get_api(1)); ++controls;
  printf("DISPATCH control app_stack_live=%u app_fini=%u mode_active=%u\n", appStackLive, finis, N::transferActive());
  return N::controlCheckpoint();
}
static bool shutdown() {
  ++shutdowns;
  if (failShutdown) { puts("INJECT native shutdown returns unsafe"); return false; }
  return N::shutdown();
}
static bool stop() { ++stops; return N::stop(); }
static bool confirmBoot() { ++confirmations; confirmedBoot = true; return true; }
static void enterTransferMode() {
  assert(!appStackLive && finis == 1 && !childEntries && !runtime->active());
  assert(storageReady && eligible() && !runtime->retained());
  const bool wasActive = N::transferActive(); N::enterTransferMode();
  if (!wasActive && N::transferActive()) ++transferEntries;
  printf("TRANSFER_ENTER app_fini=%u active=%u transitions=%u\n", finis, N::transferActive(), transferEntries);
}
static void poll() {
  ++polls;
  if (lateArmed && !lateInjected) {
    lateInjected = true; input(frame(sid, 1, "HELLO"));
    printf("POLL late_hello=1 app_stack_live=%u app_fini=%u\n", appStackLive, finis);
  }
}
static bool finiDeadlineCase() { return mode == "fini-admission-deadline" || mode == "fini-deadline-disconnect"; }
static bool malformedCase() {
  return mode == "malformed-sid" || mode == "short-sid" || mode == "zero-sid" ||
         mode == "wrong-sequence" || mode == "extra-token" || mode == "short-frame" ||
         mode == "partial-hello" || mode == "unframed-hello";
}
static std::string malformedInput() {
  if (mode == "malformed-sid") return "RTEF1 1234567z 1 HELLO\n";
  if (mode == "short-sid") return "RTEF1 1234567 1 HELLO\n";
  if (mode == "zero-sid") return "RTEF1 00000000 1 HELLO\n";
  if (mode == "wrong-sequence") return "RTEF1 12345678 2 HELLO\n";
  if (mode == "extra-token") return "RTEF1 12345678 1 HELLO extra\n";
  if (mode == "short-frame") return "RTEF1 12345678 1\n";
  if (mode == "partial-hello") return "RTEF1 12345678 1 HEL";
  assert(mode == "unframed-hello"); return "HELLO\n";
}
static std::string appCheckpoint() {
  assert(runtime->active() && appStackLive && !finis && !runtime->nativeFilesServiceSafe());
  const unsigned providerBefore = provider.totalCalls();
  const auto rc = app->service_checkpoint();
  assert(rc == RISC_RUNTIME_CHECKPOINT_OK || rc == RISC_RUNTIME_CHECKPOINT_BUSY);
  const auto out = drain();
  assert(provider.totalCalls() == providerBefore);
  assert(runtime->active() && appStackLive && !finis && !runtime->nativeFilesServiceSafe());
  printf("LIVE case=%s checkpoint_rc=%d requested=%u active=%u app_stack_live=1 app_fini=0 runtime_parked=0 hello_success=%u\n",
         mode.c_str(), int(rc), N::transferRequested(), N::transferActive(), helloSuccess(out));
  if (mode != "legacy-live") assert(!helloSuccess(out) && !N::transferActive());
  printf("WIRE live=%s%s", out.empty() ? "<empty>" : out.c_str(), out.empty() ? "\n" : "");
  return out;
}
static void expectRefusedBoot() {
  assert(!eligible()); input(frame(sid, 1, "HELLO")); appCheckpoint();
  assert(!N::transferRequested() && !N::transferActive() && !app->exit_requested() && N::exitSafe());
  // Production refusal occurs before SID commitment; no actual bank is mutated.
  N::disconnect(); assert(!N::transferRequested() && N::exitSafe());
  puts("BOOT_GATE refused_before_session=1 provider_calls=0 PASS");
}
extern "C" void test_atomic_app(unsigned hook, unsigned child) {
  app = risc_runtime_get_api(1); assert(app);
  if (hook != 1) {
    if (hook == 0) ++inits;
    else {
      assert(hook == 2); ++finis; assert(!appStackLive);
      if (finiDeadlineCase()) {
        assert(shutdowns == 1 && N::transferRequested() && !N::transferActive() && !N::outputSize());
        nowMs = RiscSerial::Files::AdmissionWaitMs + 1;
        printf("FINI admission_deadline_crossed=1 logical_ms=%u first_shutdown_complete=1\n", nowMs);
      }
    }
    assert(app->service_checkpoint() == RISC_RUNTIME_CHECKPOINT_CONTEXT); return;
  }
  if (child) { ++childEntries; assert(!"queued child must not run after transfer request"); return; }
  ++entries; appStackLive = true;
  struct StackGuard { ~StackGuard() { appStackLive = false; } } stack;
  assert(inits == 1 && entries == 1 && !finis);
  if (mode == "unconfirmed-boot") expectRefusedBoot();
  assert(app->confirm_boot() && confirmedBoot && confirmations == 1);
  printf("BOOT explicit_confirmed=1 pending_pair=%u actual_bank_store_qualification=0\n", pendingPair);
  if (mode == "pending-pair") { expectRefusedBoot(); pendingPair = false; }
  if (mode == "parked-hello") return;
  if (mode == "late-postqueued" || mode == "postqueued-direct") {
    assert(app->request_launch("child.elf"));
    if (mode == "late-postqueued") lateArmed = true;
    else {
      input(frame(sid, 1, "HELLO"));
      const auto before = dispatches + controls;
      assert(app->service_checkpoint() == RISC_RUNTIME_CHECKPOINT_BUSY);
      assert(dispatches + controls == before && !N::outputSize());
    }
    puts("LAUNCH queued_before_transfer=1 returning_to_real_cleanup=1"); return;
  }
  if (malformedCase()) {
    input(malformedInput()); appCheckpoint();
    assert(!N::transferRequested() && !N::transferActive() && !app->exit_requested());
    printf("CONTROL case=%s no_transfer_intent=1 PASS\n", mode.c_str()); return;
  }
  storageReady = mode != "live-control-hello" && mode != "parked-storage-busy";
  input(frame(sid, 1, "HELLO"));
  const auto before = dispatches + controls; app->yield_ms(1);
  assert(dispatches + controls == before && !N::outputSize());
  const auto out = appCheckpoint();
  if (mode == "legacy-live") { assert(helloSuccess(out)); return; }
  assert(N::transferRequested() && app->exit_requested());
  assert(!app->request_launch("child.elf"));
  if (mode == "disconnect-before-park") {
    N::disconnect(); assert(N::transferRequested() && !N::transferActive() && app->exit_requested());
    appCheckpoint(); assert(!N::outputSize());
    input(frame(sid, 1, "HELLO")); appCheckpoint();
    assert(N::transferRequested() && app->exit_requested());
    puts("DISCONNECT pre_park_protocol_cancelled=1 transfer_intent_latched=1 fresh_retry_requested=1 PASS");
  }
  failShutdown = mode == "failed-shutdown";
  if (mode != "parked-storage-busy") storageReady = true;
  fflush(stdout);
}
static void write(const std::string& path, const char* bytes) {
  std::ofstream file(path); file << bytes; assert(file.good());
}
static bool serviceParked() {
  assert(!appStackLive && finis == 1 && !childEntries && runtime->nativeFilesServiceSafe());
  const bool result = runtime->nativeFilesCheckpoint();
  if (result) assert(runtime->nativeFilesServiceSafe() && !runtime->active() && !risc_runtime_get_api(1));
  return result;
}
static std::string parkedCommand(unsigned session, unsigned seq, const char* body, int expected = 0) {
  input(frame(session, seq, body)); assert(serviceParked()); const auto out = drain();
  char prefix[80]; snprintf(prefix, sizeof(prefix), "RTEF1 %08x %u %d", session, seq, expected);
  assert(out.rfind(prefix, 0) == 0);
  printf("WIRE parked=%s", out.c_str()); return out;
}
static void freshSession() {
  ++sid; sequence = 1;
  assert(helloSuccess(parkedCommand(sid, sequence, "HELLO")));
  assert(N::transferActive() && transferEntries == 1);
}
static void ordinaryRead() {
  const auto calls = provider.calls;
  parkedCommand(sid, ++sequence, "PATH 0 2f66696c65");
  parkedCommand(sid, ++sequence, "GET 1"); assert(!files.exitSafe());
  assert(parkedCommand(sid, ++sequence, "READ 0").find("616263646566") != std::string::npos);
  parkedCommand(sid, ++sequence, "CLOSE"); assert(files.exitSafe() && provider.handles.empty());
  const auto prior = [&](const char* key) { const auto found = calls.find(key); return found == calls.end() ? 0u : found->second; };
  assert(provider.calls["open"] == prior("open") + 1 && provider.calls["read"] == prior("read") + 1 && provider.calls["close"] == prior("close") + 1);
}
static void successAndExitRetained() {
  printf("RESULT case=%s violations=0 PASS\n", mode.c_str()); fflush(stdout); _exit(0);
}
int main(int argc, char** argv) {
  assert(argc == 3); const std::string root = argv[1]; mode = argv[2];
  printf("MODEL case=%s selected_mode=%u provider=synthetic loaded_elf=1 boot_predicate=explicit-fixture physical_timing_claim=0\n",
         mode.c_str(), mode != "legacy-live"); fflush(stdout);
  pendingPair = mode == "pending-pair";
  write(root + "/board.json", R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  write(root + "/boot.json", R"({"board":"board.json","default_app":"default.elf","drivers":[]})");
  assert(files.bind(&provider.api, owner, safe));
  const RiscSerial::Namespace namespaces[] = {{1, "Synthetic files", &files, true}};
  const N::Admission admission{namespaces, 1, safe}; assert(N::prepare(&admission, owner, safe));
  if (mode != "legacy-live") assert(N::enableTransferMode(eligible));
  RiscBoot::Port port{owner, [](risc_runtime_health_v1*) { return true; }, [](uint32_t) {}, [](const char*) { return true; }};
  port.appExitSafe = exitSafe; port.providerStorageSafe = exitSafe; port.confirmBoot = confirmBoot;
  port.serialFilesCheckpoint = checkpoint; port.serialFilesControlCheckpoint = controlCheckpoint;
  port.serialFilesShutdown = shutdown; port.serialFilesStop = stop;
  port.serialFilesReady = []() { return storageReady; }; port.sleepServicesReady = servicesReady;
  if (mode != "legacy-live") {
    port.serialTransferRequested = N::transferRequested; port.serialTransferEnter = enterTransferMode;
    port.serialFilesPoll = poll;
  }
  RiscTest::FileSources sources({root}); runtime = new RiscBoot::Runtime(port);
  assert(sources.bind(*runtime) && runtime->prepare(root.c_str()));
  const bool result = runtime->run();
  if (mode == "failed-shutdown") {
    assert(!result && runtime->retained() && !finis && !childEntries && shutdowns == 1);
    assert(!N::transferActive() && !transferEntries && !N::outputSize() && provider.totalCalls() == 0);
    puts("RETENTION failed_native_cleanup=1 app_image_not_finalized=1 no_hello_success=1 no_transfer_activation=1 PASS");
    successAndExitRetained();
  }
  assert(result && !runtime->retained() && confirmedBoot && confirmations == 1);
  assert(!appStackLive && inits == 1 && entries == 1 && finis == 1 && !childEntries);
  assert(!runtime->active() && !risc_runtime_get_api(1) && runtime->nativeFilesServiceSafe());
  assert(files.exitSafe() && shutdowns == 2);
  if (!finiDeadlineCase()) assert(!N::outputSize());
  printf("PARKED app_fini=1 child_entries=0 runtime_parked=1 deferred_request=%u polls=%u\n", N::transferRequested(), polls);
  if (mode == "late-postqueued") assert(lateInjected && N::transferRequested());
  if (finiDeadlineCase()) {
    const auto expectedBusy = "RTEF1 12345678 1 " + std::to_string(RISC_FILES_BUSY) + "\n";
    const std::string reply(N::output(), N::outputSize());
    printf("FINI_DEADLINE observed_reply_bytes=%zu expected_busy_bytes=%zu hello_success=%u mode_active=%u\n",
           reply.size(), expectedBusy.size(), helloSuccess(reply), N::transferActive()); fflush(stdout);
    assert(reply == expectedBusy && !helloSuccess(reply) && !N::transferActive());
    if (mode == "fini-deadline-disconnect") {
      N::disconnect(); assert(!N::outputSize() && N::transferRequested() && !N::transferActive());
      puts("FINI_DEADLINE busy_reply_survived_cleanup=1 genuine_disconnect_discarded_reply=1 PASS");
    } else {
      assert(drain() == expectedBusy);
      puts("FINI_DEADLINE busy_reply_survived_cleanup=1 premature_hello_success=0 PASS");
    }
    input(frame(++sid, 1, "HELLO"));
  } else if (mode == "disconnect-after-return") {
    assert(N::transferRequested() && !N::transferActive()); N::disconnect();
    assert(N::transferRequested() && !N::transferActive()); assert(serviceParked());
    assert(N::transferActive() && !N::outputSize());
    input(frame(sid, 1, "HELLO"));
  } else if (malformedCase() || mode == "parked-hello" || mode == "legacy-live") {
    input(frame(sid, 1, "HELLO"));
  } else assert(N::transferRequested()); // Original app HELLO survived both shutdown calls.
  if (mode == "parked-storage-busy") {
    assert(!storageReady && serviceParked());
    assert(!N::transferActive() && !helloSuccess(drain()) && N::transferRequested());
    storageReady = true; puts("PARKED_READY deferred_while_storage_busy=1 PASS");
  }
  // A HELLO arriving only after run() needs one parser checkpoint to latch
  // intent, then the next trusted full-ready parked checkpoint enters mode.
  if ((malformedCase() || mode == "parked-hello") && !N::transferRequested()) {
    assert(serviceParked() && N::transferRequested() && !N::transferActive() && !N::outputSize());
  }
  assert(serviceParked()); const auto firstHello = drain(); assert(helloSuccess(firstHello));
  printf("WIRE parked=%s", firstHello.c_str());
  if (mode != "legacy-live") assert(N::transferActive() && transferEntries == 1);
  else assert(!N::transferActive() && !transferEntries);
  if (mode == "duplicate-hello") {
    const auto before = provider.totalCalls();
    assert(parkedCommand(sid, 1, "HELLO") == firstHello && provider.totalCalls() == before);
  } else if (mode == "duplicate-after-ready-loss") {
    storageReady = false;
    const auto calls = provider.totalCalls(), transitions = transferEntries;
    assert(parkedCommand(sid, 1, "HELLO") == firstHello);
    assert(provider.totalCalls() == calls && transferEntries == transitions && N::transferActive());
    input(frame(sid + 1, 1, "HELLO")); assert(serviceParked() && !N::outputSize());
    nowMs += RiscSerial::Files::AdmissionWaitMs; assert(serviceParked());
    const auto refused = drain();
    assert(!helloSuccess(refused) && refused.find(" " + std::to_string(RISC_FILES_BUSY) + "\n") != std::string::npos);
    assert(provider.totalCalls() == calls && transferEntries == transitions && N::transferActive());
    puts("REPLAY ready_loss_historical_duplicate=1 fresh_sid_withheld_then_busy=1 provider_calls=0 new_mode_transitions=0 PASS");
    storageReady = true; freshSession();
  } else if (mode == "fresh-sid") freshSession();
  else if (mode == "idle-keepalive") {
    for (unsigned i = 0; i < 4; ++i) {
      nowMs += 20000; assert(parkedCommand(sid, ++sequence, "NS 0").find(" 0 NS ") != std::string::npos);
      assert(N::transferActive() && runtime->nativeFilesServiceSafe());
    }
    puts("TIMEOUT keepalive_elapsed_ms=80000 same_sid_preserved=1 dedicated_mode_latched=1 PASS");
  } else if (mode == "idle-expiry") {
    nowMs += RiscSerial::Files::TimeoutMs - 1; assert(serviceParked());
    ++nowMs; assert(serviceParked()); assert(N::transferActive() && !N::outputSize());
    parkedCommand(sid, ++sequence, "NS 0", -10); freshSession();
    puts("TIMEOUT idle_sid_expired_at_ms=30000 fresh_hello_required=1 dedicated_mode_latched=1 PASS");
  } else if (mode == "disconnect-active") {
    N::disconnect(); assert(serviceParked() && N::transferActive());
    parkedCommand(sid, ++sequence, "NS 0", -10); freshSession();
    puts("DISCONNECT active_mode_preserved=1 fresh_session_required=1 PASS");
  } else if (mode == "partial-timeout" || mode == "failed-close") {
    parkedCommand(sid, ++sequence, "PATH 0 2f66696c65"); parkedCommand(sid, ++sequence, "GET 1");
    assert(!files.exitSafe());
    if (mode == "failed-close") {
      provider.results["close"] = RISC_FILES_IO; N::disconnect(); assert(!serviceParked());
      assert(runtime->retained() && files.retained() && !N::custodySafe() && N::transferActive());
      const auto before = provider.totalCalls(); assert(!N::shutdown() && provider.totalCalls() == before);
      puts("RETENTION failed_provider_close=1 no_close_retry=1 mode_latched=1 PASS"); successAndExitRetained();
    }
    auto partial = frame(sid, sequence + 1, "READ 0"); partial.pop_back(); input(partial);
    nowMs += RiscSerial::Files::TimeoutMs - 1; assert(serviceParked() && !files.exitSafe());
    ++nowMs; assert(serviceParked() && files.exitSafe() && provider.handles.empty());
    assert(N::transferActive() && !N::outputSize()); freshSession();
    puts("TIMEOUT partial_request_expired=1 live_handle_closed=1 mode_latched=1 PASS");
  }
  ordinaryRead();
  assert(runtime->stopNativeFiles() && stops == 1 && N::exitSafe());
  delete runtime; runtime = nullptr;
  puts("PARKED_TRANSFER real_runtime_parked=1 generic_provider_read=1 checked_stop=1 PASS");
  printf("RESULT case=%s violations=0 PASS\n", mode.c_str());
}
#else
// Regression-first host oracle for the proposed selected Waveshare transfer mode.
// Production has no dedicated-mode selector yet: no shim implements that policy.
// This fixture intentionally leaves the existing Runtime/NativeSerialFiles/Files
// path unchanged and requires HELLO success to wait for the real parked boundary.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main storage_files_adapter_fixture_main
#include "storage_files_adapter_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/NativeSerialFiles.h"
#include "RuntimeFileFixture.h"
#include <fstream>

namespace N = RiscCpu::NativeSerialFiles;
static GenericProvider provider;
static RiscBoot::RuntimeFiles files;
static RiscBoot::Runtime* runtime;
static const risc_runtime_api_v1* app;
static std::string mode;
static unsigned dispatches, controls, shutdowns, stops, inits, entries, finis, confirmations;
static unsigned violations;
static bool appStackLive, confirmedBoot, storageReady = true;
static uint32_t nowMs;
static bool owner() { return true; }
static bool safe() { return true; }
static bool exitSafe() { return N::custodySafe(); }
static bool servicesReady() { return storageReady && N::exitSafe(); }
extern "C" int64_t esp_timer_get_time() { return int64_t(nowMs) * 1000; }
extern "C" bool xPortInIsrContext() { return false; }

static bool checkpoint() {
  // active() is deliberately false during the native callback even when the
  // loaded ELF remains on the stack. It is not a parked-runtime oracle.
  assert(!runtime->active() && !risc_runtime_get_api(1));
  ++dispatches;
  printf("DISPATCH kind=storage app_stack_live=%u app_fini=%u temporary_app_authority=0\n",
         appStackLive, finis);
  return N::checkpoint();
}
static bool controlCheckpoint() {
  assert(!runtime->active() && !risc_runtime_get_api(1));
  ++controls;
  printf("DISPATCH kind=control app_stack_live=%u app_fini=%u temporary_app_authority=0\n",
         appStackLive, finis);
  return N::controlCheckpoint();
}
static bool shutdown() { ++shutdowns; return N::shutdown(); }
static bool stop() { ++stops; return N::stop(); }
static bool confirmBoot() { ++confirmations; confirmedBoot = true; return true; }
static void input(const std::string& bytes) { for (unsigned char b : bytes) N::input(b); }
static std::string drain() {
  std::string out;
  while (N::outputSize()) {
    const size_t size = N::outputSize();
    out.append(N::output(), size); N::consumeOutput(size);
  }
  return out;
}
static bool helloSuccess(const std::string& out) {
  return out.rfind("RTEF1 12345678 1 0 HELLO ", 0) == 0;
}
static std::string liveInput() {
  if (mode == "malformed-sid") return "RTEF1 1234567z 1 HELLO\n";
  if (mode == "short-sid") return "RTEF1 1234567 1 HELLO\n";
  if (mode == "zero-sid") return "RTEF1 00000000 1 HELLO\n";
  if (mode == "wrong-sequence") return "RTEF1 12345678 2 HELLO\n";
  if (mode == "extra-token") return "RTEF1 12345678 1 HELLO extra\n";
  if (mode == "short-frame") return "RTEF1 12345678 1\n";
  if (mode == "partial-hello") return "RTEF1 12345678 1 HEL";
  if (mode == "unframed-hello") return "HELLO\n";
  assert(mode == "live-hello" || mode == "live-control-hello");
  return "RTEF1 12345678 1 HELLO\n";
}
extern "C" void test_atomic_app(unsigned hook, unsigned child) {
  assert(!child);
  app = risc_runtime_get_api(1); assert(app);
  if (hook != 1) {
    if (hook == 0) ++inits;
    else { assert(hook == 2); ++finis; assert(!appStackLive); }
    assert(app->service_checkpoint() == RISC_RUNTIME_CHECKPOINT_CONTEXT);
    return;
  }
  ++entries; appStackLive = true;
  struct StackGuard { ~StackGuard() { appStackLive = false; } } stack;
  assert(inits == 1 && entries == 1 && !finis);
  assert(app->confirm_boot() && confirmedBoot && confirmations == 1);
  printf("BOOT explicit_confirmed=1 confirmations=%u admission_policy_under_test=0\n", confirmations);
  assert(runtime->active() && !runtime->nativeFilesServiceSafe());
  if (mode == "parked-hello") return;
  storageReady = mode != "live-control-hello";
  const unsigned providerBefore = provider.totalCalls();
  const auto dispatchBefore = dispatches, controlBefore = controls;
  input(liveInput());
  assert(!N::outputSize());
  app->yield_ms(1);
  assert(dispatches == dispatchBefore && controls == controlBefore && !N::outputSize());
  const int32_t result = app->service_checkpoint();
  assert(result == RISC_RUNTIME_CHECKPOINT_OK || result == RISC_RUNTIME_CHECKPOINT_BUSY);
  const auto out = drain();
  const bool parked = runtime->nativeFilesServiceSafe();
  const bool success = helloSuccess(out);
  assert(runtime->active() && appStackLive && !finis && !parked);
  assert(provider.totalCalls() == providerBefore); // HELLO/control parsing is RAM-only.
  printf("LIVE case=%s confirmed=%u checkpoint_rc=%d app_active=%u app_stack_live=%u "
         "app_fini=%u runtime_parked=%u hello_success=%u storage_dispatches=%u control_dispatches=%u provider_calls=%u\n",
         mode.c_str(), confirmedBoot, int(result), runtime->active(), appStackLive,
         finis, parked, success, dispatches - dispatchBefore, controls - controlBefore,
         provider.totalCalls() - providerBefore);
  printf("WIRE live=%s%s", out.empty() ? "<empty>" : out.c_str(), out.empty() ? "\n" : "");
  if (success && !parked) {
    ++violations;
    puts("REGRESSION FALSE_READY_HELLO: successful HELLO while loaded app is live and Runtime is not parked");
  }
  if (mode != "live-hello" && mode != "live-control-hello") {
    assert(!success);
    if (mode == "wrong-sequence" || mode == "extra-token")
      assert(out.find(" -10\n") != std::string::npos);
    else assert(out.empty());
    printf("CONTROL case=%s rejected_or_incomplete_hello=1 PASS\n", mode.c_str());
  }
  // Return cooperatively; production performs its real shutdown, ELF fini,
  // unload and graph settlement. No fixture callback fabricates parking.
  storageReady = true;
  fflush(stdout);
}
static void write(const std::string& path, const char* bytes) {
  std::ofstream file(path); file << bytes; assert(file.good());
}
static std::string parkedCommand(unsigned sequence, const char* body) {
  assert(!appStackLive && finis == 1 && runtime->nativeFilesServiceSafe());
  input("RTEF1 12345678 " + std::to_string(sequence) + " " + body + "\n");
  assert(runtime->nativeFilesCheckpoint());
  assert(runtime->nativeFilesServiceSafe() && !runtime->active() && !risc_runtime_get_api(1));
  const auto out = drain();
  const auto prefix = "RTEF1 12345678 " + std::to_string(sequence) + " 0 ";
  assert(out.rfind(prefix, 0) == 0);
  printf("WIRE parked=%s", out.c_str());
  return out;
}
int main(int argc, char** argv) {
  assert(argc == 3); const std::string root = argv[1]; mode = argv[2];
  printf("MODEL case=%s desired_profile=waveshare-dedicated production_selector=absent "
         "provider=synthetic loaded_elf=1 physical_timing_claim=0\n", mode.c_str());
  write(root + "/board.json", R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  write(root + "/boot.json", R"({"board":"board.json","default_app":"default.elf","drivers":[]})");
  assert(files.bind(&provider.api, owner, safe));
  const RiscSerial::Namespace namespaces[] = {{1, "Synthetic files", &files, true}};
  const N::Admission admission{namespaces, 1, safe}; assert(N::prepare(&admission, owner, safe));
  RiscBoot::Port port{owner, [](risc_runtime_health_v1*) { return true; },
                     [](uint32_t) {}, [](const char*) { return true; }};
  port.appExitSafe = exitSafe; port.providerStorageSafe = exitSafe; port.confirmBoot = confirmBoot;
  port.serialFilesCheckpoint = checkpoint; port.serialFilesControlCheckpoint = controlCheckpoint;
  port.serialFilesShutdown = shutdown; port.serialFilesStop = stop;
  port.serialFilesReady = []() { return storageReady; }; port.sleepServicesReady = servicesReady;
  RiscTest::FileSources sources({root}); runtime = new RiscBoot::Runtime(port);
  assert(sources.bind(*runtime) && runtime->prepare(root.c_str()));
  assert(runtime->run() && !runtime->retained());
  assert(confirmedBoot && confirmations == 1 && !appStackLive && inits == 1 && entries == 1 && finis == 1);
  assert(!runtime->active() && !risc_runtime_get_api(1) && runtime->nativeFilesServiceSafe());
  assert(N::exitSafe() && files.exitSafe() && shutdowns == 2);
  printf("PARKED confirmed=%u app_stack_live=%u app_fini=%u runtime_parked=%u shutdowns=%u\n",
         confirmedBoot, appStackLive, finis, runtime->nativeFilesServiceSafe(), shutdowns);
  assert(helloSuccess(parkedCommand(1, "HELLO")));
  parkedCommand(2, "PATH 0 2f66696c65");
  parkedCommand(3, "GET 1"); assert(!files.exitSafe());
  assert(parkedCommand(4, "READ 0").find("616263646566") != std::string::npos);
  parkedCommand(5, "CLOSE"); assert(files.exitSafe() && provider.handles.empty());
  assert(provider.calls["open"] == 1 && provider.calls["read"] == 1 && provider.calls["close"] == 1);
  assert(runtime->stopNativeFiles() && stops == 1 && N::exitSafe());
  delete runtime; runtime = nullptr;
  printf("PARKED_TRANSFER loaded_app_finalized=1 real_runtime_parked=1 generic_provider_read=1 checked_stop=1 PASS\n");
  printf("RESULT case=%s violations=%u %s\n", mode.c_str(), violations, violations ? "FAIL" : "PASS");
  return violations ? 1 : 0;
}

#endif // RISC_TEST_TRANSFER_MODE
