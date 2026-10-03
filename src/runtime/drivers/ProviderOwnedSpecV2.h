#pragma once

#include "ProviderGraphV2.h"
#include <cstdlib>
#include <cstring>
#include <new>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace RuntimeProviders {

// Graph-owned snapshot. Caller metadata, SD images, and mutable manifests
// never survive as references inside a provider node.
struct OwnedNodeV2 final {
  static constexpr size_t kImports = 128;
  static constexpr size_t kImportName = 128;
  struct ImportStorage {
    const char* pointers[kImports]{};
    char names[kImports][kImportName]{};
  };

  SpecV2 spec{};
  char id[96]{};
  char path[512]{};
  char provides[96]{};
  RequirementV2 requirements[16]{};
  char requirementNames[16][96]{};
  char requirementProviders[16][96]{};
  ImportStorage* imported = nullptr;
  uint8_t* image = nullptr;

  OwnedNodeV2() = default;
  OwnedNodeV2(const OwnedNodeV2&) = delete;
  OwnedNodeV2& operator=(const OwnedNodeV2&) = delete;
  ~OwnedNodeV2() {
    release(image);
    if (imported) imported->~ImportStorage();
    release(imported);
  }

  static void* allocate(size_t bytes) {
#ifdef ESP_PLATFORM
    void* memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory ? memory : heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
#else
    return std::malloc(bytes);
#endif
  }
  static void release(void* memory) {
#ifdef ESP_PLATFORM
    heap_caps_free(memory);
#else
    std::free(memory);
#endif
  }
  static bool copyString(char* dest, size_t capacity, const char* source) {
    if (!dest || !capacity || !source) return false;
    size_t length = 0;
    while (length < capacity && source[length]) ++length;
    if (!length || length == capacity) return false;
    std::memcpy(dest, source, length + 1);
    return true;
  }

  bool snapshot(const SpecV2& from) {
    if (!copyString(id, sizeof(id), from.id) ||
        !copyString(provides, sizeof(provides), from.provides) ||
        (from.verifiedElfPath &&
         !copyString(path, sizeof(path), from.verifiedElfPath)) ||
        from.requirementCount > 16 ||
        (from.requirementCount && !from.requirements)) return false;
    spec = from;
    spec.id = id;
    spec.provides = provides;
    spec.verifiedElfPath = from.verifiedElfPath ? path : nullptr;
    for (size_t i = 0; i < from.requirementCount; ++i) {
      if (!copyString(requirementNames[i], sizeof(requirementNames[i]),
                      from.requirements[i].capability)) return false;
      requirements[i] = {requirementNames[i], from.requirements[i].api};
      if (from.requirements[i].providerId) {
        if (!copyString(requirementProviders[i],96,from.requirements[i].providerId)) return false;
        requirements[i].providerId=requirementProviders[i];
      }
    }
    spec.requirements = from.requirementCount ? requirements : nullptr;

    if (!from.requiredOsCpuAbi) return true;
    if (!from.verifiedElfBytes || !from.verifiedElfLength ||
        from.verifiedElfLength > 8u * 1024u * 1024u ||
        !from.declaredImports || from.declaredImportCount > kImports) return false;
    void* storage = allocate(sizeof(ImportStorage));
    if (!storage) return false;
    imported = new (storage) ImportStorage();
    for (size_t i = 0; i < from.declaredImportCount; ++i) {
      if (!copyString(imported->names[i], sizeof(imported->names[i]),
                      from.declaredImports[i])) return false;
      imported->pointers[i] = imported->names[i];
      if (i && std::strcmp(imported->pointers[i - 1], imported->pointers[i]) >= 0)
        return false;
    }
    image = static_cast<uint8_t*>(allocate(from.verifiedElfLength));
    if (!image) return false;
    std::memcpy(image, from.verifiedElfBytes, from.verifiedElfLength);
    spec.verifiedElfBytes = image;
    // Even with zero names, the allocated pointer array is nonnull: private
    // loader can distinguish a verified empty list from missing declarations.
    spec.declaredImports = imported->pointers;
    return true;
  }
};

}  // namespace RuntimeProviders
