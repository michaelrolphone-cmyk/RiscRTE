#include <RiscStorageNativeCapacityV1.h>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
static_assert(offsetof(risc_storage_volume_api_v1_native_capacity,base)==0,"frozen admission prefix");
static_assert(offsetof(risc_storage_volume_api_v1_native_capacity,capacity_tag)>=sizeof(risc_storage_volume_api_v1_admission),"native-only additive suffix");
int main(){
 for(uint32_t cluster=512;cluster<=65536;cluster*=2)for(uint32_t bytes=0;bytes<=65512;++bytes){
  const uint32_t pages=(bytes+4095)/4096;uint64_t expected=6; // three auxiliary files + directory-growth clusters
  for(uint32_t i=0;i<pages;++i){uint32_t n=bytes-i*4096;if(n>4096)n=4096;expected+=(n+uint64_t(cluster)-1)/cluster;}
  expected+=((pages*9u+32u)*32u+uint64_t(cluster)-1)/cluster;
  assert(risc_storage_native_capacity_required(bytes,pages,cluster)==expected);
  assert(!risc_storage_native_capacity_required(bytes,pages+1,cluster));
 }
 for(uint32_t bad:{0u,1u,511u,513u,131072u,UINT32_MAX})assert(!risc_storage_native_capacity_required(1,1,bad));
 assert(!risc_storage_native_capacity_required(65513,16,512));assert(!risc_storage_native_capacity_required(UINT32_MAX,0,512));
 puts("native capacity: all 524104 map/cluster geometries, page boundaries, overflow and frozen prefix PASS");
}
