/* A small launch selector, not a generic subprocess sandbox. Public exec/spawn
 * APIs below are covered. libc-internal exec (including system/popen), execl*,
 * fexecve/execveat and direct syscalls are not intercepted. Startup health must
 * verify actual non-target helper env/mappings; do not infer isolation merely
 * from compositor liveness. No cleanup API is called implicitly. */
#define _GNU_SOURCE 1
#include "compositor_launch_selector.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>
#ifndef FRAME_COMPOSITOR_LAUNCH_CORE_ONLY
#include <dlfcn.h>
#include <spawn.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#if defined(__GNUC__)
#define HIDDEN __attribute__((visibility("hidden")))
#define PUBLIC __attribute__((visibility("default")))
#else
#define HIDDEN
#define PUBLIC
#endif

static size_t bounded_length(const char *s, size_t limit) {
    size_t n = 0;
    if (!s) return limit;
    while (n < limit && s[n]) ++n;
    return n;
}
static int safe_path(const char *p) {
    size_t i, n = bounded_length(p, FRAME_LAUNCH_MAX_PATH);
    if (!n || n == FRAME_LAUNCH_MAX_PATH || p[0] != '/') return 0;
    for (i = 0; i < n; ++i)
        if ((unsigned char)p[i] < 33 || (unsigned char)p[i] > 126 || p[i] == ':' || p[i] == '=') return 0;
    return 1;
}
HIDDEN int frame_launch_config_valid(const struct frame_launch_config *c) {
    return c && c->abi == FRAME_LAUNCH_CONFIG_ABI && safe_path(c->target) &&
        safe_path(c->selector) && safe_path(c->payload) && safe_path(c->audit) &&
        safe_path(c->envelope) && safe_path(c->output) && safe_path(c->journal) &&
        strcmp(c->selector, c->payload) && strcmp(c->selector, c->audit) && strcmp(c->payload, c->audit);
}
static int starts(const char *s, const char *prefix) {
    size_t n = strlen(prefix);
    return !strncmp(s, prefix, n);
}
static int diagnostic_key(const char *s) {
    return starts(s, "GHVR_FRAME_COMPOSITOR_RAW_") || starts(s, "FRAME_MASK_TRACE");
}
static int separator(char ch) { return ch == ':' || ch == ' ' || ch == '\t'; }
static int token_equals(const char *p, size_t n, const char *value) {
    return strlen(value) == n && !memcmp(p, value, n);
}
static int loader_owned(const char *p, size_t n, const struct frame_launch_config *c, int audit, int include_selector) {
    return audit ? token_equals(p, n, c->audit) :
        token_equals(p, n, c->payload) || (include_selector && token_equals(p, n, c->selector));
}
/* Foreign tokens and their original separators remain byte-for-byte. Owned token
 * bytes alone are removed; repeated separators are legal loader delimiters. */
static int filter_loader(const char *value, char *out, size_t cap,
    const struct frame_launch_config *c, int audit, int *has_foreign) {
    size_t i = 0, used = 0;
    *has_foreign = 0;
    while (value[i]) {
        size_t begin = i, n;
        if (separator(value[i])) {
            if (used + 1 >= cap) return 0;
            out[used++] = value[i++];
            continue;
        }
        while (value[i] && !separator(value[i])) ++i;
        n = i - begin;
        if (loader_owned(value + begin, n, c, audit, 1)) continue;
        if (used + n >= cap) return 0;
        memcpy(out + used, value + begin, n); used += n; *has_foreign = 1;
    }
    out[used] = 0;
    if (!*has_foreign) out[0] = 0;
    return 1;
}
/* The secondary inspection reads at most 4,096 keys and 4 KiB per loader value.
 * Other oversized values are not traversed. Uninspectable input is unsafe, so
 * refusal replaces an unbounded search or accidental diagnostic propagation. */
