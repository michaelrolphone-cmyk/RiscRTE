#pragma once
// Host-only type boundary: glibc Runtime file sources and newlib VFS DIR are
// intentionally different types. All operations forward to the real mount.
namespace NativeLocalMount {
enum class Access {AppData};
bool prepare(const void*partition);bool finish();bool retained();bool held();
bool enterAppData();void leave();
struct Guard {
 bool held;explicit Guard(Access):held(enterAppData()){}
 ~Guard(){if(held)leave();}
 Guard(const Guard&)=delete;Guard&operator=(const Guard&)=delete;
};
}
