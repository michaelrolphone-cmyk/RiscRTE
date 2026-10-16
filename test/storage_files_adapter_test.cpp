#include "bootstrap/StorageFilesVolumeAdapter.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace {
const char* activeTest = "startup";
unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
  std::fprintf(stderr, "%s:%d [%s]: %s\n", __FILE__, __LINE__, activeTest, #condition); \
  std::exit(1); } } while (0)

using Adapter = RiscBoot::StorageFilesVolumeAdapter;
using Token = risc_storage_files_handle_v1;
static_assert(RISC_STORAGE_FILES_IO_MAX == RISC_STORAGE_VOLUME_IO_MAX, "I/O bound");
static_assert(RISC_STORAGE_FILES_NAME_MAX == RISC_STORAGE_VOLUME_NAME_MAX, "name bound");
static_assert(unsigned(RISC_FILES_READ) == unsigned(RISC_STORAGE_OPEN_READ), "read flag");
static_assert(unsigned(RISC_FILES_APPEND) == unsigned(RISC_STORAGE_OPEN_APPEND), "append flag");

// Stateful native backend: the production adapter is included without shims or
// replacement logic. Every callback, including the pure observer, is counted.
struct Backend {
  struct Node { bool directory = false; std::string data; uint64_t modified = 42; };
  struct Handle { std::string path; uint32_t flags; bool directory; uint64_t position = 0; };
  risc_storage_volume_api_v1_fs api{};
  std::map<std::string, Node> nodes;
  std::map<uint32_t, Handle> handles;
  std::map<std::string, unsigned> calls;
  uint32_t nextNative = 100;
  int32_t state = RISC_STORAGE_STATE_READY;
  bool ready = true;
  bool hiddenOpen = false;
  bool shortWrite = false;
  bool excessiveTransfer = false;
  bool malformedName = false;
  bool malformedMetadata = false;
  bool errorOnHandle = false;
  std::string fail;
  std::string transition;
  int32_t transitionState = RISC_STORAGE_STATE_RETAINED;
  int32_t metadataOverride = 999;

  risc_storage_volume_api_v1_ext& ext() {
    return *reinterpret_cast<risc_storage_volume_api_v1_ext*>(&api);
  }
  risc_storage_volume_api_v1* base() { return &ext().base; }
  static Backend& from(void* context) { return *static_cast<Backend*>(context); }
  bool call(const char* operation) {
    ++calls[operation];
    if (transition == operation) state = transitionState;
    return fail != operation;
  }
  unsigned totalCalls() const {
    unsigned total = 0;
    for (const auto& item : calls) total += item.second;
    return total;
  }
  unsigned count(const char* operation) const {
    auto it = calls.find(operation);
    return it == calls.end() ? 0 : it->second;
  }
  Handle& handle(uint32_t id, bool directory) {
    auto it = handles.find(id);
    CHECK(it != handles.end());
    CHECK(it->second.directory == directory);
    return it->second;
  }
  std::vector<std::string> children(const std::string& directory) const {
    const std::string prefix = directory == "/" ? "/" : directory + "/";
    std::vector<std::string> result;
    for (const auto& item : nodes) {
      if (item.first.compare(0, prefix.size(), prefix) != 0) continue;
      const std::string name = item.first.substr(prefix.size());
      if (!name.empty() && name.find('/') == std::string::npos) result.push_back(name);
    }
    return result;
  }
  bool hasParent(const std::string& path) const {
    const size_t slash = path.find_last_of('/');
    const std::string parent = slash == 0 ? "/" : path.substr(0, slash);
    auto it = nodes.find(parent);
    return it != nodes.end() && it->second.directory;
  }
  Backend() {
    nodes["/"] = {true, "", 1};
    nodes["/dir"] = {true, "", 7};
    nodes["/file"] = {false, "abcdef", 42};
    nodes["/dir/a"] = {false, "abc", 10};
    nodes["/dir/sub"] = {true, "", 11};
    auto& v = *base();
    auto& e = ext();
    v.api_version = RISC_STORAGE_VOLUME_API_V1;
    v.struct_size = sizeof(api);
    v.context = this;
    api.state.state_tag = RISC_STORAGE_STATE_TAG;
    api.state.state_version = 1;
    api.state.observe = [](void* c) -> int32_t {
      auto& b = from(c); b.call("observe"); return b.state;
    };
    api.fs_tag = RISC_STORAGE_FS_TAG;
    api.fs_version = 1;
    v.ready = [](void* c) { auto& b = from(c); b.call("ready"); return b.ready; };
    // Legacy bool stat is deliberately poisonous: CRUD must use metadata.
    v.stat = [](void*, const char*, uint64_t*, bool*) -> bool {
      CHECK(false); return false;
    };
    api.metadata = [](void* c, const char* path, risc_storage_metadata_v1* out) -> int32_t {
      auto& b = from(c);
      CHECK(out && out->struct_size == sizeof(*out));
      if (!b.call("metadata")) return RISC_STORAGE_FS_IO;
      if (b.metadataOverride != 999) return b.metadataOverride;
      auto it = b.nodes.find(path);
      if (it == b.nodes.end()) return RISC_STORAGE_FS_NOT_FOUND;
      *out = {sizeof(*out), uint32_t(RISC_STORAGE_MODIFIED_VALID | RISC_STORAGE_MODIFIED_LOCAL),
              uint64_t(it->second.data.size()), it->second.modified};
      if (it->second.directory) out->flags |= RISC_STORAGE_DIRECTORY;
      if (b.malformedMetadata) out->struct_size = 0;
      return RISC_STORAGE_FS_OK;
    };
    e.file_open = [](void* c, const char* path, uint32_t flags) -> uint32_t {
      auto& b = from(c);
      if (!b.call("file_open")) {
        if (b.hiddenOpen) b.handles[++b.nextNative] = {path, flags, false, 0};
        return 0;
      }
      auto it = b.nodes.find(path);
      if (it == b.nodes.end()) {
        if (!(flags & RISC_STORAGE_OPEN_CREATE) || !b.hasParent(path)) return 0;
        it = b.nodes.emplace(path, Node{}).first;
      } else if ((flags & (RISC_STORAGE_OPEN_CREATE | RISC_STORAGE_OPEN_EXCLUSIVE)) ==
                 (RISC_STORAGE_OPEN_CREATE | RISC_STORAGE_OPEN_EXCLUSIVE)) return 0;
      if (it->second.directory) return 0;
      if (flags & RISC_STORAGE_OPEN_TRUNCATE) it->second.data.clear();
      const uint32_t native = ++b.nextNative;
      b.handles[native] = {path, flags, false, 0};
      return native;
    };
    v.file_read = [](void* c, uint32_t id, void* bytes, size_t capacity) -> size_t {
      auto& b = from(c); auto& h = b.handle(id, false);
      if (!b.call("file_read")) return 0;
      if (b.excessiveTransfer) return capacity + 1;
      const std::string& data = b.nodes.at(h.path).data;
      const size_t position = std::min<size_t>(h.position, data.size());
      const size_t count = std::min(capacity, data.size() - position);
      if (count) std::memcpy(bytes, data.data() + position, count);
      h.position += count;
      return count;
    };
    v.file_write = [](void* c, uint32_t id, const void* bytes, size_t size) -> size_t {
      auto& b = from(c); auto& h = b.handle(id, false);
      if (!b.call("file_write")) return 0;
      if (b.excessiveTransfer) return size + 1;
      std::string& data = b.nodes.at(h.path).data;
      if (h.flags & RISC_STORAGE_OPEN_APPEND) h.position = data.size();
      const size_t count = b.shortWrite && size ? size - 1 : size;
      if (count) {
        if (h.position + count > data.size()) data.resize(h.position + count);
        std::memcpy(&data[size_t(h.position)], bytes, count);
      }
      h.position += count;
      return count;
    };
    e.file_seek = [](void* c, uint32_t id, uint64_t offset) {
      auto& b = from(c); auto& h = b.handle(id, false);
      if (!b.call("file_seek") || offset > b.nodes.at(h.path).data.size()) return false;
      h.position = offset; return true;
    };
    e.file_info = [](void* c, uint32_t id, uint64_t* size, uint64_t* offset) {
      auto& b = from(c); auto& h = b.handle(id, false);
      if (!b.call("file_info")) return false;
      *size = b.nodes.at(h.path).data.size(); *offset = h.position; return true;
    };
    e.file_sync = [](void* c, uint32_t id) {
      auto& b = from(c); b.handle(id, false); return b.call("file_sync");
    };
    api.file_truncate = [](void* c, uint32_t id, uint64_t size) {
      auto& b = from(c); auto& h = b.handle(id, false);
      if (!b.call("file_truncate") || size > b.nodes.at(h.path).data.size()) return false;
      b.nodes.at(h.path).data.resize(size);
      h.position = std::min(h.position, size); return true;
    };
    v.file_close = [](void* c, uint32_t id, bool commit) {
      auto& b = from(c); b.handle(id, false); CHECK(commit);
      if (!b.call("file_close")) return false;
      b.handles.erase(id); return true;
    };
    e.mkdir = [](void* c, const char* path) {
      auto& b = from(c);
      if (!b.call("mkdir") || b.nodes.count(path) || !b.hasParent(path)) return false;
      b.nodes[path] = {true, "", 55}; return true;
    };
    e.rename = [](void* c, const char* source, const char* destination) {
      auto& b = from(c);
      if (!b.call("rename") || b.nodes.count(destination) || !b.nodes.count(source) ||
          !b.hasParent(destination)) return false;
      b.nodes[destination] = b.nodes.at(source); b.nodes.erase(source); return true;
    };
    v.remove = [](void* c, const char* path) {
      auto& b = from(c);
      if (!b.call("remove")) return false;
      auto it = b.nodes.find(path);
      if (it == b.nodes.end() || (it->second.directory && !b.children(path).empty())) return false;
      b.nodes.erase(it); return true;
    };
    v.dir_open = [](void* c, const char* path) -> uint32_t {
      auto& b = from(c);
      if (!b.call("dir_open")) {
        if (b.hiddenOpen) b.handles[++b.nextNative] = {path, 0, true, 0};
        return 0;
      }
      auto it = b.nodes.find(path);
      if (it == b.nodes.end() || !it->second.directory) return 0;
      const uint32_t native = ++b.nextNative;
      b.handles[native] = {path, 0, true, 0}; return native;
    };
    v.dir_next = [](void* c, uint32_t id, risc_storage_dirent_v1* out) {
      auto& b = from(c); auto& h = b.handle(id, true);
      if (!b.call("dir_next")) return false;
      const auto names = b.children(h.path);
      if (h.position >= names.size()) return false;
      const std::string name = names[size_t(h.position++)];
      const auto& node = b.nodes.at(h.path == "/" ? "/" + name : h.path + "/" + name);
      *out = {};
      std::memcpy(out->name, name.c_str(), name.size() + 1);
      out->size = node.data.size(); out->is_directory = node.directory;
      if (b.malformedName) std::memset(out->name, 'x', sizeof(out->name));
      return true;
    };
    e.dir_rewind = [](void* c, uint32_t id) {
      auto& b = from(c); auto& h = b.handle(id, true);
      if (!b.call("dir_rewind")) return false;
      h.position = 0; return true;
    };
    api.dir_tell = [](void* c, uint32_t id, uint64_t* out) {
      auto& b = from(c); auto& h = b.handle(id, true);
      if (!b.call("dir_tell")) return false;
      *out = h.position; return true;
    };
    api.dir_seek = [](void* c, uint32_t id, uint64_t offset) {
      auto& b = from(c); auto& h = b.handle(id, true);
      if (!b.call("dir_seek") || offset > b.children(h.path).size()) return false;
      h.position = offset; return true;
    };
    e.dir_close_checked = [](void* c, uint32_t id) {
      auto& b = from(c); b.handle(id, true);
      if (!b.call("dir_close")) return false;
      b.handles.erase(id); return true;
    };
    e.handle_error = [](void* c, uint32_t id, bool directory) -> uint32_t {
      auto& b = from(c); b.handle(id, directory); b.call("handle_error");
      return b.errorOnHandle ? 1 : 0;
    };
    api.replace_file = [](void*, const char*, const char*) { CHECK(false); return false; };
    api.recover_replace = [](void*, const char*) { CHECK(false); return false; };
  }
};

