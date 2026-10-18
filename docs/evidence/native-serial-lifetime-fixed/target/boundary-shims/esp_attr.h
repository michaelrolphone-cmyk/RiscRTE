#pragma once
// Compile-only boundary. Not an SDK placement/link qualification.
#define DRAM_ATTR __attribute__((section(".dram1.test")))
#define IRAM_ATTR __attribute__((section(".iram1.test")))
#define EXT_RAM_BSS_ATTR __attribute__((section(".ext_ram.bss.test")))
#define RTC_NOINIT_ATTR __attribute__((section(".rtc_noinit.test")))
