#pragma once
#include "NativeUsbTest.h"
inline void usb_serial_jtag_ll_enable_bus_clock(bool enable){
  assert(NativeUsbTest::rccDepth==1);SYSTEM.perip_clk_en1.usb_device_clk_en=NativeUsbTest::failure==NativeUsbTest::Clock?0:enable;
}
inline void usb_serial_jtag_ll_reset_register(){
  assert(NativeUsbTest::rccDepth==1 && !NativeUsbTest::installed);++NativeUsbTest::resets;
  SYSTEM.perip_rst_en1.usb_device_rst=NativeUsbTest::failure==NativeUsbTest::Reset;
  USB_SERIAL_JTAG={};
}
inline bool usb_serial_jtag_ll_module_is_enabled(){
  assert(NativeUsbTest::rccDepth==1);return SYSTEM.perip_clk_en1.usb_device_clk_en && !SYSTEM.perip_rst_en1.usb_device_rst;
}
inline void usb_serial_jtag_ll_phy_enable_pad(bool enable){
  ++NativeUsbTest::padChanges;
  USB_SERIAL_JTAG.conf0.usb_pad_enable=NativeUsbTest::failure==NativeUsbTest::DetachPad?1:NativeUsbTest::failure==NativeUsbTest::AttachPad?0:enable;
}
inline bool usb_serial_jtag_ll_phy_is_pad_enabled(){return USB_SERIAL_JTAG.conf0.usb_pad_enable;}
inline void usb_serial_jtag_ll_phy_enable_external(bool enable){
  ++NativeUsbTest::routeChanges;USB_SERIAL_JTAG.conf0.phy_sel=enable;
  RTCCNTL.usb_conf.sw_hw_usb_phy_sel=1;
  RTCCNTL.usb_conf.sw_usb_phy_sel=NativeUsbTest::failure==NativeUsbTest::Route?1:enable;
}
inline void usb_serial_jtag_ll_phy_disable_pull_override(){USB_SERIAL_JTAG.conf0.pad_pull_override=NativeUsbTest::failure==NativeUsbTest::Pull;}
