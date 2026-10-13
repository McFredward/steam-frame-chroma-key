#include "../import/compositor_main_preparation.h"
#include "../receiver/sha256.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>

namespace {
using namespace frame_import_main;
unsigned checks = 0;
void expect(bool value, const char* label) { ++checks; if(!value) { std::cerr << label << '\n'; std::exit(1); } }
ProfileBlob load(const std::string& path) {
    ProfileBlob blob{}; std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char*>(blob.data()), blob.size());
    expect(bool(file) && file.peek() == std::char_traits<char>::eof(), "canonical descriptor exact size"); return blob;
}
Digest identity(const ProfileBlob& blob) { Digest d{}; std::memcpy(d.data(), blob.data() + 2000, 32); return d; }
void rehash(ProfileBlob& blob) {
    frame_receiver::Sha256 h; h.update(blob.data(),2000); const auto d = h.finish();
    std::memcpy(blob.data() + 2000,d.data(),32);
}
void no_capabilities(const frame_compositor_main::Prepared& p) {
    expect(!p.ready() && !p.lookup() && !p.iterate() && !p.native_lock() && !p.native_unlock() && !p.native_write() &&
           !p.original_lock() && !p.original_unlock() && !p.original_write(), "refusal leaves no forwarding/discovery capability");
}
}
int main(int argc, char** argv) {
    using frame_compositor_main::Prepared;
    static_assert(!std::is_default_constructible_v<Prepared>);
    expect(pinned_identity(Role::Compositor) != Digest{} && pinned_identity(Role::Compositor) != pinned_identity(Role::Receiver),
           "compositor has distinct canonical hard pin");
    frame_compositor_main::Profiles invalid{}; errno = ERANGE;
    const auto refused = frame_compositor_main::prepare(invalid);
    expect(refused.refusal() == Refusal::InvalidProfile && errno == ERANGE, "invalid cold input preserves errno"); no_capabilities(refused);
    expect(::dlsym(RTLD_DEFAULT,"VR_InitInternal2") == nullptr && ::dlsym(RTLD_DEFAULT,"VRClientCoreFactory") == nullptr,
           "invalid compositor bootstrap does not require or force OpenVR");
    if(argc == 2) {
        frame_compositor_main::Profiles native{}; constexpr unsigned roles[]{7,3,4,5};
        for(unsigned i = 0; i < 4; ++i) {
            const auto blob = load(std::string(argv[1]) + "/module-" + std::to_string(roles[i]) + ".bin");
            const auto review = identity(blob);
            expect(decode_profile(blob,i == 3 ? review : Digest{},native.modules[i]), "actual canonical native/composite observer decoded");
            expect(native.modules[i].role == roles[i], "compositor role order fixed");
            if(i == 3) { native.reviewed_observer_identity = review; continue; }
            for(unsigned byte = 0; byte < blob.size(); ++byte) {
                auto changed = blob; changed[byte] ^= 1; Profile p{};
                expect(!decode_profile(changed,identity(changed),p), "each canonical native descriptor byte mutation refused");
            }
            for(unsigned byte : {16u,80u,144u,176u,272u,1808u}) {
                auto changed = blob; changed[byte] ^= 1; rehash(changed); Profile p{};
                expect(!decode_profile(changed,identity(changed),p), "rehashing native alteration cannot replace hard pin");
            }
        }
        errno = EDOM; const auto wrong = frame_compositor_main::prepare(native);
        expect(wrong.refusal() == Refusal::WrongExecutable && errno == EDOM, "canonical metadata does not authorize unrelated executable");
        no_capabilities(wrong);
        auto other = native; other.modules[0].role = 1;
        expect(frame_compositor_main::prepare(other).refusal() == Refusal::InvalidProfile, "receiver cannot populate compositor bootstrap");
        other = native; other.modules[0].role = 6;
        expect(frame_compositor_main::prepare(other).refusal() == Refusal::InvalidProfile, "fixture cannot populate compositor bootstrap");
        other = native; other.reviewed_observer_identity = {};
        expect(frame_compositor_main::prepare(other).refusal() == Refusal::InvalidProfile, "self-stored observer fingerprint insufficient");
        expect(::dlsym(RTLD_DEFAULT,"VR_InitInternal2") == nullptr && ::dlsym(RTLD_DEFAULT,"VRClientCoreFactory") == nullptr,
               "wrong executable preparation neither forces API/client nor invokes factory");
    } else expect(argc == 1,"optional private canonical descriptor directory only");
    std::cout << "compositor bootstrap: " << checks << " checks; host/canonical descriptors do not establish hardware admission\n";
}
