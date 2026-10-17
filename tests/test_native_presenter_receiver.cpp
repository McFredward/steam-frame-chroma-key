// Reuse CI builders and spies, without invoking their inherited test suites.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main inherited_clone_controls_not_run
#include "test_pixel_ssbo_clone_contract.cpp"
#undef main
#pragma GCC diagnostic pop
#include "../layer/native_presenter_receiver.h"
#include "../layer/pixel_snapshot_layer.cpp"
#include <dirent.h>

namespace receiver_test {
namespace r=frame_native_presenter_receiver;
constexpr const char* Capture="0123456789abcdef0123456789abcdef";
struct Directory {
    std::string path; int fd=-1;
    Directory(){char name[]="/tmp/ghvr-presenter-fixture-XXXXXX";const auto* made=mkdtemp(name);require(made,"owned temporary directory");path=made;fd=open(path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);require(fd>=0,"private directory descriptor");}
    ~Directory(){if(fd>=0){auto* stream=fdopendir(dup(fd));if(stream){while(auto* entry=readdir(stream))if(std::strcmp(entry->d_name,".")&&std::strcmp(entry->d_name,".."))unlinkat(fd,entry->d_name,0);closedir(stream);}close(fd);}rmdir(path.c_str());}
    void file(const char* name,const void* data,std::size_t size,mode_t mode=0600){unlinkat(fd,name,0);const int out=openat(fd,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,mode);require(out>=0,"create owned fixture file");require(!fchmod(out,mode)&&frame_pixel_io::writeAll(out,data,size),"write exact owned bytes");close(out);}
    void marker(){file("synthetic-presenter.enable",r::EnableMagic,std::strlen(r::EnableMagic));}
    std::string read(const char* name){const int in=openat(fd,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);require(in>=0,"read owned receipt");char data[2048]{};const auto count=::read(in,data,sizeof(data));close(in);require(count>0&&count<static_cast<ssize_t>(sizeof(data)),"bounded status receipt");return {data,static_cast<std::size_t>(count)};}
};
std::string status(bool maximum=false){Directory d;r::Source source;r::Snapshot s;s.observedDevice=17;s.observedApi=VK_API_VERSION_1_1;s.activeDevices=1;
    if(maximum){s.revision=s.pipelineAttempts=s.pipelineReplacements=s.observedDevice=UINT64_MAX;s.observedApi=s.activeDevices=UINT32_MAX;s.primitiveReason=frame_native_presenter::Reason::UnexpectedHandle;s.definitionHistoryFailed=true;}
    require(r::writeStatusCold(d.fd,Capture,source,s,maximum?32:1,maximum),"actual producer writes bounded JSON");return d.read(r::StatusFile);}
void publicFiles(){
    {Directory d;r::Source s;require(!s.prepareCold(-1)&&s.reason()==r::SourceReason::Directory,"invalid directory refused");}
    {Directory d;fchmod(d.fd,0755);r::Source s;require(!s.prepareCold(d.fd)&&s.reason()==r::SourceReason::Directory,"shared directory refused");}
    {Directory d;r::Source s;require(!s.prepareCold(d.fd)&&s.reason()==r::SourceReason::Marker,"missing explicit opt-in stays off");d.marker();require(!s.prepareCold(d.fd)&&s.reason()==r::SourceReason::Marker,"failed preparation cannot resurrect");}
    for(unsigned variant=0;variant<6;++variant){Directory d;d.marker();
        if(variant==0)d.file("synthetic-presenter.enable","GHVR_SYNTHETIC_PRESENTER/2\n",25);
        if(variant==1)fchmodat(d.fd,"synthetic-presenter.enable",0644,0);
        if(variant==2){require(!renameat(d.fd,"synthetic-presenter.enable",d.fd,"elsewhere"),"move marker");require(!symlinkat("elsewhere",d.fd,"synthetic-presenter.enable"),"symlink marker");}
        if(variant==3)require(!linkat(d.fd,"synthetic-presenter.enable",d.fd,"second",0),"hardlinked marker");
        if(variant==4){unlinkat(d.fd,"synthetic-presenter.enable",0);require(!mkfifoat(d.fd,"synthetic-presenter.enable",0600),"FIFO marker");}
        if(variant==5)d.file("synthetic-presenter.enable",r::EnableMagic,std::strlen(r::EnableMagic)-1);
        r::Source s;require(!s.prepareCold(d.fd)&&s.reason()==r::SourceReason::Marker,"noncanonical marker safely refused");}
    for(unsigned variant=0;variant<5;++variant){Directory d;d.marker();std::array<unsigned char,frame_native_presenter::GuardedFragmentBytes> bad{};d.file("synthetic-presenter.spv",bad.data(),bad.size());
        if(variant==1)fchmodat(d.fd,"synthetic-presenter.spv",0644,0);
        if(variant==2)require(!linkat(d.fd,"synthetic-presenter.spv",d.fd,"second",0),"hardlinked artifact");
        if(variant==3){require(!renameat(d.fd,"synthetic-presenter.spv",d.fd,"elsewhere"),"move artifact");require(!symlinkat("elsewhere",d.fd,"synthetic-presenter.spv"),"artifact symlink");}
        if(variant==4)d.file("synthetic-presenter.spv",bad.data(),4);
        r::Source s;require(!s.prepareCold(d.fd)&&s.reason()==(variant==0?r::SourceReason::Pin:r::SourceReason::Module),"noncanonical artifact refused before use");}
    auto text=status();require(text.find("\"source_reason\":\"disabled\"")!=std::string::npos&&text.find("\"diagnostic_atomics_not_source_cohort\":true")!=std::string::npos&&text.find("\"passthrough_authorized\":false")!=std::string::npos,"status keeps diagnostic and authority boundaries");
    require(status(true).size()<1024,"maximum width status fits envelope");
    {Directory d;r::Source s;r::Snapshot snap;require(!r::writeStatusCold(d.fd,"short",s,snap,1,false)&&!r::writeStatusCold(d.fd,Capture,s,snap,0,false)&&!r::writeStatusCold(d.fd,Capture,s,snap,33,false),"invalid status identity/budget refused");
        d.file("victim","keep",4);require(!symlinkat("victim",d.fd,r::StatusFile),"malicious final symlink");require(!r::writeStatusCold(d.fd,Capture,s,snap,1,false)&&d.read("victim")=="keep","status never follows final symlink");unlinkat(d.fd,r::StatusFile,0);
        d.file("synthetic-presenter-status.tmp","busy",4);require(!r::writeStatusCold(d.fd,Capture,s,snap,1,false)&&d.read("synthetic-presenter-status.tmp")=="busy","status cannot overwrite foreign preexisting temporary file");}
    {r::Counters c;c.deviceCreated(object<VkDevice>(7),VK_API_VERSION_1_1);frame_native_presenter::Result good;good.replaced=true;c.attempted(good);frame_native_presenter::Result bad;bad.reason=frame_native_presenter::Reason::Graphics;c.attempted(bad);c.historyFailed();c.deviceDestroyed();c.deviceDestroyed();const auto s=c.snapshot();require(s.pipelineAttempts==2&&s.pipelineReplacements==1&&s.activeDevices==0&&s.observedDevice==7&&s.definitionHistoryFailed&&s.primitiveReason==frame_native_presenter::Reason::Graphics,"bounded counters preserve coarse cold lifecycle");}
}
} // namespace receiver_test
int main(int argc,char** argv){using namespace receiver_test;
    if(argc==2&&!std::strcmp(argv[1],"--presenter-status-json")){std::cout<<status();return 0;}
    require(argc==1,"public receiver fixture arguments");publicFiles();std::cout<<"{\"scope\":\"cold-presenter-receiver-public\",\"checks\":"<<checks<<",\"native_provider_verified\":false}\n";
}
