#include "runtime/storage/ScopedUserVolume.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using RiscStorage::ScopedUserVolume;

namespace {
constexpr const char* Root = "/user";
constexpr const char* Stage = "~R";

struct Backend {
  struct Node { bool directory; std::string data; };
  struct File { std::string path; size_t offset; bool writer; uint32_t error; };
  struct Directory { std::vector<risc_storage_dirent_v1> entries; size_t offset; uint32_t error; };
  enum class Fault {
    None, ShortWrite, WriteError, ReadError, ShortRead, Sync, DirError,
    CloseBefore, CloseAfter, AbortBefore, AbortAfter,
    DirCloseBefore, DirCloseAfter, RenameBefore, RenameAfter, MkdirBefore, MkdirAfter,
    OpenReadUnavailable, OpenWriteUnavailable
  };
  struct Call { std::string operation, path, destination; uint32_t handle; bool commit; size_t bytes; };
  bool owner = true, safe = true, media = true;
  std::string revokeAfter;
  std::function<void()> reenter;
  Fault fault = Fault::None, thenFault = Fault::None;
  uint32_t nextHandle = 100;
  std::map<std::string, Node> nodes;
  std::map<uint32_t, File> files;
  std::map<uint32_t, Directory> directories;
  std::vector<Call> calls;
  std::vector<risc_storage_dirent_v1> injected;
  risc_storage_volume_api_v1_ext api{};

