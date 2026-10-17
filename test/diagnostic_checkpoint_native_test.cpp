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
 serialControl(0x12345678,5,"STAT",0,false,85);serialControl(0x12345678,5,"STAT",4000,true,85);
 assert(Serial.calls==calls);
 task=reinterpret_cast<void*>(2);serialControl(99,99,"GET",99,false,99);task=reinterpret_cast<void*>(1);
 diagnosticIsr=true;serialControl(99,99,"GET",99,false,99);diagnosticIsr=false;
 for(unsigned i=0;i<100;++i)line("chatter");
 Serial.connected=true;Serial.input="diag\n";for(unsigned i=0;i<300;++i)poll();
 assert(Serial.output.find("text=before exit")!=std::string::npos&&Serial.output.find("text=app.elf")!=std::string::npos);
 assert(Serial.output.find("event=serial-first-identity detail=305419896 cause=5")!=std::string::npos);
 assert(Serial.output.find("event=serial-first-state detail=4 cause=85")!=std::string::npos);
 assert(Serial.output.find("event=serial-deadline-wait detail=4000 cause=0")!=std::string::npos);
 assert(Serial.output.find("detail=99 cause=99")==std::string::npos);
 Serial.output.clear();start();Serial.input="diag\n";for(unsigned i=0;i<300;++i)poll();assert(Serial.output.find("checkpoint")==std::string::npos);
 std::cout<<"Native checkpoint owner/ISR, no-Serial capture, chatter isolation, replay and reset lifetime PASS\n";
}
