#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

static uintptr_t hook_begin;
static uint32_t hook_size;
static int64_t writes_left = -1;
static uint32_t writes_seen;
static bool writeHook(const volatile void* address, uint32_t, uint32_t) {
  if (reinterpret_cast<uintptr_t>(address) - hook_begin >= hook_size) return true;
  ++writes_seen;
  if (writes_left < 0) return true;
  if (!writes_left) return false;
  --writes_left;
  return true;
}
#define RISC_FAILURE_EVIDENCE_WRITE_HOOK writeHook
#include "runtime/diagnostics/FailureEvidenceStore.h"
using namespace RiscFailureEvidence;

static const uint8_t firmware[32] = {
  1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
  17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32
};
static Registers registers() {
  return {1,2,1,0x40001234,0x3ff01000,0x40005555,0x60025,28,0xdeadbeef,
          RISC_FAILURE_REGISTERS | RISC_FAILURE_ABORT, 42};
}
static void hook(Image& image, int64_t budget = -1) {
  hook_begin = reinterpret_cast<uintptr_t>(&image);
  hook_size = sizeof(image);
  writes_left = budget;
  writes_seen = 0;
}
static void unhook() { hook_size = 0; writes_left = -1; }
static risc_failure_evidence_v1 get(const Image& image, const State& state) {
  risc_failure_evidence_v1 out;
  assert(read(image, state, out) == RISC_FAILURE_EVIDENCE_OK);
  assert(out.api_version == 1 && out.struct_size == sizeof(out));
  return out;
}
static void absent(const Image& image, const State& state) {
  risc_failure_evidence_v1 out;
  std::memset(&out, 0xee, sizeof(out));
  assert(read(image, state, out) == RISC_FAILURE_EVIDENCE_NONE);
  assert(out.frame_count == 0 && out.history_count == 0 && out.flags == 0);
}
static void initialize(Image& image, State& state) {
  assert(boot(image, state, Reset::PowerOn, 1, firmware));
  assert(state.ready);
  assert(breadcrumb(image, state, "reader.elf", 0xfedcba9876543210ull,
                    RISC_FAILURE_PHASE_MAIN, 2));
}
static void capture(Image& image, State& state) {
  initialize(image, state);
  assert(capturePanicBase(image, state, registers()));
}

static void ordinaryAndRegisters() {
  Image image{};
  State state;
  absent(image, state);
  assert(!capturePanicBase(image, state, registers()));
  assert(!captureRetention(image, state, -1, "early"));
  assert(acknowledge(image, state, 1, 1) == RISC_FAILURE_EVIDENCE_NONE);
  assert(!boot(image, state, Reset::PowerOn, 1, nullptr));
  initialize(image, state);
  absent(image, state);
  assert(capturePanicBase(image, state, registers()));
  const Image unchanged = image;
  auto out = get(image, state);
  assert(out.flags == (RISC_FAILURE_PENDING | RISC_FAILURE_REGISTERS |
                       RISC_FAILURE_CONTEXT | RISC_FAILURE_ABORT));
  assert(out.kind == RISC_FAILURE_NATIVE_PANIC && out.status == 0);
  assert(out.core == 1 && out.exception == 2 && out.pseudo_excause == 1);
  assert(out.pc == registers().pc && out.sp == registers().sp && out.a0 == registers().a0);
  assert(out.ps == registers().ps && out.exccause == 28 && out.excvaddr == 0xdeadbeef);
  assert(out.phase == RISC_FAILURE_PHASE_MAIN && out.role == 2);
  assert(out.invocation == 0xfedcba9876543210ull);
  assert(std::strcmp(out.application, "reader.elf") == 0);
  assert(std::memcmp(out.firmware_sha256, firmware, 32) == 0);
  assert(out.captured_reset_hint == 42 && out.current_reset_reason == 1);
  assert(out.frame_count == 0 && out.history_count == 0 && out.stack_status == 0);
  for (unsigned i = 0; i < 3; ++i) assert(get(image, state).flags & RISC_FAILURE_PENDING);
  assert(std::memcmp(&unchanged, &image, sizeof(image)) == 0);
  // Re-entering healthy setup in the same physical boot cannot reset its state.
  assert(boot(image, state, Reset::Brownout, 999, nullptr));
  assert(std::memcmp(&unchanged, &image, sizeof(image)) == 0);
  assert(get(image, state).current_reset_reason == 1);
  assert(state.panic_started && state.boot == 1);
  assert(!capturePanicBase(image, state, registers()));
  assert(!captureRetention(image, state, -2, "later"));
  assert(!breadcrumb(image, state, "later.elf", 2, RISC_FAILURE_PHASE_MAIN, 0));
  assert(std::memcmp(&unchanged, &image, sizeof(image)) == 0);

  Image no_frame{};
  State no_frame_state;
  initialize(no_frame, no_frame_state);
  Registers raw = registers();
  raw.flags = RISC_FAILURE_ABORT;
  assert(capturePanicBase(no_frame, no_frame_state, raw));
  out = get(no_frame, no_frame_state);
  assert(!(out.flags & RISC_FAILURE_REGISTERS));
  assert(out.pc == 0 && out.sp == 0 && out.a0 == 0 && out.ps == 0);
  assert(out.exccause == 0 && out.excvaddr == 0);
  assert(out.core == 1 && out.exception == 2 && out.pseudo_excause == 1);
  Image invalid_flags{};
  State invalid_flags_state;
  initialize(invalid_flags, invalid_flags_state);
  raw.flags = 0x80000000u;
  assert(!capturePanicBase(invalid_flags, invalid_flags_state, raw));
  assert(!invalid_flags_state.panic_started);
  const uint32_t before_sequence = invalid_flags_state.sequence;
  raw.flags = RISC_FAILURE_REGISTERS;
  raw.pseudo_excause = 2;
  assert(!capturePanicBase(invalid_flags, invalid_flags_state, raw));
  raw.pseudo_excause = UINT32_MAX;
  assert(!capturePanicBase(invalid_flags, invalid_flags_state, raw));
  assert(!invalid_flags_state.panic_started && invalid_flags_state.sequence == before_sequence);
  raw.pseudo_excause = 0;
  raw.flags = 0;
  assert(capturePanicBase(invalid_flags, invalid_flags_state, raw));
  out = get(invalid_flags, invalid_flags_state);
  assert(!(out.flags & (RISC_FAILURE_REGISTERS | RISC_FAILURE_ABORT)));
  assert(out.pseudo_excause == 0 && out.captured_reset_hint == 42 && !out.pc && !out.sp);
}

