#pragma once
#include "Profile.h"
#include <cstdio>
namespace RiscProvision {
// Compiled-in inactive-volume file backend. The mount owner must establish that
// root is an independently staged store, never the installed/active filesystem.
struct FileBackend {
 void* context;
 uint32_t (*now)(void*);
 bool (*checkpoint)(void*); // owner/resource safety + cooperative yield
 bool (*hashBegin)(void*);
 bool (*hashAdd)(void*,const void*,uint32_t);
 bool (*hashEnd)(void*,uint8_t*);
 // Real whole graph + every referenced ELF/import, with no provider execution.
 bool (*admit)(void*,const char*,const Profile&);
};
class StoreFiles final {
 public:
 explicit StoreFiles(FileBackend b):io_(b){}
 StoreFiles(const StoreFiles&)=delete;
 StoreFiles& operator=(const StoreFiles&)=delete;
 // All operations are owner-serialized. Capacity is the native backend's
 // conservative usable payload bound, not raw partition size.
 bool begin(const char* root,const Profile&,const uint8_t (&digest)[32],uint32_t capacity);
 bool write(size_t file,const void*,uint32_t); // sequential, <=4096 copied bytes
 bool finish(); // full readback, exact inventory, admission, digest metadata
 bool close();  // failed close is permanently retained, never double-closed
 bool retained() const{return retained_;}
 size_t fileIndex() const{return index_;}
 static constexpr const char* DigestFile=".provision-sha256";
 private:
 bool checkpoint();
 bool filename(const char*,char (&out)[256]) const;
 bool inventory(const char*,unsigned,bool erase,size_t& count);
 bool verify(const File&);
 FileBackend io_;const Profile* profile_=nullptr;
 char root_[256]{};uint8_t digest_[32]{},buffer_[4096]{};
 uint32_t started_=0,received_=0;size_t index_=0;
 FILE* stream_=nullptr;bool begun_=false,failed_=false,finished_=false,retained_=false;
};
}