struct Fixture {
  Backend backend;
  Adapter adapter;
  Fixture() { CHECK(adapter.bind(backend.base())); }
  Token open(uint32_t flags = RISC_FILES_READ, const char* path = "/file") {
    Token token = 0;
    CHECK(adapter.open(path, flags, &token) == RISC_FILES_OK);
    CHECK(token != 0); return token;
  }
  Token directory(const char* path = "/dir") {
    Token token = 0;
    CHECK(adapter.dirOpen(path, &token) == RISC_FILES_OK);
    CHECK(token != 0); return token;
  }
};
risc_storage_files_stat_v1 statBuffer() { return {sizeof(risc_storage_files_stat_v1), 0, 0, 0}; }
risc_storage_files_entry_v1 entryBuffer() { risc_storage_files_entry_v1 entry{}; entry.struct_size = sizeof(entry); return entry; }

void testBinding() {
  activeTest = "binding and required metadata/state suffix";
  Backend b;
  Adapter unbound;
  CHECK(!unbound.bound() && !unbound.retained() && unbound.exitSafe());
  Token token = 99;
  auto st = statBuffer();
  CHECK(unbound.open("/file", RISC_FILES_READ, &token) == RISC_FILES_UNAVAILABLE && token == 0);
  CHECK(unbound.stat("/file", &st) == RISC_FILES_UNAVAILABLE);
  CHECK(unbound.mkdir("/new") == RISC_FILES_UNAVAILABLE);
  CHECK(!unbound.bind(nullptr));
  CHECK(unbound.bind(b.base()));
  CHECK(unbound.bound() && unbound.exitSafe());
  CHECK(!unbound.bind(b.base()));
  CHECK(b.totalCalls() == 0);

  const uint32_t sizes[] = {sizeof(risc_storage_volume_api_v1), sizeof(risc_storage_volume_api_v1_ext),
      sizeof(risc_storage_volume_api_v1_state), sizeof(risc_storage_volume_api_v1_fs) - 1};
  for (uint32_t size : sizes) {
    Backend legacy; legacy.base()->struct_size = size; Adapter a;
    CHECK(!a.bind(legacy.base()) && !a.bound());
  }
  for (unsigned which = 0; which < 26; ++which) {
    Backend broken; Adapter a;
    switch (which) {
      case 0: broken.base()->api_version = 2; break;
      case 1: broken.api.state.state_tag = 0; break;
      case 2: broken.api.state.state_version = 2; break;
      case 3: broken.api.state.observe = nullptr; break;
      case 4: broken.api.fs_tag = 0; break;
      case 5: broken.api.fs_version = 2; break;
      case 6: broken.api.metadata = nullptr; break;
      case 7: broken.api.file_truncate = nullptr; break;
      case 8: broken.api.dir_tell = nullptr; break;
      case 9: broken.api.dir_seek = nullptr; break;
      case 10: broken.api.replace_file = nullptr; break;
      case 11: broken.api.recover_replace = nullptr; break;
      case 12: broken.base()->ready = nullptr; break;
      case 13: broken.base()->dir_open = nullptr; break;
      case 14: broken.base()->dir_next = nullptr; break;
      case 15: broken.base()->file_read = nullptr; break;
      case 16: broken.base()->file_write = nullptr; break;
      case 17: broken.base()->file_close = nullptr; break;
      case 18: broken.base()->remove = nullptr; break;
      case 19: broken.ext().file_open = nullptr; break;
      case 20: broken.ext().file_seek = nullptr; break;
      case 21: broken.ext().file_info = nullptr; break;
      case 22: broken.ext().file_sync = nullptr; break;
      case 23: broken.ext().dir_rewind = nullptr; break;
      case 24: broken.ext().dir_close_checked = nullptr; break;
      case 25: broken.ext().handle_error = nullptr; break;
    }
    CHECK(!a.bind(broken.base())); CHECK(broken.totalCalls() == 0);
  }
  for (bool rename : {false, true}) {
    Backend broken; Adapter a;
    if (rename) broken.ext().rename = nullptr; else broken.ext().mkdir = nullptr;
    CHECK(!a.bind(broken.base()));
  }
}

void testFileCrud() {
  activeTest = "file CRUD, byte offsets, sync, truncate and append";
  Fixture f; auto& a = f.adapter; auto& b = f.backend;
  CHECK(a.exitSafe());
  CHECK(a.mkdir("/new") == RISC_FILES_OK);
  CHECK(a.mkdir("/new") == RISC_FILES_EXISTS);
  CHECK(a.mkdir("/file") == RISC_FILES_EXISTS);
  auto st = statBuffer();
  CHECK(a.stat("/new", &st) == RISC_FILES_OK && (st.flags & RISC_FILES_DIRECTORY));
  const Token token = f.open(RISC_FILES_READ | RISC_FILES_WRITE | RISC_FILES_CREATE | RISC_FILES_EXCLUSIVE, "/new/data");
  CHECK(!a.exitSafe());
  CHECK(token != b.handles.begin()->first); // App token is not a native handle.
  uint32_t count = 99; uint64_t size = 99, offset = 99;
  CHECK(a.write(token, "abcdef", 6, &count) == RISC_FILES_OK && count == 6);
  CHECK(a.info(token, &size, &offset) == RISC_FILES_OK && size == 6 && offset == 6);
  CHECK(a.seek(token, 2) == RISC_FILES_OK);
  CHECK(a.write(token, "XY", 2, &count) == RISC_FILES_OK && count == 2);
  CHECK(a.seek(token, 0) == RISC_FILES_OK);
  char bytes[16]{};
  CHECK(a.read(token, bytes, sizeof(bytes), &count) == RISC_FILES_OK && count == 6);
  CHECK(std::string(bytes, count) == "abXYef");
  CHECK(a.read(token, bytes, sizeof(bytes), &count) == RISC_FILES_OK && count == 0);
  CHECK(a.read(token, nullptr, 0, &count) == RISC_FILES_OK && count == 0);
  CHECK(a.write(token, nullptr, 0, &count) == RISC_FILES_OK && count == 0);
  CHECK(a.seek(token, 7) == RISC_FILES_IO);
  CHECK(a.seek(token, std::numeric_limits<uint64_t>::max()) == RISC_FILES_IO);
  CHECK(a.truncate(token, 7) == RISC_FILES_IO && b.nodes.at("/new/data").data.size() == 6);
  CHECK(a.truncate(token, 3) == RISC_FILES_OK);
  CHECK(a.info(token, &size, &offset) == RISC_FILES_OK && size == 3 && offset == 3);
  CHECK(a.sync(token) == RISC_FILES_OK && b.count("file_sync") == 1);
  CHECK(a.close(token) == RISC_FILES_OK && a.exitSafe() && b.handles.empty());
  CHECK(a.close(token) == RISC_FILES_INVALID);
  CHECK(a.stat("/new/data", &st) == RISC_FILES_OK && st.size == 3);
  CHECK((st.flags & RISC_FILES_MODIFIED_VALID) && (st.flags & RISC_FILES_MODIFIED_LOCAL));
  CHECK(st.modified_seconds == 42 && st.struct_size == sizeof(st));
  CHECK(a.rename("/new/data", "/new/moved") == RISC_FILES_OK);
  CHECK(a.stat("/new/data", &st) == RISC_FILES_NOT_FOUND);
  CHECK(a.stat("/new/moved", &st) == RISC_FILES_OK && st.size == 3);
  CHECK(a.rename("/new/moved", "/file") == RISC_FILES_EXISTS);
  CHECK(a.rename("/new/missing", "/new/absent") == RISC_FILES_NOT_FOUND);
  CHECK(a.remove("/new") == RISC_FILES_IO); // Nonempty directory.
  CHECK(a.remove("/new/moved") == RISC_FILES_OK);
  CHECK(a.remove("/new/moved") == RISC_FILES_NOT_FOUND);
  CHECK(a.remove("/new") == RISC_FILES_OK);
  CHECK(a.remove("/") == RISC_FILES_INVALID);

  Token append = f.open(RISC_FILES_READ | RISC_FILES_WRITE | RISC_FILES_APPEND);
  CHECK(a.seek(append, 0) == RISC_FILES_OK);
  CHECK(a.write(append, "1", 1, &count) == RISC_FILES_OK);
  CHECK(a.seek(append, 1) == RISC_FILES_OK);
  CHECK(a.write(append, "2", 1, &count) == RISC_FILES_OK);
  CHECK(b.nodes.at("/file").data == "abcdef12");
  CHECK(a.close(append) == RISC_FILES_OK);
  Token truncate = f.open(RISC_FILES_WRITE | RISC_FILES_TRUNCATE);
  CHECK(a.info(truncate, &size, &offset) == RISC_FILES_OK && size == 0 && offset == 0);
  CHECK(a.close(truncate) == RISC_FILES_OK);
  Token exclusive = 99;
  CHECK(a.open("/file", RISC_FILES_WRITE | RISC_FILES_CREATE | RISC_FILES_EXCLUSIVE, &exclusive) == RISC_FILES_EXISTS);
  CHECK(exclusive == 0);
  CHECK(a.open("/dir", RISC_FILES_READ, &exclusive) == RISC_FILES_INVALID);
  CHECK(a.exitSafe());
}

