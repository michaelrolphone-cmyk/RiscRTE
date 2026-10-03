#pragma once
#include <cstdint>
#include <limits>

// Observed filesystem coherence only. A stamp is not a content digest, receipt,
// permission or proof against unobserved media changes. Owned by HalStorage and
// always manipulated under its existing non-recursive storage mutex.
struct StorageGenerationStamp {
  uint64_t mount = 0;
  uint64_t mutation = 0;
  bool quiescent = false;
  bool matches(const StorageGenerationStamp& other) const {
    return quiescent && other.quiescent && mount == other.mount && mutation == other.mutation;
  }
};
class StorageGenerationTracker {
 public:
  explicit StorageGenerationTracker(uint64_t initial = 0) : mount_(initial), mutation_(initial) {}
  void mutationAttempt() { advance(mutation_); }
  bool mountAttempt() {
    advance(mount_);
    mutationAttempt();
    return !poisoned_ && !handles_ && !external_ && !unsafeExternal_;
  }
  void mounted(bool success) {
    if (success) needsReconcile_ = false;
  }
  bool needsReconcile() const { return needsReconcile_; }
  bool opened(bool writable) {
    if (handles_ == UINT32_MAX || (writable && writers_ == UINT32_MAX)) {
      poisoned_ = true;
      return false;
    }
    ++handles_;
    if (writable) ++writers_;
    return true;
  }
  void closed(bool writable) {
    if (!handles_ || (writable && !writers_)) {
      poisoned_ = true;
      return;
    }
    --handles_;
    if (writable) --writers_;
  }
  void externalBegin() {
    needsReconcile_ = true;
    advance(mount_);
    mutationAttempt();
    if (external_ == UINT32_MAX)
      poisoned_ = true;
    else
      ++external_;
  }
  void externalEnd(bool closed) {
    advance(mount_);
    mutationAttempt();
    if (!closed) unsafeExternal_ = true;
    if (!external_)
      poisoned_ = true;
    else
      --external_;
  }
  void externalUncertain() {
    unsafeExternal_ = true;
    mutationAttempt();
  }
  StorageGenerationStamp stamp(bool ready) const {
    return {mount_, mutation_, ready && !poisoned_ && !writers_ && !external_ && !unsafeExternal_ && !needsReconcile_};
  }

 private:
  void advance(uint64_t& counter) {
    if (counter == std::numeric_limits<uint64_t>::max())
      poisoned_ = true;
    else
      ++counter;
  }
  uint64_t mount_ = 0, mutation_ = 0;
  uint32_t handles_ = 0, writers_ = 0, external_ = 0;
  bool poisoned_ = false, unsafeExternal_ = false, needsReconcile_ = false;
};
