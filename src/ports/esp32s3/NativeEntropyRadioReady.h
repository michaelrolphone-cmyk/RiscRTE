#pragma once
namespace RiscCpu {
// The caller holds the owner-task radio lease across this check and RNG use.
// Enabled RF supplies entropy even before association; a failed start does not.
template<class WifiState,class BluetoothReady>
inline bool nativeEntropyRadioReady(bool leaseReady,const WifiState& wifi,
                                    BluetoothReady bluetoothReady){
 if(!leaseReady)return false;
 return bluetoothReady() || (wifi.wifi && wifi.started && !wifi.closing);
}
}
