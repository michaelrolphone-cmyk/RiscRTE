#pragma once
#include "bootstrap/Json.h"
#include <fstream>
#include <iterator>
#include <string>
namespace RiscTest {
// Fixture construction/readback only. Runtime behavior tests must call the
// production reader with an explicit provider and inject failures there.
inline bool readFixtureJson(const char* path,JsonDocument& doc){
  std::ifstream input(path,std::ios::binary);if(!input)return false;
  const std::string bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
  return !input.bad() && bytes.size()<=65536 && RiscBoot::parse(bytes.data(),bytes.size(),doc);
}
}
