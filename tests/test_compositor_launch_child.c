#define _GNU_SOURCE 1
#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
static int payload, audit;
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
    dl_iterate_phdr(mapped, NULL);
    p = dlsym(RTLD_DEFAULT, "frame_compositor_launch_provider_mask"); memcpy(&providers, &p, sizeof p);
    p = dlsym(RTLD_DEFAULT, "frame_fixture_tls_value"); memcpy(&tls_value, &p, sizeof p);
    printf("{\"tag\":"); quoted(tag);
    printf(",\"payload_mapped\":%d,\"audit_base_namespace\":%d,\"provider_mask\":%u,\"tls_value\":%d", payload, audit, providers ? providers() : 0, tls_value ? tls_value() : -1);
    field("preload", getenv("LD_PRELOAD")); field("audit", getenv("LD_AUDIT"));
    field("trace", getenv("FRAME_MASK_TRACE")); field("bindings", getenv("FRAME_MASK_TRACE_BINDINGS"));
    field("envelope", getenv("GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE"));
    field("output", getenv("GHVR_FRAME_COMPOSITOR_RAW_OUTPUT")); field("journal", getenv("FRAME_MASK_TRACE_OUTPUT"));
    field("foreign", getenv("FOREIGN_UNRELATED")); puts("}"); fflush(stdout);
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
int main(int argc, char **argv) {
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