static int environment_dirty(char *const envp[], const struct frame_launch_config *c) {
    size_t j;
    if (!envp) return 0;
    for (j = 0; j < FRAME_LAUNCH_MAX_INSPECT_ENTRIES && envp[j]; ++j) {
        const char *s = envp[j], *value;
        int audit;
        size_t i;
        if (diagnostic_key(s)) return 1;
        if (starts(s, "LD_PRELOAD=")) { value = s + 11; audit = 0; }
        else if (starts(s, "LD_AUDIT=")) { value = s + 9; audit = 1; }
        else continue;
        if (bounded_length(value, FRAME_LAUNCH_MAX_LOADER_VALUE + 1u) > FRAME_LAUNCH_MAX_LOADER_VALUE) return 1;
        for (i = 0; value[i];) {
            size_t begin, n;
            if (separator(value[i])) { ++i; continue; }
            begin = i;
            while (value[i] && !separator(value[i])) ++i;
            n = i - begin;
            if (loader_owned(value + begin, n, c, audit, 0)) return 1;
        }
    }
    return envp[j] != NULL;
}
static enum frame_launch_result oversized(char *const envp[], const struct frame_launch_config *c,
    struct frame_launch_environment *out) {
    out->forward = (char **)envp;
    return environment_dirty(envp, c) ? FRAME_LAUNCH_DIRTY_OVERFLOW : FRAME_LAUNCH_CLEAN_UNCHANGED;
}
static int append_path(char *out, size_t cap, const char *path) {
    size_t n = strlen(out), p = strlen(path);
    if (n + (n ? 1u : 0u) + p + 1 > cap) return 0;
    if (n) out[n++] = ':';
    memcpy(out + n, path, p + 1); return 1;
}
HIDDEN enum frame_launch_result frame_launch_build_environment(const struct frame_launch_config *c,
    char *const envp[], int target, struct frame_launch_environment *out) {
    size_t i, count = 0, bytes = 0, nout = 0;
    int preload_seen = 0, audit_seen = 0;
    if (!out) return FRAME_LAUNCH_INVALID_CONFIG;
    out->forward = (char **)envp;
    if (!frame_launch_config_valid(c)) return FRAME_LAUNCH_INVALID_CONFIG;
    if (envp) for (i = 0; envp[i]; ++i) {
        size_t n;
        if (i == FRAME_LAUNCH_MAX_ENTRIES) return oversized(envp, c, out);
        n = bounded_length(envp[i], FRAME_LAUNCH_MAX_ENV_BYTES + 1u);
        if (n >= FRAME_LAUNCH_MAX_ENV_BYTES || bytes > FRAME_LAUNCH_MAX_ENV_BYTES - n - 1u)
            return oversized(envp, c, out);
        bytes += n + 1; ++count;
        /* Duplicate loader keys need independent buffers; decline a malformed
         * duplicated loader input rather than silently changing its semantics. */
        if (starts(envp[i], "LD_PRELOAD=") && preload_seen++) return oversized(envp, c, out);
        if (starts(envp[i], "LD_AUDIT=") && audit_seen++) return oversized(envp, c, out);
    }
    for (i = 0; i < count; ++i) {
        char *s = envp[i];
        if (diagnostic_key(s)) continue;
        if (starts(s, "LD_PRELOAD=") || starts(s, "LD_AUDIT=")) {
            int is_audit = starts(s, "LD_AUDIT="), foreign;
            const char *value = s + (is_audit ? 9u : 11u);
            char *buffer = is_audit ? out->audit : out->preload;
            const char *key = is_audit ? "LD_AUDIT=" : "LD_PRELOAD=";
            size_t keylen = strlen(key), cap = is_audit ? sizeof out->audit : sizeof out->preload;
            if (bounded_length(value, FRAME_LAUNCH_MAX_LOADER_VALUE + 1u) > FRAME_LAUNCH_MAX_LOADER_VALUE)
                return oversized(envp, c, out);
            memcpy(buffer, key, keylen);
            if (!filter_loader(value, buffer + keylen, cap - keylen, c, is_audit, &foreign))
                return oversized(envp, c, out);
            if (is_audit) {
                if (target && !append_path(buffer + keylen, cap - keylen, c->audit)) return oversized(envp, c, out);
                if (foreign || target) out->entries[nout++] = buffer;
            } else {
                if (!append_path(buffer + keylen, cap - keylen, c->selector) ||
                    (target && !append_path(buffer + keylen, cap - keylen, c->payload))) return oversized(envp, c, out);
                out->entries[nout++] = buffer;
            }
        } else out->entries[nout++] = s;
    }
    if (!preload_seen) {
        strcpy(out->preload, "LD_PRELOAD=");
        if (!append_path(out->preload + 11, sizeof out->preload - 11, c->selector) ||
            (target && !append_path(out->preload + 11, sizeof out->preload - 11, c->payload))) return oversized(envp, c, out);
        out->entries[nout++] = out->preload;
    }
    if (target) {
        if (!audit_seen) {
            strcpy(out->audit, "LD_AUDIT=");
            if (!append_path(out->audit + 9, sizeof out->audit - 9, c->audit)) return oversized(envp, c, out);
            out->entries[nout++] = out->audit;
        }
        strcpy(out->values[0], "GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE="); strcat(out->values[0], c->envelope);
        strcpy(out->values[1], "GHVR_FRAME_COMPOSITOR_RAW_OUTPUT="); strcat(out->values[1], c->output);
        strcpy(out->values[2], "FRAME_MASK_TRACE_OUTPUT="); strcat(out->values[2], c->journal);
        out->entries[nout++] = out->values[0]; out->entries[nout++] = out->values[1]; out->entries[nout++] = out->values[2];
        out->entries[nout++] = "FRAME_MASK_TRACE=1";
        out->entries[nout++] = "FRAME_MASK_TRACE_BINDINGS=1";
    }
    out->entries[nout] = NULL; out->forward = out->entries;
    return FRAME_LAUNCH_REWRITTEN;
}
HIDDEN int frame_launch_is_target(const struct frame_launch_config *c, const char *file,
    const char *cwd, const char *search_path, int path_search, int has_actions) {
    const char *base, *rel = file;
    size_t dirlen, n;
    if (!frame_launch_config_valid(c) || !file || !*file) return 0;
    n = bounded_length(file, FRAME_LAUNCH_MAX_PATH);
    if (n == FRAME_LAUNCH_MAX_PATH) return 0;
    if (file[0] == '/') return !strcmp(file, c->target);
    if (has_actions) return 0;
    base = strrchr(c->target, '/'); dirlen = (size_t)(base - c->target);
    if (!strncmp(rel, "./", 2)) rel += 2;
    if (strchr(rel, '/') || strcmp(rel, base + 1)) return 0;
    if (path_search && !strchr(file, '/')) {
        if (!search_path || bounded_length(search_path, FRAME_LAUNCH_MAX_PATH) == FRAME_LAUNCH_MAX_PATH || strchr(search_path, ':')) return 0;
        if (*search_path && strcmp(search_path, "."))
            return strlen(search_path) == dirlen && !memcmp(search_path, c->target, dirlen);
    }
    return cwd && bounded_length(cwd, FRAME_LAUNCH_MAX_PATH) == dirlen && !memcmp(cwd, c->target, dirlen);
}