static void resetClassesAndFirmware() {
  Image original{};
  State captured;
  capture(original, captured);
  const Reset warm[] = {Reset::Software, Reset::DeepSleep, Reset::Panic,
    Reset::InterruptWatchdog, Reset::TaskWatchdog, Reset::Watchdog};
  for (const auto reset : warm) {
    Image image = original;
    State state;
    assert(boot(image, state, reset, 99, firmware));
    const auto out = get(image, state);
    assert(out.kind == RISC_FAILURE_NATIVE_PANIC && out.current_reset_reason == 99);
    assert(out.record_boot == captured.boot && state.boot == captured.boot + 1);
    assert(out.flags & RISC_FAILURE_PENDING);
    assert(out.captured_reset_hint == 42);
  }
  const Reset cold[] = {Reset::PowerOn, Reset::Brownout, Reset::Unknown, Reset(999)};
  for (const auto reset : cold) {
    Image image = original;
    State state;
    assert(boot(image, state, reset, 100, firmware));
    if (reset == Reset::Brownout) {
      const auto out = get(image, state);
      assert(out.kind == RISC_FAILURE_RESET_ONLY && out.flags == RISC_FAILURE_PENDING);
      assert(out.current_reset_reason == 100 && out.captured_reset_hint == 100);
      assert(!out.application[0] && !out.invocation && !out.pc && !out.frame_count);
      assert(!out.phase && !out.role && !out.history_count);
    } else absent(image, state);
    assert(!image.base.magic && !image.extension.magic && !image.acknowledgement.magic);
    State next;
    assert(boot(image, next, Reset::Software, 3, firmware));
    absent(image, next);
  }
  uint8_t other[32];
  std::memcpy(other, firmware, 32);
  other[31] ^= 1;
  for (const auto reset : warm) {
    Image image = original;
    State state;
    assert(boot(image, state, reset, 6, other));
    if (Detail::crash(reset)) {
      auto out = get(image, state);
      assert(out.kind == RISC_FAILURE_RESET_ONLY && !(out.flags & RISC_FAILURE_CONTEXT));
      assert(std::memcmp(out.firmware_sha256, other, 32) == 0);
    } else absent(image, state);
  }
  Image collision = original;
  collision.base.firmware_sha256[0] ^= 0x80;
  collision.base.checksum = Detail::checksum(collision.base);
  State collision_state;
  assert(boot(collision, collision_state, Reset::Panic, 6, firmware));
  assert(get(collision, collision_state).kind == RISC_FAILURE_RESET_ONLY);
  assert(!(get(collision, collision_state).flags & RISC_FAILURE_CONTEXT));
  // A context's compact CRC cannot substitute for the full identity anchor.
  Image contexts{};State old_state;initialize(contexts,old_state);
  const uint32_t other_crc=Detail::crc(other,32);
  for(auto& context:contexts.contexts){
    context.firmware_crc=other_crc;context.checksum=Detail::checksum(context);
  }
  State changed;assert(boot(contexts,changed,Reset::Panic,6,other));
  assert(!(get(contexts,changed).flags & RISC_FAILURE_CONTEXT));
  assert(changed.boot==1);
  assert(breadcrumb(contexts,changed,"new.elf",9,RISC_FAILURE_PHASE_MAIN,1));
  assert(capturePanicBase(contexts,changed,registers()));
  assert(std::strcmp(get(contexts,changed).application,"new.elf")==0);
}

