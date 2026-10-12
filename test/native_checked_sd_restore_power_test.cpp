#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main checked_restore_original_main
#include "native_checked_sd_restore_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include <sys/wait.h>
#include <fcntl.h>
static unsigned powerCut=0,powerAt=0;
static std::string snapshotPath;
static void exactWrite(int fd,const void*p,size_t n){const auto*b=static_cast<const uint8_t*>(p);while(n){auto done=F::__real_write(fd,b,n);assert(done>0);n-=size_t(done);b+=done;}}
static void persistNvs(){
 const int fd=::open(snapshotPath.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0);
 auto number=[&](uint32_t n){exactWrite(fd,&n,sizeof n);};auto bytes=[&](const void*p,uint32_t n){number(n);exactWrite(fd,p,n);};
 number(F::nvs.size());for(const auto&space:F::nvs){bytes(space.first.data(),space.first.size());number(space.second.size());for(const auto&row:space.second){bytes(row.first.data(),row.first.size());bytes(row.second.data(),row.second.size());}}
 assert(F::__real_fsync(fd)==0&&F::__real_close(fd)==0);
}
static void restoreNvs(){
 std::ifstream in(snapshotPath,std::ios::binary);assert(in.good());auto number=[&](){uint32_t n=0;in.read(reinterpret_cast<char*>(&n),sizeof n);assert(in.good());return n;};auto bytes=[&](){Bytes b(number());assert(b.size()<1000000);in.read(reinterpret_cast<char*>(b.data()),b.size());assert(in.good());return b;};
 F::nvs.clear();for(uint32_t count=number();count--;){auto name=bytes();std::string space(name.begin(),name.end());auto&rows=F::nvs[space];for(uint32_t count=number();count--;){auto k=bytes(),v=bytes();rows[std::string(k.begin(),k.end())]=v;}}
 assert(in.peek()==EOF);
}
static void powerPoint(const char*){if(++powerAt!=powerCut)return;persistNvs();::_exit(77);}
int main(int argc,char**argv){
 assert(argc==2);F::rootPath=argv[1];snapshotPath=F::rootPath+"-power-nvs";setbuf(stdout,nullptr);unsigned reached=0;
 for(bool resume:{false,true})for(unsigned cut=1;cut<200;++cut){
  reset();Bytes map;C::Identity id;auto card=checkpoint(&map,&id);if(resume)restoreReceipt(card);
  powerCut=cut;powerAt=0;const pid_t pid=::fork();assert(pid>=0);
  if(!pid){F::powerCheckpoint=powerPoint;NativeSd child(card);assert(child.execute()==C::Ok);F::onWorker=true;persistNvs();::_exit(0);}
  int status=0;assert(::waitpid(pid,&status,0)==pid&&WIFEXITED(status));const int result=WEXITSTATUS(status);assert(result==0||result==77);restoreNvs();F::reset();
  {NativeSd reboot(card);F::onWorker=true;const int loaded=reboot.f.core.load();reboot.f.store.finishOperation();F::onWorker=false;if(loaded!=C::Ok){assert(loaded==C::Missing||loaded==C::Initializing);assert(reboot.execute()==C::Ok);}exactMap(reboot.f.core,map,id);F::onWorker=true;}
  {F::Fixture second;assert(second.core.load()==0);exactMap(second.core,map,id);second.store.finishOperation();}
  if(!result)break;
  ++reached;assert(cut<199);
 }
 std::filesystem::remove(snapshotPath);reset();printf("Checked SD restore: %u process-death cuts initial/resume, including partial files and root publication, exact reread source recovery PASS\n",reached);
}