void testDirectories() {
  activeTest = "directories, EOF, rewind, native cursors and checked close";
  Fixture f; auto& a = f.adapter;
  Token token = f.directory();
  uint64_t cursor = 99; auto entry = entryBuffer();
  CHECK(a.dirTell(token, &cursor) == RISC_FILES_OK && cursor == 0);
  CHECK(a.dirNext(token, &entry) == RISC_FILES_OK);
  CHECK(std::string(entry.name) == "a" && entry.size == 3 && entry.flags == 0);
  CHECK(entry.struct_size == sizeof(entry));
  CHECK(a.dirTell(token, &cursor) == RISC_FILES_OK && cursor == 1);
  CHECK(a.dirNext(token, &entry) == RISC_FILES_OK);
  CHECK(std::string(entry.name) == "sub" && (entry.flags & RISC_FILES_DIRECTORY));
  CHECK(a.dirNext(token, &entry) == RISC_FILES_END);
  CHECK(a.dirNext(token, &entry) == RISC_FILES_END);
  CHECK(a.dirSeek(token, cursor) == RISC_FILES_OK);
  CHECK(a.dirNext(token, &entry) == RISC_FILES_OK && std::string(entry.name) == "sub");
  CHECK(a.dirSeek(token, 999) == RISC_FILES_IO);
  CHECK(a.dirSeek(token, std::numeric_limits<uint64_t>::max()) == RISC_FILES_IO);
  CHECK(a.dirRewind(token) == RISC_FILES_OK);
  CHECK(a.dirNext(token, &entry) == RISC_FILES_OK && std::string(entry.name) == "a");
  CHECK(a.dirClose(token) == RISC_FILES_OK && a.exitSafe());
  CHECK(a.dirClose(token) == RISC_FILES_INVALID);
  Token fresh = f.directory(); CHECK(fresh != token);
  CHECK(a.dirNext(token, &entry) == RISC_FILES_INVALID);
  CHECK(a.dirClose(fresh) == RISC_FILES_OK);
  Token missing = 99;
  CHECK(a.dirOpen("/missing", &missing) == RISC_FILES_NOT_FOUND && missing == 0);
  CHECK(a.dirOpen("/file", &missing) == RISC_FILES_INVALID && missing == 0);
  CHECK(a.mkdir("/empty") == RISC_FILES_OK);
  Token empty = f.directory("/empty");
  CHECK(a.dirNext(empty, &entry) == RISC_FILES_END);
  CHECK(a.dirClose(empty) == RISC_FILES_OK);
}

void testValidationAndAccess() {
  activeTest = "invalid paths, flags, arguments and cross-kind/stale tokens";
  Fixture f; auto& a = f.adapter; auto& b = f.backend;
  const std::vector<std::string> invalid = {"", "relative", "//", "/a/", "/a//b", "/.", "/..", "/a/./b",
    "/a/../b", "/a\\b", std::string("/a\x01"), std::string("/a\x7f"), "/\x80", "/\xc0\xaf", "/\xc2", "/\xc2x", "/\xe0\x80\xaf", "/\xed\xa0\x80", "/\xf0\x80\x80\xaf", "/\xf4\x90\x80\x80", "/\xf5\x80\x80\x80", "/\xff", std::string(512, 'a')};
  for (const auto& path : invalid) {
    const unsigned before = b.totalCalls(); Token token = 99; auto st = statBuffer();
    CHECK(a.open(path.c_str(), RISC_FILES_READ, &token) == RISC_FILES_INVALID && token == 0);
    CHECK(a.stat(path.c_str(), &st) == RISC_FILES_INVALID);
    CHECK(a.mkdir(path.c_str()) == RISC_FILES_INVALID);
    CHECK(a.remove(path.c_str()) == RISC_FILES_INVALID);
    CHECK(a.rename(path.c_str(), "/valid") == RISC_FILES_INVALID);
    CHECK(a.rename("/valid", path.c_str()) == RISC_FILES_INVALID);
    CHECK(a.dirOpen(path.c_str(), &token) == RISC_FILES_INVALID && token == 0);
    CHECK(b.totalCalls() == before);
  }
  Token out = 99; auto st = statBuffer();
  CHECK(a.open(nullptr, RISC_FILES_READ, &out) == RISC_FILES_INVALID && out == 0);
  CHECK(a.open("/file", RISC_FILES_READ, nullptr) == RISC_FILES_INVALID);
  CHECK(a.stat(nullptr, &st) == RISC_FILES_INVALID);
  CHECK(a.stat("/file", nullptr) == RISC_FILES_INVALID);
  st.struct_size = sizeof(st) - 1;
  CHECK(a.stat("/file", &st) == RISC_FILES_INVALID);
  CHECK(a.dirOpen(nullptr, &out) == RISC_FILES_INVALID);
  CHECK(a.dirOpen("/dir", nullptr) == RISC_FILES_INVALID);
  const uint32_t flags[] = {0, 64, RISC_FILES_CREATE, RISC_FILES_READ | RISC_FILES_CREATE,
    RISC_FILES_READ | RISC_FILES_TRUNCATE, RISC_FILES_READ | RISC_FILES_APPEND,
    RISC_FILES_READ | RISC_FILES_EXCLUSIVE, RISC_FILES_WRITE | RISC_FILES_EXCLUSIVE,
    std::numeric_limits<uint32_t>::max()};
  for (uint32_t value : flags) {
    const unsigned before = b.totalCalls(); out = 99;
    CHECK(a.open("/file", value, &out) == RISC_FILES_INVALID && out == 0);
    CHECK(b.totalCalls() == before);
  }
  const std::string longest = "/" + std::string(RISC_STORAGE_FILES_PATH_MAX - 2, 'x');
  st = statBuffer();
  CHECK(a.stat(longest.c_str(), &st) == RISC_FILES_NOT_FOUND);
  const std::string oversized = longest + "x";
  CHECK(a.stat(oversized.c_str(), &st) == RISC_FILES_INVALID);
  CHECK(a.stat("/caf\xc3\xa9", &st) == RISC_FILES_NOT_FOUND);
  CHECK(a.stat("/\xe2\x82\xac", &st) == RISC_FILES_NOT_FOUND);
  CHECK(a.stat("/\xf0\x9f\x93\x81", &st) == RISC_FILES_NOT_FOUND);
  CHECK(a.stat("/\xf4\x8f\xbf\xbf", &st) == RISC_FILES_NOT_FOUND);

  Token readOnly = f.open(); Token writeOnly = f.open(RISC_FILES_WRITE); Token directory = f.directory();
  char bytes[8]{}; uint32_t count = 99; uint64_t size = 0, offset = 0; auto entry = entryBuffer();
  unsigned before = b.totalCalls();
  CHECK(a.write(readOnly, "x", 1, &count) == RISC_FILES_DENIED && count == 0);
  CHECK(a.truncate(readOnly, 0) == RISC_FILES_DENIED);
  CHECK(a.read(writeOnly, bytes, sizeof(bytes), &count) == RISC_FILES_DENIED && count == 0);
  CHECK(b.totalCalls() == before);
  CHECK(a.read(readOnly, nullptr, 1, &count) == RISC_FILES_INVALID);
  CHECK(a.write(writeOnly, nullptr, 1, &count) == RISC_FILES_INVALID);
  CHECK(a.read(readOnly, bytes, RISC_STORAGE_FILES_IO_MAX + 1, &count) == RISC_FILES_INVALID);
  CHECK(a.write(writeOnly, bytes, RISC_STORAGE_FILES_IO_MAX + 1, &count) == RISC_FILES_INVALID);
  CHECK(a.read(readOnly, bytes, sizeof(bytes), nullptr) == RISC_FILES_INVALID);
  CHECK(a.write(writeOnly, bytes, sizeof(bytes), nullptr) == RISC_FILES_INVALID);
  CHECK(a.info(readOnly, nullptr, &offset) == RISC_FILES_INVALID);
  CHECK(a.info(readOnly, &size, nullptr) == RISC_FILES_INVALID);
  CHECK(a.dirTell(directory, nullptr) == RISC_FILES_INVALID);
  CHECK(a.dirNext(directory, nullptr) == RISC_FILES_INVALID);
  entry.struct_size = sizeof(entry) - 1;
  CHECK(a.dirNext(directory, &entry) == RISC_FILES_INVALID);
  entry = entryBuffer();
  for (Token invalid : {Token(0), Token(987654), directory}) {
    CHECK(a.close(invalid) == RISC_FILES_INVALID);
    CHECK(a.read(invalid, bytes, sizeof(bytes), &count) == RISC_FILES_INVALID);
    CHECK(a.write(invalid, bytes, sizeof(bytes), &count) == RISC_FILES_INVALID);
    CHECK(a.seek(invalid, 0) == RISC_FILES_INVALID);
    CHECK(a.info(invalid, &size, &offset) == RISC_FILES_INVALID);
    CHECK(a.sync(invalid) == RISC_FILES_INVALID);
    CHECK(a.truncate(invalid, 0) == RISC_FILES_INVALID);
  }
  for (Token invalid : {Token(0), Token(987654), readOnly, writeOnly}) {
    CHECK(a.dirClose(invalid) == RISC_FILES_INVALID);
    CHECK(a.dirNext(invalid, &entry) == RISC_FILES_INVALID);
    CHECK(a.dirTell(invalid, &offset) == RISC_FILES_INVALID);
    CHECK(a.dirSeek(invalid, 0) == RISC_FILES_INVALID);
    CHECK(a.dirRewind(invalid) == RISC_FILES_INVALID);
  }
  CHECK(b.totalCalls() == before);
  CHECK(a.close(readOnly) == RISC_FILES_OK);
  CHECK(a.close(writeOnly) == RISC_FILES_OK);
  CHECK(a.dirClose(directory) == RISC_FILES_OK);
  Token fresh = f.open(); CHECK(fresh != readOnly && fresh != writeOnly && fresh != directory);
  before = b.totalCalls();
  CHECK(a.read(readOnly, bytes, sizeof(bytes), &count) == RISC_FILES_INVALID);
  CHECK(a.write(writeOnly, bytes, sizeof(bytes), &count) == RISC_FILES_INVALID);
  CHECK(a.dirNext(directory, &entry) == RISC_FILES_INVALID);
  CHECK(b.totalCalls() == before);
  CHECK(a.close(fresh) == RISC_FILES_OK);
}

