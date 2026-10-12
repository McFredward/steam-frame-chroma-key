#include "import_loader_reader.h"
#include <cerrno>
#include <cstring>
#include <limits>

namespace frame_import_loader {
namespace {
thread_local unsigned char owner_marker;
thread_local const ScopedModule* active_module = nullptr;
std::uintptr_t owner() noexcept { return reinterpret_cast<std::uintptr_t>(&owner_marker); }
bool add(std::uintptr_t base,std::uint64_t value,std::uintptr_t& out) noexcept {
    if(value>std::numeric_limits<std::uintptr_t>::max()-base) return false;
    out=base+static_cast<std::uintptr_t>(value); return true;
}
}
bool ScopedModule::copy(std::uintptr_t address,void* destination,std::size_t count) const noexcept {
    // Check callback capability before examining this scope's borrowed state.
    if(active_module!=this || !destination || count==0 || count>4096) return false;
    if(!active_) return false;
    std::uintptr_t end=0;
    if(!add(address,count,end)) return false;
    for(std::size_t i=0;i<read_.range_count;++i)
        if(address>=read_.ranges[i].begin && end<=read_.ranges[i].end) {
            std::memcpy(destination,reinterpret_cast<const void*>(address),count); return true;
        }
    return false;
}
bool ScopedModule::read(void* context,std::uintptr_t address,void* out,std::size_t count) noexcept {
    return static_cast<const ScopedModule*>(context)->copy(address,out,count);
}
namespace detail {
struct Context {
    const frame_import_module::Profile& profile;
    std::uintptr_t target,caller;
    std::uint64_t label;
    Visitor visitor;
    void* visitor_owner;
    bool verify;
    Result result{};
    static int visit(dl_phdr_info* info,std::size_t size,void* opaque) noexcept {
        auto& self=*static_cast<Context*>(opaque);
        if(++self.result.visited>256) { self.result.refusal=Refusal::EnumerationLimit; return 1; }
        constexpr auto minimum=offsetof(dl_phdr_info,dlpi_phnum)+sizeof(info->dlpi_phnum);
        if(!info || size<minimum) { self.result.refusal=Refusal::InvalidMetadata; return 1; }
        if(info->dlpi_addr!=self.target) return 0;
        if(!info->dlpi_phdr || !info->dlpi_phnum || info->dlpi_phnum>frame_import_module::kMaxSegments) {
            self.result.refusal=Refusal::UnsupportedLayout; return 1;
        }
        // glibc owns both this info and l_phdr while its load-write lock is held.
        // Copy the bounded PUBLIC PHDR metadata, not a pointer to inspect later.
        ScopedModule module;
        module.phnum_=info->dlpi_phnum;
        module.read_.image_base=self.target; module.read_.caller_pc=self.caller;
        module.read_.module_lifetime_label=self.label; module.read_.read=ScopedModule::read;
        module.read_.context=&module;
        std::uint64_t total=0;
        for(std::size_t i=0;i<module.phnum_;++i) {
            ElfW(Phdr) ph{};
            std::memcpy(&ph,info->dlpi_phdr+i,sizeof(ph));
            module.phdrs_[i]={ph.p_type,ph.p_flags,ph.p_offset,ph.p_vaddr,ph.p_paddr,
                             ph.p_filesz,ph.p_memsz,ph.p_align};
            if(ph.p_type!=PT_LOAD) continue;
            if(ph.p_memsz<ph.p_filesz || ph.p_vaddr>frame_import_module::kMaxModuleBytes ||
                ph.p_memsz>frame_import_module::kMaxModuleBytes-ph.p_vaddr ||
                !(ph.p_flags&PF_R) || (ph.p_flags&PF_X && ph.p_flags&PF_W)) {
                self.result.refusal=Refusal::UnsupportedLayout; return 1;
            }
            std::uintptr_t begin=0,end=0,mem_end=0;
            if(!add(self.target,ph.p_vaddr,begin) || !add(begin,ph.p_filesz,end) || !add(begin,ph.p_memsz,mem_end)) {
                self.result.refusal=Refusal::UnsupportedLayout; return 1;
            }
            // Only immutable readable, executable file bytes may be read back.
            // Mutable RW, BSS, gaps, padding and non-executable RO stay excluded.
            if(!(ph.p_flags&PF_X) || !ph.p_filesz) continue;
            if(ph.p_filesz>frame_import_module::kMaxModuleBytes-total) {
                self.result.refusal=Refusal::UnsupportedLayout; return 1;
            }
            total+=ph.p_filesz;
            for(std::size_t j=0;j<module.read_.range_count;++j)
                if(begin<module.read_.ranges[j].end && module.read_.ranges[j].begin<end) {
                    self.result.refusal=Refusal::UnsupportedLayout; return 1;
                }
            module.read_.ranges[module.read_.range_count++]={begin,end};
            if(self.caller>=begin && self.caller<end) { module.code_begin_=begin; module.code_end_=end; }
        }
        if(!module.code_begin_ || !module.read_.range_count) {
            self.result.refusal=Refusal::CallerOutsideCode; return 1;
        }
        if(self.verify) {
            if(module.phnum_!=self.profile.phnum) { self.result.refusal=Refusal::ModuleIdentity; return 1; }
            for(std::size_t i=0;i<module.phnum_;++i) {
                const auto& a=module.phdrs_[i]; const auto& b=self.profile.segments[i];
                if(a.type!=b.type || a.flags!=b.flags || a.file_offset!=b.file_offset ||
                    a.virtual_address!=b.virtual_address || a.physical_address!=b.physical_address ||
                    a.file_size!=b.file_size || a.memory_size!=b.memory_size || a.alignment!=b.alignment) {
                    self.result.refusal=Refusal::ModuleIdentity; return 1;
                }
            }
        }
        module.active_=true; active_module=&module;
        if(self.verify) {
            self.result.module=frame_import_module::verify_startup(self.profile,module.read_);
            self.result.refusal=self.result.module.valid() ? Refusal::None : Refusal::ModuleIdentity;
        } else {
            self.visitor(module,self.visitor_owner);
            self.result.refusal=Refusal::None;
        }
        active_module=nullptr; module.active_=false;
        return 1; // Stop while still in the same locked callback.
    }
};
} // namespace detail
Reader::Reader(const frame_import_module::Profile& profile,Iterate iterate,std::uint64_t label) noexcept
    :profile_(profile),iterate_(iterate),label_(label),owner_(owner()) {}
Result Reader::run(const Dl_info& info,std::uintptr_t caller,Visitor visitor,void* context,bool verify) noexcept {
    Result result;
    if(owner()!=owner_) { result.refusal=Refusal::WrongThread; return result; }
    if(!iterate_ || !label_ || (!verify && !visitor)) return result;
    if(active_module) { result.refusal=Refusal::Recursion; return result; }
    if(consumed_) { result.refusal=Refusal::Repeated; return result; }
    consumed_=true;
    const auto target=reinterpret_cast<std::uintptr_t>(info.dli_fbase);
    if(!target || !caller) { result.refusal=Refusal::InvalidTarget; return result; }
    detail::Context state{profile_,target,caller,label_,visitor,context,verify,{}};
    state.result.refusal=Refusal::NotFound;
    const int saved=errno;
    const auto native=iterate_(detail::Context::visit,&state);
    errno=saved;
    // One is our callback's bounded stop. A different native result invalidates
    // all copied evidence; never accept a false callback or silent loss.
    if(native!=1 || state.result.refusal==Refusal::NotFound) {
        if(native!=0 || state.result.refusal!=Refusal::NotFound) state.result.refusal=Refusal::NativeEnumeration;
        state.result.module={};
    }
    return state.result;
}
Result Reader::verify(const Dl_info& info,std::uintptr_t caller) noexcept { return run(info,caller,nullptr,nullptr,true); }
Refusal Reader::inspect(const Dl_info& info,std::uintptr_t caller,Visitor visitor,void* context) noexcept {
    return run(info,caller,visitor,context,false).refusal;
}
} // namespace frame_import_loader
