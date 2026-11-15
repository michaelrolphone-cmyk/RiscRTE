# Internal ELF mapping prerequisite

This is an additive loader prerequisite, not an enabled internal executable
memory port. The legacy five-section prefix and ordinary loading path remain.
Four reserved output classes are recognized: `.risc.internal.text`, `.data`,
`.rodata` and `.bss` (each with the full `.risc.internal.` prefix).

The loader checks exact class/type/flags, alignment, nonoverlap, padded combined
allocation bounds and complete relocation-write spans. It keeps allocation
ownership separate from writable/executable views. Re-loading an already owned
mapping refuses without losing its existing custody. Partial allocation or
publication failure follows the checked cleanup path.

The production adapter always reports internal allocation unavailable and
returns `ENOTSUP`. No build flag enables an unproven heap alias. MEMPROT and
other protection settings are untouched; there is no fallback from requested
internal memory to PSRAM. Ordinary allocation/publication is unchanged. A future
usable port needs its own proved provisioning, ownership and cache-publication
contract. This patch does not provide it.

Native provider policy still requires `t5_driver_get` in ordinary `.text`.
Generic internal symbol mapping is not permission to use an internal provider
entry, activate a package, or bypass import/policy admission.

## Host checks

`test/run_internal_mapping_v1.py` compiles actual loader/validator/Xtensa
relocation code. Allocator, privileged-scope and publication leaves are modeled
in the loader fixture. A separate executable links the actual conservative
adapter; its SDK support leaves abort on unexpected ordinary allocation use.
The preserved real Xtensa ELF is read and mapped as data, never executed.

Use a separate clean baseline checkout at
`13eca114cb58469cf6004fafc3da84fbd9d29beb`, Clang and an explicit host SDK/sysroot:

```sh
python3 test/run_internal_mapping_v1.py \
  --cc /path/to/clang --host-sdk /path/to/host-sdk \
  --baseline /path/to/separate-baseline --output /path/to/fresh-results
```

The full matrix is 60 executions: 22 loader cases in normal and UBSan modes,
ordinary baseline/successor comparisons in both modes, actual-adapter refusal
with its legacy opt-in flag absent/present in both modes, and four legacy-prefix
configurations, plus ordinary/refused-internal checks with selection absent and
zero. `--normal-only` runs 34 executions. Results and every command's
output are saved even when a later step fails. Python and C assertions remain on.

The exact unchanged baseline narrows its text mapping to `Elf32_Addr`. Its one
known pointer-width warning is allowed only for the baseline ordinary profile,
at the exact source expression. The fixture compares actual owned text bytes
and export address without dereferencing or calling that truncated host mapping.
The baseline source is not modified to suppress this warning. The successor uses
pointer-width addresses for loader views; target diagnostics and all other error
gates remain unchanged.

These are hosted code/format/lifetime checks, not physical memory qualification,
cache-off reachability, target execution, a complete native-owner link, or
deployment. The native5 worker/Graph consumer closure and Drivers producer
binding are separate follow-on increments.

## Provenance

The seven production members are based on Core main at the baseline above,
reconciled with the retained private implementation. Their exact member hashes
are in `test/internal_mapping_v1/SOURCE_PROVENANCE.json`. The encoded 16756-byte
legacy fixture has SHA256
`45c1ed06b26b523e6405cdbc470fa686c1df2ba9d4df44368c63095767b6d0ba`.
Its metadata names the preserved readback digest, not an operational device.
The copied SDK version header retains its upstream license; adapter shim
declarations/constants match the recorded SDK contract, not a new allocation API.

## Default-off link compatibility

RISC_ELF_INTERNAL_PLACEMENT_V1 absent or 0 compiles without references to the
four internal allocation/support/publication callbacks. Internal output classes
still undergo structural validation, then actual mapping refuses ENOTSUP.
Value 1 selects the scoped callback path, not an available port or privilege.
Other numeric values are rejected. The production adapter remains unavailable.
The hosted loader model explicitly selects 1. Four additional default-off checks
cover absent/zero selection, ordinary linking and internal-section refusal;
normal/full totals are now 34/60. Object-level nm checks reject any new internal
port undefined symbols. Existing owning host test leaves must remain unchanged.

## Measured qualification and limits

The guarded successor passed all 60 hosted executions and both default-off O0
undefined-symbol checks. The report SHA256 is
`2f96bba148b79f9fa45a3f462e096587cf3615e055bf63b4f39f93d3309247b1`.
Only documentation and provenance changed after that run.

The existing reader-image-cache recipe returned zero on the Mac but emitted
`extra[@]: unbound variable` under Bash 3.2 before any test PASS output. This is
not a passing reader test. The unchanged scoped-provider recipe failed to link
with the same 11 unresolved symbols on both clean baseline and guarded source;
there were no new internal-port undefined symbols. No existing test leaves or
production symbol policy were changed to hide those host limitations. Linux
owning-recipe CI remains a required gate. The baseline-comparison report SHA256 is
`eb8f2266168f9ab9e416f851109350efc2d94024fa3e8e67d040676b79dbf82e`.

Earlier 22/56-case records are retained provenance for earlier snapshots, not
additional qualification of the final guard. This increment does not qualify
the separate native-v5 consumer, Drivers producer, target execution or physical
internal memory availability.