static void acknowledgeAndSynthetic() {
  Image image{};
  State state;
  capture(image, state);
  auto out = get(image, state);
  const Base base = image.base;
  assert(acknowledge(image, state, out.record_boot + 1, out.record_sequence) == RISC_FAILURE_EVIDENCE_STALE);
  assert(acknowledge(image, state, out.record_boot, out.record_sequence + 1) == RISC_FAILURE_EVIDENCE_STALE);
  assert(get(image, state).flags & RISC_FAILURE_PENDING);
  assert(acknowledge(image, state, out.record_boot, out.record_sequence) == RISC_FAILURE_EVIDENCE_OK);
  assert(acknowledge(image, state, out.record_boot, out.record_sequence) == RISC_FAILURE_EVIDENCE_OK);
  assert(!(get(image, state).flags & RISC_FAILURE_PENDING));
  assert(std::memcmp(&base, &image.base, sizeof(base)) == 0);
  State next;
  assert(boot(image, next, Reset::DeepSleep, 5, firmware));
  assert(!(get(image, next).flags & RISC_FAILURE_PENDING));
  assert(breadcrumb(image, next, "next.elf", 22, RISC_FAILURE_PHASE_INIT, 2));
  assert(capturePanicBase(image, next, registers()));
  assert(get(image, next).flags & RISC_FAILURE_PENDING);
  assert(acknowledge(image, next, out.record_boot, out.record_sequence) == RISC_FAILURE_EVIDENCE_STALE);

  Image lost{};
  State reset_only;
  assert(boot(lost, reset_only, Reset::Panic, 41, firmware));
  out = get(lost, reset_only);
  assert(out.kind == RISC_FAILURE_RESET_ONLY && out.flags == RISC_FAILURE_PENDING);
  assert(!out.pc && !out.frame_count && !out.history_count && !out.application[0]);
  assert(acknowledge(lost, reset_only, out.record_boot, out.record_sequence + 1) == RISC_FAILURE_EVIDENCE_STALE);
  assert(acknowledge(lost, reset_only, out.record_boot, out.record_sequence) == RISC_FAILURE_EVIDENCE_OK);
  assert(acknowledge(lost, reset_only, out.record_boot, out.record_sequence) == RISC_FAILURE_EVIDENCE_OK);
  assert(!lost.acknowledgement.magic);
  assert(boot(lost, reset_only, Reset::Panic, 41, firmware));
  assert(!(get(lost, reset_only).flags & RISC_FAILURE_PENDING));
  State later;
  assert(boot(lost, later, Reset::Panic, 41, firmware));
  assert(get(lost, later).flags & RISC_FAILURE_PENDING);
  assert(get(lost, later).record_boot != out.record_boot);
  State healthy;
  assert(boot(lost, healthy, Reset::Software, 3, firmware));
  absent(lost, healthy);

  Image context_only{};
  State before;
  initialize(context_only, before);
  State after;
  assert(boot(context_only, after, Reset::Watchdog, 7, firmware));
  out = get(context_only, after);
  assert(out.kind == RISC_FAILURE_RESET_ONLY && (out.flags & RISC_FAILURE_CONTEXT));
  assert(std::strcmp(out.application, "reader.elf") == 0);
}

