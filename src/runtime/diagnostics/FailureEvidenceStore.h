#pragma once
#include <RiscFailureEvidenceV1.h>
#include <stddef.h>
#include <stdint.h>

// RTC contains only an explicitly sized numeric/byte representation. State is
// native DRAM, initialized once by the healthy boot owner. The caller serializes
// healthy writers and prevents concurrent/reentrant panic entry.
namespace RiscFailureEvidence {
#if defined(__GNUC__)
#define RISC_FE_INLINE inline __attribute__((always_inline))
#else
#define RISC_FE_INLINE inline
#endif

enum class Reset : uint32_t {
  Unknown, PowerOn, Brownout, Software, DeepSleep, Panic,
  InterruptWatchdog, TaskWatchdog, Watchdog
};
struct Registers {
  uint32_t core, exception, pseudo_excause, pc, sp, a0, ps, exccause, excvaddr;
  uint32_t flags, reset_hint;
};
struct Context {
  uint32_t magic, format, firmware_crc, boot, sequence, phase, role;
  uint32_t invocation_low, invocation_high;
  char application[193];
  uint8_t reserved[7];
  uint32_t checksum;
};
struct Base {
  uint32_t magic, format, firmware_crc, boot, sequence, reset_hint, kind, flags;
  int32_t status;
  uint32_t core, exception, pseudo_excause, pc, sp, a0, ps, exccause, excvaddr;
  uint32_t phase, role, invocation_low, invocation_high;
  char application[193], detail[192];
  uint8_t firmware_sha256[32], reserved[35];
  uint32_t checksum;
};
struct Step { uint32_t sequence, phase, invocation_low, invocation_high; };
struct Extension {
  uint32_t magic, format, boot, sequence, frame_count, stack_status, history_count;
  risc_failure_frame_v1 frames[8];
  Step history[8];
  uint32_t checksum;
};
struct Acknowledgement {
  uint32_t magic, format, firmware_crc, boot, sequence, reserved[2], checksum;
};
struct Image {
  Context contexts[2];
  Base base;
  Extension extension;
  Acknowledgement acknowledgement;
};
static_assert(sizeof(Context) == 240, "RTC context layout");
static_assert(sizeof(Base) == 544, "RTC fault base layout");
static_assert(sizeof(Extension) == 224, "RTC extension layout");
static_assert(sizeof(Acknowledgement) == 32, "RTC acknowledgement layout");
static_assert(sizeof(Image) == 1280, "RTC evidence budget");
static_assert(offsetof(Context, checksum) == sizeof(Context) - 4, "context CRC tail");
static_assert(offsetof(Base, checksum) == sizeof(Base) - 4, "base CRC tail");
static_assert(offsetof(Extension, checksum) == sizeof(Extension) - 4, "extension CRC tail");
static_assert(offsetof(Acknowledgement, checksum) == sizeof(Acknowledgement) - 4, "ack CRC tail");

struct History { Step steps[8]; uint32_t count; };
struct State {
  bool initialized = false, ready = false, panic_started = false;
  bool retention_started = false, synthetic_present = false, synthetic_acked = false;
  uint32_t boot = 0, sequence = 0, current_reset_reason = 0, firmware_crc = 0;
  uint8_t firmware_sha256[32] = {};
  History histories[2] = {};
  volatile uint32_t history_slot = 0;
  Base synthetic = {};
};

namespace Detail {
// Each record independently identifies its schema and exact encoded byte size.
template<class T> RISC_FE_INLINE constexpr uint32_t format() {
  static_assert(sizeof(T) <= UINT16_MAX, "RTC record size encoding");
  return (1u << 16) | uint32_t(sizeof(T));
}
static constexpr uint32_t ContextMagic = 0x46454331u;
static constexpr uint32_t BaseMagic = 0x46454231u;
static constexpr uint32_t ExtensionMagic = 0x46455831u;
static constexpr uint32_t AckMagic = 0x46454131u;
RISC_FE_INLINE void barrier() {
#if defined(__XTENSA__)
  __asm__ __volatile__("memw" ::: "memory");
#elif defined(__GNUC__)
  __asm__ __volatile__("" ::: "memory");
#endif
}
// A test-only hook can decline a write and all later writes to simulate a reset
// at any commit boundary. Native builds have no hook or indirect call here.
RISC_FE_INLINE void put(volatile uint32_t& destination, uint32_t value) {
#ifdef RISC_FAILURE_EVIDENCE_WRITE_HOOK
  if (!RISC_FAILURE_EVIDENCE_WRITE_HOOK(&destination, value, 4u)) return;
#endif
  destination = value;
}
RISC_FE_INLINE void put(volatile int32_t& destination, int32_t value) {
#ifdef RISC_FAILURE_EVIDENCE_WRITE_HOOK
  if (!RISC_FAILURE_EVIDENCE_WRITE_HOOK(&destination, uint32_t(value), 4u)) return;
#endif
  destination = value;
}
RISC_FE_INLINE void putByte(volatile uint8_t& destination, uint8_t value) {
#ifdef RISC_FAILURE_EVIDENCE_WRITE_HOOK
  if (!RISC_FAILURE_EVIDENCE_WRITE_HOOK(&destination, value, 1u)) return;
#endif
  destination = value;
}
RISC_FE_INLINE void zero(void* destination, uint32_t size) {
  auto* bytes = static_cast<volatile uint8_t*>(destination);
  for (uint32_t i = 0; i < size; ++i) putByte(bytes[i], 0);
}
RISC_FE_INLINE void copy(void* destination, const void* source, uint32_t size) {
  auto* out = static_cast<volatile uint8_t*>(destination);
  const auto* in = static_cast<const volatile uint8_t*>(source);
  for (uint32_t i = 0; i < size; ++i) putByte(out[i], in[i]);
}
RISC_FE_INLINE void text(char* destination, const char* source, uint32_t capacity) {
  // Destinations have already been zeroed, including their final terminator.
  if (!source) return;
  const volatile char* in = source;
  for (uint32_t i = 0; i + 1 < capacity; ++i) {
    const char ch = in[i];
    if (!ch) break;
    putByte(reinterpret_cast<volatile uint8_t*>(destination)[i], uint8_t(ch));
  }
}
RISC_FE_INLINE uint32_t crc(const void* source, uint32_t size) {
  const auto* bytes = static_cast<const volatile uint8_t*>(source);
  uint32_t value = ~uint32_t(0);
  for (uint32_t i = 0; i < size; ++i) {
    value ^= bytes[i];
    for (uint32_t bit = 0; bit < 8; ++bit)
      value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
  }
  return ~value;
}
template<class T> RISC_FE_INLINE uint32_t checksum(const T& record) {
  return crc(reinterpret_cast<const uint8_t*>(&record) + 4, sizeof(T) - 8);
}
template<class T> RISC_FE_INLINE void begin(T& record) {
  put(record.magic, 0u);
  barrier();
  zero(reinterpret_cast<uint8_t*>(&record) + 4, sizeof(T) - 4);
}
template<class T> RISC_FE_INLINE void commit(T& record, uint32_t magic) {
  put(record.checksum, checksum(record));
  barrier();
  put(record.magic, magic);
  barrier();
}
RISC_FE_INLINE bool same(const void* left, const void* right, uint32_t size) {
  const auto* a = static_cast<const volatile uint8_t*>(left);
  const auto* b = static_cast<const volatile uint8_t*>(right);
  for (uint32_t i = 0; i < size; ++i) if (a[i] != b[i]) return false;
  return true;
}
RISC_FE_INLINE bool isZero(const void* source, uint32_t size) {
  const auto* bytes = static_cast<const volatile uint8_t*>(source);
  for (uint32_t i = 0; i < size; ++i) if (bytes[i]) return false;
  return true;
}
RISC_FE_INLINE bool phase(uint32_t value) {
  return value >= RISC_FAILURE_PHASE_LOAD && value <= RISC_FAILURE_PHASE_IDLE;
}
RISC_FE_INLINE bool valid(const Context& record, uint32_t firmware_crc) {
  return record.magic == ContextMagic && record.format == format<Context>() &&
    record.firmware_crc == firmware_crc && record.boot && record.sequence &&
    phase(record.phase) && record.role <= 2 && !record.application[192] &&
    isZero(record.reserved, sizeof(record.reserved)) &&
    record.checksum == checksum(record);
}
RISC_FE_INLINE bool valid(const Base& record, const State& state) {
  const uint32_t allowed = RISC_FAILURE_PENDING | RISC_FAILURE_REGISTERS |
    RISC_FAILURE_CONTEXT | RISC_FAILURE_ABORT;
  return record.magic == BaseMagic && record.format == format<Base>() &&
    record.firmware_crc == state.firmware_crc && record.boot && record.sequence &&
    (record.kind == RISC_FAILURE_NATIVE_PANIC || record.kind == RISC_FAILURE_RETENTION) &&
    !(record.flags & ~allowed) && (record.flags & RISC_FAILURE_PENDING) &&
    (!(record.flags & RISC_FAILURE_CONTEXT) || phase(record.phase)) &&
    record.role <= 2 && record.pseudo_excause <= 1 &&
    !record.application[192] && !record.detail[191] &&
    isZero(record.reserved, sizeof(record.reserved)) &&
    same(record.firmware_sha256, state.firmware_sha256, 32) &&
    record.checksum == checksum(record);
}
RISC_FE_INLINE bool valid(const Extension& record, const Base& base) {
  if (record.magic != ExtensionMagic || record.format != format<Extension>() ||
      record.boot != base.boot || record.sequence != base.sequence ||
      record.frame_count > 8 || record.history_count > 8 ||
      record.stack_status > RISC_FAILURE_STACK_UNSUPPORTED ||
      record.checksum != checksum(record)) return false;
  for (uint32_t i = 0; i < record.history_count; ++i)
    if (!record.history[i].sequence || !phase(record.history[i].phase)) return false;
  return true;
}
RISC_FE_INLINE bool valid(const Acknowledgement& record, const Base& base) {
  return record.magic == AckMagic && record.format == format<Acknowledgement>() &&
    record.firmware_crc == base.firmware_crc && record.boot == base.boot &&
    record.sequence == base.sequence && isZero(record.reserved, sizeof(record.reserved)) &&
    record.checksum == checksum(record);
}
RISC_FE_INLINE bool pendingPrior(const Image& image,const State& state) {
  return valid(image.base,state) && image.base.boot<state.boot &&
    !valid(image.acknowledgement,image.base);
}
RISC_FE_INLINE bool readRtc(const Image& image,const State& state) {
  return valid(image.base,state) &&
    (!state.synthetic_present || image.base.boot==state.boot);
}
RISC_FE_INLINE int latest(const Image& image, uint32_t firmware_crc) {
  const bool a = valid(image.contexts[0], firmware_crc);
  const bool b = valid(image.contexts[1], firmware_crc);
  if (!a) return b ? 1 : -1;
  if (!b) return 0;
  const Context& left = image.contexts[0];
  const Context& right = image.contexts[1];
  return right.boot > left.boot || (right.boot == left.boot &&
    right.sequence > left.sequence) ? 1 : 0;
}
RISC_FE_INLINE bool warm(Reset reset) {
  return reset == Reset::Software || reset == Reset::DeepSleep ||
    reset == Reset::Panic || reset == Reset::InterruptWatchdog ||
    reset == Reset::TaskWatchdog || reset == Reset::Watchdog;
}
RISC_FE_INLINE bool crash(Reset reset) {
  return reset == Reset::Brownout || reset == Reset::Panic || reset == Reset::InterruptWatchdog ||
    reset == Reset::TaskWatchdog || reset == Reset::Watchdog;
}
RISC_FE_INLINE void contextToBase(Base& base, const Context* context) {
  if (!context || (!context->application[0] && !context->invocation_low &&
                   !context->invocation_high)) return;
  put(base.flags, base.flags | RISC_FAILURE_CONTEXT);
  put(base.phase, context->phase);
  put(base.role, context->role);
  put(base.invocation_low, context->invocation_low);
  put(base.invocation_high, context->invocation_high);
  copy(base.application, context->application, sizeof(base.application));
}
RISC_FE_INLINE bool next(State& state, uint32_t& sequence) {
  if (state.sequence == UINT32_MAX) return false;
  sequence = ++state.sequence;
  return true;
}
RISC_FE_INLINE bool writeContext(Image& image, State& state, const char* application,
                                uint64_t invocation, uint32_t phase_value, uint32_t role) {
  uint32_t sequence;
  if (!phase(phase_value) || role > 2 || !next(state, sequence)) return false;
  const int previous = latest(image, state.firmware_crc);
  Context& context = image.contexts[previous == 0 ? 1 : 0];
  begin(context);
  put(context.format, format<Context>());
  put(context.firmware_crc, state.firmware_crc);
  put(context.boot, state.boot);
  put(context.sequence, sequence);
  put(context.phase, phase_value);
  put(context.role, role);
  put(context.invocation_low, uint32_t(invocation));
  put(context.invocation_high, uint32_t(invocation >> 32));
  text(context.application, application, sizeof(context.application));
  commit(context, ContextMagic);
  return valid(context, state.firmware_crc) && context.boot == state.boot &&
    context.sequence == sequence;
}
RISC_FE_INLINE void history(State& state, uint32_t phase_value, uint64_t invocation) {
  const uint32_t old_slot = state.history_slot & 1u, new_slot = old_slot ^ 1u;
  const History& old_history = state.histories[old_slot];
  History& new_history = state.histories[new_slot];
  const uint32_t count = old_history.count <= 8 ? old_history.count : 0;
  const uint32_t keep = count < 8 ? count : 7;
  const uint32_t start = count == 8 ? 1 : 0;
  for (uint32_t i = 0; i < keep; ++i)
    copy(&new_history.steps[i], &old_history.steps[start + i], sizeof(Step));
  Step& step = new_history.steps[keep];
  step.sequence = state.sequence;
  step.phase = phase_value;
  step.invocation_low = uint32_t(invocation);
  step.invocation_high = uint32_t(invocation >> 32);
  new_history.count = keep + 1;
  barrier();
  state.history_slot = new_slot;
  barrier();
}
RISC_FE_INLINE bool startBase(Image& image, State& state, uint32_t kind,
                             uint32_t reset_hint, uint32_t sequence) {
  const int slot = latest(image, state.firmware_crc);
  const Context* context = slot >= 0 && image.contexts[slot].boot == state.boot
    ? &image.contexts[slot] : nullptr;
  Base& base = image.base;
  begin(base);
  put(base.format, format<Base>());
  put(base.firmware_crc, state.firmware_crc);
  put(base.boot, state.boot);
  put(base.sequence, sequence);
  put(base.reset_hint, reset_hint);
  put(base.kind, kind);
  put(base.flags, uint32_t(RISC_FAILURE_PENDING));
  copy(base.firmware_sha256, state.firmware_sha256, 32);
  contextToBase(base, context);
  return true;
}
} // namespace Detail

// A boot never consumes evidence. Firmware and reset classification must be
// supplied while the system is healthy; no SDK lookup occurs during capture.
RISC_FE_INLINE bool boot(Image& image, State& state, Reset reset,
                         uint32_t raw_reset_reason, const uint8_t firmware_sha256[32]) {
  if (state.initialized) return state.ready;
  if (!firmware_sha256) return false;
  state.initialized = true;
  state.current_reset_reason = raw_reset_reason;
  Detail::copy(state.firmware_sha256, firmware_sha256, 32);
  state.firmware_crc = Detail::crc(state.firmware_sha256, 32);
  const bool accepted = Detail::warm(reset);
  // Contexts use a compact CRC field, but it is never firmware identity alone.
  // The full identity anchor is prestaged in the base area at every healthy
  // boot, even when there is no committed fault. A torn anchor rejects context.
  const bool identity_matches = Detail::same(image.base.firmware_sha256,state.firmware_sha256,32);
  const int slot = accepted && identity_matches ? Detail::latest(image, state.firmware_crc) : -1;
  const bool base_valid = accepted && Detail::valid(image.base, state);
  uint32_t previous_boot = slot >= 0 ? image.contexts[slot].boot : 0;
  if (base_valid && image.base.boot > previous_boot) previous_boot = image.base.boot;
  if (previous_boot == UINT32_MAX) return false;
  state.boot = previous_boot + 1;
  if (Detail::crash(reset) && (!base_valid || Detail::valid(image.acknowledgement,image.base))) {
    // Brownout is reportable, but never trusts or reuses the RTC image.
    Base& synthetic = state.synthetic;
    Detail::zero(&synthetic, sizeof(synthetic));
    synthetic.boot = state.boot;
    synthetic.sequence = 1;
    synthetic.kind = RISC_FAILURE_RESET_ONLY;
    synthetic.flags = RISC_FAILURE_PENDING;
    synthetic.reset_hint = raw_reset_reason;
    Detail::copy(synthetic.firmware_sha256, state.firmware_sha256, 32);
    Detail::contextToBase(synthetic, slot >= 0 ? &image.contexts[slot] : nullptr);
    state.synthetic_present = true;
  }
  if (!accepted || !identity_matches) Detail::zero(&image, sizeof(image));
  else if (!base_valid) {
    Detail::put(image.base.magic, 0u);
    Detail::put(image.extension.magic, 0u);
    Detail::put(image.acknowledgement.magic, 0u);
    Detail::barrier();
  }
  if (!base_valid) Detail::copy(image.base.firmware_sha256,state.firmware_sha256,32);
  state.ready = Detail::writeContext(image, state, nullptr, 0, RISC_FAILURE_PHASE_IDLE, 0);
  return state.ready;
}

RISC_FE_INLINE bool breadcrumb(Image& image, State& state, const char* application,
                               uint64_t invocation, uint32_t phase, uint32_t role) {
  if (!state.ready || state.panic_started || state.retention_started) return false;
  if (!Detail::writeContext(image, state, application, invocation, phase, role)) return false;
  Detail::history(state, phase, invocation);
  return true;
}

RISC_FE_INLINE bool capturePanicBase(Image& image, State& state, const Registers& registers) {
  if (!state.ready || state.panic_started ||
      registers.pseudo_excause > 1 ||
      (registers.flags & ~(uint32_t(RISC_FAILURE_ABORT) | uint32_t(RISC_FAILURE_REGISTERS))))
    return false;
  if (Detail::pendingPrior(image,state)) {
    state.panic_started=true;Detail::barrier();return false;
  }
  uint32_t sequence;
  if (!Detail::next(state, sequence)) return false;
  state.panic_started = true;
  Detail::barrier();
  Detail::startBase(image, state, RISC_FAILURE_NATIVE_PANIC, registers.reset_hint, sequence);
  Base& base = image.base;
  Detail::put(base.flags, base.flags | registers.flags);
  Detail::put(base.core, registers.core);
  Detail::put(base.exception, registers.exception);
  Detail::put(base.pseudo_excause, registers.pseudo_excause);
  if (registers.flags & RISC_FAILURE_REGISTERS) {
    Detail::put(base.pc, registers.pc);
    Detail::put(base.sp, registers.sp);
    Detail::put(base.a0, registers.a0);
    Detail::put(base.ps, registers.ps);
    Detail::put(base.exccause, registers.exccause);
    Detail::put(base.excvaddr, registers.excvaddr);
  }
  Detail::commit(base, Detail::BaseMagic);
  return Detail::valid(base, state) && base.boot == state.boot && base.sequence == sequence;
}

// An interrupted extension cannot invalidate the independently committed base.
// The first committed extension wins. An incomplete extension may be retried.
// Frames are already checked by the native wrapper; the store checks only bounds.
RISC_FE_INLINE bool captureExtension(Image& image, State& state,
                                     const risc_failure_frame_v1* frames,
                                     uint32_t frame_count, uint32_t stack_status) {
  if (!state.ready || frame_count > 8 || (frame_count && !frames) ||
      stack_status > RISC_FAILURE_STACK_UNSUPPORTED ||
      !Detail::valid(image.base, state) || image.base.boot != state.boot ||
      Detail::valid(image.extension, image.base)) return false;
  const uint32_t slot = state.history_slot & 1u;
  const History& history = state.histories[slot];
  if (history.count > 8) return false;
  Extension& extension = image.extension;
  Detail::begin(extension);
  Detail::put(extension.format, Detail::format<Extension>());
  Detail::put(extension.boot, image.base.boot);
  Detail::put(extension.sequence, image.base.sequence);
  Detail::put(extension.frame_count, frame_count);
  Detail::put(extension.stack_status, stack_status);
  Detail::put(extension.history_count, history.count);
  for (uint32_t i = 0; i < frame_count; ++i) {
    Detail::put(extension.frames[i].pc, frames[i].pc);
    Detail::put(extension.frames[i].sp, frames[i].sp);
  }
  for (uint32_t i = 0; i < history.count; ++i)
    Detail::copy(&extension.history[i], &history.steps[i], sizeof(Step));
  Detail::commit(extension, Detail::ExtensionMagic);
  return Detail::valid(extension, image.base);
}

RISC_FE_INLINE bool captureRetention(Image& image, State& state, int32_t status,
                                     const char* detail) {
  if (!state.ready || state.retention_started || state.panic_started) return false;
  // An unsafe first presentation must not overwrite the evidence it could not
  // acknowledge. Freeze this boot's capture attempts while preserving it.
  if (Detail::pendingPrior(image,state)) {
    state.retention_started=true;Detail::barrier();return false;
  }
  uint32_t sequence;
  if (!Detail::next(state, sequence)) return false;
  state.retention_started = true;
  Detail::barrier();
  Detail::startBase(image, state, RISC_FAILURE_RETENTION, state.current_reset_reason, sequence);
  Detail::put(image.base.status, status);
  Detail::text(image.base.detail, detail, sizeof(image.base.detail));
  Detail::commit(image.base, Detail::BaseMagic);
  if (!Detail::valid(image.base, state) || image.base.boot != state.boot ||
      image.base.sequence != sequence) return false;
  // History is optional and separately committed, even for a healthy retention.
  (void)captureExtension(image, state, nullptr, 0, RISC_FAILURE_STACK_UNAVAILABLE);
  return true;
}

RISC_FE_INLINE int32_t read(const Image& image, const State& state,
                           risc_failure_evidence_v1& out) {
  Detail::zero(&out, sizeof(out));
  out.api_version = RISC_FAILURE_EVIDENCE_API_V1;
  out.struct_size = sizeof(out);
  out.current_reset_reason = state.current_reset_reason;
  if (!state.ready) return RISC_FAILURE_EVIDENCE_NONE;
  const bool rtc = Detail::readRtc(image, state);
  if (!rtc && !state.synthetic_present) return RISC_FAILURE_EVIDENCE_NONE;
  const Base& base = rtc ? image.base : state.synthetic;
  out.record_boot = base.boot;
  out.record_sequence = base.sequence;
  out.captured_reset_hint = base.reset_hint;
  out.kind = base.kind;
  out.flags = base.flags;
  if (rtc ? Detail::valid(image.acknowledgement, base) : state.synthetic_acked)
    out.flags &= ~uint32_t(RISC_FAILURE_PENDING);
  out.status = base.status;
  out.core = base.core;
  out.exception = base.exception;
  out.pseudo_excause = base.pseudo_excause;
  out.pc = base.pc;
  out.sp = base.sp;
  out.a0 = base.a0;
  out.ps = base.ps;
  out.exccause = base.exccause;
  out.excvaddr = base.excvaddr;
  out.phase = base.phase;
  out.role = base.role;
  out.invocation = (uint64_t(base.invocation_high) << 32) | base.invocation_low;
  Detail::copy(out.application, base.application, sizeof(out.application));
  Detail::copy(out.detail, base.detail, sizeof(out.detail));
  Detail::copy(out.firmware_sha256, base.firmware_sha256, 32);
  if (rtc && Detail::valid(image.extension, base)) {
    const Extension& extension = image.extension;
    out.frame_count = extension.frame_count;
    out.stack_status = extension.stack_status;
    out.history_count = extension.history_count;
    for (uint32_t i = 0; i < out.frame_count; ++i) {
      out.frames[i].pc = extension.frames[i].pc;
      out.frames[i].sp = extension.frames[i].sp;
    }
    for (uint32_t i = 0; i < out.history_count; ++i) {
      out.history[i].sequence = extension.history[i].sequence;
      out.history[i].phase = extension.history[i].phase;
      out.history[i].invocation = (uint64_t(extension.history[i].invocation_high) << 32) |
        extension.history[i].invocation_low;
    }
  }
  return RISC_FAILURE_EVIDENCE_OK;
}

RISC_FE_INLINE int32_t acknowledge(Image& image, State& state,
                                  uint32_t record_boot, uint32_t record_sequence) {
  if (!state.ready) return RISC_FAILURE_EVIDENCE_NONE;
  const bool rtc = Detail::readRtc(image, state);
  if (!rtc && !state.synthetic_present) return RISC_FAILURE_EVIDENCE_NONE;
  const Base& base = rtc ? image.base : state.synthetic;
  if (record_boot != base.boot || record_sequence != base.sequence)
    return RISC_FAILURE_EVIDENCE_STALE;
  if (!rtc) {
    state.synthetic_acked = true;
    return RISC_FAILURE_EVIDENCE_OK;
  }
  if (Detail::valid(image.acknowledgement, base)) return RISC_FAILURE_EVIDENCE_OK;
  Acknowledgement& ack = image.acknowledgement;
  Detail::begin(ack);
  Detail::put(ack.format, Detail::format<Acknowledgement>());
  Detail::put(ack.firmware_crc, base.firmware_crc);
  Detail::put(ack.boot, record_boot);
  Detail::put(ack.sequence, record_sequence);
  Detail::commit(ack, Detail::AckMagic);
  return Detail::valid(ack, base) ? RISC_FAILURE_EVIDENCE_OK : RISC_FAILURE_EVIDENCE_INVALID;
}

#undef RISC_FE_INLINE
} // namespace RiscFailureEvidence
