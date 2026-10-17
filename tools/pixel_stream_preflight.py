"""Cold operator gate for an explicitly approved pixel-diagnostic session.

Native UI health, source-sampler readiness and optional receiver admission are
independent. A healthy compositor alone must not trigger a pointless headset
test when its cold device contract already refused the receiver. This reads
bounded public diagnostic receipts; it never contacts or changes a device.
"""


# Stable original journal reason bits. Legacy receipts may contain only this
# mask and a misleading global firstRefusal; never invent missing nested data.
_JOURNAL_REASONS = ("none", "native_creation_failed", "not_initialized", "already_initialized", "device_ineligible", "null_handle", "duplicate_handle", "capacity", "generation_exhausted", "unknown_set", "unknown_layout", "unknown_renderpass", "unknown_shader", "contract", "no_ready", "unknown_object", "stale_pipeline", "unsupported_route")
MAX_EVENTS = 256
_NATIVE_DRIVER_PATH = "/usr/lib/libvulkan_freedreno.so"
_NATIVE_DRIVER_RVAS = {6: (0xb113e0, 0xb11940), 7: (0xb12180, 0xb11d60), 8: (0xb18040, 0xb185a0)}


def _native_provider(events, prepared):
    """Read an already cold-verified receipt; this does not mint runtime permits."""
    records = [event for event in events if event["event"] == "native_subpass_provider"]
    if len(records) != 1:
        return {"ready": False, "reason": "native_provider_missing_or_ambiguous"}
    provider = records[0]
    if provider.get("state") != "ready" or provider.get("reason") != "none":
        return {"ready": False, "reason": "native_provider_refused", "provider": provider}
    values = ("epoch", "device", "module_base", "inode")
    if (any(type(provider.get(key)) is not int or not 0 < provider[key] < 1 << 64 for key in values)
            or len(prepared) != 1 or type(prepared[0].get("native_device")) is not int
            or prepared[0]["native_device"] != provider["device"]
            or provider.get("provider_path") != _NATIVE_DRIVER_PATH
            or type(provider.get("provider_path_bytes")) is not int
            or provider["provider_path_bytes"] != len(_NATIVE_DRIVER_PATH)
            or provider.get("provider_path_truncated") is not False
            or provider.get("native_frame_association_proven") is not False
            or provider.get("mask_enabled") is not False):
        return {"ready": False, "reason": "native_provider_identity_invalid", "provider": provider}
    chip, mask, rvas = provider.get("chip"), provider.get("route_mask"), provider.get("entry_rvas")
    if (type(chip) is not int or chip not in _NATIVE_DRIVER_RVAS or type(mask) is not int
            or not 0 <= mask <= 15 or mask & 10 != 10 or type(rvas) is not list or len(rvas) != 4
            or any(type(value) is not int or not 0 <= value < 1 << 32 for value in rvas)
            or provider["module_base"] > (1 << 64) - 1 - max(rvas)
            or any(rvas[index] != (_NATIVE_DRIVER_RVAS[chip][index // 2] if mask & (1 << index) else 0)
                   for index in range(4))):
        return {"ready": False, "reason": "native_provider_route_invalid", "provider": provider}
    return {"ready": True, "reason": "none", "provider": provider}


def _definition_context(events):
    reasons, contexts, inventory, set_facts = set(), {}, [], None
    overflow = False
    for event in events:
        if event["event"] != "ssbo_definition_refusals":
            continue
        mask = event.get("new_reasons", 0)
        if type(mask) is not int or mask < 0 or mask >= 1 << len(_JOURNAL_REASONS):
            raise ValueError("invalid refusal mask")
        reasons.update(name for index, name in enumerate(_JOURNAL_REASONS) if mask & (1 << index))
        entries = event.get("contexts", [])
        if not isinstance(entries, list) or len(entries) > 8:
            raise ValueError("invalid context group")
        for entry in entries:
            if (not isinstance(entry, dict) or entry.get("reason") not in _JOURNAL_REASONS
                    or type(entry.get("kind")) is not int or not 0 <= entry["kind"] < 6
                    or type(entry.get("context_index")) is not int or not 0 <= entry["context_index"] < 64
                    or not isinstance(entry.get("contract_reason"), str) or not 0 < len(entry["contract_reason"]) <= 64
                    or not entry["contract_reason"].isascii() or not entry["contract_reason"].replace("_", "").isalnum()
                    or any(type(entry.get(key)) is not int or not 0 <= entry[key] <= (1 << bits) - 1
                           for key, bits in (("contract_index", 32), ("contract_structure_type", 32), ("first_object", 64), ("last_object", 64), ("first_generation", 64), ("last_generation", 64), ("count", 64)))):
                raise ValueError("invalid context entry")
            index = entry["context_index"]
            if index in contexts and contexts[index] != entry:
                raise ValueError("conflicting context slot")
            contexts[index] = entry
            reasons.add(entry["reason"])
        current = event.get("inventory", [])
        if current:
            if (not isinstance(current, list) or len(current) != 6 or any(
                    not isinstance(item, dict) or type(item.get("kind")) is not int or item["kind"] != index
                    or type(item.get("live")) is not int or type(item.get("capacity")) is not int
                    or not 0 <= item["live"] <= item["capacity"] <= (1 << 32) - 1
                    for index, item in enumerate(current))):
                raise ValueError("invalid inventory")
            inventory = current
        if "set_fact_count" in event or "set_fact_capacity" in event:
            count, capacity = event.get("set_fact_count"), event.get("set_fact_capacity")
            if type(count) is not int or type(capacity) is not int or not 0 <= count <= capacity <= (1 << 32) - 1:
                raise ValueError("invalid semantic set inventory")
            set_facts = {"count": count, "capacity": capacity}
        amount = event.get("context_overflow", 0)
        if type(amount) is not int or not 0 <= amount <= (1 << 64) - 1:
            raise ValueError("invalid overflow")
        shape_overflow = event.get("creation_shape_overflow", False)
        graphics_overflow = event.get("graphics_observation_overflow", False)
        if type(shape_overflow) is not bool or type(graphics_overflow) is not bool:
            raise ValueError("invalid shape overflow")
        overflow |= bool(amount) or shape_overflow or graphics_overflow
    return {"reasons": sorted(reasons), "contexts": [contexts[key] for key in sorted(contexts)],
            "inventory": inventory, "set_facts": set_facts, "context_available": bool(contexts), "overflow": overflow}


def _recipe_observed(events):
    return any(event["event"] == "ssbo_definition_owned"
               and type(event.get("pipeline_generation")) is int and event["pipeline_generation"] > 0
               and event.get("current_at_cold_lookup") is True
               and event.get("native_lifetime_lease") is False
               and event.get("native_compatibility_proven") is False
               and event.get("pipeline_changed") is False and event.get("mask_enabled") is False
               for event in events)


def assess(status, native_health, events, *, definitions=False, transaction=False,
           require_profile=False, require_clone=False, require_native_provider=False):
    reasons = []
    if not isinstance(status, dict) or not isinstance(native_health, dict):
        return {"ready": False, "reasons": ["invalid_status"]}
    if not isinstance(events, list) or len(events) > MAX_EVENTS or any(
            not isinstance(event, dict) or not isinstance(event.get("event"), str)
            for event in events):
        return {"ready": False, "reasons": ["invalid_events"]}
    try:
        definition_context = _definition_context(events)
    except ValueError:
        return {"ready": False, "reasons": ["invalid_definition_context"]}
    if (transaction and not definitions) or ((require_profile or require_clone or require_native_provider) and not transaction) or type(require_native_provider) is not bool:
        reasons.append("invalid_profile")
    mapping = status.get("active_mappings", {})
    identities = native_health.get("process_identities", {})
    compositor = identities.get("compositor", {}) if isinstance(identities, dict) else {}
    pid = compositor.get("pid") if isinstance(compositor, dict) else None
    if (native_health.get("status") != "ready" or native_health.get("ready") is not True
            or type(pid) is not int or pid <= 0):
        reasons.append("native_ui_not_ready")
    if (not isinstance(mapping, dict) or mapping.get("uncertain") is not False
            or mapping.get("foreign_mapping_pids") != []
            or not isinstance(mapping.get("owned_mapping_pids"), list)
            or pid not in mapping.get("owned_mapping_pids", [])):
        reasons.append("diagnostic_mapping_not_verified")
    prepared = [event for event in events if event["event"] == "prepared"]
    provider = _native_provider(events, prepared) if require_native_provider else None
    if provider is not None and not provider["ready"]:
        reasons.append(provider["reason"])
    if (len(prepared) != 1 or prepared[0].get("sampler_ready") is not True
            or prepared[0].get("uniform_companion_enabled") is not False
            or prepared[0].get("mask_enabled") is not False):
        reasons.append("source_sampler_not_ready")
    if any(event["event"].endswith(("_refused", "_failed", "_failure")) for event in events):
        reasons.append("diagnostic_failed")
    if definitions:
        features = [event for event in events if event["event"] == "ssbo_definition_feature"]
        if (len(features) != 1 or features[0].get("initialized") is not True
                or features[0].get("observed") is not True
                or features[0].get("enabled") is not True
                or type(features[0].get("raw")) is not int or features[0].get("raw") != 1
                or features[0].get("contract_reason") != "none"):
            reasons.append("device_definition_not_admitted")
    if transaction:
        states = [event for event in events if event["event"] == "ssbo_transaction_state"]
        if not states:
            reasons.append("transaction_state_missing")
        elif any(event.get("reason") != "none" or type(event.get("transaction_reason")) is not int
                 or event.get("transaction_reason") != 0 for event in states):
            reasons.append("transaction_refused")
        else:
            if (require_profile or require_clone) and states[-1].get("profile") is not True:
                reasons.append("original_output_profile_not_ready")
            if require_profile and states[-1].get("queue_retrieved") is not True:
                reasons.append("original_queue_not_observed")
            # Before streaming these native target definitions may not exist
            # yet. That readiness permits observation, never an arm/capture.
            # A proven retention failure is different from not-yet-observed
            # inputs and should not request another pointless stream.
            recipe = _recipe_observed(events)
            if not recipe and {"capacity", "generation_exhausted"}.intersection(definition_context["reasons"]):
                reasons.append("definition_retention_refused")
            if (require_profile or require_clone) and not recipe:
                reasons.append("no_complete_pipeline_definition_observed")
            if not recipe and definition_context["overflow"]:
                reasons.append("definition_evidence_truncated")
            if require_clone and states[-1].get("ready") is not True:
                reasons.append("original_output_clone_not_ready")
    return {"ready": not reasons, "reasons": reasons,
            "native_provider": provider,
            "definition_diagnostics": definition_context,
            "readiness_phase": "capture_admission" if (require_profile or require_clone) else "pre_stream_observation",
            "receiver_pipeline_admitted": _recipe_observed(events) if transaction else False,
            "awaiting_original_pipeline": transaction and not _recipe_observed(events),
            "requires_maintainer_visible_ui_confirmation": True,
            "native_frame_association_proven": False, "passthrough_authorized": False}