static void boundsAndRetention() {
  Image image{};
  State state;
  assert(boot(image, state, Reset::PowerOn, 1, firmware));
  char app[300], detail[300];
  std::memset(app, 'a', sizeof(app));
  std::memset(detail, 'd', sizeof(detail));
  assert(!breadcrumb(image, state, app, 1, 0, 1));
  const Image no_change = image;
  const uint32_t sequence = state.sequence;
  assert(!breadcrumb(image, state, app, 1, RISC_FAILURE_PHASE_MAIN, 3));
  assert(!breadcrumb(image, state, app, 1, RISC_FAILURE_PHASE_MAIN, UINT32_MAX));
  assert(std::memcmp(&image, &no_change, sizeof(image)) == 0 && state.sequence == sequence);
  for (uint32_t role = 0; role <= 2; ++role)
    assert(breadcrumb(image, state, app, 1, RISC_FAILURE_PHASE_MAIN, role));
  for (unsigned i = 0; i < 12; ++i)
    assert(breadcrumb(image, state, app, 100 + i, RISC_FAILURE_PHASE_MAIN, 1));
  assert(captureRetention(image, state, -1234567, detail));
  auto out = get(image, state);
  assert(out.kind == RISC_FAILURE_RETENTION && out.status == -1234567);
  assert(out.exception == 0 && !(out.flags & RISC_FAILURE_REGISTERS));
  assert(std::strlen(out.application) == 192 && std::strlen(out.detail) == 191);
  assert(out.history_count == 8 && out.history[0].invocation == 104 && out.history[7].invocation == 111);
  assert(out.frame_count == 0 && out.stack_status == RISC_FAILURE_STACK_UNAVAILABLE);
  const Image first = image;
  assert(!captureRetention(image, state, -8, "second"));
  assert(std::memcmp(&first, &image, sizeof(image)) == 0);
  assert(capturePanicBase(image, state, registers()));
  out = get(image, state);
  assert(out.kind == RISC_FAILURE_NATIVE_PANIC && !out.detail[0] && out.status == 0);
  assert(out.history_count == 0); // A former record's extension cannot be reused.
  risc_failure_frame_v1 frames[8];
  for (unsigned i = 0; i < 8; ++i) frames[i] = {0x40000000 + 4 * i, 0x3ff00000 + 16 * i};
  const Base base = image.base;
  assert(!captureExtension(image, state, frames, UINT32_MAX, RISC_FAILURE_STACK_LIMIT));
  assert(!captureExtension(image, state, frames, 9, RISC_FAILURE_STACK_LIMIT));
  assert(!captureExtension(image, state, nullptr, 1, RISC_FAILURE_STACK_COMPLETE));
  assert(!captureExtension(image, state, frames, 8, UINT32_MAX));
  assert(captureExtension(image, state, frames, 8, RISC_FAILURE_STACK_LIMIT));
  out = get(image, state);
  assert(out.frame_count == 8 && out.stack_status == RISC_FAILURE_STACK_LIMIT && out.history_count == 8);
  assert(out.frames[7].pc == frames[7].pc && out.frames[7].sp == frames[7].sp);
  assert(std::memcmp(&base, &image.base, sizeof(base)) == 0);
  const Image first_extension = image;
  assert(!captureExtension(image, state, frames, 1, RISC_FAILURE_STACK_COMPLETE));
  assert(std::memcmp(&image, &first_extension, sizeof(image)) == 0);
  state.histories[state.history_slot].count = UINT32_MAX;
  assert(!captureExtension(image, state, frames, 8, RISC_FAILURE_STACK_LIMIT));

  Image empty{};
  State empty_state;
  assert(boot(empty, empty_state, Reset::PowerOn, 1, firmware));
  assert(captureRetention(empty, empty_state, 1, nullptr));
  assert(!get(empty, empty_state).detail[0]);
}

static void failedPresentationPreservesPending() {
  Image image{};State crashed;capture(image,crashed);
  const risc_failure_frame_v1 frames[2]={{0x42001230,0x3fc90000},{0x42005670,0x3fc90020}};
  assert(captureExtension(image,crashed,frames,2,RISC_FAILURE_STACK_COMPLETE));
  const auto original=get(image,crashed);
  State display;assert(boot(image,display,Reset::Panic,4,firmware));
  assert(breadcrumb(image,display,"host.elf",99,RISC_FAILURE_PHASE_MAIN,1));
  const Base base=image.base;const Extension extension=image.extension;
  assert(!captureRetention(image,display,-5,"display did not complete"));
  assert(display.retention_started);
  assert(!capturePanicBase(image,display,registers()));
  assert(display.panic_started);
  assert(std::memcmp(&base,&image.base,sizeof(base))==0);
  assert(std::memcmp(&extension,&image.extension,sizeof(extension))==0);
  auto out=get(image,display);
  assert(out.record_boot==original.record_boot && out.record_sequence==original.record_sequence);
  assert((out.flags&RISC_FAILURE_PENDING) && out.frame_count==2);
  State retry;assert(boot(image,retry,Reset::Software,3,firmware));
  assert(get(image,retry).flags&RISC_FAILURE_PENDING);
  assert(acknowledge(image,retry,original.record_boot,original.record_sequence)==0);
  // A later hard watchdog reset with no capture must not disappear behind an
  // already acknowledged historical record.
  State later;assert(boot(image,later,Reset::Watchdog,7,firmware));
  out=get(image,later);
  assert(out.kind==RISC_FAILURE_RESET_ONLY && (out.flags&RISC_FAILURE_PENDING));
  assert(out.record_boot!=original.record_boot && !out.frame_count && !out.pc);
  assert(acknowledge(image,later,original.record_boot,original.record_sequence)==RISC_FAILURE_EVIDENCE_STALE);
  assert(acknowledge(image,later,out.record_boot,out.record_sequence)==0);
  assert(!(get(image,later).flags&RISC_FAILURE_PENDING));
  assert(boot(image,later,Reset::Panic,4,firmware));
  assert(!(get(image,later).flags&RISC_FAILURE_PENDING));
  assert(capturePanicBase(image,later,registers()));
  assert(get(image,later).kind==RISC_FAILURE_NATIVE_PANIC);
  assert(get(image,later).record_boot==later.boot);
}

