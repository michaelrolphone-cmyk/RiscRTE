#pragma once
#include "bootstrap/Board.h"
namespace RiscCpu {
// Shared by real boot and side-effect-free provisioning admission. These are
// native-port reservations, never product pin assignments from a profile.
inline void reserveNativePins(RiscBoot::Board& board){
 for(int p=22;p<=37;++p)board.reservePin(p);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
 board.reservePin(19);board.reservePin(20);
#else
 board.reservePin(43);board.reservePin(44);
#endif
}
}
