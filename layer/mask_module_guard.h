#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <utility>

namespace mr::mask {

// The legacy layer does not yet supply the explicit mask descriptor or decoded
// color-frame identity. A valid SPIR-V module alone does not make that safe:
// vkCreateShaderModule does not validate the eventual pipeline descriptor layout.
inline bool legacyOverrideAllowed(const void *data, std::size_t size) {
    if (!data || size < 20 || size % 4) return false;
    const auto *bytes = static_cast<const unsigned char *>(data);
    std::uint32_t magic = 0;
    std::memcpy(&magic, bytes, 4);
    if (magic != 0x07230203u) return false;
    for (std::size_t offset = 20; offset < size;) {
        std::uint32_t instruction = 0;
        std::memcpy(&instruction, bytes + offset, 4);
        const auto words = instruction >> 16;
        if (!words || words > (size - offset) / 4) return false;
        const std::size_t length = static_cast<std::size_t>(words) * 4;
        if ((instruction & 0xffffu) == 330u) { // OpModuleProcessed
            const auto *text = reinterpret_cast<const char *>(bytes + offset + 4);
            const auto textLength = length - 4;
            if (!std::memchr(text, '\0', textLength)) return false;
            constexpr char prefix[] = "MRMASK/";
            if (textLength >= sizeof(prefix) - 1 && !std::memcmp(text, prefix, sizeof(prefix) - 1)) return false;
        }
        offset += length;
    }
    return true;
}

// Also compare native descriptor bindings. This catches a mask module even if
// a packaging/optimization step stripped its debug marker. Changed descriptors
// need a pipeline-layout adapter, which this legacy layer does not supply.
inline bool descriptorBindings(const void *data, std::size_t size,
                               std::multiset<std::pair<std::uint32_t, std::uint32_t>> &out) {
    if (!data || size < 20 || size % 4) return false;
    const auto *bytes = static_cast<const unsigned char *>(data);
    const auto word = [&](std::size_t offset) {
        std::uint32_t value = 0;
        std::memcpy(&value, bytes + offset, 4);
        return value;
    };
    if (word(0) != 0x07230203u) return false;
    struct Decorations { std::uint32_t set{}, binding{}; bool hasSet{}, hasBinding{}; };
    std::map<std::uint32_t, Decorations> declarations;
    for (std::size_t offset = 20; offset < size;) {
        const auto instruction = word(offset);
        const auto words = instruction >> 16;
        const auto opcode = instruction & 0xffffu;
        if (!words || words > (size - offset) / 4) return false;
        // Grouped/ID decorations are not inferred by this deliberately narrow guard.
        if (opcode == 74 || opcode == 75 || opcode == 332) return false;
        if (opcode == 71 && words >= 3) { // OpDecorate
            const auto decoration = word(offset + 8);
            if (decoration == 33 || decoration == 34) { // Binding / DescriptorSet
                if (words != 4 || declarations.size() > 1024) return false;
                auto &declaration = declarations[word(offset + 4)];
                if (decoration == 33) {
                    if (declaration.hasBinding) return false;
                    declaration.binding = word(offset + 12);
                    declaration.hasBinding = true;
                } else {
                    if (declaration.hasSet) return false;
                    declaration.set = word(offset + 12);
                    declaration.hasSet = true;
                }
            }
        }
        offset += static_cast<std::size_t>(words) * 4;
    }
    for (const auto &pair : declarations) {
        if (!pair.second.hasSet || !pair.second.hasBinding) return false;
        out.emplace(pair.second.set, pair.second.binding);
    }
    return true;
}

inline bool legacyOverrideAllowed(const void *replacement, std::size_t replacementSize,
                                  const void *original, std::size_t originalSize) {
    if (!legacyOverrideAllowed(replacement, replacementSize)) return false;
    std::multiset<std::pair<std::uint32_t, std::uint32_t>> replacementBindings, originalBindings;
    return descriptorBindings(replacement, replacementSize, replacementBindings) &&
           descriptorBindings(original, originalSize, originalBindings) &&
           replacementBindings == originalBindings;
}

} // namespace mr::mask