static void semanticValidation() {
  static_assert(sizeof(Image) == 1280, "RTC image remains within budget");
  Image original{};
  State state;
  capture(original, state);
  const risc_failure_frame_v1 frame{0x40001000, 0x3ff01000};
  assert(captureExtension(original, state, &frame, 1, RISC_FAILURE_STACK_COMPLETE));
  const auto token = get(original, state);
  assert(acknowledge(original, state, token.record_boot, token.record_sequence) == RISC_FAILURE_EVIDENCE_OK);
  const int slot = Detail::latest(original, state.firmware_crc);
  assert(slot >= 0);
  assert(original.contexts[slot].format == ((1u << 16) | sizeof(Context)));
  assert(original.base.format == ((1u << 16) | sizeof(Base)));
  assert(original.extension.format == ((1u << 16) | sizeof(Extension)));
  assert(original.acknowledgement.format == ((1u << 16) | sizeof(Acknowledgement)));
  for (uint32_t wrong : {1u, (2u << 16) | 240u, (1u << 16) | 239u, UINT32_MAX}) {
    Image bad = original;
    bad.contexts[slot].format = wrong;
    bad.contexts[slot].checksum = Detail::checksum(bad.contexts[slot]);
    assert(!Detail::valid(bad.contexts[slot], state.firmware_crc));
    bad.base.format = wrong;
    bad.base.checksum = Detail::checksum(bad.base);
    absent(bad, state);
    bad = original;
    bad.extension.format = wrong;
    bad.extension.checksum = Detail::checksum(bad.extension);
    assert(!get(bad, state).frame_count);
    bad = original;
    bad.acknowledgement.format = wrong;
    bad.acknowledgement.checksum = Detail::checksum(bad.acknowledgement);
    assert(get(bad, state).flags & RISC_FAILURE_PENDING);
  }
  for (size_t i = 0; i < sizeof(Context::reserved); ++i) {
    Image bad = original;
    bad.contexts[slot].reserved[i] = 1;
    bad.contexts[slot].checksum = Detail::checksum(bad.contexts[slot]);
    assert(!Detail::valid(bad.contexts[slot], state.firmware_crc));
  }
  for (size_t i = 0; i < sizeof(Base::reserved); ++i) {
    Image bad = original;
    bad.base.reserved[i] = 1;
    bad.base.checksum = Detail::checksum(bad.base);
    absent(bad, state);
  }
  for (unsigned i = 0; i < 2; ++i) {
    Image bad = original;
    bad.acknowledgement.reserved[i] = 1;
    bad.acknowledgement.checksum = Detail::checksum(bad.acknowledgement);
    assert(get(bad, state).flags & RISC_FAILURE_PENDING);
  }
  for (uint32_t role : {3u, UINT32_MAX}) {
    Image bad = original;
    bad.contexts[slot].role = role;
    bad.contexts[slot].checksum = Detail::checksum(bad.contexts[slot]);
    assert(!Detail::valid(bad.contexts[slot], state.firmware_crc));
    bad.base.role = role;
    bad.base.checksum = Detail::checksum(bad.base);
    absent(bad, state);
  }
  for (uint32_t pseudo : {2u, UINT32_MAX}) {
    Image bad = original;
    bad.base.pseudo_excause = pseudo;
    bad.base.checksum = Detail::checksum(bad.base);
    absent(bad, state);
  }
  for (unsigned i = 0; i < 32; ++i) {
    Image bad = original;
    bad.base.firmware_sha256[i] ^= 1;
    bad.base.checksum = Detail::checksum(bad.base);
    absent(bad, state);
  }
  for (unsigned i = 0; i < 3; ++i) {
    Image bad = original;
    if (i == 0) bad.acknowledgement.firmware_crc ^= 1;
    if (i == 1) ++bad.acknowledgement.boot;
    if (i == 2) ++bad.acknowledgement.sequence;
    bad.acknowledgement.checksum = Detail::checksum(bad.acknowledgement);
    assert(get(bad, state).flags & RISC_FAILURE_PENDING);
  }
  // A stale call and an already committed acknowledgement perform no RTC writes.
  hook(original, 0);
  assert(acknowledge(original, state, 0, token.record_sequence) == RISC_FAILURE_EVIDENCE_STALE);
  assert(acknowledge(original, state, token.record_boot, 0) == RISC_FAILURE_EVIDENCE_STALE);
  assert(acknowledge(original, state, token.record_boot, token.record_sequence) == RISC_FAILURE_EVIDENCE_OK);
  assert(writes_seen == 0);
  unhook();
}

