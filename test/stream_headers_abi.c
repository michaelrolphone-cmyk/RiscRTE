#include <RiscStreamClientV1.h>
#include <RiscStreamSessionProviderV1.h>
#include <RiscSerialStreamSessionV1.h>
#ifdef __cplusplus
#define ASSERT static_assert
#else
#define ASSERT _Static_assert
#endif
ASSERT(offsetof(risc_runtime_api_v1,stream_client)==RISC_RUNTIME_TRACE_V1_SIZE,"Runtime suffix preserves every old byte");
ASSERT(sizeof(risc_runtime_api_v1)==RISC_RUNTIME_STREAM_CLIENT_V1_SIZE,"new Runtime final suffix");
ASSERT(offsetof(risc_driver_streams_v2,driver)==0,"provider base prefix");
ASSERT(offsetof(risc_driver_poll_v2,streams)==0,"provider stream prefix");
ASSERT(offsetof(risc_driver_stream_sessions_v2,poll)==0,"provider complete poll prefix");
ASSERT(offsetof(risc_driver_stream_sessions_v2,extension_tag)==sizeof(risc_driver_poll_v2),"tag follows poll");
ASSERT(offsetof(risc_driver_stream_sessions_v2,extension_version)==sizeof(risc_driver_poll_v2)+4,"version follows tag");
ASSERT(offsetof(risc_driver_stream_sessions_v2,stream_sessions)==sizeof(risc_driver_poll_v2)+8,"adapter follows tag/version");
ASSERT(sizeof(risc_serial_stream_open_v1)==32,"serial request size");
ASSERT(sizeof(risc_serial_stream_call_v1)==24,"serial call size");
ASSERT(RISC_STREAM_OK==0 && RISC_STREAM_AGAIN==1 && RISC_STREAM_EOF==2,"T5 success values");
ASSERT(RISC_STREAM_INVALID==-1 && RISC_STREAM_TIMEOUT==-10 && RISC_STREAM_RETAINED==-11,"T5 errors and new retained value");