#ifndef FRAME_COMPOSITOR_LAUNCH_CORE_ONLY
extern char **environ;
__attribute__((weak, visibility("hidden"))) const struct frame_launch_config *frame_compositor_launch_config_v1(void) { return NULL; }
typedef int (*execve_fn)(const char *, char *const[], char *const[]);
typedef int (*spawn_fn)(pid_t *, const char *, const posix_spawn_file_actions_t *,
    const posix_spawnattr_t *, char *const[], char *const[]);
static execve_fn native_execve, native_execvpe;
static spawn_fn native_spawn, native_spawnp;
static struct frame_launch_environment clean_baseline;
static int cleaned;
#if defined(__aarch64__)
#define EXECVE_VERSION "GLIBC_2.17"
#define EXECVPE_VERSION "GLIBC_2.17"
#define SPAWN_VERSION "GLIBC_2.17"
#elif defined(__x86_64__)
#define EXECVE_VERSION "GLIBC_2.2.5"
#define EXECVPE_VERSION "GLIBC_2.11"
#define SPAWN_VERSION "GLIBC_2.15"
#else
#error "Launch selector provider versions must be reviewed for this architecture"
#endif
__attribute__((constructor)) static void resolve_providers(void) {
    int saved = errno;
    void *p = dlvsym(RTLD_NEXT, "execve", EXECVE_VERSION); memcpy(&native_execve, &p, sizeof p);
    p = dlvsym(RTLD_NEXT, "execvpe", EXECVPE_VERSION); memcpy(&native_execvpe, &p, sizeof p);
    p = dlvsym(RTLD_NEXT, "posix_spawn", SPAWN_VERSION); memcpy(&native_spawn, &p, sizeof p);
    p = dlvsym(RTLD_NEXT, "posix_spawnp", SPAWN_VERSION); memcpy(&native_spawnp, &p, sizeof p);
    errno = saved;
}
PUBLIC unsigned frame_compositor_launch_provider_mask(void) {
    return (native_execve ? 1u : 0u) | (native_execvpe ? 2u : 0u) |
        (native_spawn ? 4u : 0u) | (native_spawnp ? 8u : 0u);
}
static int target_path(const struct frame_launch_config *c, const char *path, int search, int actions) {
    char cwd[FRAME_LAUNCH_MAX_PATH];
    const char *current = NULL, *path_value = NULL;
    size_t i;
    if (!path || path[0] == '/') return frame_launch_is_target(c, path, NULL, NULL, search, actions);
    /* Linux public cwd metadata only. Avoid libc getcwd fallback directory
     * traversal/allocation after fork; failure declines relative admission. */
    if (!actions && syscall(SYS_getcwd, cwd, sizeof cwd) > 0) current = cwd;
    /* GNU execvpe's search PATH is the process environ, not its envp argument. */
    if (search && environ) for (i = 0; i < FRAME_LAUNCH_MAX_ENTRIES && environ[i]; ++i)
        if (starts(environ[i], "PATH=")) { path_value = environ[i] + 5; break; }
    return frame_launch_is_target(c, path, current, path_value, search, actions);
}
static enum frame_launch_result prepare(const char *path, int search, int actions,
    char *const envp[], struct frame_launch_environment *buffer) {
    const struct frame_launch_config *c = frame_compositor_launch_config_v1();
    return frame_launch_build_environment(c, envp, target_path(c, path, search, actions), buffer);
}
static int forward_exec(execve_fn provider, const char *path, char *const argv[], char *const envp[], int search) {
    struct frame_launch_environment buffer;
    int saved = errno;
    enum frame_launch_result r = prepare(path, search, 0, envp, &buffer);
    if (!provider) { errno = ENOSYS; return -1; }
    if (r == FRAME_LAUNCH_DIRTY_OVERFLOW) { errno = E2BIG; return -1; }
    errno = saved; return provider(path, argv, buffer.forward);
}
PUBLIC int execve(const char *path, char *const argv[], char *const envp[]) { return forward_exec(native_execve, path, argv, envp, 0); }
PUBLIC int execvpe(const char *path, char *const argv[], char *const envp[]) { return forward_exec(native_execvpe, path, argv, envp, 1); }
/* GLIBC execv/execvp are exact shims to execve/execvpe with process environ. */
PUBLIC int execv(const char *path, char *const argv[]) { return forward_exec(native_execve, path, argv, environ, 0); }
PUBLIC int execvp(const char *path, char *const argv[]) { return forward_exec(native_execvpe, path, argv, environ, 1); }
static int forward_spawn(spawn_fn provider, pid_t *pid, const char *path,
    const posix_spawn_file_actions_t *actions, const posix_spawnattr_t *attr,
    char *const argv[], char *const envp[], int search) {
    struct frame_launch_environment buffer;
    int saved = errno;
    enum frame_launch_result r = prepare(path, search, actions != NULL, envp, &buffer);
    if (!provider) { errno = saved; return ENOSYS; }
    if (r == FRAME_LAUNCH_DIRTY_OVERFLOW) { errno = saved; return E2BIG; }
    errno = saved; return provider(pid, path, actions, attr, argv, buffer.forward);
}
PUBLIC int posix_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *actions,
    const posix_spawnattr_t *attr, char *const argv[], char *const envp[]) {
    return forward_spawn(native_spawn, pid, path, actions, attr, argv, envp, 0);
}
PUBLIC int posix_spawnp(pid_t *pid, const char *path, const posix_spawn_file_actions_t *actions,
    const posix_spawnattr_t *attr, char *const argv[], char *const envp[]) {
    return forward_spawn(native_spawnp, pid, path, actions, attr, argv, envp, 1);
}
PUBLIC int frame_compositor_launch_clean_environment(void) {
    int saved = errno;
    enum frame_launch_result r;
    if (cleaned) { errno = saved; return 0; }
    r = frame_launch_build_environment(frame_compositor_launch_config_v1(), environ, 0, &clean_baseline);
    if (r == FRAME_LAUNCH_DIRTY_OVERFLOW) { errno = saved; return E2BIG; }
    if (r != FRAME_LAUNCH_REWRITTEN) { errno = saved; return EINVAL; }
    environ = clean_baseline.forward; cleaned = 1; errno = saved; return 0;
}
#endif
