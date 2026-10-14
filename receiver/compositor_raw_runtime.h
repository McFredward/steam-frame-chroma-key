#pragma once

#include "../import/compositor_main_preparation.h"
#include "../import/compositor_startup_audit.h"
#include "../import/compositor_resource_raw.h"
#include "../import/compositor_journal_bridge.h"
#include <atomic>
#include <pthread.h>

namespace frame_compositor_runtime {
constexpr std::size_t kRecordLimit=32;
constexpr std::uint64_t kTag=0xc000000000000001ULL,kCookie=0xc100000000000001ULL;
// These labels are local diagnostic ownership, never resource/frame generations.
using Self=pthread_t (*)();
using Attributes=int (*)(pthread_t,pthread_attr_t*);
using GetStack=int (*)(const pthread_attr_t*,void**,std::size_t*);
using DestroyAttributes=int (*)(pthread_attr_t*);
struct Setup {
    std::uintptr_t lock_entry=0,unlock_entry=0,write_entry=0;
    Self self=nullptr;Attributes attributes=nullptr;
    pthread_t main_thread{}; // Cached through the authenticated provider at constructor startup.
    GetStack get_stack=nullptr;DestroyAttributes destroy_attributes=nullptr;
    std::uintptr_t self_data_begin=0,self_data_end=0;
    // Production operator-controlled captures reserve their only candidate
    // until an authenticated writer publishes the arm signal.
    bool require_operator_arm=false;
};
enum class Kind : unsigned { Prepared,Attached,Bound,BeginStaged,ThreadAdmitted,RawMatched,
    RawRefused,Receipt,Refused,Retired };
enum class Refusal : unsigned { None,Unprepared,Preparation,Setup,Registration,Audit,Thread,
    NativeSource,Raw,Queue,NotificationLoss,Journal,Finalized };
struct Record {
    Kind kind=Kind::Refused;
    std::uint64_t ordinal=0;
    Refusal refusal=Refusal::None;
    frame_compositor_audit::BoundProvider bindings{};
    frame_import_native::Entry entry{};
    frame_compositor_raw::RawRecord raw{};
    frame_compositor_journal::Receipt receipt{};
    unsigned thread_reason=0;
    bool forward_only=false;
};
struct Status {
    bool prepared=false,attached=false,retired=false,started=false,terminal=false;
    Refusal refusal=Refusal::Unprepared;
    std::uint64_t queued=0,drained=0;
    std::size_t parent_bytes=0,scalar_bytes=0,wire_bytes=0;
    bool candidate_arm_required=false,candidate_armed=false,candidate_claimed=false;
    std::uint64_t begins_before_arm=0,begins_after_arm=0,skipped_prearm_afters=0;
    bool begin_counts_overflow=false;
};
bool initialize(const frame_compositor_main::Prepared&,Setup) noexcept;
// Metadata publication only, after cold initialization and audit attachment.
// Single-use: duplicates, completed/retired captures and legacy automatic
// captures refuse without resetting any claim, stack ownership or read budget.
__attribute__((visibility("hidden"))) bool arm_candidate_once() noexcept;
frame_compositor_audit::BridgeResponse bridge(frame_compositor_audit::BridgeRequest) noexcept;
bool pop(Record&) noexcept;
Status status() noexcept;
void finalize() noexcept;
bool quiescent() noexcept;
bool association_current() noexcept;
} // namespace frame_compositor_runtime
