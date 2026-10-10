#pragma once
/* Instantiate used templates as PIC in the ELF. The toolchain's static archive
 * contains non-PIC iostream/locale code and must not be linked into a module. */
#include <bits/c++config.h>
#undef _GLIBCXX_EXTERN_TEMPLATE
#define _GLIBCXX_EXTERN_TEMPLATE 0
