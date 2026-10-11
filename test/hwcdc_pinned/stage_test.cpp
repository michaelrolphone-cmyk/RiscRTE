#include "vendor/HWCDC.cpp"
#include "ports/esp32s3/SleepDiagnostics.h"
#include <iostream>
using namespace RiscDiagnostics;
static void host(bool present){Stub::host=present;s_usb_serial_jtag_conn_status=present;connected=present;}
static void drain(){
  for(unsigned i=0;i<200 && tx_ring_buf && !tx_ring_buf->data.empty();++i){
    Stub::intrStatus|=USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY;assert(Stub::isr);Stub::isr(nullptr);
  }
  assert(tx_ring_buf && tx_ring_buf->data.empty());
}
static void fresh(){
  Serial.end();assert(Stub::live==0);Stub::failure=Stub::None;host(false);
  assert(prepareSerial());assert(tx_ring_buf && tx_ring_buf->capacity==8192);
  assert(!tx_lock); // Allocation precedes the driver's first begin.
  Serial.begin(115200);start();host(true);Stub::hostTx.clear();
  assert(Serial.availableForWrite()==8192);
}
int main(){
  usb_serial_jtag_conn_status_init();fresh();
  // An undrained 6 KiB startup burst fits the actual upstream driver's ring.
  std::string expected;
  for(unsigned i=0;i<30;++i){std::string text="boot-stage-"+std::to_string(i)+" "+std::string(180,'x');line(text.c_str());expected+=text+'\n';}
  assert(Stub::hostTx.empty() && tx_ring_buf->data.size()==expected.size());
  drain();poll();drain();assert(Stub::hostTx==expected);
  // A full ring still drops immediately and reports that loss after draining.
  fresh();const std::string block(127,'f');
  for(unsigned i=0;i<64;++i)line(block.c_str());
  assert(Serial.availableForWrite()==0);line("full-ring-drop");
  assert(tx_ring_buf->data.size()==8192);drain();poll();drain();
  assert(Stub::hostTx.find("full-ring-drop")==std::string::npos);
  assert(Stub::hostTx.find("RTE_LOG lost=1 truncated=0\n")!=std::string::npos);
  // Before the host is ready, complete statements enter the same bounded ring
  // and later emerge automatically. No initialization retries or waits occur.
  fresh();host(false);const auto allocations=Stub::allocationCalls;
  for(unsigned i=0;i<100;++i){line("no-host");poll();}
  assert(tx_ring_buf->data.size()==800 && Stub::allocationCalls==allocations);
  host(true);poll();drain();
  expected.clear();for(unsigned i=0;i<100;++i)expected+="no-host\n";
  assert(Stub::hostTx==expected);
  // The delivered paired/app-data setup emits these 13 statements before its
  // first manifest-prepare endpoint. Reproduce the delayed CDC handshake, with
  // an empty 8 KiB ring: all 13 must precede that endpoint on reconnection.
  fresh();host(false);expected.clear();
  const char* startup[]={
    "RTE_STAGE us=10 usb tx-buffer requested=8192 result=ready",
    "RTE_STAGE us=20 boot begin reset=1 wake=0 setup_start_us=0",
    "RTE_SOURCE=example-source-identity",
    "RTE_STAGE us=30 boot bank-selection begin",
    "RTE_STAGE us=40 boot bank-selection end result=ok elapsed_us=10",
    "RTE_STAGE us=50 boot filesystem-mount begin",
    "RTE_STAGE us=60 boot filesystem-mount end result=0 elapsed_us=10",
    "RTE_STAGE us=70 boot app-data begin",
    "RTE_STAGE us=80 boot app-data end result=ok elapsed_us=10",
    "RTE_STAGE us=90 boot provisioning begin",
    "RTE_STAGE us=100 boot provisioning end action=continue-installed reason=no-input elapsed_us=10",
    "RTE_PROVISION action=continue-installed reason=no-input",
    "RTE_STAGE us=110 boot manifest-prepare begin"};
  static_assert(sizeof(startup)/sizeof(startup[0])==13,"startup statement count");
  for(const char* statement:startup){line(statement);expected+=statement;expected+='\n';}
  assert(tx_ring_buf->data.size()==expected.size() && Serial.availableForWrite()>4096);
  // A plugged host is not yet a connected CDC reader until its first IN event.
  Stub::host=true;s_usb_serial_jtag_conn_status=true;connected=false;
  assert(!bool(Serial));
  Stub::intrStatus|=USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY;Stub::isr(nullptr);
  assert(bool(Serial));
  const char* ready="RTE_STAGE us=3230000 boot manifest-prepare end result=ok";
  line(ready);expected+=ready;expected+='\n';drain();poll();drain();
  assert(Stub::hostTx==expected);
  // While unplugged a full ring preserves the oldest startup bytes; the
  // adapter must never invoke upstream FIFO eviction to make room.
  fresh();host(false);expected.clear();
  for(unsigned i=0;i<64;++i){line(block.c_str());expected+=block+'\n';}
  line("must-not-evict-startup");assert(tx_ring_buf->data.size()==8192);
  host(true);drain();poll();drain();
  assert(Stub::hostTx==expected+"RTE_LOG lost=1 truncated=0\n");
  // Actual end() frees the 8 KiB allocation; recovery restores the same size.
  fresh();lightReturn(ESP_OK,4);poll();assert(!tx_ring_buf && Stub::live==0);
  Stub::tick+=20;poll();assert(tx_ring_buf && tx_ring_buf->capacity==8192);
  host(true);assert(Serial.availableForWrite()==8192);
  // Allocation failure is bounded; next begin retains its ordinary fallback
  // opportunity. No host wait or automatic allocation retry is introduced.
  Serial.end();Stub::failure=Stub::Tx;assert(!prepareSerial());
  Stub::failure=Stub::None;Serial.begin(115200);Serial.setTxTimeoutMs(0);
  assert(tx_ring_buf && tx_ring_buf->capacity==256);
  Serial.end();assert(Stub::live==0 && Stub::delays==0 && Stub::positiveWaits==0);
  std::cout<<"Actual HWCDC stage TX: preallocation, 6 KiB burst, full-ring preservation, delayed-host 13-line startup, no-host boundedness, recovery capacity, allocation fallback and zero waits PASS\n";
}
