# Explicit expanded app-data bank layout

RISC_PAIRED_APP_DATA_EXPANDED requires RISC_PAIRED_APP_DATA and selects store ABI3, layout riscrte-paired-appdata-v3. Ordinary ABI1/ABI2 builds are unchanged. The explicit partitions-paired-appdata-expanded.csv table retains both 0x260000 firmware capacities and the existing 0x270000/0x80000 app-data partition. It assigns the previous 0x80000 gap to the two SPIFFS stores: bootfs0 0x2f0000/0x550000, app1 0x840000/0x260000, bootfs1 0xaa0000/0x550000. NVS, journal and OTA metadata offsets are unchanged.

This is an incompatible layout requiring explicit initial provisioning or a separately qualified migration. Existing firmware must not accept new-layout OTA payloads, nor vice versa. Bank record validation checks store ABI and capacity; cohort metadata checks exact layout/ABI/capacity; native initialization checks physical partition offsets and sizes. The app-data capacity is preserved, but a full initial image still erases its contents if flashed.

run_expanded_layout_test.py exercises all three variants, cross-layout CRC-valid record/cohort refusal, Python journal compatibility and partition boundaries. paired_bank_test.cpp also runs with the expanded macros. Native-bank test fixtures derive bank1 offsets from the selected layout. Host tests and target builds do not qualify flash hardware or migration. No device operation is performed.
