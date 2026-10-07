#include "ports/esp32s3/SleepDiagnostics.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <iostream>
using namespace RiscDiagnostics;
static std::string dump(){Serial.output.clear();Serial.input="diag\n";for(unsigned i=0;i<200;++i)poll();assert(Serial.output.find("RTE_DIAG end\n")!=std::string::npos);return Serial.output;}
int main(){
  start();assert(Serial.timeout==0);line("unplugged");lightEnter();lightReturn(-1,0);deepEnter();
  Serial.input="diag\n";for(unsigned i=0;i<100;++i)poll();assert(Serial.writes==0&&Serial.reads==0);
  Serial.connected=true;
  Serial.output.clear();line(std::string(200,'x').c_str());assert(Serial.output==std::string(200,'x')+"\n");
  auto text=dump();assert(text.find("event=light-enter")!=std::string::npos&&text.find("event=light-return detail=-1 cause=0")!=std::string::npos&&text.find("event=deep-enter")!=std::string::npos);
  // No serial or journal mutation is admitted outside the single runtime owner.
  task=reinterpret_cast<void*>(2);const auto count=Serial.writes;Serial.input="diag\n";line("WRONG OWNER");lightEnter();lightReturn(0,7);deepEnter();poll();assert(Serial.writes==count&&Serial.input=="diag\n");
  task=reinterpret_cast<void*>(1);text=dump();assert(text.find("WRONG OWNER")==std::string::npos);
  for(int reason:{ESP_RST_SW,ESP_RST_PANIC,ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT,ESP_RST_DEEPSLEEP}){
    resetReason=reason;wake=3;start();text=dump();assert(text.find("event=boot-retained")!=std::string::npos);
  }
  for(int reason:{ESP_RST_UNKNOWN,ESP_RST_POWERON,ESP_RST_EXT,ESP_RST_BROWNOUT}){
    resetReason=reason;start();text=dump();assert(text.find("events=1 messages=0")!=std::string::npos&&text.find("event=boot-fresh")!=std::string::npos);
  }
  Serial.space=0;const auto before=Serial.writes;line("backpressure");Serial.input="diag\n";poll();assert(Serial.writes==before);
  Serial.connected=false;poll();Serial.connected=true;Serial.space=256;text=dump();assert(text.find("backpressure")!=std::string::npos);
  Serial.input=std::string(40,'x');const auto reads=Serial.reads;poll();assert(Serial.reads-reads==16);
  std::cout<<"native diagnostic adapter: zero-timeout USB, absent transport, ownership, bounded RX/TX, reset policy, backpressure and reconnect passed\n";
}
