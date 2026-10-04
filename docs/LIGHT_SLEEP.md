# Owned GPIO light sleep

The append-only `light_sleep` field in `garden_gpio_v1` and
`risc_gpio_bank_api_v1` shares `RiscLightSleepV1.h`. Existing field offsets and
API versions are unchanged. Check the corresponding `*_LIGHT_SLEEP_V1_SIZE`
macro and nonnull function before calling. The GPIO-bank base is copied from
Watch 17ecd7c302d27b86ca68746e1b4b9495be5a467f; only the documented tail is added.
The bank must validate its own input claim and translate it to the private raw
CPU token. Passing its public token directly is incorrect. Watch owns that
facade implementation; the generic runtime implements the raw boundary.

The CPU owner task retains open GPIO/I2C/SPI resources across synchronous light
sleep. This is not `Port::quiescent()`, graph teardown, deep sleep, or app reload.
Wrong-task/ISR, missing/stale/foreign/output tokens, active I2C calls, held SPI
transactions, nested sleep and poisoned resources are refused. Native SPI also
checks retained pending DMA. The caller must first drain frames and suspend
all consumers; CPU checks alone cannot prove an application has released a
frame. Native code remains trusted and cooperative, not memory-isolated.

Only this CPU port owns sleep/wake configuration in this minimal deployment.
No Wi-Fi, Bluetooth, timer or other wake owner is admitted by this operation.
An inactive GPIO level is required before arm and is checked again after arm.
An assertion in the final entry race may yield immediate wake or SDK rejection;
callers must acknowledge/debounce and rearm only after release. GPIO light sleep
uses ordinary digital wake, not the narrower RTC-only deep-sleep pin set.
The caller supplies polarity from its typed selected configuration; no Watch
pin, PMU register or rail policy is compiled into the CPU port.

Every arm attempt is followed by pin/global-GPIO wake cleanup, including SDK
entry failure. Already-disabled global GPIO wake is successful cleanup per
IDF4.4.7's documented INVALID_STATE result. A failed cleanup poisons the port,
retains the wake claim and returns RETAINED; restart is required. The API never
waits for button release. CPU sleep itself intentionally lasts until a wake
source occurs. Calls return generic GPIO/OTHER wake cause only after successful
SDK sleep; no ELF callback runs inside the low-level sleep operation.

Drivers must implement explicit reversible prepare/resume policies, including
rollback after partial prepare, outside this generic port. Failure does not
justify cutting rails or force-unmapping a live driver. The Watch owner owns
PMU interrupt masking/acknowledgement and panel sleep commands; a wake animation
belongs to its app. Physical power, USB reconnect and repeated wake behavior
remain unqualified by host models or target compilation.

Run `bash test/run_light_sleep_test.sh` for actual CPU-port fault-path tests.
The existing real clock/driver integration remains unchanged and independently
runnable. Firmware source version is 0.1.1; stable 0.1.0/e27 remains separate.
