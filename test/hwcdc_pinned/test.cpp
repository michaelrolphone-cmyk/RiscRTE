#include "vendor/HWCDC.cpp"
#include "ports/esp32s3/SleepDiagnostics.h"
#include <iostream>
using namespace RiscDiagnostics;
static void host(bool present){Stub::host=present;s_usb_serial_jtag_conn_status=present;connected=present;}
static void inject(const char* text){for(;*text;++text)Stub::hostRx.push_back(uint8_t(*text));Stub::intrStatus|=USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT;assert(Stub::isr);Stub::isr(nullptr);}
static void serviceTx(){if(Stub::isr && (Stub::intrMask&USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY)){Stub::intrStatus|=USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY;Stub::isr(nullptr);}}
static bool initialized(){return Serial.available()>=0 && Serial.availableForWrite()>0;}
static void clean(){Serial.end();assert(Stub::live==0);Stub::failure=Stub::None;host(false);}
int main(){
 usb_serial_jtag_conn_status_init();
 // Actual pinned begin/end: public readiness exactly catches all silent failures.
 for(auto f:{Stub::None,Stub::Mutex,Stub::Rx,Stub::Tx,Stub::Interrupt}){
  clean();Stub::failure=f;auto frees=Stub::interruptFrees,allocs=Stub::interruptAllocations;
  Serial.begin(115200);Serial.setTxTimeoutMs(0);
  assert(initialized()==(f==Stub::None));
  assert(Stub::interruptAllocations==allocs+1);
  assert(Stub::interruptFrees==frees+(f==Stub::Interrupt?1:0));
  assert(Stub::live<=4);
  if(f==Stub::None){assert(tx_ring_buf && tx_ring_buf->capacity==256);assert(USB_SERIAL_JTAG.conf0.pad_pull_override==0&&USB_SERIAL_JTAG.conf0.usb_pad_enable==1);}
  Serial.end();assert(Stub::live==0&&Stub::intrMask==0);
  assert(Stub::pinModeValue[19]==OUTPUT_OPEN_DRAIN&&Stub::pinModeValue[20]==OUTPUT_OPEN_DRAIN);
  assert(Stub::pinLevel[19]==LOW&&Stub::pinLevel[20]==LOW);
 }
 clean();Serial.begin(115200);start();host(true);line("before sleep");serviceTx();
 // Adapter failure recovery must have no busy retries even with actual driver.
 for(auto f:{Stub::None,Stub::Mutex,Stub::Rx,Stub::Tx,Stub::Interrupt}){
  Stub::failure=f;auto allocs=Stub::allocationCalls,frees=Stub::interruptFrees;
  lightReturn(ESP_OK,4);assert(Stub::allocationCalls==allocs&&Stub::interruptFrees==frees);
  line("journal while pending");poll();assert(Stub::interruptFrees==frees+1&&Stub::live==0);
  for(unsigned i=0;i<100;++i)poll();assert(Stub::allocationCalls==allocs);
  Stub::tick+=19;poll();assert(Stub::allocationCalls==allocs);
  Stub::tick+=1;poll();assert(Stub::allocationCalls==allocs+4);
  auto snapshot=Stub::allocationCalls;
  for(unsigned i=0;i<1000;++i){++Stub::tick;poll();line("after attempt");}
  assert(Stub::allocationCalls==snapshot&&Stub::live<=4);
  if(f!=Stub::None){auto pins=Stub::pinChanges,flushes=Stub::flushes;lightReturn(ESP_FAIL,0);poll();line("still failed");assert(Stub::pinChanges==pins&&Stub::flushes==flushes);}
 }
 // A later success admits one bounded new attempt; absent host never delays it.
 Stub::failure=Stub::None;host(false);lightReturn(ESP_OK,4);poll();Stub::tick+=20;poll();assert(initialized());
 // Wrong owner cannot disconnect, initialize, read, write or set recovery state.
 auto frees=Stub::interruptFrees,allocs=Stub::allocationCalls,pins=Stub::pinChanges;
 currentTask=reinterpret_cast<void*>(2);lightReturn(ESP_OK,4);poll();line("wrong owner");deepEnter();
 currentTask=reinterpret_cast<void*>(1);poll();assert(Stub::interruptFrees==frees&&Stub::allocationCalls==allocs&&Stub::pinChanges==pins);
 // New host commands still traverse the actual RX queue/ISR after recovery.
 host(true);Stub::hostTx.clear();inject("diag\n");for(unsigned i=0;i<300;++i){poll();serviceTx();}
#if RISC_SLEEP_DIAGNOSTICS
 assert(Stub::hostTx.find("RTE_DIAG end\n")!=std::string::npos);
#endif
 clean();assert(Stub::delays==0&&Stub::positiveWaits==0);
 std::cout<<"Actual Arduino 2.0.17 HWCDC: begin/end, all four allocation failures, zero-wait adapter recovery, no host, wrong owner, failed-state suppression, explicit retry and RX replay passed\n";
}
