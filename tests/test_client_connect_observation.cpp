#include "import/client_connect_observation.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace frame_client_connect;
using Operation = frame_import_forwarding::Operation;
using Phase = frame_import_forwarding::Phase;
using Note = frame_import_forwarding::Notification;
using Entry = frame_import_native::Entry;
namespace {
unsigned checks = 0;
void check_at(bool okay, unsigned line) {
    ++checks;
    if (!okay) {
        std::fprintf(stderr, "connect observation check %u failed at line %u\n", checks, line);
        std::abort();
    }
}
#define check(value) check_at((value), __LINE__)
constexpr std::uintptr_t bias = 0x500000, sp = 0x100000;
Target target() {
    Target t;
    // Independent literal source anchors; this fixture does not grant real
    // mapping/birth permission or a production Ready capability.
    t.module.load_bias = bias; t.module.code_begin = bias;
    t.module.code_end = bias + 0x60699a; t.module.module_lifetime_label = 72;
    t.module.code_identity[0] = 0xb5;
    t.object = {0x123400, bias + 0x649ff0, 824865, 1,
                frame_import_pipe::Domain::Client045, frame_import_pipe::Reason::None};
    return t;
}
Entry lock_entry() {
    Entry e;
    for (unsigned i = 0; i < 31; ++i) e.x[i] = 0x4000 + i * 16;
    e.sp = e.x[29] = sp; e.x[0] = e.x[19] = bias + 0x649ff0;
    e.x[30] = bias + 0x245ea8;
    return e;
}
Entry connect_entry() {
    auto e = lock_entry(); e.x[0] = 7;
    e.x[1] = e.x[20] = sp + 80; e.x[2] = e.x[22] = 16;
    e.x[30] = bias + 0x245f9c;
    return e;
}
Entry unlock_entry(bool success = true) {
    auto e = lock_entry(); e.x[30] = bias + (success ? 0x245fb0 : 0x245ff0);
    return e;
}
Note note(Operation op, Phase phase, Entry e, std::uint64_t result = 0, int error = 0) {
    return {op, phase, e, result, error};
}
struct Fixture {
    Gate gate;
    Scope scope = Scope::prepare_current();
    explicit Fixture(Target t = target()) : gate(t) {}
    Outcome send(Operation op, Phase phase, Entry e, std::uint64_t bits = 0, int error = 0) {
        return gate.observe(note(op, phase, e, bits, error), scope);
    }
    void locked() {
        check(send(Operation::Lock, Phase::Before, lock_entry()).kind == Kind::None);
        check(!scope.ownership_active());
        check(send(Operation::Lock, Phase::After, lock_entry()).kind == Kind::None);
        check(scope.ownership_active());
    }
    Outcome connected(std::uint64_t bits = 0, int error = 0) {
        locked();
        check(send(Operation::Connect, Phase::Before, connect_entry()).kind == Kind::None);
        return send(Operation::Connect, Phase::After, connect_entry(), bits, error);
    }
    void refused(Outcome out, Reason reason) {
        check(out.kind == Kind::Rejected && out.reason == reason);
        check(scope.expired() && !scope.ownership_active());
        check(send(Operation::Connect, Phase::After, connect_entry()).kind == Kind::None);
    }
};
void no_association(const Outcome& out) {
    check(!out.continuous_connection_owned && !out.native_cross_process_namespace &&
          !out.source_frame_associated && !out.gpu_consumed_coherent);
}
} // namespace

