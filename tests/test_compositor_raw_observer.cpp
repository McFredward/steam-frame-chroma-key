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

extern "C" void frame_import_lock_entry() {}
extern "C" void frame_import_unlock_entry() {}
extern "C" void frame_import_write_entry() {}
namespace writer_stubs {
std::array<frame_compositor_runtime::Record,4> records{};
std::size_t count=0,drained=0;
std::atomic<bool> current{true};
bool validate_result=true,retire_during_validate=false,finalized=false,child_retired=false;
unsigned validates=0;
}
namespace frame_compositor_runtime {
bool initialize(const frame_compositor_main::Prepared&,Setup) noexcept {return false;}
frame_compositor_audit::BridgeResponse bridge(frame_compositor_audit::BridgeRequest) noexcept {return {};}
bool pop(Record& r) noexcept {
    if(writer_stubs::drained>=writer_stubs::count)return false;
    r=writer_stubs::records[writer_stubs::drained++];return true;
}
Status status() noexcept {
    return {false,false,writer_stubs::finalized,true,writer_stubs::count==2,Refusal::Finalized,
        writer_stubs::count,writer_stubs::drained,16,364,21};
}
void finalize() noexcept {writer_stubs::finalized=true;writer_stubs::current=false;}
bool quiescent() noexcept {return true;}
bool association_current() noexcept {return writer_stubs::current.load();}
}
namespace frame_compositor_journal {
bool initialize_before_main() noexcept {return false;}
bool validate(const Receipt&) noexcept {
    ++writer_stubs::validates;
    if(writer_stubs::retire_during_validate)writer_stubs::current=false;
    return writer_stubs::validate_result;
}
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
    r.image.format=r.view.format=VK_FORMAT_R8G8B8A8_UNORM;r.image.extent={1152,4608,1};r.image.arrayLayers=1;return value;
}
void join_controls() {
    auto raw=raw_fixture();auto receipt=receipt_fixture();check(join(raw,receipt) && writer_stubs::validates==1);
    for(unsigned i=0;i<26;++i) {
        auto c=raw;auto r=receipt;
        switch(i) {
        case 0:c.matched=false;break;case 1:c.native_send_success=false;break;case 2:c.reply_token=0;break;
        case 3:r.current_after_drain=false;break;case 4:r.resource.resourceComplete=false;break;
        case 5:r.resource.errors=1;break;case 6:r.resource.lossCount=1;break;
        case 7:r.resource.nativeImportAccepted=true;break;case 8:r.resource.sourceFrameAssociated=true;break;
        case 9:r.resource.nativeCrossProcessNamespace=true;break;case 10:r.resource.acceptedSubmit=true;break;case 11:r.resource.gpuConsumedCoherent=true;break;
        case 12:++r.resource.invocationTag;break;case 13:++r.resource.cookie;break;
        case 14:++r.resource.imageInvocationTag;break;case 15:++r.resource.viewInvocationTag;break;
        case 16:++r.resource.device;break;case 17:++r.resource.requestedImage;break;case 18:++r.resource.requestedView;break;
        case 19:++r.resource.image.identity.handle;break;case 20:++r.resource.view.identity.handle;break;
        case 21:++r.resource.memory.identity.generation;break;case 22:++r.resource.image.memoryOffset;break;
        case 23:r.resource.view.format=VK_FORMAT_UNDEFINED;break;case 24:++r.resource.image.extent.width;break;
        case 25:++r.resource.image.arrayLayers;break;
        }
        check(!join(c,r));
    }
    writer_stubs::validate_result=false;check(!join(raw,receipt));writer_stubs::validate_result=true;
    writer_stubs::current=false;check(!join(raw,receipt));writer_stubs::current=true;
    writer_stubs::retire_during_validate=true;check(!join(raw,receipt));writer_stubs::retire_during_validate=false;writer_stubs::current=true;
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
    check(std::string(second.bytes.data(),second.used).find("\"image_generation\":1")!=std::string::npos);
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
    check(write_background(nullptr)==nullptr && output_fd==-1 && writer_stubs::finalized);
    const auto written=read_file(name);check(written.size()<=OutputBytes && std::count(written.begin(),written.end(),'\n')==4);
    check(written.find("\"local_cpu_resource_join\":true")!=std::string::npos && written.find("\"quiescent\":true")!=std::string::npos);
    check(written.find("\"incomplete\":false")!=std::string::npos);
    check(!unlink(name.c_str()) && !rmdir(dir.c_str()));
}
void fork_controls() {
    writer_stubs::finalized=false;writer_stubs::child_retired=false;stopping=false;writer_started=true;errno=E2BIG;
    child_retire();check(writer_stubs::finalized && writer_stubs::child_retired && stopping && !writer_started && errno==E2BIG);
}
}
int main() {
    check(!writer_started && output_fd==-1); // Actual production constructor inert on host.
    launch_controls();join_controls();formatter_controls();output_controls();fork_controls();
    std::printf("compositor observer: %u pure/writer controls passed; transport stubs are not owning-resource or hardware proof\n",checks);
}
