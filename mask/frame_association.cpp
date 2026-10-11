#include "frame_association.h"

namespace mr::mask {
namespace {

bool validSession(const SessionId& session) noexcept {
    for (const auto byte : session) if (byte != 0) return true;
    return false;
}

bool validExtent(EyeExtent extent) noexcept {
    return extent.width != 0 && extent.height != 0 &&
           extent.width <= 4096 && extent.height <= 4096 &&
           std::size_t(extent.width) * extent.height <= kMaxMaskBytes;
}

bool same(EyeExtent a, EyeExtent b) noexcept {
    return a.width == b.width && a.height == b.height;
}

bool same(NativeFrameIdentity a, NativeFrameIdentity b) noexcept {
    return a.key == b.key && a.generation == b.generation;
}

bool before(NativeFrameIdentity a, NativeFrameIdentity b) noexcept {
    return a.generation < b.generation ||
           (a.generation == b.generation && a.key < b.key);
}

bool validResource(FrameResourceIdentity identity) noexcept {
    return identity.namespaceId != 0 && identity.handle != 0 &&
           identity.generation != 0;
}

bool sameHandle(FrameResourceIdentity a, FrameResourceIdentity b) noexcept {
    return a.namespaceId == b.namespaceId && a.handle == b.handle;
}

bool same(FrameResourceIdentity a, FrameResourceIdentity b) noexcept {
    return sameHandle(a, b) && a.generation == b.generation;
}

template<class Entries, class Predicate>
auto findEntry(Entries& entries, Predicate predicate) noexcept -> decltype(&entries[0]) {
    for (auto& entry : entries) if (entry.used && predicate(entry)) return &entry;
    return nullptr;
}

template<class Entries>
auto unusedEntry(Entries& entries) noexcept -> decltype(&entries[0]) {
    for (auto& entry : entries) if (!entry.used) return &entry;
    return nullptr;
}

} // namespace

FrameAssociationResult FrameAssociationLedger::beginSession(
    const SessionId& session) noexcept {
    if (!validSession(session) || (haveSession_ && session == session_))
        return poison(FrameAssociationResult::Invalid);
    session_ = session;
    haveSession_ = bound_ = healthy_ = true;
    haveRetirement_ = false;
    sourceFloor_ = 0;
    nativeFloor_ = {};
    sources_ = {};
    natives_ = {};
    decoded_ = {};
    imported_ = {};
    return FrameAssociationResult::Accepted;
}

void FrameAssociationLedger::endSession() noexcept {
    bound_ = healthy_ = false;
}

void FrameAssociationLedger::invalidate() noexcept { healthy_ = false; }

bool FrameAssociationLedger::active() const noexcept { return bound_ && healthy_; }

FrameAssociationResult FrameAssociationLedger::admission(
    const SessionId& session) const noexcept {
    if (!bound_) return FrameAssociationResult::Inactive;
    if (session != session_) return FrameAssociationResult::WrongSession;
    return healthy_ ? FrameAssociationResult::Accepted : FrameAssociationResult::Inactive;
}

FrameAssociationResult FrameAssociationLedger::poison(
    FrameAssociationResult reason) noexcept {
    healthy_ = false;
    return reason;
}

bool FrameAssociationLedger::sourceRetired(std::uint64_t frame) const noexcept {
    return haveRetirement_ && frame <= sourceFloor_;
}

bool FrameAssociationLedger::nativeRetired(NativeFrameIdentity native) const noexcept {
    return haveRetirement_ && !before(nativeFloor_, native);
}

FrameAssociationResult FrameAssociationLedger::recordSource(
    const SourceFrameEvidence& source) noexcept {
    const auto status = admission(source.session);
    if (status != FrameAssociationResult::Accepted) return status;
    if (!validExtent(source.eyes[0]) || !validExtent(source.eyes[1]))
        return poison(FrameAssociationResult::Invalid);
    if (sourceRetired(source.frame)) return FrameAssociationResult::Stale;
    auto* entry = findEntry(sources_, [&](const auto& e) { return e.frame == source.frame; });
    if (entry) {
        if (same(entry->eyes[0], source.eyes[0]) && same(entry->eyes[1], source.eyes[1]))
            return FrameAssociationResult::Duplicate;
        return poison(FrameAssociationResult::Conflict);
    }
    entry = unusedEntry(sources_);
    if (!entry) return poison(FrameAssociationResult::CapacityExceeded);
    *entry = {true, source.frame, source.eyes};
    return FrameAssociationResult::Accepted;
}

FrameAssociationResult FrameAssociationLedger::recordNative(const SessionId& session,
    std::uint64_t sourceFrame, NativeFrameIdentity native) noexcept {
    const auto status = admission(session);
    if (status != FrameAssociationResult::Accepted) return status;
    if (native.generation == 0) return poison(FrameAssociationResult::Invalid);
    if (sourceRetired(sourceFrame) || nativeRetired(native))
        return FrameAssociationResult::Stale;
    auto* entry = findEntry(natives_, [&](const auto& e) { return same(e.identity, native); });
    if (entry) {
        if (entry->frame == sourceFrame) return FrameAssociationResult::Duplicate;
        return poison(FrameAssociationResult::Conflict);
    }
    entry = unusedEntry(natives_);
    if (!entry) return poison(FrameAssociationResult::CapacityExceeded);
    *entry = {true, native, sourceFrame};
    return FrameAssociationResult::Accepted;
}

FrameAssociationResult FrameAssociationLedger::recordDecoded(const SessionId& session,
    NativeFrameIdentity native, FrameResourceIdentity surface) noexcept {
    const auto status = admission(session);
    if (status != FrameAssociationResult::Accepted) return status;
    if (native.generation == 0 || !validResource(surface))
        return poison(FrameAssociationResult::Invalid);
    auto* entry = findEntry(decoded_, [&](const auto& e) { return sameHandle(e.identity, surface); });
    if (entry) {
        if (surface.generation < entry->identity.generation)
            return FrameAssociationResult::Stale;
        if (surface.generation == entry->identity.generation) {
            if (entry->retired) return FrameAssociationResult::Stale;
            if (same(entry->native, native)) return FrameAssociationResult::Duplicate;
            return poison(FrameAssociationResult::Conflict);
        }
    } else {
        entry = unusedEntry(decoded_);
        if (!entry) return poison(FrameAssociationResult::CapacityExceeded);
    }
    // Even a retired native token must invalidate a previous use of this surface.
    const bool retired = nativeRetired(native);
    *entry = {true, retired, surface, native};
    return retired ? FrameAssociationResult::Stale : FrameAssociationResult::Accepted;
}

FrameAssociationResult FrameAssociationLedger::recordImported(const SessionId& session,
    FrameResourceIdentity surface, FrameResourceIdentity resource) noexcept {
    const auto status = admission(session);
    if (status != FrameAssociationResult::Accepted) return status;
    if (!validResource(surface) || !validResource(resource))
        return poison(FrameAssociationResult::Invalid);
    auto* entry = findEntry(imported_, [&](const auto& e) { return sameHandle(e.identity, resource); });
    if (entry) {
        if (resource.generation < entry->identity.generation)
            return FrameAssociationResult::Stale;
        if (resource.generation == entry->identity.generation) {
            if (entry->retired) return FrameAssociationResult::Stale;
            if (same(entry->decoded, surface)) return FrameAssociationResult::Duplicate;
            return poison(FrameAssociationResult::Conflict);
        }
    } else {
        entry = unusedEntry(imported_);
        if (!entry) return poison(FrameAssociationResult::CapacityExceeded);
    }
    *entry = {true, false, resource, surface};
    return FrameAssociationResult::Accepted;
}

std::optional<FrameIdentity> FrameAssociationLedger::resolveExact(
    const SelectedFrameEvidence& selected) const noexcept {
    if (admission(selected.session) != FrameAssociationResult::Accepted ||
        selected.native.generation == 0 || selected.eye > 1 ||
        !validExtent(selected.extent) || !validResource(selected.decodedSurface) ||
        !validResource(selected.importedResource) || nativeRetired(selected.native))
        return std::nullopt;
    const auto* imported = findEntry(imported_, [&](const auto& e) {
        return same(e.identity, selected.importedResource);
    });
    if (!imported || imported->retired || !same(imported->decoded, selected.decodedSurface))
        return std::nullopt;
    const auto* decoded = findEntry(decoded_, [&](const auto& e) {
        return same(e.identity, selected.decodedSurface);
    });
    if (!decoded || decoded->retired || !same(decoded->native, selected.native))
        return std::nullopt;
    const auto* native = findEntry(natives_, [&](const auto& e) {
        return same(e.identity, selected.native);
    });
    if (!native || sourceRetired(native->frame)) return std::nullopt;
    const auto* source = findEntry(sources_, [&](const auto& e) { return e.frame == native->frame; });
    if (!source || !same(source->eyes[selected.eye], selected.extent)) return std::nullopt;
    return FrameIdentity{session_, source->frame, selected.eye,
                         selected.extent.width, selected.extent.height};
}

FrameAssociationResult FrameAssociationLedger::retireThrough(const SessionId& session,
    std::uint64_t sourceFloor, NativeFrameIdentity nativeFloor) noexcept {
    const auto status = admission(session);
    if (status != FrameAssociationResult::Accepted) return status;
    if (nativeFloor.generation == 0) return poison(FrameAssociationResult::Invalid);
    if (haveRetirement_) {
        if (sourceFloor < sourceFloor_ || before(nativeFloor, nativeFloor_))
            return FrameAssociationResult::Stale;
        if (sourceFloor == sourceFloor_ && same(nativeFloor, nativeFloor_))
            return FrameAssociationResult::Duplicate;
    }
    sourceFloor_ = sourceFloor;
    nativeFloor_ = nativeFloor;
    haveRetirement_ = true;
    for (auto& entry : sources_) if (entry.used && sourceRetired(entry.frame)) entry.used = false;
    for (auto& entry : natives_) if (entry.used && nativeRetired(entry.identity)) entry.used = false;
    for (auto& entry : decoded_) if (entry.used && nativeRetired(entry.native)) entry.retired = true;
    return FrameAssociationResult::Accepted;
}

FrameAssociationResult FrameAssociationLedger::retireDecoded(const SessionId& session,
    FrameResourceIdentity surface) noexcept {
    const auto status = admission(session);
    if (status != FrameAssociationResult::Accepted) return status;
    if (!validResource(surface)) return poison(FrameAssociationResult::Invalid);
    auto* entry = findEntry(decoded_, [&](const auto& e) { return sameHandle(e.identity, surface); });
    if (entry) {
        if (surface.generation < entry->identity.generation) return FrameAssociationResult::Stale;
        if (surface.generation == entry->identity.generation && entry->retired)
            return FrameAssociationResult::Duplicate;
    } else {
        entry = unusedEntry(decoded_);
        if (!entry) return poison(FrameAssociationResult::CapacityExceeded);
    }
    *entry = {true, true, surface, {}};
    return FrameAssociationResult::Accepted;
}

FrameAssociationResult FrameAssociationLedger::retireImported(const SessionId& session,
    FrameResourceIdentity resource) noexcept {
    const auto status = admission(session);
    if (status != FrameAssociationResult::Accepted) return status;
    if (!validResource(resource)) return poison(FrameAssociationResult::Invalid);
    auto* entry = findEntry(imported_, [&](const auto& e) { return sameHandle(e.identity, resource); });
    if (entry) {
        if (resource.generation < entry->identity.generation) return FrameAssociationResult::Stale;
        if (resource.generation == entry->identity.generation && entry->retired)
            return FrameAssociationResult::Duplicate;
    } else {
        entry = unusedEntry(imported_);
        if (!entry) return poison(FrameAssociationResult::CapacityExceeded);
    }
    *entry = {true, true, resource, {}};
    return FrameAssociationResult::Accepted;
}

} // namespace mr::mask