void testCapacity() {
  activeTest = "shared file/directory slot exhaustion and recovery";
  Fixture f; std::vector<Token> tokens;
  for (unsigned i = 0; i < Adapter::HandleCapacity; ++i)
    tokens.push_back(i % 2 ? f.directory() : f.open());
  const unsigned opens = f.backend.count("file_open") + f.backend.count("dir_open");
  Token out = 99;
  CHECK(f.adapter.open("/file", RISC_FILES_READ, &out) == RISC_FILES_BUSY && out == 0);
  CHECK(f.adapter.dirOpen("/dir", &out) == RISC_FILES_BUSY && out == 0);
  CHECK(f.backend.count("file_open") + f.backend.count("dir_open") == opens);
  CHECK(f.adapter.close(tokens.front()) == RISC_FILES_OK);
  Token fresh = f.open(); CHECK(fresh != tokens.front());
  CHECK(f.adapter.close(tokens.front()) == RISC_FILES_INVALID);
  CHECK(f.adapter.close(fresh) == RISC_FILES_OK);
  for (unsigned i = 1; i < tokens.size(); ++i)
    CHECK((i % 2 ? f.adapter.dirClose(tokens[i]) : f.adapter.close(tokens[i])) == RISC_FILES_OK);
  CHECK(f.adapter.exitSafe() && f.backend.handles.empty());
}

void testErrorClassification() {
  activeTest = "authoritative missing versus unavailable, invalid metadata and I/O";
  Fixture f; auto& a = f.adapter; auto& b = f.backend; auto st = statBuffer(); Token out = 99;
  CHECK(a.stat("/absent", &st) == RISC_FILES_NOT_FOUND);
  CHECK(a.open("/absent", RISC_FILES_READ, &out) == RISC_FILES_NOT_FOUND && out == 0);
  CHECK(b.count("file_open") == 0);
  b.fail = "metadata";
  CHECK(a.stat("/absent", &st) == RISC_FILES_IO);
  CHECK(a.open("/absent", RISC_FILES_WRITE | RISC_FILES_CREATE, &out) == RISC_FILES_IO);
  CHECK(a.mkdir("/absent") == RISC_FILES_IO);
  CHECK(a.remove("/absent") == RISC_FILES_IO);
  CHECK(a.rename("/file", "/absent") == RISC_FILES_IO);
  CHECK(a.dirOpen("/dir", &out) == RISC_FILES_IO);
  CHECK(b.count("file_open") == 0 && b.count("mkdir") == 0 && b.count("remove") == 0 && b.count("rename") == 0);
  CHECK(!a.retained() && a.exitSafe());
  b.fail.clear(); b.metadataOverride = RISC_STORAGE_FS_INVALID;
  CHECK(a.stat("/file", &st) == RISC_FILES_INVALID);
  b.metadataOverride = 8123;
  CHECK(a.stat("/file", &st) == RISC_FILES_IO);
  b.metadataOverride = 999; b.malformedMetadata = true;
  CHECK(a.stat("/file", &st) == RISC_FILES_IO);
  b.malformedMetadata = false; b.state = RISC_STORAGE_STATE_UNAVAILABLE;
  unsigned metadata = b.count("metadata");
  CHECK(a.stat("/absent", &st) == RISC_FILES_UNAVAILABLE);
  CHECK(a.open("/file", RISC_FILES_READ, &out) == RISC_FILES_UNAVAILABLE && out == 0);
  CHECK(a.dirOpen("/dir", &out) == RISC_FILES_UNAVAILABLE && out == 0);
  CHECK(a.mkdir("/absent") == RISC_FILES_UNAVAILABLE);
  CHECK(a.remove("/file") == RISC_FILES_UNAVAILABLE);
  CHECK(a.rename("/file", "/new") == RISC_FILES_UNAVAILABLE);
  CHECK(b.count("metadata") == metadata && !a.retained() && a.exitSafe());
  b.state = RISC_STORAGE_STATE_READY;
  Token recovered = f.open(); CHECK(a.close(recovered) == RISC_FILES_OK);

  for (bool directory : {false, true}) {
    Fixture clean; clean.backend.fail = directory ? "dir_open" : "file_open";
    CHECK((directory ? clean.adapter.dirOpen("/dir", &out) : clean.adapter.open("/file", RISC_FILES_READ, &out)) == RISC_FILES_IO);
    CHECK(out == 0 && clean.backend.count("ready") == 1);
    CHECK(clean.adapter.exitSafe() && !clean.adapter.retained());
  }
  for (const char* operation : {"mkdir", "rename", "remove"}) {
    Fixture failing; failing.backend.fail = operation;
    int32_t result = std::strcmp(operation, "mkdir") == 0 ? failing.adapter.mkdir("/new") :
      std::strcmp(operation, "rename") == 0 ? failing.adapter.rename("/file", "/new") : failing.adapter.remove("/file");
    CHECK(result == RISC_FILES_IO && !failing.adapter.retained() && failing.adapter.exitSafe());
  }
}

void testTransferAndHandleFailures() {
  activeTest = "short transfers, handle errors and operation failures";
  Fixture f; auto& a = f.adapter; auto& b = f.backend;
  Token file = f.open(RISC_FILES_READ | RISC_FILES_WRITE); Token directory = f.directory();
  uint32_t count = 99; char bytes[16]{}; uint64_t size = 77, offset = 88;
  b.shortWrite = true;
  CHECK(a.write(file, "XYZ", 3, &count) == RISC_FILES_OK && count == 2);
  CHECK(b.nodes.at("/file").data == "XYcdef");
  b.shortWrite = false; b.errorOnHandle = true;
  CHECK(a.seek(file, 0) == RISC_FILES_OK);
  CHECK(a.read(file, bytes, 2, &count) == RISC_FILES_IO && count == 2);
  CHECK(a.write(file, "Q", 1, &count) == RISC_FILES_IO && count == 1);
  auto entry = entryBuffer();
  CHECK(a.dirNext(directory, &entry) == RISC_FILES_IO);
  CHECK(a.dirSeek(directory, 2) == RISC_FILES_OK);
  CHECK(a.dirNext(directory, &entry) == RISC_FILES_IO); // An error is not clean EOF.
  b.errorOnHandle = false; b.malformedName = true;
  CHECK(a.dirRewind(directory) == RISC_FILES_OK);
  CHECK(a.dirNext(directory, &entry) == RISC_FILES_IO);
  CHECK(!a.retained()); b.malformedName = false;
  b.fail = "file_seek"; CHECK(a.seek(file, 0) == RISC_FILES_IO);
  b.fail = "file_info"; CHECK(a.info(file, &size, &offset) == RISC_FILES_IO && size == 77 && offset == 88);
  b.fail = "file_sync"; CHECK(a.sync(file) == RISC_FILES_IO);
  b.fail = "file_truncate"; CHECK(a.truncate(file, 0) == RISC_FILES_IO);
  b.fail = "dir_rewind"; CHECK(a.dirRewind(directory) == RISC_FILES_IO);
  b.fail = "dir_tell"; CHECK(a.dirTell(directory, &offset) == RISC_FILES_IO && offset == 88);
  b.fail = "dir_seek"; CHECK(a.dirSeek(directory, 0) == RISC_FILES_IO);
  b.fail = "file_read"; b.errorOnHandle = true;
  CHECK(a.read(file, bytes, 2, &count) == RISC_FILES_IO && count == 0);
  b.fail = "file_write";
  CHECK(a.write(file, "Q", 1, &count) == RISC_FILES_IO && count == 0);
  b.fail = "dir_next";
  CHECK(a.dirNext(directory, &entry) == RISC_FILES_IO);
  b.fail.clear(); b.errorOnHandle = false;
  CHECK(a.close(file) == RISC_FILES_OK && a.dirClose(directory) == RISC_FILES_OK);
  CHECK(a.exitSafe());

  Fixture maximum; Token t = maximum.open(RISC_FILES_READ | RISC_FILES_WRITE);
  std::vector<char> block(RISC_STORAGE_FILES_IO_MAX, 'x');
  CHECK(maximum.adapter.write(t, block.data(), block.size(), &count) == RISC_FILES_OK && count == block.size());
  CHECK(maximum.adapter.seek(t, 0) == RISC_FILES_OK);
  CHECK(maximum.adapter.read(t, block.data(), block.size(), &count) == RISC_FILES_OK && count == block.size());
  CHECK(maximum.adapter.close(t) == RISC_FILES_OK);
}

