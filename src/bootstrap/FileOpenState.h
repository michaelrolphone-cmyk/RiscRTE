#pragma once
#include <T5FileOpenApi.h>

namespace RiscBoot {
// Session-owned copies only. No parser, source-app or loaded-ELF pointers.
struct FileOpenMetadata {
  static constexpr size_t MaxTypes=12, TypeBytes=16;
  t5_file_handler_t handler{};
  char types[MaxTypes][TypeBytes]{};
  uint8_t count=0;
};
struct FileOpenState {
  enum class Phase : uint8_t { Idle, Requested, Receiving, Result };
  char source[T5_FILE_OPEN_PATH_MAX]{};
  uint64_t cookie=0;
  int32_t result=0;
  int8_t caller=-1, receiver=-1;
  Phase phase=Phase::Idle;
};
}
