/* Public launch inputs only; no native private-memory observations. */
#ifndef FRAME_COMPOSITOR_LAUNCH_SELECTOR_H
#define FRAME_COMPOSITOR_LAUNCH_SELECTOR_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FRAME_LAUNCH_CONFIG_ABI 1u
#define FRAME_LAUNCH_MAX_ENTRIES 1024u
#define FRAME_LAUNCH_MAX_INSPECT_ENTRIES 4096u
#define FRAME_LAUNCH_MAX_ENV_BYTES (1024u * 1024u)
#define FRAME_LAUNCH_MAX_LOADER_VALUE 4096u
#define FRAME_LAUNCH_MAX_PATH 1024u
struct frame_launch_config {
    unsigned abi;
    const char *target;
    const char *selector;
    const char *payload;
    const char *audit;
    const char *envelope;
    const char *output;
    const char *journal;
};
/* A generated, reviewed TU overrides the inert weak default. No runtime config I/O. */
const struct frame_launch_config *frame_compositor_launch_config_v1(void);
enum frame_launch_result {
    FRAME_LAUNCH_REWRITTEN = 1,
    FRAME_LAUNCH_CLEAN_UNCHANGED = 2,
    FRAME_LAUNCH_INVALID_CONFIG = 3,
    /* Already diagnostic-bearing, or cannot be proved clean within inspection bounds. */
    FRAME_LAUNCH_DIRTY_OVERFLOW = 4
};
struct frame_launch_environment {
    char *entries[FRAME_LAUNCH_MAX_ENTRIES + 8u];
    char preload[FRAME_LAUNCH_MAX_LOADER_VALUE + FRAME_LAUNCH_MAX_PATH * 2u + 32u];
    char audit[FRAME_LAUNCH_MAX_LOADER_VALUE + FRAME_LAUNCH_MAX_PATH + 32u];
    char values[3u][FRAME_LAUNCH_MAX_PATH + 64u];
    char **forward;
};
/* target is independently established from the public launch pathname. */
enum frame_launch_result frame_launch_build_environment(
    const struct frame_launch_config *, char *const envp[], int target,
    struct frame_launch_environment *);
int frame_launch_config_valid(const struct frame_launch_config *);
/* Exact path, or unambiguous public cwd/PATH resolution. Opaque spawn actions
 * prohibit relative target admission. Unknown paths still launch natively with
 * owned diagnostics removed. argv and native provider path stay untouched. */
int frame_launch_is_target(const struct frame_launch_config *, const char *file,
    const char *cwd, const char *search_path, int path_search, int has_actions);
#define FRAME_LAUNCH_FREEZE_ABI 1u
enum frame_launch_freeze_result {
    FRAME_LAUNCH_FROZEN = 0,
    FRAME_LAUNCH_FREEZE_INVALID_ABI = 1,
    FRAME_LAUNCH_FREEZE_INVALID_CONFIG = 2,
    FRAME_LAUNCH_FREEZE_WRONG_PROCESS = 3,
    FRAME_LAUNCH_FREEZE_NOT_COLD = 4,
    FRAME_LAUNCH_FREEZE_ENVIRONMENT = 5,
    FRAME_LAUNCH_FREEZE_PROVIDER = 6,
    FRAME_LAUNCH_FREEZE_ALREADY_ATTEMPTED = 7
};
/* Explicit cold-start handshake, never called automatically by this DSO.
 * The observer must call it before its first thread is started. Exact public
 * /proc/self/exe and bounded /proc/self/task metadata prove target identity and
 * a single-threaded phase while signals are blocked. Once exact target admission
 * begins, an attempt is sticky and cannot be retried. Success publishes a clean actual environ:
 * native internal exec, system and popen inherit only the tiny selector.
 * Five exact diagnostic getenv keys remain frozen for this PID only, allowing
 * lazy capture/layer reads. Fork children never receive those virtual values.
 * Unknown diagnostic keys, duplicated keys and non-exact values are refused.
 * After single-thread admission, a denied handshake also removes owned launch
 * settings without exposing frozen keys. If cold admission itself fails, no
 * environ mutation is safe: start no diagnostic threads and reject readiness.
 * errno is preserved on every result. New threads may start only after success. */
int frame_compositor_launch_freeze_environment_v1(unsigned abi);
/* Read-only PID-local admission; inherited fork state is never admitted. */
unsigned frame_compositor_launch_frozen_status_v1(void);
/* Genuine execve/execvpe/spawn/spawnp/getenv/secure_getenv: all bits = 63. */
unsigned frame_compositor_launch_provider_mask(void);
#ifdef __cplusplus
}
#endif
#endif
