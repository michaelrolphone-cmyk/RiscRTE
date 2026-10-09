#include "ports/esp32s3/SleepDiagnostics.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <cassert>
#include <string>
static unsigned observed;
static std::string last;
[[maybe_unused]] static bool recurse;
#ifndef TEST_ABSENT_OBSERVER
extern "C" void risc_native_diagnostic_observer(const char* text) {
 ++observed;last=text;
 if(recurse){recurse=false;RiscDiagnostics::line("observer-reentry-must-not-run");}
}
#endif
int main() {
 RiscDiagnostics::start();Serial.space=0;Serial.connected=false;
 RiscDiagnostics::line("startup-before-USB");
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER && !defined(TEST_ABSENT_OBSERVER)
 assert(observed==1&&last=="startup-before-USB"&&Serial.output.empty());
 RISC_STAGE_LOG("provider start begin id=panel");
 assert(observed==2&&last.find("provider start begin id=panel")!=std::string::npos);
 task=reinterpret_cast<void*>(2);RiscDiagnostics::line("foreign");assert(observed==2);
 task=reinterpret_cast<void*>(1);RiscDiagnostics::line(nullptr);assert(observed==2);
 recurse=true;RiscDiagnostics::line("outer");assert(observed==3&&last=="outer");
 Serial.connected=true;Serial.space=256;Serial.output.clear();
 RiscDiagnostics::line("connected-line");assert(observed==4&&last=="connected-line"&&Serial.output=="connected-line\n");
#else
 assert(observed==0);
#endif
 return 0;
}
