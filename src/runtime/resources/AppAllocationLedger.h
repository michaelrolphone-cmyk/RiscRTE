#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace RuntimeResources {
// One invocation; synchronization and metadata storage belong to the host.
// Provider/firmware allocations never pass through this ledger.
class AppAllocationLedger {
 public:
  struct Entry { void* pointer; size_t bytes; uint32_t caps; };
  struct Backend {
    void* (*allocate)(size_t, uint32_t);
    void* (*resize)(void*, size_t, uint32_t);
    void (*release)(void*);
    void (*yield)();
  };
  void begin(Entry* entries, size_t capacity, Backend backend) {
    entries_ = entries; capacity_ = capacity; backend_ = backend;
    count_ = bytes_ = peak_ = 0;
    std::memset(entries_, 0, capacity_ * sizeof(Entry));
  }
  void* allocate(size_t bytes, uint32_t caps = 0) {
    if (!entries_ || !bytes || count_ == capacity_) return nullptr;
    void* pointer = backend_.allocate(bytes, caps);
    if (!pointer) return nullptr;
    for (size_t i=0; i<capacity_; ++i) if (!entries_[i].pointer) {
      entries_[i] = {pointer, bytes, caps}; ++count_; bytes_ += bytes;
      if (bytes_ > peak_) peak_ = bytes_;
      return pointer;
    }
    backend_.release(pointer);
    return nullptr;
  }
  void* calloc(size_t count, size_t size, uint32_t caps = 0) {
    if (size && count > std::numeric_limits<size_t>::max()/size) return nullptr;
    const size_t bytes=count*size;
    void* pointer=allocate(bytes,caps);
    if (pointer) std::memset(pointer,0,bytes);
    return pointer;
  }
  bool release(void* pointer) {
    if (!pointer) return true;
    if (!entries_) return false;
    for(size_t i=0;i<capacity_;++i) if(entries_[i].pointer==pointer) {
      bytes_-=entries_[i].bytes; --count_; entries_[i]={};
      backend_.release(pointer); return true;
    }
    return false;
  }
  void* resize(void* pointer, size_t bytes) {
    if (!pointer) return allocate(bytes);
    if (!bytes) { release(pointer); return nullptr; }
    if (!entries_) return nullptr;
    for(size_t i=0;i<capacity_;++i) if(entries_[i].pointer==pointer) {
      auto& entry=entries_[i];
      void* replacement=backend_.resize(pointer,bytes,entry.caps);
      if(!replacement) return nullptr; // Original ownership survives failure.
      bytes_=bytes_-entry.bytes+bytes;
      entry.pointer=replacement; entry.bytes=bytes;
      if(bytes_>peak_) peak_=bytes_;
      return replacement;
    }
    return nullptr;
  }
  void end() {
    if(!entries_) return;
    for(size_t i=0;i<capacity_;++i) {
      if(entries_[i].pointer) { backend_.release(entries_[i].pointer); entries_[i]={}; }
      if((i & 63u)==63u && backend_.yield) backend_.yield();
    }
    entries_=nullptr; capacity_=count_=bytes_=0;
  }
  size_t count() const { return count_; }
  size_t bytes() const { return bytes_; }
  size_t peak() const { return peak_; }
 private:
  Entry* entries_=nullptr;
  size_t capacity_=0,count_=0,bytes_=0,peak_=0;
  Backend backend_{};
};
}