int main() {
    {
        Fixture f;
        const auto out = f.connected();
        check(out.kind == Kind::NativeConnectCall && out.reason == Reason::None);
        check(out.argument_fd == 7 && out.native_result == 0 && out.result_bits == 0);
        check(out.entry.sp == sp && out.entry.x == connect_entry().x);
        no_association(out); check(f.scope.ownership_active());
        check(f.send(Operation::Unlock, Phase::Before, unlock_entry()).kind == Kind::None);
        check(!f.scope.ownership_active()); // Ownership ends before forwarding.
        check(f.send(Operation::Unlock, Phase::After, unlock_entry()).kind == Kind::None);
        check(!f.scope.expired());
        check(f.connected().kind == Kind::NativeConnectCall); // New supported invocation.
    }
    {
        Fixture f;
        errno = E2BIG;
        const auto out = f.connected(0xffffffff, ECONNREFUSED);
        check(errno == E2BIG); // Consumer does not alter process errno.
        check(out.kind == Kind::NativeConnectCall && out.native_result == -1);
        check(out.result_bits == 0xffffffff && out.preserved_errno == ECONNREFUSED);
        no_association(out);
        check(f.send(Operation::Unlock, Phase::Before, unlock_entry(false)).kind == Kind::None);
        check(!f.scope.ownership_active());
        check(f.send(Operation::Unlock, Phase::After, unlock_entry(false)).kind == Kind::None);
    }
    {
        Fixture f; Scope unprepared;
        const auto out = f.gate.observe(note(Operation::Lock, Phase::Before, lock_entry()), unprepared);
        check(out.kind == Kind::Rejected && out.reason == Reason::UnpreparedScope);
        no_association(out);
    }
    for (unsigned field = 0; field < 10; ++field) {
        auto t = target();
        switch (field) {
        case 0: t.module.load_bias += 16; break;
        case 1: t.module.code_begin = bias + 0x245eac; break;
        case 2: t.module.code_end = bias + 0x246044; break;
        case 3: t.module.module_lifetime_label = 0; break;
        case 4: t.module.code_identity.fill(0); break;
        case 5: t.object.pipe += 4096; break;
        case 6: t.object.registry_identity = 0; break;
        case 7: t.object.object_generation = 0; break;
        case 8: t.object.process_generation = 0; break;
        case 9: t.object.domain = frame_import_pipe::Domain::Compositor045; break;
        }
        Fixture f(t); f.refused(f.send(Operation::Lock, Phase::Before, lock_entry()), Reason::Target);
    }
    {
        Fixture f; Outcome out;
        std::thread other([&] {
            out = f.gate.observe(note(Operation::Lock, Phase::Before, lock_entry()), f.scope);
            check(!f.scope.current() && !f.scope.ownership_active() && !f.scope.expired());
        });
        other.join();
        check(out.kind == Kind::Rejected && out.reason == Reason::WrongThread);
        // Wrong-thread rejection cannot race by mutating the owner's local TLS.
        check(!f.scope.expired()); no_association(out);
    }
    {
        Fixture f; Gate other(target());
        check(f.send(Operation::Lock, Phase::Before, lock_entry()).kind == Kind::None);
        f.refused(other.observe(note(Operation::Lock, Phase::After, lock_entry()), f.scope), Reason::Target);
    }
    {
        Fixture f; auto unrelated = lock_entry(); unrelated.x[0] += 16;
        check(f.send(Operation::Lock, Phase::Before, unrelated).kind == Kind::None);
        check(!f.scope.expired());
        f.refused(f.send(Operation::Lock, Phase::After, lock_entry()), Reason::Unsupported);
    }
    for (unsigned at = 0; at <= 31; ++at) {
        Fixture f; auto changed = lock_entry();
        check(f.send(Operation::Lock, Phase::Before, lock_entry()).kind == Kind::None);
        if (at == 31) changed.sp += 16; else changed.x[at] ^= 1;
        f.refused(f.send(Operation::Lock, Phase::After, changed), Reason::Invocation);
    }
    for (unsigned at = 0; at <= 31; ++at) {
        Fixture f; auto changed = connect_entry(); f.locked();
        check(f.send(Operation::Connect, Phase::Before, connect_entry()).kind == Kind::None);
        if (at == 31) changed.sp += 16; else changed.x[at] ^= 1;
        f.refused(f.send(Operation::Connect, Phase::After, changed), Reason::Invocation);
    }
    for (unsigned at = 0; at <= 31; ++at) {
        Fixture f; auto changed = unlock_entry(); check(f.connected().kind == Kind::NativeConnectCall);
        check(f.send(Operation::Unlock, Phase::Before, unlock_entry()).kind == Kind::None);
        if (at == 31) changed.sp += 16; else changed.x[at] ^= 1;
        f.refused(f.send(Operation::Unlock, Phase::After, changed), Reason::Invocation);
    }
    {
        Fixture f; check(f.send(Operation::Lock, Phase::Before, lock_entry()).kind == Kind::None);
        auto out = f.send(Operation::Lock, Phase::After, lock_entry(), EBUSY, ENOENT);
        check(out.native_result == EBUSY && out.preserved_errno == ENOENT);
        f.refused(out, Reason::NativeLockFailure);
    }
    {
        Fixture f; check(f.send(Operation::Lock, Phase::Before, lock_entry()).kind == Kind::None);
        f.refused(f.send(Operation::Lock, Phase::Before, lock_entry()), Reason::Invocation);
    }
    {
        Fixture f; f.locked(); auto inner = lock_entry(); inner.x[30] = bias + 0x244ec4;
        f.refused(f.send(Operation::Lock, Phase::Before, inner), Reason::Unsupported);
    }
    for (unsigned at = 0; at < 6; ++at) {
        Fixture f; f.locked(); auto e = connect_entry();
        switch (at) {
        case 0: e.x[0] = 0xffffffff; break;
        case 1: e.x[1] += 16; break;
        case 2: e.x[20] += 16; break;
        case 3: e.x[2] = 2; e.x[22] = 2; break;
        case 4: e.x[2] = 111; e.x[22] = 111; break;
        case 5: e.x[22] = 15; break;
        }
        f.refused(f.send(Operation::Connect, Phase::Before, e), Reason::Arguments);
    }
    for (unsigned at = 0; at < 4; ++at) {
        Fixture f; f.locked(); auto e = connect_entry();
        switch (at) {
        case 0: e.sp = e.x[29] = sp + 16; break;
        case 1: e.x[29] += 16; break;
        case 2: e.x[19] += 16; break;
        case 3: e.sp = e.x[29] = (std::uint64_t{1} << 48) - 16; break;
        }
        f.refused(f.send(Operation::Connect, Phase::Before, e), Reason::Invocation);
    }
    for (auto bits : {std::uint64_t{1}, std::uint64_t{0x100000000}}) {
        Fixture f; auto out = f.connected(bits);
        f.refused(out, Reason::ResultEncoding); no_association(out);
    }
    {
        Fixture f; f.locked(); auto wrong = connect_entry(); wrong.x[30] += 4;
        f.refused(f.send(Operation::Connect, Phase::Before, wrong), Reason::Unsupported);
    }
    {
        Fixture f; f.locked();
        f.refused(f.send(Operation::Connect, Phase::After, connect_entry()), Reason::Unsupported);
    }
    {
        Fixture f; check(f.connected().kind == Kind::NativeConnectCall);
        f.refused(f.send(Operation::Unlock, Phase::Before, unlock_entry(false)), Reason::Unsupported);
    }
    {
        Fixture f; check(f.connected(0xffffffff).kind == Kind::NativeConnectCall);
        f.refused(f.send(Operation::Unlock, Phase::Before, unlock_entry()), Reason::Unsupported);
    }
    {
        Fixture f; f.locked(); auto e = unlock_entry(); e.x[30] = bias + 0x246044;
        f.refused(f.send(Operation::Unlock, Phase::Before, e), Reason::Abandoned);
    }
    {
        Fixture f; f.locked(); auto e = unlock_entry(); e.x[30] = bias + 0x245ebc;
        f.refused(f.send(Operation::Unlock, Phase::Before, e), Reason::Unsupported);
    }
    {
        Fixture f; check(f.connected().kind == Kind::NativeConnectCall);
        check(f.send(Operation::Unlock, Phase::Before, unlock_entry()).kind == Kind::None);
        auto out = f.send(Operation::Unlock, Phase::After, unlock_entry(), EPERM);
        check(out.native_result == EPERM); f.refused(out, Reason::NativeUnlockFailure);
    }
    {
        Fixture f; f.locked();
        f.refused(f.send(Operation::Destroy, Phase::Before, lock_entry()), Reason::Destroyed);
    }
    {
        Fixture f; f.locked(); f.gate.expire(f.scope);
        check(f.scope.expired() && !f.scope.ownership_active());
        check(f.send(Operation::Connect, Phase::Before, connect_entry()).kind == Kind::None);
    }
    std::printf("%u register-only callback-consumer checks passed; fixture input is not native startup, connection continuity, namespace, frame or GPU evidence\n", checks);
}
