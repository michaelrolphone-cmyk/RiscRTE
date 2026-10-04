#pragma once
#include <ArduinoJson.h>
#include "runtime/packages/PackageJsonGuard.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
namespace RiscBoot {
inline bool keys(JsonObjectConst o, std::initializer_list<const char*> required,
                 std::initializer_list<const char*> optional = {}) {
  if (o.isNull()) return false;
  for (const char* k : required) if (o[k].isNull()) return false;
  for (JsonPairConst kv : o) {
    bool known = false;
    for (const char* k : required) if (!strcmp(kv.key().c_str(), k)) known = true;
    for (const char* k : optional) if (!strcmp(kv.key().c_str(), k)) known = true;
    if (!known) return false;
  }
  return true;
}
inline bool utf8(const char* data, size_t size) {
  for (size_t i=0;i<size;) {
    uint8_t c=static_cast<uint8_t>(data[i++]);
    if (c<0x80) { if(!c) return false; continue; }
    unsigned count; uint32_t value, minimum;
    if(c>=0xc2 && c<=0xdf) { count=1;value=c&31;minimum=0x80; }
    else if(c>=0xe0 && c<=0xef) { count=2;value=c&15;minimum=0x800; }
    else if(c>=0xf0 && c<=0xf4) { count=3;value=c&7;minimum=0x10000; }
    else return false;
    if(size-i<count) return false;
    while(count--) { c=static_cast<uint8_t>(data[i++]); if((c&0xc0)!=0x80)return false; value=(value<<6)|(c&63); }
    if(value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff)) return false;
  }
  return true;
}
inline bool text(JsonVariantConst v, char* out, size_t cap) {
  if (!v.is<const char*>()) return false;
  JsonString s = v.as<JsonString>();
  if (!s.size() || s.size() >= cap || strlen(s.c_str()) != s.size() || !utf8(s.c_str(),s.size())) return false;
  memcpy(out, s.c_str(), s.size()+1); return true;
}
inline bool eq(JsonVariantConst v, const char* expected) {
  return v.is<const char*>() && v.as<JsonString>().size() == strlen(expected) && !strcmp(v.as<const char*>(), expected);
}
inline bool integer(JsonVariantConst v, int64_t lo, int64_t hi, int64_t& out) {
  if (!v.is<int64_t>()) return false;
  out = v.as<int64_t>(); return out >= lo && out <= hi;
}
inline bool path(const char* root, const char* relative, char* out, size_t cap) {
  if (!relative || !*relative || relative[0]=='/' || strlen(relative)>192) return false;
  const char* start = relative;
  for (const char* p=relative;;++p) {
    if (*p=='/' || !*p) {
      size_t n=static_cast<size_t>(p-start);
      if (!n || (n==1 && start[0]=='.') || (n==2 && start[0]=='.' && start[1]=='.')) return false;
      if (!*p) break;
      start=p+1;
    } else if (!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p=='_' || *p=='-' || *p=='.')) return false;
  }
  int n=snprintf(out,cap,"%s/%s",root,relative); return n>0 && static_cast<size_t>(n)<cap;
}
inline bool parse(const char* bytes, size_t size, JsonDocument& doc) {
  return RuntimePackages::PackageJsonGuard(bytes,size).objectOnly() &&
         !deserializeJson(doc,bytes,size,DeserializationOption::NestingLimit(10));
}
bool readJson(const char* filename, JsonDocument& doc);
}
