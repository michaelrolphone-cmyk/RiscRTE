#pragma once
#include <cassert>
#include <cstdint>
#include <esp_err.h>
namespace NativeUsbTest {
enum Failure {None,Clock,Reset,DetachPad,AttachPad,Route,Pull,Install,Uninstall};
inline Failure failure=None;
inline unsigned rccDepth=0,resets=0,padChanges=0,routeChanges=0,installs=0,uninstalls=0,writes=0,observed=0,drained=0;
inline bool installed=false,host=false;
inline int core=0,installedCore=-1;
inline void* task=reinterpret_cast<void*>(1);
inline int64_t now=1000;
struct RccGuard {
  bool active=true;
  RccGuard(){assert(!rccDepth);++rccDepth;}
  ~RccGuard(){assert(rccDepth==1);--rccDepth;}
};
}
struct NativeUsbRegisters {struct{unsigned phy_sel=0,pad_pull_override=0,dp_pullup=0,usb_pad_enable=0;}conf0;};
inline NativeUsbRegisters USB_SERIAL_JTAG;
struct NativeRtcRegisters {struct{unsigned sw_hw_usb_phy_sel=0,sw_usb_phy_sel=0;}usb_conf;};
inline NativeRtcRegisters RTCCNTL;
struct NativeSystemRegisters {struct{unsigned usb_device_clk_en=1;}perip_clk_en1;struct{unsigned usb_device_rst=0;}perip_rst_en1;};
inline NativeSystemRegisters SYSTEM;
