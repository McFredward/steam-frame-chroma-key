#include "receiver_observer_core.h"
#include "sha256.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <gnu/libc-version.h>
#include <link.h>
#include <pthread.h>
#include <sys/auxv.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Different public alias names prevent the forward edge from resolving back to
// our interposed pthread_mutex_* definitions. Versions are link-time contracts;
// there is no dlsym/bootstrap path in a lock callback or constructor recursion.
extern "C" int receiver_libc_lock(pthread_mutex_t*);
extern "C" int receiver_libc_unlock(pthread_mutex_t*);
#if defined(__aarch64__)
__asm__(".symver receiver_libc_lock,__pthread_mutex_lock@GLIBC_2.17");
__asm__(".symver receiver_libc_unlock,__pthread_mutex_unlock@GLIBC_2.17");
#elif defined(__x86_64__)
__asm__(".symver receiver_libc_lock,__pthread_mutex_lock@GLIBC_2.2.5");
__asm__(".symver receiver_libc_unlock,__pthread_mutex_unlock@GLIBC_2.2.5");
#else
#error "Receiver observer supports only the audited AArch64 ABI and the x86-64 offline fixture"
#endif

namespace {
using namespace frame_receiver;
constexpr char kReceiverHash[] = "0000009e5c7124bb03bfd444f7200304e0bb3ec1ec0cf80f0086a19ec048132b";
constexpr std::uint64_t kDeadlineNs = 120ULL * 1000 * 1000 * 1000;
constexpr std::size_t kByteLimit = 1024 * 1024;
Observer observer{};
thread_local Scope local_scope __attribute__((tls_model("initial-exec"))) {};
std::atomic<bool> stopping{false};
std::atomic<std::uint64_t> callbacks{0}, unlock_failures{0};
pthread_t writer{};
bool writer_started = false;
int output_fd = -1, parent_fd = -1;
char arm_name[256]{};
std::uintptr_t load_bias = 0;
struct CodeRange { std::uintptr_t start = 0, end = 0; };
CodeRange executable_ranges[8]{};
std::size_t executable_range_count = 0;
std::atomic<bool> validated_image{false};
std::uint64_t started_ns = 0;
char executable_hash[65]{};
std::uint64_t sites[4] = {kSelectedLock, kSelectedUnlock, kMetadataLock, kMetadataUnlock};

std::uint64_t now() noexcept {
    timespec value{};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
    return static_cast<std::uint64_t>(value.tv_sec) * 1000000000ULL + value.tv_nsec;
}

std::uint64_t caller_site(void* return_address) noexcept {
    if (!validated_image.load(std::memory_order_acquire)) return 0;
    constexpr std::uint64_t native_sites[] = {kSelectedLock,kSelectedUnlock,kMetadataLock,kMetadataUnlock};
    const auto address = reinterpret_cast<std::uintptr_t>(return_address);
    for (std::size_t i = 0; i < executable_range_count; ++i) {
        if (address >= executable_ranges[i].start && address < executable_ranges[i].end) {
            const auto rva = address - load_bias;
            for (unsigned j = 0; j < 4; ++j)
                if (rva == sites[j]) return native_sites[j];
            return 0;
        }
    }
    return 0;
}

int main_image(dl_phdr_info* info, std::size_t, void*) noexcept {
    if (info->dlpi_name && info->dlpi_name[0]) return 0;
    load_bias = info->dlpi_addr;
    for (std::size_t i = 0; i < info->dlpi_phnum; ++i) {
        const auto& p = info->dlpi_phdr[i];
        if (p.p_type != PT_LOAD || !(p.p_flags & PF_X)) continue;
        if (executable_range_count == 8 || p.p_vaddr > UINTPTR_MAX - load_bias ||
            p.p_memsz > UINTPTR_MAX - load_bias - p.p_vaddr) {
            executable_range_count = 0; return 1;
        }
        executable_ranges[executable_range_count++] = {load_bias + p.p_vaddr,
                                                       load_bias + p.p_vaddr + p.p_memsz};
    }
    return 1;
}

bool provider_verified() noexcept {
    Dl_info lock{}, unlock{}, libc{};
    return dladdr(reinterpret_cast<void*>(&receiver_libc_lock), &lock) &&
           dladdr(reinterpret_cast<void*>(&receiver_libc_unlock), &unlock) &&
           dladdr(reinterpret_cast<void*>(&gnu_get_libc_version), &libc) &&
           lock.dli_fbase && lock.dli_fbase == unlock.dli_fbase &&
           lock.dli_fbase == libc.dli_fbase;
}

bool identify_executable() noexcept {
#if !defined(FRAME_RECEIVER_OBSERVER_TEST_FIXTURE)
    char path[4096]{};
    const auto path_size=readlink("/proc/self/exe",path,sizeof(path)-1);
    if (path_size <= 0 || static_cast<std::size_t>(path_size) >= sizeof(path)-1) return false;
    const char* basename=std::strrchr(path,'/');
    if (!basename || std::strcmp(basename+1,"vrlink")) return false;
#endif
    const int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat before{}, after{};
    Elf64_Ehdr header{};
    bool valid = fstat(fd, &before) == 0 && S_ISREG(before.st_mode) &&
                 before.st_size >= static_cast<off_t>(sizeof(header)) && before.st_size <= 16*1024*1024 &&
                 pread(fd, &header, sizeof(header), 0) == static_cast<ssize_t>(sizeof(header));
    valid = valid && std::memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 &&
            header.e_ident[EI_CLASS] == ELFCLASS64 && header.e_ident[EI_DATA] == ELFDATA2LSB &&
            (header.e_type == ET_DYN || header.e_type == ET_EXEC);
#if defined(FRAME_RECEIVER_OBSERVER_TEST_FIXTURE)
    valid = valid && header.e_machine == EM_X86_64;
#else
    valid = valid && header.e_machine == EM_AARCH64;
#endif
    Sha256 digest;
    unsigned char buffer[4096]{};
    off_t read_bytes = 0;
    while (valid && read_bytes < before.st_size) {
        const auto count = read(fd, buffer, sizeof(buffer));
        if (count <= 0) { valid = false; break; }
        read_bytes += count;
        if (read_bytes > before.st_size) { valid = false; break; }
        digest.update(buffer, static_cast<std::size_t>(count));
    }
    valid = valid && fstat(fd, &after) == 0 && read_bytes == before.st_size &&
            before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
            before.st_size == after.st_size && before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
            before.st_mtim.tv_nsec == after.st_mtim.tv_nsec;
    close(fd);
    if (!valid) return false;
    const auto bytes = digest.finish();
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) {
        executable_hash[2*i] = hex[bytes[i] >> 4];
        executable_hash[2*i+1] = hex[bytes[i] & 15];
    }
#if defined(FRAME_RECEIVER_OBSERVER_TEST_FIXTURE)
    const char* expected = std::getenv("FRAME_RECEIVER_TEST_SHA256");
    if (!expected || std::strlen(expected) != 64 || std::strcmp(executable_hash, expected)) return false;
#else
    if (std::strcmp(executable_hash, kReceiverHash)) return false;
#endif
    dl_iterate_phdr(main_image, nullptr);
    if (!executable_range_count) return false;
#if defined(FRAME_RECEIVER_OBSERVER_TEST_FIXTURE)
    const char* labels[] = {"fixture_selected_lock_return", "fixture_selected_unlock_return",
                           "fixture_metadata_lock_return", "fixture_metadata_unlock_return"};
    for (unsigned i = 0; i < 4; ++i) {
        const auto address = reinterpret_cast<std::uintptr_t>(dlsym(RTLD_DEFAULT, labels[i]));
        if (!address || address < load_bias) return false;
        sites[i] = address - load_bias;
    }
#endif
    // All four sites must lie inside executable PT_LOAD bounds.
    for (unsigned i = 0; i < 4; ++i) {
        bool inside = false;
        if (sites[i] > UINTPTR_MAX - load_bias) return false;
        for (std::size_t j = 0; j < executable_range_count; ++j)
            inside = inside || (load_bias + sites[i] >= executable_ranges[j].start &&
                                load_bias + sites[i] < executable_ranges[j].end);
        if (!inside) return false;
    }
    return true;
}

