"""Regress the native run's healthy-UI / refused-device combination offline."""
import copy
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location("pixel_stream_preflight", Path(__file__).parents[1] / "tools/pixel_stream_preflight.py")
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


class StreamPreflightTests(unittest.TestCase):
    def setUp(self):
        self.status = {"active_mappings": {"uncertain": False, "foreign_mapping_pids": [], "owned_mapping_pids": [17]}}
        self.health = {"status": "ready", "ready": True, "process_identities": {"compositor": {"pid": 17}}}
        self.events = [
            {"event": "prepared", "sampler_ready": True, "uniform_companion_enabled": False, "mask_enabled": False},
            {"event": "ssbo_definition_feature", "initialized": True, "observed": True, "enabled": True, "raw": 1, "contract_reason": "none"},
            {"event": "ssbo_definition_owned", "pipeline_generation": 7, "current_at_cold_lookup": True,
             "native_lifetime_lease": False, "native_compatibility_proven": False, "pipeline_changed": False, "mask_enabled": False},
            {"event": "ssbo_transaction_state", "reason": "none", "transaction_reason": 0, "profile": False, "ready": False, "queue_retrieved": True}]

    def assess(self, **kwargs):
        return gate.assess(self.status, self.health, self.events, definitions=True, transaction=True, **kwargs)

    def test_healthy_ui_cannot_override_exact_native_device_refusal(self):
        # Native 2026-10-16 run: enabled fragment stores, healthy UI/sampler,
        # but the device definition stopped at HostQueryReset, chain index5.
        self.events[1].update(contract_reason="extension", contract_index=5, contract_structure_type=1000261000)
        self.events.append({"event": "ssbo_definition_refusals", "first_reason": "device_ineligible"})
        result = self.assess()
        self.assertFalse(result["ready"])
        self.assertIn("device_definition_not_admitted", result["reasons"])

    def test_actual_candidate_clone_can_wait_for_manual_arm(self):
        self.assertTrue(self.assess()["ready"])
        self.assertFalse(self.assess(require_profile=True)["ready"])
        self.assertFalse(self.assess(require_clone=True)["ready"])
        self.events[-1]["profile"] = True
        # Arm permits metadata-only selection of the actual streamed candidate.
        # Requiring its clone beforehand would make selection impossible.
        self.assertTrue(self.assess(require_profile=True)["ready"])
        self.assertFalse(self.assess(require_clone=True)["ready"])
        self.events[-1].update(profile=True, ready=True)
        result = self.assess(require_clone=True)
        self.assertTrue(result["ready"])
        self.assertFalse(result["passthrough_authorized"])
        self.assertFalse(result["native_frame_association_proven"])
        self.events[-1]["queue_retrieved"] = False
        self.assertFalse(self.assess(require_profile=True)["ready"])

    def test_unrelated_native_definition_rejections_are_not_device_failure(self):
        self.events.append({"event": "ssbo_definition_refusals", "first_reason": "contract", "first_contract_reason": "shader_pin"})
        self.assertTrue(self.assess()["ready"])

    def test_profile_without_any_complete_recipe_cannot_arm(self):
        self.events[-1]["profile"] = True
        self.events = [event for event in self.events if event["event"] != "ssbo_definition_owned"]
        result = self.assess(require_profile=True)
        self.assertFalse(result["ready"])
        self.assertIn("no_complete_pipeline_definition_observed", result["reasons"])
        self.assertNotIn("original_output_clone_not_ready", result["reasons"])

    def test_prestream_missing_target_is_observation_only_not_a_causal_deadlock(self):
        self.events = [event for event in self.events if event["event"] != "ssbo_definition_owned"]
        before = self.assess()
        self.assertTrue(before["ready"])
        self.assertEqual(before["readiness_phase"], "pre_stream_observation")
        self.assertTrue(before["awaiting_original_pipeline"])
        self.assertFalse(before["receiver_pipeline_admitted"])
        self.assertFalse(self.assess(require_profile=True)["ready"])

    def test_r2_healthy_profile_without_recipes_explains_actual_legacy_masks(self):
        self.events[-1]["profile"] = True
        self.events = [event for event in self.events if event["event"] != "ssbo_definition_owned"]
        # The real r2 receipt kept global first_reason=no_ready for all masks.
        for mask in (16384, 139264, 32768, 1152):
            self.events.append({"event": "ssbo_definition_refusals", "new_reasons": mask, "first_reason": "no_ready", "first_contract_reason": "none"})
        result = self.assess(require_profile=True)
        self.assertFalse(result["ready"])
        self.assertIn("no_complete_pipeline_definition_observed", result["reasons"])
        self.assertIn("definition_retention_refused", result["reasons"])
        details = result["definition_diagnostics"]
        self.assertTrue({"capacity", "unknown_layout", "contract", "unsupported_route"}.issubset(details["reasons"]))
        self.assertFalse(details["context_available"])
        self.assertEqual(details["contexts"], [])  # Legacy receipt does not prove a nested RP2 payload.
        self.assertFalse(self.assess()["ready"])

    def test_keyed_cause_and_inventory_survive_global_first_no_ready(self):
        context = {"context_index": 2, "reason": "contract", "kind": 2, "contract_reason": "bounds", "contract_index": 3, "contract_structure_type": 1000109004,
                   "first_object": 21, "last_object": 92, "first_generation": 0, "last_generation": 0, "count": 12}
        inventory = [{"kind": i, "live": i, "capacity": i + 1} for i in range(6)]
        self.events.append({"event": "ssbo_definition_refusals", "new_reasons": 8192, "first_reason": "no_ready", "contexts": [context], "inventory": inventory, "set_fact_count": 12, "set_fact_capacity": 384})
        details = self.assess()["definition_diagnostics"]
        self.assertEqual(details["contexts"], [context])
        self.assertEqual(details["inventory"], inventory)
        self.assertEqual(details["set_facts"], {"count": 12, "capacity": 384})
        self.assertEqual(details["reasons"], ["contract"])
        self.assertTrue(self.assess()["ready"])  # Another complete recipe still establishes the separate admission condition.

    def test_context_overflow_and_malformed_nested_data_fail_closed(self):
        self.events = [event for event in self.events if event["event"] != "ssbo_definition_owned"]
        self.events.append({"event": "ssbo_definition_refusals", "new_reasons": 0, "context_overflow": 1})
        self.assertIn("definition_evidence_truncated", self.assess()["reasons"])
        self.events[-1] = {"event": "ssbo_definition_refusals", "contexts": [{"reason": "contract"}]}
        self.assertIn("invalid_definition_context", self.assess()["reasons"])
        self.events[-1] = {"event": "ssbo_definition_refusals", "new_reasons": 1 << 63}
        self.assertIn("invalid_definition_context", self.assess()["reasons"])
        self.events[-1] = {"event": "ssbo_definition_refusals", "set_fact_count": 385, "set_fact_capacity": 384}
        self.assertIn("invalid_definition_context", self.assess()["reasons"])

    def test_context_reports_can_cross_old_event_bound_but_not_the_new_fixed_bound(self):
        self.events += [{"event": "bounded_cold_metadata"}] * (gate.MAX_EVENTS - len(self.events))
        self.assertTrue(self.assess()["ready"])
        self.events.append({"event": "bounded_cold_metadata"})
        self.assertEqual(self.assess()["reasons"], ["invalid_events"])

    def test_target_graphics_overflow_is_explicit_without_authorizing_a_recipe(self):
        self.events = [event for event in self.events if event["event"] != "ssbo_definition_owned"]
        self.events.append({"event": "ssbo_definition_refusals", "graphics_observation_overflow": True})
        self.assertIn("definition_evidence_truncated", self.assess()["reasons"])
        self.events[-1]["graphics_observation_overflow"] = 1
        self.assertEqual(self.assess()["reasons"], ["invalid_definition_context"])

    def test_real_failure_domains_remain_independent(self):
        original = copy.deepcopy((self.status, self.health, self.events))
        for domain in ("health", "mapping", "sampler", "feature", "transaction", "failure"):
            with self.subTest(domain=domain):
                self.status, self.health, self.events = copy.deepcopy(original)
                if domain == "health": self.health["ready"] = False
                if domain == "mapping": self.status["active_mappings"]["owned_mapping_pids"] = [19]
                if domain == "sampler": self.events[0]["sampler_ready"] = False
                if domain == "feature": self.events[1]["enabled"] = False
                if domain == "transaction": self.events[-1]["reason"] = "clone"
                if domain == "failure": self.events.append({"event": "tracking_refused"})
                self.assertFalse(self.assess()["ready"])

    def test_missing_malformed_and_oversized_receipts_never_allow_a_test(self):
        for events in (None, [], [None], [{"event": 1}], self.events * gate.MAX_EVENTS):
            with self.subTest(events=str(events)[:40]):
                self.assertFalse(gate.assess(self.status, self.health, events, definitions=True, transaction=True)["ready"])
        self.assertFalse(gate.assess(None, self.health, self.events)["ready"])
        self.assertFalse(gate.assess(self.status, self.health, self.events, transaction=True)["ready"])


