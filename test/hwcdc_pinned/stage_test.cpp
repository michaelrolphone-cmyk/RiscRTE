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
  // Missing host never fills the ring, waits, or retries initialization.
  fresh();host(false);const auto allocations=Stub::allocationCalls;
  for(unsigned i=0;i<100;++i){line("no-host");poll();}
  assert(tx_ring_buf->data.empty() && Stub::allocationCalls==allocations);
  host(true);poll();drain();assert(Stub::hostTx=="RTE_LOG lost=100 truncated=0\n");
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
  std::cout<<"Actual HWCDC stage TX: 8192-byte preallocation, 6 KiB burst, full-ring loss, no host, recovery capacity, allocation fallback and zero waits PASS\n";
}
