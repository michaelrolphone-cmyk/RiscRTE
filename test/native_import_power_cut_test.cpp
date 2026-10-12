// Reuse actual checked AppDataFiles/NvsReplicaMetadata plus only the host NVS
// system-call model and immutable-source helpers. The included main is not run.
#define main legacy_inventory_test_main
#include "native_nvs_legacy_sources_test.cpp"
#undef main
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
static Rows expected(){return {{"added",{6}},{"original",{5}},{"journal",{4}},{"untouched",{2}}};}
static void checkImported(F::Fixture&f,const A::Witness&w){
 assert(f.core.ready()&&f.core.revision()==1&&f.core.sequence()==0&&!f.core.pendingCount()&&!f.core.identity().known);
 const auto b=f.core.baseline();assert(b.origin()==C::Origin::ImportedLegacy&&b.archiveObligation()&&!memcmp(b.sourceFingerprint(),w.fingerprint,32));
 for(const auto&r:expected()){std::string got;assert(F::get(f.core,r.first.c_str(),&got)==C::Ok&&Bytes(got.begin(),got.end())==r.second);}
}
static void assertOriginal(const decltype(F::nvs)&before){for(const char*space:{"rte00000006","kvj00000006","kvd00000006"})assert(F::nvs.at(space)==before.at(space));}
static int finish(F::Fixture&f,bool resume,A::Witness*out){
 Hash sourceHash,mapHash;Sources s(6,Owner,LegacyOwner,30000,f.inspector,sourceHash.api(),hooks());A factory(&s,f.core,guard(),mapHash.api(),s.sources());C::Initialization a;
 int rc=0;if(resume)rc=factory.admitImportedResume(&a);else{rc=s.inventory(out);if(!rc)rc=factory.admitImported(selection(*out),&a);}return rc?rc:f.core.initializeImportedBaseline(a);
}
int main(int argc,char**argv){assert(argc==2);F::rootPath=argv[1];snapshotPath=F::rootPath+"-power-nvs";setbuf(stdout,nullptr);unsigned reached=0;
 for(bool startWithReceipt:{false,true})for(bool rnrExists:{false,true})for(unsigned cut=1;cut<200;++cut){
  reset();seed();if(!rnrExists)F::nvs.erase("rnr00000006");const auto before=F::nvs;A::Witness witness{};
  {F::Fixture f;Hash h;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks());assert(s.inventory(&witness)==0);}
  if(startWithReceipt){F::Fixture f;Hash h,m;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks());A factory(&s,f.core,guard(),m.api(),s.sources());C::Initialization a;assert(factory.admitImported(selection(witness),&a)==0);F::fault="nvs-set";F::cut=2;F::after=false;F::armed=true;assert(f.core.initializeImportedBaseline(a)!=0);F::armed=false;f.store.finishOperation();assert(!memcmp(F::nvs.at("rnr00000006").at("root").data(),"RTENLI1",8));}
  powerCut=cut;powerAt=0;const pid_t pid=::fork();assert(pid>=0);
  if(!pid){F::powerCheckpoint=powerPoint;F::Fixture child;A::Witness ignored{};assert(finish(child,startWithReceipt,&ignored)==C::Ok);persistNvs();::_exit(0);}
  int status=0;assert(::waitpid(pid,&status,0)==pid&&WIFEXITED(status));const int result=WEXITSTATUS(status);assert(result==0||result==77);restoreNvs();F::reset();assertOriginal(before);
  {F::Fixture reboot;int rc=reboot.core.load();if(rc==C::Missing){A::Witness now{};assert(finish(reboot,false,&now)==C::Ok&&!memcmp(now.fingerprint,witness.fingerprint,32));}else if(rc==C::Initializing){A::Witness ignored{};assert(finish(reboot,true,&ignored)==C::Ok);}else assert(rc==C::Ok);checkImported(reboot,witness);assertOriginal(before);reboot.store.finishOperation();}
  {F::Fixture second;assert(second.core.load()==0);checkImported(second,witness);second.store.finishOperation();}
  if(result==0)break;
  assert(cut<199);++reached;
 }
 std::filesystem::remove(snapshotPath);reset();printf("Imported initialization: %u process-death cuts (initial/resume, originally absent/empty rnr), including partial writes, no destructors, exact-source reboot recovery PASS\n",reached);
}