static void corruption() {
  assert(Detail::crc("123456789", 9) == 0xcbf43926u);
  Image original{};
  State state;
  capture(original, state);
  const risc_failure_frame_v1 frames[] = {{0x40001000, 0x3ff01000}};
  assert(captureExtension(original, state, frames, 1, RISC_FAILURE_STACK_COMPLETE));
  const auto token = get(original, state);
  assert(acknowledge(original, state, token.record_boot, token.record_sequence) == RISC_FAILURE_EVIDENCE_OK);
  for (size_t i = 0; i < sizeof(Base); ++i) {
    Image corrupt = original;
    reinterpret_cast<uint8_t*>(&corrupt.base)[i] ^= 1;
    absent(corrupt, state);
  }
  for (size_t i = 0; i < sizeof(Extension); ++i) {
    Image corrupt = original;
    reinterpret_cast<uint8_t*>(&corrupt.extension)[i] ^= 1;
    const auto out = get(corrupt, state);
    assert(out.pc == token.pc && !out.frame_count && !out.history_count);
  }
  for (size_t i = 0; i < sizeof(Acknowledgement); ++i) {
    Image corrupt = original;
    reinterpret_cast<uint8_t*>(&corrupt.acknowledgement)[i] ^= 1;
    assert(get(corrupt, state).flags & RISC_FAILURE_PENDING);
  }
  Image no_base = original;
  no_base.base.magic = 0;
  const int latest = Detail::latest(no_base, state.firmware_crc);
  assert(latest >= 0);
  for (size_t i = 0; i < sizeof(Context); ++i) {
    Image corrupt = no_base;
    reinterpret_cast<uint8_t*>(&corrupt.contexts[latest])[i] ^= 1;
    assert(!Detail::valid(corrupt.contexts[latest], state.firmware_crc));
    State fresh;
    assert(boot(corrupt, fresh, Reset::Panic, 9, firmware));
    assert(!(get(corrupt, fresh).flags & RISC_FAILURE_CONTEXT));
  }
  Image bad = original;
  bad.base.format = 2;
  bad.base.checksum = Detail::checksum(bad.base);
  absent(bad, state);
  bad = original;
  bad.base.kind = UINT32_MAX;
  bad.base.checksum = Detail::checksum(bad.base);
  absent(bad, state);
  bad = original;
  bad.base.flags |= 0x80000000u;
  bad.base.checksum = Detail::checksum(bad.base);
  absent(bad, state);
  for (unsigned which = 0; which < 6; ++which) {
    bad = original;
    if (which == 0) bad.extension.frame_count = UINT32_MAX;
    if (which == 1) bad.extension.history_count = UINT32_MAX;
    if (which == 2) bad.extension.stack_status = UINT32_MAX;
    if (which == 3) bad.extension.sequence++;
    if (which == 4) bad.extension.format++;
    if (which == 5) bad.extension.history[0].phase = UINT32_MAX;
    bad.extension.checksum = Detail::checksum(bad.extension);
    assert(get(bad, state).frame_count == 0);
  }
  bad = original;
  bad.acknowledgement.format++;
  bad.acknowledgement.checksum = Detail::checksum(bad.acknowledgement);
  assert(get(bad, state).flags & RISC_FAILURE_PENDING);
}

static void interruptedContext() {
  Image original{};
  State original_state;
  initialize(original, original_state);
  Image complete = original;
  State complete_state = original_state;
  hook(complete);
  assert(breadcrumb(complete, complete_state, "new.elf", 99, RISC_FAILURE_PHASE_INIT, 2));
  const uint32_t writes = writes_seen;
  unhook();
  for (uint32_t cutoff = 0; cutoff <= writes; ++cutoff) {
    Image image = original;
    State state = original_state;
    hook(image, cutoff);
    const bool done = breadcrumb(image, state, "new.elf", 99, RISC_FAILURE_PHASE_INIT, 2);
    unhook();
    assert(done == (cutoff == writes));
    assert(capturePanicBase(image, state, registers()));
    const auto out = get(image, state);
    assert(std::strcmp(out.application, done ? "new.elf" : "reader.elf") == 0);
    assert(out.invocation == (done ? 99 : 0xfedcba9876543210ull));
  }
}

