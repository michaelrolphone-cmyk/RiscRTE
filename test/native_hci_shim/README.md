# ESP-IDF HCI test boundary

These are intentionally small declarations for the APIs used by NativeHci.h,
checked against the official ESP-IDF 4.4.7 ESP32-C3/S3 esp_bt.h and controller
bt.c sources linked in docs/BLUETOOTH_HCI.md. They are not copied SDK headers or
a Bluetooth implementation. The test compiles the actual production NativeHci.h
and models lifecycle status, queue callbacks, copied send data, allocation,
backpressure/deadlines and failure injection. SDK calls must occur outside the
queue critical section; callback copies hold the lock until finished.

Malformed and overflow RX fail the session. Callbacks racing with close are
rejected before the bounded storage is freed; late callbacks see a null queue.
No callback or controller buffer contains an app/provider code pointer. Failed
init deliberately retains its allocation, because a public IDLE status cannot
prove the pinned SDK rolled back all early-init resources.

The RX ring charges actual packet bytes plus a three-byte header against 4,124
bytes. `native_hci_burst_test.cpp` verifies short event bursts, mixed event/ACL
FIFO wraparound, maximum packets, malformed lengths, byte-capacity exhaustion,
failed-close retention and wiping before free. The allocation remains no larger
than the former four-slot queue. `run_hci_scanner_burst_test.py` adds the exact
selected external HCI and scanner providers and real CpuPort to that boundary.