// After uncertain custody, even observing the native backend again is forbidden.
// Use valid live tokens so local argument rejection cannot mask a provider call.
void checkFrozen(Fixture& f, Token file = 0, Token directory = 0) {
  CHECK(f.adapter.retained() && !f.adapter.exitSafe());
  auto& a = f.adapter; auto& b = f.backend;
  b.state = RISC_STORAGE_STATE_READY; b.ready = true; b.fail.clear(); b.transition.clear();
  const unsigned before = b.totalCalls(); Token out = 99; auto st = statBuffer();
  CHECK(a.open("/file", RISC_FILES_READ, &out) == RISC_FILES_RETAINED && out == 0);
  CHECK(a.dirOpen("/dir", &out) == RISC_FILES_RETAINED && out == 0);
  CHECK(a.stat("/file", &st) == RISC_FILES_RETAINED);
  CHECK(a.mkdir("/new") == RISC_FILES_RETAINED);
  CHECK(a.rename("/file", "/new") == RISC_FILES_RETAINED);
  CHECK(a.remove("/file") == RISC_FILES_RETAINED);
  CHECK(a.close(0) == RISC_FILES_RETAINED && a.dirClose(0) == RISC_FILES_RETAINED);
  CHECK(!a.bind(b.base()));
  if (file) {
    uint32_t count = 99; char bytes[4]{}; uint64_t size = 0, offset = 0;
    CHECK(a.read(file, bytes, sizeof(bytes), &count) == RISC_FILES_RETAINED && count == 0);
    CHECK(a.write(file, bytes, sizeof(bytes), &count) == RISC_FILES_RETAINED && count == 0);
    CHECK(a.seek(file, 0) == RISC_FILES_RETAINED);
    CHECK(a.info(file, &size, &offset) == RISC_FILES_RETAINED);
    CHECK(a.sync(file) == RISC_FILES_RETAINED);
    CHECK(a.truncate(file, 0) == RISC_FILES_RETAINED);
    CHECK(a.close(file) == RISC_FILES_RETAINED);
  }
  if (directory) {
    auto entry = entryBuffer(); uint64_t cursor = 0;
    CHECK(a.dirNext(directory, &entry) == RISC_FILES_RETAINED);
    CHECK(a.dirTell(directory, &cursor) == RISC_FILES_RETAINED);
    CHECK(a.dirSeek(directory, 0) == RISC_FILES_RETAINED);
    CHECK(a.dirRewind(directory) == RISC_FILES_RETAINED);
    CHECK(a.dirClose(directory) == RISC_FILES_RETAINED);
  }
  CHECK(b.totalCalls() == before);
}

void testRetention() {
  activeTest = "sticky retention, hidden failed opens and failed close custody";
  for (int32_t state : {int32_t(RISC_STORAGE_STATE_RETAINED), int32_t(12345)}) {
    Fixture f; f.backend.state = state; auto st = statBuffer();
    CHECK(f.adapter.stat("/file", &st) == RISC_FILES_RETAINED);
    CHECK(f.backend.count("metadata") == 0); checkFrozen(f);
  }
  for (bool directory : {false, true}) {
    Fixture f;
    Token file = f.open(RISC_FILES_READ | RISC_FILES_WRITE); Token dir = f.directory();
    f.backend.state = RISC_STORAGE_STATE_UNAVAILABLE;
    CHECK((directory ? f.adapter.dirRewind(dir) : f.adapter.sync(file)) == RISC_FILES_RETAINED);
    checkFrozen(f, file, dir);
  }
  for (bool directory : {false, true}) {
    for (bool observerReports : {false, true}) {
      Fixture f; auto& b = f.backend; Token out = 99;
      b.fail = directory ? "dir_open" : "file_open"; b.hiddenOpen = true;
      if (observerReports) { b.transition = b.fail; b.transitionState = RISC_STORAGE_STATE_UNAVAILABLE; }
      else b.ready = false;
      CHECK((directory ? f.adapter.dirOpen("/dir", &out) : f.adapter.open("/file", RISC_FILES_READ, &out)) == RISC_FILES_RETAINED);
      CHECK(out == 0 && b.handles.size() == 1);
      CHECK(b.count("file_close") == 0 && b.count("dir_close") == 0);
      checkFrozen(f);
    }
  }
  for (bool directory : {false, true}) {
    for (bool failed : {false, true}) {
      Fixture f; Token file = f.open(RISC_FILES_READ | RISC_FILES_WRITE); Token dir = f.directory();
      const char* operation = directory ? "dir_close" : "file_close";
      if (failed) f.backend.fail = operation;
      else { f.backend.transition = operation; f.backend.transitionState = RISC_STORAGE_STATE_UNAVAILABLE; }
      CHECK((directory ? f.adapter.dirClose(dir) : f.adapter.close(file)) == RISC_FILES_RETAINED);
      CHECK(f.backend.count(operation) == 1);
      checkFrozen(f, file, dir);
      CHECK(f.backend.count(operation) == 1);
    }
  }
  for (bool write : {false, true}) {
    Fixture f; Token file = f.open(RISC_FILES_READ | RISC_FILES_WRITE); Token dir = f.directory();
    f.backend.excessiveTransfer = true; char bytes[4]{}; uint32_t count = 99;
    CHECK((write ? f.adapter.write(file, bytes, sizeof(bytes), &count) : f.adapter.read(file, bytes, sizeof(bytes), &count)) == RISC_FILES_RETAINED);
    CHECK(count == 0); checkFrozen(f, file, dir);
  }
}

void testPostCallStateFences() {
  activeTest = "post-call state fences on every backend operation";
  const char* operations[] = {"file_read", "file_write", "file_seek", "file_info", "file_sync", "file_truncate",
    "dir_next", "dir_rewind", "dir_tell", "dir_seek", "handle_error", "metadata", "mkdir", "rename", "remove"};
  for (const char* operation : operations) {
    Fixture f; Token file = f.open(RISC_FILES_READ | RISC_FILES_WRITE); Token dir = f.directory();
    f.backend.transition = operation; char bytes[4]{}; uint32_t count = 99;
    uint64_t size = 77, offset = 88; auto entry = entryBuffer(); auto st = statBuffer();
    int32_t result = 999;
    const std::string op(operation);
    if (op == "file_read" || op == "handle_error") result = f.adapter.read(file, bytes, sizeof(bytes), &count);
    else if (op == "file_write") result = f.adapter.write(file, bytes, sizeof(bytes), &count);
    else if (op == "file_seek") result = f.adapter.seek(file, 0);
    else if (op == "file_info") result = f.adapter.info(file, &size, &offset);
    else if (op == "file_sync") result = f.adapter.sync(file);
    else if (op == "file_truncate") result = f.adapter.truncate(file, 0);
    else if (op == "dir_next") result = f.adapter.dirNext(dir, &entry);
    else if (op == "dir_rewind") result = f.adapter.dirRewind(dir);
    else if (op == "dir_tell") result = f.adapter.dirTell(dir, &offset);
    else if (op == "dir_seek") result = f.adapter.dirSeek(dir, 0);
    else if (op == "metadata") result = f.adapter.stat("/file", &st);
    else if (op == "mkdir") result = f.adapter.mkdir("/new");
    else if (op == "rename") result = f.adapter.rename("/file", "/new");
    else if (op == "remove") result = f.adapter.remove("/file");
    CHECK(result == RISC_FILES_RETAINED);
    if (op == "file_read" || op == "file_write") CHECK(count == 0);
    if (op == "file_info") CHECK(size == 77 && offset == 88);
    if (op == "dir_tell") CHECK(offset == 88);
    checkFrozen(f, file, dir);
  }
  for (bool directory : {false, true}) {
    Fixture f; Token out = 99;
    f.backend.transition = directory ? "dir_open" : "file_open";
    CHECK((directory ? f.adapter.dirOpen("/dir", &out) : f.adapter.open("/file", RISC_FILES_READ, &out)) == RISC_FILES_RETAINED);
    CHECK(out == 0 && f.backend.handles.size() == 1);
    checkFrozen(f);
  }
}
// This provider implements only the public storage.files ABI. It has no
// storage.volume table, observer, mount API, filesystem suffix or device data.
struct GenericProvider {
  struct File { bool directory = false; std::string bytes; };
  struct OpenFile { std::string path; uint32_t flags; bool directory; uint64_t position = 0; };
  risc_storage_files_v1 api{};
  std::map<std::string, File> files;
  std::map<uint32_t, OpenFile> handles;
  std::map<std::string, unsigned> calls;
  std::map<std::string, int32_t> results;
  uint32_t generation = 700;
  bool tokenOnFailure = false, successWithoutToken = false;
  bool shortTransfer = false, oversizedTransfer = false, transferOnError = false;
  bool badStatSize = false, badEntrySize = false, badEntryName = false;

