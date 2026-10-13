# Earlier executed stage diagnosis

These unchanged source/log files executed against the repaired historical
fixture source `6c43965bdf88861f7d440636352079aaf183eba0`, not the later binding.
With errno reset per operation, flash-program failure #2 before open returns a
valid descriptor with EIO. The first 512-byte write succeeds with errno zero;
the second asserts in lfs.c:262. Injection after a clean open returns -1/EIO
from write and close succeeds. Both 512/8192-byte chunk variants are preserved.
