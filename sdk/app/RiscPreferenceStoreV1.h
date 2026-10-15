#ifndef RISC_PREFERENCE_STORE_V1_H
#define RISC_PREFERENCE_STORE_V1_H
#include "RiscKeyValueAtomicV1.h"
#define RISC_PREFERENCE_STORE_CAPABILITY "storage.preferences"
#define RISC_PREFERENCE_STORE_API_V1 1u
#define RISC_PREFERENCE_STORE_COPIED_INPUT 4u
#define RISC_PREFERENCE_STORE_ATOMIC_GROUP 8u
/* Namespace-bound preferences, independent of physical persistence technology.
 * The operation layout and RISC_ATOMIC_* statuses are reused, not the atomic
 * capability's stronger durability/worker promises. state is an opaque snapshot;
 * callers must use its revision for get/submit and must collect accepted work.
 * COPIED_INPUT: no borrowed input survives submit; work may finish synchronously.
 * ATOMIC_GROUP: the backend supports all-or-none groups and DELETE mutations.
 * Without ATOMIC_GROUP only PUT is supported, writes are individually committed,
 * and any failure after an accepted submission is an UNCERTAIN receipt, possibly
 * with some values changed. Never automatically replay an uncertain operation.
 * cancel is a request; a completed operation may still collect successfully.
 * Each grant owns its operation token; release/exit requires terminal collection.
 * Exact namespace authority comes from the boot grant, never application paths.
 * Backend selection is fixed by registered capabilities, never app/device tests.
 */
typedef risc_key_value_atomic_state_v1 risc_preference_store_state_v1;
typedef risc_key_value_atomic_mutation_v1 risc_preference_store_mutation_v1;
typedef risc_key_value_atomic_receipt_v1 risc_preference_store_receipt_v1;
typedef risc_key_value_atomic_v1 risc_preference_store_v1;
#endif
