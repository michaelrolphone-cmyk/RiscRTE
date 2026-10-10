#include "diagnostics/Journal.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace RiscDiagnostics;
struct Wire {
 bool up=true;size_t space=64,partial=1,calls=0;std::string text;
 bool connected(){return up;} size_t writable(){return space;}
 size_t write(const uint8_t* p,size_t n){assert(n<=64&&n<=space);++calls;n=n<partial?n:partial;text.append((const char*)p,n);return n;}
};
static void request(Replay& r,const Journal& j,const Checkpoint& c){for(char ch:std::string("diag\n"))r.input(ch,j,&c);}
static void drain(Replay& r,Wire& w){for(unsigned n=0;n<20000&&r.active();++n)r.poll(w);assert(!r.active());}
int main(){
 Journal j{};begin(j,false,0,1,0);Checkpoint c{};std::string text(768,'z'),app(188,'a');app+=".elf";
 assert(captureCheckpoint(c,1,2,app.c_str(),UINT64_MAX,text.data(),UINT32_MAX)==1);
 assert(c.truncated&&c.length==768&&c.applicationLength==192&&c.text[768]==0&&c.application[192]==0);
 text.assign(768,'x');assert(c.text[0]=='z');auto saved=c;
 assert(captureCheckpoint(c,1,2,"",1,"a",1)==-2);assert(c.sequence==saved.sequence);
 assert(captureCheckpoint(c,1,2,(app+"x").c_str(),1,"a",1)==-2);
 assert(captureCheckpoint(c,1,2,"a.elf",1,nullptr,1)==-2);
 for(unsigned i=0;i<100;++i){message(j,i,"ordinary chatter");}assert(c.sequence==saved.sequence);
 Replay r;Wire w;request(r,j,c);assert(r.active());
 assert(captureCheckpoint(c,1,4,"next.elf",2,"replacement",11)==0);
 w.space=0;r.poll(w);assert(!w.calls);w.space=64;w.partial=0;r.poll(w);assert(w.text.empty());w.partial=1;drain(r,w);
 assert(w.text.find("invocation=18446744073709551615 length=768 truncated=1")!=std::string::npos);
 assert(w.text.find("offset=672 text="+std::string(96,'z'))!=std::string::npos);
 assert(w.text.find("replacement")==std::string::npos&&w.text.find("RTE_DIAG end\n")!=std::string::npos);
 request(r,j,c);r.poll(w);w.up=false;r.poll(w);assert(!r.active());w.up=true;w.text.clear();request(r,j,c);drain(r,w);assert(w.text.find("replacement")!=std::string::npos);
 const char odd[]={'a','\0','\n',char(255)};assert(captureCheckpoint(c,1,5,"app.elf",3,odd,4)==0);assert(std::string(c.text)=="a???");
 c.sequence=UINT32_MAX;assert(captureCheckpoint(c,1,6,"app.elf",3,"a",1)==-2&&c.sequence==UINT32_MAX);
 static_assert(sizeof(Journal)==1288,"RTC journal changed");
 std::cout<<"Checkpoint bounded copy, UINT32_MAX, explicit truncation, identity, chatter isolation, immutable 1-byte replay, backpressure and reconnect PASS\n";
}
