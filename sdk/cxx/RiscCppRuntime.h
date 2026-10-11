#pragma once
/* Local linked SDK support, not a retained cross-module callback. Install only
 * for this ELF invocation; a failure handler must not return. */
extern "C" void risc_cpp_set_failure_handler(void(*)());
