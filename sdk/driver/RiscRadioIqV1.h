#pragma once
/* radio.iq API 1. The driver owns bring-up and the SRAM burst.
 * Callers only pass a buffer. Layout is append-only. */
#include "RiscProviderV2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_RADIO_IQ_API_V1 1u
#define RISC_RADIO_IQ_PAIRS 256u
/* Legacy entry points retain the 256-pair limit. Query the optional extension
 * before using larger, independently acquired coherent bursts. */
#define RISC_RADIO_IQ_MAX_PAIRS 8192u
#define RISC_RADIO_IQ_AUTO 0xffffffffu
#define RISC_RADIO_IQ_FORMAT_S10_I0_Q10 1u
#define RISC_RADIO_IQ_FLAG_COHERENT_BURST (1u << 0)
#define RISC_RADIO_IQ_FLAG_NOMINAL_FREQUENCIES (1u << 1)
#define RISC_RADIO_IQ_FLAG_UNCALIBRATED_AMPLITUDE (1u << 2)
#define RISC_RADIO_IQ_CONTROL_CENTER (1u << 0)
#define RISC_RADIO_IQ_CONTROL_RATE (1u << 1)
#define RISC_RADIO_IQ_CONTROL_BANDWIDTH (1u << 2)
#define RISC_RADIO_IQ_CONTROL_GAIN (1u << 3)
#define RISC_RADIO_IQ_CONTROL_RF_GAIN (1u << 4)
#define RISC_RADIO_IQ_CONTROL_BB_GAIN (1u << 5)
#define RISC_RADIO_IQ_CONTROL_FILTER (1u << 6)
#define RISC_RADIO_IQ_CONTROL_DC (1u << 7)
#define RISC_RADIO_IQ_CONTROL_IQ (1u << 8)

#define RISC_RADIO_IQ_OK 0
#define RISC_RADIO_IQ_NOT_RUNNING 1
#define RISC_RADIO_IQ_PLL_FAILED 2
#define RISC_RADIO_IQ_PBUS_FAILED 3
#define RISC_RADIO_IQ_DUMP_TIMEOUT 4
#define RISC_RADIO_IQ_BAD_ARGUMENT 5
#define RISC_RADIO_IQ_BUSY 6
#define RISC_RADIO_IQ_CLEANUP_RETAINED 7

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    /* One receive burst. count is 1..RISC_RADIO_IQ_PAIRS words.
     * Each word is the published dump pair: signed 10-bit I in bits 0-9,
     * signed 10-bit Q in bits 10-19. Returns RISC_RADIO_IQ_*. */
    int (*capture_burst)(void *context, uint32_t *pairs, uint32_t count);
    /* Append-only cleanup extension. Idempotent; false retains the resource
     * lease and provider. Retry without normal I/O until this returns true. */
    bool (*suspend)(void *context);
} risc_radio_iq_api_v1;

/* Optional copied diagnostic extension. No sample data or borrowed pointers.
 * Old consumers retain the unchanged risc_radio_iq_api_v1 prefix. */
enum { RISC_RADIO_IQ_STAGE_IDLE, RISC_RADIO_IQ_STAGE_CLAIM,
       RISC_RADIO_IQ_STAGE_PLL, RISC_RADIO_IQ_STAGE_RECEIVER,
       RISC_RADIO_IQ_STAGE_DUMP, RISC_RADIO_IQ_STAGE_COPY,
       RISC_RADIO_IQ_STAGE_COMPLETE, RISC_RADIO_IQ_STAGE_CLEANUP };
typedef struct {
    uint32_t struct_size, stage;
    int32_t result;
    uint32_t requested_pairs, clock_mask, dump_before, dump_after, elapsed_cycles;
    uint32_t dump_ready, cleanup_ok;
} risc_radio_iq_diagnostics_v1;
typedef bool (*risc_radio_iq_trace_v1)(void *context, const char *stage);
typedef struct {
    risc_radio_iq_api_v1 base;
    bool (*diagnostics)(void *context, risc_radio_iq_diagnostics_v1 *out);
    /* Optional synchronous stage callback; never retained after this call. */
    int (*capture_burst_traced)(void *context,uint32_t *pairs,uint32_t count,
                               risc_radio_iq_trace_v1 trace,void *trace_context);
} risc_radio_iq_diagnostics_api_v1;

/* All settings are per invocation; none change the defaults of legacy calls.
 * These are nominal RF settings and raw upstream receiver codes, never dB/dBm.
 * Gain selector: 0..127 (forced, not live AGC); RF word: 0..511 or AUTO;
 * BB code: 0..127 or AUTO; filter: I[5:0] | Q[5:0]<<8 (mask 0x3f3f);
 * DC: four 0..511 codes or AUTO; IQ: amplitude[4:0] | phase[5:0]<<8
 * (mask 0x3f1f) or AUTO. AUTO retains native calibrated selection/correction.
 * Center: 1841666667..2790000000 Hz, quantized by the PLL; rate: 16M or 80M;
 * width: 20M or 40M. Width does not guarantee a measured analog passband. */
