#include "import_module_identity.h"
#include "../receiver/sha256.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace frame_import_module {
namespace {
constexpr Digest kProfileIdentity{{
    0x1d,0xa2,0x39,0x0f,0x9d,0x65,0xa5,0x27,0x1f,0x07,0x50,0xd9,0xd5,0x55,0x8e,0x90,
    0x71,0x6c,0x35,0x62,0x9f,0x2f,0x39,0x58,0x3b,0xa5,0xae,0xef,0x2d,0x45,0x1b,0x71}};
constexpr std::uint64_t kCallerRva = 0x244dbc;
std::uint64_t little(const std::uint8_t* bytes, std::size_t count) noexcept {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) value |= std::uint64_t(bytes[i]) << (8 * i);
    return value;
}
void hash_scalar(frame_receiver::Sha256& hash, std::uint64_t value) noexcept {
    std::uint8_t bytes[8]{};
    for (unsigned i = 0; i < 8; ++i) bytes[i] = std::uint8_t(value >> (8 * i));
    hash.update(bytes, sizeof(bytes));
}
Digest profile_identity(const Profile& p) noexcept {
    frame_receiver::Sha256 hash;
    const std::uint64_t scalars[]{p.schema,p.file_size,p.phoff,p.phnum,p.build_note_rva,
                                p.build_note_size,p.build_id_size,p.caller_rva,p.machine};
    for (const auto value : scalars) hash_scalar(hash, value);
    for (const auto* digest : {&p.file_digest,&p.header_digest,&p.phdr_digest,&p.build_note_digest,&p.code_digest})
        hash.update(digest->data(), digest->size());
    hash.update(p.build_id.data(), p.build_id.size());
    for (const auto& ph : p.segments) {
        const std::uint64_t fields[]{ph.type,ph.flags,ph.file_offset,ph.virtual_address,
            ph.physical_address,ph.file_size,ph.memory_size,ph.alignment};
        for (const auto value : fields) hash_scalar(hash, value);
    }
    for (const auto& digest : p.rx_digests) hash.update(digest.data(), digest.size());
    return hash.finish();
}
bool add(std::uintptr_t base, std::uint64_t offset, std::uintptr_t& address) noexcept {
    if (offset > std::numeric_limits<std::uintptr_t>::max() - base) return false;
    address = base + static_cast<std::uintptr_t>(offset);
    return true;
}
bool file_span(const Profile& p, std::uint64_t rva, std::size_t count) noexcept {
    for (std::size_t i = 0; i < p.phnum; ++i) {
        const auto& ph = p.segments[i];
        if (ph.type == 1 && (ph.flags & 4) && rva >= ph.virtual_address &&
            rva - ph.virtual_address <= ph.file_size && count <= ph.file_size - (rva - ph.virtual_address))
            return true;
    }
    return false;
}
Refusal read_owned(const Profile& p, const ModuleRead& m, std::uint64_t rva,
                   void* destination, std::size_t count) noexcept {
    std::uintptr_t address = 0, end = 0;
    if (!add(m.image_base, rva, address) || !add(address, count, end)) return Refusal::AddressOverflow;
    if (!file_span(p, rva, count)) return Refusal::Unreadable;
    bool readable = false;
    for (std::size_t i = 0; i < m.range_count; ++i)
        if (m.ranges[i].begin <= address && end <= m.ranges[i].end) readable = true;
    if (!readable) return Refusal::Unreadable;
    return m.read(m.context, address, destination, count) ? Refusal::None : Refusal::ReadFailed;
}
Digest digest_bytes(const void* bytes, std::size_t size) noexcept {
    frame_receiver::Sha256 hash;
    hash.update(bytes, size);
    return hash.finish();
}
Result fail(Refusal why) noexcept { Result r; r.refusal = why; return r; }
bool match_header(const std::array<std::uint8_t,64>& h, const Profile& p) noexcept {
    return h[0]==0x7f && h[1]=='E' && h[2]=='L' && h[3]=='F' && h[4]==2 && h[5]==1 && h[6]==1 &&
        little(h.data()+16,2)==3 && little(h.data()+18,2)==183 && little(h.data()+20,4)==1 &&
        little(h.data()+32,8)==p.phoff && little(h.data()+52,2)==64 &&
        little(h.data()+54,2)==56 && little(h.data()+56,2)==p.phnum;
}
bool match_phdr(const std::uint8_t* bytes, const ProgramHeader& p) noexcept {
    return little(bytes,4)==p.type && little(bytes+4,4)==p.flags &&
        little(bytes+8,8)==p.file_offset && little(bytes+16,8)==p.virtual_address &&
        little(bytes+24,8)==p.physical_address && little(bytes+32,8)==p.file_size &&
        little(bytes+40,8)==p.memory_size && little(bytes+48,8)==p.alignment;
}
} // namespace

