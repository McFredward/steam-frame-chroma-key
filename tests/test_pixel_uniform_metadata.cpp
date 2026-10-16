// Synthetic CPU formatter/dispatch controls. No GPU, native provider or headset
// is touched. Optional --emit writes an original cross-language reader fixture.
#include "layer/pixel_uniform_metadata.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
unsigned checks = 0;
constexpr char Capture[] = "0123456789abcdef0123456789abcdef";
constexpr VkDeviceSize Tail = 28, Capacity = 65536, UniformBytes = 6544;
constexpr std::array<VkDeviceSize, 5> Ranges{{272, 2800, 400, 272, 2800}};
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class T> T h(std::uint64_t value) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
    else return static_cast<T>(value);
}
struct Fake {
    static Fake* active;
    unsigned resolutions = 0, barriers = 0, copies = 0, unexpected = 0;
    Fake() { active = this; }
    static VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags,
        VkDependencyFlags, std::uint32_t, const VkMemoryBarrier*, std::uint32_t,
        const VkBufferMemoryBarrier*, std::uint32_t, const VkImageMemoryBarrier*) { ++active->barriers; }
    static VKAPI_ATTR void VKAPI_CALL copy(VkCommandBuffer, VkBuffer, VkBuffer, std::uint32_t count,
                                           const VkBufferCopy* region) {
        ++active->copies;
        if (count != 1 || !region) ++active->unexpected;
    }
    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name) {
        ++active->resolutions;
        if (device != h<VkDevice>(1)) ++active->unexpected;
        if (!std::strcmp(name, "vkCmdPipelineBarrier")) return reinterpret_cast<PFN_vkVoidFunction>(barrier);
        if (!std::strcmp(name, "vkCmdCopyBuffer")) return reinterpret_cast<PFN_vkVoidFunction>(copy);
        ++active->unexpected; return nullptr;
    }
};
Fake* Fake::active = nullptr;

