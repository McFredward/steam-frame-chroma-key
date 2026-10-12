#include "receiver/client_startup_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <type_traits>

// Production Ready cannot be faked for a host test. Positive native-window,
// loader, forwarder and sealed cold-preparation evidence lives in their focused
// suites. This fixture exercises readiness/lifecycle refusals of the actual glue;
// it does not introduce a fixture Role or mutable activation flag into it.
namespace runtime=frame_client_startup;
namespace mainprep=frame_import_main;
namespace audit=frame_import_startup_audit;
static_assert(!std::is_default_constructible<mainprep::Prepared>::value,
              "A caller must not fabricate production Ready");
static_assert(!std::is_constructible<mainprep::Prepared,mainprep::Proof>::value,
              "A generic fixture proof must not authorize the runtime");
static_assert(!std::is_convertible<mainprep::Image,mainprep::Prepared>::value,
              "Scalar image data must not become a production capability");
namespace {
unsigned checks=0;
void check(bool value,const char* message) {
    ++checks;if(!value){std::fprintf(stderr,"%u: %s\n",checks,message);std::abort();}
}
void inert(audit::BridgeRequest request) {
    const auto response=runtime::bridge(request);
    check(!response.ready && response.hidden_entry==0,"Inert runtime exposed an entry address");
}
}
int main() {
    runtime::Record record;
    const auto empty=runtime::status();
    check(!empty.prepared && !empty.audit_attached && !empty.gate_constructed &&
          !empty.startup_completed && !empty.retired && !empty.finalized,"Runtime activated automatically");
    check(!empty.connect_sample_completed && !empty.resource_sample_completed,"Empty glue invented completed native samples");
    check(!empty.resource_thread_stack_prepared,"Empty glue invented decoder stack ownership");
    check(!runtime::registry() && !runtime::pop(record),"Unprepared runtime invented a registry/record");
    check(!frame_import_forwarding::callbacks_registered() &&
          !frame_import_startup_forwarding::providers_prepared(),"Native subscribers activated automatically");
    audit::BridgeRequest request{};
    inert(request);request.missed_before_preinit=true;inert(request);
    request={};request.stage=audit::Stage::Bind;request.provider.function=audit::Function::Init;
    request.provider.original=0x123450;inert(request);
    request.provider.function=audit::Function::Shutdown;inert(request);
    request.provider.function=audit::Function::MutexInit;
    request.provider.libc={0x1000,0x2000,0x3000,71};
    request.provider.client={0x4000,0x4000,0x60699a,72};inert(request);
    request.provider.function=audit::Function::MutexDestroy;inert(request);
    request.provider.function=audit::Function::MutexLock;inert(request);
    request.provider.function=audit::Function::MutexUnlock;inert(request);
    request.provider.function=audit::Function::Connect;inert(request);
    request.provider.function=audit::Function::EpollWait;inert(request);
    request.stage=audit::Stage::Retire;request.retired_module={0x1000,0x2000,0x3000,55};inert(request);
    request.stage=audit::Stage::Loss;inert(request);

    // A generic owned fixture fact can be valid for its separate reader while
    // still having no conversion/constructor route into production preparation.
    mainprep::Proof fixture{};fixture.refusal=mainprep::Refusal::None;
    fixture.image.role=mainprep::Role::Fixture;check(fixture.valid(),"Generic fixture fact unexpectedly invalid");
    mainprep::Profiles fixture_profiles{};
    for(auto& p:fixture_profiles.modules)p.role=static_cast<unsigned>(mainprep::Role::Fixture);
    const auto denied=mainprep::prepare(fixture_profiles);
    check(!denied.ready() && !denied.lookup() && !denied.iterate() &&
          !denied.native_init() && !denied.native_shutdown(),"Fixture role created production providers");
    const auto copied=denied;check(!copied.ready(),"Copy transformed a refused capability");
    frame_import_module::ProfileBlob missing_client{};
    runtime::Setup setup{};setup.process_generation=999;setup.local_verification_label=888;
    setup.hidden_init_entry=0x4000;setup.hidden_shutdown_entry=0x5000;
    setup.client.loader_name[0]='x';setup.client.rx_rva_end=0x60699a;
    check(!runtime::initialize_once(denied,missing_client,setup),"Fixture capability authorized actual glue");
    const auto refused=runtime::status();
    check(!refused.prepared && !refused.audit_attached && !refused.gate_constructed &&
          !refused.startup_completed && refused.refusal==runtime::Refusal::MainPreparation,
          "Refused initialization presented production Ready");
    check(!runtime::registry() && !frame_import_forwarding::callbacks_registered() &&
          !frame_import_startup_forwarding::providers_prepared(),"Refusal registered native hooks/providers");
    check(runtime::pop(record) && record.kind==runtime::Kind::Refused &&
          record.refusal==runtime::Refusal::MainPreparation,"Bounded refusal receipt missing");
    check(!record.object.valid() && !record.module.load_bias && record.verified_loader_name[0]==0 &&
          !record.actual_init_token && record.local_verification_label==0,
          "Refusal receipt invented native/module/epoch facts");
    check(!runtime::pop(record),"Refusal emitted duplicate receipts");
    check(!runtime::initialize_once(copied,missing_client,setup),"Once-only preparation was replaced");
    check(runtime::status().queued==1 && runtime::status().drained==1,"Failed repetition grew diagnostic records");
    request={};inert(request);request.stage=audit::Stage::Bind;inert(request);
    request.provider.function=audit::Function::MutexInit;inert(request);
    request.provider.function=audit::Function::MutexDestroy;inert(request);
    request.stage=audit::Stage::Retire;inert(request);
    runtime::finalize();const auto closed=runtime::status();
    check(closed.finalized && closed.retired && !closed.prepared && !closed.gate_constructed,
          "Finalizer did not leave an inert capability");
    check(!closed.connect_sample_completed && !closed.resource_sample_completed,"Finalization invented native sample success");
    check(!closed.resource_thread_stack_prepared,"Finalization invented decoder stack ownership");
    const auto queued=closed.queued;
    request={};inert(request);request.stage=audit::Stage::Bind;inert(request);
    request.stage=audit::Stage::Retire;request.retired_module.observed_open_generation=77;inert(request);
    request.stage=audit::Stage::Loss;inert(request);
    runtime::finalize();
    check(runtime::status().queued==queued && !runtime::pop(record),"Late la_objclose used finalized context");
    check(!runtime::initialize_once(denied,missing_client,setup),"Finalized runtime reactivated");
    std::printf("%u client runtime readiness/lifecycle controls passed; production positive path requires actual sealed native preparation, no host Ready bypass\n",checks);
}
