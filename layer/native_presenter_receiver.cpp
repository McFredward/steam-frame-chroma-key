#include "native_presenter_receiver.h"
#include "pixel_snapshot_io.h"
#include <cstdio>
#include <limits>

namespace frame_native_presenter_receiver {
namespace {
bool directoryOwned(int fd) noexcept {
    struct stat info{};
    return fd >= 0 && !fstat(fd, &info) && S_ISDIR(info.st_mode) && info.st_uid == getuid() &&
        (info.st_mode & 0777) == 0700;
}
bool readOwned(int directory, const char* name, void* bytes, std::size_t size) noexcept {
    const int fd = openat(directory, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat before{}, after{};
    const bool valid = !fstat(fd, &before) && S_ISREG(before.st_mode) && before.st_uid == getuid() &&
        (before.st_mode & 0777) == 0600 && before.st_nlink == 1 && before.st_size == static_cast<off_t>(size);
    std::size_t used = 0;
    while (valid && used < size) {
        const auto count = read(fd, static_cast<unsigned char*>(bytes) + used, size - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        used += static_cast<std::size_t>(count);
    }
    const bool unchanged = !fstat(fd, &after) && before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
        after.st_uid == getuid() && S_ISREG(after.st_mode) && (after.st_mode & 0777) == 0600 &&
        after.st_nlink == 1 && after.st_size == static_cast<off_t>(size);
    close(fd);
    return valid && unchanged && used == size;
}
struct Scope {
    VkDevice device{};
    VkPipeline original{};
    PFN_vkDestroyPipeline downstream = nullptr;
    Retirement retirement{};
    Scope* previous = nullptr;
};
thread_local Scope* current = nullptr;
struct ScopeLifetime {
    explicit ScopeLifetime(Scope& scope) noexcept : scope_(scope) { scope_.previous = current; current = &scope_; }
    ~ScopeLifetime() { current = scope_.previous; }
    Scope& scope_;
};
VKAPI_ATTR void VKAPI_CALL retiringDestroy(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* allocator) {
    auto* scope = current;
    if (!scope || !scope->downstream) return;
    if (device == scope->device && pipeline == scope->original && scope->retirement.beforeDestroy)
        scope->retirement.beforeDestroy(scope->retirement.owner, device, pipeline);
    scope->downstream(device, pipeline, allocator);
}
template<class T> void bump(std::atomic<T>& value) noexcept {
    // Diagnostic saturation is harmless; never wrap into a misleading fresh
    // count. Cold contention may omit a diagnostic increment after 8 attempts.
    auto old = value.load(std::memory_order_relaxed);
    for (unsigned budget = 0; budget < 8 && old != std::numeric_limits<T>::max(); ++budget)
        if (value.compare_exchange_weak(old, old + 1, std::memory_order_relaxed)) break;
}
bool ownedExistingOrAbsent(int directory, const char* name) noexcept {
    struct stat info{};
    if (fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW)) return errno == ENOENT;
    return S_ISREG(info.st_mode) && info.st_uid == getuid() && (info.st_mode & 0777) == 0600 &&
        info.st_nlink == 1 && info.st_size >= 0 && info.st_size <= 1024;
}
} // namespace

const char* sourceReasonName(SourceReason reason) noexcept {
    switch (reason) {
    case SourceReason::Disabled:return "disabled"; case SourceReason::Directory:return "directory";
    case SourceReason::Marker:return "marker"; case SourceReason::Module:return "module";
    case SourceReason::Pin:return "pin"; case SourceReason::Ready:return "ready";
    } return "unknown";
}
const char* primitiveReasonName(native::Reason reason) noexcept {
    switch (reason) {
#define REASON(name, text) case native::Reason::name:return text;
    REASON(None,"none") REASON(Artifact,"artifact") REASON(ApiVersion,"api_version")
    REASON(Original,"original") REASON(Identity,"identity") REASON(Interface,"interface")
    REASON(Graphics,"graphics") REASON(Dispatch,"dispatch") REASON(NativeCreation,"native_creation")
    REASON(ShaderCreation,"shader_creation") REASON(PipelineCreation,"pipeline_creation") REASON(UnexpectedHandle,"unexpected_handle")
#undef REASON
    } return "unknown";
}
const char* graphicsGateName(native::GraphicsGate gate) noexcept {
    switch(gate){
    case native::GraphicsGate::None:return "none"; case native::GraphicsGate::Contract:return "contract";
    case native::GraphicsGate::Blend:return "blend"; case native::GraphicsGate::Dynamic:return "dynamic";
    case native::GraphicsGate::ColorWrite:return "color_write"; case native::GraphicsGate::Attachment:return "attachment";
    case native::GraphicsGate::Format:return "format"; case native::GraphicsGate::Multisample:return "multisample";
    case native::GraphicsGate::Stages:return "stages";
    }return "unknown";
}
bool Source::prepareCold(int directory) noexcept {
    if (attempted_) return ready();
    attempted_ = true;
    if (!directoryOwned(directory)) { reason_ = SourceReason::Directory; return false; }
    char marker[sizeof("GHVR_SYNTHETIC_PRESENTER/1\n") - 1]{};
    if (!readOwned(directory, "synthetic-presenter.enable", marker, sizeof(marker)) ||
        std::memcmp(marker, EnableMagic, sizeof(marker))) { reason_ = SourceReason::Marker; return false; }
    std::array<std::uint32_t, native::GuardedFragmentBytes / 4> words{};
    if (!readOwned(directory, "synthetic-presenter.spv", words.data(), sizeof(words))) {
        reason_ = SourceReason::Module; return false;
    }
    if (!fragment_.prepare(words.data(), sizeof(words))) { reason_ = SourceReason::Pin; return false; }
    reason_ = SourceReason::Ready;
    return true;
}
native::Result complete(VkDevice device, VkPipelineCache cache, std::uint32_t api, VkResult originalResult,
        const VkGraphicsPipelineCreateInfo& info, const VkAllocationCallbacks* allocator, VkPipeline& pipeline,
        const frame_pixel_ssbo_clone::Recipe& recipe, const Source& source,
        const native::Dispatch& dispatch, Retirement retirement) noexcept {
    if (!source.ready()) return {native::Reason::Artifact};
    // A reentrant optional replacement must not borrow or replace its caller's
    // retirement scope. The original nested native call remains unchanged.
    if (current || !retirement.owner || !retirement.beforeDestroy) return {native::Reason::Dispatch};
    Scope scope{device, pipeline, dispatch.destroyPipeline, retirement, nullptr};
    ScopeLifetime lifetime(scope);
    auto wrapped = dispatch;
    wrapped.destroyPipeline = retiringDestroy;
    if (!dispatch.destroyPipeline) return {native::Reason::Dispatch};
    return native::replaceCompletedPipeline(device, cache, api, originalResult, info, allocator,
                                            pipeline, recipe, source.fragment(), wrapped);
}
void Counters::deviceCreated(VkDevice device, std::uint32_t api) noexcept {
    device_.store(frame_pixel_ssbo_clone::handle(device), std::memory_order_relaxed);
    api_.store(api, std::memory_order_relaxed); bump(devices_); bump(revision_);
}
void Counters::deviceDestroyed() noexcept {
    auto old = devices_.load(std::memory_order_relaxed);
    for (unsigned budget = 0; budget < 8 && old; ++budget)
        if (devices_.compare_exchange_weak(old, old - 1, std::memory_order_relaxed)) break;
    bump(revision_);
}
void Counters::attempted(const native::Result& result) noexcept {
    bump(attempts_); if (result.replaced) bump(replacements_);
    reason_.store(result.reason, std::memory_order_relaxed);
    graphicsGate_.store(result.graphicsGate, std::memory_order_relaxed);
    attachmentFormat_.store(result.attachmentFormat, std::memory_order_relaxed);
    unsupportedDynamic_.store(result.unsupportedDynamic, std::memory_order_relaxed); bump(revision_);
}
void Counters::historyFailed() noexcept { failed_.store(true, std::memory_order_relaxed); bump(revision_); }
Snapshot Counters::snapshot() const noexcept {
    return {revision_.load(std::memory_order_relaxed), attempts_.load(std::memory_order_relaxed),
            replacements_.load(std::memory_order_relaxed), device_.load(std::memory_order_relaxed),
            api_.load(std::memory_order_relaxed), devices_.load(std::memory_order_relaxed),
            reason_.load(std::memory_order_relaxed), failed_.load(std::memory_order_relaxed),
            graphicsGate_.load(std::memory_order_relaxed), attachmentFormat_.load(std::memory_order_relaxed),
            unsupportedDynamic_.load(std::memory_order_relaxed)};
}
bool writeStatusCold(int directory, const char* capture, const Source& source, const Snapshot& s,
                     unsigned update, bool exhausted) noexcept {
    if (!directoryOwned(directory) || !capture || strnlen(capture, 33) != 32 || !update || update > MaximumStatusUpdates)
        return false;
    for (unsigned i = 0; i < 32; ++i) if (!((capture[i] >= '0' && capture[i] <= '9') || (capture[i] >= 'a' && capture[i] <= 'f'))) return false;
    if (!ownedExistingOrAbsent(directory, StatusFile)) return false;
    char text[1024]{};
    const auto count = std::snprintf(text, sizeof(text),
        "{\"schema\":\"frame-synthetic-presenter-receiver-v1\",\"mode\":\"synthetic_format_guarded_presenter\","
        "\"capture_id\":\"%s\",\"source_ready\":%s,\"source_reason\":\"%s\",\"candidate_sha256\":\"%s\",\"candidate_bytes\":%zu,"
        "\"observed_device\":%llu,\"observed_api\":%u,\"active_devices\":%u,\"pipeline_attempts\":%llu,\"pipeline_replacements\":%llu,"
        "\"last_primitive_reason\":\"%s\",\"graphics_gate\":\"%s\",\"attachment_format\":%u,\"unsupported_dynamic\":%u,"
        "\"definition_history_failed\":%s,\"revision\":%llu,\"updates\":%u,\"budget_exhausted\":%s,"
        "\"diagnostic_atomics_not_source_cohort\":true,\"native_frame_association_proven\":false,\"passthrough_authorized\":false}\n",
        capture, source.ready() ? "true" : "false", sourceReasonName(source.reason()), native::GuardedFragmentSha256,
        native::GuardedFragmentBytes, static_cast<unsigned long long>(s.observedDevice), s.observedApi, s.activeDevices,
        static_cast<unsigned long long>(s.pipelineAttempts), static_cast<unsigned long long>(s.pipelineReplacements),
        primitiveReasonName(s.primitiveReason), graphicsGateName(s.graphicsGate), unsigned(s.attachmentFormat), unsigned(s.unsupportedDynamic),
        s.definitionHistoryFailed ? "true" : "false",
        static_cast<unsigned long long>(s.revision), update, exhausted ? "true" : "false");
    if (count <= 0 || static_cast<std::size_t>(count) >= sizeof(text)) return false;
    constexpr const char* temporary = "synthetic-presenter-status.tmp";
    const int fd = openat(directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    const bool written = !fchmod(fd, 0600) && frame_pixel_io::writeAll(fd, text, static_cast<std::size_t>(count));
    struct stat owned{}; const bool known = !fstat(fd, &owned); close(fd);
    if (written && known && !renameat(directory, temporary, directory, StatusFile)) return true;
    struct stat currentFile{};
    if (known && !fstatat(directory, temporary, &currentFile, AT_SYMLINK_NOFOLLOW) &&
        currentFile.st_dev == owned.st_dev && currentFile.st_ino == owned.st_ino) unlinkat(directory, temporary, 0);
    return false;
}
} // namespace frame_native_presenter_receiver