void word(std::uint8_t* data, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data[i] = static_cast<std::uint8_t>(value >> (i * 8));
}
void floating(std::uint8_t* data, float value) {
    std::uint32_t bits = 0; static_assert(sizeof(value) == sizeof(bits), "fixture needs32-bit float");
    std::memcpy(&bits, &value, sizeof(bits)); word(data, bits);
}
struct Fixture {
    Fake fake;
    frame_pixel_capture::Candidate candidate{};
    frame_uniform_copy::CopyRecorder recorder;
    frame_uniform_copy::Dispatch dispatch;
    frame_pixels::View view{};
    std::array<std::uint8_t, Capacity> storage{};
    std::array<frame_uniform_copy::Slice, 5> sources{};
    explicit Fixture(VkDeviceSize tail = Tail, bool foreignSource = false) {
        candidate.device = 1; candidate.command = h<VkCommandBuffer>(60); candidate.commandGeneration = 4;
        candidate.uniformFinalDraw = true; candidate.uniformDrawOrdinal = 2;
        candidate.uniformRefusal = frame_pixel_capture::UniformRefusal::None;
        candidate.uniformPipeline = {0xabcd, 9};
        candidate.binding.image.identity = {70, 2}; candidate.binding.view.identity = {80, 5};
        candidate.binding.view.image = candidate.binding.image.identity;
        auto& plan = candidate.uniformPlan;
        plan.metadataCandidate = true; plan.device = 1;
        // Core and LiveTracker own DIFFERENT generation namespaces. The same
        // native handle need not carry an identical counter in the two owners.
        plan.command = {60, 4004}; plan.pipelineLayout = {0xabcd1, 10};
        plan.set = {0xabcd2, 11}; plan.setLayout = {0xabcd3, 12};
        plan.setRevision = 100; plan.commandBindingRevision = 101;
        auto& v = plan.stages.vertex; auto& f = plan.stages.fragment;
        v.stage = VK_SHADER_STAGE_VERTEX_BIT; v.module = 300; v.generation = 50;
        v.hash = frame_pixel_uniform::NativeVertexFingerprint; v.bytes = frame_pixel_uniform::NativeVertexBytes; v.complete = true;
        std::memcpy(v.entry, "VS_DISTORT", sizeof("VS_DISTORT"));
        f.stage = VK_SHADER_STAGE_FRAGMENT_BIT; f.module = 301; f.generation = 51;
        f.hash = frame_pixel_uniform::NativeFragmentFingerprint; f.bytes = frame_pixel_uniform::NativeFragmentBytes; f.complete = true;
        std::memcpy(f.entry, "PS_DISTORT", sizeof("PS_DISTORT"));
        candidate.uniformStages = plan.stages;
        VkDeviceSize cursor = 0;
        for (std::size_t i = 0; i < Ranges.size(); ++i) {
            auto& slice = plan.slices[i]; const auto bytes = Ranges[i];
            slice.binding = frame_pixel_uniform::UniformBindings[i]; slice.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            slice.descriptorStages = VK_SHADER_STAGE_ALL; slice.descriptorRevision = 20 + i;
            slice.buffer.identity = {100 + i, 30 + i}; slice.buffer.size = bytes + 256;
            slice.buffer.usage = 19; slice.buffer.complete = true;
            slice.buffer.memory = {200 + i, 40 + i}; slice.buffer.memoryOffset = 128;
            slice.buffer.allocation.identity = slice.buffer.memory; slice.buffer.allocation.complete = true;
            slice.buffer.allocation.allocationSize = bytes + 512; slice.buffer.allocation.memoryTypeIndex = 1;
            slice.descriptorOffset = slice.effectiveOffset = 256; slice.descriptorRange = slice.effectiveRange = bytes;
            slice.destinationOffset = cursor;
            sources[i] = {h<VkBuffer>(slice.buffer.identity.handle), slice.buffer.size, slice.buffer.usage, slice.effectiveOffset, bytes};
            cursor += bytes;
        }
        plan.totalBytes = cursor;
        if (foreignSource) sources[2].buffer = h<VkBuffer>(999);
        const frame_uniform_copy::Preparation prep{h<VkBuffer>(90), Capacity, VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        if (recorder.prepare(h<VkDevice>(1), Fake::resolve, prep) != frame_uniform_copy::Status::Ok ||
            !recorder.admit(sources.data(), sources.size(), tail, dispatch)) throw std::runtime_error("sealed synthetic dispatch preparation failed");
        view.data = storage.data(); view.bytes = 12; view.capacity = Capacity;
        view.source = h<VkImage>(70); view.sourceGeneration = 2; view.commandGeneration = 4;
        view.oneTimeSubmit = false; view.retirementRequired = true; view.generationRetired = true;
        view.layout.format = frame_pixels::NativeP010; view.layout.extent = {2, 2}; view.layout.bytes = 12;
        // Original2x2 paired prefix:12 raw bytes +16 RGBA8 bytes, alpha opaque.
        for (unsigned i = 0; i < 4; ++i) storage[12 + i * 4 + 3] = 255;
        auto* payload = storage.data() + tail;
        word(payload + plan.slices[0].destinationOffset + 256, 1);
        word(payload + plan.slices[3].destinationOffset + 256, 1);
        word(payload + 264, 0x12345678);
        word(payload + plan.slices[1].destinationOffset, 0xffffffff);
        const auto metadata = plan.slices[2].destinationOffset;
        word(payload + metadata + 384, 2); word(payload + metadata + 388, 7); word(payload + metadata + 392, 0x3f800000);
        for (unsigned selector = 0; selector < 2; ++selector) {
            const float values[] = {.1f, selector ? .12f : .11f, 8.f, 0.f, .2f, selector ? .23f : .22f, 9.f, 0.f,
                                    .8f, selector ? .78f : .77f, 10.f, .31f, .9f, selector ? .89f : .88f, 11.f, .42f};
            const auto base = plan.slices[4].destinationOffset + 432 + 448 * selector;
            for (unsigned i = 0; i < 16; ++i) floating(payload + base + 4 * i, values[i]);
            word(payload + base + 12, 0x7fc00001); // Raw native UINT flags; its NaN FLOAT interpretation is irrelevant.
        }
    }
};

void refused(const frame_pixel_capture::Candidate& candidate, const frame_uniform_copy::Dispatch& dispatch,
             const frame_pixels::View& view, VkDeviceSize sampledEnd = Tail) {
    require(!frame_uniform_metadata::compatible(candidate, dispatch, view, sampledEnd), "incompatible provenance/tail is refused");
    std::ostringstream output; output << "unchanged";
    require(!frame_uniform_metadata::append(output, candidate, dispatch, view, sampledEnd, Capture) && output.str() == "unchanged",
            "refused metadata writes no partial JSON or hashes");
}
void controls() {
    Fixture fixture;
    require(fixture.candidate.uniformPlan.command.generation != fixture.candidate.commandGeneration &&
            frame_uniform_metadata::compatible(fixture.candidate, fixture.dispatch, fixture.view, Tail),
            "same native command with distinct Core/LiveTracker generations is a valid metadata envelope");
    require(fixture.dispatch.offset() == Tail && fixture.dispatch.bytes() == UniformBytes && fixture.dispatch.sliceCount() == 5,
            "sealed dispatch is the exact five-range6544-byte tail after the28-byte original paired prefix");
    require(fixture.recorder.record(fixture.candidate.command, fixture.dispatch) == frame_uniform_copy::Status::Ok &&
            fixture.fake.barriers == 2 && fixture.fake.copies == 5 && !fixture.fake.unexpected,
            "dispatch recording uses only original fake CPU callbacks, no real GPU functions");
    std::ostringstream positive; positive << "{\"synthetic_fixture\":true";
    require(frame_uniform_metadata::append(positive, fixture.candidate, fixture.dispatch, fixture.view, Tail, Capture),
            "production formatter accepts the exact synthetic companion envelope");
    require(positive.str().find("\"native_uniform_control\"") != std::string::npos &&
            positive.str().find("VS_DISTORT") != std::string::npos && positive.str().find("PS_DISTORT") != std::string::npos,
            "real formatter writes the native stage profile and uniform control field");
    require(fixture.fake.barriers == 2 && fixture.fake.copies == 5 && fixture.fake.resolutions == 2,
            "formatting performs no additional native resolutions or GPU commands");

    for (unsigned which = 0; which < 19; ++which) {
        auto candidate = fixture.candidate;
        switch (which) {
        case 0: candidate.uniformFinalDraw = false; break;
        case 1: candidate.uniformPlan.metadataCandidate = false; break;
        case 2: candidate.uniformPlan.totalBytes = 0; break;
        case 3: candidate.uniformPlan.totalBytes = frame_pixel_uniform::MaximumTotalBytes + 4; break;
        case 4: candidate.uniformPipeline.handle = 0; break;
        case 5: candidate.uniformPipeline.generation = 0; break;
        case 6: candidate.uniformDrawOrdinal = 0; break;
        case 7: candidate.uniformPlan.stages.vertex.hash ^= 1; break;
        case 8: candidate.uniformPlan.stages.fragment.bytes = 0; break;
        case 9: candidate.uniformPlan.device = 2; break;
        case 10: candidate.uniformPlan.command.handle += 1; break;
        case 11: candidate.binding.view.image.generation += 1; break;
        case 12: candidate.uniformPlan.slices[2].binding = 31; break;
        case 13: candidate.uniformPlan.slices[2].destinationOffset += 4; break;
        case 14: candidate.uniformPlan.slices[2].effectiveRange = 0; break;
        case 15: candidate.uniformPlan.slices[2].buffer.identity.handle += 1; break;
        case 16: candidate.uniformPlan.slices[2].buffer.size += 4; break;
        case 17: candidate.uniformPlan.slices[2].buffer.usage ^= VK_BUFFER_USAGE_TRANSFER_DST_BIT; break;
        case 18: candidate.uniformPlan.slices[2].effectiveOffset += 4; break;
        }
        refused(candidate, fixture.dispatch, fixture.view);
    }
    for (unsigned which = 0; which < 10; ++which) {
        auto view = fixture.view;
        switch (which) {
        case 0: view.commandGeneration += 1; break;
        case 1: view.sourceGeneration += 1; break;
        case 2: view.source = h<VkImage>(71); break;
        case 3: view.generationRetired = false; break;
        case 4: view.retirementRequired = false; break;
        case 5: view.oneTimeSubmit = true; break;
        case 6: view.capacity = Tail + UniformBytes - 4; break;
        case 7: view.capacity = frame_uniform_copy::MaximumCapacity + 1; break;
        case 8: view.data = nullptr; break;
        case 9: view.commandGeneration = 0; break;
        }
        refused(fixture.candidate, fixture.dispatch, view);
    }
    auto oneTime = fixture.view; oneTime.oneTimeSubmit = true; oneTime.retirementRequired = false; oneTime.generationRetired = false;
    require(frame_uniform_metadata::compatible(fixture.candidate, fixture.dispatch, oneTime, Tail),
            "one-time completion does not invent an unnecessary reusable retirement");
    refused(fixture.candidate, fixture.dispatch, fixture.view, Tail + 4);
    frame_uniform_copy::Dispatch empty;
    refused(fixture.candidate, empty, fixture.view);
    Fixture aliasedTail(24); refused(aliasedTail.candidate, aliasedTail.dispatch, aliasedTail.view);
    Fixture foreign(Tail, true); refused(foreign.candidate, foreign.dispatch, foreign.view);
    for (const auto& capture : std::array<std::string, 8>{{"", std::string(31, 'a'), std::string(33, 'a'),
        std::string(32, '0'), std::string(32, 'A'), std::string(32, 'g'), std::string(31, 'a') + '"', std::string(31, 'a') + '\0'}}) {
        std::ostringstream output; output << "untouched";
        require(!frame_uniform_metadata::append(output, fixture.candidate, fixture.dispatch, fixture.view, Tail, capture) && output.str() == "untouched",
                "invalid capture ID is rejected before output or byte hashing");
    }
}

void emit(const std::filesystem::path& directory) {
    Fixture fixture;
    std::filesystem::create_directories(directory);
    const auto jsonPath = directory / "native-uniform-control.json", binaryPath = directory / "uniform-bytes.bin";
    if (std::filesystem::exists(jsonPath) || std::filesystem::exists(binaryPath)) throw std::runtime_error("fixture outputs already exist");
    std::ostringstream output; output << "{\"synthetic_fixture\":true";
    if (!frame_uniform_metadata::append(output, fixture.candidate, fixture.dispatch, fixture.view, Tail, Capture))
        throw std::runtime_error("production formatter refused the synthetic emission");
    output << ",\"cpp_checks\":" << checks << ",\"core_command_generation\":" << fixture.candidate.uniformPlan.command.generation << "}\n";
    std::ofstream json(jsonPath); json << output.str(); json.close();
    std::ofstream binary(binaryPath, std::ios::binary);
    binary.write(reinterpret_cast<const char*>(fixture.storage.data() + Tail), UniformBytes); binary.close();
    if (!json || !binary) throw std::runtime_error("synthetic fixture write failed");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 1 && !(argc == 3 && std::string(argv[1]) == "--emit"))
            throw std::runtime_error("usage: test_pixel_uniform_metadata [--emit DIRECTORY]");
        controls();
        if (argc == 3) emit(argv[2]);
        std::cout << "pixel uniform metadata: " << checks << " controls passed; synthetic CPU formatter only, no GPU/native proof\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
