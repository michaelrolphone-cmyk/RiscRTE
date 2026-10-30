#include "runtime/workers/ResidentWorker.h"
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdio>
using RiscWorkers::ResidentWorker;
static const auto ownerThread=std::this_thread::get_id();
static bool owned=true,failCreate=false,early=false;static unsigned calls=0;
static std::vector<std::thread> threads;
static bool owner(){return owned&&std::this_thread::get_id()==ownerThread;}
static uint64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static void delay(uint32_t n){std::this_thread::sleep_for(std::chrono::milliseconds(n));}
static bool create(void(*fn)(void*),void*arg,uint32_t,uint32_t,int32_t){if(failCreate)return false;if(early){fn(arg);return true;}threads.emplace_back(fn,arg);return true;}
static void done(void*,const risc_worker_control_v1*){++calls;}
static void cancelLoop(void*arg,const risc_worker_control_v1*c){auto*seen=static_cast<std::atomic<bool>*>(arg);seen->store(true);while(!c->cancelled(c->context))c->yield_ms(c->context,1);}
int main(){
 ResidentWorker w({owner,now,delay,create,2,25});uint64_t t=77;
 assert(w.start(1,nullptr,nullptr,4096,1,0,&t)==RISC_WORKER_INVALID&&!t);
 failCreate=true;assert(w.start(1,done,nullptr,4096,1,0,&t)==RISC_WORKER_ALLOCATION&&!t&&w.idle()&&!calls);failCreate=false;
 early=true;assert(w.start(1,done,nullptr,4096,1,-1,&t)==0&&calls==1&&!w.idle());
 assert(w.release(1,t)==RISC_WORKER_BUSY);assert(w.join(2,t,0)==RISC_WORKER_STALE);
 owned=false;assert(w.join(1,t,0)==RISC_WORKER_CONTEXT&&w.release(1,t)==RISC_WORKER_CONTEXT);owned=true;
 assert(w.join(1,t,0)==0&&w.release(1,t)==0&&w.idle());const uint64_t stale=t;assert(w.release(1,stale)==RISC_WORKER_STALE);
 uint64_t full[RISC_RESIDENT_WORKER_SLOTS]{};
 for(auto&v:full)assert(w.start(1,done,nullptr,4096,1,0,&v)==0);
 uint64_t overflow=9;assert(w.start(1,done,nullptr,4096,1,0,&overflow)==RISC_WORKER_CAPACITY&&!overflow);
 for(auto v:full)assert(w.join(1,v,0)==0&&w.release(1,v)==0);
 for(auto stack:{0u,4095u,4097u,32772u})assert(w.start(1,done,nullptr,stack,1,0,&overflow)==RISC_WORKER_INVALID&&!overflow);
 assert(w.start(1,done,nullptr,4096,6,0,&overflow)==RISC_WORKER_INVALID&&!overflow);
 assert(w.start(1,done,nullptr,4096,1,2,&overflow)==RISC_WORKER_INVALID&&!overflow);
 early=false;std::atomic<bool>entered{false};assert(w.start(1,cancelLoop,&entered,8192,2,1,&t)==0&&t>stale);
 while(!entered.load())delay(1);
 std::thread foreign([&](){assert(w.cancel(1,t)==RISC_WORKER_CONTEXT&&w.join(1,t,0)==RISC_WORKER_CONTEXT&&w.release(1,t)==RISC_WORKER_CONTEXT);});foreign.join();
 assert(w.join(1,t,2)==RISC_WORKER_TIMEOUT&&!w.idle());assert(w.release(1,t)==RISC_WORKER_BUSY);
 assert(w.cancel(2,t)==RISC_WORKER_STALE&&w.cancel(1,t)==0);assert(w.join(1,t,2000)==0&&w.release(1,t)==0&&w.idle());
 for(auto&thread:threads)thread.join();
 puts("Resident worker actual concurrency: failed start, early completion, timeout/cancel, owner/scope/stale/join-before-release PASS");
}
