#include "runtime/serial/Files.h"
#include "runtime/storage/PosixFiles.h"
#include <cassert>
#include <fstream>
#include <string>
#include <algorithm>
#include <cstdio>
static bool owned=true,safe=true;
static uint32_t now=100;
static unsigned opens=0,writes=0,reads=0,closes=0,syncs=0;
static int fault=0;
static const risc_storage_files_v1* native=nullptr;
static RiscSerial::Files* callbackEndpoint=nullptr;
static bool owner(){return owned;}
static bool ready(){return safe;}
static uint32_t millis(){return now;}
static uint32_t crc32(const std::string&s){uint32_t crc=~0u;for(unsigned char b:s){crc^=b;for(unsigned i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}return ~crc;}
static std::string hex(const std::string&s){const char*d="0123456789abcdef";std::string r;for(unsigned char b:s){r+=d[b>>4];r+=d[b&15];}return r;}
static std::string readFile(const std::string&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
struct Wire {
 RiscSerial::Files& f;unsigned seq=0;std::string last;
 void feed(const std::string&line){for(unsigned char c:line)f.input(c);}
 std::string drain(){std::string out;while(f.outputSize()){size_t n=std::min<size_t>(7,f.outputSize());out.append(f.output(),n);f.consumeOutput(n);}return out;}
 std::string command(const std::string&cmd,int expected=0){last="RTEF1 12345678 "+std::to_string(++seq)+" "+cmd+"\n";feed(last);const unsigned before=opens+writes+reads+closes+syncs;assert(before==opens+writes+reads+closes+syncs);f.checkpoint();auto out=drain();const auto prefix="RTEF1 12345678 "+std::to_string(seq)+" "+std::to_string(expected);if(out.rfind(prefix,0)!=0)fprintf(stderr,"cmd=%s expected=%s actual=%s\n",cmd.c_str(),prefix.c_str(),out.c_str());assert(out.rfind(prefix,0)==0 && (out[prefix.size()]==' '||out[prefix.size()]=='\n'));return out;}
 void path(const std::string&p){for(size_t off=0;off<p.size();off+=48)command("PATH "+std::to_string(off)+" "+hex(p.substr(off,48)));}
 void put(const std::string&p,const std::string&data,const char*mode="create"){
  path(p);command("PUT 7 "+std::string(mode)+" "+std::to_string(data.size()));
  uint64_t offset=0;while(offset<data.size()){auto out=command("WRITE "+std::to_string(offset)+" "+hex(data.substr(offset,48)));const auto pos=out.find(" WRITE ");assert(pos!=std::string::npos);auto next=std::stoull(out.substr(pos+7));assert(next>offset&&next<=offset+48);offset=next;}
  char crc[9];snprintf(crc,sizeof(crc),"%08x",crc32(data));command("COMMIT "+std::string(crc));
 }
};
int main(int argc,char**argv){
 assert(argc==3);std::string root=argv[1],scenario=argv[2];
 RiscStorage::PosixFiles provider(root.c_str());native=provider.api();auto api=*native;
 api.open=[](void*c,const char*p,uint32_t f,uint32_t*h){++opens;return native->open(c,p,f,h);};
 api.write=[](void*c,uint32_t h,const void*b,uint32_t n,uint32_t*a){++writes;
  if(callbackEndpoint){const unsigned before=writes;callbackEndpoint->checkpoint();assert(writes==before);}
  if(fault==1)return native->write(c,h,b,std::min(n,3u),a);
  if(fault==2){*a=0;return int32_t(RISC_FILES_OK);}
  if(fault==3){int32_t rc=native->write(c,h,b,std::min(n,2u),a);return rc?rc:int32_t(RISC_FILES_NO_SPACE);}
  if(fault==4){*a=0;return int32_t(RISC_FILES_RETAINED);}
  if(fault==5){*a=n+1;return int32_t(RISC_FILES_OK);}
  int32_t rc=native->write(c,h,b,n,a);if(fault==6)safe=false;return rc;
 };
 api.read=[](void*c,uint32_t h,void*b,uint32_t n,uint32_t*a){++reads;return native->read(c,h,b,fault==1?std::min(n,3u):n,a);};
 api.close=[](void*c,uint32_t h){++closes;return fault==7?int32_t(RISC_FILES_IO):native->close(c,h);};
 api.sync=[](void*c,uint32_t h){++syncs;return fault==8?int32_t(RISC_FILES_IO):native->sync(c,h);};
 RiscBoot::RuntimeFiles files,missing;assert(files.bind(&api,owner,ready));
 RiscSerial::Namespace map[]={{7,"Selected files",&files,true},{8,"Read only",&files,false},{9,"Absent",&missing,true}};
 RiscSerial::Files endpoint;callbackEndpoint=&endpoint;assert(endpoint.bind(map,3,{owner,ready,millis}));assert(!endpoint.bind(map,3,{owner,ready,millis}));
 Wire wire{endpoint};assert(wire.command("HELLO").find("HELLO 48 511 3")!=std::string::npos);
 wire.command("NS 0");wire.command("NS 3",RISC_FILES_NOT_FOUND);
 std::ofstream(root+"/sibling")<<"untouched";assert(!mkdir((root+"/other-app").c_str(),0700));std::ofstream(root+"/other-app/elf")<<"application";
 wire.path("/selected");wire.command("PUT 9 create 3",RISC_FILES_UNAVAILABLE);wire.command("PUT 99 create 3",RISC_FILES_NOT_FOUND);wire.command("PUT 8 create 3",RISC_FILES_DENIED);
 if(scenario=="normal"){
  std::string data;for(unsigned i=0;i<779;++i)data+=char(i);fault=1;wire.put("/selected",data);assert(readFile(root+"/selected")==data);
  wire.path("/selected");wire.command("PUT 7 create 1",RISC_FILES_EXISTS);wire.put("/selected","tiny","replace");assert(readFile(root+"/selected")=="tiny");
  // Exact duplicate PUT never repeats truncation; changed replay is refused.
  wire.path("/selected");wire.command("PUT 7 replace 3");unsigned count=opens;wire.feed(wire.last);endpoint.checkpoint();assert(wire.drain().find(" 0 PUT\n")!=std::string::npos&&opens==count);
  wire.feed("RTEF1 12345678 "+std::to_string(wire.seq)+" PUT 7 replace 99\n");endpoint.checkpoint();assert(wire.drain().find(" -10\n")!=std::string::npos&&opens==count);
  wire.command("WRITE 1 616263",RISC_FILES_INVALID);wire.command("WRITE 0 616263");wire.command("COMMIT 00000000",-11);wire.command("WRITE 3 00",RISC_FILES_INVALID);wire.command("ABORT");assert(readFile(root+"/selected")=="abc");
  wire.path("/selected");assert(wire.command("GET 7").find("GET 3")!=std::string::npos);assert(wire.command("READ 0").find("READ 3 616263")!=std::string::npos);auto response=wire.last;count=reads;wire.feed(response);endpoint.checkpoint();assert(wire.drain().find("616263")!=std::string::npos&&reads==count);assert(wire.command("CLOSE").find("352441c2")!=std::string::npos);
  wire.put("/empty","");assert(readFile(root+"/empty").empty());
  wire.path("/folder");wire.command("MKDIR 7");wire.command("PUT 7 replace 1",RISC_FILES_DENIED);wire.path("/../bad");wire.command("PUT 7 create 1",RISC_FILES_INVALID);wire.path("/bad//name");wire.command("GET 7",RISC_FILES_INVALID);wire.path("/");wire.command("PUT 7 replace 0",RISC_FILES_INVALID);
  // Oversize, control, malformed hex, and invalid UTF-8 must not reach storage.
  count=opens;wire.feed("RTEF1 "+std::string(230,'x')+"\n");endpoint.checkpoint();assert(wire.drain().empty());wire.command("PATH 0 ff");wire.command("GET 7",RISC_FILES_INVALID);wire.command("PATH 0 00",RISC_FILES_INVALID);assert(opens==count);
  // Receive and non-owner checkpoints cannot dispatch. Safe owner may later.
  wire.path("/selected");wire.last="RTEF1 12345678 "+std::to_string(++wire.seq)+" GET 7\n";count=opens;wire.feed(wire.last);assert(opens==count);owned=false;endpoint.checkpoint();assert(opens==count);owned=true;safe=false;endpoint.checkpoint();assert(opens==count);safe=true;endpoint.checkpoint();assert(opens==count+1);wire.drain();
  // Disconnect only latches cancellation; close occurs at the owner checkpoint.
  count=closes;endpoint.disconnect();assert(closes==count&&!endpoint.exitSafe());endpoint.checkpoint();assert(closes==count+1&&endpoint.exitSafe());
  wire.seq=0;wire.command("HELLO");wire.path("/selected");wire.command("PUT 7 replace 8");wire.command("WRITE 0 6162");count=closes;now+=RiscSerial::Files::TimeoutMs;endpoint.checkpoint();assert(closes==count+1&&endpoint.exitSafe());assert(readFile(root+"/selected")=="ab");
 }else{
  wire.path("/selected");wire.command("PUT 7 create 4");
  if(scenario=="zero"){fault=2;wire.command("WRITE 0 61626364",RISC_FILES_IO);fault=0;wire.command("ABORT");assert(endpoint.exitSafe());}
  else if(scenario=="full"){fault=3;assert(wire.command("WRITE 0 61626364",RISC_FILES_NO_SPACE).find("WRITE 2")!=std::string::npos);fault=0;wire.command("ABORT");assert(readFile(root+"/selected")=="ab");}
  else if(scenario=="sync"){wire.command("WRITE 0 61626364");fault=8;wire.command("COMMIT ed82cd11",RISC_FILES_IO);assert(syncs==1&&closes==0);fault=0;wire.command("ABORT");}
  else {fault=scenario=="retained"?4:scenario=="invalid-count"?5:scenario=="lost-owner"?6:7;
   if(fault==7)wire.command("ABORT",RISC_FILES_RETAINED);else wire.command("WRITE 0 61626364",RISC_FILES_RETAINED);
   assert(!endpoint.custodySafe()&&!endpoint.exitSafe());safe=true;unsigned count=closes+writes;wire.command("ABORT",RISC_FILES_RETAINED);endpoint.disconnect();endpoint.checkpoint();assert(!endpoint.shutdown()&&closes+writes==count);
  }
 }
 assert(readFile(root+"/sibling")=="untouched"&&readFile(root+"/other-app/elf")=="application");
 printf("Serial files %s PASS (endpoint bytes=%zu)\n",scenario.c_str(),sizeof(endpoint));
}
