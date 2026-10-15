#!/usr/bin/env python3
"""Execute the production bounded refusal formatter, without native processes.

The C++ fixture constructs frozen snapshots and also exercises a genuine Core
loss through LiveTracker. No compositor, headset, Vulkan device or capture runs.
Generated source and compiler output are private and removed after each suite.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "layer/pixel_refusal_status.h"
TRACKER = ROOT / "layer/pixel_capture_tracker.h"

FIXTURE = r'''
#include "layer/pixel_refusal_status.h"
#include <array>
#include <cstring>
#include <memory>
#include <limits>
using frame_pixel_capture::Failure;
using frame_pixel_capture::FailureSnapshot;
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const char* mode = argv[1];
    FailureSnapshot snapshot{};
    snapshot.complete = true;
    snapshot.failure = Failure::CoreLoss;
    snapshot.site = "traceUpdateDescriptorSets";
    snapshot.coreLostState = 19;
    for (std::size_t i = 0; i < snapshot.coreReasonCounts.size(); ++i)
        snapshot.coreReasonCounts[i] = 100 + i;
    for (std::size_t i = 0; i < snapshot.coreCapacityCounts.size(); ++i)
        snapshot.coreCapacityCounts[i] = 200 + i;
    bool selected = false, armed = false;
    std::size_t capacity = frame_pixel_status::MaximumRefusalBytes;
    std::array<char, 81> label{};
    if (!std::strcmp(mode, "contention")) snapshot.failure = Failure::Contention;
    else if (!std::strcmp(mode, "capacity")) snapshot.failure = Failure::Capacity;
    else if (!std::strcmp(mode, "unknown")) snapshot.failure = Failure::Unknown;
    else if (!std::strcmp(mode, "generation_wrap")) snapshot.failure = Failure::GenerationWrap;
    else if (!std::strcmp(mode, "invalid_enum")) snapshot.failure = static_cast<Failure>(255);
    else if (!std::strcmp(mode, "incomplete")) snapshot.complete = false;
    else if (!std::strcmp(mode, "no_failure")) snapshot.failure = Failure::None;
    else if (!std::strcmp(mode, "short")) capacity = 128;
    else if (!std::strcmp(mode, "zero")) capacity = 0;
    else if (!std::strcmp(mode, "oversized")) ++capacity;
    else if (!std::strcmp(mode, "malicious_site")) snapshot.site = "trace\"event\n";
    else if (!std::strcmp(mode, "null_site")) snapshot.site = nullptr;
    else if (!std::strcmp(mode, "empty_site")) snapshot.site = "";
    else if (!std::strcmp(mode, "long_site")) { label.fill('x'); label[80] = 0; snapshot.site = label.data(); }
    else if (!std::strcmp(mode, "maximum")) {
        label.fill('x'); label[79] = 0; snapshot.site = label.data();
        snapshot.coreLostState = UINT64_MAX;
        snapshot.coreReasonCounts.fill(UINT64_MAX);
        snapshot.coreCapacityCounts.fill(UINT64_MAX);
        selected = true; armed = true;
    } else if (!std::strcmp(mode, "live_loss")) {
        auto tracker = std::make_unique<frame_pixel_capture::LiveTracker>();
        if (tracker->observe([](frame_bindings::Core& core) { core.reportUnsupported(1); },
                             "traceCreateSampler")) return 3;
        if (!tracker->poisoned()) return 4;
        // A later unsupported API cannot replace the frozen first failure.
        tracker->observe([](frame_bindings::Core& core) { core.reportUnsupported(1); },
                         "traceCreateImage");
        snapshot = tracker->failureSnapshot();
        if (!snapshot.complete) return 5;
        armed = true;
    }
    std::array<char, frame_pixel_status::MaximumRefusalBytes + 16> output;
    output.fill('!');
    auto* destination = std::strcmp(mode, "null_output") ? output.data() : nullptr;
    const auto bytes = frame_pixel_status::trackingRefusal(destination, capacity, snapshot, selected, armed);
    const auto boundary = capacity <= frame_pixel_status::MaximumRefusalBytes ? capacity : 0;
    for (std::size_t i = boundary; i < output.size(); ++i) if (output[i] != '!') return 6;
    if (!bytes) {
        std::puts("{\"returned\":0,\"canary_intact\":true}");
        return 0;
    }
    if (bytes >= capacity || output[bytes] != 0 || output[bytes - 1] != '\n') return 7;
    return std::fwrite(output.data(), 1, bytes, stdout) == bytes ? 0 : 8;
}
'''


class PixelRefusalStatusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="frame-pixel-refusal-status-")
        folder = Path(cls.temporary.name)
        source = folder / "fixture.cpp"
        cls.binary = folder / "fixture"
        source.write_text(FIXTURE)
        try:
            result = subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-pthread",
                                     "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I", str(ROOT),
                                     str(source), "-o", str(cls.binary)],
                                    text=True, capture_output=True, timeout=60)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            cls.binary_sha256 = hashlib.sha256(cls.binary.read_bytes()).hexdigest()
        except BaseException:
            cls.temporary.cleanup()
            raise

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def record(self, mode="core_loss"):
        result = subprocess.run([str(self.binary), mode], text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertLessEqual(len(result.stdout.encode()), 2048)
        self.assertEqual(len(result.stdout.splitlines()), 1)
        return json.loads(result.stdout)

    def test_frozen_counters_and_native_site_are_emitted_without_permission_claims(self):
        record = self.record()
        self.assertEqual(record["event"], "tracking_refused")
        self.assertEqual(record["failure"], "core_loss")
        self.assertEqual(record["failure_code"], 3)
        self.assertEqual(record["site"], "traceUpdateDescriptorSets")
        self.assertEqual(record["core_lost_state"], 19)
        self.assertEqual(record["core_reason_counts"], dict(zip(
            ("none", "capacity", "contention", "unknown", "unsupported", "stale", "bounds", "layout_mismatch"),
            range(100, 108))))
        self.assertEqual(record["core_capacity_counts"], dict(zip(
            ("set_layout", "pipeline_layout", "pool", "set", "buffer", "image", "view", "sampler", "memory", "command", "conversion"),
            range(200, 211))))
        self.assertFalse(record["selected"])
        self.assertNotIn("not_gpu_admitted", record)
        self.assertFalse(record["armed"])
        self.assertFalse(record["mask_enabled"])
        self.assertFalse(record["native_frame_association_proven"])

    def test_all_tracker_failure_enums_have_explicit_names(self):
        for mode, code in (("contention", 1), ("capacity", 2), ("unknown", 4), ("generation_wrap", 5)):
            with self.subTest(mode=mode):
                record = self.record(mode)
                self.assertEqual(record["failure"], mode)
                self.assertEqual(record["failure_code"], code)
        self.assertEqual(self.record("invalid_enum")["failure"], "invalid")

    def test_incomplete_no_failure_and_invalid_buffer_emit_no_partial_record(self):
        for mode in ("incomplete", "no_failure", "short", "zero", "oversized", "null_output"):
            with self.subTest(mode=mode):
                self.assertEqual(self.record(mode), {"returned": 0, "canary_intact": True})

    def test_api_site_validation_cannot_insert_json_or_unbounded_text(self):
        self.assertEqual(self.record("malicious_site")["site"], "invalid_site")
        self.assertEqual(self.record("long_site")["site"], "invalid_site")
        self.assertEqual(self.record("null_site")["site"], "unknown_site")
        self.assertEqual(self.record("empty_site")["site"], "unknown_site")

    def test_maximum_counters_and_label_fit_without_truncation(self):
        record = self.record("maximum")
        self.assertEqual(record["site"], "x" * 79)
        self.assertEqual(record["core_lost_state"], 2**64 - 1)
        self.assertTrue(all(x == 2**64 - 1 for x in record["core_reason_counts"].values()))
        self.assertTrue(all(x == 2**64 - 1 for x in record["core_capacity_counts"].values()))
        self.assertTrue(record["selected"])
        self.assertNotIn("not_gpu_admitted", record)
        self.assertTrue(record["armed"])

    def test_genuine_core_loss_reports_first_api_and_frozen_real_counters(self):
        record = self.record("live_loss")
        self.assertEqual(record["failure"], "core_loss")
        self.assertEqual(record["site"], "traceCreateSampler")
        self.assertEqual(record["core_lost_state"], 1)
        self.assertEqual(record["core_reason_counts"]["unsupported"], 1)
        self.assertEqual(sum(record["core_reason_counts"].values()), 1)
        self.assertEqual(sum(record["core_capacity_counts"].values()), 0)
        self.assertFalse(record["selected"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--receipt", type=Path)
    args, unittest_args = parser.parse_known_args()
    result = unittest.main(argv=[__file__] + unittest_args, exit=False).result
    if args.receipt:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(json.dumps({
            "schema": 1, "kind": "production_tracking_refusal_formatter_controls",
            "tests_run": result.testsRun, "successful": result.wasSuccessful(),
            "binary_sha256": getattr(PixelRefusalStatusTests, "binary_sha256", None),
            "source_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                              for path in (Path(__file__), HEADER, TRACKER)},
            "temporary_compiler_cache_removed": True,
            "native_compositor_tested": False, "headset_tested": False,
            "decoded_pixels_read": False, "passthrough_authorized": False,
        }, indent=2) + "\n")
    raise SystemExit(0 if result.wasSuccessful() else 1)
