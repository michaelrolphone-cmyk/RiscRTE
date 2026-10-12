#define NATIVE_ATOMIC_ENDPOINT_FIXTURE_ONLY
#include "native_atomic_key_value_test.cpp"
extern "C" {
 int wifi_bridge_prepare(const risc_key_value_atomic_v1*,unsigned,const char*,const char*);
 int wifi_bridge_submit(void);int wifi_bridge_collect(void);int wifi_bridge_cancel(void);int wifi_bridge_clean(void);
 int wifi_bridge_check(const risc_key_value_atomic_v1*,unsigned,const char*,const char*);
 uint64_t wifi_bridge_revision(void);
}
void save(A&a,Gate&gate,const risc_key_value_atomic_v1&api,const char*ssid,const char*password){
 assert(wifi_bridge_prepare(&api,1,ssid,password)==11);assert(wifi_bridge_submit()==10&&wifi_bridge_clean());gate.run(a);assert(wifi_bridge_collect()==0&&wifi_bridge_clean());
}
int main(){
 Disk d;Alloc allocation;C c(6,d.backend(),limits(),allocation.api());assert(!c.initialize(nullptr,0,{}));Gate gate;A a(c,gate.api());auto api=a.api();
 save(a,gate,api,"Fixture network","testpass123");assert(wifi_bridge_check(&api,0,"Fixture network","testpass123"));
 save(a,gate,api,"Second network","second-pass");assert(wifi_bridge_check(&api,1,"Second network","second-pass"));
 save(a,gate,api,"Fixture network","testpass123");assert(c.pendingCount()==3);
 uint64_t id=0;uint32_t n=0;uint8_t intent[160]{};assert(c.pending(2,&id,intent,sizeof intent,&n)==0&&id==3&&n==160&&!memcmp(intent,"WFI0001",8)&&intent[8]==1&&intent[9]==1);
 // Expected fingerprint is SHA256(domain + NUL + length-prefixed record).
 const uint8_t expected[32]={0x37,0x6c,0x99,0x68,0xe5,0xa8,0xc9,0xd0,0xfe,0x3f,0x55,0xc5,0x8d,0xeb,0xbd,0xa1,0x22,0x42,0x5d,0x58,0x79,0xb8,0x98,0xf,0x42,0x52,0x2b,0x41,0x33,0x71,0x63,0xa7};assert(!memcmp(intent+113,expected,32));
 assert(wifi_bridge_prepare(&api,2,"Fixture network","testpass123")==11);
 assert(wifi_bridge_submit()==10);gate.run(a);assert(wifi_bridge_collect()==0);assert(wifi_bridge_check(&api,0,nullptr,nullptr));
 assert(c.pending(3,&id,intent,sizeof intent,&n)==0&&intent[8]==2&&intent[9]==1);for(unsigned i=49;i<113;i++)assert(!intent[i]);
 // This prepared semantic Save must never replay its numeric slot after any
 // intervening native operation, even an unrelated setting edit.
 assert(wifi_bridge_prepare(&api,1,"Third network","third-pass")==11);assert(put(c,"other-setting","changed")==0);
 assert(wifi_bridge_submit()==RISC_ATOMIC_STALE&&wifi_bridge_clean());assert(wifi_bridge_check(&api,0,nullptr,nullptr));
 // Cancellation receipt is retained through the actual copied worker.
 assert(wifi_bridge_prepare(&api,1,"Third network","third-pass")==11);assert(wifi_bridge_submit()==10);assert(wifi_bridge_cancel()==10);gate.run(a);assert(wifi_bridge_collect()==RISC_ATOMIC_CANCELLED);
 C reboot(6,d.backend(),limits(),allocation.api());assert(!reboot.load());Gate restoredGate;A restored(reboot,restoredGate.api());auto restoredApi=restored.api();
 assert(wifi_bridge_check(&restoredApi,0,nullptr,nullptr)&&wifi_bridge_check(&restoredApi,1,"Second network","second-pass")&&reboot.pendingCount()==5);
 puts("Actual System C codec + native async adapter + NativeReplicaCore: count/chunks/selector/intent, expected fingerprint, Forget, stale/cancel and reboot PASS");
}
