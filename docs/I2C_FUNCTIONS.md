# I2C peripheral functions

`peripheral.i2c-function@1` is a typed hardware descriptor for a function of a single physical I2C peripheral. It does not define a capability or a chip protocol. A board record specifies `parent_instance_id` and `function_id`; the parser derives the existing parent's bus/address/chip identity after parsing all devices. Parent order is irrelevant. A function does not duplicate the physical address or pins and cannot select an arbitrary new address. Multiple distinct functions of the same physical parent are allowed; duplicate function identities, function-parent cycles and missing/nonphysical parents are refused.

Functions still need `hardware.device` and explicitly bound services such as `i2c.bus`. The normal Runtime graph's hardware-binding and bus-scope checks remain unchanged. No implicit singleton exception or global bus grant was introduced. Driver identifiers and function numbers are selected by the board; clients continue to request generic capabilities. Drivers sharing the chip must coordinate register policies and hold only bounded I2C address leases.

This lets a battery gauge and an independent power-rail function coexist on one PMIC without making display power depend on the battery capability or exposing a private PMIC suffix to the application. The native parser contains no rail registers, voltage policy or board pin mappings.

Host tests cover order-independent resolution, retained bus/address identity, multiple functions, missing/self/function parents, unknown configuration fields and preservation of duplicate-physical-address rejection. Hardware validation remains a separate task.
