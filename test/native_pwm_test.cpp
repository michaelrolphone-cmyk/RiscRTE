#include <cstdint>
#include <cassert>
#include <cstdio>
constexpr int LEDC_LOW_SPEED_MODE=0,LEDC_TIMER_10_BIT=10,LEDC_AUTO_CLK=0,LEDC_INTR_DISABLE=0,ESP_OK=0;
using ledc_timer_t=int;using ledc_channel_t=int;
struct ledc_timer_config_t {int speed_mode,duty_resolution,timer_num;uint32_t freq_hz;int clk_cfg;};
struct ledc_channel_config_t {int gpio_num,speed_mode,channel,intr_type,timer_sel;uint32_t duty;};
static int pwmPins[4]={-1,-1,-1,-1};
static bool timerOk=true,channelOk=true,staticOk=true,level=false;static unsigned timers=0,channels=0,statics=0;static uint32_t lastDuty;
static int ledc_timer_config(const ledc_timer_config_t*t){++timers;assert(t->duty_resolution==10);return timerOk?0:-1;}
static int ledc_channel_config(const ledc_channel_config_t*c){++channels;lastDuty=c->duty;return channelOk?0:-1;}
static bool gpioWrite(uint8_t pin,bool l){++statics;level=l;if(staticOk)for(auto& x:pwmPins)if(x==pin)x=-1;return staticOk;}
#include "ports/esp32s3/NativePwm.inc"
int main(){
 assert(!pwmWrite(49,1000,1,100) && !pwmWrite(8,0,1,100) && !pwmWrite(8,1000,1,0) && !pwmWrite(8,1000,101,100));assert(!timers && !channels && !statics);
 for(uint16_t n=1;n<1024;++n){assert(pwmWrite(8,25000,n,1024));assert(lastDuty==n);}
 assert(pwmWrite(9,25000,512,1024) && lastDuty==512);
 assert(pwmWrite(45,1000,40,100) && lastDuty==409); // Existing Watch ratio.
 const auto prior=timers;assert(pwmWrite(8,25000,0,1024) && !level);assert(pwmWrite(9,25000,1024,1024) && level);assert(timers==prior);
 timerOk=false;assert(!pwmWrite(8,25000,1,1024));timerOk=true;
 channelOk=false;assert(!pwmWrite(8,25000,1,1024));assert(pwmPins[0]==8);channelOk=true;
 staticOk=false;assert(!pwmWrite(8,25000,0,1024) && pwmPins[0]==8);staticOk=true;assert(pwmWrite(8,25000,0,1024) && pwmPins[0]==-1);
 puts("Native PWM: all 1023 intermediate ratios, exact static endpoints, Watch ratio and failure retention PASS");
}