  static GenericProvider& from(void* context) { return *static_cast<GenericProvider*>(context); }
  int32_t call(const char* operation) {
    ++calls[operation];
    const auto found = results.find(operation);
    return found == results.end() ? RISC_FILES_OK : found->second;
  }
  unsigned totalCalls() const {
    unsigned total = 0;
    for (const auto& call : calls) total += call.second;
    return total;
  }
  OpenFile& handle(uint32_t token, bool directory) {
    auto found = handles.find(token);
    CHECK(found != handles.end() && found->second.directory == directory);
    return found->second;
  }
  bool parentExists(const std::string& path) const {
    const size_t slash = path.find_last_of('/');
    const auto parent = files.find(slash == 0 ? "/" : path.substr(0, slash));
    return parent != files.end() && parent->second.directory;
  }
  std::vector<std::string> children(const std::string& path) const {
    const std::string prefix = path == "/" ? "/" : path + "/";
    std::vector<std::string> names;
    for (const auto& file : files) {
      if (file.first.compare(0, prefix.size(), prefix) != 0) continue;
      const std::string name = file.first.substr(prefix.size());
      if (!name.empty() && name.find('/') == std::string::npos) names.push_back(name);
    }
    return names;
  }
  int32_t open(const char* path, uint32_t flags, bool directory, Token* out) {
    CHECK(out && *out == 0);
    const int32_t result = call(directory ? "dir_open" : "open");
    if (result != RISC_FILES_OK && !tokenOnFailure) return result;
    if (result == RISC_FILES_OK && successWithoutToken) return result;
    auto file = files.find(path);
    if (file == files.end()) {
      if (directory || !(flags & RISC_FILES_CREATE)) return RISC_FILES_NOT_FOUND;
      if (!parentExists(path)) return RISC_FILES_NOT_FOUND;
      file = files.emplace(path, File{}).first;
    } else if ((flags & (RISC_FILES_CREATE | RISC_FILES_EXCLUSIVE)) == (RISC_FILES_CREATE | RISC_FILES_EXCLUSIVE)) {
      return RISC_FILES_EXISTS;
    }
    if (file->second.directory != directory) return RISC_FILES_INVALID;
    if (flags & RISC_FILES_TRUNCATE) file->second.bytes.clear();
    *out = ++generation;
    handles[*out] = {path, flags, directory, 0};
    return result;
  }
  GenericProvider() {
    files["/"] = {true, ""}; files["/dir"] = {true, ""};
    files["/file"] = {false, "abcdef"}; files["/dir/a"] = {false, "abc"};
    files["/dir/sub"] = {true, ""};
    api.api_version = RISC_STORAGE_FILES_API_V1; api.struct_size = sizeof(api); api.context = this;
    api.open = [](void* c, const char* path, uint32_t flags, Token* out) {
      return from(c).open(path, flags, false, out);
    };
    api.dir_open = [](void* c, const char* path, Token* out) {
      return from(c).open(path, 0, true, out);
    };
    api.close = [](void* c, Token token) -> int32_t {
      auto& p = from(c); p.handle(token, false); const int32_t result = p.call("close");
      if (result == RISC_FILES_OK) p.handles.erase(token);
      return result;
    };
    api.dir_close = [](void* c, Token token) -> int32_t {
      auto& p = from(c); p.handle(token, true); const int32_t result = p.call("dir_close");
      if (result == RISC_FILES_OK) p.handles.erase(token);
      return result;
    };
    api.read = [](void* c, Token token, void* buffer, uint32_t capacity, uint32_t* transferred) -> int32_t {
      auto& p = from(c); auto& h = p.handle(token, false); CHECK(transferred && *transferred == 0);
      const int32_t result = p.call("read");
      if (result != RISC_FILES_OK && !(result == RISC_FILES_IO && p.transferOnError)) return result;
      if (p.oversizedTransfer) { *transferred = capacity + 1; return result; }
      const std::string& bytes = p.files.at(h.path).bytes;
      const size_t position = std::min<size_t>(h.position, bytes.size());
      size_t count = std::min<size_t>(capacity, bytes.size() - position);
      if (p.shortTransfer) count /= 2;
      if (count) std::memcpy(buffer, bytes.data() + position, count);
      h.position += count; *transferred = uint32_t(count); return result;
    };
    api.write = [](void* c, Token token, const void* buffer, uint32_t size, uint32_t* transferred) -> int32_t {
      auto& p = from(c); auto& h = p.handle(token, false); CHECK(transferred && *transferred == 0);
      const int32_t result = p.call("write");
      if (result != RISC_FILES_OK && !(result == RISC_FILES_IO && p.transferOnError)) return result;
      if (p.oversizedTransfer) { *transferred = size + 1; return result; }
      std::string& bytes = p.files.at(h.path).bytes;
      if (h.flags & RISC_FILES_APPEND) h.position = bytes.size();
      const size_t count = p.shortTransfer ? size / 2 : size;
      if (count) {
        if (h.position + count > bytes.size()) bytes.resize(h.position + count);
        std::memcpy(&bytes[size_t(h.position)], buffer, count);
      }
      h.position += count; *transferred = uint32_t(count); return result;
    };
    api.seek = [](void* c, Token token, uint64_t offset) -> int32_t {
      auto& p = from(c); auto& h = p.handle(token, false); const int32_t result = p.call("seek");
      if (result != RISC_FILES_OK) return result;
      if (offset > p.files.at(h.path).bytes.size()) return RISC_FILES_IO;
      h.position = offset; return RISC_FILES_OK;
    };
    api.info = [](void* c, Token token, uint64_t* size, uint64_t* offset) -> int32_t {
      auto& p = from(c); auto& h = p.handle(token, false); const int32_t result = p.call("info");
      if (result != RISC_FILES_OK) return result;
      *size = p.files.at(h.path).bytes.size(); *offset = h.position; return RISC_FILES_OK;
    };
    api.sync = [](void* c, Token token) -> int32_t {
      auto& p = from(c); p.handle(token, false); return p.call("sync");
    };
    api.truncate = [](void* c, Token token, uint64_t size) -> int32_t {
      auto& p = from(c); auto& h = p.handle(token, false); const int32_t result = p.call("truncate");
      if (result != RISC_FILES_OK) return result;
      if (size > p.files.at(h.path).bytes.size()) return RISC_FILES_IO;
      p.files.at(h.path).bytes.resize(size); h.position = std::min(h.position, size); return RISC_FILES_OK;
    };
    api.stat = [](void* c, const char* path, risc_storage_files_stat_v1* out) -> int32_t {
      auto& p = from(c); CHECK(out && out->struct_size == sizeof(*out));
      const int32_t result = p.call("stat"); if (result != RISC_FILES_OK) return result;
      const auto file = p.files.find(path); if (file == p.files.end()) return RISC_FILES_NOT_FOUND;
      *out = {sizeof(*out), uint32_t(RISC_FILES_MODIFIED_VALID), uint64_t(file->second.bytes.size()), 4242};
      if (file->second.directory) out->flags |= RISC_FILES_DIRECTORY;
      if (p.badStatSize) out->struct_size = 0;
      return RISC_FILES_OK;
    };
    api.mkdir = [](void* c, const char* path) -> int32_t {
      auto& p = from(c); const int32_t result = p.call("mkdir"); if (result != RISC_FILES_OK) return result;
      if (p.files.count(path)) return RISC_FILES_EXISTS;
      if (!p.parentExists(path)) return RISC_FILES_NOT_FOUND;
      p.files[path] = {true, ""}; return RISC_FILES_OK;
    };
    api.rename = [](void* c, const char* source, const char* destination) -> int32_t {
      auto& p = from(c); const int32_t result = p.call("rename"); if (result != RISC_FILES_OK) return result;
      if (p.files.count(destination)) return RISC_FILES_EXISTS;
      if (!p.files.count(source) || !p.parentExists(destination)) return RISC_FILES_NOT_FOUND;
      p.files[destination] = p.files.at(source); p.files.erase(source); return RISC_FILES_OK;
    };
    api.remove = [](void* c, const char* path) -> int32_t {
      auto& p = from(c); const int32_t result = p.call("remove"); if (result != RISC_FILES_OK) return result;
      const auto file = p.files.find(path); if (file == p.files.end()) return RISC_FILES_NOT_FOUND;
      if (file->second.directory && !p.children(path).empty()) return RISC_FILES_IO;
      p.files.erase(file); return RISC_FILES_OK;
    };
    api.dir_next = [](void* c, Token token, risc_storage_files_entry_v1* out) -> int32_t {
      auto& p = from(c); auto& h = p.handle(token, true); CHECK(out && out->struct_size == sizeof(*out));
      const int32_t result = p.call("dir_next"); if (result != RISC_FILES_OK) return result;
      const auto names = p.children(h.path); if (h.position >= names.size()) return RISC_FILES_END;
      const std::string name = names[size_t(h.position++)];
      const auto& file = p.files.at(h.path == "/" ? "/" + name : h.path + "/" + name);
      *out = {}; out->struct_size = sizeof(*out); out->flags = file.directory ? RISC_FILES_DIRECTORY : 0;
      out->size = file.bytes.size(); std::memcpy(out->name, name.c_str(), name.size() + 1);
      if (p.badEntrySize) out->struct_size = 0;
      if (p.badEntryName) std::memset(out->name, 'x', sizeof(out->name));
      return RISC_FILES_OK;
    };
    api.dir_rewind = [](void* c, Token token) -> int32_t {
      auto& p = from(c); auto& h = p.handle(token, true); const int32_t result = p.call("dir_rewind");
      if (result == RISC_FILES_OK) h.position = 0;
      return result;
    };
    api.dir_tell = [](void* c, Token token, uint64_t* out) -> int32_t {
      auto& p = from(c); auto& h = p.handle(token, true); const int32_t result = p.call("dir_tell");
      if (result == RISC_FILES_OK) *out = h.position;
      return result;
    };
    api.dir_seek = [](void* c, Token token, uint64_t offset) -> int32_t {
      auto& p = from(c); auto& h = p.handle(token, true); const int32_t result = p.call("dir_seek");
      if (result != RISC_FILES_OK) return result;
      if (offset > p.children(h.path).size()) return RISC_FILES_IO;
      h.position = offset; return RISC_FILES_OK;
    };
  }
};
struct GenericFixture {
  GenericProvider provider;
  Adapter adapter;
  GenericFixture() { CHECK(adapter.bindFiles(&provider.api)); CHECK(provider.totalCalls() == 0); }
  Token open(uint32_t flags = RISC_FILES_READ | RISC_FILES_WRITE, const char* path = "/file") {
    Token token = 0; CHECK(adapter.open(path, flags, &token) == RISC_FILES_OK); CHECK(token); return token;
  }
  Token directory() {
    Token token = 0; CHECK(adapter.dirOpen("/dir", &token) == RISC_FILES_OK); CHECK(token); return token;
  }
};

