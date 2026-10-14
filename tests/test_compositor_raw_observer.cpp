// Production writer/formatter implementation, tested with explicit immutable
// CPU metadata and transport/currentness stubs. No fixture may manufacture a
// Ready production compositor preparation or claim real resource association.
#ifndef FRAME_COMPOSITOR_OBSERVER_SOURCE
#define FRAME_COMPOSITOR_OBSERVER_SOURCE "../receiver/compositor_raw_observer.cpp"
#endif
#include FRAME_COMPOSITOR_OBSERVER_SOURCE

#include <algorithm>
#include <fstream>
#include <string>
#include <thread>

extern "C" void frame_import_lock_entry() {}
extern "C" void frame_import_unlock_entry() {}
extern "C" void frame_import_write_entry() {}
namespace writer_stubs {
std::array<frame_compositor_runtime::Record,4> records{};
std::size_t count=0,drained=0;
std::atomic<bool> current{true};
bool validate_result=true,retire_during_validate=false,finalized=false,child_retired=false,stop_during_validate=false;
frame_bindings::ImportReceiptValidationReason refusal_reason=frame_bindings::ImportReceiptValidationReason::PendingReplay;
frame_compositor_journal::BridgeValidationReason bridge_override=frame_compositor_journal::BridgeValidationReason::None;
unsigned validates=0,initial_refusals=0;
std::uint64_t validate_delay_ns=0;
std::array<std::uint64_t,64> validation_times{};
const frame_compositor_journal::Receipt* first_receipt=nullptr;
bool immutable_receipt=true;
std::array<unsigned char,sizeof(frame_compositor_journal::Receipt)> first_receipt_bytes{};
std::atomic<bool> arm_required{false},armed{false};
std::atomic<unsigned> arm_calls{0};
frame_compositor_journal::Counters delivery_progress{};
}
namespace frame_compositor_runtime {
bool initialize(const frame_compositor_main::Prepared&,Setup) noexcept {return false;}
frame_compositor_audit::BridgeResponse bridge(frame_compositor_audit::BridgeRequest) noexcept {return {};}
bool pop(Record& r) noexcept {
    if(writer_stubs::arm_required && !writer_stubs::armed)return false;
    if(writer_stubs::drained>=writer_stubs::count)return false;
    r=writer_stubs::records[writer_stubs::drained++];return true;
}
Status status() noexcept {
    const bool active=!writer_stubs::arm_required || writer_stubs::armed;
    return {false,false,writer_stubs::finalized,active,active && writer_stubs::count==2,Refusal::Finalized,
        writer_stubs::count,writer_stubs::drained,active?16u:0u,active?364u:0u,active?21u:0u,
        writer_stubs::arm_required,writer_stubs::armed,writer_stubs::armed};
}
bool arm_candidate_once() noexcept {++writer_stubs::arm_calls;writer_stubs::armed=true;return true;}
void finalize() noexcept {writer_stubs::finalized=true;writer_stubs::current=false;}
bool quiescent() noexcept {return true;}
bool association_current() noexcept {return writer_stubs::current.load();}
}
namespace frame_compositor_journal {
bool initialize_before_main() noexcept {return false;}
Validation validate_detailed(const Receipt& receipt) noexcept {
    const auto count=writer_stubs::validates++;
    if(count<writer_stubs::validation_times.size())writer_stubs::validation_times[count]=now();
    if(!writer_stubs::first_receipt) {
        writer_stubs::first_receipt=&receipt;
        std::memcpy(writer_stubs::first_receipt_bytes.data(),&receipt,sizeof(receipt));
    } else if(writer_stubs::first_receipt!=&receipt || std::memcmp(writer_stubs::first_receipt_bytes.data(),&receipt,sizeof(receipt)))
        writer_stubs::immutable_receipt=false;
    if(writer_stubs::validate_delay_ns) {
        const auto delay=writer_stubs::validate_delay_ns;
        const timespec pause{static_cast<time_t>(delay/1000000000),static_cast<long>(delay%1000000000)};
        nanosleep(&pause,nullptr);
    }
    if(writer_stubs::retire_during_validate)writer_stubs::current=false;
    if(writer_stubs::stop_during_validate)stopping=true;
    Validation out;
    if(writer_stubs::bridge_override!=BridgeValidationReason::None) {
        out.bridge_reason=writer_stubs::bridge_override;return out;
    }
    out.valid=writer_stubs::validate_result && writer_stubs::validates>writer_stubs::initial_refusals;
    out.bridge_reason=out.valid?BridgeValidationReason::None:BridgeValidationReason::Journal;
    auto& journal=out.journal;
    journal.before_observed=true;
    journal.expected_before=journal.tickets_before=5+count;
    journal.empty_before=true;
    if(out.valid) {
        journal.valid=true;journal.reason=frame_bindings::ImportReceiptValidationReason::Valid;
        journal.after_observed=journal.core_checked=true;
        journal.expected_after=journal.tickets_after=journal.tickets_before;journal.empty_after=true;
        return out;
    }
    using Reason=frame_bindings::ImportReceiptValidationReason;
    journal.reason=writer_stubs::refusal_reason;
    switch(journal.reason) {
    case Reason::ActivePublication:journal.active_before=1;journal.transient=true;break;
    case Reason::PendingReplay:journal.empty_before=false;++journal.tickets_before;journal.transient=true;break;
    case Reason::PublicationChanged:
        journal.after_observed=journal.core_checked=true;
        journal.expected_after=journal.expected_before;journal.tickets_after=journal.tickets_before+1;
        journal.empty_after=false;journal.transient=true;break;
    case Reason::Closed:journal.closed_before=true;break;
    case Reason::Halted:journal.halted_before=true;break;
    case Reason::JournalLoss:journal.loss_before=1;break;
    case Reason::CoreGenerationMismatch:
        journal.after_observed=journal.core_checked=true;
        journal.expected_after=journal.tickets_after=journal.tickets_before;journal.empty_after=true;break;
    case Reason::Valid:break;
    }
    out.transient=journal.transient;return out;
}
Counters counters() noexcept {return writer_stubs::delivery_progress;}
bool validate(const Receipt& receipt) noexcept {return validate_detailed(receipt).valid;}
void retire_child_atomically() noexcept {writer_stubs::child_retired=true;}
}
namespace {
unsigned checks=0;
void check_at(bool v,unsigned line){++checks;if(!v){std::fprintf(stderr,"observer check %u failed at line %u\n",checks,line);std::abort();}}
#define check(v) check_at((v),__LINE__)
namespace launch_stubs {
unsigned calls=0,mask=LaunchProviderMask,frozen=1,abi=0;
int result=0;
int freeze(unsigned value) {abi=value;calls=calls*10+1;errno=EPERM;return result;}
unsigned providers() {calls=calls*10+2;errno=ENOENT;return mask;}
unsigned status() {calls=calls*10+3;errno=ESRCH;return frozen;}
void reset() {calls=abi=0;mask=LaunchProviderMask;frozen=1;result=0;}
}
void launch_controls() {
    // These pointer-only controls cannot manufacture a production launch or a
    // sealed native preparation. The actual linked weak imports stay absent.
    using namespace launch_stubs;
    reset();errno=E2BIG;
    check(freeze_before_observation(freeze,providers,status));
    check(calls==123 && abi==1 && errno==E2BIG);
    for(const int denied:{1,2,3,4,5,6,7,-1}) {
        reset();result=denied;errno=E2BIG;
        check(!freeze_before_observation(freeze,providers,status));
        check(calls==1 && abi==LaunchFreezeAbi && errno==E2BIG);
    }
    for(const unsigned incomplete:{0u,15u,31u,62u,127u}) {
        reset();mask=incomplete;errno=E2BIG;
        check(!freeze_before_observation(freeze,providers,status));
        check(calls==12 && errno==E2BIG);
    }
    for(const unsigned denied:{0u,2u,7u}) {
        reset();frozen=denied;errno=E2BIG;
        check(!freeze_before_observation(freeze,providers,status));
        check(calls==123 && errno==E2BIG);
    }
    for(unsigned missing=0;missing!=3;++missing) {
        reset();errno=E2BIG;
        check(!freeze_before_observation(missing==0?nullptr:freeze,
            missing==1?nullptr:providers,missing==2?nullptr:status));
        check(calls==0 && errno==E2BIG);
    }
    check(!frame_compositor_launch_freeze_environment_v1 &&
        !frame_compositor_launch_provider_mask && !frame_compositor_launch_frozen_status_v1);
    // Diagnostic-bearing environment alone cannot enter the cold constructor:
    // without the reviewed selector it creates neither output nor writers.
    check(!setenv("GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE","/invalid/raw-envelope",1));
    check(!setenv("GHVR_FRAME_COMPOSITOR_RAW_OUTPUT","/invalid/raw-output",1));
    const bool prior_finalized=writer_stubs::finalized;errno=E2BIG;start();
    check(errno==E2BIG && !writer_started && output_fd==-1 && owner_pid==0 &&
        frame_compositor_raw_ready.load()==0 && writer_stubs::finalized==prior_finalized);
    check(!std::strcmp(std::getenv("GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE"),"/invalid/raw-envelope"));
    check(!unsetenv("GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE") && !unsetenv("GHVR_FRAME_COMPOSITOR_RAW_OUTPUT"));
}
frame_compositor_raw::RawRecord raw_fixture() {
    frame_compositor_raw::RawRecord r{};r.raw_only=r.matched=r.terminal=r.native_send_success=true;
    r.reply_token=0x8877665544332211ULL;r.reply_bytes=21;r.scalar_bytes_copied=364;r.wire_bytes_copied=21;
    r.image.device=71;r.image.native_device=0xabc0;r.image.image=10;r.image.format=37;r.image.width=1152;r.image.height=4608;r.image.depth=1;r.image.layers=1;
    r.bind.memory=20;r.bind.offset=16;r.view.view=30;r.view.format=37;
    r.begin_lock_succeeded=r.early_unlock_succeeded=r.native_payload_read=r.image_succeeded=r.bind_succeeded=r.view_succeeded=true;
    r.send_lock_succeeded=r.reply_complete=r.finish_unlock_succeeded=true;
    r.last_boundary=frame_compositor_raw::Boundary::Finish;r.last_phase=frame_compositor_raw::Phase::After;
    r.last_native_result=0;r.last_native_result_available=true;return r;
}
frame_compositor_journal::Receipt receipt_fixture() {
    frame_compositor_journal::Receipt value{};auto& r=value.resource;
    value.current_after_drain=r.resourceComplete=true;
    r.journalInstance=31;r.localReplayOrder=5;r.resourceGenerationCursor=3;
    r.invocationTag=r.imageInvocationTag=r.viewInvocationTag=runtime::kTag;r.cookie=runtime::kCookie;
    r.device=71;r.requestedImage=10;r.requestedView=30;
    r.image.identity={10,1};r.memory.identity={20,2};r.view.identity={30,3};
    r.image.memory=r.memory.identity;r.view.image=r.image.identity;r.image.memoryOffset=16;
    r.image.format=r.view.format=VK_FORMAT_R8G8B8A8_UNORM;r.image.extent={1152,4608,1};r.image.arrayLayers=1;
    r.image.complete=r.view.complete=r.memory.complete=true;r.memory.allocationSize=8192;r.reason=frame_bindings::Reason::None;
    value.delivery_validation_attempts=1;auto& delivery=value.delivery_validation;
    delivery.valid=true;delivery.bridge_reason=frame_compositor_journal::BridgeValidationReason::None;
    delivery.journal.valid=true;delivery.journal.reason=frame_bindings::ImportReceiptValidationReason::Valid;
    delivery.journal.before_observed=delivery.journal.after_observed=delivery.journal.core_checked=true;
    delivery.journal.expected_before=delivery.journal.expected_after=delivery.journal.tickets_before=delivery.journal.tickets_after=5;
    delivery.journal.empty_before=delivery.journal.empty_after=true;return value;
}
void reset_validation() {
    writer_stubs::validates=writer_stubs::initial_refusals=0;writer_stubs::validate_delay_ns=0;
    writer_stubs::validate_result=true;writer_stubs::retire_during_validate=false;writer_stubs::stop_during_validate=false;
    writer_stubs::refusal_reason=frame_bindings::ImportReceiptValidationReason::PendingReplay;
    writer_stubs::bridge_override=frame_compositor_journal::BridgeValidationReason::None;
    writer_stubs::current=true;writer_stubs::first_receipt=nullptr;writer_stubs::immutable_receipt=true;
    writer_stubs::validation_times.fill(0);writer_stubs::delivery_progress={};
}
void join_controls() {
    reset_validation();
    auto raw=raw_fixture();auto receipt=receipt_fixture();check(join(raw,receipt) && writer_stubs::validates==1);
    for(unsigned i=0;i<31;++i) {
        auto c=raw;auto r=receipt;std::uint64_t expected=std::uint64_t(1)<<i;
        switch(i) {
        case 0:c.matched=false;break;case 1:c.native_send_success=false;break;case 2:c.reply_token=0;break;
        case 3:r.current_after_drain=false;break;case 4:r.resource.resourceComplete=false;break;
        case 5:r.resource.errors=1;break;case 6:r.resource.lossCount=1;break;
        case 7:r.resource.nativeImportAccepted=true;break;case 8:r.resource.sourceFrameAssociated=true;break;
        case 9:r.resource.nativeCrossProcessNamespace=true;break;case 10:r.resource.acceptedSubmit=true;break;case 11:r.resource.gpuConsumedCoherent=true;break;
        case 12:++r.resource.invocationTag;break;case 13:++r.resource.cookie;break;
        case 14:++r.resource.imageInvocationTag;break;case 15:++r.resource.viewInvocationTag;break;
        case 16:++r.resource.device;break;case 17:++r.resource.requestedImage;break;case 18:++r.resource.requestedView;break;
        case 19:++r.resource.image.identity.handle;expected|=join_bit(JoinPredicate::ViewImageIdentity);break;
        case 20:++r.resource.view.identity.handle;break;case 21:++r.resource.view.image.generation;break;
        case 22:++r.resource.memory.identity.handle;expected|=join_bit(JoinPredicate::ImageMemoryIdentity);break;
        case 23:++r.resource.memory.identity.generation;break;case 24:++r.resource.image.memoryOffset;break;
        case 25:r.resource.image.format=VK_FORMAT_UNDEFINED;break;case 26:r.resource.view.format=VK_FORMAT_UNDEFINED;break;
        case 27:++r.resource.image.extent.width;break;case 28:++r.resource.image.extent.height;break;
        case 29:++r.resource.image.extent.depth;break;case 30:++r.resource.image.arrayLayers;break;
        }
        JoinValidation diagnostics;
        check(compatibility(c,r)==expected && !join(c,r,&diagnostics));
        check(diagnostics.compatibility_checked && diagnostics.compatibility_reason_bits==expected &&
            diagnostics.end==JoinEnd::Incompatible && !diagnostics.attempts && !diagnostics.runtime_before_checked && !diagnostics.journal_checked);
    }
    check(writer_stubs::validates==1);
    writer_stubs::validate_result=false;check(!join(raw,receipt));writer_stubs::validate_result=true;
    const auto before=writer_stubs::validates;
    writer_stubs::current=false;check(!join(raw,receipt) && writer_stubs::validates==before);writer_stubs::current=true;
    writer_stubs::retire_during_validate=true;check(!join(raw,receipt));writer_stubs::retire_during_validate=false;writer_stubs::current=true;
    reset_validation();
}
void formatter_controls() {
    runtime::Record r{};r.kind=runtime::Kind::RawMatched;r.raw=raw_fixture();r.ordinal=2;
    r.raw.creation_route=frame_import_native::CreationRoute::Vulkan;
    r.entry.sp=0xabcdef00;r.entry.x[29]=0xabcdef20;r.entry.x[30]=0x98765400;
    Line l;record(l,r);check(l.valid && l.used<l.bytes.size() && l.bytes[l.used-1]=='\n');
    const std::string text(l.bytes.data(),l.used);
    for(const auto* name:{"\"mask_enabled\":false","\"source_frame_associated\":false","\"accepted_submit\":false","\"gpu_consumed\":false"})check(text.find(name)!=std::string::npos);
    check(text.find("\"reply_token\":\"8877665544332211\"")!=std::string::npos);
    for(const auto* field:{"\"entry_sp\":\"abcdef00\"", "\"entry_fp\":\"abcdef20\"", "\"entry_lr\":\"98765400\"",
        "\"last_boundary\":7", "\"last_phase\":1", "\"creation_route\":1", "\"last_native_result\":0", "\"last_native_result_available\":true"})
        check(text.find(field)!=std::string::npos);
    auto refused=r;refused.kind=runtime::Kind::RawRefused;refused.raw.matched=false;
    refused.raw.reason=frame_compositor_raw::InvalidEntry;refused.raw.last_boundary=frame_compositor_raw::Boundary::Image;
    refused.raw.creation_route=frame_import_native::CreationRoute::Facet;
    refused.raw.last_phase=frame_compositor_raw::Phase::Before;refused.raw.last_native_result=-1;
    refused.raw.last_native_result_available=false;Line refusal;record(refusal,refused);
    check(refusal.valid && refusal.used<refusal.bytes.size());
    const std::string denial(refusal.bytes.data(),refusal.used);
    for(const auto* field:{"\"reason\":128", "\"last_boundary\":2", "\"last_phase\":0", "\"creation_route\":2",
        "\"last_native_result\":-1", "\"last_native_result_available\":false", "\"entry_lr\":\"98765400\""})
        check(denial.find(field)!=std::string::npos);
    r.kind=runtime::Kind::Receipt;r.receipt=receipt_fixture();Line second;record(second,r);check(second.valid && second.used<second.bytes.size());
    const std::string receipt_text(second.bytes.data(),second.used);
    for(const auto* field:{"\"image_generation\":1","\"requested_image\":\"a\"","\"requested_view\":\"1e\"",
        "\"image_tag\":\"c000000000000001\"","\"view_tag\":\"c000000000000001\"",
        "\"image_memory\":{\"handle\":\"14\",\"generation\":2}","\"view_image\":{\"handle\":\"a\",\"generation\":1}",
        "\"memory_offset\":16","\"image_format\":37","\"view_format\":37","\"extent\":[1152,4608,1]","\"layers\":1",
        "\"native_import_accepted\":false","\"native_cross_process_namespace\":false","\"gpu_consumed_coherent\":false",
        "\"delivery_validation_attempts\":1","\"delivery_validation\":{\"checked\":true","\"core_checked\":true"})
        check(receipt_text.find(field)!=std::string::npos);
    JoinValidation validation;validation.compatibility_checked=true;validation.compatibility_reason_bits=std::uint64_t(1)<<30;
    validation.end=JoinEnd::Incompatible;Line diagnostics;diagnostics.add("{");join_diagnostics(diagnostics,validation);diagnostics.add("}\n");
    check(diagnostics.valid && diagnostics.used<FooterReserve);
    // A leading comma is expected because this formatter appends to a record.
    check(std::string(diagnostics.bytes.data(),diagnostics.used).find("\"compatibility_reason_bits\":1073741824")!=std::string::npos);
    Line overflow;const std::string huge(overflow.bytes.size(),'x');overflow.add("%s",huge.c_str());check(!overflow.valid);
    char out[32]{};check(!path("relative",out,sizeof(out)));check(path("/owned/path",out,sizeof(out)) && !std::strcmp(out,"/owned/path"));
    check(!path(nullptr,out,sizeof(out)));check(!path(huge.c_str(),out,sizeof(out)));
}
std::string read_file(const std::string& name) {std::ifstream f(name);check(bool(f));return std::string(std::istreambuf_iterator<char>(f),{});}
void output_controls() {
    char pattern[]="/tmp/frame-compositor-writer-XXXXXX";char* directory=mkdtemp(pattern);check(directory);
    const std::string dir(directory),name=dir+"/capture.jsonl";
    check(path(name.c_str(),output_path,sizeof(output_path)));owner_pid=getpid();
    output_fd=open_output();check(output_fd>=0);struct stat s{};check(!fstat(output_fd,&s) && (s.st_mode&07777)==0600 && s.st_nlink==1 && s.st_uid==geteuid());
    check(open_output()<0);Line line;line.add("{\"owned\":true}\n");std::size_t total=0;
    check(write_line(line,total) && total==line.used);
    const auto original=owner_pid;owner_pid=original+1;check(!write_line(line,total));owner_pid=original;
    Line too_large;too_large.add("%s",std::string(2048,'x').c_str());std::size_t near=OutputBytes-1500;
    check(!write_line(too_large,near));Line invalid;invalid.valid=false;check(!write_line(invalid,total));
    close(output_fd);output_fd=-1;check(read_file(name)==std::string(line.bytes.data(),line.used));check(!unlink(name.c_str()));
    check(!chmod(dir.c_str(),0755));check(open_output()<0);check(!chmod(dir.c_str(),0700));
    check(!symlink("target",name.c_str()));check(open_output()<0);check(!unlink(name.c_str()));
    output_fd=open_output();check(output_fd>=0);
    writer_stubs::records[0].kind=runtime::Kind::RawMatched;writer_stubs::records[0].raw=raw_fixture();writer_stubs::records[0].ordinal=0;
    writer_stubs::records[1].kind=runtime::Kind::Receipt;writer_stubs::records[1].receipt=receipt_fixture();writer_stubs::records[1].ordinal=1;
    writer_stubs::count=2;writer_stubs::drained=0;writer_stubs::finalized=false;writer_stubs::current=true;stopping=false;
    capture_started_ns=now();
    check(write_background(nullptr)==nullptr && output_fd==-1 && writer_stubs::finalized);
    const auto written=read_file(name);check(written.size()<=OutputBytes && std::count(written.begin(),written.end(),'\n')==4);
    check(written.find("\"local_cpu_resource_join\":true")!=std::string::npos && written.find("\"quiescent\":true")!=std::string::npos);
    check(written.find("\"incomplete\":false")!=std::string::npos);
    check(!unlink(name.c_str()) && !rmdir(dir.c_str()));
}
void revalidation_controls() {
    // Run the actual production owner loop with immutable receipt/transport
    // stubs. These controls cannot establish native resource/GPU association.
    char pattern[]="/tmp/frame-compositor-revalidation-XXXXXX";char* directory=mkdtemp(pattern);check(directory);
    const std::string dir(directory),name=dir+"/capture.jsonl";
    check(path(name.c_str(),output_path,sizeof(output_path)));owner_pid=getpid();
    const auto prepare=[&] {
        reset_validation();stopping=false;writer_stubs::finalized=false;writer_stubs::drained=0;
        writer_stubs::count=2;writer_stubs::arm_required=false;writer_stubs::armed=false;
        writer_stubs::records[0]={};writer_stubs::records[0].kind=runtime::Kind::RawMatched;writer_stubs::records[0].raw=raw_fixture();
        writer_stubs::records[1]={};writer_stubs::records[1].kind=runtime::Kind::Receipt;writer_stubs::records[1].receipt=receipt_fixture();
        writer_stubs::delivery_progress.requested=writer_stubs::delivery_progress.delivered=1;
        capture_started_ns=now();output_fd=open_output();check(output_fd>=0);
    };
    const auto run=[&] {
        const auto original_start=capture_started_ns;
        check(write_background(nullptr)==nullptr && output_fd==-1 && writer_stubs::finalized);
        check(capture_started_ns==original_start); // Retry never resets capture time.
        const auto text=read_file(name);
        check(text.size()<=OutputBytes && std::count(text.begin(),text.end(),'\n')<=4);
        check(writer_stubs::drained==2 && writer_stubs::immutable_receipt);
        check(!unlink(name.c_str()));return text;
    };
    std::string text;
    for(const auto reason:{frame_bindings::ImportReceiptValidationReason::ActivePublication,
            frame_bindings::ImportReceiptValidationReason::PendingReplay,frame_bindings::ImportReceiptValidationReason::PublicationChanged}) {
        prepare();writer_stubs::initial_refusals=3;writer_stubs::refusal_reason=reason;
        text=run();check(writer_stubs::validates==4);
    for(const auto* field:{"\"local_cpu_resource_join\":true","\"attempt_count\":4","\"journal_validation_refusals\":3",
        "\"transient_validation_refusals\":3","\"compatibility_reason_bits\":0","\"last_outcome\":\"joined\""})check(text.find(field)!=std::string::npos);
    for(unsigned i=1;i<writer_stubs::validates;++i)check(writer_stubs::validation_times[i]-writer_stubs::validation_times[i-1]>=JoinRetryDelayNs);
    }
    prepare();writer_stubs::initial_refusals=JoinAttemptLimit;
    text=run();check(writer_stubs::validates==JoinAttemptLimit);
    for(const auto* field:{"\"local_cpu_resource_join\":false","\"attempt_count\":64","\"journal_validation_refusals\":64",
        "\"transient_validation_refusals\":64","\"recovered_validation_refusals\":0","\"retry_limit_reached\":true","\"last_outcome\":\"journal_validation_limit\""})check(text.find(field)!=std::string::npos);
    for(const auto reason:{frame_bindings::ImportReceiptValidationReason::Closed,frame_bindings::ImportReceiptValidationReason::Halted,
            frame_bindings::ImportReceiptValidationReason::JournalLoss,frame_bindings::ImportReceiptValidationReason::CoreGenerationMismatch}) {
        prepare();writer_stubs::validate_result=false;writer_stubs::refusal_reason=reason;
        text=run();check(writer_stubs::validates==1);
        check(text.find("\"local_cpu_resource_join\":false")!=std::string::npos && text.find("\"transient_validation_refusals\":0")!=std::string::npos);
        check(text.find(std::string("\"journal_reason_name\":\"")+journal_reason(reason)+"\"")!=std::string::npos);
        check(text.find("\"retry_limit_reached\":false")!=std::string::npos);
    }
    for(const auto reason:{frame_compositor_journal::BridgeValidationReason::ChildRetired,frame_compositor_journal::BridgeValidationReason::Uninitialized,
            frame_compositor_journal::BridgeValidationReason::Stopped,frame_compositor_journal::BridgeValidationReason::Loss,
            frame_compositor_journal::BridgeValidationReason::ReceiptNotCurrent}) {
        prepare();writer_stubs::bridge_override=reason;
        text=run();check(writer_stubs::validates==1);
        check(text.find(std::string("\"bridge_reason_name\":\"")+bridge_reason(reason)+"\"")!=std::string::npos);
        check(text.find("\"before_checks_observed\":false")!=std::string::npos && text.find("\"local_cpu_resource_join\":false")!=std::string::npos);
    }
    prepare();writer_stubs::stop_during_validate=true;
    text=run();check(writer_stubs::validates==1 && text.find("\"last_outcome\":\"stopped\"")!=std::string::npos);
    prepare();writer_stubs::retire_during_validate=true;
    text=run();check(writer_stubs::validates==1 && !writer_stubs::current);
    check(text.find("\"local_cpu_resource_join\":false")!=std::string::npos && text.find("\"last_outcome\":\"runtime_currentness_after\"")!=std::string::npos);
    prepare();++writer_stubs::records[1].receipt.resource.image.extent.width;
    text=run();check(!writer_stubs::validates);
    check(text.find("\"compatibility_reason_bits\":134217728")!=std::string::npos && text.find("\"attempt_count\":0")!=std::string::npos);
    prepare();writer_stubs::initial_refusals=JoinAttemptLimit;capture_started_ns=now()-DurationNs+2000000;
    text=run();check(writer_stubs::validates>0 && writer_stubs::validates<JoinAttemptLimit);
    check(text.find("\"last_outcome\":\"capture_deadline\"")!=std::string::npos && text.find("\"deadline_reached\":true")!=std::string::npos);
    prepare();writer_stubs::validate_delay_ns=10000000;capture_started_ns=now()-DurationNs+5000000;
    text=run();check(writer_stubs::validates==1);
    check(text.find("\"local_cpu_resource_join\":false")!=std::string::npos && text.find("\"journal_current\":true")!=std::string::npos &&
        text.find("\"deadline_reached\":true")!=std::string::npos);
    prepare();capture_started_ns=now()-DurationNs;
    check(write_background(nullptr)==nullptr && !writer_stubs::validates);
    text=read_file(name);check(text.find("\"end\":\"deadline\"")!=std::string::npos && text.find("\"attempt_count\":0")!=std::string::npos);
    check(!unlink(name.c_str()));
    prepare();writer_stubs::count=1;writer_stubs::delivery_progress.delivered=0;
    writer_stubs::delivery_progress.transient_deferred=7;writer_stubs::delivery_progress.pending_waits=11;
    capture_started_ns=now()-DurationNs+2000000;
    check(write_background(nullptr)==nullptr && !writer_stubs::validates && writer_stubs::drained==1);
    text=read_file(name);
    for(const auto* field:{"\"initial_receipt_delivery\":{\"attempt_limit\":64,\"requested\":1,\"delivered\":0",
        "\"transient_deferred\":7","\"pending_waits\":11","\"compatibility_checked\":false"})check(text.find(field)!=std::string::npos);
    check(!unlink(name.c_str()));
    prepare();auto& withheld=writer_stubs::records[1].receipt;
    withheld.current_after_drain=false;withheld.delivery_validation_attempts=64;
    withheld.delivery_validation.valid=false;withheld.delivery_validation.transient=true;
    withheld.delivery_validation.bridge_reason=frame_compositor_journal::BridgeValidationReason::Journal;
    withheld.delivery_validation.journal.valid=false;withheld.delivery_validation.journal.transient=true;
    withheld.delivery_validation.journal.reason=frame_bindings::ImportReceiptValidationReason::PendingReplay;
    withheld.delivery_validation.journal.after_observed=withheld.delivery_validation.journal.core_checked=false;
    withheld.delivery_validation.journal.empty_before=false;++withheld.delivery_validation.journal.tickets_before;
    writer_stubs::delivery_progress.invalid_after_drain=writer_stubs::delivery_progress.delivery_exhausted=1;
    writer_stubs::delivery_progress.transient_deferred=63;
    text=run();check(!writer_stubs::validates);
    for(const auto* field:{"\"delivery_validation_attempts\":64","\"journal_reason_name\":\"pending_replay\"",
        "\"delivery_exhausted\":1","\"compatibility_reason_bits\":8"})check(text.find(field)!=std::string::npos);
    reset_validation();check(!rmdir(dir.c_str()));
}
void footer_reservation_controls() {
    runtime::Status state;
    state.started=state.terminal=state.candidate_arm_required=state.candidate_armed=state.candidate_claimed=true;
    state.stream_image_required=state.begin_counts_overflow=true;
    state.queued=state.drained=UINT64_MAX;
    state.parent_bytes=state.scalar_bytes=state.wire_bytes=SIZE_MAX;
    state.begins_before_arm=state.begins_after_arm=state.skipped_prearm_afters=UINT64_MAX;
    state.staged_begins=state.excluded_images=state.eligible_images=state.staging_refusals=state.abandoned_stages=UINT64_MAX;
    state.staging_threads=UINT32_MAX;
    JoinValidation validation;validation.compatibility_checked=true;validation.compatibility_reason_bits=UINT64_MAX;
    validation.first_ns=validation.finished_ns=UINT64_MAX;
    validation.attempts=validation.journal_refusals=validation.transient_refusals=UINT32_MAX;
    validation.runtime_before_checked=validation.runtime_current_before=validation.journal_checked=true;
    validation.runtime_after_checked=validation.runtime_current_after=true;
    validation.end=JoinEnd::AttemptLimit;
    auto& decision=validation.journal_validation;decision.bridge_reason=frame_compositor_journal::BridgeValidationReason::Journal;
    decision.journal.reason=frame_bindings::ImportReceiptValidationReason::CoreGenerationMismatch;
    auto& j=decision.journal;
    j.active_before=j.active_after=j.expected_before=j.expected_after=j.tickets_before=j.tickets_after=j.loss_before=j.loss_after=UINT64_MAX;
    // Conservative formatter maxima are intentionally not a valid lifecycle or
    // production preparation. They test byte reservation, never join admission.
    frame_compositor_journal::Counters progress;
    progress.requested=progress.delivered=progress.invalid_after_drain=progress.refused=UINT64_MAX;
    progress.transient_deferred=progress.delivery_exhausted=progress.pending_waits=UINT64_MAX;
    Line footer;footer_record(footer,state,validation,"write_or_byte_limit",UINT32_MAX,SIZE_MAX,false,false,progress);
    check(footer.valid && footer.used<footer.bytes.size());
    std::printf("conservative maximum footer: %zu bytes; reserved %zu\n",footer.used,FooterReserve);
    check(footer.used<=FooterReserve);
    char pattern[]="/tmp/frame-compositor-footer-XXXXXX";char* directory=mkdtemp(pattern);check(directory);
    const std::string dir(directory),name=dir+"/capture.jsonl";check(path(name.c_str(),output_path,sizeof(output_path)));
    owner_pid=getpid();output_fd=open_output();check(output_fd>=0);
    const auto filled=OutputBytes-FooterReserve;std::string prefix(filled,' ');
    check(write(output_fd,prefix.data(),prefix.size())==static_cast<ssize_t>(prefix.size()));
    std::size_t total=filled;check(write_line(footer,total,0) && total<=OutputBytes);
    check(!close(output_fd));output_fd=-1;check(read_file(name).size()==total);
    check(!unlink(name.c_str()) && !rmdir(dir.c_str()));
}
void marker_controls() {
    using namespace frame_compositor_arm;
    char pattern[]="/tmp/frame-compositor-marker-XXXXXX";char* directory=mkdtemp(pattern);check(directory);
    const std::string dir(directory),raw=dir+"/compositor-raw.jsonl",marker=dir+"/capture-arm.marker";
    char derived[1024]{};check(frame_compositor_arm::path(raw.c_str(),derived,sizeof(derived)) && derived==marker);
    for(const char* bad:{"relative/compositor-raw.jsonl","/compositor-raw.jsonl","/a//compositor-raw.jsonl",
            "/a/../compositor-raw.jsonl","/a/./compositor-raw.jsonl","/a/foreign.jsonl","/a b/compositor-raw.jsonl"})
        check(!frame_compositor_arm::path(bad,derived,sizeof(derived)));
    check(!frame_compositor_arm::path(raw.c_str(),derived,marker.size()));
    errno=E2BIG;check(frame_compositor_arm::read(marker.c_str())==Result::Missing && errno==E2BIG);
    const auto put=[&](const char* bytes,std::size_t length,mode_t mode) {
        const int fd=open(marker.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,mode);check(fd>=0);
        check(write(fd,bytes,length)==static_cast<ssize_t>(length));check(!close(fd));
    };
    put(Contents,sizeof(Contents)-1,0600);errno=E2BIG;
    check(frame_compositor_arm::read(marker.c_str())==Result::Valid && errno==E2BIG);
    check(!chmod(marker.c_str(),0644));check(frame_compositor_arm::read(marker.c_str())==Result::Invalid);
    check(!chmod(marker.c_str(),0600));const auto linked=dir+"/linked";
    check(!link(marker.c_str(),linked.c_str()));check(frame_compositor_arm::read(marker.c_str())==Result::Invalid);
    check(!unlink(linked.c_str()) && !unlink(marker.c_str()));
    put(Contents,sizeof(Contents),0600);check(frame_compositor_arm::read(marker.c_str())==Result::Invalid);check(!unlink(marker.c_str()));
    const std::string wrong(sizeof(Contents)-1,'x');put(wrong.data(),wrong.size(),0600);
    check(frame_compositor_arm::read(marker.c_str())==Result::Invalid);check(!unlink(marker.c_str()));
    check(!symlink("foreign",marker.c_str()));check(frame_compositor_arm::read(marker.c_str())==Result::Invalid);check(!unlink(marker.c_str()));
    check(!mkfifo(marker.c_str(),0600));check(frame_compositor_arm::read(marker.c_str())==Result::Invalid);check(!unlink(marker.c_str()));
    check(!chmod(dir.c_str(),0755));check(frame_compositor_arm::read(marker.c_str())==Result::Invalid);check(!chmod(dir.c_str(),0700));
    const auto alias=dir+"-alias";check(!symlink(dir.c_str(),alias.c_str()));
    check(frame_compositor_arm::read((alias+"/capture-arm.marker").c_str())==Result::Invalid);check(!unlink(alias.c_str()));

    // Exercise the production background loop and protected file. Transport
    // stubs below only assert sequencing, not native import/frame ownership.
    check(::path(raw.c_str(),output_path,sizeof(output_path)) && frame_compositor_arm::path(raw.c_str(),arm_path,sizeof(arm_path)));
    const auto run=[&](bool expired,bool invalid) {
        reset_validation();writer_stubs::count=2;
        writer_stubs::records[0]={};writer_stubs::records[0].kind=runtime::Kind::RawMatched;writer_stubs::records[0].raw=raw_fixture();
        writer_stubs::records[1]={};writer_stubs::records[1].kind=runtime::Kind::Receipt;writer_stubs::records[1].receipt=receipt_fixture();
        output_fd=open_output();check(output_fd>=0);owner_pid=getpid();capture_started_ns=now()-(expired?DurationNs:0);
        stopping=false;writer_stubs::finalized=false;writer_stubs::current=true;writer_stubs::drained=0;
        writer_stubs::arm_required=true;writer_stubs::armed=false;writer_stubs::arm_calls=0;
        if(invalid)put(wrong.data(),wrong.size(),0600);
        if(!expired && !invalid) {
            std::thread background([]{write_background(nullptr);});
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            check(writer_stubs::arm_calls==0 && !writer_stubs::armed);
            put(Contents,sizeof(Contents)-1,0600);background.join();
        } else write_background(nullptr);
        const auto text=read_file(raw);check(text.size()<=OutputBytes && output_fd==-1);
        if(!expired && !invalid) {
            check(writer_stubs::arm_calls==1 && text.find("\"operator_armed\":true")!=std::string::npos);
            check(text.find("\"local_cpu_resource_join\":true")!=std::string::npos);
            check(text.find("\"candidate_armed\":true")!=std::string::npos);
        } else {
            check(writer_stubs::arm_calls==0 && !writer_stubs::armed);
            check(text.find(expired?"\"end\":\"deadline\"":"\"end\":\"invalid_operator_marker\"")!=std::string::npos);
        }
        check(!unlink(raw.c_str()));if(!expired)check(!unlink(marker.c_str()));
    };
    run(false,false);run(false,true);run(true,false);
    writer_stubs::arm_required=false;writer_stubs::armed=false;
    check(!rmdir(dir.c_str()));
}
void fork_controls() {
    writer_stubs::finalized=false;writer_stubs::child_retired=false;stopping=false;writer_started=true;errno=E2BIG;
    child_retire();check(writer_stubs::finalized && writer_stubs::child_retired && stopping && !writer_started && errno==E2BIG);
}
}
int main(int argc,char** argv) {
    if(argc==2 && !std::strcmp(argv[1],"--footer-only")) {footer_reservation_controls();std::printf("compositor observer footer subset: %u controls passed\n",checks);return 0;}
    if(argc==2 && !std::strcmp(argv[1],"--marker-only")) {marker_controls();std::printf("compositor observer marker subset: %u controls passed\n",checks);return 0;}
    check(!writer_started && output_fd==-1); // Actual production constructor inert on host.
    launch_controls();join_controls();formatter_controls();output_controls();revalidation_controls();footer_reservation_controls();marker_controls();fork_controls();
    std::printf("compositor observer: %u pure/writer controls passed; transport stubs are not owning-resource or hardware proof\n",checks);
}
