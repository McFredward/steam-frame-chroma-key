// Owned host DSO for cold provider-verifier tests; unrelated to any native ICD.
extern "C" {
__attribute__((visibility("default"),noinline)) void lease_fixture_begin() { asm volatile("" ::: "memory"); }
__attribute__((visibility("default"),noinline)) void lease_fixture_next() { asm volatile("" ::: "memory"); }
__attribute__((visibility("default"),noinline)) void lease_fixture_wrapper() { asm volatile("" ::: "memory"); }
}
