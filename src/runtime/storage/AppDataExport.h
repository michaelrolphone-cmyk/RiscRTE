#pragma once
#include <RiscAppDataExportV1.h>
#include "bootstrap/AppDataBackend.h"
#include <cstddef>
#include <cstdint>
namespace RiscStorage {
class AppDataExport final {
public:
 static constexpr size_t EntryMax=16,PathMax=191,SlotMax=4;
 struct Entry {char owner[96]{};uint32_t nameSpace=0;char name[49]{};char path[PathMax+1]{};bool writable=false;};
 struct Hooks {void* context;bool (*owner)(void*);bool (*safe)(void*);void* (*allocate)(size_t);void (*deallocate)(void*);};
 explicit AppDataExport(Hooks h):hooks_(h){}
 ~AppDataExport();
 AppDataExport(const AppDataExport&)=delete;
 AppDataExport& operator=(const AppDataExport&)=delete;
 bool configure(const RiscBoot::AppDataBackend*,const Entry*,size_t,const char* label);
 bool begin(risc_app_data_export_v1*);
 bool end();
 bool retained()const{return retained_;}
 bool exitSafe()const{return !retained_&&!busy_;}
 bool active()const{return context_!=0;}
 static bool validEntries(const Entry*,size_t);
private:
 Hooks hooks_;RiscBoot::AppDataBackend backend_{};
 Entry entries_[EntryMax]{};size_t count_=0;char label_[64]{},error_[96]{};
 uintptr_t context_=0;bool configured_=false,busy_=false,retained_=false;
 uint32_t read_=0,write_=0,dir_=0;unsigned char* readBytes_=nullptr,*writeBytes_=nullptr,*ioBytes_=nullptr;
 uint32_t readSize_=0,readAt_=0,writeSize_=0;int writeEntry_=-1;int32_t writeError_=0,dirError_=0;
 int32_t admissionError_=RISC_APP_DATA_CONTEXT;
 bool writeAttempted_=false;char dirPath_[PathMax+1]{};size_t dirAt_=0;
 static AppDataExport* slots_[SlotMax];static uintptr_t nextContext_;static uint32_t nextHandle_;
 static AppDataExport* resolve(void*);
 static bool validPath(const char*,bool root=true);
 const Entry* find(const char*)const;
 bool directory(const char*)const;
 bool enter();bool leave(int32_t);
 int32_t status(int32_t);
 int32_t statRevision(const char*,uint32_t*,uint64_t*);
 int32_t readRevision(const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*);
 int32_t replaceRevision(const char*,uint64_t,const void*,uint32_t);
 bool stat(const char*,uint64_t*,bool*);
 uint32_t dirOpen(const char*);bool dirNext(uint32_t,risc_storage_dirent_v1*);bool dirClose(uint32_t);
 uint32_t openRead(const char*,uint64_t*);size_t read(uint32_t,void*,size_t);
 uint32_t openWrite(const char*);size_t write(uint32_t,const void*,size_t);bool close(uint32_t,bool);
 void dropRead();void dropWrite();
};
}
#include "AppDataExportImpl.h"
