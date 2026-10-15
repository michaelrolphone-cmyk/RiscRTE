#pragma once
// The real UART header has an inline argument-check helper that logs errors.
// Logging is not part of this test; retain the SDK error/check declarations.
#define ESP_LOGE(...) ((void)0)
