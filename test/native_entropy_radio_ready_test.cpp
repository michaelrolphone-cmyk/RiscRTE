#include "ports/esp32s3/NativeEntropyRadioReady.h"
#include <cassert>
#include <cstdio>
struct WifiState {
 bool wifi=false,startAttempted=false,started=false,closing=false,established=false;
};
int main(){
 WifiState wifi;
 bool ble=false;unsigned checks=0;
 const auto healthy=[&](){++checks;return ble;};
 assert(!RiscCpu::nativeEntropyRadioReady(false,wifi,healthy));assert(!checks);
 assert(!RiscCpu::nativeEntropyRadioReady(true,wifi,healthy));
 // A fully enabled Bluetooth controller is sufficient with Wi-Fi/RF initially off.
 ble=true;assert(RiscCpu::nativeEntropyRadioReady(true,wifi,healthy));
 ble=false;assert(!RiscCpu::nativeEntropyRadioReady(true,wifi,healthy));
 // Failed/attempted initialization never qualifies as a main entropy source.
 wifi.wifi=true;wifi.startAttempted=true;
 assert(!RiscCpu::nativeEntropyRadioReady(true,wifi,healthy));
 // A successfully started Wi-Fi interface need not be associated with a router.
 wifi.started=true;assert(!wifi.established);
 assert(RiscCpu::nativeEntropyRadioReady(true,wifi,healthy));
 wifi.closing=true;assert(!RiscCpu::nativeEntropyRadioReady(true,wifi,healthy));
 ble=true;assert(RiscCpu::nativeEntropyRadioReady(true,wifi,healthy));
 const unsigned before=checks;
 assert(!RiscCpu::nativeEntropyRadioReady(false,wifi,healthy));assert(checks==before);
 puts("Entropy RF readiness: initially off, healthy BLE-only, unassociated Wi-Fi, failed start, cleanup and owner lease PASS");
}
