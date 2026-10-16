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
        for events in (None, [], [None], [{"event": 1}], self.events * 44):
            with self.subTest(events=str(events)[:40]):
                self.assertFalse(gate.assess(self.status, self.health, events, definitions=True, transaction=True)["ready"])
        self.assertFalse(gate.assess(None, self.health, self.events)["ready"])
        self.assertFalse(gate.assess(self.status, self.health, self.events, transaction=True)["ready"])


if __name__ == "__main__":
    unittest.main()
