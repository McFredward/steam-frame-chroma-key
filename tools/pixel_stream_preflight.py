"""Cold operator gate for an explicitly approved pixel-diagnostic session.

Native UI health, source-sampler readiness and optional receiver admission are
independent. A healthy compositor alone must not trigger a pointless headset
test when its cold device contract already refused the receiver. This reads
bounded public diagnostic receipts; it never contacts or changes a device.
"""


def assess(status, native_health, events, *, definitions=False, transaction=False,
           require_clone=False):
    reasons = []
    if not isinstance(status, dict) or not isinstance(native_health, dict):
        return {"ready": False, "reasons": ["invalid_status"]}
    if not isinstance(events, list) or len(events) > 128 or any(
            not isinstance(event, dict) or not isinstance(event.get("event"), str)
            for event in events):
        return {"ready": False, "reasons": ["invalid_events"]}
    if transaction and not definitions or require_clone and not transaction:
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
        elif require_clone and (states[-1].get("profile") is not True or states[-1].get("ready") is not True):
            reasons.append("original_output_clone_not_ready")
    return {"ready": not reasons, "reasons": reasons,
            "requires_maintainer_visible_ui_confirmation": True,
            "native_frame_association_proven": False, "passthrough_authorized": False}
