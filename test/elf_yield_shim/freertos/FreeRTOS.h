#pragma once
#define pdTRUE 1
void yield_test_trace(void);
#define traceISR_EXIT_TO_SCHEDULER() yield_test_trace()
