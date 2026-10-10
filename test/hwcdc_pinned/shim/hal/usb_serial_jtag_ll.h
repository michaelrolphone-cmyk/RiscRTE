#pragma once
#include "sdk.h"
inline void usb_serial_jtag_ll_enable_bus_clock(bool enabled){SYSTEM.perip_clk_en1.usb_device_clk_en=Stub::failure==Stub::UsbClock?0:enabled;}
inline void usb_serial_jtag_ll_reset_register(){
 ++Stub::usbResets;SYSTEM.perip_rst_en1.usb_device_rst=Stub::failure==Stub::UsbReset;
 USB_SERIAL_JTAG={};Stub::intrStatus=Stub::intrMask=0;
}
inline void usb_serial_jtag_ll_enable_pad(bool enabled){USB_SERIAL_JTAG.conf0.usb_pad_enable=Stub::failure==Stub::UsbPad?1:enabled;}
inline bool usb_serial_jtag_ll_module_is_enabled(){return SYSTEM.perip_clk_en1.usb_device_clk_en&&!SYSTEM.perip_rst_en1.usb_device_rst;}
