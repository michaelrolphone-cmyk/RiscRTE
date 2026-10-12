#include "bootstrap/Runtime.h"
#include "runtime/storage/FileSourceFiles.h"
extern "C" const unsigned runtime_source_size=sizeof(RiscBoot::Runtime);
extern "C" const unsigned source_api_size=sizeof(risc_file_open_source_api_v1);
extern "C" const unsigned source_info_size=sizeof(risc_file_open_source_info_v1);
extern "C" const unsigned source_backend_size=sizeof(RiscStorage::FileSourceFiles);
