#pragma once
// Reuse the real OpenSSL-backed host SHA adapter for Runtime's mirror digest.
// Keep all other pressure-fixture SDK/allocator stubs unchanged.
#include "../../../native_bank_stubs/mbedtls/sha256.h"
