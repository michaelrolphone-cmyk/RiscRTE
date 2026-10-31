# Infrastructure relay main-task stack budget

The native main-task default is 24576 bytes, up from 16384. Product providers
execute on this task. The prior live infrastructure capture reported 3688 bytes
of stack headroom against its unchanged 4096-byte optional-admission floor.
The signed webhook relay adds HTTP header and HMAC call depth; raising the task
allocation preserves that floor instead of weakening admission checks.

This costs 8192 additional internal RAM bytes for the main task. It does not
change a hardware ABI, TLS validation, driver admission, or the shared USB/radio
diagnostic implementation. Native builds must verify the effective generated
SDK configuration, not merely this default file. Host sanitizer and Xtensa link
checks do not establish the actual device's minimum headroom; live qualification
remains separate.