static void interruptedBaseExtensionAndAck() {
  Image original{};
  State original_state;
  initialize(original, original_state);
  Image complete = original;
  State complete_state = original_state;
  hook(complete);
  assert(capturePanicBase(complete, complete_state, registers()));
  const uint32_t base_writes = writes_seen;
  unhook();
  for (uint32_t cutoff = 0; cutoff <= base_writes; ++cutoff) {
    Image image = original;
    State state = original_state;
    hook(image, cutoff);
    const bool done = capturePanicBase(image, state, registers());
    unhook();
    assert(done == (cutoff == base_writes));
    assert(!capturePanicBase(image, state, registers()));
    if (done) assert(get(image, state).kind == RISC_FAILURE_NATIVE_PANIC);
    else absent(image, state);
    State rebooted;
    assert(boot(image, rebooted, Reset::Panic, 8, firmware));
    assert(get(image, rebooted).kind == (done ? RISC_FAILURE_NATIVE_PANIC : RISC_FAILURE_RESET_ONLY));
  }
  original = complete;
  original_state = complete_state;
  risc_failure_frame_v1 frames[8];
  for (unsigned i = 0; i < 8; ++i) frames[i] = {0x40000000 + i * 4, 0x3ff00000 + i * 16};
  hook(complete);
  assert(captureExtension(complete, complete_state, frames, 8, RISC_FAILURE_STACK_LIMIT));
  const uint32_t extension_writes = writes_seen;
  unhook();
  for (uint32_t cutoff = 0; cutoff <= extension_writes; ++cutoff) {
    Image image = original;
    State state = original_state;
    hook(image, cutoff);
    const bool done = captureExtension(image, state, frames, 8, RISC_FAILURE_STACK_LIMIT);
    unhook();
    assert(done == (cutoff == extension_writes));
    const auto out = get(image, state);
    assert(out.pc == registers().pc && (out.flags & RISC_FAILURE_PENDING));
    assert(out.frame_count == (done ? 8 : 0));
    assert(std::memcmp(&image.base, &original.base, sizeof(Base)) == 0);
    State rebooted;
    assert(boot(image, rebooted, Reset::Panic, 8, firmware));
    assert(get(image, rebooted).frame_count == (done ? 8 : 0));
  }
  original = complete;
  const auto token = get(complete, complete_state);
  hook(complete);
  assert(acknowledge(complete, complete_state, token.record_boot, token.record_sequence) == RISC_FAILURE_EVIDENCE_OK);
  const uint32_t ack_writes = writes_seen;
  unhook();
  for (uint32_t cutoff = 0; cutoff <= ack_writes; ++cutoff) {
    Image image = original;
    State state = original_state;
    hook(image, cutoff);
    const int32_t result = acknowledge(image, state, token.record_boot, token.record_sequence);
    unhook();
    const bool done = cutoff == ack_writes;
    assert(result == (done ? RISC_FAILURE_EVIDENCE_OK : RISC_FAILURE_EVIDENCE_INVALID));
    auto out = get(image, state);
    assert(bool(out.flags & RISC_FAILURE_PENDING) == !done);
    assert(out.frame_count == 8);
    assert(std::memcmp(&image.base, &original.base, sizeof(Base)) == 0);
    State fresh;
    assert(boot(image, fresh, Reset::Software, 3, firmware));
    assert(bool(get(image, fresh).flags & RISC_FAILURE_PENDING) == !done);
    assert(acknowledge(image, fresh, token.record_boot, token.record_sequence) == RISC_FAILURE_EVIDENCE_OK);
  }
}

static void interruptedRetentionAndReplacement() {
  Image original{};
  State original_state;
  initialize(original, original_state);
  Image complete = original;
  State complete_state = original_state;
  hook(complete);
  assert(captureRetention(complete, complete_state, -73, "first retention"));
  const uint32_t writes = writes_seen;
  unhook();
  bool saw_base_without_extension = false;
  for (uint32_t cutoff = 0; cutoff <= writes; ++cutoff) {
    Image image = original;
    State state = original_state;
    hook(image, cutoff);
    const bool done = captureRetention(image, state, -73, "first retention");
    unhook();
    assert(done == Detail::valid(image.base, state));
    assert(state.retention_started);
    const Image first = image;
    assert(!captureRetention(image, state, -99, "second retention"));
    assert(!breadcrumb(image, state, "later", 1, RISC_FAILURE_PHASE_MAIN, 0));
    assert(std::memcmp(&image, &first, sizeof(image)) == 0);
    if (done) {
      const auto out = get(image, state);
      assert(out.kind == RISC_FAILURE_RETENTION && out.status == -73);
      assert(out.history_count == (cutoff == writes ? 1 : 0));
      if (!out.history_count) saw_base_without_extension = true;
    } else absent(image, state);
    State rebooted;
    assert(boot(image, rebooted, Reset::Panic, 8, firmware));
    assert(get(image, rebooted).kind == (done ? RISC_FAILURE_RETENTION : RISC_FAILURE_RESET_ONLY));
  }
  assert(saw_base_without_extension);

  // A later physical boot may replace old evidence. Before its first write the
  // old record is intact; after invalidation only a fully new commit is valid.
  original = complete;
  original_state = complete_state;
  auto old_token = get(original, original_state);
  assert(acknowledge(original, original_state, old_token.record_boot, old_token.record_sequence)
         == RISC_FAILURE_EVIDENCE_OK);
  State new_boot;
  assert(boot(original, new_boot, Reset::Software, 3, firmware));
  assert(breadcrumb(original, new_boot, "replacement.elf", 77, RISC_FAILURE_PHASE_MAIN, 1));
  complete = original;
  complete_state = new_boot;
  hook(complete);
  assert(capturePanicBase(complete, complete_state, registers()));
  const uint32_t panic_writes = writes_seen;
  unhook();
  for (uint32_t cutoff = 0; cutoff <= panic_writes; ++cutoff) {
    Image image = original;
    State state = new_boot;
    hook(image, cutoff);
    const bool done = capturePanicBase(image, state, registers());
    unhook();
    assert(done == (cutoff == panic_writes));
    assert(!capturePanicBase(image, state, registers()));
    if (cutoff == 0) {
      const auto out = get(image, state);
      assert(out.kind == RISC_FAILURE_RETENTION && !(out.flags & RISC_FAILURE_PENDING));
    } else if (done) {
      const auto out = get(image, state);
      assert(out.kind == RISC_FAILURE_NATIVE_PANIC && (out.flags & RISC_FAILURE_PENDING));
      assert(out.record_boot != old_token.record_boot && !out.history_count && !out.frame_count);
      assert(acknowledge(image, state, old_token.record_boot, old_token.record_sequence)
             == RISC_FAILURE_EVIDENCE_STALE);
    } else absent(image, state);
    State rebooted;
    assert(boot(image, rebooted, Reset::Panic, 8, firmware));
    assert(get(image, rebooted).kind == (done ? RISC_FAILURE_NATIVE_PANIC : RISC_FAILURE_RESET_ONLY));
  }
}

