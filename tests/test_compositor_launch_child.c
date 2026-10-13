#define _GNU_SOURCE 1
#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
extern char **environ;
static int payload, audit;
typedef int (*freeze_fn)(unsigned);
typedef unsigned (*status_fn)(void);
static freeze_fn freeze_api(void) {
    freeze_fn result = NULL; void *p = dlsym(RTLD_DEFAULT, "frame_compositor_launch_freeze_environment_v1");
    memcpy(&result, &p, sizeof p); return result;
}
static unsigned frozen_status(void) {
    status_fn result = NULL; void *p = dlsym(RTLD_DEFAULT, "frame_compositor_launch_frozen_status_v1");
    memcpy(&result, &p, sizeof p); return result ? result() : 0;
}
static const char *actual(const char *key) {
    size_t n = strlen(key); char **entry;
    for (entry = environ; entry && *entry; ++entry)
        if (!strncmp(*entry, key, n) && (*entry)[n] == '=') return *entry + n + 1u;
    return NULL;
}
static int mapped(struct dl_phdr_info *i, size_t size, void *unused) {
    (void)size; (void)unused;
    if (strstr(i->dlpi_name, "fixture_payload")) payload = 1;
    if (strstr(i->dlpi_name, "fixture_audit")) audit = 1;
    return 0;
}
static void quoted(const char *s) {
    const unsigned char *p = (const unsigned char *)(s ? s : ""); putchar('"');
    for (; *p; ++p) { if (*p == '"' || *p == '\\') putchar('\\'); if (*p >= 32) putchar(*p); }
    putchar('"');
}
static void field(const char *key, const char *v) { putchar(','); quoted(key); putchar(':'); quoted(v); }
static void report(const char *tag) {
    unsigned (*providers)(void) = NULL; void *p;
    int (*tls_value)(void) = NULL;
    const char *(*early_foreign)(void) = NULL, *(*early_secure)(void) = NULL;
    int (*ctor_result)(void) = NULL, (*early_errno)(void) = NULL, (*early_edges)(void) = NULL;
    dl_iterate_phdr(mapped, NULL);
    p = dlsym(RTLD_DEFAULT, "frame_compositor_launch_provider_mask"); memcpy(&providers, &p, sizeof p);
    p = dlsym(RTLD_DEFAULT, "frame_fixture_tls_value"); memcpy(&tls_value, &p, sizeof p);
    p = dlsym(RTLD_DEFAULT, "frame_fixture_early_foreign"); memcpy(&early_foreign, &p, sizeof p);
    p = dlsym(RTLD_DEFAULT, "frame_fixture_early_secure"); memcpy(&early_secure, &p, sizeof p);
    p = dlsym(RTLD_DEFAULT, "frame_fixture_ctor_result"); memcpy(&ctor_result, &p, sizeof p);
    p = dlsym(RTLD_DEFAULT, "frame_fixture_early_errno"); memcpy(&early_errno, &p, sizeof p);
    p = dlsym(RTLD_DEFAULT, "frame_fixture_early_edges"); memcpy(&early_edges, &p, sizeof p);
    printf("{\"tag\":"); quoted(tag);
    printf(",\"payload_mapped\":%d,\"audit_base_namespace\":%d,\"provider_mask\":%u,\"tls_value\":%d", payload, audit, providers ? providers() : 0, tls_value ? tls_value() : -1);
    field("preload", getenv("LD_PRELOAD")); field("audit", getenv("LD_AUDIT"));
    field("trace", getenv("FRAME_MASK_TRACE")); field("bindings", getenv("FRAME_MASK_TRACE_BINDINGS"));
    field("envelope", getenv("GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE"));
    field("output", getenv("GHVR_FRAME_COMPOSITOR_RAW_OUTPUT")); field("journal", getenv("FRAME_MASK_TRACE_OUTPUT"));
    field("foreign", getenv("FOREIGN_UNRELATED"));
    if (ctor_result) {
        printf(",\"ctor_result\":%d,\"early_errno\":%d,\"early_edges\":%d", ctor_result(), early_errno(), early_edges());
        field("early_foreign", early_foreign()); field("early_secure_foreign", early_secure());
    }
    puts("}"); fflush(stdout);
}
static void freeze_report(unsigned abi) {
    freeze_fn freeze = freeze_api();
    int saved, result;
    uint64_t before = 0, after = 0;
    if (syscall(SYS_rt_sigprocmask, 0, NULL, &before, sizeof before)) exit(106);
    errno = 77; result = freeze ? freeze(abi) : 99; saved = errno;
    if (syscall(SYS_rt_sigprocmask, 0, NULL, &after, sizeof after)) exit(107);
    printf("{\"freeze_result\":%d,\"errno\":%d,\"frozen_status\":%u,\"signal_mask_unchanged\":%u", result, saved, frozen_status(), before == after);
    field("actual_preload", actual("LD_PRELOAD")); field("actual_audit", actual("LD_AUDIT"));
    field("actual_trace", actual("FRAME_MASK_TRACE")); field("actual_envelope", actual("GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE"));
    field("secure_trace", secure_getenv("FRAME_MASK_TRACE")); puts("}"); fflush(stdout);
}
static int launch(const char *route, const char *path, char **argv, int actions_mode) {
    pid_t pid; int code, status;
    posix_spawn_file_actions_t actions, *ap = NULL;
    if (actions_mode) { posix_spawn_file_actions_init(&actions); posix_spawn_file_actions_addchdir_np(&actions, "/"); ap = &actions; }
    if (!strcmp(route, "execve")) return execve(path, argv, environ);
    if (!strcmp(route, "execv")) return execv(path, argv);
    if (!strcmp(route, "execvpe")) return execvpe(path, argv, environ);
    if (!strcmp(route, "execvp")) return execvp(path, argv);
    errno = 77;
    if (!strcmp(route, "spawn")) code = posix_spawn(&pid, path, ap, NULL, argv, environ);
    else if (!strcmp(route, "spawnp")) code = posix_spawnp(&pid, path, ap, NULL, argv, environ);
    else return 90;
    if (ap) posix_spawn_file_actions_destroy(ap);
    if (code) { printf("{\"error\":%d,\"errno\":%d}\n", code, errno); return 91; }
    if (waitpid(pid, &status, 0) != pid) return 92;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 93;
}
static int internal_launch(const char *route, const char *path) {
    char command[4096]; FILE *reader; char bytes[8192]; size_t n;
    if (snprintf(command, sizeof command, "'%s' --report internal-child", path) >= (int)sizeof command) return 95;
    if (!strcmp(route, "system")) return system(command);
    if (strcmp(route, "popen")) return 96;
    reader = popen(command, "r"); if (!reader) return 97;
    while ((n = fread(bytes, 1, sizeof bytes, reader))) fwrite(bytes, 1, n, stdout);
    return pclose(reader);
}
static void *held_thread(void *arg) {
    char b; int fd = *(int *)arg;
    if (read(fd, &b, 1) != 1) return (void *)1;
    return NULL;
}
int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--freeze-denied")) {
        if (!strcmp(argv[2], "threads")) {
            int channel[2]; pthread_t thread; void *joined = NULL;
            if (pipe(channel) || pthread_create(&thread, NULL, held_thread, &channel[0])) return 98;
            freeze_report(1); if (write(channel[1], "x", 1) != 1 || pthread_join(thread, &joined) || joined) return 99;
            close(channel[0]); close(channel[1]); freeze_report(1); return 0;
        }
        if (!strcmp(argv[2], "value")) setenv("FRAME_MASK_TRACE", "0", 1);
        if (!strcmp(argv[2], "unknown")) setenv("GHVR_FRAME_COMPOSITOR_RAW_UNKNOWN", "1", 1);
        if (!strcmp(argv[2], "missing")) unsetenv("FRAME_MASK_TRACE_BINDINGS");
        if (!strcmp(argv[2], "duplicate")) {
            size_t n = 0, i; char **copy;
            while (environ[n]) ++n;
            copy = calloc(n + 2u, sizeof(char *)); if (!copy) return 105;
            for (i = 0; i < n; ++i) copy[i] = environ[i];
            copy[n] = "FRAME_MASK_TRACE=1"; environ = copy;
        }
        freeze_report(!strcmp(argv[2], "abi") ? 999 : 1); return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "--frozen")) {
        report("target-before-freeze"); freeze_report(1); report("target-frozen");
        if (!strcmp(argv[2], "system") || !strcmp(argv[2], "popen")) return internal_launch(argv[2], argv[3]);
        if (!strcmp(argv[2], "fork")) {
            pid_t child = fork(); int status;
            if (child < 0) return 100;
            if (!child) {
                printf("{\"fork_frozen_status\":%u", frozen_status());
                field("trace", getenv("FRAME_MASK_TRACE")); field("secure_trace", secure_getenv("FRAME_MASK_TRACE"));
                field("actual_trace", actual("FRAME_MASK_TRACE")); puts("}"); fflush(stdout); _exit(0);
            }
            if (waitpid(child, &status, 0) != child) return 101;
            return WIFEXITED(status) ? WEXITSTATUS(status) : 102;
        }
        if (!strcmp(argv[2], "repeat")) { freeze_report(1); report("after-repeat"); return 0; }
        if (!strcmp(argv[2], "vulkan")) {
            void *library = dlopen(argv[3], RTLD_NOW | RTLD_LOCAL), *p;
            int (*run)(int, char **) = NULL;
            char *arguments[] = {"late-vulkan", "loader", NULL};
            if (!library) { fprintf(stderr, "late Vulkan dlopen: %s\n", dlerror()); return 103; }
            p = dlsym(library, "frame_fixture_vulkan_run"); memcpy(&run, &p, sizeof p);
            return run ? run(2, arguments) : 104;
        }
        { char *child[] = {argv[3], "--report", "descendant", NULL}; return launch(argv[2], argv[3], child, 0); }
    }
    if (argc >= 3 && !strcmp(argv[1], "--report")) { report(argv[2]); return 0; }
    if (argc >= 5 && (!strcmp(argv[1], "--relay") || !strcmp(argv[1], "--actions"))) {
        report("launcher"); return launch(argv[2], argv[3], argv + 3, !strcmp(argv[1], "--actions"));
    }
    if (argc == 4 && !strcmp(argv[1], "--descend")) {
        char *child[] = {argv[3], "--report", "descendant", NULL};
        report("target"); return launch(argv[2], argv[3], child, 0);
    }
    if (argc == 3 && !strcmp(argv[1], "--overflow")) {
        unsigned i; char **many = calloc(1027, sizeof(char *));
        char *child[] = {"/certainly/not/present", NULL};
        int r;
        for (i = 0; i < 1025; ++i) many[i] = "KEEP=x";
        many[1024] = "FRAME_MASK_TRACE=1";
        errno = 77;
        if (!strcmp(argv[2], "spawn")) { pid_t p; r = posix_spawn(&p, child[0], NULL, NULL, child, many); }
        else r = execve(child[0], child, many);
        printf("{\"result\":%d,\"errno\":%d}\n", r, errno); free(many); return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--error")) {
        char *child[] = {"/certainly/not/present", NULL};
        int r, saved;
        errno = 77; r = launch(argv[2], child[0], child, 0); saved = errno;
        printf("{\"result\":%d,\"errno\":%d}\n", r, saved); return 0;
    }
    return 94;
}
