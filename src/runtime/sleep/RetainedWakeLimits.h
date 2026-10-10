#pragma once
/* Native composition choice. The public legacy record remains 128 bytes. */
#ifndef RISC_RETAINED_WAKE_BYTES
#define RISC_RETAINED_WAKE_BYTES 128
#endif
#if RISC_RETAINED_WAKE_BYTES != 128 && RISC_RETAINED_WAKE_BYTES != 512
#error RISC_RETAINED_WAKE_BYTES must be 128 or 512
#endif