void testGenericBindingAndValidation() {
  activeTest = "direct provider table admission, local validation and rights";
  Adapter empty; CHECK(!empty.bindFiles(nullptr));
  for (unsigned which = 0; which < 20; ++which) {
    GenericProvider p; Adapter a;
    switch (which) {
      case 0: p.api.api_version = 2; break;
      case 1: p.api.struct_size = sizeof(p.api) - 1; break;
      case 2: p.api.open = nullptr; break;
      case 3: p.api.close = nullptr; break;
      case 4: p.api.read = nullptr; break;
      case 5: p.api.write = nullptr; break;
      case 6: p.api.seek = nullptr; break;
      case 7: p.api.info = nullptr; break;
      case 8: p.api.sync = nullptr; break;
      case 9: p.api.truncate = nullptr; break;
      case 10: p.api.stat = nullptr; break;
      case 11: p.api.mkdir = nullptr; break;
      case 12: p.api.rename = nullptr; break;
      case 13: p.api.remove = nullptr; break;
      case 14: p.api.dir_open = nullptr; break;
      case 15: p.api.dir_next = nullptr; break;
      case 16: p.api.dir_close = nullptr; break;
      case 17: p.api.dir_rewind = nullptr; break;
      case 18: p.api.dir_tell = nullptr; break;
      case 19: p.api.dir_seek = nullptr; break;
    }
    CHECK(!a.bindFiles(&p.api) && !a.bound() && a.exitSafe() && !a.retained());
    CHECK(p.totalCalls() == 0);
  }
  GenericFixture f; auto& a = f.adapter; auto& p = f.provider;
  CHECK(a.bound() && a.exitSafe() && !a.bindFiles(&p.api));
  Backend legacy; CHECK(!a.bind(legacy.base()));
  Fixture volume; CHECK(!volume.adapter.bindFiles(&p.api));
  Token out = 99; auto st = statBuffer();
  const char* paths[] = {nullptr, "", "relative", "//file", "/a/../file", "/a/", "/\\x", "/\xc0\xaf"};
  for (const char* path : paths) {
    CHECK(a.open(path, RISC_FILES_READ, &out) == RISC_FILES_INVALID && out == 0);
    CHECK(a.stat(path, &st) == RISC_FILES_INVALID);
    CHECK(a.dirOpen(path, &out) == RISC_FILES_INVALID && out == 0);
    CHECK(a.mkdir(path) == RISC_FILES_INVALID);
    CHECK(a.rename(path, "/dest") == RISC_FILES_INVALID);
    CHECK(a.rename("/source", path) == RISC_FILES_INVALID);
    CHECK(a.remove(path) == RISC_FILES_INVALID);
  }
  CHECK(a.open("/file", RISC_FILES_READ | RISC_FILES_CREATE, &out) == RISC_FILES_INVALID);
  CHECK(a.open("/file", 128, &out) == RISC_FILES_INVALID);
  CHECK(a.remove("/") == RISC_FILES_INVALID);
  CHECK(p.totalCalls() == 0);
  Token read = f.open(RISC_FILES_READ), write = f.open(RISC_FILES_WRITE), dir = f.directory();
  unsigned before = p.totalCalls(); uint32_t count = 99; char bytes[8]{}; uint64_t size = 0, position = 0;
  CHECK(a.write(read, bytes, 1, &count) == RISC_FILES_DENIED && count == 0);
  CHECK(a.truncate(read, 0) == RISC_FILES_DENIED);
  CHECK(a.read(write, bytes, 1, &count) == RISC_FILES_DENIED && count == 0);
  CHECK(a.read(read, bytes, RISC_STORAGE_FILES_IO_MAX + 1, &count) == RISC_FILES_INVALID);
  CHECK(a.write(write, bytes, RISC_STORAGE_FILES_IO_MAX + 1, &count) == RISC_FILES_INVALID);
  CHECK(a.read(read, nullptr, 1, &count) == RISC_FILES_INVALID);
  CHECK(a.write(write, nullptr, 1, &count) == RISC_FILES_INVALID);
  CHECK(a.read(read, bytes, 1, nullptr) == RISC_FILES_INVALID);
  CHECK(a.write(write, bytes, 1, nullptr) == RISC_FILES_INVALID);
  CHECK(a.info(read, nullptr, &position) == RISC_FILES_INVALID);
  CHECK(a.info(read, &size, nullptr) == RISC_FILES_INVALID);
  CHECK(a.dirTell(dir, nullptr) == RISC_FILES_INVALID);
  CHECK(a.dirNext(dir, nullptr) == RISC_FILES_INVALID);
  CHECK(a.close(dir) == RISC_FILES_INVALID && a.dirClose(read) == RISC_FILES_INVALID);
  CHECK(a.sync(0) == RISC_FILES_INVALID && a.seek(123456, 0) == RISC_FILES_INVALID);
  CHECK(p.totalCalls() == before);
  CHECK(a.close(read) == RISC_FILES_OK && a.close(write) == RISC_FILES_OK && a.dirClose(dir) == RISC_FILES_OK);
  Token fresh = f.open(); CHECK(fresh != read && fresh != write && fresh != dir);
  before = p.totalCalls();
  CHECK(a.close(read) == RISC_FILES_INVALID && a.read(read, bytes, 1, &count) == RISC_FILES_INVALID);
  CHECK(p.totalCalls() == before);
  CHECK(a.close(fresh) == RISC_FILES_OK && a.exitSafe());
}

void testGenericCrudAndDirectories() {
  activeTest = "direct provider complete CRUD, append, truncation, directories and cursors";
  GenericFixture f; auto& a = f.adapter; auto& p = f.provider; auto st = statBuffer(); Token out = 99;
  CHECK(a.stat("/absent", &st) == RISC_FILES_NOT_FOUND);
  CHECK(a.open("/absent", RISC_FILES_READ, &out) == RISC_FILES_NOT_FOUND && out == 0);
  CHECK(a.mkdir("/new") == RISC_FILES_OK && a.mkdir("/new") == RISC_FILES_EXISTS);
  Token file = f.open(RISC_FILES_READ | RISC_FILES_WRITE | RISC_FILES_CREATE | RISC_FILES_EXCLUSIVE, "/new/data");
  CHECK(!a.exitSafe() && file != p.handles.begin()->first);
  uint32_t count = 99; uint64_t size = 99, position = 99; char bytes[16]{};
  CHECK(a.write(file, "123456", 6, &count) == RISC_FILES_OK && count == 6);
  CHECK(a.info(file, &size, &position) == RISC_FILES_OK && size == 6 && position == 6);
  CHECK(a.seek(file, 1) == RISC_FILES_OK);
  CHECK(a.write(file, "XY", 2, &count) == RISC_FILES_OK && count == 2);
  CHECK(a.seek(file, 0) == RISC_FILES_OK);
  CHECK(a.read(file, bytes, sizeof(bytes), &count) == RISC_FILES_OK && std::string(bytes, count) == "1XY456");
  CHECK(a.read(file, bytes, sizeof(bytes), &count) == RISC_FILES_OK && count == 0);
  CHECK(a.read(file, nullptr, 0, &count) == RISC_FILES_OK && count == 0);
  CHECK(a.write(file, nullptr, 0, &count) == RISC_FILES_OK && count == 0);
  CHECK(a.seek(file, 7) == RISC_FILES_IO && a.truncate(file, 7) == RISC_FILES_IO);
  CHECK(a.truncate(file, 3) == RISC_FILES_OK);
  CHECK(a.info(file, &size, &position) == RISC_FILES_OK && size == 3 && position == 3);
  CHECK(a.sync(file) == RISC_FILES_OK);
  CHECK(a.close(file) == RISC_FILES_OK && a.exitSafe());
  CHECK(a.stat("/new/data", &st) == RISC_FILES_OK && st.size == 3 && st.modified_seconds == 4242);
  CHECK(st.flags == RISC_FILES_MODIFIED_VALID);
  CHECK(a.open("/new/data", RISC_FILES_WRITE | RISC_FILES_CREATE | RISC_FILES_EXCLUSIVE, &out) == RISC_FILES_EXISTS && out == 0);
  CHECK(a.rename("/new/data", "/new/moved") == RISC_FILES_OK);
  CHECK(a.rename("/new/moved", "/file") == RISC_FILES_EXISTS);
  CHECK(a.stat("/new/data", &st) == RISC_FILES_NOT_FOUND);
  CHECK(a.remove("/new") == RISC_FILES_IO);
  CHECK(a.remove("/new/moved") == RISC_FILES_OK && a.remove("/new/moved") == RISC_FILES_NOT_FOUND);
  CHECK(a.remove("/new") == RISC_FILES_OK);
  file = f.open(RISC_FILES_WRITE | RISC_FILES_APPEND);
  CHECK(a.seek(file, 0) == RISC_FILES_OK && a.write(file, "1", 1, &count) == RISC_FILES_OK);
  CHECK(a.seek(file, 1) == RISC_FILES_OK && a.write(file, "2", 1, &count) == RISC_FILES_OK);
  CHECK(p.files.at("/file").bytes == "abcdef12"); CHECK(a.close(file) == RISC_FILES_OK);
  file = f.open(RISC_FILES_WRITE | RISC_FILES_TRUNCATE);
  CHECK(a.info(file, &size, &position) == RISC_FILES_OK && size == 0 && position == 0);
  CHECK(a.close(file) == RISC_FILES_OK);
  CHECK(a.dirOpen("/file", &out) == RISC_FILES_INVALID && out == 0);
  CHECK(a.dirOpen("/absent", &out) == RISC_FILES_NOT_FOUND && out == 0);
  Token dir = f.directory(); auto entry = entryBuffer();
  CHECK(a.dirTell(dir, &position) == RISC_FILES_OK && position == 0);
  CHECK(a.dirNext(dir, &entry) == RISC_FILES_OK && std::string(entry.name) == "a" && entry.size == 3);
  CHECK(a.dirTell(dir, &position) == RISC_FILES_OK && position == 1);
  CHECK(a.dirNext(dir, &entry) == RISC_FILES_OK && std::string(entry.name) == "sub" && (entry.flags & RISC_FILES_DIRECTORY));
  CHECK(a.dirNext(dir, &entry) == RISC_FILES_END && a.dirNext(dir, &entry) == RISC_FILES_END);
  CHECK(a.dirSeek(dir, position) == RISC_FILES_OK);
  CHECK(a.dirNext(dir, &entry) == RISC_FILES_OK && std::string(entry.name) == "sub");
  CHECK(a.dirSeek(dir, 999) == RISC_FILES_IO);
  CHECK(a.dirRewind(dir) == RISC_FILES_OK);
  CHECK(a.dirNext(dir, &entry) == RISC_FILES_OK && std::string(entry.name) == "a");
  CHECK(a.dirClose(dir) == RISC_FILES_OK && a.exitSafe() && p.handles.empty());
  const char* operations[] = {"open", "close", "read", "write", "seek", "info", "sync", "truncate", "stat", "mkdir", "rename", "remove",
    "dir_open", "dir_next", "dir_close", "dir_rewind", "dir_tell", "dir_seek"};
  for (const char* operation : operations) CHECK(p.calls[operation] > 0);
}

void checkGenericFrozen(GenericFixture& f, Token file = 0, Token dir = 0) {
  auto& a = f.adapter; auto& p = f.provider;
  CHECK(a.retained() && !a.exitSafe()); p.results.clear();
  const unsigned before = p.totalCalls(); Token token = 99; auto st = statBuffer(); auto entry = entryBuffer();
  CHECK(a.open("/file", RISC_FILES_READ, &token) == RISC_FILES_RETAINED && token == 0);
  CHECK(a.dirOpen("/dir", &token) == RISC_FILES_RETAINED && token == 0);
  CHECK(a.stat("/file", &st) == RISC_FILES_RETAINED);
  CHECK(a.mkdir("/new") == RISC_FILES_RETAINED && a.remove("/file") == RISC_FILES_RETAINED);
  CHECK(a.rename("/file", "/new") == RISC_FILES_RETAINED);
  CHECK(!a.bindFiles(&p.api));
  CHECK(a.close(0) == RISC_FILES_RETAINED && a.dirClose(0) == RISC_FILES_RETAINED);
  uint32_t count = 99; uint64_t size = 0, offset = 0; char bytes[8]{};
  if (file) {
    CHECK(a.read(file, bytes, sizeof(bytes), &count) == RISC_FILES_RETAINED && count == 0);
    CHECK(a.write(file, bytes, sizeof(bytes), &count) == RISC_FILES_RETAINED && count == 0);
    CHECK(a.seek(file, 0) == RISC_FILES_RETAINED && a.info(file, &size, &offset) == RISC_FILES_RETAINED);
    CHECK(a.sync(file) == RISC_FILES_RETAINED && a.truncate(file, 0) == RISC_FILES_RETAINED);
    CHECK(a.close(file) == RISC_FILES_RETAINED);
  }
  if (dir) {
    CHECK(a.dirNext(dir, &entry) == RISC_FILES_RETAINED && a.dirTell(dir, &offset) == RISC_FILES_RETAINED);
    CHECK(a.dirSeek(dir, 0) == RISC_FILES_RETAINED && a.dirRewind(dir) == RISC_FILES_RETAINED);
    CHECK(a.dirClose(dir) == RISC_FILES_RETAINED);
  }
  CHECK(p.totalCalls() == before);
}