bool decode_profile(const ProfileBlob& blob, Profile& output) noexcept {
    Profile p{};
    std::size_t cursor = 0;
    auto scalar = [&]() noexcept { auto v = little(blob.data()+cursor,8); cursor += 8; return v; };
    p.schema=scalar(); p.file_size=scalar(); p.phoff=scalar(); p.phnum=scalar();
    p.build_note_rva=scalar(); p.build_note_size=scalar(); p.build_id_size=scalar();
    p.caller_rva=scalar(); p.machine=scalar();
    auto bytes = [&](auto& value) noexcept {
        std::memcpy(value.data(), blob.data()+cursor, value.size()); cursor += value.size();
    };
    bytes(p.file_digest); bytes(p.header_digest); bytes(p.phdr_digest); bytes(p.build_note_digest);
    bytes(p.code_digest); bytes(p.build_id);
    for (auto& ph : p.segments) {
        ph.type=scalar(); ph.flags=scalar(); ph.file_offset=scalar(); ph.virtual_address=scalar();
        ph.physical_address=scalar(); ph.file_size=scalar(); ph.memory_size=scalar(); ph.alignment=scalar();
    }
    for (auto& digest : p.rx_digests) bytes(digest);
    bytes(p.fingerprint);
    if (p.fingerprint != kProfileIdentity || profile_identity(p) != kProfileIdentity) return false;
    output = p;
    return cursor == blob.size();
}

