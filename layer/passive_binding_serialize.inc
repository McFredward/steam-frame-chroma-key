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
    const auto chain = [&](const frame_bindings::extensions::ChainSummary& value) {
        bool ok = append(out, capacity, used, ",\"chain\":{\"complete\":%s,\"error\":%u,\"error_flags\":%u,\"types\":[",
            boolean(value.complete), static_cast<unsigned>(value.error), value.errorFlags);
        for (std::uint32_t i = 0; ok && i < value.count && i < value.types.size(); ++i)
            ok = append(out, capacity, used, "%s%u", i ? "," : "", static_cast<unsigned>(value.types[i]));
        return ok && append(out, capacity, used, "]}");
    };
    const auto allocation = [&](const frame_bindings::MemoryMetadata& value) {
        const auto& e = value.extensions;
        return append(out, capacity, used, ",\"allocation\":{") && identity("identity", value.identity) &&
            append(out, capacity, used, ",\"size\":%llu,\"memory_type\":%u,\"complete\":%s",
                static_cast<unsigned long long>(value.allocationSize), value.memoryTypeIndex, boolean(value.complete)) &&
            chain(e.chain) && append(out, capacity, used,
                ",\"has_import\":%s,\"import_type\":%u,\"input_fd_scalar\":%d,\"has_export\":%s,\"export_types\":%u,"
                "\"has_dedicated\":%s,\"dedicated_image_input_handle\":\"%llx\",\"dedicated_buffer_input_handle\":\"%llx\",",
                boolean(e.hasImport), static_cast<unsigned>(e.importType), e.inputFD,
                boolean(e.hasExport), e.exportTypes, boolean(e.hasDedicated),
                static_cast<unsigned long long>(e.dedicatedImageHandle),
                static_cast<unsigned long long>(e.dedicatedBufferHandle)) &&
            identity("dedicated_image", value.dedicatedImage) && append(out, capacity, used, ",") &&
            identity("dedicated_buffer", value.dedicatedBuffer) && append(out, capacity, used, "}");
    };
    const auto conversion = [&](const frame_bindings::ConversionMetadata& value) {
        if (!value.identity.handle) return true;
        return append(out, capacity, used, ",\"conversion\":{") && identity("identity", value.identity) &&
            append(out, capacity, used,
                ",\"format\":%u,\"model\":%u,\"range\":%u,\"components\":[%u,%u,%u,%u],"
                "\"chroma_offsets\":[%u,%u],\"chroma_filter\":%u,\"force_explicit_reconstruction\":%u,\"complete\":%s",
                static_cast<unsigned>(value.format), static_cast<unsigned>(value.model), static_cast<unsigned>(value.range),
                static_cast<unsigned>(value.components.r), static_cast<unsigned>(value.components.g),
                static_cast<unsigned>(value.components.b), static_cast<unsigned>(value.components.a),
                static_cast<unsigned>(value.xChromaOffset), static_cast<unsigned>(value.yChromaOffset),
                static_cast<unsigned>(value.chromaFilter), value.forceExplicitReconstruction, boolean(value.complete)) &&
            chain(value.extensions.chain) && append(out, capacity, used, "}");
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
                    "\"subresource\":[%u,%u,%u,%u,%u],\"layout\":%u,\"complete\":%s",
                    static_cast<unsigned>(b.view.type), static_cast<unsigned>(b.view.format),
                    static_cast<unsigned>(b.view.components.r), static_cast<unsigned>(b.view.components.g),
                    static_cast<unsigned>(b.view.components.b), static_cast<unsigned>(b.view.components.a),
                    b.view.subresource.aspectMask, b.view.subresource.baseMipLevel, b.view.subresource.levelCount,
                    b.view.subresource.baseArrayLayer, b.view.subresource.layerCount,
                    static_cast<unsigned>(b.imageLayout), boolean(b.view.complete)) && chain(b.view.extensions.chain) &&
                append(out, capacity, used, ",\"has_conversion\":%s,\"conversion_handle\":\"%llx\"",
                    boolean(b.view.extensions.hasConversion), static_cast<unsigned long long>(b.view.extensions.conversionHandle)) &&
                conversion(b.view.conversion) && append(out, capacity, used, "}");
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
                allocation(b.image.allocation) && chain(b.image.extensions.chain);
            const auto& e = b.image.extensions;
            valid = valid && append(out, capacity, used,
                ",\"has_external\":%s,\"external_handle_types\":%u,\"has_drm_explicit\":%s,"
                "\"drm_modifier\":\"%llx\",\"memory_plane_count\":%u,\"memory_planes\":[",
                boolean(e.hasExternal), e.externalHandleTypes, boolean(e.hasDrmExplicit),
                static_cast<unsigned long long>(e.drmModifier), e.planeCount);
            for (std::uint32_t j = 0; valid && j < e.planeCount && j < e.planes.size(); ++j) {
                const auto& plane = e.planes[j];
                valid = append(out, capacity, used,
                    "%s{\"offset\":%llu,\"row_pitch\":%llu,\"array_pitch\":%llu,\"depth_pitch\":%llu}", j ? "," : "",
                    static_cast<unsigned long long>(plane.offset), static_cast<unsigned long long>(plane.rowPitch),
                    static_cast<unsigned long long>(plane.arrayPitch), static_cast<unsigned long long>(plane.depthPitch));
            }
            valid = valid && append(out, capacity, used, "],\"has_drm_list\":%s,\"modifier_count\":%u,\"modifier_candidates\":[",
                boolean(e.hasDrmList), e.modifierCount);
            for (std::uint32_t j = 0; valid && j < e.modifierCount && j < e.modifiers.size(); ++j)
                valid = append(out, capacity, used, "%s\"%llx\"", j ? "," : "", static_cast<unsigned long long>(e.modifiers[j]));
            valid = valid && append(out, capacity, used, "]}");
        }
        if (b.sampler.identity.handle) {
            const float values[] = {b.sampler.mipLodBias, b.sampler.minLod, b.sampler.maxLod, b.sampler.maxAnisotropy};
            std::uint32_t bits[4]{};
            std::memcpy(bits, values, sizeof(bits)); // Preserve exact scalar bits, including non-finite inputs.
            valid = valid && append(out, capacity, used, ",\"sampler\":{") && identity("identity", b.sampler.identity) &&
                append(out, capacity, used, ",\"filters\":[%u,%u,%u],\"address_modes\":[%u,%u,%u],"
                    "\"lod_anisotropy_float_bits\":[%u,%u,%u,%u],\"anisotropy_enable\":%u,\"compare_enable\":%u,"
                    "\"compare_op\":%u,\"border_color\":%u,\"unnormalized_coordinates\":%u,\"complete\":%s",
                    static_cast<unsigned>(b.sampler.magFilter), static_cast<unsigned>(b.sampler.minFilter),
                    static_cast<unsigned>(b.sampler.mipmapMode), static_cast<unsigned>(b.sampler.addressModeU),
                    static_cast<unsigned>(b.sampler.addressModeV), static_cast<unsigned>(b.sampler.addressModeW),
                    bits[0], bits[1], bits[2], bits[3], b.sampler.anisotropyEnable, b.sampler.compareEnable,
                    static_cast<unsigned>(b.sampler.compareOp), static_cast<unsigned>(b.sampler.borderColor),
                    b.sampler.unnormalizedCoordinates, boolean(b.sampler.complete)) && chain(b.sampler.extensions.chain);
            const auto& e = b.sampler.extensions;
            valid = valid && append(out, capacity, used,
                ",\"has_custom_border\":%s,\"custom_border_bits\":[%u,%u,%u,%u],\"custom_border_format\":%u,"
                "\"has_conversion\":%s,\"conversion_handle\":\"%llx\"", boolean(e.hasCustomBorder),
                e.customBorderBits[0], e.customBorderBits[1], e.customBorderBits[2], e.customBorderBits[3],
                static_cast<unsigned>(e.customBorderFormat), boolean(e.hasConversion),
                static_cast<unsigned long long>(e.conversionHandle)) && conversion(b.sampler.conversion) &&
                append(out, capacity, used, "}");
        }
        valid = valid && append(out, capacity, used, "}");
    }
    return valid && append(out, capacity, used, "]}");
}
