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
/* OPTIONAL, NOT CALLED by this prototype. Lazy Vulkan/capture env reads and
 * an existing observer writer thread currently prohibit production use.
 * Call ONCE, during single-threaded startup, AFTER the observer has copied every
 * startup value it needs. Never call before the observer freezes its config.
 * This changes only the current environ pointer, not caller-owned env strings.
 * 0: clean baseline published; E2BIG/EINVAL: no mutation. */
int frame_compositor_launch_clean_environment(void);
/* Genuine GLIBC-versioned execve/execvpe/spawn/spawnp providers: all bits = 15. */
unsigned frame_compositor_launch_provider_mask(void);
#ifdef __cplusplus
}
#endif
#endif
