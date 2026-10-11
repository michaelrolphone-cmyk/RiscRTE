#pragma once
#include <stdint.h>

// Loader-only adapters, not ordinary libc imports. Admission requires the
// matching per-provider diagnostic ABI policy and an available native sink.
#ifdef __cplusplus
extern "C" {
#endif
extern const uint32_t risc_provider_diagnostic_build_abi_v1;
uint32_t risc_provider_diagnostic_abi_v1(void);
int risc_provider_diagnostic_printf(const char* format,...);
int risc_provider_diagnostic_puts(const char* text);
int risc_provider_diagnostic_putchar(int value);
#ifdef __cplusplus
}
#endif

// Calls emit one completed record, retaining no partial lines. printf/puts
// return the accepted rendered byte count (0..255), excluding the sink's LF;
// putchar returns its unsigned-char input. Rejection returns -1. Transport
// loss does not change acceptance. Disabled/non-owner/ISR/reentrant calls
// reject before accessing caller pointers. These are not libc return values.
//
// Format strings require a NUL within 256 bytes; strings are read at most 256
// bytes (or an explicit precision). Output truncates to 252 bytes plus "...".
// One final LF or CRLF is removed, interior CR/LF/TAB become spaces, and other
// ASCII controls/DEL become '?'. Non-ASCII bytes are preserved. putchar LF is
// an empty record, CR/TAB a space, and other controls '?'.
//
// Supported: d/i/u/o/x/X with hh/h/l/ll/z/t/j; c/s/p and %%. Integer flags
// -+ #0, width and precision follow printf semantics; pointer formatting is
// lowercase 0x hex (including 0x0), with -/#/0, width and precision supported.
// Strings support -/width/precision; characters support -/width. Each width
// and precision is at most 64; dynamic values must be within [-64,64], with
// negative precision omitted. At most 32 conversions (including %%) per call.
// Floats, %n, positional arguments and malformed formats reject as a whole.