  Backend() {
    nodes.emplace("/user", Node{true, {}});
    nodes.emplace("/user/docs", Node{true, {}});
    nodes.emplace("/user/docs/nested", Node{true, {}});
    nodes.emplace("/user/hello.txt", Node{false, "hello"});
    nodes.emplace("/user/docs/book.txt", Node{false, "chapter one"});
    nodes.emplace("/apps", Node{true, {}});
    nodes.emplace("/apps/installed.elf", Node{false, "installed private image"});
    nodes.emplace("/private", Node{true, {}});
    nodes.emplace("/private/app-data", Node{false, "private app data"});
    nodes.emplace("/user-other", Node{true, {}});
    nodes.emplace("/user-other/secret", Node{false, "sibling"});
    api.base = {1, sizeof(api), this, refresh, ready, label, stat, dirOpen,
      dirNext, dirCloseUnchecked, fileOpenRead, fileRead, fileOpenWrite,
      fileWrite, fileClose, remove, lastError};
    api.file_sync = sync;
    api.file_info = fileInfo;
    api.dir_close_checked = dirClose;
    api.handle_error = handleError;
    api.rename = rename;
    api.mkdir = mkdir;
  }
  static Backend& get(void* context) {
    assert(context);
    auto& b = *static_cast<Backend*>(context);
    assert(b.owner && b.safe); // No callback may cross the adapter's fences.
    return b;
  }
  static bool isOwner(void* c) { return static_cast<Backend*>(c)->owner; }
  static bool isSafe(void* c) { return static_cast<Backend*>(c)->safe; }
  ScopedUserVolume::Hooks hooks() { return {this, isOwner, isSafe}; }
  void call(const char* op, const char* path = nullptr, uint32_t handle = 0,
            bool commit = false, size_t bytes = 0, const char* destination = nullptr) {
    if (path) assert(confined(path));
    if (destination) assert(confined(destination));
    calls.push_back({op, path ? path : "", destination ? destination : "", handle, commit, bytes});
    if (reenter) reenter();
    if (revokeAfter == op) safe = false;
  }
  static bool confined(const std::string& path) {
    return path == Root || path.compare(0, std::strlen(Root) + 1, std::string(Root) + "/") == 0;
  }
  static std::string parent(const std::string& path) { return path.substr(0, path.find_last_of('/')); }
  static std::string leaf(const std::string& path) { return path.substr(path.find_last_of('/') + 1); }
  bool hit(Fault wanted) { if (fault != wanted) return false; fault = thenFault; thenFault = Fault::None; return true; }
  size_t count(const std::string& operation) const {
    return static_cast<size_t>(std::count_if(calls.begin(), calls.end(),
      [&](const Call& c) { return c.operation == operation; }));
  }
  std::vector<std::string> stages() const {
    std::vector<std::string> result;
    for (const auto& pair : nodes) if (leaf(pair.first).compare(0, std::strlen(Stage), Stage) == 0) result.push_back(pair.first);
    return result;
  }
  static risc_storage_dirent_v1 entry(const std::string& name, bool dir = false, uint64_t size = 0) {
    risc_storage_dirent_v1 e{};
    assert(name.size() < sizeof(e.name));
    std::memcpy(e.name, name.c_str(), name.size() + 1);
    e.size = size; e.is_directory = dir;
    return e;
  }
  static bool refresh(void* c) { auto& b=get(c); b.call("refresh"); return true; }
  static bool ready(void* c) { auto& b=get(c); b.call("ready"); return b.media; }
  static bool label(void* c, char* out, size_t n) {
    auto& b=get(c); b.call("label");
    if (!out || n < 7) return false;
    std::memcpy(out,"DEVICE",7); return true;
  }
  static bool stat(void* c, const char* p, uint64_t* size, bool* dir) {
    auto& b=get(c); b.call("stat",p); auto it=b.nodes.find(p);
    if (it == b.nodes.end()) return false;
    if (size) *size=it->second.data.size();
    if (dir) *dir=it->second.directory;
    return true;
  }
  static uint32_t dirOpen(void* c, const char* p) {
    auto& b=get(c); b.call("dir_open",p); auto it=b.nodes.find(p);
    if (it == b.nodes.end() || !it->second.directory) return 0;
    Directory d{{},0,0};
    d.entries=b.injected;
    for (const auto& pair : b.nodes) if (parent(pair.first)==p)
      d.entries.push_back(entry(leaf(pair.first),pair.second.directory,pair.second.data.size()));
    const uint32_t h=++b.nextHandle; b.directories.emplace(h,std::move(d)); return h;
  }
  static bool dirNext(void* c, uint32_t h, risc_storage_dirent_v1* out) {
    auto& b=get(c); b.call("dir_next",nullptr,h); auto it=b.directories.find(h);
    assert(it!=b.directories.end()); auto& d=it->second;
    if (b.hit(Fault::DirError)) {d.error=71; return false;}
    if (!out || d.error || d.offset==d.entries.size()) return false;
    *out=d.entries[d.offset++]; return true;
  }
  static void dirCloseUnchecked(void*, uint32_t) { assert(false && "must use checked directory close"); }
  static bool dirClose(void* c, uint32_t h) {
    auto& b=get(c); b.call("dir_close",nullptr,h); assert(b.directories.count(h));
    if (b.hit(Fault::DirCloseBefore)) return false;
    b.directories.erase(h);
    return !b.hit(Fault::DirCloseAfter);
  }
  static uint32_t fileOpenRead(void* c, const char* p, uint64_t* size) {
    auto& b=get(c); b.call("open_read",p); auto it=b.nodes.find(p);
    if(b.hit(Fault::OpenReadUnavailable))return 0;
    if (it==b.nodes.end() || it->second.directory) return 0;
    if (size) *size=it->second.data.size();
    const uint32_t h=++b.nextHandle; b.files.emplace(h,File{p,0,false,0}); return h;
  }
  static size_t fileRead(void* c, uint32_t h, void* out, size_t n) {
    auto& b=get(c); b.call("read",nullptr,h,false,n);
    assert(n<=512 && out && n); auto it=b.files.find(h); assert(it!=b.files.end());
    auto& f=it->second; assert(!f.writer);
    if (b.hit(Fault::ReadError)) {f.error=72; return 0;}
    if (f.error) return 0;
    auto& data=b.nodes.at(f.path).data; size_t count=std::min(n,data.size()-f.offset);
    if (b.hit(Fault::ShortRead) && count) --count;
    std::memcpy(out,data.data()+f.offset,count); f.offset+=count; return count;
  }
  static uint32_t fileOpenWrite(void* c, const char* p) {
    auto& b=get(c); b.call("open_write",p);
    if(b.hit(Fault::OpenWriteUnavailable))return 0;
    if (b.nodes.count(p)) return 0;
    auto dir=b.nodes.find(parent(p)); if (dir==b.nodes.end() || !dir->second.directory) return 0;
    b.nodes.emplace(p,Node{false,{}});
    const uint32_t h=++b.nextHandle; b.files.emplace(h,File{p,0,true,0}); return h;
  }
  static size_t fileWrite(void* c, uint32_t h, const void* bytes, size_t n) {
    auto& b=get(c); b.call("write",nullptr,h,false,n);
    assert(n<=512 && bytes && n); auto it=b.files.find(h); assert(it!=b.files.end());
    auto& f=it->second; assert(f.writer);
    if (b.hit(Fault::WriteError)) {f.error=73; return 0;}
    if (f.error) return 0;
    const size_t count=b.hit(Fault::ShortWrite) ? n-1 : n;
    b.nodes.at(f.path).data.append(static_cast<const char*>(bytes),count); f.offset+=count; return count;
  }
  static bool fileClose(void* c, uint32_t h, bool commit) {
    auto& b=get(c); b.call("file_close",nullptr,h,commit); auto it=b.files.find(h); assert(it!=b.files.end());
    const bool abort=it->second.writer && !commit;
    if (b.hit(abort ? Fault::AbortBefore : Fault::CloseBefore)) return false;
    if (abort) b.nodes.erase(it->second.path);
    b.files.erase(it);
    return !b.hit(abort ? Fault::AbortAfter : Fault::CloseAfter);
  }
  static bool remove(void* c, const char* p) {
    auto& b=get(c); b.call("remove",p);
    auto it=b.nodes.find(p); if (it==b.nodes.end()) return false;
    for (const auto& file:b.files) if (file.second.path==p) return false;
    for (const auto& node:b.nodes) if (parent(node.first)==p) return false;
    b.nodes.erase(it); return true;
  }
  static bool lastError(void* c, char* out, size_t n) {
    auto& b=get(c); b.call("last_error"); if(!out || !n)return false; out[0]=0;return true;
  }
  static bool fileInfo(void* c, uint32_t h, uint64_t* size, uint64_t* position) {
    auto& b=get(c);b.call("file_info",nullptr,h);auto it=b.files.find(h);assert(it!=b.files.end());
    if(size)*size=b.nodes.at(it->second.path).data.size();
    if(position)*position=it->second.offset;
    return true;
  }
  static bool sync(void* c, uint32_t h) {
    auto& b=get(c); b.call("sync",nullptr,h); auto it=b.files.find(h);assert(it!=b.files.end());
    if (b.hit(Fault::Sync)) {it->second.error=74; return false;}
    return it->second.error==0;
  }
  static uint32_t handleError(void* c, uint32_t h, bool directory) {
    auto& b=get(c); b.call(directory?"dir_error":"file_error",nullptr,h);
    if (directory) {auto it=b.directories.find(h); assert(it!=b.directories.end()); return it->second.error;}
    auto it=b.files.find(h); assert(it!=b.files.end()); return it->second.error;
  }
  static bool rename(void* c, const char* source, const char* destination) {
    auto& b=get(c); b.call("rename",source,0,false,0,destination);
    if(leaf(source).compare(0,2,Stage)==0)assert(parent(source)==parent(destination));
    if (b.hit(Fault::RenameBefore)) return false;
    if (b.nodes.count(destination) || !b.nodes.count(source)) return false;
    auto dir=b.nodes.find(parent(destination));if(dir==b.nodes.end() || !dir->second.directory)return false;
    for(const auto& f:b.files) assert(f.second.path!=source); // Commit closes first.
    std::vector<std::pair<std::string,Node>> moved;
    for(auto it=b.nodes.begin();it!=b.nodes.end();){
      if(it->first.compare(0,std::strlen(source)+1,std::string(source)+"/")==0){
        moved.emplace_back(std::string(destination)+it->first.substr(std::strlen(source)),it->second);
        it=b.nodes.erase(it);
      }else ++it;
    }
    auto node=b.nodes.extract(source);node.key()=destination;b.nodes.insert(std::move(node));
    for(auto& item:moved)b.nodes.emplace(std::move(item));
    return !b.hit(Fault::RenameAfter);
  }
  static bool mkdir(void* c,const char* path){
    auto& b=get(c);b.call("mkdir",path);
    if(b.hit(Fault::MkdirBefore) || b.nodes.count(path))return false;
    auto parentNode=b.nodes.find(parent(path));
    if(parentNode==b.nodes.end() || !parentNode->second.directory)return false;
    b.nodes.emplace(path,Node{true,{}});
    return !b.hit(Fault::MkdirAfter);
  }
};

struct Fixture {
  Backend backend;
  ScopedUserVolume volume;
  risc_storage_volume_api_v1 api{};
  Fixture():volume(backend.hooks()) {
    assert(volume.configure(&backend.api.base,Root,"User files"));
    assert(volume.begin(&api));
    assert(api.api_version==1 && api.struct_size==sizeof(api) && api.context);
  }
  void endClean() {
    assert(volume.end());
    assert(!volume.retained() && volume.exitSafe());
    assert(backend.files.empty() && backend.directories.empty());
  }
};

struct ExtendedFixture:Fixture {
  risc_storage_volume_api_v1_ext ext{};
  ExtendedFixture(){assert(volume.end());assert(volume.beginExtended(&ext));api=ext.base;}
};

std::set<std::string> list(risc_storage_volume_api_v1& api,const char* path="/") {
  const auto dir=api.dir_open(api.context,path);assert(dir);
  std::set<std::string> names;risc_storage_dirent_v1 entry{};
  size_t limit=128;
  while(api.dir_next(api.context,dir,&entry)) {assert(limit--); names.insert(entry.name);}
  api.dir_close(api.context,dir);return names;
}
void writeAll(risc_storage_volume_api_v1& api,uint32_t file,const std::string& data) {
  size_t offset=0;
  while(offset<data.size()) {
    const size_t n=api.file_write(api.context,file,data.data()+offset,data.size()-offset);
    assert(n && n<=512);offset+=n;
  }
}
void assertNoProviderAfterRetention(Fixture& f) {
  assert(f.volume.retained() && !f.volume.exitSafe());
  const size_t count=f.backend.calls.size();
  char text[1024]{}; uint64_t size=1;bool dir=true;risc_storage_dirent_v1 entry{};
  assert(!f.api.refresh(f.api.context));assert(!f.api.ready(f.api.context));
  assert(!f.api.label(f.api.context,text,sizeof(text)));
  assert(!f.api.stat(f.api.context,"/hello.txt",&size,&dir));
  assert(!f.api.dir_open(f.api.context,"/"));
  assert(!f.api.dir_next(f.api.context,1,&entry));f.api.dir_close(f.api.context,1);
  assert(!f.api.file_open_read(f.api.context,"/hello.txt",&size));
  assert(!f.api.file_read(f.api.context,1,text,sizeof(text)));
  assert(!f.api.file_open_write(f.api.context,"/later"));
  assert(!f.api.file_write(f.api.context,1,text,sizeof(text)));
  assert(!f.api.file_close(f.api.context,1,false));assert(!f.api.remove(f.api.context,"/hello.txt"));
  (void)f.api.last_error(f.api.context,text,sizeof(text));
  assert(!f.volume.end());
  risc_storage_volume_api_v1 again{};assert(!f.volume.begin(&again));
  assert(!f.volume.configure(&f.backend.api.base,Root,"retry"));
  assert(f.backend.calls.size()==count);
}

void ordinary() {
  Fixture f;auto& api=f.api;auto& b=f.backend;
  assert(api.refresh(api.context) && api.ready(api.context));
  char label[64]{};assert(api.label(api.context,label,sizeof(label)));assert(std::string(label)=="User files");
  assert(b.count("label")==0);
  uint64_t size=0;bool dir=false;
  assert(api.stat(api.context,"/",&size,&dir) && dir);
  assert(api.stat(api.context,"/hello.txt",&size,&dir) && !dir && size==5);
  assert(api.stat(api.context,"/docs/book.txt",&size,&dir) && !dir && size==11);
  const auto names=list(api);assert(names==std::set<std::string>({"docs","hello.txt"}));
  assert(list(api,"/docs")==std::set<std::string>({"book.txt","nested"}));
  auto file=api.file_open_read(api.context,"/hello.txt",&size);assert(file && size==5);
  const size_t openCount=b.count("open_read");
  assert(!api.file_open_read(api.context,"/docs/book.txt",&size));assert(b.count("open_read")==openCount);
  auto dh=api.dir_open(api.context,"/docs");assert(dh);assert(!api.dir_open(api.context,"/"));
  char out[1024]{};assert(api.file_read(api.context,file,out,sizeof(out))==5);assert(std::string(out,5)=="hello");
  assert(!api.file_read(api.context,file,out,sizeof(out)));assert(api.file_close(api.context,file,true));
  api.dir_close(api.context,dh);
  b.nodes.emplace("/user/large",Backend::Node{false,std::string(1300,'r')});
  file=api.file_open_read(api.context,"/large",&size);assert(file && size==1300);
  size_t read=0;for(;;){const size_t got=api.file_read(api.context,file,out,sizeof(out));if(!got)break;assert(got<=512);read+=got;}
  assert(read==1300 && api.file_close(api.context,file,true));
  const size_t before=b.calls.size();assert(!api.file_read(api.context,file,out,sizeof(out)));assert(b.calls.size()==before);
  file=api.file_open_write(api.context,"/docs/new.txt");assert(file);
  assert(!b.nodes.count("/user/docs/new.txt"));
  const auto stage=b.stages();assert(stage.size()==1 && Backend::parent(stage[0])=="/user/docs");
  writeAll(api,file,std::string(1300,'w'));
  assert(list(api,"/docs")==std::set<std::string>({"book.txt","nested"}));
  const size_t start=b.calls.size();assert(api.file_close(api.context,file,true));
  assert(b.stages().empty() && b.nodes.at("/user/docs/new.txt").data==std::string(1300,'w'));
  size_t sync=start,close=start,rename=start;
  for(size_t i=start;i<b.calls.size();++i){if(b.calls[i].operation=="sync")sync=i;if(b.calls[i].operation=="file_close")close=i;if(b.calls[i].operation=="rename")rename=i;}
  assert(sync<close && close<rename && b.calls[close].commit);
  assert(!api.file_open_write(api.context,"/docs/new.txt"));assert(b.nodes.at("/user/docs/new.txt").data==std::string(1300,'w'));
  assert(api.remove(api.context,"/docs/new.txt"));assert(!b.nodes.count("/user/docs/new.txt"));
  file=api.file_open_write(api.context,"/aborted");assert(file);writeAll(api,file,"partial");
  assert(api.file_close(api.context,file,false));assert(!b.nodes.count("/user/aborted") && b.stages().empty());
  f.endClean();
}

void pathsAndEntries() {
  Fixture f;auto& api=f.api;auto& b=f.backend;
  const std::vector<std::string> invalid={"..","/..","/../private/app-data","/docs/../../apps/installed.elf",
    ".","/.","/docs/./book.txt","/docs/../hello.txt","//private","/docs//book.txt",
    "/docs\\book.txt","/bad\nname","/bad\177name","/bad:name","/bad?name","/bad*name",
    "/bad<name","/bad>name","/bad|name","/bad\"name","/trailing.","/trailing ",
    "/~R000001.TMP","/docs/~r000002.tmp","/~Reserved/leaf",
    std::string(600,'x')};
  for(const auto& path:invalid){
    const size_t count=b.calls.size();uint64_t size=0;bool directory=false;
    assert(!api.stat(api.context,path.c_str(),&size,&directory));
    assert(!api.dir_open(api.context,path.c_str()));
    assert(!api.file_open_read(api.context,path.c_str(),&size));
    assert(!api.file_open_write(api.context,path.c_str()));assert(!api.remove(api.context,path.c_str()));
    assert(b.calls.size()==count);
  }
  for(const char* path:{"/apps/installed.elf","/private/app-data","/user-other/secret"}){
    uint64_t size=0;bool directory=false;assert(!api.stat(api.context,path,&size,&directory));
    assert(!api.file_open_read(api.context,path,&size));
  }
  const size_t before=b.calls.size();assert(!api.remove(api.context,"/"));assert(b.calls.size()==before);
  b.nodes.emplace("/user/~RAB0001.TMP",Backend::Node{false,"unfinished"});
  b.injected={Backend::entry("."),Backend::entry(".."),Backend::entry("../private"),Backend::entry("a/b"),
    Backend::entry("a\\b"),Backend::entry("bad\nname"),Backend::entry("~R000002.TMP")};
  auto malformed=Backend::entry("irrelevant");std::memset(malformed.name,'x',sizeof(malformed.name));b.injected.push_back(malformed);
  const auto names=list(api);
  for(const auto& name:names)assert(name=="docs" || name=="hello.txt");
  f.endClean();
}

void lifetimeAndIsolation() {
  uint64_t size=0;
  Fixture a;auto stale=a.api;
  auto oldFile=stale.file_open_read(stale.context,"/hello.txt",&size);assert(oldFile);
  auto oldDir=stale.dir_open(stale.context,"/");assert(oldDir);
  a.endClean();const size_t stopped=a.backend.calls.size();
  char buffer[8]{};risc_storage_dirent_v1 entry{};
  assert(!stale.refresh(stale.context));assert(!stale.file_read(stale.context,oldFile,buffer,sizeof(buffer)));
  assert(!stale.dir_next(stale.context,oldDir,&entry));stale.dir_close(stale.context,oldDir);
  assert(!stale.file_close(stale.context,oldFile,true));assert(a.backend.calls.size()==stopped);
  assert(a.volume.begin(&a.api));assert(a.api.context!=stale.context);
  auto fresh=a.api.file_open_read(a.api.context,"/hello.txt",&size);assert(fresh && fresh!=oldFile);
  const size_t before=a.backend.calls.size();
  assert(!stale.ready(stale.context));assert(!a.api.file_read(a.api.context,oldFile,buffer,sizeof(buffer)));
  assert(!stale.file_read(stale.context,fresh,buffer,sizeof(buffer)));assert(a.backend.calls.size()==before);
  Fixture b;auto other=b.api.file_open_read(b.api.context,"/hello.txt",&size);assert(other);
  const size_t ac=a.backend.calls.size(),bc=b.backend.calls.size();
  assert(!a.api.file_read(a.api.context,other,buffer,sizeof(buffer)));
  assert(!b.api.file_read(b.api.context,fresh,buffer,sizeof(buffer)));
  assert(!a.api.file_close(a.api.context,other,true));assert(!b.api.file_close(b.api.context,fresh,true));
  assert(a.backend.calls.size()==ac && b.backend.calls.size()==bc);
  assert(a.api.file_read(a.api.context,fresh,buffer,sizeof(buffer))==5);
  assert(b.api.file_read(b.api.context,other,buffer,sizeof(buffer))==5);
  assert(a.api.file_close(a.api.context,fresh,true));assert(b.api.file_close(b.api.context,other,true));
  a.endClean();b.endClean();
}

void simultaneousInstanceHandles() {
  Fixture a,b;uint64_t size=0;char out[8]{};risc_storage_dirent_v1 entry{};
  const auto af=a.api.file_open_read(a.api.context,"/hello.txt",&size);
  const auto bf=b.api.file_open_read(b.api.context,"/hello.txt",&size);
  const auto ad=a.api.dir_open(a.api.context,"/");const auto bd=b.api.dir_open(b.api.context,"/");
  assert(af && bf && ad && bd && af!=bf && ad!=bd && af!=ad && bf!=bd);
  const size_t ac=a.backend.calls.size(),bc=b.backend.calls.size();
  assert(!a.api.file_read(a.api.context,bf,out,sizeof(out)));
  assert(!b.api.file_read(b.api.context,af,out,sizeof(out)));
  assert(!a.api.file_close(a.api.context,bf,true));assert(!b.api.file_close(b.api.context,af,true));
  assert(!a.api.dir_next(a.api.context,bd,&entry));assert(!b.api.dir_next(b.api.context,ad,&entry));
  a.api.dir_close(a.api.context,bd);b.api.dir_close(b.api.context,ad);
  assert(!a.api.file_read(a.api.context,ad,out,sizeof(out)));
  assert(!a.api.dir_next(a.api.context,af,&entry));
  assert(a.backend.calls.size()==ac && b.backend.calls.size()==bc);
  a.endClean();b.endClean();
}

void endRollback() {
  Fixture f;auto file=f.api.file_open_write(f.api.context,"/docs/unfinished");assert(file);
  writeAll(f.api,file,"never publish");assert(f.api.dir_open(f.api.context,"/"));
  const size_t renames=f.backend.count("rename");f.endClean();
  assert(!f.backend.nodes.count("/user/docs/unfinished") && f.backend.stages().empty());
  assert(f.backend.count("rename")==renames);
  const auto closed=std::find_if(f.backend.calls.rbegin(),f.backend.calls.rend(),[](const Backend::Call& c){return c.operation=="file_close";});
  assert(closed!=f.backend.calls.rend() && !closed->commit);
}

void writerErrors() {
  uint64_t size=0;
  for(auto fault:{Backend::Fault::ShortWrite,Backend::Fault::WriteError,Backend::Fault::Sync}){
    Fixture f;auto file=f.api.file_open_write(f.api.context,"/docs/bad");assert(file);
    f.backend.fault=fault;
    const size_t got=f.api.file_write(f.api.context,file,"payload",7);
    if(fault==Backend::Fault::Sync)assert(got==7);else assert(got<7);
    assert(!f.api.file_close(f.api.context,file,true));
    assert(!f.backend.nodes.count("/user/docs/bad") && f.backend.stages().empty());
    assert(f.backend.count("rename")==0 && f.volume.retained());
    const auto closed=std::find_if(f.backend.calls.rbegin(),f.backend.calls.rend(),[](const Backend::Call& c){return c.operation=="file_close";});
    assert(closed!=f.backend.calls.rend() && !closed->commit);
    assertNoProviderAfterRetention(f);
  }
  Fixture read;auto file=read.api.file_open_read(read.api.context,"/hello.txt",&size);assert(file);
  read.backend.fault=Backend::Fault::ReadError;char out[8]{};assert(!read.api.file_read(read.api.context,file,out,sizeof(out)));
  assert(read.api.file_close(read.api.context,file,true));read.endClean();
  Fixture dir;auto dh=dir.api.dir_open(dir.api.context,"/");assert(dh);
  dir.backend.fault=Backend::Fault::DirError;risc_storage_dirent_v1 entry{};assert(!dir.api.dir_next(dir.api.context,dh,&entry));
  dir.api.dir_close(dir.api.context,dh);dir.endClean();
}

void custodyFailures() {
  uint64_t size=0;
  for(auto fault:{Backend::Fault::CloseBefore,Backend::Fault::CloseAfter,
      Backend::Fault::AbortBefore,Backend::Fault::AbortAfter,
      Backend::Fault::RenameBefore,Backend::Fault::RenameAfter}){
    Fixture f;auto file=f.api.file_open_write(f.api.context,"/docs/uncertain");assert(file);writeAll(f.api,file,"payload");
    const bool abort=fault==Backend::Fault::AbortBefore || fault==Backend::Fault::AbortAfter;
    f.backend.fault=fault;assert(!f.api.file_close(f.api.context,file,!abort));
    assert(f.backend.count("file_close")==1);
    if(fault==Backend::Fault::RenameBefore || fault==Backend::Fault::RenameAfter)assert(f.backend.count("rename")==1);
    else assert(!f.backend.count("rename"));
    assertNoProviderAfterRetention(f);
  }
  for(auto fault:{Backend::Fault::CloseBefore,Backend::Fault::CloseAfter}){
    Fixture f;auto file=f.api.file_open_read(f.api.context,"/hello.txt",&size);assert(file);
    f.backend.fault=fault;assert(!f.api.file_close(f.api.context,file,true));
    assert(f.backend.count("file_close")==1);assertNoProviderAfterRetention(f);
  }
  for(auto fault:{Backend::Fault::DirCloseBefore,Backend::Fault::DirCloseAfter}){
    Fixture f;auto dir=f.api.dir_open(f.api.context,"/");assert(dir);
    f.backend.fault=fault;f.api.dir_close(f.api.context,dir);
    assert(f.backend.count("dir_close")==1);assertNoProviderAfterRetention(f);
  }
  for(auto fault:{Backend::Fault::AbortBefore,Backend::Fault::AbortAfter}){
    Fixture f;assert(f.api.file_open_write(f.api.context,"/end-uncertain"));
    f.backend.fault=fault;assert(!f.volume.end());assert(f.backend.count("file_close")==1);
    assertNoProviderAfterRetention(f);
  }
  Fixture conflict;auto file=conflict.api.file_open_write(conflict.api.context,"/race");assert(file);
  writeAll(conflict.api,file,"our content");conflict.backend.nodes.emplace("/user/race",Backend::Node{false,"racing writer"});
  assert(!conflict.api.file_close(conflict.api.context,file,true));
  assert(conflict.backend.nodes.at("/user/race").data=="racing writer");assertNoProviderAfterRetention(conflict);
}

void fences() {
  uint64_t size=0;
  Fixture f;auto file=f.api.file_open_read(f.api.context,"/hello.txt",&size);assert(file);
  auto dir=f.api.dir_open(f.api.context,"/");assert(dir);
  f.backend.owner=false;
  const size_t before=f.backend.calls.size();char out[16]{};bool directory=false;risc_storage_dirent_v1 entry{};
  assert(!f.api.refresh(f.api.context));assert(!f.api.ready(f.api.context));
  assert(!f.api.stat(f.api.context,"/hello.txt",&size,&directory));
  assert(!f.api.file_read(f.api.context,file,out,sizeof(out)));
  assert(!f.api.file_write(f.api.context,file,"x",1));assert(!f.api.file_close(f.api.context,file,true));
  assert(!f.api.dir_next(f.api.context,dir,&entry));f.api.dir_close(f.api.context,dir);
  assert(!f.api.file_open_write(f.api.context,"/no"));assert(!f.api.remove(f.api.context,"/hello.txt"));
  assert(!f.volume.end());
  assert(f.backend.calls.size()==before && !f.volume.retained());
  f.backend.owner=true;
  assert(f.api.file_close(f.api.context,file,true));f.api.dir_close(f.api.context,dir);f.endClean();
  Fixture unsafe;assert(unsafe.api.file_open_write(unsafe.api.context,"/unfinished"));unsafe.backend.safe=false;
  const size_t count=unsafe.backend.calls.size();assert(!unsafe.volume.end());
  assert(unsafe.backend.calls.size()==count);unsafe.backend.safe=true;assertNoProviderAfterRetention(unsafe);
  Fixture revoked;revoked.backend.safe=false;const size_t calls=revoked.backend.calls.size();
  assert(!revoked.api.ready(revoked.api.context));assert(revoked.backend.calls.size()==calls);
  revoked.backend.safe=true;assertNoProviderAfterRetention(revoked);
}

void safetyLossAtEveryCallback() {
  for(const char* operation:{"refresh","ready","stat","dir_open","dir_next","dir_error","dir_close",
      "open_read","file_info","read","file_error","open_write","write","sync","file_close","rename","remove","last_error"}){
    Fixture f;auto& api=f.api;auto& b=f.backend;uint64_t size=0;bool directory=false;
    char buffer[16];std::memset(buffer,42,sizeof(buffer));risc_storage_dirent_v1 entry{};
    const std::string op=operation;
    uint32_t handle=0;
    if(op=="dir_next" || op=="dir_error" || op=="dir_close")handle=api.dir_open(api.context,"/");
    if(op=="file_info" || op=="read" || op=="file_error")handle=api.file_open_read(api.context,"/hello.txt",&size);
    if(op=="write" || op=="sync" || op=="file_close" || op=="rename")handle=api.file_open_write(api.context,"/new");
    b.revokeAfter=op;const size_t before=b.calls.size();
    if(op=="refresh")assert(!api.refresh(api.context));
    else if(op=="ready")assert(!api.ready(api.context));
    else if(op=="stat")assert(!api.stat(api.context,"/hello.txt",&size,&directory));
    else if(op=="dir_open")assert(!api.dir_open(api.context,"/"));
    else if(op=="dir_next" || op=="dir_error")assert(!api.dir_next(api.context,handle,&entry));
    else if(op=="dir_close")api.dir_close(api.context,handle);
    else if(op=="open_read")assert(!api.file_open_read(api.context,"/hello.txt",&size));
    else if(op=="file_info" || op=="read" || op=="file_error"){
      assert(!api.file_read(api.context,handle,buffer,sizeof(buffer)));
      for(char byte:buffer)assert(byte==42);
    }
    else if(op=="open_write")assert(!api.file_open_write(api.context,"/new"));
    else if(op=="write")assert(!api.file_write(api.context,handle,"data",4));
    else if(op=="sync" || op=="file_close" || op=="rename")assert(!api.file_close(api.context,handle,true));
    else if(op=="remove")assert(!api.remove(api.context,"/hello.txt"));
    else if(op=="last_error")assert(!api.stat(api.context,"/missing",&size,&directory));
    assert(b.calls.size()>before && b.calls.back().operation==op && !b.safe);
    b.safe=true;b.revokeAfter.clear();assertNoProviderAfterRetention(f);
  }
  // Skipped entries loop back to the provider: handle_error may revoke
  // custody even when it returns no error, so continuation needs a fence.
  for(const char* name:{".","..","~R000001.TMP","~r000002.tmp"}){
    Fixture f;f.backend.injected={Backend::entry(name)};
    const auto dir=f.api.dir_open(f.api.context,"/");assert(dir);
    f.backend.revokeAfter="dir_error";const size_t before=f.backend.calls.size();
    const size_t nextCalls=f.backend.count("dir_next");
    risc_storage_dirent_v1 out{};std::strcpy(out.name,"unchanged");
    assert(!f.api.dir_next(f.api.context,dir,&out));assert(std::string(out.name)=="unchanged");
    assert(f.backend.calls.size()==before+2);
    assert(f.backend.calls[before].operation=="dir_next" && f.backend.calls.back().operation=="dir_error");
    assert(f.backend.count("dir_next")==nextCalls+1 && !f.backend.safe);
    f.backend.safe=true;f.backend.revokeAfter.clear();assertNoProviderAfterRetention(f);
  }
  // Exercise each member of compound refresh and begin paths independently.
  for(const char* operation:{"refresh","ready","stat"}){
    Fixture f;f.backend.revokeAfter=operation;assert(!f.api.refresh(f.api.context));
    assert(f.backend.calls.back().operation==operation);f.backend.safe=true;f.backend.revokeAfter.clear();
    assertNoProviderAfterRetention(f);
  }
  for(const char* operation:{"ready","stat"}){
    Backend b;ScopedUserVolume v(b.hooks());assert(v.configure(&b.api.base,Root,"User"));
    b.revokeAfter=operation;risc_storage_volume_api_v1 api{};assert(!v.begin(&api));
    assert(v.retained() && !v.exitSafe() && b.calls.back().operation==operation);
    b.safe=true;b.revokeAfter.clear();const size_t before=b.calls.size();
    assert(!v.end());assert(!v.begin(&api));assert(b.calls.size()==before);
  }
  // A failed destination stat cannot be followed by staging after safety loss.
  Fixture stage;stage.backend.revokeAfter="stat";assert(!stage.api.file_open_write(stage.api.context,"/new"));
  assert(stage.backend.calls.back().operation=="stat" && stage.backend.stages().empty());
  stage.backend.safe=true;stage.backend.revokeAfter.clear();assertNoProviderAfterRetention(stage);
}

void directoryValidationAndBounds() {
  const std::vector<std::string> malformed={"", "../private", "a/b", "a\\b", "bad\nname", "bad.", "bad ", "bad:name"};
  for(const auto& name:malformed){
    Fixture f;f.backend.injected={Backend::entry(name)};const auto dir=f.api.dir_open(f.api.context,"/");assert(dir);
    risc_storage_dirent_v1 out{};std::strcpy(out.name,"unchanged");
    assert(!f.api.dir_next(f.api.context,dir,&out));assert(std::string(out.name)=="unchanged");
    const size_t before=f.backend.calls.size();assert(!f.api.dir_next(f.api.context,dir,&out));
    assert(f.backend.calls.size()==before);char error[128]{};assert(f.api.last_error(f.api.context,error,sizeof(error)) && error[0]);
    f.api.dir_close(f.api.context,dir);f.endClean();
  }
  Fixture unterminated;auto bad=Backend::entry("x");std::memset(bad.name,'x',sizeof(bad.name));unterminated.backend.injected={bad};
  auto dir=unterminated.api.dir_open(unterminated.api.context,"/");risc_storage_dirent_v1 out{};
  assert(!unterminated.api.dir_next(unterminated.api.context,dir,&out));unterminated.api.dir_close(unterminated.api.context,dir);unterminated.endClean();
  Fixture invalidKind;auto kind=Backend::entry("normal.txt");kind.is_directory=2;invalidKind.backend.injected={kind};
  dir=invalidKind.api.dir_open(invalidKind.api.context,"/");assert(dir);std::strcpy(out.name,"unchanged");out.is_directory=1;
  assert(!invalidKind.api.dir_next(invalidKind.api.context,dir,&out));assert(std::string(out.name)=="unchanged" && out.is_directory==1);
  const size_t kindCalls=invalidKind.backend.calls.size();assert(!invalidKind.api.dir_next(invalidKind.api.context,dir,&out));
  assert(invalidKind.backend.calls.size()==kindCalls);char kindError[128]{};
  assert(invalidKind.api.last_error(invalidKind.api.context,kindError,sizeof(kindError)) && kindError[0]);
  invalidKind.api.dir_close(invalidKind.api.context,dir);invalidKind.endClean();
  Fixture hidden;hidden.backend.injected={Backend::entry("."),Backend::entry(".."),Backend::entry("~r000001.tmp"),Backend::entry("~R000002.TMP")};
  assert(list(hidden.api)==std::set<std::string>({"docs","hello.txt"}));hidden.endClean();
  Fixture bounded;for(size_t i=0;i<ScopedUserVolume::SkipMax+2;++i)bounded.backend.injected.push_back(Backend::entry("~R000003.TMP"));
  dir=bounded.api.dir_open(bounded.api.context,"/");const size_t start=bounded.backend.count("dir_next");
  assert(!bounded.api.dir_next(bounded.api.context,dir,&out));
  assert(bounded.backend.count("dir_next")-start==ScopedUserVolume::SkipMax);
  char error[128]{};assert(bounded.api.last_error(bounded.api.context,error,sizeof(error)) && error[0]);
  bounded.api.dir_close(bounded.api.context,dir);bounded.endClean();
}

void readIntegrity() {
  for(auto fault:{Backend::Fault::ShortRead,Backend::Fault::ReadError}){
    Fixture f;uint64_t size=0;const auto file=f.api.file_open_read(f.api.context,"/hello.txt",&size);assert(file);
    char out[8];std::memset(out,42,sizeof(out));f.backend.fault=fault;
    assert(!f.api.file_read(f.api.context,file,out,sizeof(out)));for(char byte:out)assert(byte==42);
    const size_t count=f.backend.calls.size();assert(!f.api.file_read(f.api.context,file,out,sizeof(out)));assert(f.backend.calls.size()==count);
    assert(f.api.file_close(f.api.context,file,true));f.endClean();
  }
  Fixture changed;uint64_t size=0;auto file=changed.api.file_open_read(changed.api.context,"/hello.txt",&size);assert(file);
  changed.backend.nodes.at("/user/hello.txt").data="changed length";char out[16];std::memset(out,42,sizeof(out));
  assert(!changed.api.file_read(changed.api.context,file,out,sizeof(out)));for(char byte:out)assert(byte==42);
  assert(changed.backend.count("read")==0);assert(changed.api.file_close(changed.api.context,file,true));changed.endClean();
}

void exactCleanupCustody() {
  for(auto fault:{Backend::Fault::AbortBefore,Backend::Fault::AbortAfter,
      Backend::Fault::DirCloseBefore,Backend::Fault::DirCloseAfter}){
    Fixture f;auto file=f.api.file_open_write(f.api.context,"/unfinished");assert(file);
    assert(f.api.file_write(f.api.context,file,"x",1)==1);assert(f.api.dir_open(f.api.context,"/"));
    f.backend.fault=fault;assert(!f.volume.end());
    assert(f.backend.count("file_close")==1);
    if(fault==Backend::Fault::AbortBefore || fault==Backend::Fault::AbortAfter){
      assert(!f.backend.count("dir_close") && f.backend.directories.size()==1);
    }else{
      assert(f.backend.count("dir_close")==1 && f.backend.files.empty() && f.backend.stages().empty());
    }
    assertNoProviderAfterRetention(f);
  }
  for(auto failure:{Backend::Fault::ShortWrite,Backend::Fault::WriteError,Backend::Fault::Sync}){
    for(auto rollback:{Backend::Fault::AbortBefore,Backend::Fault::AbortAfter}){
      Fixture f;auto file=f.api.file_open_write(f.api.context,"/failed-rollback");assert(file);
      f.backend.fault=failure;f.backend.thenFault=rollback;
      const size_t written=f.api.file_write(f.api.context,file,"content",7);
      assert((failure==Backend::Fault::Sync && written==7) || (failure!=Backend::Fault::Sync && !written));
      assert(!f.api.file_close(f.api.context,file,true));
      assert(f.backend.count("file_close")==1 && !f.backend.count("rename"));assertNoProviderAfterRetention(f);
    }
  }
}

void stagingCollision() {
  Fixture f;auto file=f.api.file_open_write(f.api.context,"/first");assert(file);
  const auto stage=f.backend.stages();assert(stage.size()==1);unsigned value=0;
  assert(std::sscanf(Backend::leaf(stage[0]).c_str(),"~R%06X.TMP",&value)==1);
  assert(f.api.file_close(f.api.context,file,false));char next[32]{};std::snprintf(next,sizeof(next),"/user/~R%06X.TMP",value+1);
  f.backend.nodes.emplace(next,Backend::Node{false,"previous invocation"});
  assert(!f.api.file_open_write(f.api.context,"/collision"));
  assert(f.backend.nodes.at(next).data=="previous invocation" && !f.backend.nodes.count("/user/collision"));
  assert(!f.volume.retained());
  assert(list(f.api)==std::set<std::string>({"docs","hello.txt"}));f.endClean();
}

void unavailableAndSlots() {
  Backend b;ScopedUserVolume v(b.hooks());assert(v.configure(&b.api.base,Root,"User files"));
  risc_storage_volume_api_v1 api{};b.media=false;assert(!v.begin(&api));assert(!v.retained() && v.exitSafe());
  b.media=true;b.nodes.erase(Root);assert(!v.begin(&api));assert(!v.retained() && v.exitSafe());
  b.nodes.emplace(Root,Backend::Node{false,"not a directory"});assert(!v.begin(&api));
  b.nodes.at(Root).directory=true;assert(v.begin(&api));assert(v.end());
  std::vector<std::unique_ptr<Backend>> backends;
  std::vector<std::unique_ptr<ScopedUserVolume>> volumes;
  std::vector<risc_storage_volume_api_v1> tables(ScopedUserVolume::SlotMax+1);
  for(size_t i=0;i<tables.size();++i){
    backends.emplace_back(new Backend);volumes.emplace_back(new ScopedUserVolume(backends.back()->hooks()));
    assert(volumes.back()->configure(&backends.back()->api.base,Root,"User"));
    if(i<ScopedUserVolume::SlotMax)assert(volumes.back()->begin(&tables[i]));
    else {assert(!volumes.back()->begin(&tables[i]));assert(backends.back()->calls.empty());}
  }
  const auto expired=tables.front();assert(volumes.front()->end());
  assert(volumes.back()->begin(&tables.back()));assert(expired.context!=tables.back().context);
  assert(!expired.ready(expired.context));
  for(auto& volume:volumes)assert(volume->end());
}

void invalidArgumentsAndStaleObject() {
  Fixture f;auto& api=f.api;uint64_t size=0;bool directory=false;char out[8]{};risc_storage_dirent_v1 entry{};
  const size_t before=f.backend.calls.size();
  assert(!api.stat(api.context,nullptr,&size,&directory));assert(!api.stat(api.context,"/hello.txt",nullptr,&directory));
  assert(!api.stat(api.context,"/hello.txt",&size,nullptr));assert(!api.dir_open(api.context,nullptr));
  assert(!api.file_open_read(api.context,"/hello.txt",nullptr));assert(!api.file_open_read(api.context,nullptr,&size));
  assert(!api.file_open_write(api.context,nullptr));assert(!api.remove(api.context,nullptr));
  assert(!api.file_read(api.context,0,out,sizeof(out)));assert(!api.file_write(api.context,0,"x",1));
  assert(!api.file_close(api.context,0,false));assert(!api.dir_next(api.context,0,&entry));api.dir_close(api.context,0);
  assert(!api.label(api.context,nullptr,sizeof(out)));assert(!api.last_error(api.context,nullptr,sizeof(out)));
  assert(f.backend.calls.size()==before);
  auto file=api.file_open_read(api.context,"/hello.txt",&size);assert(file);const size_t count=f.backend.calls.size();
  assert(!api.file_read(api.context,file,nullptr,1));assert(!api.file_read(api.context,file,nullptr,0));
  assert(!api.file_write(api.context,file,"x",1));assert(f.backend.calls.size()==count);
  assert(api.file_close(api.context,file,true));
  file=api.file_open_write(api.context,"/empty");assert(file);const size_t calls=f.backend.calls.size();
  assert(!api.file_write(api.context,file,nullptr,1));assert(!api.file_write(api.context,file,nullptr,0));
  assert(!api.file_read(api.context,file,out,sizeof(out)));assert(f.backend.calls.size()==calls);
  assert(api.file_close(api.context,file,true));assert(f.backend.nodes.at("/user/empty").data.empty());f.endClean();
  risc_storage_volume_api_v1 destroyed{};
  {Fixture temporary;destroyed=temporary.api;temporary.endClean();}
  Fixture replacement;const size_t current=replacement.backend.calls.size();
  assert(!destroyed.ready(destroyed.context));
  assert(!replacement.api.ready(reinterpret_cast<void*>(uintptr_t(0xdeadbeef))));
  assert(replacement.backend.calls.size()==current);replacement.endClean();
}

void reentrantAdmissionAndSnapshots() {
  Fixture f;auto& api=f.api;auto& b=f.backend;uint32_t file=0,dir=0;size_t attempts=0;
  b.reenter=[&]{
    ++attempts;const size_t before=b.calls.size();char out[16]{};uint64_t size=0;bool directory=false;risc_storage_dirent_v1 entry{};
    assert(!api.refresh(api.context));assert(!api.ready(api.context));assert(!api.label(api.context,out,sizeof(out)));
    assert(!api.stat(api.context,"/hello.txt",&size,&directory));assert(!api.dir_open(api.context,"/"));
    assert(!api.dir_next(api.context,dir,&entry));api.dir_close(api.context,dir);
    assert(!api.file_open_read(api.context,"/hello.txt",&size));assert(!api.file_open_write(api.context,"/reentrant"));
    assert(!api.file_read(api.context,file,out,sizeof(out)));assert(!api.file_write(api.context,file,"x",1));
    assert(!api.file_close(api.context,file,false));assert(!api.remove(api.context,"/hello.txt"));
    (void)api.last_error(api.context,out,sizeof(out));risc_storage_volume_api_v1 other{};
    assert(!f.volume.begin(&other));assert(!f.volume.end());assert(!f.volume.exitSafe());assert(b.calls.size()==before);
  };
  uint64_t size=0;assert(api.refresh(api.context));assert(api.ready(api.context));
  file=api.file_open_read(api.context,"/hello.txt",&size);assert(file);dir=api.dir_open(api.context,"/");assert(dir);
  char out[8]{};assert(api.file_read(api.context,file,out,sizeof(out))==5);risc_storage_dirent_v1 entry{};
  while(api.dir_next(api.context,dir,&entry)){}
  api.dir_close(api.context,dir);dir=0;assert(api.file_close(api.context,file,true));file=0;
  file=api.file_open_write(api.context,"/created");assert(file);assert(api.file_write(api.context,file,"content",7)==7);
  assert(api.file_close(api.context,file,true));file=0;assert(api.remove(api.context,"/created"));
  bool directory=false;assert(!api.stat(api.context,"/missing",&size,&directory));
  assert(attempts>=20);b.reenter={};f.endClean();
  Fixture snapshot;file=snapshot.api.file_open_write(snapshot.api.context,"/snapshot");assert(file);
  std::string source="original";snapshot.backend.reenter=[&]{source.assign(source.size(),'x');};
  assert(snapshot.api.file_write(snapshot.api.context,file,source.data(),source.size())==8);
  assert(source=="xxxxxxxx");snapshot.backend.reenter={};assert(snapshot.api.file_close(snapshot.api.context,file,true));
  assert(snapshot.backend.nodes.at("/user/snapshot").data=="original");snapshot.endClean();
}

void cleanEofDiagnostics() {
  Fixture f;auto& api=f.api;uint64_t size=0;char out[16]{},error[128]{};
  auto file=api.file_open_read(api.context,"/hello.txt",&size);assert(file);
  assert(api.file_read(api.context,file,out,sizeof(out))==5);
  assert(!api.file_read(api.context,0,out,sizeof(out)));assert(api.last_error(api.context,error,sizeof(error)) && error[0]);
  assert(!api.file_read(api.context,file,out,sizeof(out)));assert(api.last_error(api.context,error,sizeof(error)) && !error[0]);
  assert(api.file_close(api.context,file,true));auto dir=api.dir_open(api.context,"/");assert(dir);risc_storage_dirent_v1 entry{};
  while(api.dir_next(api.context,dir,&entry)){}
  assert(api.last_error(api.context,error,sizeof(error)) && !error[0]);api.dir_close(api.context,dir);f.endClean();
}

void configuration() {
  for(const char* path:{"", "/", "relative", "/../user", "/user/..", "/user//docs", "/user/.", "/user\\docs"}){
    Backend b;ScopedUserVolume v(b.hooks());assert(!v.configure(&b.api.base,path,"User files"));assert(b.calls.empty());
  }
  Backend b;
  for(int missing=0;missing<10;++missing){
    auto api=b.api;
    switch(missing){case 0:api.base.api_version=2;break;case 1:api.base.struct_size=sizeof(api.base);break;
      case 2:api.base.file_close=nullptr;break;case 3:api.file_sync=nullptr;break;case 4:api.dir_close_checked=nullptr;break;
      case 5:api.handle_error=nullptr;break;case 6:api.rename=nullptr;break;case 7:api.base.file_open_write=nullptr;break;
      case 8:api.base.stat=nullptr;break;case 9:api.file_info=nullptr;break;}
    ScopedUserVolume v(b.hooks());assert(!v.configure(&api.base,Root,"User files"));assert(b.calls.empty());
  }
  ScopedUserVolume unconfigured(b.hooks());risc_storage_volume_api_v1 out{};assert(!unconfigured.begin(&out));
  assert(!unconfigured.configure(nullptr,Root,"User files"));
  assert(!unconfigured.configure(&b.api.base,nullptr,"User files"));
  Fixture f;assert(!f.volume.configure(&f.backend.api.base,Root,"Second"));
  risc_storage_volume_api_v1 second{};assert(!f.volume.begin(&second));f.endClean();
}

void assertExtendedRetained(ExtendedFixture& f){
  assertNoProviderAfterRetention(f);
  const size_t before=f.backend.calls.size();
  assert(!f.ext.mkdir(f.api.context,"/never"));
  assert(!f.ext.rename(f.api.context,"/hello.txt","/never"));
  assert(!f.ext.dir_close_checked(f.api.context,1));
  assert(f.ext.handle_error(f.api.context,1,true));
  assert(f.ext.handle_error(f.api.context,1,false));
  risc_storage_volume_api_v1_ext fresh{};assert(!f.volume.beginExtended(&fresh));
  assert(f.backend.calls.size()==before);
}

void extendedAdmissionAndLifetime(){
  {
    Backend b;b.api.mkdir=nullptr;ScopedUserVolume volume(b.hooks());
    assert(volume.configure(&b.api.base,Root,"User"));risc_storage_volume_api_v1_ext ext{};
    assert(!volume.beginExtended(&ext));assert(!ext.base.context && b.calls.empty());
    risc_storage_volume_api_v1 legacy{};assert(volume.begin(&legacy));
    assert(legacy.struct_size==sizeof(legacy) && !risc_storage_volume_extension(&legacy));assert(volume.end());
  }
  Backend b;ScopedUserVolume volume(b.hooks());risc_storage_volume_api_v1_ext ext{};
  assert(!volume.beginExtended(&ext));assert(b.calls.empty());
  assert(volume.configure(&b.api.base,Root,"User"));
  const size_t before=b.calls.size();
  assert(!volume.beginExtended(nullptr));assert(b.calls.size()==before);
  assert(volume.beginExtended(&ext));
  assert(risc_storage_volume_extension(&ext.base)==&ext && ext.base.struct_size==sizeof(ext));
  assert(ext.dir_close_checked && ext.handle_error && ext.mkdir && ext.rename);
  assert(!ext.file_open && !ext.file_seek && !ext.file_info && !ext.file_sync && !ext.dir_rewind);
  assert(!risc_storage_volume_power(&ext.base));
  auto old=ext;auto dir=ext.base.dir_open(ext.base.context,"/");assert(dir);
  assert(ext.dir_close_checked(ext.base.context,dir));assert(ext.handle_error(ext.base.context,dir,true));
  assert(volume.end());assert(volume.beginExtended(&ext));assert(ext.base.context!=old.base.context);
  const size_t stopped=b.calls.size();
  assert(!old.mkdir(old.base.context,"/stale"));assert(!old.rename(old.base.context,"/hello.txt","/stale"));
  assert(!old.dir_close_checked(old.base.context,dir));assert(old.handle_error(old.base.context,dir,true));
  assert(!ext.dir_close_checked(ext.base.context,dir));assert(b.calls.size()==stopped);
  assert(volume.end());
  // A provider table can be unmapped after end. Admission must establish fresh
  // custody before any access, including checking optional function pointers.
  Backend expired;auto*table=new risc_storage_volume_api_v1_ext(expired.api);
  ScopedUserVolume invalidated(expired.hooks());assert(invalidated.configure(&table->base,Root,"User"));
  risc_storage_volume_api_v1_ext first{};assert(invalidated.beginExtended(&first));assert(invalidated.end());
  delete table;expired.safe=false;const size_t calls=expired.calls.size();
  risc_storage_volume_api_v1_ext untouched{};untouched.base.api_version=77;
  expired.owner=false;assert(!invalidated.beginExtended(&untouched));assert(!invalidated.retained());
  expired.owner=true;assert(!invalidated.beginExtended(&untouched));assert(invalidated.retained());
  assert(untouched.base.api_version==77 && expired.calls.size()==calls);
}

void extendedManagement(){
  ExtendedFixture f;auto& e=f.ext;auto& a=f.api;auto& b=f.backend;
  assert(e.mkdir(a.context,"/destination"));assert(b.nodes.at("/user/destination").directory);
  assert(e.rename(a.context,"/hello.txt","/destination/greeting.txt"));
  assert(!b.nodes.count("/user/hello.txt") && b.nodes.at("/user/destination/greeting.txt").data=="hello");
  assert(e.rename(a.context,"/docs","/destination/books"));
  assert(b.nodes.at("/user/destination/books/book.txt").data=="chapter one");
  assert(b.nodes.at("/user/destination/books/nested").directory && !b.nodes.count("/user/docs"));
  assert(list(a,"/destination")==std::set<std::string>({"books","greeting.txt"}));
  const size_t mutations=b.count("rename")+b.count("mkdir");
  assert(!e.mkdir(a.context,"/destination"));
  assert(!e.rename(a.context,"/missing","/elsewhere"));
  assert(!e.rename(a.context,"/destination/greeting.txt","/destination/books/book.txt"));
  assert(!e.rename(a.context,"/destination/books","/destination/books/nested/new"));
  assert(!e.rename(a.context,"/destination/books","/DESTINATION/BOOKS/nested/new"));
  assert(!e.rename(a.context,"/destination","/destination"));
  assert(b.count("rename")+b.count("mkdir")==mutations && !f.volume.retained());
  assert(b.nodes.at("/private/app-data").data=="private app data");
  assert(b.nodes.at("/apps/installed.elf").data=="installed private image");
  assert(b.nodes.at("/user-other/secret").data=="sibling");f.endClean();
}

void extendedInvalidPathsAndBusy(){
  ExtendedFixture f;auto& e=f.ext;auto& a=f.api;auto& b=f.backend;
  std::vector<std::string> paths={"", "/", "relative", "/../private", "/docs/../hello.txt", "/docs//x", "/docs/.",
    "/docs/..", "/docs/", "/~r000001.tmp", "/docs/~Reserved", "/bad:name", "/bad\\name", "/bad?name",
    "/bad\nname", "/bad\177name", "/name.", "/name ", "/"+std::string(128,'x'), "/"+std::string(193,'x')};
  for(const auto& p:paths){const size_t calls=b.calls.size();
    assert(!e.mkdir(a.context,p.c_str()));assert(!e.rename(a.context,"/hello.txt",p.c_str()));
    assert(!e.rename(a.context,p.c_str(),"/valid"));assert(b.calls.size()==calls);
  }
  const size_t before=b.calls.size();assert(!e.mkdir(a.context,nullptr));
  assert(!e.rename(a.context,nullptr,"/valid"));assert(!e.rename(a.context,"/hello.txt",nullptr));
  assert(b.calls.size()==before);
  for(unsigned kind=0;kind<3;++kind){uint64_t size=0;uint32_t h=kind==0?a.dir_open(a.context,"/"):
    kind==1?a.file_open_read(a.context,"/hello.txt",&size):a.file_open_write(a.context,"/pending");assert(h);
    const size_t calls=b.calls.size();assert(!e.mkdir(a.context,"/blocked"));
    assert(!e.rename(a.context,"/docs","/blocked"));assert(b.calls.size()==calls);
    if(kind==0)assert(e.dir_close_checked(a.context,h));else assert(a.file_close(a.context,h,false));
  }
  f.endClean();
}

void extendedHandleDiagnostics(){
  ExtendedFixture f;auto& e=f.ext;auto& a=f.api;auto& b=f.backend;
  auto dir=a.dir_open(a.context,"/");assert(dir);risc_storage_dirent_v1 entry{};
  while(a.dir_next(a.context,dir,&entry)){}
  const size_t before=b.calls.size();assert(!e.handle_error(a.context,dir,true));
  assert(e.handle_error(a.context,dir,false) && e.handle_error(a.context,0,true));
  assert(b.calls.size()==before);assert(e.dir_close_checked(a.context,dir));
  b.injected={Backend::entry("bad/name")};dir=a.dir_open(a.context,"/");assert(dir);
  assert(!a.dir_next(a.context,dir,&entry));char error[128]{},after[128]{};
  assert(a.last_error(a.context,error,sizeof(error)) && error[0]);
  const size_t failedCalls=b.calls.size();assert(e.handle_error(a.context,dir,true));
  assert(a.last_error(a.context,after,sizeof(after)) && !std::strcmp(error,after));assert(b.calls.size()==failedCalls);
  assert(e.dir_close_checked(a.context,dir));
  auto file=a.file_open_write(a.context,"/fail");assert(file);b.fault=Backend::Fault::ShortWrite;
  assert(!a.file_write(a.context,file,"data",4));assert(e.handle_error(a.context,file,false));
  assert(a.file_close(a.context,file,false));assert(e.handle_error(a.context,file,false));
  uint64_t size=0;file=a.file_open_read(a.context,"/hello.txt",&size);assert(file);
  ExtendedFixture other;auto otherDir=other.api.dir_open(other.api.context,"/");assert(otherDir);
  const size_t own=b.calls.size(),foreign=other.backend.calls.size();
  assert(e.handle_error(a.context,otherDir,true));assert(!e.dir_close_checked(a.context,otherDir));
  assert(other.ext.handle_error(other.api.context,file,false));assert(b.calls.size()==own && other.backend.calls.size()==foreign);
  b.owner=false;assert(e.handle_error(a.context,file,false));assert(!e.mkdir(a.context,"/no"));
  assert(!e.rename(a.context,"/docs","/no"));assert(b.calls.size()==own && !f.volume.retained());
  b.owner=true;assert(!e.handle_error(a.context,file,false));
  assert(a.file_close(a.context,file,true));other.endClean();f.endClean();
}

void extendedUncertainMutations(){
  for(auto fault:{Backend::Fault::MkdirBefore,Backend::Fault::MkdirAfter,
      Backend::Fault::RenameBefore,Backend::Fault::RenameAfter}){
    ExtendedFixture f;auto& b=f.backend;b.fault=fault;
    const bool mkdir=fault==Backend::Fault::MkdirBefore || fault==Backend::Fault::MkdirAfter;
    if(mkdir)assert(!f.ext.mkdir(f.api.context,"/created"));
    else assert(!f.ext.rename(f.api.context,"/docs","/moved"));
    assert(b.count(mkdir?"mkdir":"rename")==1);
    if(fault==Backend::Fault::MkdirAfter)assert(b.nodes.at("/user/created").directory);
    if(fault==Backend::Fault::RenameAfter)assert(b.nodes.at("/user/moved/book.txt").data=="chapter one");
    assertExtendedRetained(f);
  }
  for(auto fault:{Backend::Fault::DirCloseBefore,Backend::Fault::DirCloseAfter}){
    ExtendedFixture f;auto dir=f.api.dir_open(f.api.context,"/");assert(dir);f.backend.fault=fault;
    assert(!f.ext.dir_close_checked(f.api.context,dir));assert(f.backend.count("dir_close")==1);assertExtendedRetained(f);
  }
  // Another writer wins after the destination absence check. Never overwrite,
  // delete the winner or retry the inconclusive rename.
  ExtendedFixture race;auto& b=race.backend;
  b.reenter=[&]{if(b.calls.back().operation=="rename")b.nodes.emplace("/user/race",Backend::Node{false,"winner"});};
  assert(!race.ext.rename(race.api.context,"/hello.txt","/race"));
  assert(b.nodes.at("/user/race").data=="winner" && b.nodes.at("/user/hello.txt").data=="hello");
  b.reenter={};assertExtendedRetained(race);
}

void extendedFencesAndSnapshots(){
  for(bool mkdir:{false,true})for(unsigned cut=0;cut<(mkdir?2u:3u);++cut){
    ExtendedFixture f;auto& b=f.backend;const size_t before=b.calls.size();
    b.reenter=[&]{if(b.calls.size()==before+cut+1)b.safe=false;};
    if(mkdir)assert(!f.ext.mkdir(f.api.context,"/created"));
    else assert(!f.ext.rename(f.api.context,"/hello.txt","/renamed"));
    assert(b.calls.size()==before+cut+1);b.reenter={};b.safe=true;assertExtendedRetained(f);
  }
  ExtendedFixture lost;auto dir=lost.api.dir_open(lost.api.context,"/");assert(dir);
  const size_t before=lost.backend.calls.size();lost.backend.safe=false;
  assert(lost.ext.handle_error(lost.api.context,dir,true));assert(lost.backend.calls.size()==before);
  lost.backend.safe=true;assertExtendedRetained(lost);
  ExtendedFixture f;auto& b=f.backend;char source[]="/hello.txt",destination[]="/renamed.txt",folder[]="/new-folder";
  size_t attempts=0;
  b.reenter=[&]{++attempts;const size_t calls=b.calls.size();
    assert(!f.ext.mkdir(f.api.context,"/reentrant"));assert(!f.ext.rename(f.api.context,"/docs","/reentrant"));
    assert(!f.ext.dir_close_checked(f.api.context,1));assert(f.ext.handle_error(f.api.context,1,true));
    risc_storage_volume_api_v1_ext nested{};assert(!f.volume.beginExtended(&nested));
    assert(b.calls.size()==calls);std::memset(source+1,'x',sizeof(source)-2);
    std::memset(destination+1,'x',sizeof(destination)-2);std::memset(folder+1,'x',sizeof(folder)-2);
  };
  assert(f.ext.rename(f.api.context,source,destination));assert(b.nodes.at("/user/renamed.txt").data=="hello");
  std::strcpy(folder,"/new-folder");assert(f.ext.mkdir(f.api.context,folder));assert(b.nodes.at("/user/new-folder").directory);
  assert(attempts==5);b.reenter={};f.endClean();
}

void dualHandleAdmissionAndCopy(){
  for(bool writerFirst:{false,true}){
    ExtendedFixture f;auto&a=f.api;auto&b=f.backend;uint64_t size=0;uint32_t reader=0,writer=0;
    b.nodes.at("/user/hello.txt").data=std::string(1500,'q');
    if(writerFirst)writer=a.file_open_write(a.context,"/docs/copy.txt");
    reader=a.file_open_read(a.context,"/hello.txt",&size);assert(reader && size==1500);
    if(!writerFirst)writer=a.file_open_write(a.context,"/docs/copy.txt");
    assert(writer && writer!=reader && b.files.size()==2);
    const size_t before=b.calls.size();
    assert(!a.file_open_read(a.context,"/docs/book.txt",&size));
    assert(!a.file_open_write(a.context,"/other"));
    assert(!a.refresh(a.context) && !a.remove(a.context,"/docs/book.txt"));
    assert(!f.ext.rename(a.context,"/docs","/moved") && !f.ext.mkdir(a.context,"/new"));
    char bytes[1024]{};assert(!a.file_read(a.context,writer,bytes,sizeof(bytes)));
    assert(!a.file_write(a.context,reader,"bad",3));assert(b.calls.size()==before);
    size_t copied=0;while(copied<1500){size_t got=a.file_read(a.context,reader,bytes,sizeof(bytes));assert(got && got<=512);
      assert(a.file_write(a.context,writer,bytes,got)==got);copied+=got;}
    assert(!f.ext.handle_error(a.context,reader,false) && !f.ext.handle_error(a.context,writer,false));
    if(writerFirst){
      assert(a.file_close(a.context,writer,true));assert(b.files.size()==1);
      assert(!a.file_read(a.context,reader,bytes,sizeof(bytes)) && !f.ext.handle_error(a.context,reader,false));
      assert(a.file_close(a.context,reader,true));
    }else{
      assert(a.file_close(a.context,reader,true));assert(b.files.size()==1);
      assert(a.file_close(a.context,writer,true));
    }
    assert(b.nodes.at("/user/docs/copy.txt").data==b.nodes.at("/user/hello.txt").data && b.stages().empty());
    const size_t stopped=b.calls.size();assert(!a.file_close(a.context,reader,false));assert(!a.file_close(a.context,writer,false));
    assert(b.calls.size()==stopped);f.endClean();
  }
}

void dualHandleFailureIsolation(){
  for(bool readFails:{false,true}){
    ExtendedFixture f;auto&a=f.api;uint64_t size=0;char bytes[8]{};
    auto reader=a.file_open_read(a.context,"/hello.txt",&size);auto writer=a.file_open_write(a.context,"/copy");assert(reader && writer);
    f.backend.fault=readFails?Backend::Fault::ReadError:Backend::Fault::WriteError;
    if(readFails){assert(!a.file_read(a.context,reader,bytes,sizeof(bytes)));assert(a.file_write(a.context,writer,"okay",4)==4);}
    else {assert(!a.file_write(a.context,writer,"bad",3));assert(a.file_read(a.context,reader,bytes,sizeof(bytes))==5);}
    assert(bool(f.ext.handle_error(a.context,reader,false))==readFails);
    assert(bool(f.ext.handle_error(a.context,writer,false))!=readFails);
    assert(a.file_close(a.context,reader,true));assert(a.file_close(a.context,writer,false));
    assert(f.backend.stages().empty() && !f.backend.nodes.count("/user/copy"));f.endClean();
  }
  ExtendedFixture collision;auto&a=collision.api;uint64_t size=0;
  auto reader=a.file_open_read(a.context,"/hello.txt",&size);assert(reader);
  assert(!a.file_open_write(a.context,"/hello.txt"));
  assert(!collision.ext.handle_error(a.context,reader,false));
  assert(a.file_close(a.context,reader,true));collision.endClean();
}

void dualHandleOrderedCleanup(){
  for(auto fault:{Backend::Fault::None,Backend::Fault::CloseBefore,Backend::Fault::CloseAfter,
      Backend::Fault::AbortBefore,Backend::Fault::AbortAfter,Backend::Fault::DirCloseBefore,Backend::Fault::DirCloseAfter}){
    ExtendedFixture f;auto&a=f.api;auto&b=f.backend;uint64_t size=0;
    auto reader=a.file_open_read(a.context,"/hello.txt",&size);auto writer=a.file_open_write(a.context,"/unfinished");
    assert(reader && writer && a.dir_open(a.context,"/"));b.fault=fault;
    const size_t start=b.calls.size();const bool ended=f.volume.end();
    assert(ended==(fault==Backend::Fault::None));
    assert(b.calls[start].operation=="file_close" && b.calls[start].commit);
    if(fault==Backend::Fault::CloseBefore || fault==Backend::Fault::CloseAfter){
      assert(b.count("file_close")==1 && !b.count("dir_close"));assert(b.stages().size()==1);
    }else{
      assert(b.count("file_close")==2 && b.calls[start+1].operation=="file_close" && !b.calls[start+1].commit);
      if(fault==Backend::Fault::AbortBefore || fault==Backend::Fault::AbortAfter)assert(!b.count("dir_close"));
      else assert(b.count("dir_close")==1);
    }
    if(!ended)assertExtendedRetained(f);else assert(b.files.empty() && b.directories.empty() && b.stages().empty());
  }
}

void dualHandleCustodyAndLifetime(){
  for(const char*operation:{"open_read","open_write","file_info","read","file_error","write","file_close"}){
    ExtendedFixture f;auto&a=f.api;auto&b=f.backend;uint64_t size=0;uint32_t reader=0,writer=0;
    const std::string op=operation;
    if(op!="open_read")reader=a.file_open_read(a.context,"/hello.txt",&size);
    if(op!="open_write")writer=a.file_open_write(a.context,"/pending");
    b.revokeAfter=op;char out[8];memset(out,42,sizeof(out));
    if(op=="open_read")assert(!a.file_open_read(a.context,"/hello.txt",&size));
    else if(op=="open_write")assert(!a.file_open_write(a.context,"/pending"));
    else if(op=="write")assert(!a.file_write(a.context,writer,"x",1));
    else if(op=="file_close")assert(!f.volume.end());
    else {assert(!a.file_read(a.context,reader,out,sizeof(out)));for(char c:out)assert(c==42);}
    assert(b.calls.back().operation==op);b.revokeAfter.clear();b.safe=true;assertExtendedRetained(f);
  }
  ExtendedFixture f;uint64_t size=0;auto old=f.api;
  const auto reader=old.file_open_read(old.context,"/hello.txt",&size),writer=old.file_open_write(old.context,"/pending");
  assert(reader && writer);f.endClean();assert(f.volume.beginExtended(&f.ext));f.api=f.ext.base;
  const auto nextReader=f.api.file_open_read(f.api.context,"/hello.txt",&size),nextWriter=f.api.file_open_write(f.api.context,"/next");
  assert(nextReader && nextWriter && nextReader!=reader && nextReader!=writer && nextWriter!=reader && nextWriter!=writer);
  const size_t before=f.backend.calls.size();char out[8]{};
  assert(!f.api.file_read(f.api.context,reader,out,sizeof(out)));assert(!f.api.file_write(f.api.context,writer,"x",1));
  assert(!old.file_close(old.context,nextReader,true));assert(!old.file_close(old.context,nextWriter,false));
  assert(f.backend.calls.size()==before);f.endClean();
}
} // namespace

int main() {
  configuration();ordinary();pathsAndEntries();lifetimeAndIsolation();simultaneousInstanceHandles();
  endRollback();writerErrors();readIntegrity();custodyFailures();exactCleanupCustody();fences();
  safetyLossAtEveryCallback();stagingCollision();unavailableAndSlots();invalidArgumentsAndStaleObject();directoryValidationAndBounds();
  reentrantAdmissionAndSnapshots();cleanEofDiagnostics();
  extendedAdmissionAndLifetime();extendedManagement();extendedInvalidPathsAndBusy();extendedHandleDiagnostics();
  extendedUncertainMutations();extendedFencesAndSnapshots();
  dualHandleAdmissionAndCopy();dualHandleFailureIsolation();dualHandleOrderedCleanup();dualHandleCustodyAndLifetime();
  std::puts("Scoped user volume: 28 test groups, confinement, bounded dual-handle I/O, exclusive staging, extended management, lifetime isolation, owner fences and no-retry retained custody PASS");
  return 0;
}
