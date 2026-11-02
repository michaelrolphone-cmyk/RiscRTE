#include "bootstrap/RuntimeFileImage.h"
extern "C" bool risc_runtime_image_target_probe(RiscBoot::RuntimeFileImage* image,
    RiscBoot::RuntimeFileView source,const char* path,RiscBoot::RuntimeFileImage::Hooks hooks,
    size_t expected,size_t maximum,bool envelope) {
  return image->read(source,path,hooks,expected,maximum,envelope);
}
