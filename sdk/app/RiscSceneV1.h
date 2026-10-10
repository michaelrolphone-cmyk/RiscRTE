#pragma once
/* Optional, transport-neutral semantic presentation service. The minimal
 * runtime does not implement, discover implicitly, or require this service.
 * All calls are serialized on the grant-owning task. Documents and navigation
 * are copied before return; no app address or callback survives a call.
 * Handles are invocation-local and MUST NOT be serialized. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_SCENE_CAPABILITY "ui.scene"
#define RISC_SCENE_API_V1 1u
#define RISC_SCENE_MAX_ROUTES 8u
#define RISC_SCENE_MAX_NODES 24u
#define RISC_SCENE_LABEL 40u
#define RISC_SCENE_TEXT 72u
#define RISC_SCENE_MAX_DEPTH 8u

enum { RISC_SCENE_OK=0, RISC_SCENE_IDLE=1, RISC_SCENE_AGAIN=2,
       RISC_SCENE_INVALID=-1, RISC_SCENE_STALE=-2, RISC_SCENE_BUSY=-3,
       RISC_SCENE_UNAVAILABLE=-4, RISC_SCENE_RETAINED=-9 };
enum { RISC_SCENE_TEXT_NODE=1, RISC_SCENE_TIME_OF_DAY=2,
       RISC_SCENE_INTEGER=3, RISC_SCENE_BOOLEAN=4,
       RISC_SCENE_ACTION=5, RISC_SCENE_LINK=6 };
enum { RISC_SCENE_DISABLED=1u, RISC_SCENE_PRIMARY=2u,
       RISC_SCENE_DESTRUCTIVE=4u, RISC_SCENE_HIDDEN=8u };
enum { RISC_SCENE_ACTION_EVENT=1, RISC_SCENE_VALUE_EVENT=2,
       RISC_SCENE_SUSPEND_EVENT=3 };
enum { RISC_SCENE_PUSH=1, RISC_SCENE_POP=2, RISC_SCENE_ROOT=3 };
enum { RISC_SCENE_PRESENTING=1u, RISC_SCENE_CLOSING=2u };

typedef struct {
    uint32_t id, back_action;
    char title[RISC_SCENE_LABEL];
} risc_scene_route_v1;
typedef struct {
    uint32_t id, route, kind, flags, action, target;
    int32_t value, minimum, maximum, step;
    char label[RISC_SCENE_LABEL];
    char text[RISC_SCENE_TEXT];
} risc_scene_node_v1;
typedef struct {
    uint32_t api_version, struct_size, revision, root;
    uint32_t route_count, node_count, reserved[2];
    risc_scene_route_v1 routes[RISC_SCENE_MAX_ROUTES];
    risc_scene_node_v1 nodes[RISC_SCENE_MAX_NODES];
} risc_scene_document_v1;
/* A logical route path, not a collection of form-factor-specific pages.
 * Focus is a semantic node ID, zero when unspecified. Unused entries are zero.
 * Renderers derive pagination and may rematerialize everything from this state. */
typedef struct {
    uint32_t api_version, struct_size, depth, reserved;
    uint32_t routes[RISC_SCENE_MAX_DEPTH], focus[RISC_SCENE_MAX_DEPTH];
} risc_scene_navigation_v1;
typedef struct {
    uint32_t struct_size, kind, document_revision, node, action;
    int32_t value;
    uint64_t sequence;
} risc_scene_event_v1;
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    int32_t (*open)(void *, const risc_scene_document_v1 *,
                    const risc_scene_navigation_v1 *, uint64_t *session);
    /* Strictly increasing, nonzero document revisions. A queued event is only
     * meaningful against the revision on which it was presented. */
    int32_t (*update)(void *, uint64_t, const risc_scene_document_v1 *);
    /* Bounded, nonblocking: copied semantic event (OK), or IDLE. Continues
     * servicing input during asynchronous display transfer. Never sleeps. */
    int32_t (*next)(void *, uint64_t, risc_scene_event_v1 *);
    int32_t (*navigate)(void *, uint64_t, uint32_t operation, uint32_t route);
    int32_t (*snapshot)(void *, uint64_t, risc_scene_navigation_v1 *, uint32_t *flags);
    /* AGAIN: keep the grant and retry after yielding. RETAINED: uncertain
     * custody; retain the invocation and return without further service I/O.
     * OK is the only result permitting release/unmapping of this session. */
    int32_t (*close)(void *, uint64_t);
} risc_scene_api_v1;
#ifdef __cplusplus
}
#endif