Result verify_startup(const Profile& p, const ModuleRead& m) noexcept {
    if (p.fingerprint != kProfileIdentity || profile_identity(p) != kProfileIdentity)
        return fail(Refusal::ProfileIdentity);
    if (p.schema!=1 || p.machine!=183 || p.file_size>kMaxModuleBytes || p.file_size<64 ||
        p.phnum==0 || p.phnum>kMaxSegments || p.phoff>p.file_size ||
        p.phnum*56>p.file_size-p.phoff || p.build_note_size!=36 || p.build_id_size!=20 || p.caller_rva!=kCallerRva)
        return fail(Refusal::UnsupportedProfile);
    if (!m.image_base || !m.module_lifetime_label || !m.read) return fail(Refusal::MissingLease);
    if (m.range_count==0 || m.range_count>kMaxSegments) return fail(Refusal::InvalidRanges);
    for (std::size_t i=0; i<m.range_count; ++i) {
        if (!m.ranges[i].begin || m.ranges[i].begin>=m.ranges[i].end) return fail(Refusal::InvalidRanges);
        for (std::size_t j=0; j<i; ++j)
            if (m.ranges[i].begin<m.ranges[j].end && m.ranges[j].begin<m.ranges[i].end)
                return fail(Refusal::InvalidRanges);
    }
    std::uintptr_t expected_pc=0, code_begin=0, code_end=0;
    if (!add(m.image_base,p.caller_rva,expected_pc)) return fail(Refusal::AddressOverflow);
    if (expected_pc!=m.caller_pc) return fail(Refusal::CallerMismatch);
    std::uint64_t total_rx=0;
    for (std::size_t i=0; i<p.phnum; ++i) {
        const auto& ph=p.segments[i];
        if (ph.file_offset>p.file_size || ph.file_size>p.file_size-ph.file_offset ||
            ph.memory_size<ph.file_size || ph.virtual_address>kMaxModuleBytes ||
            ph.memory_size>kMaxModuleBytes-ph.virtual_address)
            return fail(Refusal::UnsupportedProfile);
        std::uintptr_t mapped_end=0;
        if (!add(m.image_base,ph.virtual_address+ph.memory_size,mapped_end)) return fail(Refusal::AddressOverflow);
        if (ph.type==1 && (ph.flags&1)) {
            if (!(ph.flags&4) || (ph.flags&2) || ph.file_size>kMaxModuleBytes-total_rx)
                return fail(Refusal::UnsupportedProfile);
            total_rx+=ph.file_size;
            if (p.caller_rva>=ph.virtual_address && p.caller_rva-ph.virtual_address<ph.file_size) {
                if (!add(m.image_base,ph.virtual_address,code_begin) ||
                    !add(code_begin,ph.file_size,code_end)) return fail(Refusal::AddressOverflow);
            }
        }
    }
    if (!code_begin || !total_rx) return fail(Refusal::CallerMismatch);
    std::array<std::uint8_t,64> header{};
    auto why=read_owned(p,m,0,header.data(),header.size());
    if (why!=Refusal::None) return fail(why);
    if (digest_bytes(header.data(),header.size())!=p.header_digest) return fail(Refusal::HeaderIdentity);
    if (!match_header(header,p)) return fail(Refusal::HeaderLayout);
    std::array<std::uint8_t,kMaxSegments*56> phdrs{};
    const auto phsize=static_cast<std::size_t>(p.phnum*56);
    why=read_owned(p,m,p.phoff,phdrs.data(),phsize);
    if (why!=Refusal::None) return fail(why);
    if (digest_bytes(phdrs.data(),phsize)!=p.phdr_digest) return fail(Refusal::ProgramHeadersIdentity);
    for (std::size_t i=0;i<p.phnum;++i)
        if (!match_phdr(phdrs.data()+i*56,p.segments[i])) return fail(Refusal::ProgramHeadersLayout);
    std::array<std::uint8_t,36> note{};
    why=read_owned(p,m,p.build_note_rva,note.data(),note.size());
    if (why!=Refusal::None) return fail(why);
    if (digest_bytes(note.data(),note.size())!=p.build_note_digest || little(note.data(),4)!=4 ||
        little(note.data()+4,4)!=20 || little(note.data()+8,4)!=3 ||
        std::memcmp(note.data()+12,"GNU\0",4)!=0 || std::memcmp(note.data()+16,p.build_id.data(),20)!=0)
        return fail(Refusal::BuildIdentity);
    std::array<std::uint8_t,4096> chunk{};
    frame_receiver::Sha256 combined;
    for (std::size_t i=0;i<p.phnum;++i) {
        const auto& ph=p.segments[i];
        if (ph.type!=1 || !(ph.flags&1)) continue;
        frame_receiver::Sha256 segment;
        std::uint64_t cursor=0;
        while (cursor<ph.file_size) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(),ph.file_size-cursor));
            why=read_owned(p,m,ph.virtual_address+cursor,chunk.data(),count);
            if (why!=Refusal::None) return fail(why);
            segment.update(chunk.data(),count); combined.update(chunk.data(),count); cursor+=count;
        }
        if (segment.finish()!=p.rx_digests[i]) return fail(Refusal::CodeIdentity);
    }
    if (combined.finish()!=p.code_digest) return fail(Refusal::CodeIdentity);
    Result r; r.refusal=Refusal::None; r.load_bias=m.image_base; r.code_begin=code_begin; r.code_end=code_end;
    r.module_lifetime_label=m.module_lifetime_label; r.code_identity=p.code_digest;
    return r;
}
} // namespace frame_import_module
