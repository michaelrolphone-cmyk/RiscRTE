// Pipe-only executable: exercise the production endpoint and provider, never a
// serial device. Fault seams are bounded generic-ABI callbacks, not a fake wire
// implementation. Each Python test launches a fresh process and private root.
#include "runtime/serial/Files.h"
#include "runtime/storage/PosixFiles.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace {
bool yes(){return true;}
uint32_t ticks(){
  using namespace std::chrono;
  return uint32_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
using Write = int32_t (*)(void*,uint32_t,const void*,uint32_t,uint32_t*);
using Read = int32_t (*)(void*,uint32_t,void*,uint32_t,uint32_t*);
Write realWrite=nullptr;
Read realRead=nullptr;
int32_t shortWrite(void*c,uint32_t h,const void*p,uint32_t n,uint32_t*a){return realWrite(c,h,p,std::min(n,7u),a);}
int32_t shortRead(void*c,uint32_t h,void*p,uint32_t n,uint32_t*a){return realRead(c,h,p,std::min(n,11u),a);}
int32_t failedWrite(void*c,uint32_t h,const void*p,uint32_t n,uint32_t*a){
  const int32_t rc=realWrite(c,h,p,std::min(n,5u),a);
  return rc?rc:int32_t(RISC_FILES_IO);
}
int32_t failedSync(void*,uint32_t){return RISC_FILES_IO;}
int32_t zeroWrite(void*,uint32_t,const void*,uint32_t,uint32_t*a){*a=0;return RISC_FILES_OK;}
}

int main(int argc,char**argv){
  if(argc<2 || argc>3){std::fprintf(stderr,"usage: serial_files_wire ROOT [diagnostics]\n");return 2;}
  const bool diagnostics=argc==3 && !std::strcmp(argv[2],"diagnostics");
  RiscStorage::PosixFiles backing(argv[1]);
  RiscBoot::RuntimeFiles files,unavailable,shortFiles,failedFiles,syncFiles,zeroFiles;
  auto shortTable=*backing.api(),failedTable=*backing.api(),syncTable=*backing.api(),zeroTable=*backing.api();
  realWrite=shortTable.write;realRead=shortTable.read;
  shortTable.write=shortWrite;shortTable.read=shortRead;
  failedTable.write=failedWrite;syncTable.sync=failedSync;zeroTable.write=zeroWrite;
  if(!files.bind(backing.api(),yes,yes) || !shortFiles.bind(&shortTable,yes,yes) ||
     !failedFiles.bind(&failedTable,yes,yes) || !syncFiles.bind(&syncTable,yes,yes) ||
     !zeroFiles.bind(&zeroTable,yes,yes))return 3;
  const RiscSerial::Namespace namespaces[]{
    {1,"fixture",&files,true},{2,"read-only fixture",&files,false},
    {3,"unavailable fixture",&unavailable,true},{4,"short I/O fixture",&shortFiles,true},
    {5,"write failure fixture",&failedFiles,true},{6,"sync failure fixture",&syncFiles,true},
    {7,"zero write fixture",&zeroFiles,true}
  };
  RiscSerial::Files endpoint;
  if(!endpoint.bind(namespaces,sizeof(namespaces)/sizeof(namespaces[0]),{yes,yes,ticks}))return 4;
  int byte=0;
  while((byte=std::getchar())!=EOF){
    endpoint.input(uint8_t(byte));
    endpoint.checkpoint();
    if(endpoint.outputSize() && diagnostics){
      std::fputs("runtime: settled checkpoint\r\n",stdout);
      for(unsigned i=0;i<300;++i)std::fputc('d',stdout);
      std::fputc('\n',stdout);
    }
    while(endpoint.outputSize()){
      const size_t count=std::min(endpoint.outputSize(),size_t(7));
      if(std::fwrite(endpoint.output(),1,count,stdout)!=count)return 5;
      endpoint.consumeOutput(count);
    }
    std::fflush(stdout);
  }
  const bool clean=endpoint.shutdown();
  if(!clean || !files.exitSafe() || !unavailable.exitSafe() || !shortFiles.exitSafe() ||
     !failedFiles.exitSafe() || !syncFiles.exitSafe() || !zeroFiles.exitSafe() || !backing.exitSafe()){
    std::fputs("wire fixture retained custody on shutdown\n",stderr);return 6;
  }
  return 0;
}
