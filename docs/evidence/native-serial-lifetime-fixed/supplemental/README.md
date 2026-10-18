# Supplemental host checks

The final default feature-off and explicit native default-Run suites were rerun
after the pre-parking old-session check was frozen. Commands, each exit 0:

```
SANITIZE=0 ASAN_OPTIONS=detect_leaks=0 bash test/run_runtime_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_runtime_test.sh
SANITIZE=0 ASAN_OPTIONS=detect_leaks=0 bash test/run_serial_native_provider_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_serial_native_provider_test.sh
```

Earlier supplemental checks passed during this implementation, before the final
old-session hardening. These are supporting coverage; final authoritative
source-hashed host matrices are in the parent directory and lifetime-guards.

```
bash test/run_serial_files_test.sh
bash test/run_serial_files_client_test.sh
bash test/run_provider_exit_lifecycle_test.sh
python3 test/run_native_serial_storage_test.py
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 python3 test/run_native_serial_storage_test.py
```

The native Settings availability test also passed feature off, on, and on with
ASan/UBSan. Its runner was invoked with an exported shell `c++` function adding
`-DRISC_NATIVE_SERIAL_FILES=0` or `=1`; this did not change repository sources.

The existing installed-app discovery runner failed compilation because GCC14
raises `-Wmisleading-indentation` in the unmodified fixture. The same fixture
passed with only that diagnostic exempted:

```
c++ -std=c++17 -Wall -Wextra -Werror -Wno-misleading-indentation -Isrc -Isdk/app -Isdk/driver test/installed_app_discovery_test.cpp -o /workspace/shared/serial-lifetime-evidence/installed-discovery/test
/workspace/shared/serial-lifetime-evidence/installed-discovery/test
```

The actual transport test was attempted with the locally available partial
headers and could not compile: `esp_idf_version.h` and USB serial driver headers
are missing. Its failing log is preserved; transport execution is not claimed.

```
IDF_PATH=/workspace/shared/stat-observation-recovery/idf-headers bash test/run_native_serial_files_transport_test.sh
```

`sources-at-freeze.json` records the final checkout state; it does not imply the
earlier supplemental logs ran against every byte in that manifest. No target
link, hardware access, flash or physical qualification occurred.
