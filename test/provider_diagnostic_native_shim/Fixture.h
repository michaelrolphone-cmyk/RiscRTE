#pragma once
// Native-only state for the host diagnostic composition fixture. Target SDK
// services and transport are doubles; the graph, loader and sink remain real.
#include <cstddef>
#include <string>
#include <freertos/task.h>
inline TaskHandle_t task=reinterpret_cast<void*>(1);
inline bool diagnosticIsr=false;
namespace ProviderDiagnosticTest {
struct ConsoleState {
  bool connected=false;
  size_t space=128,writes=0;
  std::string output;
};
inline ConsoleState console;
}
