#pragma once
#include "native_settings_catalog_fixture.h"
#include "experimental/native_replica/CheckedFirstSdCheckpoint.h"
#include "experimental/native_replica/NativeBaselineAdmission.h"
using F=RiscExperimental::CheckedFirstSdCheckpoint;
struct FirstSd:DirectorySd {
 C*core=nullptr;unsigned randoms=0;bool quiet=true,entropyOk=true,space=true,retainOnQuiet=false;
 uint32_t(*oldOpenWrite)(void*,const char*)=nullptr;
 explicit FirstSd(Files&files,C*core):DirectorySd(files),core(core){
  auto&ext=download.base.base.fs.state.prepared.base.sleep.terminal.power.volume;
  ext.mkdir=[](void*p,const char*path){auto&s=*static_cast<FirstSd*>(p);s.allowed(path);assert(s.core->exportPrepared());bool fail=s.point("mkdir");if(fail&&!s.after){s.finish(true);return false;}s.dirs.insert(path);++s.mutations;return !s.finish(fail);};
  oldOpenWrite=ext.base.file_open_write;ext.base.file_open_write=[](void*p,const char*path)->uint32_t{auto&s=*static_cast<FirstSd*>(p);assert(s.core->exportPrepared());C::ExportMarker m;assert(s.core->preparedExport(&m)==0&&m.firstCreation());return s.oldOpenWrite(p,path);};
 }
 F::Authority authority(){return {this,[](void*p){auto&s=*static_cast<FirstSd*>(p);if(s.retainOnQuiet)s.retained=true;return s.quiet;},[](void*p,uint8_t out[16]){auto&s=*static_cast<FirstSd*>(p);memset(out,0,16);out[0]=uint8_t(++s.randoms+64);return s.entropyOk;},[](void*p,uint32_t,uint32_t){return static_cast<FirstSd*>(p)->space;}};}
 void runAll(F&f){run(f);if(f.state()==F::State::Admitted){assert(f.prepareNative()==C::Ok);run(f);}}
};