class NativeProviderPreflightTests(unittest.TestCase):
    def setUp(self):
        source = StreamPreflightTests()
        source.setUp()
        self.status, self.health, self.events = source.status, source.health, source.events
        self.events[0]["native_device"] = 42
        self.provider = {"event": "native_subpass_provider", "state": "ready", "reason": "none", "epoch": 1,
            "device": 42, "module_base": 4096, "inode": 9, "chip": 7, "route_mask": 15,
            "entry_rvas": [0xb12180, 0xb12180, 0xb11d60, 0xb11d60],
            "provider_path": gate._NATIVE_DRIVER_PATH, "provider_path_bytes": len(gate._NATIVE_DRIVER_PATH),
            "provider_path_truncated": False, "mask_enabled": False, "native_frame_association_proven": False}
        self.events.append(self.provider)

    def assess(self, **kwargs):
        return gate.assess(self.status, self.health, self.events, definitions=True, transaction=True,
                           require_native_provider=True, **kwargs)

    def test_actual_known_khr_provider_can_precede_stream(self):
        self.assertTrue(self.assess()["ready"])
        self.assertFalse(self.assess()["passthrough_authorized"])
        self.provider.update(route_mask=10, entry_rvas=[0, 0xb12180, 0, 0xb11d60])
        self.assertTrue(self.assess()["ready"])

    def test_missing_refused_or_duplicate_provider_never_requests_stream(self):
        self.events.pop()
        self.assertFalse(self.assess()["ready"])
        self.events.append(self.provider)
        self.events.append(copy.deepcopy(self.provider))
        self.assertFalse(self.assess()["ready"])
        self.events.pop()
        for reason in ("module_path", "entry_point", "changed", "loaded_code"):
            self.provider.update(state="refused", reason=reason)
            self.assertFalse(self.assess()["ready"])

    def test_each_identity_or_route_mutant_refuses(self):
        for key, value in (("epoch", True), ("device", 43), ("inode", 0), ("module_base", -1),
                ("module_base", (1 << 64) - 1),
                ("provider_path", "/other/wrapper.so"), ("provider_path_truncated", True),
                ("provider_path_bytes", 999), ("chip", 9), ("chip", True), ("route_mask", 5),
                ("entry_rvas", [0xb12180, 1, 0xb11d60, 2]), ("entry_rvas", [0] * 3),
                ("mask_enabled", True), ("native_frame_association_proven", True)):
            with self.subTest(key=key, value=value):
                previous = self.provider[key]
                self.provider[key] = value
                self.assertFalse(self.assess()["ready"])
                self.provider[key] = previous

    def test_writer_device_must_match_provider(self):
        self.events[0]["native_device"] = 41
        self.assertFalse(self.assess()["ready"])
        self.events[0].pop("native_device")
        self.assertFalse(self.assess()["ready"])


if __name__ == "__main__":
    unittest.main()
