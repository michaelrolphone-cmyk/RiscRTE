#pragma once
/* Compile callers and production dlfcn.c with this header. Do NOT apply it to
 * backend.c: the backend alone must reach the operating system's ELF loader. */
#define dlopen risc_test_target_dlopen
#define dlsym risc_test_target_dlsym
#define dlclose risc_test_target_dlclose
#define dlerror risc_test_target_dlerror
