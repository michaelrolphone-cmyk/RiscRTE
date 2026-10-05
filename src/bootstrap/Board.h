#pragma once
#include "Json.h"
#include <RiscHardwareConfigV1.h>
#include <TWatchHardwareV1.h>
namespace RiscBoot {
class Board final {
 public:
  static constexpr size_t MaxDevices=64, MaxBuses=8;
  // Private decoded metadata, never the serialized/shared hardware envelope.
  // Board::load validates1..INT32_MAX before assigning this32-bit value.
  struct Binding { char capability[96]{}; uint32_t instance=0; };
  static_assert(sizeof(Binding)==100,"Binding metadata padding regression");
  static_assert(sizeof(((risc_hardware_device_v1*)0)->instance_id)==8,"Shared hardware ID remains64-bit");
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
      tw_hw_gpio_controller_v1 gpioController;
      tw_hw_i2c_controller_v1 i2cController;
      tw_hw_i2c_device_v1 peripheral;
      tw_hw_axp2101_v1 power;
      tw_hw_audio_v1 audio;
      tw_hw_lora_v1 lora;
      Config() : display{} {}
    } config;
    Binding bindings[16]{};
    size_t bindingCount=0;
  };
  Board()=default;
  Board(const Board&)=delete;
  Board& operator=(const Board&)=delete;
  // Explicit owner-authorized mappings for original catalogs lacking metadata.
  bool port(JsonObjectConst declaration);
  bool load(JsonObjectConst root);
  int physicalController(uint64_t busId) const;
  uint64_t deviceBus(uint64_t deviceId) const;
  const Device* device(uint64_t id) const;
  size_t deviceCount() const { return devicesCount_; }
  const Device* deviceAt(size_t i) const { return i<devicesCount_?&devices_[i]:nullptr; }
  const risc_hw_bus_v1* bus(uint64_t id) const;
  const char* error() const { return error_; }
  const risc_hardware_board_identity_v2* identity() const { return &identity_; }
  // Target reserves flash/PSRAM and boot diagnostic pads before parsing.
  bool reservePin(int pin);
 private:
  bool fail(const char* s) { snprintf(error_,sizeof(error_),"%s",s); return false; }
  bool claim(int16_t pin, bool optional=false);
  bool materialize(JsonObjectConst c, Device& d);
  bool i2cDevice(JsonObjectConst c, tw_hw_i2c_device_v1& out);
  bool address(uint64_t bus, uint8_t value);
  struct Mapping { uint64_t bus=0; uint32_t physical=0; bool logical=false; };
  Mapping mappings_[MaxBuses]{};
  size_t mappingCount_=0;
  uint32_t physical_[MaxBuses]{};
  struct Address { uint64_t bus; uint8_t value; } addresses_[MaxDevices]{};
  size_t addressCount_=0;
  bool portSet_=false;
  char boardId_[96]{}, revision_[96]{}, error_[128]{};
  risc_hardware_board_identity_v2 identity_{};
  risc_hw_bus_v1 buses_[MaxBuses]{};
  Device devices_[MaxDevices]{};
  size_t busesCount_=0, devicesCount_=0;
  bool pins_[49]{}, loaded_=false;
};
}
