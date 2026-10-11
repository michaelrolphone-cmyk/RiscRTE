#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#define CONFIG_IDF_TARGET_ESP32S3 1
#define CONFIG_IDF_TARGET_ESP32C3 0
#define ARDUINO_USB_MODE 1
#define ARDUINO_USB_CDC_ON_BOOT 1
#define IRAM_ATTR
#define RTC_NOINIT_ATTR
#define ARDUINO_ISR_ATTR
#define ESP_SYSTEM_INIT_FN(name, mask) static void name()
#define BIT(x) (1u<<(x))
#define portYIELD_FROM_ISR() ((void)0)
#define pdMS_TO_TICKS(x) (x)
#define log_e(...) ((void)0)
#define log_w(...) ((void)0)
#define isr_log_e(...) ((void)0)
using BaseType_t=int; using UBaseType_t=unsigned; using portBASE_TYPE=int;
constexpr int pdTRUE=1,pdPASS=1,portTICK_PERIOD_MS=1,tskNO_AFFINITY=-1;
using esp_err_t=int; constexpr int ESP_OK=0,ESP_FAIL=-1;
namespace Stub {
 enum Failure {None,Mutex,Rx,Tx,Interrupt,UsbClock,UsbReset,UsbRoute,UsbPad};
 inline Failure failure=None;
 inline unsigned allocationCalls=0,live=0,delays=0,interruptAllocations=0,interruptFrees=0,freeNull=0,flushes=0,pinChanges=0,positiveWaits=0;
 inline unsigned usbResets=0,detachedDelays=0;
 inline uint32_t tick=1,intrMask=0,intrStatus=0;
 inline void (*isr)(void*)=nullptr;
 inline std::deque<uint8_t> hostRx;
 inline std::string hostTx;
 inline bool host=false;
 inline unsigned pinModeValue[49]{},pinLevel[49]{};
 inline bool fail(Failure kind){++allocationCalls;return failure==kind;}
 inline void checkedWait(unsigned wait){if(wait)++positiveWaits;}
}
using TaskHandle_t=void*;
inline TaskHandle_t currentTask=reinterpret_cast<void*>(1);
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return currentTask;}
inline uint32_t millis(){return Stub::tick;}
constexpr unsigned USB_DM_GPIO_NUM=19,USB_DP_GPIO_NUM=20,OUTPUT_OPEN_DRAIN=3,LOW=0;
struct UsbRegisters {struct{unsigned phy_sel=0,pad_pull_override=0,dp_pullup=0,usb_pad_enable=0;}conf0;struct{unsigned sof_int_raw=0;}int_raw;};
inline UsbRegisters USB_SERIAL_JTAG;
struct UsbRouteRegister {
 unsigned value=0;
 operator unsigned()const{return value;}
 UsbRouteRegister& operator=(unsigned next){value=Stub::failure==Stub::UsbRoute?1:next;return *this;}
};
struct RtcRegisters {struct{unsigned sw_hw_usb_phy_sel=0;UsbRouteRegister sw_usb_phy_sel;}usb_conf;};
inline RtcRegisters RTCCNTL;
struct SystemRegisters {struct{unsigned usb_device_clk_en=1;}perip_clk_en1;struct{unsigned usb_device_rst=0;}perip_rst_en1;};
inline SystemRegisters SYSTEM;
inline void delay(unsigned n){++Stub::delays;Stub::tick+=n;if(!USB_SERIAL_JTAG.conf0.usb_pad_enable)++Stub::detachedDelays;}
inline void pinMode(unsigned p,unsigned mode){++Stub::pinChanges;Stub::pinModeValue[p]=mode;USB_SERIAL_JTAG.conf0.pad_pull_override=1;USB_SERIAL_JTAG.conf0.usb_pad_enable=0;}
inline void digitalWrite(unsigned p,unsigned level){++Stub::pinChanges;Stub::pinLevel[p]=level;}
inline void uartSetDebug(void*){}
inline void ets_install_putc2(void(*)(char)){}
inline bool diagnosticIsr=false;
inline bool xPortInIsrContext(){return diagnosticIsr;}
inline void esp_register_freertos_tick_hook(void(*)()){}
struct Mutex {bool locked=false;}; using xSemaphoreHandle=Mutex*;
inline Mutex* xSemaphoreCreateMutex(){if(Stub::fail(Stub::Mutex))return nullptr;++Stub::live;return new Mutex;}
inline void vSemaphoreDelete(Mutex* p){assert(p);--Stub::live;delete p;}
inline int xSemaphoreTake(Mutex* p,unsigned wait){Stub::checkedWait(wait);if(!p||p->locked)return 0;p->locked=true;return 1;}
inline void xSemaphoreGive(Mutex* p){assert(p&&p->locked);p->locked=false;}
struct Queue {size_t capacity;std::deque<uint8_t> data;};using xQueueHandle=Queue*;
inline Queue* xQueueCreate(size_t n,size_t item){assert(item==1);if(Stub::fail(Stub::Rx))return nullptr;++Stub::live;return new Queue{n,{}};}
inline void vQueueDelete(Queue* p){assert(p);--Stub::live;delete p;}
inline int xQueueSendFromISR(Queue* p,const void* data,BaseType_t*){if(!p||p->data.size()==p->capacity)return 0;p->data.push_back(*static_cast<const uint8_t*>(data));return 1;}
inline int uxQueueMessagesWaiting(Queue* p){assert(p);return int(p->data.size());}
inline int xQueuePeek(Queue* p,void* data,unsigned wait){Stub::checkedWait(wait);assert(p);if(p->data.empty())return 0;*static_cast<uint8_t*>(data)=p->data.front();return 1;}
inline int xQueueReceive(Queue* p,void* data,unsigned wait){int r=xQueuePeek(p,data,wait);if(r)p->data.pop_front();return r;}
struct Ring {size_t capacity;std::vector<uint8_t> data,taken;};using RingbufHandle_t=Ring*;
constexpr int RINGBUF_TYPE_BYTEBUF=0;
inline Ring* xRingbufferCreate(size_t n,int){if(Stub::fail(Stub::Tx))return nullptr;++Stub::live;return new Ring{n,{},{}};}
inline void vRingbufferDelete(Ring* p){assert(p);--Stub::live;delete p;}
inline size_t xRingbufferGetCurFreeSize(Ring* p){assert(p);return p->capacity-p->data.size();}
inline int xRingbufferSend(Ring* p,void* bytes,size_t n,unsigned wait){Stub::checkedWait(wait);assert(p);if(n>xRingbufferGetCurFreeSize(p))return 0;auto b=static_cast<uint8_t*>(bytes);p->data.insert(p->data.end(),b,b+n);return 1;}
inline int xRingbufferSendFromISR(Ring* p,void* bytes,size_t n,BaseType_t*){return xRingbufferSend(p,bytes,n,0);}
inline void vRingbufferGetInfo(Ring* p,void*,void*,void*,void*,UBaseType_t* n){assert(p);*n=unsigned(p->data.size());}
inline void* xRingbufferReceiveUpTo(Ring* p,size_t* n,unsigned wait,size_t max){Stub::checkedWait(wait);assert(p);*n=std::min(p->data.size(),max);if(!*n)return nullptr;p->taken.assign(p->data.begin(),p->data.begin()+*n);p->data.erase(p->data.begin(),p->data.begin()+*n);return p->taken.data();}
inline void* xRingbufferReceiveUpToFromISR(Ring* p,size_t* n,size_t max){return xRingbufferReceiveUpTo(p,n,0,max);}
inline void vRingbufferReturnItem(Ring* p,void*){p->taken.clear();}
inline void vRingbufferReturnItemFromISR(Ring* p,void* item,BaseType_t*){vRingbufferReturnItem(p,item);}
using intr_handle_t=int*;constexpr int ETS_USB_SERIAL_JTAG_INTR_SOURCE=1;
inline esp_err_t esp_intr_alloc(int,int,void(*isr)(void*),void*,intr_handle_t* h){++Stub::interruptAllocations;if(Stub::fail(Stub::Interrupt))return ESP_FAIL;++Stub::live;*h=new int(1);Stub::isr=isr;return ESP_OK;}
inline esp_err_t esp_intr_free(intr_handle_t h){++Stub::interruptFrees;if(!h){++Stub::freeNull;return ESP_FAIL;}--Stub::live;delete h;Stub::isr=nullptr;return ESP_OK;}
constexpr unsigned USB_SERIAL_JTAG_INTR_SOF=2,USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY=8,USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT=4,USB_SERIAL_JTAG_INTR_BUS_RESET=512,USB_SERIAL_JTAG_LL_INTR_MASK=0x7ffff;
inline void usb_serial_jtag_ll_clr_intsts_mask(unsigned m){Stub::intrStatus&=~m;if(m&2)USB_SERIAL_JTAG.int_raw.sof_int_raw=0;}
inline unsigned usb_serial_jtag_ll_get_intsts_mask(){return Stub::intrStatus;}
inline void usb_serial_jtag_ll_disable_intr_mask(unsigned m){Stub::intrMask&=~m;}
inline void usb_serial_jtag_ll_ena_intr_mask(unsigned m){Stub::intrMask|=m;}
inline unsigned usb_serial_jtag_ll_txfifo_writable(){return Stub::host?1:0;}
inline void usb_serial_jtag_ll_txfifo_flush(){++Stub::flushes;}
inline size_t usb_serial_jtag_ll_write_txfifo(const uint8_t* data,size_t n){Stub::hostTx.append(reinterpret_cast<const char*>(data),n);return n;}
inline unsigned usb_serial_jtag_ll_read_rxfifo(uint8_t* data,unsigned n){unsigned count=0;while(count<n&&!Stub::hostRx.empty()){data[count++]=Stub::hostRx.front();Stub::hostRx.pop_front();}return count;}
using esp_event_base_t=const char*;using esp_event_loop_handle_t=int*;using esp_event_handler_t=void(*)(void*,esp_event_base_t,int32_t,void*);
#define ESP_EVENT_DEFINE_BASE(name) const char* name=#name
#define ESP_EVENT_DECLARE_BASE(name) extern const char* name
constexpr int ESP_EVENT_ANY_ID=-1;
struct esp_event_loop_args_t{int queue_size;const char* task_name;int task_priority,task_stack_size,task_core_id;};
inline esp_err_t esp_event_isr_post_to(esp_event_loop_handle_t,esp_event_base_t,int32_t,void*,size_t,BaseType_t*){return ESP_OK;}
inline esp_err_t esp_event_loop_create(const esp_event_loop_args_t*,esp_event_loop_handle_t* h){++Stub::live;*h=new int(1);return ESP_OK;}
inline esp_err_t esp_event_handler_register_with(esp_event_loop_handle_t,esp_event_base_t,int32_t,esp_event_handler_t,void*){return ESP_OK;}
inline void esp_event_loop_delete(esp_event_loop_handle_t h){--Stub::live;delete h;}
class Stream {};
constexpr int ESP_RST_UNKNOWN=0,ESP_RST_POWERON=1,ESP_RST_EXT=2,ESP_RST_SW=3,ESP_RST_PANIC=4,ESP_RST_INT_WDT=5,ESP_RST_TASK_WDT=6,ESP_RST_WDT=7,ESP_RST_DEEPSLEEP=8,ESP_RST_BROWNOUT=9;
inline int esp_reset_reason(){return ESP_RST_POWERON;}
inline int esp_sleep_get_wakeup_cause(){return 0;}
