#include "ports/esp32s3/SleepDiagnostics.h"
#include <Arduino.h>
#include <cassert>
#include <iostream>
using namespace RiscDiagnostics;
int main(){
 start();const auto calls=Serial.calls;
 assert(checkpoint("app.elf",7,"before exit",11)==0);assert(Serial.calls==calls);
 task=reinterpret_cast<void*>(2);assert(checkpoint("wrong.elf",8,"wrong",5)==-1);task=reinterpret_cast<void*>(1);
 diagnosticIsr=true;assert(checkpoint("isr.elf",8,"wrong",5)==-1);diagnosticIsr=false;
 for(unsigned i=0;i<100;++i)line("chatter");
 Serial.connected=true;Serial.input="diag\n";for(unsigned i=0;i<300;++i)poll();
 assert(Serial.output.find("text=before exit")!=std::string::npos&&Serial.output.find("text=app.elf")!=std::string::npos);
 Serial.output.clear();start();Serial.input="diag\n";for(unsigned i=0;i<300;++i)poll();assert(Serial.output.find("checkpoint")==std::string::npos);
 std::cout<<"Native checkpoint owner/ISR, no-Serial capture, chatter isolation, replay and reset lifetime PASS\n";
}
