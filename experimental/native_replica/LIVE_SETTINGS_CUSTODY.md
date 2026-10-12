# Construction-bound mount and live settings custody

This default-off lifecycle repair leaves product policy and selection unchanged.
It removes the cycle in which the permanent assembly needs stores/inspectors
before `NativeSettingsWorker::prepare` can create their real workers.

## Authority and lifetime

The inspector permanently binds the pinned VFS context, actual partition,
mount/private-root paths, registration/root-admission and all-writer/redirection
identity. Its constructor needs no worker or accepted lease and performs no I/O.
`boundTo` remains RAM-only and the factory still requires the exact namespace,
concrete inspector type/address, source binding and whole INTERNAL control range.

Every inspection obtains two matching trusted port snapshots. They must report
the real registered worker and coordinator, the exact namespace, a live accepted
lease generation, a current mount-enter generation and the held native resource.
These are latched only for the current inspection and checked before/after each
I/O/cooperation boundary. A safe later lease can reuse the inspector; an identity
change mid-operation refuses, retaining any unresolved descriptors. No namespace,
source, image grammar or concrete-inspector admission is weakened.

The registered coordinator issues generations for *all* accepted leases, both
native clients and atomic operations (including RAM-only state/get). One owner
increments a saturating uint32 counter only after background acquisition succeeds.
It publishes the generation before publishing the holder. Worker snapshots use
acquire loads and matching generation/holder reads; seeing a new holder cannot
pair it with an older generation, and seeing a new generation cannot resurrect
the previous holder. Release clears the holder before any next generation.
`UINT32_MAX` is the final accepted generation. Native acquisition then returns
`NoSpace`; the existing atomic bool guard reports `Busy`. Neither attempts the
background resource or I/O after exhaustion. Existing 64-bit lease serial fields
carry this value; no 64-bit atomic is used.

## Optional real mount dispatch

When `RISC_NATIVE_APP_DATA_MOUNT` is explicitly selected, the actual worker enters
`Access::Settings` only with live registered worker custody. It holds the mount
through the whole `service.workerStep`. No-lease idle polls never enter the mount.
A lease held while the owner waits on SD phases can enter/leave a RAM-only guard
during probes; eliminating those probes is deliberately not another mailbox
execution mechanism.

Concrete clients finish/check their store and sources before returning. The
outer worker tail checks only the permanent store and mount, performs no I/O,
and never follows a reconstructible client pointer after terminal publication.
The mount's sticky failure is an immediate coordinator/atomic retained predicate
on both tasks, so terminal collection cannot release an unsafe background lease.
Mount leave itself refuses outstanding descriptors. Retained worker, mount,
store, source and native resource custody are never deleted or fabricated.

## Software qualification

Reproducible commands and exact output hashes are recorded under
`evidence/live-settings-custody`. Main coverage includes:

- Existing real ESP worker-binding and coordinator suites, normal, ASan/UBSan
  and ThreadSanitizer, including two namespaces, foreign tasks, blocked I/O,
  close failure, cancellation and concurrent owner/worker publication
- New coordinator generation checks: 256 native releases/reacquisitions
  interleaved with atomic reads; final native and atomic generations; refusal
  after overflow without another resource acquisition or I/O
- Full pinned LittleFS fresh/imported/SD inspector suite, normal and ASan/UBSan:
  construction with absent dynamic identity, 128 later generations, exact two
  namespaces, wrong worker, release/reacquire, changed lease/mount custody/
  worker/coordinator mid-I/O and retained handles
- Real `NativeAppDataMount.cpp` + actual registered worker tasks + two real
  checked stores and concrete inspectors: construct before registration, 66
  native jobs and atomic writes; healthy, physical flash fault and checked-close
  failure; normal, ASan/UBSan and ThreadSanitizer. This test uses the actual
  registered/fenced VFS and pinned LittleFS. RTOS, radio, flash hardware and
  durable metadata are host models, with synthetic opaque payloads.
- Existing Linux/unsupported artifact, fresh initialization, NVS fresh sources,
  checked SD restore, restore power-cut and imported initialization suites pass
- All 64 source-pinned permanent assembly/lifecycle cases, normal and ASan/UBSan,
  including RAM-only LittleFS construction with no pretend worker/lease
- Serialized Xtensa GCC 8.4.0 worker (with and without the real mount selection),
  inspector and assembly probes. Default-off worker/assembly objects are empty;
  AppDataFiles remains byte-identical to `2d362e8`. No 64-bit atomic helper is
  present in selected worker objects.

The target inspector is 504 bytes, up 48; host size is 616, up 56. The compiler
frame for the authority check grows 464 to 528 bytes. File (656), inventory (752)
and inspect (96) frames are unchanged. Coordinator (4,392), Binding (12,960),
registry (16), two-worker requested/static total (25,966) and assembly controls
(50,568) are unchanged. This excludes supplied mount/backend controls, allocator
bookkeeping, callback/vendor stack and all variable payloads; it is not a final
whole-product RAM or stack high-water budget.

LeakSanitizer is disabled in ASan runs because the execution environment rejects
its traced scan and native bindings deliberately remain permanent. No leak,
physical flash/NVS durability, target execution or hardware qualification is
claimed. The separate concrete product backend/selector, startup source dispatch,
first-SD capacity proof and final product build/activation are not completed by
this lifecycle checkpoint. Watch SD policy is unchanged.
