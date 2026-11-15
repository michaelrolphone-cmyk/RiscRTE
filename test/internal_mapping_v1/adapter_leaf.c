#include <stdlib.h>
#include "esp_heap_caps.h"
/* Refusal must not allocate/free, use a success stub or access hardware. */
void *heap_caps_malloc(size_t size,uint32_t caps) { (void)size;(void)caps;abort(); }
void heap_caps_free(void *p) { (void)p;abort(); }
