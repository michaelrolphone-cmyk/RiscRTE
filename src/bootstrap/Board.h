#pragma once
#include "Json.h"
#include <RiscHardwareConfigV1.h>
namespace RiscBoot {
class Board final {
 public:
  static constexpr size_t MaxDevices=64, MaxBuses=8;
  struct Binding { char capability[96]{}; uint64_t instance=0; };
  struct Device {
    char compatible[96]{}, revision[96]{}, type[96]{};
    risc_hardware_device_v1 hardware{};
    union Config {
      risc_hw_gpio_bank_v1 gpio;
      risc_hw_quadrature_v1 quadrature;
      risc_hw_pixel_v1 pixel;
      risc_hw_spi_display_v1 display;
      risc_hw_i2c_touch_v1 touch;
      risc_hw_sd_spi_v1 sd;
      risc_hw_radio_v1 radio;
      Config() : display{} {}
    } config;
    Binding bindings[16]{};
    size_t bindingCount=0;
  };
  Board()=default;
  Board(const Board&)=delete;
  Board& operator=(const Board&)=delete;
  bool load(JsonObjectConst root);
  const Device* device(uint64_t id) const;
  const risc_hw_bus_v1* bus(uint64_t id) const;
  const char* error() const { return error_; }
  const risc_hardware_board_identity_v2* identity() const { return &identity_; }
  // Target reserves flash/PSRAM and boot diagnostic pads before parsing.
  bool reservePin(int pin);
 private:
  bool fail(const char* s) { snprintf(error_,sizeof(error_),"%s",s); return false; }
  bool claim(int16_t pin, bool optional=false);
  bool materialize(JsonObjectConst c, Device& d);
  char boardId_[96]{}, revision_[96]{}, error_[128]{};
  risc_hardware_board_identity_v2 identity_{};
  risc_hw_bus_v1 buses_[MaxBuses]{};
  Device devices_[MaxDevices]{};
  size_t busesCount_=0, devicesCount_=0;
  bool pins_[49]{}, loaded_=false;
};
}
