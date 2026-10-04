#pragma once
#include <stdint.h>
uint32_t millis();
struct FakeEsp {uint32_t getFlashChipSize() const;void restart();};
extern FakeEsp ESP;
