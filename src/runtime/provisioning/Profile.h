#pragma once
#include <cstddef>
#include <cstdint>
namespace RiscProvision {
constexpr size_t MaxFiles=128, ChunkBytes=4096;
constexpr uint32_t MaxFileBytes=8*1024*1024, MaxStoreBytes=16*1024*1024;
struct File { char path[193]{}, url[385]{}; uint32_t bytes=0; uint8_t sha256[32]{}; };
// Trusted owner-supplied profile, never an app grant or a remote authority.
// Allocate this bounded object outside the small native task stack. The caller
// owns and must clear the input JSON buffer, which may also contain credentials.
struct Profile {
  char ssid[33]{}, password[64]{};
  File files[MaxFiles]{};
  size_t count=0;
  Profile()=default;
  Profile(const Profile&)=delete;
  Profile& operator=(const Profile&)=delete;
  ~Profile(){clear();}
  void clear();
};
// Strict schema 1 (per-file URL) / 2 (shared HTTPS base_url). All files have relative paths, exact
// length and SHA256; boot.json/board.json/default.elf must all be present.
// On error no usable output or credential data remains in output.
bool parseProfile(const char*,size_t,Profile&);
}
