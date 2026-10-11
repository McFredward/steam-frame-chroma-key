#include "../mask/frame_association.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace mr::mask;

namespace {

unsigned checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

SessionId session(std::uint8_t seed = 1) {
    SessionId id{};
    id[0] = seed;
    return id;
}

SourceFrameEvidence source(std::uint64_t frame = 100, std::uint8_t seed = 1) {
    return {session(seed), frame, {{{80, 50}, {90, 60}}}};
}

SelectedFrameEvidence selected(std::uint16_t key = 17, std::uint64_t nativeGeneration = 3,
    std::uint64_t surface = 12, std::uint64_t resource = 27, std::uint8_t seed = 1) {
    return {session(seed), {key, nativeGeneration}, {5, surface, 1},
            {8, resource, 1}, 0, {80, 50}};
}

void begin(FrameAssociationLedger& ledger, std::uint8_t seed = 1) {
    require(ledger.beginSession(session(seed)) == FrameAssociationResult::Accepted,
            "fresh session accepted");
}

void complete(FrameAssociationLedger& ledger, const SourceFrameEvidence& input,
    const SelectedFrameEvidence& pick) {
    require(ledger.recordSource(input) == FrameAssociationResult::Accepted, "source recorded");
    require(ledger.recordNative(input.session, input.frame, pick.native) ==
            FrameAssociationResult::Accepted, "sender native key mapping recorded");
    require(ledger.recordDecoded(input.session, pick.native, pick.decodedSurface) ==
            FrameAssociationResult::Accepted, "decoded association recorded");
    require(ledger.recordImported(input.session, pick.decodedSurface, pick.importedResource) ==
            FrameAssociationResult::Accepted, "imported association recorded");
}

void exact(const FrameAssociationLedger& ledger, const SelectedFrameEvidence& pick,
    std::uint64_t frame) {
    const auto result = ledger.resolveExact(pick);
    require(result && result->session == pick.session && result->frame == frame &&
            result->eye == pick.eye && result->width == pick.extent.width &&
            result->height == pick.extent.height, "lookup preserves exact source frame/eye/extent");
}

void missingStagesAndReorderedEvidence() {
    std::array<unsigned, 4> order{{0, 1, 2, 3}};
    do {
        FrameAssociationLedger ledger;
        const auto input = source();
        const auto pick = selected();
        require(!ledger.resolveExact(pick), "unbound ledger stays opaque");
        begin(ledger);
        for (std::size_t i = 0; i < order.size(); ++i) {
            require(!ledger.resolveExact(pick), "every missing association stage stays opaque");
            FrameAssociationResult result = FrameAssociationResult::Invalid;
            switch (order[i]) {
            case 0: result = ledger.recordSource(input); break;
            case 1: result = ledger.recordNative(input.session, input.frame, pick.native); break;
            case 2: result = ledger.recordDecoded(input.session, pick.native, pick.decodedSurface); break;
            case 3: result = ledger.recordImported(input.session, pick.decodedSurface, pick.importedResource); break;
            }
            require(result == FrameAssociationResult::Accepted, "independent evidence accepts all stage orders");
        }
        exact(ledger, pick, input.frame);
        exact(ledger, pick, input.frame); // Repeated presentation must not consume identity.
    } while (std::next_permutation(order.begin(), order.end()));
}

void mismatchNeverFallsBack() {
    FrameAssociationLedger ledger;
    begin(ledger);
    const auto input = source();
    const auto pick = selected();
    complete(ledger, input, pick);
    auto wrong = pick; wrong.native.key++;
    require(!ledger.resolveExact(wrong), "another native key cannot use latest known frame");
    wrong = pick; wrong.native.generation++;
    require(!ledger.resolveExact(wrong), "same key in another epoch is not the same frame");
    wrong = pick; wrong.decodedSurface.generation++;
    require(!ledger.resolveExact(wrong), "decoder reuse generation is required");
    wrong = pick; wrong.importedResource.generation++;
    require(!ledger.resolveExact(wrong), "imported resource generation is required");
    wrong = pick; wrong.importedResource.namespaceId++;
    require(!ledger.resolveExact(wrong), "resource handle namespace is required");
    wrong = pick; wrong.session = session(2);
    require(!ledger.resolveExact(wrong), "another transport session is not the same frame");
    wrong = pick; wrong.extent.width++;
    require(!ledger.resolveExact(wrong), "extent must match actual eye coverage");
    wrong = pick; wrong.eye = 1;
    require(!ledger.resolveExact(wrong), "right eye cannot use left eye extent");
    wrong.extent = input.eyes[1];
    exact(ledger, wrong, input.frame);
    wrong.eye = 2;
    require(!ledger.resolveExact(wrong), "unknown eye remains opaque");
    wrong = pick; wrong.decodedSurface.handle = 0;
    require(!ledger.resolveExact(wrong), "unknown handle remains opaque");
    wrong = pick; wrong.native.generation = 0;
    require(!ledger.resolveExact(wrong), "missing epoch is not inferred");
    exact(ledger, pick, input.frame);
}

void reorderedFramesAndExplicitWrap() {
    FrameAssociationLedger ledger;
    begin(ledger);
    const auto old = selected(65535, 7, 1, 2);
    const auto next = selected(0, 8, 3, 4);
    complete(ledger, source(103), next);
    complete(ledger, source(102), old); // Source ID intentionally unrelated to native key.
    exact(ledger, old, 102);
    exact(ledger, next, 103);
    auto wrong = next; wrong.native.generation = 7;
    require(!ledger.resolveExact(wrong), "16-bit wrap does not infer the next epoch");

    // One composition may be encoded/presented more than once; each key still
    // requires an explicit association rather than a latest-frame substitution.
    const auto repeat = selected(1, 8, 5, 6);
    require(ledger.recordNative(session(), 103, repeat.native) == FrameAssociationResult::Accepted,
            "explicit repeated encoding of same source accepted");
    require(ledger.recordDecoded(session(), repeat.native, repeat.decodedSurface) ==
            FrameAssociationResult::Accepted, "repeated source uses exact decoder result");
    require(ledger.recordImported(session(), repeat.decodedSurface, repeat.importedResource) ==
            FrameAssociationResult::Accepted, "repeated source has exact import");
    exact(ledger, repeat, 103);

    // Multiple capture results can retain the same V4L2 timestamp/native token.
    const auto otherSurface = selected(65535, 7, 7, 8);
    require(ledger.recordDecoded(session(), old.native, otherSurface.decodedSurface) ==
            FrameAssociationResult::Accepted, "one token can produce another capture surface");
    require(ledger.recordImported(session(), otherSurface.decodedSurface, otherSurface.importedResource) ==
            FrameAssociationResult::Accepted, "additional capture result requires own import");
    exact(ledger, otherSurface, 102);
}

void generationReuseAndRetirement() {
    FrameAssociationLedger ledger;
    begin(ledger);
    const auto first = selected();
    complete(ledger, source(100), first);
    auto second = first;
    second.native = {18, 3};
    second.decodedSurface.generation = 2;
    second.importedResource.generation = 2;
    require(ledger.recordSource(source(101)) == FrameAssociationResult::Accepted, "next source recorded");
    require(ledger.recordNative(session(), 101, second.native) == FrameAssociationResult::Accepted,
            "next native association recorded");
    require(ledger.recordDecoded(session(), second.native, second.decodedSurface) ==
            FrameAssociationResult::Accepted, "surface reused with explicit next generation");
    require(!ledger.resolveExact(first), "surface reuse invalidates old selected association immediately");
    require(!ledger.resolveExact(second), "new surface cannot use old import");
    require(ledger.recordDecoded(session(), first.native, first.decodedSurface) ==
            FrameAssociationResult::Stale, "out-of-order old surface evidence cannot revive it");
    require(ledger.recordImported(session(), second.decodedSurface, second.importedResource) ==
            FrameAssociationResult::Accepted, "new import completes reused surface");
    exact(ledger, second, 101);
    require(ledger.recordImported(session(), first.decodedSurface, first.importedResource) ==
            FrameAssociationResult::Stale, "old imported generation cannot be reinstated");
    require(ledger.retireDecoded(session(), second.decodedSurface) == FrameAssociationResult::Accepted,
            "explicit surface release recorded");
    require(!ledger.resolveExact(second), "released surface never resolves");
    require(ledger.recordDecoded(session(), second.native, second.decodedSurface) ==
            FrameAssociationResult::Stale, "surface tombstone blocks replay");

    FrameAssociationLedger importRelease;
    begin(importRelease);
    complete(importRelease, source(), first);
    require(importRelease.retireImported(session(), first.importedResource) == FrameAssociationResult::Accepted,
            "explicit imported resource release recorded");
    require(!importRelease.resolveExact(first), "released imported resource remains opaque");
    require(importRelease.recordImported(session(), first.decodedSurface, first.importedResource) ==
            FrameAssociationResult::Stale, "import tombstone blocks replay");

    FrameAssociationLedger namespaces;
    begin(namespaces);
    complete(namespaces, source(), first);
    auto separate = first;
    separate.decodedSurface.namespaceId++;
    separate.importedResource.namespaceId++;
    require(namespaces.recordDecoded(session(), first.native, separate.decodedSurface) ==
            FrameAssociationResult::Accepted, "same handle in another decoder namespace is independent");
    require(namespaces.recordImported(session(), separate.decodedSurface, separate.importedResource) ==
            FrameAssociationResult::Accepted, "same handle in another importer namespace is independent");
    exact(namespaces, separate, 100);
    exact(namespaces, first, 100);
}

void independentResourceReuseAndDuplicates() {
    FrameAssociationLedger ledger;
    begin(ledger);
    const auto input = source();
    const auto first = selected();
    complete(ledger, input, first);
    require(ledger.recordSource(input) == FrameAssociationResult::Duplicate,
            "same source evidence is idempotent");
    require(ledger.recordNative(session(), 100, first.native) == FrameAssociationResult::Duplicate,
            "same native evidence is idempotent");
    require(ledger.recordDecoded(session(), first.native, first.decodedSurface) == FrameAssociationResult::Duplicate,
            "same decoded evidence is idempotent");
    require(ledger.recordImported(session(), first.decodedSurface, first.importedResource) == FrameAssociationResult::Duplicate,
            "same import evidence is idempotent");

    // Reusing only the import already invalidates the old selection, even while
    // the original decoder surface has not been reused or released.
    auto second = first;
    second.native.generation++;
    second.decodedSurface.handle++;
    second.importedResource.generation++;
    require(ledger.recordImported(session(), second.decodedSurface, second.importedResource) ==
            FrameAssociationResult::Accepted, "new imported content generation accepted first");
    require(!ledger.resolveExact(first), "import-only reuse invalidates old selected content");
    require(!ledger.resolveExact(second), "import-first evidence cannot guess missing native/decoder stages");
    require(ledger.recordNative(session(), 101, second.native) == FrameAssociationResult::Accepted,
            "same 16-bit key with explicit next epoch can map a different source");
    require(ledger.recordDecoded(session(), second.native, second.decodedSurface) ==
            FrameAssociationResult::Accepted, "next epoch matches its exact decoded result");
    require(!ledger.resolveExact(second), "missing source metadata still leaves next epoch opaque");
    require(ledger.recordSource(source(101)) == FrameAssociationResult::Accepted, "late next source recorded");
    exact(ledger, second, 101);

    FrameAssociationLedger tombstones;
    begin(tombstones);
    require(tombstones.retireDecoded(session(), first.decodedSurface) == FrameAssociationResult::Accepted,
            "release can arrive before metadata for a decoder surface");
    require(tombstones.recordDecoded(session(), first.native, first.decodedSurface) == FrameAssociationResult::Stale,
            "late metadata cannot recreate an already released surface");
    require(tombstones.retireImported(session(), first.importedResource) == FrameAssociationResult::Accepted,
            "release can arrive before import metadata");
    require(tombstones.recordImported(session(), first.decodedSurface, first.importedResource) == FrameAssociationResult::Stale,
            "late metadata cannot recreate an already released import");
    require(tombstones.active(), "unambiguous delayed stale metadata does not poison valid future evidence");
}

void explicitRetirementBarriers() {
    FrameAssociationLedger ledger;
    begin(ledger);
    const auto first = selected(65535, 1);
    const auto next = selected(0, 2, 2, 3);
    complete(ledger, source(100), first);
    complete(ledger, source(101), next);
    require(ledger.retireThrough(session(), 100, first.native) == FrameAssociationResult::Accepted,
            "trusted retirement barriers recorded");
    require(!ledger.resolveExact(first), "retired source/native chain stays opaque");
    exact(ledger, next, 101);
    require(ledger.recordSource(source(100)) == FrameAssociationResult::Stale,
            "retired source cannot be recreated after reclaim");
    require(ledger.recordNative(session(), 102, first.native) == FrameAssociationResult::Stale,
            "retired native key/epoch cannot be rebound to a new source");
    require(ledger.retireThrough(session(), 99, first.native) == FrameAssociationResult::Stale,
            "retirement never regresses from out-of-order barrier delivery");
    exact(ledger, next, 101);

    // A surface reuse is a lifetime event even when the new token is stale:
    // it must invalidate the previously complete selected chain.
    auto reused = next.decodedSurface;
    ++reused.generation;
    require(ledger.recordDecoded(session(), first.native, reused) == FrameAssociationResult::Stale,
            "new surface generation cannot restore an already retired native token");
    require(!ledger.resolveExact(next), "stale-token surface reuse still invalidates previous content");

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    require(ledger.retireThrough(session(), maximum, {65535, maximum}) ==
            FrameAssociationResult::Accepted, "explicit terminal barriers are representable without overflow");
    require(ledger.recordSource(source(0)) == FrameAssociationResult::Stale,
            "source counter wrap cannot revive retired frame zero");
    require(ledger.recordNative(session(), maximum, {0, maximum}) == FrameAssociationResult::Stale,
            "native key truncation cannot bypass terminal retirement");
    begin(ledger, 2);
    const auto restarted = selected(0, 1, 12, 27, 2);
    complete(ledger, source(0, 2), restarted);
    exact(ledger, restarted, 0);
}

void conflictsDisableWholeAssociation() {
    for (unsigned stage = 0; stage < 4; ++stage) {
        FrameAssociationLedger ledger;
        begin(ledger);
        const auto pick = selected();
        complete(ledger, source(), pick);
        FrameAssociationResult result = FrameAssociationResult::Invalid;
        switch (stage) {
        case 0: {
            auto conflict = source(); conflict.eyes[0].width++;
            result = ledger.recordSource(conflict); break;
        }
        case 1: result = ledger.recordNative(session(), 101, pick.native); break;
        case 2: result = ledger.recordDecoded(session(), {18, 3}, pick.decodedSurface); break;
        case 3: result = ledger.recordImported(session(), {5, 13, 1}, pick.importedResource); break;
        }
        require(result == FrameAssociationResult::Conflict, "contradictory evidence is rejected");
        require(!ledger.active() && !ledger.resolveExact(pick), "a conflicting chain cannot leave another chain trusted");
        require(ledger.recordSource(source(101)) == FrameAssociationResult::Inactive,
                "later evidence does not silently recover a poisoned session");
        require(ledger.beginSession(session()) == FrameAssociationResult::Invalid,
                "same-session reset cannot hide conflicting evidence");
        begin(ledger, 2);
        complete(ledger, source(0, 2), selected(17, 3, 12, 27, 2));
        exact(ledger, selected(17, 3, 12, 27, 2), 0);
    }
}

void malformedAndForeignEvidence() {
    FrameAssociationLedger ledger;
    require(ledger.beginSession({}) == FrameAssociationResult::Invalid, "unknown session ID is rejected");
    begin(ledger);
    const auto pick = selected();
    complete(ledger, source(), pick);
    require(ledger.recordSource(source(0, 2)) == FrameAssociationResult::WrongSession,
            "late foreign session evidence cannot switch sessions");
    exact(ledger, pick, 100);
    ledger.invalidate();
    require(!ledger.resolveExact(pick), "an explicit evidence gap immediately keeps rendering opaque");
    begin(ledger, 2);
    ledger.endSession();
    require(!ledger.active(), "ended session cannot resolve");
    require(ledger.beginSession(session(2)) == FrameAssociationResult::Invalid,
            "session ID cannot be reused after disconnect");

    for (unsigned kind = 0; kind < 4; ++kind) {
        FrameAssociationLedger invalid;
        begin(invalid);
        FrameAssociationResult result = FrameAssociationResult::Accepted;
        switch (kind) {
        case 0: {
            auto bad = source(); bad.eyes[1].height = 0;
            result = invalid.recordSource(bad); break;
        }
        case 1: result = invalid.recordNative(session(), 100, {17, 0}); break;
        case 2: result = invalid.recordDecoded(session(), {17, 3}, {5, 0, 1}); break;
        case 3: result = invalid.recordImported(session(), {5, 12, 1}, {0, 27, 1}); break;
        }
        require(result == FrameAssociationResult::Invalid && !invalid.active(),
                "unknown source/epoch/resource data cannot keep a session trusted");
    }
    FrameAssociationLedger oversized;
    begin(oversized);
    auto bad = source(); bad.eyes[0].width = 4097;
    require(oversized.recordSource(bad) == FrameAssociationResult::Invalid,
            "ledger cannot produce an unsupported mask extent");
}

void boundedStorageAndReclaim() {
    for (unsigned kind = 0; kind < 4; ++kind) {
        FrameAssociationLedger ledger;
        begin(ledger);
        const auto cap = kind == 0 ? FrameAssociationLedger::SourceCapacity :
            kind == 1 ? FrameAssociationLedger::NativeCapacity :
            kind == 2 ? FrameAssociationLedger::DecodedCapacity : FrameAssociationLedger::ImportedCapacity;
        for (std::size_t i = 0; i <= cap; ++i) {
            const auto handle = std::uint64_t(i + 1);
            const NativeFrameIdentity native{static_cast<std::uint16_t>(i), 1};
            FrameAssociationResult result = FrameAssociationResult::Invalid;
            switch (kind) {
            case 0: result = ledger.recordSource(source(handle)); break;
            case 1: result = ledger.recordNative(session(), handle, native); break;
            case 2: result = ledger.recordDecoded(session(), native, {5, handle, 1}); break;
            case 3: result = ledger.recordImported(session(), {5, handle, 1}, {8, handle, 1}); break;
            }
            require(result == (i < cap ? FrameAssociationResult::Accepted : FrameAssociationResult::CapacityExceeded),
                    "fixed-capacity exhaustion is explicit and never evicts an identity");
        }
        require(!ledger.active(), "capacity loss fails closed");
    }

    // Long streams can safely reuse bounded tables when the native adapter
    // proves retirement. No latest-frame or timeout eviction is involved.
    FrameAssociationLedger ledger;
    begin(ledger);
    for (std::uint64_t frame = 0; frame < 1000; ++frame) {
        auto pick = selected(static_cast<std::uint16_t>(frame), 1, 12, 27);
        pick.decodedSurface.generation = frame + 1;
        pick.importedResource.generation = frame + 1;
        complete(ledger, source(frame), pick);
        exact(ledger, pick, frame);
        require(ledger.retireThrough(session(), frame, pick.native) == FrameAssociationResult::Accepted,
                "explicit barriers reclaim frame/key capacity across repeated content generations");
        require(!ledger.resolveExact(pick), "retirement cannot retain a previous frame association");
    }
    require(ledger.active(), "bounded long stream remains usable with valid retirement proof");
}

} // namespace

int main() {
    try {
        missingStagesAndReorderedEvidence();
        mismatchNeverFallsBack();
        reorderedFramesAndExplicitWrap();
        generationReuseAndRetirement();
        independentResourceReuseAndDuplicates();
        explicitRetirementBarriers();
        conflictsDisableWholeAssociation();
        malformedAndForeignEvidence();
        boundedStorageAndReclaim();
        std::cout << checks << " frame association checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Frame association failure: " << error.what() << '\n';
        return 1;
    }
}
