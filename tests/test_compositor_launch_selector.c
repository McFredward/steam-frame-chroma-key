#include "receiver/compositor_launch_selector.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); abort(); } } while (0)
static const struct frame_launch_config config = {1, "/opt/steamvr/bin/linuxarm64/vrcompositor", "/owned/selector.so", "/owned/payload.so", "/owned/audit.so", "/owned/envelope", "/owned/output", "/owned/journal"};
static const char *value(char *const env[], const char *key) {
    size_t n = strlen(key), i;
    for (i = 0; env && env[i]; ++i) if (!strncmp(env[i], key, n) && env[i][n] == '=') return env[i] + n + 1;
    return NULL;
}
int main(void) {
    struct frame_launch_environment b;
    char foreign[] = "KEEP=unchanged with spaces";
    char *clean[] = {foreign, "LD_PRELOAD=/foreign/one.so /foreign/two.so", "LD_AUDIT=/foreign/audit.so", NULL};
    char *dirty[] = {foreign, "LD_PRELOAD=/foreign/one.so:/owned/payload.so:/owned/selector.so", "LD_AUDIT=/foreign/audit.so:/owned/audit.so", "FRAME_MASK_TRACE=1", "GHVR_FRAME_COMPOSITOR_RAW_OUTPUT=/stale", NULL};
    CHECK(frame_launch_config_valid(&config));
    CHECK(frame_launch_build_environment(&config, clean, 1, &b) == FRAME_LAUNCH_REWRITTEN);
    CHECK(b.forward[0] == foreign);
    CHECK(!strcmp(value(b.forward, "KEEP"), "unchanged with spaces"));
    CHECK(!strcmp(value(b.forward, "LD_PRELOAD"), "/foreign/one.so /foreign/two.so:/owned/selector.so:/owned/payload.so"));
    CHECK(!strcmp(value(b.forward, "LD_AUDIT"), "/foreign/audit.so:/owned/audit.so"));
    CHECK(!strcmp(value(b.forward, "GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE"), config.envelope));
    CHECK(!strcmp(value(b.forward, "FRAME_MASK_TRACE_OUTPUT"), config.journal));
    CHECK(frame_launch_build_environment(&config, dirty, 0, &b) == FRAME_LAUNCH_REWRITTEN);
    CHECK(b.forward[0] == foreign);
    CHECK(!strcmp(value(b.forward, "LD_PRELOAD"), "/foreign/one.so:::/owned/selector.so"));
    CHECK(!strcmp(value(b.forward, "LD_AUDIT"), "/foreign/audit.so:"));
    CHECK(!value(b.forward, "FRAME_MASK_TRACE"));
    CHECK(!value(b.forward, "GHVR_FRAME_COMPOSITOR_RAW_OUTPUT"));
    CHECK(frame_launch_build_environment(&config, NULL, 0, &b) == FRAME_LAUNCH_REWRITTEN);
    CHECK(!strcmp(value(b.forward, "LD_PRELOAD"), config.selector));
    CHECK(!value(b.forward, "LD_AUDIT"));
    CHECK(frame_launch_is_target(&config, config.target, NULL, NULL, 0, 1));
    CHECK(!frame_launch_is_target(&config, "/opt/steamvr/bin/linuxarm64/not-compositor", NULL, NULL, 0, 0));
    CHECK(!frame_launch_is_target(&config, "/opt/steamvr/bin/linuxarm64/../linuxarm64/vrcompositor", NULL, NULL, 0, 0));
    CHECK(frame_launch_is_target(&config, "./vrcompositor", "/opt/steamvr/bin/linuxarm64", NULL, 0, 0));
    CHECK(!frame_launch_is_target(&config, "vrcompositor", "/other", NULL, 0, 0));
    CHECK(!frame_launch_is_target(&config, "vrcompositor", "/opt/steamvr/bin/linuxarm64", NULL, 0, 1));
    CHECK(frame_launch_is_target(&config, "vrcompositor", "/other", "/opt/steamvr/bin/linuxarm64", 1, 0));
    CHECK(!frame_launch_is_target(&config, "vrcompositor", "/other", "/opt/steamvr/bin/linuxarm64:/other", 1, 0));
    CHECK(!frame_launch_is_target(&config, "vrcompositor", "/opt/steamvr/bin/linuxarm64", NULL, 1, 0));
    CHECK(frame_launch_is_target(&config, "vrcompositor", "/opt/steamvr/bin/linuxarm64", ".", 1, 0));
    {
        char long_path[FRAME_LAUNCH_MAX_PATH + 2u];
        memset(long_path, 'x', sizeof long_path - 1u); long_path[sizeof long_path - 1u] = 0;
        CHECK(!frame_launch_is_target(&config, "vrcompositor", "/opt/steamvr/bin/linuxarm64", long_path, 1, 0));
    }
    CHECK(!frame_launch_is_target(&config, "../vrcompositor", "/opt/steamvr/bin/linuxarm64", NULL, 0, 0));
    {
        struct frame_launch_config invalid = config;
        invalid.payload = "/owned/bad:payload";
        CHECK(!frame_launch_config_valid(&invalid));
        CHECK(frame_launch_build_environment(&invalid, dirty, 0, &b) == FRAME_LAUNCH_INVALID_CONFIG);
        CHECK(b.forward == dirty);
        invalid.payload = "/owned/../payload.so"; CHECK(!frame_launch_config_valid(&invalid));
        invalid.payload = "/owned//payload.so"; CHECK(!frame_launch_config_valid(&invalid));
        invalid.payload = "/owned/./payload.so"; CHECK(!frame_launch_config_valid(&invalid));
        invalid.payload = "/owned/payload.so/"; CHECK(!frame_launch_config_valid(&invalid));
    }
    {
        char **many = calloc(FRAME_LAUNCH_MAX_ENTRIES + 3u, sizeof(char *));
        size_t i;
        for (i = 0; i <= FRAME_LAUNCH_MAX_ENTRIES; ++i) many[i] = foreign;
        CHECK(frame_launch_build_environment(&config, many, 1, &b) == FRAME_LAUNCH_CLEAN_UNCHANGED);
        CHECK(b.forward == many);
        many[FRAME_LAUNCH_MAX_ENTRIES] = "FRAME_MASK_TRACE_BINDINGS=1";
        CHECK(frame_launch_build_environment(&config, many, 0, &b) == FRAME_LAUNCH_DIRTY_OVERFLOW);
        many[FRAME_LAUNCH_MAX_ENTRIES] = "LD_AUDIT=/owned/audit.so";
        CHECK(frame_launch_build_environment(&config, many, 0, &b) == FRAME_LAUNCH_DIRTY_OVERFLOW);
        free(many);
        many = calloc(FRAME_LAUNCH_MAX_INSPECT_ENTRIES + 2u, sizeof(char *));
        for (i = 0; i <= FRAME_LAUNCH_MAX_INSPECT_ENTRIES; ++i) many[i] = foreign;
        CHECK(frame_launch_build_environment(&config, many, 0, &b) == FRAME_LAUNCH_DIRTY_OVERFLOW);
        free(many);
    }
    {
        char *large = malloc(FRAME_LAUNCH_MAX_ENV_BYTES + 100u);
        char *input[] = {large, NULL};
        memcpy(large, "LARGE=", 6); memset(large + 6, 'x', FRAME_LAUNCH_MAX_ENV_BYTES + 92u); large[FRAME_LAUNCH_MAX_ENV_BYTES + 98u] = 0;
        CHECK(frame_launch_build_environment(&config, input, 0, &b) == FRAME_LAUNCH_CLEAN_UNCHANGED);
        CHECK(b.forward == input);
        memcpy(large, "FRAME_MASK_TRACE=", 17);
        CHECK(frame_launch_build_environment(&config, input, 0, &b) == FRAME_LAUNCH_DIRTY_OVERFLOW);
        free(large);
    }
    {
        char *dups[] = {"LD_PRELOAD=/foreign/one", "LD_PRELOAD=/foreign/two", NULL};
        CHECK(frame_launch_build_environment(&config, dups, 1, &b) == FRAME_LAUNCH_CLEAN_UNCHANGED);
        dups[1] = "LD_PRELOAD=/owned/payload.so";
        CHECK(frame_launch_build_environment(&config, dups, 0, &b) == FRAME_LAUNCH_DIRTY_OVERFLOW);
    }
    {
        char *maximum[FRAME_LAUNCH_MAX_ENTRIES + 1u];
        size_t i;
        for (i = 0; i < FRAME_LAUNCH_MAX_ENTRIES; ++i) maximum[i] = foreign;
        maximum[FRAME_LAUNCH_MAX_ENTRIES] = NULL;
        CHECK(frame_launch_build_environment(&config, maximum, 1, &b) == FRAME_LAUNCH_REWRITTEN);
        for (i = 0; b.forward[i]; ++i) {}
        CHECK(i == FRAME_LAUNCH_MAX_ENTRIES + 7u);
    }
    printf("launch selector core: %u checks passed\n", checks);
    return 0;
}
