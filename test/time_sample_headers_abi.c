#include <RiscTimeSampleV1.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
_Static_assert(offsetof(risc_time_sample_api_v1,api_version)==0,"version prefix");
_Static_assert(offsetof(risc_time_sample_api_v1,struct_size)==4,"size prefix");
_Static_assert(offsetof(risc_time_sample_v1,utc_seconds)==8,"UTC offset");
_Static_assert(offsetof(risc_time_sample_v1,observed_monotonic_ms)==16,"clock offset");
_Static_assert(sizeof(risc_time_sample_v1)==32,"fixed sample size");
static int32_t open_sample(void* c,const risc_time_sample_request_v1* r,uint64_t* t){assert(c&&r&&t);*t=1;return 0;}
static int32_t step_sample(void* c,uint64_t t,risc_time_sample_v1* s){assert(c&&t&&s);return 1;}
static int32_t close_sample(void* c,uint64_t t){assert(c&&t);return 0;}
int main(void){int context=0;risc_time_sample_api_v1 api={1,sizeof(api),&context,open_sample,step_sample,close_sample};risc_time_sample_request_v1 r={sizeof(r),5000,"pool.ntp.org"};risc_time_sample_v1 s={0};s.struct_size=sizeof(s);uint64_t token=0;assert(api.open(api.context,&r,&token)==0&&api.step(api.context,token,&s)==1&&api.close(api.context,token)==0);puts("Time sample C ABI fields and callback signatures PASS");}
