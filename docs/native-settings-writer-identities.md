# Native Settings copied writer identities

Writer binding capacity and distinct identity capacity are separate bounds. The default
identity capacity equals RISC_NATIVE_SETTINGS_MAX_WRITERS, preserving every existing
configuration's capacity. A product may explicitly select
RISC_NATIVE_SETTINGS_MAX_IDENTITIES only after counting distinct strings in its real
writer table. Binding order, exact namespace 1/6 pairs and duplicate-pair refusal are
unchanged. Neither capacity changes Runtime's separately bounded atomic bindings.

The registry owns permanent 96-byte string cells inside the existing lifecycle State.
It stores each distinct identity once and each ordered binding as an identity index
plus namespace bit. It does not borrow Assembly or caller buffers, allocate memory,
move the State, alter IQ placement or release a registered pointer. Rejected registry
additions do not mutate the registry. Lifecycle prepare remains one-shot, including
partial setup failure; clients and retained failure handling are unchanged.

The 96-byte cells are retained without shortening or truncating identities. Existing
RuntimePackages::safeId policy currently accepts at most 63 characters because package
Identity::id is 64 bytes. The previous lifecycle also applied this policy. Tests cover
63 accepted and 64, 95 and 96 rejected; this change does not widen package identity rules.

The generic host tests cover minimum and maximum capacities, multiple namespaces for
the same identity, distinct-identity overflow with remaining binding capacity,
duplicate pairs, malformed IDs, pointer stability and source mutation. Actual lifecycle
tests verify all seven ordered bindings, both read-through registrations, pre-worker
refusal, activation/retained refusal, no storage I/O and no retry after selection.
Existing launch/coordinator/availability tests remain regression requirements.
Host internal-memory predicates and scheduling are modeled. Permanent clients exit
without artificial unregister; CI disables leak detection for that existing lifetime.

X4's seven-binding successor has six distinct identities. Its predecessor has six
bindings and six identities. Native link, exact State symbol size, complete BSS/IQ
boundary, map and source closure must be measured for both profiles with this Runtime.
No host structure size or successful host admission establishes target memory, storage
capacity, product firmware provenance or deployment qualification.
