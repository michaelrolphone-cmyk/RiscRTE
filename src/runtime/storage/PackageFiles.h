#pragma once
#include <RiscStorageVolumeFsV1.h>
#include <cstdint>
#include <cstddef>
namespace RiscStorage {
/* Filesystem adapter beneath the copied native storage worker. Scope is an
 * already mounted private root. Hooks serialize native mount custody, including
 * every open handle. No mount, format, arbitrary roots, task or app authority. */
class PackageFiles final {
 public:
 struct Hooks {void*context;bool(*enter)(void*);void(*leave)(void*);bool(*retained)(void*);bool(*usage)(void*,uint64_t*,uint64_t*);};
 explicit PackageFiles(Hooks hooks):hooks_(hooks){}
 bool configure(const char*root);
 const risc_storage_volume_api_v1*api(){return &api_.state.prepared.base.sleep.terminal.power.volume.base;}
 bool exitSafe()const{return !retained_&&!file_&&!directory_;}
 bool retained()const{return retained_||hooks_.retained(hooks_.context);}
 bool freeBytes(uint64_t*);
 private:
 struct Call {PackageFiles&s;bool ok;explicit Call(PackageFiles&);~Call();};
 bool path(const char*,char*,size_t);bool roots();bool mutation(const char*);
 int32_t metadata(const char*,risc_storage_metadata_v1*);
 uint32_t open(const char*,bool,uint64_t*);bool close(uint32_t,bool);
 bool fault(bool);uint32_t next();
 Hooks hooks_;risc_storage_volume_api_v1_fs api_{};
 char root_[256]{},writePath_[512]{},directoryPath_[512]{};
 int descriptor_=-1;void*directory_=nullptr;
 uint32_t file_=0,dirToken_=0,serial_=0,depth_=0,error_=0,dirError_=0;
 bool configured_=false,writing_=false,retained_=false,custody_=false;
};
}