bool path_parts(const char* input, char* directory, char* basename) noexcept {
    if (!input || input[0] != '/' || std::strlen(input) >= 4096) return false;
    const char* slash = std::strrchr(input, '/');
    const auto length = static_cast<std::size_t>(slash - input);
    if (!length || !slash[1] || std::strlen(slash+1) > 255) return false;
    std::memcpy(directory, input, length); directory[length] = 0;
    std::strcpy(basename, slash+1);
    return std::strcmp(basename, ".") && std::strcmp(basename, "..");
}

int private_directory(const char* path) noexcept {
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    const char* next = path + 1;
    while (*next) {
        const char* slash = std::strchr(next, '/');
        const auto length = slash ? static_cast<std::size_t>(slash-next) : std::strlen(next);
        char part[256]{};
        if (!length || length > 255) { close(fd); return -1; }
        std::memcpy(part, next, length);
        if (!std::strcmp(part,".") || !std::strcmp(part,"..")) { close(fd); return -1; }
        const int child = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(fd); fd = child;
        if (fd < 0) return -1;
        if (!slash) break;
        next = slash + 1;
    }
    struct stat directory{};
    if (fstat(fd,&directory) || directory.st_uid != geteuid() || (directory.st_mode & 0077)) {
        close(fd); return -1;
    }
    return fd;
}

