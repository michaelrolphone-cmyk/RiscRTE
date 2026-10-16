#pragma once

#include "bootstrap/Runtime.h"
#include "runtime/storage/PosixFiles.h"
#include <cassert>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace RiscTest {
// Test assembly only: every metadata root is explicit and copied before any
// Runtime is bound. Keep this owner alive until all Runtime users are destroyed
// (or until the fixture process exits when Runtime intentionally retains state).
class FileSources final {
 public:
  using Check = RiscBoot::RuntimeFiles::Check;
  struct Mapping { std::string logical; std::string backend; };

  explicit FileSources(std::initializer_list<std::string> roots,
                       Check owner = allowed, Check safe = allowed) {
    for (const auto& root : roots) add(root, root, owner, safe);
    seal();
  }
  explicit FileSources(std::initializer_list<Mapping> roots,
                       Check owner = allowed, Check safe = allowed) {
    for (const auto& root : roots) add(root.logical, root.backend, owner, safe);
    seal();
  }
  FileSources(const FileSources&) = delete;
  FileSources& operator=(const FileSources&) = delete;
  FileSources(FileSources&&) = delete;
  FileSources& operator=(FileSources&&) = delete;

  const RiscBoot::RuntimeFileSources* sources() const { return sources_.get(); }
  bool bind(RiscBoot::Runtime& runtime) const {
    return runtime.bindFileSources(sources());
  }

 private:
  static bool allowed() { return true; }
  struct Source {
    const std::string logical;
    const std::string backend;
    RiscStorage::PosixFiles provider;
    RiscBoot::RuntimeFiles files;
    Source(const std::string& name, const std::string& path, Check owner, Check safe)
        : logical(name), backend(path), provider(backend.c_str()) {
      const bool bound = files.bind(provider.api(), owner, safe,
                                    provider.identityApi(), provider.directoryApi());
      assert(bound);
      (void)bound;
    }
  };
  void add(const std::string& root, const std::string& backend, Check owner, Check safe) {
    owned_.emplace_back(new Source(root, backend, owner, safe));
    auto& source = *owned_.back();
    entries_.push_back({source.logical.c_str(), &source.files});
  }
  void seal() {
    sources_.reset(new RiscBoot::RuntimeFileSources(entries_.data(), entries_.size()));
  }
  std::vector<std::unique_ptr<Source>> owned_;
  std::vector<RiscBoot::RuntimeFileSources::Entry> entries_;
  std::unique_ptr<const RiscBoot::RuntimeFileSources> sources_;
};
} // namespace RiscTest
