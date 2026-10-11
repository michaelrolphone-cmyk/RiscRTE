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
  // Schema 3 downloads one immutable filesystem image; files remain the exact
  // required readback/admission inventory and have no individual URLs.
  File image{};
  size_t count=0;
  bool imageMode() const{return image.bytes!=0;}
  size_t downloads() const{return imageMode()?1:count;}
  const File& download(size_t index) const{return imageMode()?image:files[index];}
  Profile()=default;
  Profile(const Profile&)=delete;
  Profile& operator=(const Profile&)=delete;
  ~Profile(){clear();}
  void clear();
};
// Strict schema 1 (per-file URL), 2 (shared HTTPS base_url), or 3 (store image).
// All inventory files have relative paths, exact
// length and SHA256; boot.json/board.json/default.elf must all be present.
// On error no usable output or credential data remains in output.
bool parseProfile(const char*,size_t,Profile&);
}