int32_t invokeGeneric(GenericFixture& f, const std::string& op, Token file, Token dir) {
  char bytes[4]{}; uint32_t count = 99; uint64_t size = 77, offset = 88;
  auto st = statBuffer(); auto entry = entryBuffer(); Token out = 99;
  if (op == "open") return f.adapter.open("/file", RISC_FILES_READ, &out);
  if (op == "dir_open") return f.adapter.dirOpen("/dir", &out);
  if (op == "read") return f.adapter.read(file, bytes, sizeof(bytes), &count);
  if (op == "write") return f.adapter.write(file, bytes, sizeof(bytes), &count);
  if (op == "seek") return f.adapter.seek(file, 0);
  if (op == "info") {
    const int32_t result = f.adapter.info(file, &size, &offset);
    if (result != RISC_FILES_OK) CHECK(size == 77 && offset == 88);
    return result;
  }
  if (op == "sync") return f.adapter.sync(file);
  if (op == "truncate") return f.adapter.truncate(file, 0);
  if (op == "stat") return f.adapter.stat("/file", &st);
  if (op == "mkdir") return f.adapter.mkdir("/new");
  if (op == "rename") return f.adapter.rename("/file", "/new");
  if (op == "remove") return f.adapter.remove("/file");
  if (op == "dir_next") return f.adapter.dirNext(dir, &entry);
  if (op == "dir_rewind") return f.adapter.dirRewind(dir);
  if (op == "dir_tell") {
    const int32_t result = f.adapter.dirTell(dir, &offset);
    if (result != RISC_FILES_OK) CHECK(offset == 88);
    return result;
  }
  if (op == "dir_seek") return f.adapter.dirSeek(dir, 0);
  CHECK(false); return 999;
}

void testGenericErrorsAndRetention() {
  activeTest = "direct provider clean errors versus retained custody on every callback";
  const char* operations[] = {"open", "dir_open", "read", "write", "seek", "info", "sync", "truncate", "stat", "mkdir", "rename", "remove",
    "dir_next", "dir_rewind", "dir_tell", "dir_seek"};
  const int32_t errors[] = {RISC_FILES_NOT_FOUND, RISC_FILES_BUSY, RISC_FILES_UNAVAILABLE, RISC_FILES_DENIED,
    RISC_FILES_IO, RISC_FILES_INVALID, RISC_FILES_CONTEXT, RISC_FILES_NO_SPACE, RISC_FILES_RETAINED, 123456};
  for (const char* operation : operations) {
    for (int32_t result : errors) {
      GenericFixture f; Token file = f.open(), dir = f.directory();
      f.provider.results[operation] = result;
      CHECK(invokeGeneric(f, operation, file, dir) == (result == 123456 ? RISC_FILES_IO : result));
      if (result == RISC_FILES_RETAINED) checkGenericFrozen(f, file, dir);
      else {
        CHECK(!f.adapter.retained()); f.provider.results.clear();
        CHECK(f.adapter.close(file) == RISC_FILES_OK && f.adapter.dirClose(dir) == RISC_FILES_OK);
        CHECK(f.adapter.exitSafe());
      }
    }
  }
  for (bool directory : {false, true}) {
    for (int32_t result : errors) {
      GenericFixture f; Token file = f.open(), dir = f.directory();
      f.provider.results[directory ? "dir_close" : "close"] = result;
      CHECK((directory ? f.adapter.dirClose(dir) : f.adapter.close(file)) == RISC_FILES_RETAINED);
      CHECK(f.provider.handles.size() == 2); checkGenericFrozen(f, file, dir);
    }
  }
  for (bool directory : {false, true}) {
    for (bool tokenOnFailure : {false, true}) {
      GenericFixture f; Token out = 99;
      f.provider.tokenOnFailure = tokenOnFailure; f.provider.successWithoutToken = !tokenOnFailure;
      if (tokenOnFailure) f.provider.results[directory ? "dir_open" : "open"] = RISC_FILES_DENIED;
      CHECK((directory ? f.adapter.dirOpen("/dir", &out) : f.adapter.open("/file", RISC_FILES_READ, &out)) == RISC_FILES_RETAINED);
      CHECK(out == 0 && f.provider.handles.size() == unsigned(tokenOnFailure));
      checkGenericFrozen(f);
    }
  }
}

void testGenericMalformedResultsAndTransfers() {
  activeTest = "direct provider malformed result buffers and bounded legal short transfers";
  GenericFixture f; auto& a = f.adapter; auto& p = f.provider;
  Token file = f.open(), dir = f.directory(); uint32_t count = 99; char bytes[16]{}; auto st = statBuffer(); auto entry = entryBuffer();
  p.badStatSize = true; CHECK(a.stat("/file", &st) == RISC_FILES_IO); p.badStatSize = false;
  p.badEntrySize = true; CHECK(a.dirNext(dir, &entry) == RISC_FILES_IO); p.badEntrySize = false;
  p.badEntryName = true; CHECK(a.dirNext(dir, &entry) == RISC_FILES_IO); p.badEntryName = false;
  CHECK(!a.retained());
  p.shortTransfer = true;
  CHECK(a.read(file, bytes, 6, &count) == RISC_FILES_OK && count == 3 && std::string(bytes, count) == "abc");
  CHECK(a.write(file, "WXYZ", 4, &count) == RISC_FILES_OK && count == 2);
  CHECK(p.files.at("/file").bytes == "abcWXf");
  p.results["write"] = RISC_FILES_IO; p.transferOnError = true;
  CHECK(a.write(file, "1234", 4, &count) == RISC_FILES_IO && count == 2);
  p.results["read"] = RISC_FILES_IO;
  CHECK(a.seek(file, 0) == RISC_FILES_OK);
  CHECK(a.read(file, bytes, 4, &count) == RISC_FILES_IO && count == 2);
  CHECK(!a.retained()); p.results.clear(); p.shortTransfer = false;
  CHECK(a.close(file) == RISC_FILES_OK && a.dirClose(dir) == RISC_FILES_OK);
  for (bool write : {false, true}) {
    GenericFixture oversized; file = oversized.open(); dir = oversized.directory();
    oversized.provider.oversizedTransfer = true;
    CHECK((write ? oversized.adapter.write(file, bytes, 4, &count) : oversized.adapter.read(file, bytes, 4, &count)) == RISC_FILES_RETAINED);
    CHECK(count == 0); checkGenericFrozen(oversized, file, dir);
  }
}

void testGenericInstancesAndCapacity() {
  activeTest = "independent direct providers, mapped handles and bounded slots";
  GenericFixture first, second; Token a = first.open(), b = second.open(); uint32_t count = 99; char bytes[8]{};
  CHECK(first.provider.handles.begin()->first == second.provider.handles.begin()->first);
  CHECK(first.adapter.write(a, "FIRST!", 6, &count) == RISC_FILES_OK);
  CHECK(second.adapter.read(b, bytes, 6, &count) == RISC_FILES_OK && std::string(bytes, count) == "abcdef");
  CHECK(first.provider.files.at("/file").bytes == "FIRST!");
  CHECK(second.provider.files.at("/file").bytes == "abcdef");
  first.provider.results["sync"] = RISC_FILES_RETAINED;
  CHECK(first.adapter.sync(a) == RISC_FILES_RETAINED); checkGenericFrozen(first, a);
  CHECK(!second.adapter.retained() && second.adapter.sync(b) == RISC_FILES_OK);
  CHECK(second.adapter.close(b) == RISC_FILES_OK && second.adapter.exitSafe());
  CHECK(first.provider.handles.size() == 1 && second.provider.handles.empty());

  GenericFixture capacity; std::vector<Token> tokens;
  for (unsigned i = 0; i < Adapter::HandleCapacity; ++i) tokens.push_back(i % 2 ? capacity.directory() : capacity.open());
  const unsigned before = capacity.provider.totalCalls(); Token out = 99;
  CHECK(capacity.adapter.open("/file", RISC_FILES_READ, &out) == RISC_FILES_BUSY && out == 0);
  CHECK(capacity.adapter.dirOpen("/dir", &out) == RISC_FILES_BUSY && out == 0);
  CHECK(capacity.provider.totalCalls() == before);
  CHECK(capacity.adapter.close(tokens[0]) == RISC_FILES_OK); Token fresh = capacity.open(); CHECK(fresh != tokens[0]);
  CHECK(capacity.adapter.close(tokens[0]) == RISC_FILES_INVALID && capacity.adapter.close(fresh) == RISC_FILES_OK);
  for (unsigned i = 1; i < tokens.size(); ++i)
    CHECK((i % 2 ? capacity.adapter.dirClose(tokens[i]) : capacity.adapter.close(tokens[i])) == RISC_FILES_OK);
  CHECK(capacity.adapter.exitSafe());
}

} // namespace

int main() {
  testBinding();
  testFileCrud();
  testDirectories();
  testValidationAndAccess();
  testCapacity();
  testErrorClassification();
  testTransferAndHandleFailures();
  testRetention();
  testPostCallStateFences();
  testGenericBindingAndValidation();
  testGenericCrudAndDirectories();
  testGenericErrorsAndRetention();
  testGenericMalformedResultsAndTransfers();
  testGenericInstancesAndCapacity();
  std::printf("storage files adapter: %u checks; generic + legacy CRUD, directories/cursors, errors, access, capacity, admission and sticky custody PASS\n", checks);
}
