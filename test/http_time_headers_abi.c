#include <RiscHttpTimeV1.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
_Static_assert(offsetof(risc_http_client_time_v1,base)==0,"legacy HTTP prefix first");
_Static_assert(offsetof(risc_http_client_time_v1,time_tag)==sizeof(risc_http_client_v1),"append-only tag");
_Static_assert(offsetof(risc_http_client_time_v1,time_version)==sizeof(risc_http_client_v1)+4,"tag version");
_Static_assert(offsetof(risc_http_client_time_v1,open_with_time)==sizeof(risc_http_client_v1)+8,"append-only callback");
static int32_t open_(void* c,const risc_http_request_v1* r,uint64_t* o){(void)c;(void)r;(void)o;return 0;}
static int32_t read_(void* c,uint64_t t,void* b,uint32_t n,uint32_t* o){(void)c;(void)t;(void)b;(void)n;(void)o;return 0;}
static int32_t info_(void* c,uint64_t t,risc_http_response_v1* o){(void)c;(void)t;(void)o;return 0;}
static int32_t close_(void* c,uint64_t t){(void)c;(void)t;return 0;}
static int32_t time_(void* c,const risc_http_request_v1* r,const char* h,uint64_t* o){(void)c;(void)r;(void)h;(void)o;return 0;}
int main(void){
 const risc_http_client_v1 old={1,sizeof(old),NULL,open_,read_,info_,close_};
 const risc_http_client_time_v1 valid={{1,sizeof(valid),NULL,open_,read_,info_,close_},RISC_HTTP_TIME_TAG_V1,RISC_HTTP_TIME_VERSION_V1,time_};
 assert(!risc_http_client_time_from_v1(NULL));assert(!risc_http_client_time_from_v1(&old));
 assert(risc_http_client_time_from_v1(&valid.base)==&valid);
 for(uint32_t size=0;size<sizeof(valid);++size){risc_http_client_time_v1 api=valid;api.base.struct_size=size;assert(!risc_http_client_time_from_v1(&api.base));}
 for(unsigned which=0;which<9;++which){risc_http_client_time_v1 api=valid;
  switch(which){case 0:api.base.api_version=2;break;case 1:api.time_tag^=1;break;case 2:api.time_version=2;break;
   case 3:api.base.open=NULL;break;case 4:api.base.read=NULL;break;case 5:api.base.info=NULL;break;case 6:api.base.close=NULL;break;case 7:api.open_with_time=NULL;break;case 8:api.time_tag=0;break;}
  assert(!risc_http_client_time_from_v1(&api.base));
 }
 risc_http_client_time_v1 future=valid;future.base.struct_size+=8;assert(risc_http_client_time_from_v1(&future.base)==&future);
 puts("HTTP time suffix: C ABI, old prefix, every truncation, tags/version and all callbacks PASS");
}
