#pragma once
#include "RuntimeFileImage.h"
#include "runtime/storage/VolumeImageFiles.h"
#include <new>
namespace RiscBoot {
// Existing volume selection is adapted by a concrete provider. Image reading
// uses only the same generic file session and direct destination as internal ELF.
struct RemovableAppImage {
  static constexpr size_t MaxBytes=2u*1024u*1024u;
  using Hooks=RuntimeFileImage::Hooks;
  struct Session {
    RiscStorage::VolumeImageFiles provider;RuntimeFiles files;
    RuntimeFileSources::Entry entry{"/",&files};RuntimeFileSources sources{&entry,1};
    explicit Session(const risc_storage_volume_api_v1* v,Hooks h):provider(v,h.safe,h.context){}
  };
  uint8_t* bytes=nullptr;size_t size=0;bool closeRetained=false;
  Session* retainedSession=nullptr;
  const char* error="image source unavailable";
  bool read(const void* table,bool directFiles,const char* path,Hooks h) {
    auto* session=new(std::nothrow) Session(directFiles?nullptr:static_cast<const risc_storage_volume_api_v1*>(table),h);
    if(!session){error="image source allocation failed";return false;}
    if(!session->files.bind(directFiles?static_cast<const risc_storage_files_v1*>(table):session->provider.api(),[](){return true;},[](){return true;})){delete session;return false;}
    RuntimeFileImage image;
    const bool ok=image.read(RuntimeFileView(&session->sources,"/"),path,h,0,MaxBytes);
    bytes=image.bytes;size=image.size;closeRetained=image.retained;
    // Terminal sessions are deliberately never dispatched again: their safety
    // callback may borrow the completed read invocation's stack context.
    if(closeRetained){retainedSession=session;error="image source custody retained; restart required";return false;}
    delete session;
    if(!ok)error=image.error==ENOMEM?"image snapshot allocation failed":"image source read failed";
    return ok;
  }
};
}