static void overflowAndHeaderRejection() {
  Image image{};
  State state;
  initialize(image, state);
  state.sequence = UINT32_MAX;
  const Image original = image;
  assert(!breadcrumb(image, state, "overflow", 1, RISC_FAILURE_PHASE_MAIN, 1));
  assert(!capturePanicBase(image, state, registers()));
  assert(!captureRetention(image, state, -1, "overflow"));
  assert(std::memcmp(&image, &original, sizeof(image)) == 0);
  int slot = Detail::latest(image, state.firmware_crc);
  image.contexts[slot].boot = UINT32_MAX;
  image.contexts[slot].checksum = Detail::checksum(image.contexts[slot]);
  State exhausted;
  assert(!boot(image, exhausted, Reset::Software, 3, firmware));
  assert(!exhausted.ready && !capturePanicBase(image, exhausted, registers()));
  assert(!boot(image, exhausted, Reset::PowerOn, 1, firmware));
  absent(image, exhausted);
  Image bad = original;
  slot = Detail::latest(bad, state.firmware_crc);
  bad.contexts[slot].format++;
  bad.contexts[slot].checksum = Detail::checksum(bad.contexts[slot]);
  assert(!Detail::valid(bad.contexts[slot], state.firmware_crc));
  bad = original;
  bad.contexts[slot].phase = UINT32_MAX;
  bad.contexts[slot].checksum = Detail::checksum(bad.contexts[slot]);
  assert(!Detail::valid(bad.contexts[slot], state.firmware_crc));
  bad = original;
  bad.contexts[slot].sequence = 0;
  bad.contexts[slot].checksum = Detail::checksum(bad.contexts[slot]);
  assert(!Detail::valid(bad.contexts[slot], state.firmware_crc));

  Image last_sequence{};
  State last_state;
  initialize(last_sequence, last_state);
  last_state.sequence = UINT32_MAX - 1;
  assert(capturePanicBase(last_sequence, last_state, registers()));
  const auto last_token = get(last_sequence, last_state);
  assert(last_token.record_sequence == UINT32_MAX);
  assert(acknowledge(last_sequence, last_state, last_token.record_boot, last_token.record_sequence)
         == RISC_FAILURE_EVIDENCE_OK);
  assert(!captureRetention(last_sequence, last_state, -1, "exhausted"));

  Image last_boot = original;
  slot = Detail::latest(last_boot, state.firmware_crc);
  last_boot.contexts[slot].boot = UINT32_MAX - 1;
  last_boot.contexts[slot].checksum = Detail::checksum(last_boot.contexts[slot]);
  State terminal_boot;
  assert(boot(last_boot, terminal_boot, Reset::Software, 3, firmware));
  assert(terminal_boot.boot == UINT32_MAX);
  assert(capturePanicBase(last_boot, terminal_boot, registers()));
  // Base independently protects exhaustion if both context slots are lost.
  last_boot.contexts[0].magic = last_boot.contexts[1].magic = 0;
  State too_late;
  assert(!boot(last_boot, too_late, Reset::Panic, 8, firmware));
  assert(!too_late.ready && too_late.boot == 0);
}

int main() {
  failedPresentationPreservesPending();
  ordinaryAndRegisters();
  resetClassesAndFirmware();
  acknowledgeAndSynthetic();
  boundsAndRetention();
  semanticValidation();
  corruption();
  interruptedContext();
  interruptedBaseExtensionAndAck();
  interruptedRetentionAndReplacement();
  overflowAndHeaderRejection();
  std::puts("failure evidence store: PASS (CRC, resets, bounds, interrupted commits, exact ack)");
}
