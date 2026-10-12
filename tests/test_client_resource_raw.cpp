#include "import/client_resource_raw.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {
using namespace frame_client_raw;
using frame_client_native::StackBytes;
unsigned checks = 0;
void check_at(bool value, unsigned line) {
    ++checks;
    if (!value) { std::fprintf(stderr, "raw resource check %u failed at line %u\n", checks, line); std::abort(); }
}
#define check(value) check_at((value), __LINE__)
constexpr std::uintptr_t kBase = 0x100000, kClient = 0x500000, kReceiver = 0x900000;
constexpr std::uintptr_t kImport = kBase + 864, kCaller = kImport + 352;
constexpr std::uintptr_t kPipe = 0x700000, kRecord = 0x800000, kMarker = 0xb00000;
constexpr std::uint64_t kToken = 0x8877665544332211ULL;
constexpr std::uint32_t kSequence = 0xffffffff;

template<std::size_t N> void put_le(std::array<std::uint8_t, N>& bytes, unsigned offset,
                                  std::uint64_t value, unsigned count) {
    check(offset <= N && count <= N - offset);
    for (unsigned i = 0; i < count; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
struct Fixture {
    Observer observer{};
    Scope scope{};
    std::array<std::uint8_t, 1552> stack_bytes{};
    std::array<std::uint8_t, 12> request{}, header{};
    std::array<std::uint8_t, 9> payload{};
    unsigned total = 21;
    std::uint32_t sequence = kSequence;
    template<class T> void put(std::uintptr_t at, T value) {
        check(at >= kBase && at - kBase <= stack_bytes.size() - sizeof(T));
        std::memcpy(stack_bytes.data() + at - kBase, &value, sizeof(T));
    }
    void link(std::uintptr_t frame, std::uintptr_t parent, std::uintptr_t bias, std::uint64_t rva) {
        put(frame, std::uint64_t{parent}); put(frame + 8, std::uint64_t{bias + rva});
    }
    explicit Fixture(unsigned size = 21, std::uint32_t seq = kSequence) : total(size), sequence(seq) {
        link(kImport, kCaller, kReceiver, 0x34608);
        put(kImport + 136, std::uint64_t{kPipe}); put(kImport + 16, std::uint64_t{kRecord});
        put_le(request, 0, 0x234, 4); put_le(request, 4, sequence, 4); put_le(request, 8, total - 12, 4);
        put_le(header, 0, 0x235, 4); put_le(header, 4, sequence, 4); put_le(header, 8, 9, 4);
        payload[0] = 0x09; put_le(payload, 1, kToken, 8);
    }
    Entry entry(Boundary b, std::uintptr_t frame) const {
        constexpr std::uint64_t sites[]{0x3623c0,0x2456c8,0x245334,0x245460,0x3624c4,0x362d58,0x244dbc};
        Entry out{}; out.x[30] = kClient + sites[static_cast<unsigned>(b)];
        out.x[29] = out.sp = frame; return out;
    }
    Entry direct(Boundary b) {
        auto out = entry(b, kImport);
        out.x[0] = out.x[21] = kPipe; out.x[20] = kRecord + 32;
        out.x[25] = 0x12345678beefULL; // Never a sequence at Begin or Pending.
        if (b == Boundary::Pending) {
            put(kImport + 224, std::uint32_t{1}); put(kImport + 232, kToken);
            out.x[24] = 1; out.x[21] = 0xaaaa; out.x[25] = 0xbbbb;
        }
        return out;
    }
    Entry writer(unsigned offset) {
        const auto rounded = (total + 15) & ~15U;
        const auto send = kImport - 80, packet = send - rounded, frame = packet - 80;
        link(frame, send, kClient, 0x2459fc); link(send, kImport, kClient, 0x362418);
        put(frame + 32, std::uint32_t{0x234}); put(frame + 40, sequence);
        put(send + 64, std::uint64_t{kPipe}); put(send + 24, std::uint64_t{kRecord + 32});
        auto out = entry(Boundary::Write, frame);
        out.x[0] = 7; out.x[19] = offset; out.x[20] = total; out.x[21] = total - offset;
        out.x[22] = kPipe; out.x[23] = packet; out.x[1] = packet + offset; out.x[2] = total - offset;
        return out;
    }
    Entry reader(Boundary b, unsigned offset) {
        const auto waiter = kImport - 192, frame = waiter - 96;
        link(frame, waiter, kClient, b == Boundary::HeaderRead ? 0x364b58 : 0x364ba8);
        link(waiter, kImport, kClient, 0x362440); put(waiter + 116, sequence);
        put(frame + 16, std::uint64_t{kPipe}); auto out = entry(b, frame);
        out.x[0] = 7; out.x[19] = offset; out.x[20] = kPipe;
        if (b == Boundary::HeaderRead) {
            out.x[24] = 12; out.x[25] = frame + 80; out.x[1] = frame + 80 + offset; out.x[2] = 12 - offset;
        } else {
            put(waiter + 156, std::uint32_t{9}); out.x[21] = out.x[23] = 9;
            out.x[22] = 0xa00000; out.x[1] = out.x[22] + offset; out.x[2] = 9 - offset;
        }
        return out;
    }
    Entry reference() {
        const auto frame = kCaller - 240;
        link(frame, kCaller, kReceiver, 0x3462c); put(frame + 104, std::uint64_t{kPipe});
        put(frame + 16, std::uint64_t{kRecord}); put(frame + 224, kToken);
        auto out = entry(Boundary::Reference, frame); out.x[0] = out.x[19] = kPipe; out.x[24] = kToken;
        return out;
    }
    Event before(Boundary b, Entry entry) {
        Event out{}; out.boundary = b; out.entry = entry; out.thread_identity = kMarker;
        out.current_thread_owned = out.native_module_current = true;
        if (b == Boundary::Pending) {
            out.scalars = frame_client_native::decode(b, entry,
                {stack_bytes.data(), kBase, stack_bytes.size()}, kClient, kReceiver, scope.expected());
            out.decoded_available = true; out.scalar_bytes_copied = 44;
        }
        return out;
    }
    Event after(const Event& before, std::int64_t result) {
        Event out = before; out.phase = Phase::After; out.native_result = result;
        out.native_result_available = true; out.preserved_errno = EBUSY;
        if (before.boundary == Boundary::Pending) { out.scalar_bytes_copied = 0; return out; }
        if (result < 0 || (result == 0 && before.boundary != Boundary::Begin && before.boundary != Boundary::Reference)) return out;
        if (before.boundary != Boundary::Begin && before.boundary != Boundary::Reference &&
            static_cast<std::uint64_t>(result) > before.entry.x[2]) return out;
        out.scalars = frame_client_native::decode(before.boundary, before.entry,
            {stack_bytes.data(), kBase, stack_bytes.size()}, kClient, kReceiver, scope.expected());
        out.decoded_available = true;
        out.scalar_bytes_copied = before.boundary == Boundary::Begin ? 32 :
            before.boundary == Boundary::Reference ? 40 : before.boundary == Boundary::Write ? 88 :
            before.boundary == Boundary::HeaderRead ? 76 : 80;
        if (before.boundary == Boundary::Write) {
            const auto offset = static_cast<unsigned>(before.entry.x[19]);
            out.prefix.count = offset < 12 ? std::min<std::size_t>(result, 12 - offset) : 0;
            if (out.prefix.count) std::memcpy(out.prefix.bytes.data(), request.data() + offset, out.prefix.count);
        } else if (before.boundary == Boundary::HeaderRead || before.boundary == Boundary::PayloadRead) {
            const auto offset = static_cast<unsigned>(before.entry.x[19]);
            out.prefix.count = result;
            const auto* bytes = before.boundary == Boundary::HeaderRead ? header.data() : payload.data();
            if (out.prefix.count) std::memcpy(out.prefix.bytes.data(), bytes + offset, out.prefix.count);
        }
        out.prefix.successful_prefix_owned = out.prefix.count != 0;
        return out;
    }
    Outcome pair(Boundary b, Entry entry, std::int64_t result) {
        const auto input = before(b, entry);
        auto probe = input;
        probe.decoded_available = false; probe.scalars = {}; probe.scalar_bytes_copied = 0;
        check(observer.can_acquire(probe, scope) == (b == Boundary::Pending));
        check(observer.observe(input, scope).kind == Kind::None);
        probe.phase = Phase::After; probe.native_result_available = true; probe.native_result = result;
        check(observer.can_acquire(probe, scope) == (b != Boundary::Pending));
        return observer.observe(after(input, result), scope);
    }
    void start() {
        check(pair(Boundary::Begin, direct(Boundary::Begin), 0).kind == Kind::None);
        check(scope.active() && !scope.finished());
        check(scope.expected().invocation.import_frame == kImport && !scope.expected().sequence_available);
    }
    void write(unsigned offset, unsigned count) { check(pair(Boundary::Write, writer(offset), count).kind == Kind::None); }
    void read(Boundary b, unsigned offset, unsigned count) { check(pair(b, reader(b, offset), count).kind == Kind::None); }
    void wire() { write(0, total); read(Boundary::HeaderRead, 0, 12); read(Boundary::PayloadRead, 0, 9); }
    Outcome end() {
        check(pair(Boundary::Pending, direct(Boundary::Pending), 0).kind == Kind::None);
        check(scope.record().matching_pending && !scope.record().following_reference_observed);
        return pair(Boundary::Reference, reference(), 0);
    }
};
void no_proof(const RawRecord& r) {
    check(r.raw_only && !r.connection_generation && !r.continuous_connection_owned && !r.immutable_lifetime_lease);
    check(!r.reference_succeeded && !r.native_cross_endpoint_associated && !r.native_namespace_associated);
    check(!r.source_frame_associated && !r.uv_owned && !r.accepted_submit && !r.gpu_consumed);
}
void matched(const Fixture& f, const Outcome& result) {
    check(result.kind == Kind::Matched && result.record.matched && result.record.terminal && result.record.reason == None);
    check(f.scope.finished() && !f.scope.active()); no_proof(result.record);
    const auto& r = result.record;
    check(r.request_complete && r.reply_complete && r.begin_lock_succeeded && r.pending_unlock_succeeded);
    check(r.native_wait_success && r.matching_pending && r.reference_lock_succeeded && r.following_reference_observed);
    check(r.sequence_available && r.sequence == f.sequence && r.fd == 7 && r.request_bytes == f.total);
    check(r.request_header == f.request && r.reply_header == f.header && r.reply_payload == f.payload);
    check(r.reply_token == kToken && r.parsed_token == kToken && r.referenced_token == kToken);
    check(r.scalar_bytes_copied == 116 + r.writes * 88 + r.header_reads * 76 + r.payload_reads * 80);
    check(r.scalar_bytes_copied <= 2068 && r.wire_bytes_copied == 33);
}
void rejected(const Fixture& f, const Outcome& result, std::uint64_t reason) {
    check(result.kind == Kind::Refused && result.record.terminal && !result.record.matched);
    check((result.record.reason & reason) == reason);
    check(f.scope.finished()); no_proof(result.record);
    check(result.record.scalar_bytes_copied <= kMaxScalarBytes && result.record.wire_bytes_copied <= kMaxWireBytes);
    check(f.observer.abandon(const_cast<Scope&>(f.scope)).kind == Kind::None);
}
template<class Mutate> void rejected_after(Boundary b, unsigned stage, Mutate mutate, std::uint64_t reason) {
    Fixture f; f.start();
    if (stage >= 1) f.write(0, f.total);
    if (stage >= 2) f.read(Boundary::HeaderRead, 0, 12);
    if (stage >= 3) f.read(Boundary::PayloadRead, 0, 9);
    if (stage >= 4) check(f.pair(Boundary::Pending, f.direct(Boundary::Pending), 0).kind == Kind::None);
    Entry entry = b == Boundary::Write ? f.writer(0) : b == Boundary::Reference ? f.reference() :
        b == Boundary::Pending ? f.direct(b) : f.reader(b, 0);
    auto before = f.before(b, entry); check(f.observer.observe(before, f.scope).kind == Kind::None);
    auto after = f.after(before, b == Boundary::Write ? f.total : b == Boundary::HeaderRead ? 12 : b == Boundary::PayloadRead ? 9 : 0);
    mutate(after); rejected(f, f.observer.observe(after, f.scope), reason);
}
}

int main() {
    for (unsigned total : {13U, 21U, 255U, 333U, 512U}) for (std::uint32_t sequence : {0U, 1U, kSequence}) {
        Fixture f(total, sequence); f.start(); f.wire(); matched(f, f.end());
        check(f.observer.observe(f.before(Boundary::Begin, f.direct(Boundary::Begin)), f.scope).kind == Kind::None);
    }
    // Every two-part native request split, including crossing the header/body
    // boundary. The request body is represented by counts only.
    for (unsigned total : {13U, 21U, 255U, 333U, 512U}) for (unsigned split = 1; split < total; ++split) {
        Fixture f(total); f.start(); f.write(0, split); f.write(split, total - split);
        f.read(Boundary::HeaderRead, 0, 12); f.read(Boundary::PayloadRead, 0, 9); matched(f, f.end());
    }
    // All ordered partitions of header/payload lengths up to the eight-call
    // cap, independently. Above-cap partitions are deliberately unsupported.
    for (Boundary b : {Boundary::HeaderRead, Boundary::PayloadRead}) {
        const unsigned length = b == Boundary::HeaderRead ? 12 : 9;
        for (unsigned mask = 0; mask < (1U << (length - 1)); ++mask) {
            unsigned calls = 1; for (unsigned bit = 0; bit < length - 1; ++bit) calls += (mask >> bit) & 1U;
            if (calls > 8) continue;
            Fixture f; f.start(); f.write(0, f.total);
            if (b == Boundary::PayloadRead) f.read(Boundary::HeaderRead, 0, 12);
            unsigned offset = 0;
            for (unsigned bit = 0; bit < length; ++bit) {
                if (bit + 1 == length || (mask & (1U << bit))) {
                    f.read(b, offset, bit + 1 - offset); offset = bit + 1;
                }
            }
            if (b == Boundary::HeaderRead) f.read(Boundary::PayloadRead, 0, 9);
            matched(f, f.end());
        }
    }
    { // Simultaneous maximum successful transfer and acquisition budgets.
        Fixture f(512); f.start();
        for (unsigned i = 0; i < 8; ++i) f.write(i * 64, 64);
        for (unsigned i = 0; i < 8; ++i) f.read(Boundary::HeaderRead, i < 7 ? i : 7, i < 7 ? 1 : 5);
        for (unsigned i = 0; i < 8; ++i) f.read(Boundary::PayloadRead, i < 7 ? i : 7, i < 7 ? 1 : 2);
        const auto result = f.end(); matched(f, result);
        check(result.record.scalar_bytes_copied == 2068 && result.record.wire_bytes_copied == 33);
        check(result.record.writes + result.record.header_reads + result.record.payload_reads == 24);
    }
    for (Boundary b : {Boundary::Write, Boundary::HeaderRead, Boundary::PayloadRead}) {
        Fixture f; f.start(); if (b != Boundary::Write) f.write(0, f.total);
        if (b == Boundary::PayloadRead) f.read(Boundary::HeaderRead, 0, 12);
        for (unsigned i = 0; i < 8; ++i) {
            if (b == Boundary::Write) f.write(i, 1); else f.read(b, i, 1);
        }
        auto before = f.before(b, b == Boundary::Write ? f.writer(8) : f.reader(b, 8));
        rejected(f, f.observer.observe(before, f.scope), TransferLimit);
    }
    for (auto b : {Boundary::Write, Boundary::HeaderRead, Boundary::PayloadRead}) {
        const unsigned stage = b == Boundary::Write ? 0 : b == Boundary::HeaderRead ? 1 : 2;
        for (std::int64_t result : {-1, 0}) {
            rejected_after(b, stage, [=](Event& e) { e.native_result = result; e.decoded_available = false;
                e.scalar_bytes_copied = 0; e.prefix = {}; }, NativeFailure | UnsupportedTransfer);
        }
        rejected_after(b, stage, [](Event& e) { e.native_result = static_cast<std::int64_t>(e.entry.x[2]) + 1;
            e.decoded_available = false; e.scalar_bytes_copied = 0; e.prefix = {}; }, UnsupportedTransfer);
        rejected_after(b, stage, [](Event& e) { --e.prefix.count; }, InvalidPrefix);
        rejected_after(b, stage, [](Event& e) { e.prefix.successful_prefix_owned = false; }, InvalidPrefix);
        rejected_after(b, stage, [](Event& e) { e.scalars.refusal = frame_client_native::Refusal::Link; }, InvalidDecode);
        rejected_after(b, stage, [](Event& e) { e.scalar_bytes_copied++; }, AcquisitionBudget);
        rejected_after(b, stage, [](Event& e) { e.entry.x[3]++; }, ChangedEntry);
        rejected_after(b, stage, [](Event& e) { e.native_result_available = false; }, UnmatchedCompletion);
        rejected_after(b, stage, [](Event& e) { e.scalars.invocation.record += 8; }, ChangedInvocation);
        rejected_after(b, stage, [](Event& e) { e.scalars.offset++; }, ChangedSpan);
    }
    for (unsigned index : {0U, 4U, 8U}) {
        rejected_after(Boundary::Write, 0, [=](Event& e) { e.prefix.bytes[index] ^= 1; }, BadRequest);
        rejected_after(Boundary::HeaderRead, 1, [=](Event& e) { e.prefix.bytes[index] ^= 1; }, BadReply | ParserContamination);
    }
    rejected_after(Boundary::PayloadRead, 2, [](Event& e) { e.prefix.bytes[0] = 0x08; }, BadReply | ParserContamination);
    rejected_after(Boundary::PayloadRead, 2, [](Event& e) { e.prefix.bytes.fill(0); e.prefix.bytes[0] = 0x09; }, BadReply);
    { // An invalid header cannot be repaired with a later convenient reply.
        Fixture f; f.start(); f.write(0, f.total); auto before = f.before(Boundary::HeaderRead, f.reader(Boundary::HeaderRead, 0));
        check(f.observer.observe(before, f.scope).kind == Kind::None); auto after = f.after(before, 12); after.prefix.bytes[4] ^= 1;
        rejected(f, f.observer.observe(after, f.scope), ParserContamination);
        check(f.observer.observe(before, f.scope).kind == Kind::None && !f.scope.record().matched);
    }
    for (Boundary b : {Boundary::Write, Boundary::HeaderRead, Boundary::PayloadRead}) {
        Fixture f; f.start(); if (b != Boundary::Write) f.write(0, f.total);
        if (b == Boundary::PayloadRead) f.read(Boundary::HeaderRead, 0, 12);
        if (b == Boundary::Write) f.write(0, 1); else f.read(b, 0, 1);
        auto before = f.before(b, b == Boundary::Write ? f.writer(1) : f.reader(b, 1));
        check(f.observer.observe(before, f.scope).kind == Kind::None); auto after = f.after(before, 1);
        after.scalars.buffer_base += 16; after.entry.x[1] += 16;
        // Changing both the proposed span and entry never repairs the pair.
        rejected(f, f.observer.observe(after, f.scope), ChangedEntry);
    }
    { Fixture f; f.start(); f.write(0, 1); auto before = f.before(Boundary::Write, f.writer(1));
        before.entry.x[0] = 8; check(f.observer.observe(before, f.scope).kind == Kind::None);
        auto after = f.after(before, 1); rejected(f, f.observer.observe(after, f.scope), ChangedDescriptor); }
    { Fixture f; f.start(); f.write(0, 1); f.sequence--;
        auto before = f.before(Boundary::Write, f.writer(1)); check(f.observer.observe(before, f.scope).kind == Kind::None);
        rejected(f, f.observer.observe(f.after(before, 1), f.scope), ChangedInvocation); }
    { Fixture f; f.start(); f.write(0, 1);
        auto before = f.before(Boundary::Write, f.writer(0)); check(f.observer.observe(before, f.scope).kind == Kind::None);
        rejected(f, f.observer.observe(f.after(before, 1), f.scope), ChangedSpan); }
    { Fixture f; f.start(); f.write(0, 1); f.total++;
        auto before = f.before(Boundary::Write, f.writer(1)); check(f.observer.observe(before, f.scope).kind == Kind::None);
        rejected(f, f.observer.observe(f.after(before, 1), f.scope), ChangedSpan); }
    { // A genuinely decoded new payload buffer still contaminates this reply.
        Fixture f; f.start(); f.write(0, f.total); f.read(Boundary::HeaderRead, 0, 12);
        f.read(Boundary::PayloadRead, 0, 1); auto entry = f.reader(Boundary::PayloadRead, 1);
        entry.x[22] += 16; entry.x[1] += 16;
        auto before = f.before(Boundary::PayloadRead, entry);
        check(f.observer.observe(before, f.scope).kind == Kind::None);
        rejected(f, f.observer.observe(f.after(before, 8), f.scope), ChangedSpan | ParserContamination); }
    { Fixture f; f.start(); f.write(0, f.total); f.read(Boundary::HeaderRead, 0, 12);
        rejected(f, f.observer.observe(f.before(Boundary::HeaderRead, f.reader(Boundary::HeaderRead, 0)), f.scope),
                 Order | ParserContamination); }
    for (Boundary wrong : {Boundary::HeaderRead, Boundary::PayloadRead, Boundary::Pending, Boundary::Reference}) {
        Fixture f; f.start(); auto input = f.before(wrong, wrong == Boundary::Pending ? f.direct(wrong) :
            wrong == Boundary::Reference ? f.reference() : f.reader(wrong, 0));
        rejected(f, f.observer.observe(input, f.scope), wrong == Boundary::Reference ? MissingPending : Order);
    }
    for (unsigned stage = 0; stage < 5; ++stage) {
        Fixture f; f.start(); if (stage > 0) f.write(0, f.total);
        if (stage > 1) f.read(Boundary::HeaderRead, 0, 12);
        if (stage > 2) f.read(Boundary::PayloadRead, 0, 9);
        if (stage > 3) check(f.pair(Boundary::Pending, f.direct(Boundary::Pending), 0).kind == Kind::None);
        rejected(f, f.observer.observe(f.before(Boundary::Begin, f.direct(Boundary::Begin)), f.scope), NestedImport);
    }
    { Fixture f; auto before = f.before(Boundary::Begin, f.direct(Boundary::Begin));
        rejected(f, f.observer.observe(f.after(before, 0), f.scope), UnmatchedCompletion); }
    { Fixture f; auto before = f.before(Boundary::Begin, f.direct(Boundary::Begin));
        check(f.observer.observe(before, f.scope).kind == Kind::None); auto after = before;
        after.phase = Phase::After; after.native_result_available = true; after.native_result = EINVAL;
        rejected(f, f.observer.observe(after, f.scope), NativeFailure); }
    rejected_after(Boundary::Pending, 3, [](Event& e) { e.native_result = EPERM; }, NativeFailure | PendingFailed);
    rejected_after(Boundary::Pending, 3, [](Event& e) { e.scalars.token++; }, ChangedSnapshot);
    rejected_after(Boundary::Pending, 3, [](Event& e) { e.scalar_bytes_copied = 44; }, AcquisitionBudget);
    for (unsigned failure = 0; failure < 3; ++failure) {
        Fixture f; f.start(); f.wire(); auto before = f.before(Boundary::Pending, f.direct(Boundary::Pending));
        if (failure == 0) before.scalars.native_wait_success = false;
        if (failure == 1) before.scalars.token_present = false;
        if (failure == 2) before.scalars.token++;
        check(f.observer.observe(before, f.scope).kind == Kind::None);
        rejected(f, f.observer.observe(f.after(before, 0), f.scope), PendingFailed);
    }
    rejected_after(Boundary::Reference, 4, [](Event& e) { e.scalars.token++; }, InvalidReference | TokenMismatch);
    rejected_after(Boundary::Reference, 4, [](Event& e) { e.native_result = EDEADLK;
        e.decoded_available = false; e.scalar_bytes_copied = 0; }, NativeFailure | InvalidReference);
    for (unsigned where = 0; where < 4; ++where) {
        Fixture f; if (where > 0) f.start();
        auto before = f.before(where ? Boundary::Write : Boundary::Begin,
            where ? f.writer(0) : f.direct(Boundary::Begin));
        if (where > 1) check(f.observer.observe(before, f.scope).kind == Kind::None);
        auto input = where > 1 ? f.after(before, f.total) : before;
        input.current_thread_owned = false; input.thread_reason = 9;
        rejected(f, f.observer.observe(input, f.scope), ThreadUnavailable);
        check(f.scope.record().thread_reason == 9);
        input.current_thread_owned = true; input.thread_reason = 0;
        check(f.observer.observe(input, f.scope).kind == Kind::None && !f.scope.record().matched);
    }
    { Fixture f; f.start(); rejected(f, f.observer.expire(10, f.scope), ThreadUnavailable);
        check(f.scope.record().thread_reason == 10); }
    { Fixture f; f.start(); auto input = f.before(Boundary::Write, f.writer(0)); input.thread_identity++;
        rejected(f, f.observer.observe(input, f.scope), ThreadUnavailable); }
    { Fixture f; f.start(); auto input = f.before(Boundary::Write, f.writer(0)); input.thread_reason = 9;
        rejected(f, f.observer.observe(input, f.scope), ThreadUnavailable); }
    { Fixture f; f.start(); auto input = f.before(Boundary::Write, f.writer(0)); input.thread_identity = 0;
        rejected(f, f.observer.observe(input, f.scope), ThreadUnavailable); }
    { Fixture f; f.start(); auto input = f.before(Boundary::Write, f.writer(0)); input.native_module_current = false;
        rejected(f, f.observer.observe(input, f.scope), ModuleUnavailable); }
    { Fixture f; f.start(); rejected(f, f.observer.abandon(f.scope), Abandoned); }
    { Fixture f; f.start(); Observer other; rejected(f, other.abandon(f.scope), OwnerMismatch | Abandoned); }
    { Fixture f; f.start(); Observer other;
        rejected(f, other.observe(f.before(Boundary::Write, f.writer(0)), f.scope), OwnerMismatch); }
    { Fixture f; f.start(); auto before = f.before(Boundary::Write, f.writer(0)); before.scalar_bytes_copied = 88;
        rejected(f, f.observer.observe(before, f.scope), AcquisitionBudget); }
    { Fixture f; f.start(); auto before = f.before(Boundary::Write, f.writer(0)); before.decoded_available = true;
        rejected(f, f.observer.observe(before, f.scope), AcquisitionBudget); }
    { Fixture f; f.start(); auto before = f.before(Boundary::Write, f.writer(0)); before.prefix.count = 1;
        rejected(f, f.observer.observe(before, f.scope), AcquisitionBudget); }
    rejected_after(Boundary::Write, 0, [](Event& e) { e.scalar_bytes_copied = std::numeric_limits<std::size_t>::max(); }, AcquisitionBudget);
    rejected_after(Boundary::HeaderRead, 1, [](Event& e) { e.prefix.count = std::numeric_limits<std::size_t>::max(); }, InvalidPrefix);
    { Fixture f; f.start(); auto before = f.before(Boundary::Write, f.writer(0)); before.native_result_available = true;
        rejected(f, f.observer.observe(before, f.scope), UnmatchedCompletion); }
    { Fixture f; f.start(); auto input = f.before(Boundary::Write, f.writer(0)); input.phase = static_cast<Phase>(99);
        rejected(f, f.observer.observe(input, f.scope), Order); }
    { Fixture f; f.start(); auto input = f.before(Boundary::Write, f.writer(0)); input.boundary = Boundary::AcceptedInit;
        rejected(f, f.observer.observe(input, f.scope), UnsupportedBoundary); }
    { // Admission itself never decodes, copies, advances a Scope or publishes.
        Fixture f; auto before = f.before(Boundary::Begin, f.direct(Boundary::Begin));
        check(!f.observer.can_acquire(before, f.scope));
        auto probe = before; probe.phase = Phase::After; probe.native_result_available = true;
        check(!f.observer.can_acquire(probe, f.scope));
        check(f.observer.observe(before, f.scope).kind == Kind::None);
        for (unsigned i = 0; i < 3; ++i) check(f.observer.can_acquire(probe, f.scope));
        check(!f.scope.record().scalar_bytes_copied && !f.scope.record().begin_lock_succeeded);
        auto wrong = probe; wrong.entry.x[20]++; check(!f.observer.can_acquire(wrong, f.scope));
        wrong = probe; wrong.boundary = Boundary::Reference; check(!f.observer.can_acquire(wrong, f.scope));
        wrong = probe; wrong.phase = Phase::Before; check(!f.observer.can_acquire(wrong, f.scope));
        wrong = probe; wrong.native_result_available = false; check(!f.observer.can_acquire(wrong, f.scope));
        wrong = probe; wrong.native_result = EBUSY; check(!f.observer.can_acquire(wrong, f.scope));
        wrong = probe; wrong.current_thread_owned = false; check(!f.observer.can_acquire(wrong, f.scope));
        wrong = probe; wrong.native_module_current = false; check(!f.observer.can_acquire(wrong, f.scope));
        wrong = probe; wrong.thread_reason = 9; check(!f.observer.can_acquire(wrong, f.scope));
        wrong = probe; wrong.thread_identity++; check(!f.observer.can_acquire(wrong, f.scope));
        wrong = probe; wrong.decoded_available = true; check(!f.observer.can_acquire(wrong, f.scope));
        wrong = probe; wrong.scalar_bytes_copied = 32; check(!f.observer.can_acquire(wrong, f.scope));
        wrong = probe; wrong.prefix.count = 1; check(!f.observer.can_acquire(wrong, f.scope));
        wrong = probe; wrong.prefix.successful_prefix_owned = true; check(!f.observer.can_acquire(wrong, f.scope));
        Observer other; check(!other.can_acquire(probe, f.scope));
        check(f.observer.observe(f.after(before, 0), f.scope).kind == Kind::None);
        check(!f.observer.can_acquire(probe, f.scope)); // Duplicate Begin After.
        auto pending = f.before(Boundary::Pending, f.direct(Boundary::Pending));
        pending.decoded_available = false; pending.scalars = {}; pending.scalar_bytes_copied = 0;
        check(!f.observer.can_acquire(pending, f.scope)); // Wrong source order.
        auto ref = f.before(Boundary::Reference, f.reference()); ref.phase = Phase::After; ref.native_result_available = true;
        check(!f.observer.can_acquire(ref, f.scope));
        auto input = f.before(Boundary::Write, f.writer(0));
        check(!f.observer.can_acquire(input, f.scope));
        check(f.observer.observe(input, f.scope).kind == Kind::None);
        probe = input; probe.phase = Phase::After; probe.native_result_available = true; probe.native_result = f.total;
        check(f.observer.can_acquire(probe, f.scope));
        for (std::int64_t result : std::array<std::int64_t, 4>{{-1, 0, 22, std::numeric_limits<std::int64_t>::max()}}) {
            wrong = probe; wrong.native_result = result; check(!f.observer.can_acquire(wrong, f.scope));
        }
        wrong = probe; wrong.entry.x[1]++; check(!f.observer.can_acquire(wrong, f.scope));
        wrong = probe; wrong.boundary = Boundary::HeaderRead; check(!f.observer.can_acquire(wrong, f.scope));
        rejected(f, f.observer.abandon(f.scope), Abandoned);
        check(!f.observer.can_acquire(probe, f.scope));
    }
    { Fixture f; f.start(); f.wire(); auto pending = f.before(Boundary::Pending, f.direct(Boundary::Pending));
        auto probe = pending; probe.decoded_available = false; probe.scalars = {}; probe.scalar_bytes_copied = 0;
        check(f.observer.can_acquire(probe, f.scope));
        auto wrong = probe; wrong.entry.x[0]++; check(!f.observer.can_acquire(wrong, f.scope));
        wrong = probe; wrong.entry.sp -= 16; wrong.entry.x[29] = wrong.entry.sp;
        check(!f.observer.can_acquire(wrong, f.scope));
        check(f.observer.observe(pending, f.scope).kind == Kind::None);
        check(!f.observer.can_acquire(probe, f.scope)); // Duplicate Pending Before.
        probe.phase = Phase::After; probe.native_result_available = true;
        check(!f.observer.can_acquire(probe, f.scope)); // Pending After is reuse only.
        check(f.observer.observe(f.after(pending, 0), f.scope).kind == Kind::None);
        auto ref = f.before(Boundary::Reference, f.reference()); check(f.observer.observe(ref, f.scope).kind == Kind::None);
        probe = ref; probe.phase = Phase::After; probe.native_result_available = true;
        check(f.observer.can_acquire(probe, f.scope));
        wrong = probe; wrong.entry.x[24]++; check(!f.observer.can_acquire(wrong, f.scope));
        matched(f, f.observer.observe(f.after(ref, 0), f.scope));
        check(!f.observer.can_acquire(probe, f.scope));
    }
    { // Raw evidence cannot enter the strict carrier without invented leases.
        Fixture f; f.start(); f.wire(); const auto result = f.end(); matched(f, result);
        frame_client_carrier::Observer strict; strict.armed = true; frame_client_carrier::Scope scope;
        const auto& r = result.record;
        frame_client_carrier::Route route{}; route.import_frame = r.invocation.import_frame;
        route.receiver_frame = r.invocation.receiver_frame; route.pipe = r.invocation.pipe; route.record = r.invocation.record;
        route.fd = r.fd; route.exact_native_route = true; route.active_invocation_owned = true;
        route.connection_generation = r.connection_generation; route.continuous_connection_owned = r.continuous_connection_owned;
        route.immutable_lifetime_lease = r.immutable_lifetime_lease;
        check(strict.begin({route, frame_client_carrier::kBegin, 0}, scope));
        check(scope.record.reason & frame_client_carrier::InvalidRoute);
        strict.abandon(frame_client_carrier::Abandoned, scope);
        frame_client_carrier::Record output; check(strict.take(output));
        check(!output.output_published && !output.reference_succeeded && !output.gpu_consumed);
    }
    std::printf("%u raw resource metadata checks passed; no acquisition, leases, frame association or GPU admission\n", checks);
}
