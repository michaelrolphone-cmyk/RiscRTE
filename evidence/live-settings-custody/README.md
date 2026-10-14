# Live settings custody qualification

The source contracts and limits are documented in
`experimental/native_replica/LIVE_SETTINGS_CUSTODY.md`.

`qualification.json` records exact source hashes, the base and independently
consumed real-mount peer, test variants and resource measurements. Logs preserve
full commands where the runner prints them. Host runner scripts reproduce their
cases; the mount runner accepts `--mount-root` while changes are separate and
uses the current checkout by default once integrated. `.img` files, native
credentials and product images are not included.

The final normal, ASan/UBSan and ThreadSanitizer actual mount/worker logs include
healthy, physical flash failure and checked-close failure. Corresponding manifests
record the exact real mount, worker and inspector inputs. The full 64-case
source-pinned factory suites pass normal and ASan/UBSan. Profile logs cover the
complete fresh/imported/SD grammar and mutation-recovery regressions.

One initial ASan attempt reached an environment-specific LeakSanitizer fatal
error before final reporting. It is not represented as a pass. All final ASan
logs use `ASAN_OPTIONS=detect_leaks=0`; this checkpoint makes no leak claim. An
initial integration-test compile exposed a host `dirent` header collision; the
fixture uses the pinned VFS type and final normal/sanitizer builds pass. No
production source was weakened for either fixture/setup issue.

Target checks compile one translation unit at a time with the installed Xtensa
GCC 8.4.0 SDK. They do not link or activate a firmware image. Default-off worker
and assembly objects are empty; AppDataFiles matches the base object. No 64-bit
atomic helper dependency is emitted. Resource totals are component-only, and
physical hardware/power-loss/whole-product stack qualification remains open.
