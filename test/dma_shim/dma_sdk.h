#pragma once
#include <cstdint>
#define DRAM_ATTR
#define CONFIG_IDF_TARGET_ESP32S3 1
#define ESP_IDF_VERSION_VAL(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(6,1,0)
#define GDMA_LL_AHB_NUM_GROUPS 1
#define GDMA_LL_AHB_PAIRS_PER_GROUP 5
#define ESP_OK 0
#define ESP_INTR_FLAG_INTRDISABLED 32
#define taskSCHEDULER_RUNNING 1
using esp_err_t=int;using BaseType_t=int;using TaskHandle_t=void*;using intr_handle_t=void*;
struct Channel{int id;};using gdma_channel_handle_t=Channel*;
struct gdma_channel_alloc_config_t{};struct gdma_trigger_t{int instance_id,bus_id;};
enum {SOC_GDMA_BUS_AHB,SOC_GDMA_TRIG_PERIPH_SPI2,SOC_GDMA_TRIG_PERIPH_SPI3,SOC_GDMA_TRIG_PERIPH_UHCI0,SOC_GDMA_TRIG_PERIPH_I2S0,SOC_GDMA_TRIG_PERIPH_I2S1,SOC_GDMA_TRIG_PERIPH_LCD0,SOC_GDMA_TRIG_PERIPH_AES0,SOC_GDMA_TRIG_PERIPH_SHA0,SOC_GDMA_TRIG_PERIPH_RMT0};
enum {ETS_SPI2_INTR_SOURCE,ETS_SPI3_INTR_SOURCE,ETS_UHCI0_INTR_SOURCE,ETS_I2S0_INTR_SOURCE,ETS_I2S1_INTR_SOURCE,ETS_LCD_CAM_INTR_SOURCE,ETS_AES_INTR_SOURCE,ETS_SHA_INTR_SOURCE,ETS_RMT_INTR_SOURCE};
struct DmaHw{struct {struct {struct{unsigned val;}int_ena;}out;}channel[5];};extern DmaHw dmaHw;
#define GDMA_LL_GET_HW(n) (&dmaHw)
bool xPortInIsrContext();bool xPortCanYield();int xTaskGetSchedulerState();int xTaskGetCoreID(TaskHandle_t);int xPortGetCoreID();TaskHandle_t xTaskGetCurrentTaskHandle();
int gdma_new_ahb_channel(const gdma_channel_alloc_config_t*,gdma_channel_handle_t*,gdma_channel_handle_t*);
int gdma_get_group_channel_id(gdma_channel_handle_t,int*,int*);int gdma_connect(gdma_channel_handle_t,gdma_trigger_t);int gdma_disconnect(gdma_channel_handle_t);int gdma_del_channel(gdma_channel_handle_t);
bool gdma_ll_tx_is_desc_fsm_idle(DmaHw*,int);unsigned gdma_ll_tx_get_interrupt_status(DmaHw*,int,bool);
int esp_intr_alloc(int,int,void(*)(void*),void*,intr_handle_t*);int esp_intr_enable(intr_handle_t);int esp_intr_disable(intr_handle_t);int esp_intr_free(intr_handle_t);
