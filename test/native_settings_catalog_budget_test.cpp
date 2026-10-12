// Reuse the existing synthetic SD catalog allocation cases only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main catalog_fault_matrix_main
#include "native_settings_catalog_test.cpp"
#undef main
#pragma GCC diagnostic pop
int main(){memoryBounds();v3CapacityAndPreconstruction();}
