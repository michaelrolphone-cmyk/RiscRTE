#include "NativeRealtime.h"
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <sys/time.h>
#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#if !CONFIG_ESP32S3_TIME_SYSCALL_USE_RTC_FRC1
#error Retained realtime requires the pinned SDK RTC plus high-resolution timer
#endif
#endif
namespace RiscCpu { namespace NativeRealtime {
namespace {
// Only validity is retained here. The SDK owns RTC time/base/calibration and
// advances wall time through deep sleep; no guessed duration is added to it.
struct Stamp { uint32_t magic, inverse; };
RTC_NOINIT_ATTR Stamp stamp;
constexpr uint32_t Magic=0x52544331u;
bool valid=false,started=false;
bool (*ownerTask)()=nullptr;
bool owner(){return started && ownerTask && ownerTask();}
}
void configure(bool (*owner)()){ownerTask=owner;}
void start(){
 valid=esp_reset_reason()==ESP_RST_DEEPSLEEP && stamp.magic==Magic && stamp.inverse==~Magic;
 stamp={};started=true;
}
int32_t seed(int64_t seconds,uint32_t nanos){
 // Pinned IDF4/newlib time_t is signed 32-bit. Reject instead of wrapping.
 if(!owner())return RISC_REALTIME_CONTEXT;
 if(seconds<0 || seconds>INT32_MAX || nanos>=1000000000u || nanos%1000u)return RISC_REALTIME_INVALID;
 timeval value{};value.tv_sec=static_cast<time_t>(seconds);value.tv_usec=nanos/1000u;
 valid=false;stamp={};
 if(settimeofday(&value,nullptr)!=0)return RISC_REALTIME_IO;
 valid=true;return RISC_REALTIME_OK;
}
int32_t read(risc_realtime_snapshot_v1* out){
 if(!owner())return RISC_REALTIME_CONTEXT;
 if(!out || out->struct_size!=sizeof(*out))return RISC_REALTIME_INVALID;
 risc_realtime_snapshot_v1 result{};result.struct_size=sizeof(result);
 const int64_t before=esp_timer_get_time();
 // Before an explicit seed (or an admitted deep-sleep checkpoint), the SDK
 // wall clock has no authority. It may contain stale/invalid state after a
 // reset or firmware replacement. Return UNSET with a monotonic bracket so
 // clients can recover from their authorized external clock. Reading or
 // validating that untrusted wall value here would turn UNSET into IO and
 // prevent the very seed which repairs it.
 timeval wall{};const int rc=valid?gettimeofday(&wall,nullptr):0;
 const int64_t after=esp_timer_get_time();
 if(before<0 || after<before)return RISC_REALTIME_IO;
 if(valid && (rc || wall.tv_sec<0 || wall.tv_sec>INT32_MAX || wall.tv_usec<0 || wall.tv_usec>=1000000))return RISC_REALTIME_IO;
 result.monotonic_before_us=uint64_t(before);result.monotonic_after_us=uint64_t(after);
 if(valid){result.validity=RISC_REALTIME_VALID;result.epoch_seconds=wall.tv_sec;result.nanoseconds=uint32_t(wall.tv_usec)*1000u;}
 *out=result;return RISC_REALTIME_OK;
}
void enter(void (*terminal)()){
 // Called only after CPU quiescence/wake validation. Refusal never commits;
 // an unexpected terminal return removes eligibility without changing live time.
 stamp={};if(valid)stamp={Magic,~Magic};
 terminal();stamp={};
}
}}
