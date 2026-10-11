// Serialized exclusively by the background writer. Public API scalar inputs
// are observations, not image contents or a certified GPU/frame association.
bool Capture::serializeBindings(const frame_bindings::Snapshot& s, char* out,
                                std::size_t capacity, std::size_t& used) {
    const char* presence[] = {"unknown", "absent", "present"};
    const char* states[] = {"unknown", "absent", "unwritten", "written"};
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    const auto identity = [&](const char* key, const frame_bindings::Identity& value) {
        return append(out, capacity, used, "\"%s\":{\"handle\":\"%llx\",\"generation\":%llu}", key,
            static_cast<unsigned long long>(value.handle), static_cast<unsigned long long>(value.generation));
    };
    const auto allocation = [&](const frame_bindings::MemoryMetadata& value) {
        return append(out, capacity, used, ",\"allocation\":{") && identity("identity", value.identity) &&
            append(out, capacity, used, ",\"size\":%llu,\"memory_type\":%u,\"complete\":%s}",
                static_cast<unsigned long long>(value.allocationSize), value.memoryTypeIndex, boolean(value.complete));
    };
    bool valid = append(out, capacity, used, ",\"bindings\":{\"version\":1,\"scope\":\"recording_time_api_scalars\","
        "\"source_complete\":%s,\"gpu_consumed_coherent\":false,\"video_frame_associated\":false,"
        "\"reason\":%u,\"layout_has_binding31\":\"%s\",", boolean(s.sourceComplete),
        static_cast<unsigned>(s.reason), presence[static_cast<unsigned>(s.layout_has_binding31)]);
    valid = valid && identity("command", s.command) && append(out, capacity, used, ",") &&
        identity("pipeline_layout", s.pipelineLayout) && append(out, capacity, used, ",") &&
        identity("set", s.set) && append(out, capacity, used, ",") && identity("set_layout", s.setLayout) &&
        append(out, capacity, used, ",\"set_revision\":%llu,\"command_binding_revision\":%llu,\"targets\":[",
            static_cast<unsigned long long>(s.setRevision), static_cast<unsigned long long>(s.commandBindingRevision));
    for (std::size_t i = 0; valid && i < s.bindings.size(); ++i) {
        const auto& b = s.bindings[i];
        valid = append(out, capacity, used, "%s{\"binding\":%u,\"state\":\"%s\",\"type\":%u,"
            "\"count\":%u,\"stages\":%u,\"revision\":%llu,\"complete\":%s", i ? "," : "", b.binding,
            states[static_cast<unsigned>(b.state)], static_cast<unsigned>(b.type), b.descriptorCount,
            b.stages, static_cast<unsigned long long>(b.revision), boolean(b.complete));
        if (b.buffer.identity.handle) {
            valid = valid && append(out, capacity, used, ",\"buffer\":{") && identity("identity", b.buffer.identity) &&
                append(out, capacity, used, ",\"size\":%llu,\"usage\":%u,\"offset\":%llu,\"range\":%llu,"
                    "\"dynamic_offset\":%u,\"effective_offset\":%llu,\"effective_range\":%llu,\"complete\":%s,",
                    static_cast<unsigned long long>(b.buffer.size), b.buffer.usage,
                    static_cast<unsigned long long>(b.bufferOffset), static_cast<unsigned long long>(b.bufferRange),
                    b.dynamicOffset, static_cast<unsigned long long>(b.effectiveOffset),
                    static_cast<unsigned long long>(b.effectiveRange), boolean(b.buffer.complete)) &&
                identity("memory", b.buffer.memory) && append(out, capacity, used, ",\"memory_offset\":%llu",
                    static_cast<unsigned long long>(b.buffer.memoryOffset)) && allocation(b.buffer.allocation) &&
                append(out, capacity, used, "}");
        }
        if (b.view.identity.handle) {
            valid = valid && append(out, capacity, used, ",\"view\":{") && identity("identity", b.view.identity) &&
                append(out, capacity, used, ",") && identity("image", b.view.image) &&
                append(out, capacity, used, ",\"type\":%u,\"format\":%u,\"components\":[%u,%u,%u,%u],"
                    "\"subresource\":[%u,%u,%u,%u,%u],\"layout\":%u,\"complete\":%s}",
                    static_cast<unsigned>(b.view.type), static_cast<unsigned>(b.view.format),
                    static_cast<unsigned>(b.view.components.r), static_cast<unsigned>(b.view.components.g),
                    static_cast<unsigned>(b.view.components.b), static_cast<unsigned>(b.view.components.a),
                    b.view.subresource.aspectMask, b.view.subresource.baseMipLevel, b.view.subresource.levelCount,
                    b.view.subresource.baseArrayLayer, b.view.subresource.layerCount,
                    static_cast<unsigned>(b.imageLayout), boolean(b.view.complete));
        }
        if (b.image.identity.handle) {
            valid = valid && append(out, capacity, used, ",\"image\":{") && identity("identity", b.image.identity) &&
                append(out, capacity, used, ",\"type\":%u,\"format\":%u,\"extent\":[%u,%u,%u],\"mip_levels\":%u,"
                    "\"array_layers\":%u,\"samples\":%u,\"tiling\":%u,\"usage\":%u,\"complete\":%s,",
                    static_cast<unsigned>(b.image.type), static_cast<unsigned>(b.image.format), b.image.extent.width,
                    b.image.extent.height, b.image.extent.depth, b.image.mipLevels, b.image.arrayLayers,
                    static_cast<unsigned>(b.image.samples), static_cast<unsigned>(b.image.tiling), b.image.usage,
                    boolean(b.image.complete)) && identity("memory", b.image.memory) &&
                append(out, capacity, used, ",\"memory_offset\":%llu", static_cast<unsigned long long>(b.image.memoryOffset)) &&
                allocation(b.image.allocation) && append(out, capacity, used, "}");
        }
        if (b.sampler.identity.handle) {
            const float values[] = {b.sampler.mipLodBias, b.sampler.minLod, b.sampler.maxLod, b.sampler.maxAnisotropy};
            std::uint32_t bits[4]{};
            std::memcpy(bits, values, sizeof(bits)); // Preserve exact scalar bits, including non-finite inputs.
            valid = valid && append(out, capacity, used, ",\"sampler\":{") && identity("identity", b.sampler.identity) &&
                append(out, capacity, used, ",\"filters\":[%u,%u,%u],\"address_modes\":[%u,%u,%u],"
                    "\"lod_anisotropy_float_bits\":[%u,%u,%u,%u],\"anisotropy_enable\":%u,\"compare_enable\":%u,"
                    "\"compare_op\":%u,\"border_color\":%u,\"unnormalized_coordinates\":%u,\"complete\":%s}",
                    static_cast<unsigned>(b.sampler.magFilter), static_cast<unsigned>(b.sampler.minFilter),
                    static_cast<unsigned>(b.sampler.mipmapMode), static_cast<unsigned>(b.sampler.addressModeU),
                    static_cast<unsigned>(b.sampler.addressModeV), static_cast<unsigned>(b.sampler.addressModeW),
                    bits[0], bits[1], bits[2], bits[3], b.sampler.anisotropyEnable, b.sampler.compareEnable,
                    static_cast<unsigned>(b.sampler.compareOp), static_cast<unsigned>(b.sampler.borderColor),
                    b.sampler.unnormalizedCoordinates, boolean(b.sampler.complete));
        }
        valid = valid && append(out, capacity, used, "}");
    }
    return valid && append(out, capacity, used, "]}");
}
