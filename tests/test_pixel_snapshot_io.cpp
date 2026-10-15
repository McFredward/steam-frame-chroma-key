// Cold diagnostic filesystem controls; no headset/user configuration is touched.
#include "layer/pixel_snapshot_io.h"
#include "layer/passive_binding_trace.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
unsigned checks=0;
void require(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
void file(const fs::path& path,const std::string& bytes){
    const int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
    require(fd>=0,"fixture file creation");
    const bool good=frame_pixel_io::writeAll(fd,bytes.data(),bytes.size());close(fd);require(good,"fixture complete write");
}
int main(){
    char pattern[]="/tmp/ghvr-pixel-io-XXXXXX";
    const auto* created=mkdtemp(pattern);require(created,"private temp");const fs::path root(created);
    try{
        const auto run=root/"run";fs::create_directory(run);chmod(run.c_str(),0700);
        const auto config=root/"session.conf";
        file(config,std::string(frame_pixel_io::ConfigMagic)+"0123456789abcdef0123456789abcdef\n"+run.string()+"\n");
        frame_pixel_io::Config good;require(frame_pixel_io::load(config.c_str(),good),"valid private config");
        require(!frame_pixel_io::armed(good),"not armed by default");
        file(run/"capture.arm",frame_pixel_io::ArmMagic);require(frame_pixel_io::armed(good),"exact owned arm");
        chmod((run/"capture.arm").c_str(),0644);require(!frame_pixel_io::armed(good),"public arm refused");
        chmod((run/"capture.arm").c_str(),0600);
        int output=frame_pixel_io::create(good,"snapshot.p010");require(output>=0,"exclusive owned output");close(output);
        require(frame_pixel_io::create(good,"snapshot.p010")<0,"never replace output");
        fs::create_symlink(config,root/"linked.conf");frame_pixel_io::Config a;
        require(!frame_pixel_io::load((root/"linked.conf").c_str(),a),"config symlink refused");
        fs::create_directory_symlink(root,root/"linked-parent");frame_pixel_io::Config b;
        require(!frame_pixel_io::load((root/"linked-parent/session.conf").c_str(),b),"parent config symlink refused");
        fs::create_hard_link(config,root/"hard.conf");frame_pixel_io::Config c;
        require(!frame_pixel_io::load(config.c_str(),c),"multiply linked config refused");fs::remove(root/"hard.conf");
        chmod(config.c_str(),0644);frame_pixel_io::Config d;require(!frame_pixel_io::load(config.c_str(),d),"public config refused");
        chmod(config.c_str(),0600);chmod(run.c_str(),0755);frame_pixel_io::Config e;
        require(!frame_pixel_io::load(config.c_str(),e),"nonprivate output directory refused");chmod(run.c_str(),0700);
        fs::rename(run,root/"retained-run");
        output=frame_pixel_io::create(good,"after-rename");require(output>=0,"output stays on pinned directory");close(output);
        require(fs::is_regular_file(root/"retained-run/after-rename"),"no path retarget after rename");
        require(frame_pixel_io::digest("abc",3)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","known SHA256");
        auto core=std::make_unique<frame_bindings::Core>();
        const auto before=core->lostState();core->bindImageMemory2(1,0,nullptr);core->bindBufferMemory2(1,0,nullptr);
        require(core->lostState()==before,"zero native batch observes nothing");
        core->bindImageMemory2(1,33,reinterpret_cast<const VkBindImageMemoryInfo*>(1));
        core->bindBufferMemory2(1,33,reinterpret_cast<const VkBindBufferMemoryInfo*>(1));
        require(core->lossCount(frame_bindings::Reason::Bounds)==2,"oversized batch refuses before pointer access");
        std::cout<<"PASS pixel IO and batch boundary controls: "<<checks<<"\n";
    }catch(const std::exception& error){fs::remove_all(root);std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
    fs::remove_all(root);return 0;
}
