#pragma once
#include "NativeUsbTest.h"
#define PERIPH_RCC_ATOMIC() for(NativeUsbTest::RccGuard _rcc;_rcc.active;_rcc.active=false)