typedef struct {
    uint32_t struct_size;
    uint32_t center_hz, sample_rate_hz, bandwidth_hz, gain_selector;
    uint32_t rf_gain, bb_gain, filter, dc[4], iq_correction;
} risc_radio_iq_settings_v1;
#define RISC_RADIO_IQ_SETTINGS_DEFAULT {sizeof(risc_radio_iq_settings_v1), \
    2440000000u, 80000000u, 40000000u, 24u, RISC_RADIO_IQ_AUTO, \
    RISC_RADIO_IQ_AUTO, 0u, {RISC_RADIO_IQ_AUTO, RISC_RADIO_IQ_AUTO, \
    RISC_RADIO_IQ_AUTO, RISC_RADIO_IQ_AUTO}, RISC_RADIO_IQ_AUTO}
typedef struct {
    uint32_t struct_size, flags, controls;
    uint32_t min_pairs, max_pairs, center_min_hz, center_max_hz;
    uint32_t sample_rates_hz[2], bandwidths_hz[2];
    uint32_t gain_selector_max, rf_gain_max, bb_gain_max;
    uint32_t filter_mask, dc_max, iq_correction_mask, automatic_value;
    uint32_t sample_format, component_bits, component_full_scale;
    risc_radio_iq_settings_v1 defaults;
} risc_radio_iq_capabilities_v1;
typedef struct {
    uint32_t struct_size, flags;
    uint32_t center_hz, sample_rate_hz, bandwidth_hz, pair_count;
    uint32_t sample_format, component_bits, component_full_scale;
    uint32_t rf_gain, bb_gain, dc[4], iq_correction, lo_mode;
} risc_radio_iq_format_v1;
typedef struct {
    /* Test base.base.struct_size before reading any new function pointer. */
    risc_radio_iq_diagnostics_api_v1 base;
    /* Copied metadata only; no hardware access. Caller sets out->struct_size. */
    bool (*capabilities)(void *context, risc_radio_iq_capabilities_v1 *out);
    /* One coherent 1..max_pairs burst, never assembled from separate captures.
     * NULL settings selects defaults. Set format->struct_size before calling;
     * format and pairs are valid only on OK. Valid-sized outputs are cleared
     * on failure. Invalid count/pointers cannot authorize clearing their memory.
     * On failure, hardware state is restored or CLEANUP_RETAINED keeps custody
     * until suspend succeeds. No settings persist, even after a failed capture.
     * Calls must be serialized, as with the original provider API. */
    int (*capture_configured)(void *context, uint32_t *pairs, uint32_t count,
                              const risc_radio_iq_settings_v1 *settings,
                              risc_radio_iq_format_v1 *format);
    /* Same synchronous, never-retained stage callback as the old extension. */
    int (*capture_configured_traced)(void *context, uint32_t *pairs, uint32_t count,
                                     const risc_radio_iq_settings_v1 *settings,
                                     risc_radio_iq_format_v1 *format,
                                     risc_radio_iq_trace_v1 trace, void *trace_context);
} risc_radio_iq_extended_api_v1;
/* IQt1 timed envelope extension; old capture tables remain exact prefixes.
 * Hardware captures coherent bursts at the requested cadence. It does not
 * claim uninterrupted IQ between bursts. Every record includes real timestamp,
 * observed IQ duration and loss flags; consumers must preserve these limits. */
#define RISC_RADIO_IQ_TEMPORAL_ABI 0x31745149u
#define RISC_RADIO_IQ_ENVELOPE_GAP 1u
typedef struct {uint64_t timestamp_us;float magnitude;uint32_t observed_us,sequence,flags;} risc_radio_iq_envelope_v1;
typedef struct {
    risc_radio_iq_extended_api_v1 base;
    uint32_t temporal_abi;
    int (*start_envelope)(void *,const risc_radio_iq_settings_v1 *,uint32_t interval_us,risc_radio_iq_format_v1 *);
    int (*read_envelope)(void *,risc_radio_iq_envelope_v1 *,uint32_t capacity,uint32_t *count,uint32_t *dropped);
    int (*latest_iq)(void *,uint32_t *pairs,uint32_t count,uint64_t *timestamp_us);
    bool (*stop_envelope)(void *);
} risc_radio_iq_temporal_api_v1;
static inline const risc_radio_iq_temporal_api_v1*risc_radio_iq_temporal(const risc_radio_iq_extended_api_v1*p){
 return p&&p->base.base.struct_size>=sizeof(risc_radio_iq_temporal_api_v1)&&((const risc_radio_iq_temporal_api_v1*)p)->temporal_abi==RISC_RADIO_IQ_TEMPORAL_ABI?(const risc_radio_iq_temporal_api_v1*)p:0;
}
#ifdef __cplusplus
}
#endif