bool prepare_output() noexcept {
    char output_directory[4096]{}, arm_directory[4096]{}, output_name[256]{};
    if (!path_parts(std::getenv("FRAME_RECEIVER_TRACE_OUTPUT"), output_directory, output_name) ||
        !path_parts(std::getenv("FRAME_RECEIVER_TRACE_ARM"), arm_directory, arm_name) ||
        std::strcmp(output_directory,arm_directory) || !std::strcmp(output_name,arm_name)) return false;
    parent_fd = private_directory(output_directory);
    if (parent_fd < 0) return false;
    output_fd = openat(parent_fd, output_name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    return output_fd >= 0;
}

bool arm_present() noexcept {
    struct stat marker{};
    return fstatat(parent_fd, arm_name, &marker, AT_SYMLINK_NOFOLLOW) == 0 &&
           S_ISREG(marker.st_mode) && marker.st_uid == geteuid() &&
           (marker.st_mode & 0077) == 0 && marker.st_size == 0;
}

bool write_line(const char* data, std::size_t length, std::size_t& output_bytes) noexcept {
    if (length > kByteLimit - output_bytes) return false;
    std::size_t written = 0;
    while (written < length) {
        const auto count = write(output_fd, data + written, length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        written += static_cast<std::size_t>(count);
    }
    output_bytes += length;
    return true;
}

void* record_loop(void*) noexcept {
    // Only this background writer owns the liveness lock. An open descriptor
    // or a live receiver does not prove the bounded recorder is still running.
    if (flock(output_fd, LOCK_EX | LOCK_NB) != 0) {
        observer.armed.store(false,std::memory_order_seq_cst);
        return nullptr;
    }
    std::size_t bytes = 0;
    char line[2048]{};
    auto count = std::snprintf(line,sizeof(line),
        "{\"kind\":\"header\",\"supported\":true,\"executable_sha256\":\"%s\","
        "\"sample_limit\":32,\"byte_limit\":1048576,\"deadline_seconds\":120,"
        "\"writer_liveness\":\"exclusive_flock_v1\","
        "\"evidence\":\"protected_selection_attempt\",\"accepted_submit\":false,"
        "\"source_frame_associated\":false,\"gpu_consumed_coherent\":false}\n",executable_hash);
    bool io_ok = count > 0 && write_line(line,static_cast<std::size_t>(count),bytes);
    bool was_armed = false, deadline = false;
    std::uint64_t serialized = 0;
    while (io_ok && !stopping.load(std::memory_order_acquire)) {
        const auto time = now();
        if (!time || time < started_ns || time - started_ns >= kDeadlineNs) { deadline=true; break; }
        if (!was_armed && arm_present()) {
            was_armed = true;
            observer.armed.store(true,std::memory_order_seq_cst);
        }
        Record record{};
        while (observer.take(record)) {
            count = std::snprintf(line,sizeof(line),
                "{\"kind\":\"selection_attempt\",\"observation\":%llu,\"native_token\":%llu,"
                "\"native_key\":%u,\"content_generation\":%llu,\"native_metadata_word\":%u,"
                "\"metadata_pairs\":%u,\"metadata_valid\":%s,\"reason_bits\":%u,"
                "\"accepted_submit\":false,\"source_frame_associated\":false}\n",
                static_cast<unsigned long long>(record.observation),
                static_cast<unsigned long long>(record.selected.token),record.selected.key,
                static_cast<unsigned long long>(record.selected.content_generation),record.metadata_word,
                record.metadata_pairs,record.metadata_valid?"true":"false",record.reason);
            io_ok = count > 0 && static_cast<std::size_t>(count) < sizeof(line) &&
                    write_line(line,static_cast<std::size_t>(count),bytes);
            if (!io_ok) break;
            ++serialized;
        }
        if (observer.counters.completed.load(std::memory_order_acquire) >= kSampleLimit) break;
        const timespec interval{0,5000000}; nanosleep(&interval,nullptr);
    }
    observer.armed.store(false,std::memory_order_seq_cst);
    // This wait belongs to the recorder only. Receiver callbacks never wait.
    for (unsigned i=0;i<20 && !observer.quiescent(callbacks);++i) {
        const timespec interval{0,5000000}; nanosleep(&interval,nullptr);
    }
    // Latch before the final drain. A late producer after a timed-out closure
    // cannot turn old counters plus a newly empty active count into completion.
    const bool closed_quiescent=observer.quiescent(callbacks);
    Record tail{};
    while (io_ok && observer.take(tail)) {
        count=std::snprintf(line,sizeof(line),
            "{\"kind\":\"selection_attempt\",\"observation\":%llu,\"native_token\":%llu,"
            "\"native_key\":%u,\"content_generation\":%llu,\"native_metadata_word\":%u,"
            "\"metadata_pairs\":%u,\"metadata_valid\":%s,\"reason_bits\":%u,"
            "\"accepted_submit\":false,\"source_frame_associated\":false}\n",
            static_cast<unsigned long long>(tail.observation),static_cast<unsigned long long>(tail.selected.token),
            tail.selected.key,static_cast<unsigned long long>(tail.selected.content_generation),tail.metadata_word,
            tail.metadata_pairs,tail.metadata_valid?"true":"false",tail.reason);
        io_ok=count>0 && write_line(line,static_cast<std::size_t>(count),bytes);
        if (io_ok) ++serialized;
    }
    const auto completed=observer.counters.completed.load();
    const auto active=observer.counters.active.load();
    const auto queue_loss=observer.counters.queue_loss.load();
    const auto invalid=observer.counters.invalid.load();
    const auto lock_failure=observer.counters.lock_failures.load();
    const auto unlock_failure=unlock_failures.load();
    const auto pending_callbacks=callbacks.load(std::memory_order_seq_cst);
    const bool complete=closed_quiescent && was_armed && serialized>0 && io_ok && serialized==completed && !active && !pending_callbacks && !queue_loss && !invalid &&
                        !lock_failure && !unlock_failure && !observer.counters.unmatched.load();
    count=std::snprintf(line,sizeof(line),
        "{\"kind\":\"footer\",\"supported\":true,\"armed\":%s,\"deadline_reached\":%s,"
        "\"serialized\":%llu,\"completed\":%llu,\"active_scopes\":%llu,\"valid\":%llu,"
        "\"invalid\":%llu,\"cap_skipped\":%llu,\"queue_loss\":%llu,\"lock_failures\":%llu,\"pending_callbacks\":%llu,"
        "\"unlock_failures\":%llu,\"unmatched\":%llu,\"nested\":%llu,\"pointer_rejections\":%llu,"
        "\"closed_quiescent\":%s,\"selection_evidence_complete\":%s,\"io_ok\":%s,\"accepted_submit\":false,"
        "\"source_frame_associated\":false,\"gpu_consumed_coherent\":false}\n",
        was_armed?"true":"false",deadline?"true":"false",static_cast<unsigned long long>(serialized),
        static_cast<unsigned long long>(completed),static_cast<unsigned long long>(active),
        static_cast<unsigned long long>(observer.counters.valid.load()),static_cast<unsigned long long>(invalid),
        static_cast<unsigned long long>(observer.counters.cap_skipped.load()),static_cast<unsigned long long>(queue_loss),
        static_cast<unsigned long long>(lock_failure),static_cast<unsigned long long>(pending_callbacks),static_cast<unsigned long long>(unlock_failure),
        static_cast<unsigned long long>(observer.counters.unmatched.load()),
        static_cast<unsigned long long>(observer.counters.nested.load()),
        static_cast<unsigned long long>(observer.counters.pointer_rejections.load()),
        closed_quiescent?"true":"false",complete?"true":"false",io_ok?"true":"false");
    if (count>0) write_line(line,static_cast<std::size_t>(count),bytes);
    flock(output_fd, LOCK_UN);
    return nullptr;
}

void refusal(const char* reason) noexcept {
    char line[384]{};
    const auto count=std::snprintf(line,sizeof(line),
        "{\"kind\":\"header\",\"supported\":false,\"reason\":\"%s\",\"observation_enabled\":false}\n"
        "{\"kind\":\"footer\",\"supported\":false,\"serialized\":0,\"source_frame_associated\":false}\n",reason);
    std::size_t bytes=0;
    if (count>0 && output_fd>=0) write_line(line,static_cast<std::size_t>(count),bytes);
}

__attribute__((constructor)) void initialize() noexcept {
    const int saved_errno=errno;
    // A service-level preload is inherited by unrelated children. They must not
    // claim the receiver's exclusive output file or hash arbitrary executables.
    if (!std::getenv("FRAME_RECEIVER_TRACE_OUTPUT") || !std::getenv("FRAME_RECEIVER_TRACE_ARM") ||
        !identify_executable()) { errno=saved_errno; return; }
    if (!prepare_output()) { errno=saved_errno; return; }
    if (getauxval(AT_SECURE)) refusal("secure_execution");
    else if (!provider_verified()) refusal("unsupported_libc_provider");
    else if (!(started_ns=now())) refusal("monotonic_clock_unavailable");
    else {
        validated_image.store(true,std::memory_order_release);
        if (pthread_create(&writer,nullptr,record_loop,nullptr)!=0) {
            validated_image.store(false,std::memory_order_release);
            refusal("writer_start_failed");
        } else writer_started=true;
    }
    errno=saved_errno;
}

__attribute__((destructor)) void shutdown() noexcept {
    observer.armed.store(false,std::memory_order_seq_cst);
    stopping.store(true,std::memory_order_release);
    if (writer_started) pthread_join(writer,nullptr);
    if (output_fd>=0) close(output_fd);
    if (parent_fd>=0) close(parent_fd);
}
} // namespace

extern "C" __attribute__((visibility("default"),noinline))
int pthread_mutex_lock(pthread_mutex_t* mutex) noexcept {
    const auto site=caller_site(__builtin_extract_return_addr(__builtin_return_address(0)));
    const int result=receiver_libc_lock(mutex);
    const int forwarded_errno=errno;
    if (site) {
        callbacks.fetch_add(1,std::memory_order_seq_cst);
        observer.after_lock(site,reinterpret_cast<std::uintptr_t>(mutex),result,local_scope);
        callbacks.fetch_sub(1,std::memory_order_seq_cst);
    }
    errno=forwarded_errno;
    return result;
}

extern "C" __attribute__((visibility("default"),noinline))
int pthread_mutex_unlock(pthread_mutex_t* mutex) noexcept {
    const auto site=caller_site(__builtin_extract_return_addr(__builtin_return_address(0)));
    const int incoming_errno=errno;
    bool tracked=false;
    if (site) {
        callbacks.fetch_add(1,std::memory_order_seq_cst);
        tracked=observer.armed.load(std::memory_order_seq_cst) || local_scope.active;
        observer.before_unlock(site,reinterpret_cast<std::uintptr_t>(mutex),local_scope);
    }
    errno=incoming_errno;
    const int result=receiver_libc_unlock(mutex);
    const int forwarded_errno=errno;
    if (site) {
        if (tracked && result!=0) {
            unlock_failures.fetch_add(1,std::memory_order_relaxed);
            if (local_scope.active) local_scope.record.reason |= frame_receiver::LockFailure;
        }
        callbacks.fetch_sub(1,std::memory_order_seq_cst);
    }
    errno=forwarded_errno;
    return result;
}
