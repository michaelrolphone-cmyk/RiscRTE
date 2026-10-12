# Experimental settings and file-source Runtime union

The union retains the exact file-source implementation from published Runtime
`98b9d2b08cd6a05f75ec0ca0cd755a63484a2246` (tree
`169b8ac1d4b7d7a6553584f66ed217004c290f2c`). Its local missing parent was not
reconstructed: the source delta against existing `e650d1ad` was applied to the
settings branch, with unchanged mount files verified byte-identical.

The Port suffix is appended after all three native settings lifecycle callbacks.
App/provider exit predicates preserve asynchronous settings discovery and also
require safe file-source custody. Full Runtime shutdown drains settings before
file-source cleanup; any retention prevents later graph cleanup. Native source
prepare remains explicit, after the existing app-data mount. No feature is
implicitly selected.

The first combined host check passes all 29 file-source lifecycle scenarios,
including the actual Files controller staging 12 MiB + 123 bytes through a fresh
receiver/caller. This is a host fixture size, not native private capacity: the
selected native app-data partition is 512 KiB, with source capacity limited by
actual free space and its required reserve. The physical medium, write latency
and combined selected target require separate qualification. Further regression
and target results will be recorded against their exact source commits.
