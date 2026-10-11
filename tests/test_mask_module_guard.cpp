#include "layer/mask_module_guard.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static void require(bool condition, const char *description) {
    if (!condition) { std::cerr << description << '\n'; std::exit(1); }
}

static std::vector<uint32_t> module(unsigned opcode, const char *text) {
    const size_t bytes = std::strlen(text) + 1;
    const size_t words = (bytes + 3) / 4;
    std::vector<uint32_t> result{0x07230203, 0x00010300, 0, 1, 0};
    result.push_back(static_cast<uint32_t>(((words + 1) << 16) | opcode));
    result.resize(result.size() + words, 0);
    std::memcpy(result.data() + 6, text, bytes);
    return result;
}

int main(int argc, char **argv) {
    const auto accepts = [](const auto &words) {
        return mr::mask::legacyOverrideAllowed(words.data(), words.size() * 4);
    };
    require(accepts(module(330, "ordinary module")), "Legacy module was blocked");
    require(!accepts(module(330, "MRMASK/1 source-frame-binding-required")), "Explicit mask module was accepted without adapter");
    require(!accepts(module(330, "MRMASK/2 future version")), "Future mask module bypassed guard");
    require(accepts(module(5, "unrelated MRMASK/1 string")), "Unrelated debug text was blocked");
    require(!mr::mask::legacyOverrideAllowed(nullptr, 0), "Empty module accepted");
    auto malformed = module(330, "ordinary");
    malformed[5] = 330;
    require(!accepts(malformed), "Zero-word instruction accepted");
    malformed[5] = (100u << 16) | 330u;
    require(!accepts(malformed), "Out-of-range instruction accepted");
    malformed = module(330, "ordinary");
    for (size_t index = 6; index < malformed.size(); ++index) malformed[index] = 0x61616161;
    require(!accepts(malformed), "Nonterminated module marker accepted");
    const auto valid = module(330, "ordinary");
    require(!mr::mask::legacyOverrideAllowed(valid.data(), valid.size() * 4 - 1), "Misaligned module accepted");
    const auto matches = [&](const auto &replacement, const auto &original) {
        return mr::mask::legacyOverrideAllowed(replacement.data(), replacement.size() * 4,
                                               original.data(), original.size() * 4);
    };
    auto newBinding = valid;
    newBinding.insert(newBinding.end(), {(4u << 16) | 71u, 42, 34, 0, (4u << 16) | 71u, 42, 33, 31});
    require(!matches(newBinding, valid), "Unmarked added descriptor bypassed guard");
    require(matches(newBinding, newBinding), "Identical native descriptor bindings were blocked");
    auto renamed = newBinding;
    renamed[renamed.size() - 7] = renamed[renamed.size() - 3] = 99;
    require(matches(renamed, newBinding), "Equivalent descriptor identifiers were blocked");
    renamed.back() = 30;
    require(!matches(renamed, newBinding), "Changed descriptor binding was accepted");
    for (int index = 1; index < argc; index += 3) {
        require(index + 2 < argc, "Expected allow/deny, shader path and baseline triples");
        const auto load = [&](const char *path) {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            require(bool(input), "Cannot open generated shader");
            const auto size = input.tellg();
            require(size >= 20 && size <= 8 * 1024 * 1024, "Invalid generated shader size");
            std::vector<unsigned char> bytes(static_cast<size_t>(size));
            input.seekg(0);
            require(bool(input.read(reinterpret_cast<char *>(bytes.data()), size)), "Cannot read generated shader");
            return bytes;
        };
        const auto bytes = load(argv[index + 1]);
        const auto baseline = load(argv[index + 2]);
        const bool expected = std::string(argv[index]) == "allow";
        require(expected || std::string(argv[index]) == "deny", "Invalid guard expectation");
        require(mr::mask::legacyOverrideAllowed(bytes.data(), bytes.size(), baseline.data(), baseline.size()) == expected,
                "Actual generated shader bypassed legacy adapter guard or legacy shader was blocked");
    }
    std::cout << "PASS: legacy compositor rejects unbound explicit-mask modules\n";
}
