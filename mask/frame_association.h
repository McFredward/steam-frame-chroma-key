#pragma once

#include "mask_protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mr::mask {

struct EyeExtent {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
};

struct SourceFrameEvidence {
    SessionId session{};
    std::uint64_t frame = 0;
    std::array<EyeExtent, 2> eyes{};
};

// Supplied by a proven sender adapter. Generation is an explicit stream/key
// epoch: it must advance before a key is reused. It is never inferred from
// arrival order, a PresentNumber, decoder slot or a truncated source frame ID.
struct NativeFrameIdentity {
    std::uint16_t key = 0;
    std::uint64_t generation = 0;
};

// A namespace disambiguates handles from different devices/importers. Generation
// identifies one content/lifetime association and increases when that handle is
// reused. A Vulkan allocation generation alone does not identify changing video
// content in an allocation. Zero namespace, handle or generation means unknown.
struct FrameResourceIdentity {
    std::uint64_t namespaceId = 0;
    std::uint64_t handle = 0;
    std::uint64_t generation = 0;
};

struct SelectedFrameEvidence {
    SessionId session{};
    NativeFrameIdentity native{};
    FrameResourceIdentity decodedSurface{};
    FrameResourceIdentity importedResource{};
    std::uint8_t eye = 0;
    EyeExtent extent{};
};

enum class FrameAssociationResult {
    Accepted, Duplicate, WrongSession, Invalid, Stale, Conflict,
    CapacityExceeded, Inactive
};

// Association prerequisite, not a Steam Link adapter or functioning passthrough.
// Each record* call is a trust boundary: its caller must have established that
// mapping from the actual composition, encoder/transmitter, selected decoder
// surface and imported resource. This class cannot certify private ABI evidence.
// It stores scalar identities only, with no networking, pixels, clocks, threads,
// dynamic allocations, or native/GPU calls. Call it serially.
//
// Stages may arrive out of order. Resolution requires the complete exact chain;
// an incomplete, expired, reused or mismatched chain always returns no identity.
// Repeated presentation is a non-consuming lookup of the same selected chain,
// not a fallback to the latest received mask. Mask storage remains Receiver's or
// the renderer's responsibility; GPU lifetime, eye/UV mapping and fences still
// need independent proof before an actual composition can use the identity.
//
// No implicit eviction occurs. A trusted retirement barrier reclaims source and
// native records. Per-handle generation tombstones remain until a fresh session,
// so delayed evidence cannot revive a retired/reused surface. Capacity loss,
// conflicting evidence or malformed active-session evidence disables all lookup
// until beginSession with a fresh ID. The caller must never reuse session IDs;
// only immediate ID reuse can be detected with this bounded model.
class FrameAssociationLedger {
public:
    static constexpr std::size_t SourceCapacity = 64;
    static constexpr std::size_t NativeCapacity = 64;
    static constexpr std::size_t DecodedCapacity = 128;
    static constexpr std::size_t ImportedCapacity = 128;

    FrameAssociationResult beginSession(const SessionId& session) noexcept;
    void endSession() noexcept;
    // Report any lost/unverifiable adapter evidence before further presentation.
    void invalidate() noexcept;
    bool active() const noexcept;

    FrameAssociationResult recordSource(const SourceFrameEvidence& source) noexcept;
    // Requires the complete same-frame composition -> encoder input -> encoded
    // output -> transmitter key proof. A matching PresentNumber is insufficient.
    FrameAssociationResult recordNative(const SessionId& session,
        std::uint64_t sourceFrame, NativeFrameIdentity native) noexcept;
    // The adapter must report reuse/release before a former resource can be
    // selected again. If a lifecycle event cannot be tracked, call invalidate;
    // this ledger cannot detect an unreported native resource change itself.
    FrameAssociationResult recordDecoded(const SessionId& session,
        NativeFrameIdentity native, FrameResourceIdentity surface) noexcept;
    FrameAssociationResult recordImported(const SessionId& session,
        FrameResourceIdentity surface, FrameResourceIdentity resource) noexcept;

    std::optional<FrameIdentity> resolveExact(
        const SelectedFrameEvidence& selected) const noexcept;

    // Both inclusive floors must be explicitly proven unselectable by the
    // adapter. Native ordering is (generation, key), independently of source
    // frame ordering. Never choose either floor from packet arrival order or the
    // newest decoded frame. Both floors must advance monotonically; a source
    // counter/generation wrap requires a fresh session.
    FrameAssociationResult retireThrough(const SessionId& session,
        std::uint64_t sourceFloor, NativeFrameIdentity nativeFloor) noexcept;
    FrameAssociationResult retireDecoded(const SessionId& session,
        FrameResourceIdentity surface) noexcept;
    FrameAssociationResult retireImported(const SessionId& session,
        FrameResourceIdentity resource) noexcept;

private:
    struct SourceEntry {
        bool used = false;
        std::uint64_t frame = 0;
        std::array<EyeExtent, 2> eyes{};
    };
    struct NativeEntry {
        bool used = false;
        NativeFrameIdentity identity{};
        std::uint64_t frame = 0;
    };
    struct DecodedEntry {
        bool used = false;
        bool retired = false;
        FrameResourceIdentity identity{};
        NativeFrameIdentity native{};
    };
    struct ImportedEntry {
        bool used = false;
        bool retired = false;
        FrameResourceIdentity identity{};
        FrameResourceIdentity decoded{};
    };

    FrameAssociationResult admission(const SessionId& session) const noexcept;
    FrameAssociationResult poison(FrameAssociationResult reason) noexcept;
    bool sourceRetired(std::uint64_t frame) const noexcept;
    bool nativeRetired(NativeFrameIdentity native) const noexcept;

    SessionId session_{};
    bool haveSession_ = false;
    bool bound_ = false;
    bool healthy_ = false;
    bool haveRetirement_ = false;
    std::uint64_t sourceFloor_ = 0;
    NativeFrameIdentity nativeFloor_{};
    std::array<SourceEntry, SourceCapacity> sources_{};
    std::array<NativeEntry, NativeCapacity> natives_{};
    std::array<DecodedEntry, DecodedCapacity> decoded_{};
    std::array<ImportedEntry, ImportedCapacity> imported_{};
};

} // namespace mr::mask
