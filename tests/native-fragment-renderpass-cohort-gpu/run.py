#!/usr/bin/env python3
"""Private actual-host GPU proof of complete legacy/RP2 subpass cohorts."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import native_fragment_ssbo_probe as tool

spec = importlib.util.spec_from_file_location("owned_ssbo_runner", ROOT / "tests/native-fragment-ssbo-gpu/run.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
demand = base.demand


def invoke(command, env=None):
    result = subprocess.run([str(value) for value in command], capture_output=True,
                            text=True, timeout=90, check=False, env=env)
    return {"command": [str(value) for value in command], "returncode": result.returncode,
            "stdout": result.stdout, "stderr": result.stderr}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--driver", choices=("intel", "software", "both"), default="both")
    args = parser.parse_args(argv)
    report = {"schema": "private-renderpass-cohort-gpu-v1", "status": "FAIL", "steps": [], "devices": [],
              "scope": "Actual legacy/RP2 two/three-subpass cohorts, selected index1, depth/MS resolve/input/preserve; same-route production views and indexed original restoration.",
              "limitations": ["The raster vertex is owned fixture code, not the pinned native vertex.",
                              "The checked legacy/RP2 capture/views used by Clone execute; full native-pinned Clone factory is not invoked.",
                              "The four owned modeled cohorts are proven, not any uncaptured native creation payload.",
                              "No receiver hooks, Frame execution, frame/eye association or passthrough presentation are proven."],
              "native_admission_proven": False, "native_frame_eye_proven": False, "passthrough_authorized": False}
    private, source_hashes, code = None, {}, 1
    try:
        demand(not args.record.exists() and not args.record.is_symlink(), "exclusive receipt required")
        original = tool.recovery.read_owned(args.original, tool.ssa.MAX_BYTES)
        demand(len(original) == tool.ssa.ORIGINAL_BYTES and tool.probe.digest(original) == tool.ssa.ORIGINAL_SHA256,
               "pinned private original PS required")
        report["original_sha256"] = tool.ssa.ORIGINAL_SHA256
        sources = [Path(__file__), Path(__file__).with_name("fixture.cpp"), ROOT / "layer/pixel_ssbo_clone_contract.h", ROOT / "layer/pixel_graphics_definition.h", ROOT / "tests/native-fragment-renderpass2-gpu/fixture.cpp",
                   ROOT / "tests/native-fragment-ssbo-gpu/fixture.cpp", ROOT / "tests/native-fragment-ssbo-gpu/run.py",
                   ROOT / "tests/native-fragment-ssa-gpu/fixture.cpp", ROOT / "tests/native-fragment-ssa-gpu/fixture.vert",
                   ROOT / "tools/native_fragment_ssbo_probe.py", ROOT / "tools/native_fragment_ssa_probe.py",
                   ROOT / "tests/native-fragment-atlas-gpu/run.py"]
        source_hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
        report["source_sha256"] = source_hashes
        with tempfile.TemporaryDirectory(prefix="owned-renderpass-cohort-gpu-") as directory:
            private = Path(directory)
            fixture, vertex, probe = private / "fixture", private / "vertex.spv", private / "probe.spv"
            for command in (["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O2",
                             Path(__file__).with_name("fixture.cpp"), "-lvulkan", "-o", fixture],
                            ["glslc", "--target-env=vulkan1.1", ROOT / "tests/native-fragment-ssa-gpu/fixture.vert", "-o", vertex]):
                step = invoke(command)
                report["steps"].append(step)
                demand(step["returncode"] == 0, "new owned RP2 fixture compilation")
            binary, transformation = tool.transform(original, base.admission())
            probe.write_bytes(binary)
            report["transformation"] = transformation
            step = invoke(["spirv-val", "--target-env", "vulkan1.1", probe])
            report["steps"].append(step)
            demand(step["returncode"] == 0, "private original-invocation SSBO shader validates")
            report["fixture_sha256"] = hashlib.sha256(fixture.read_bytes()).hexdigest()
            report["vertex_sha256"] = hashlib.sha256(vertex.read_bytes()).hexdigest()
            report["probe_sha256"] = hashlib.sha256(binary).hexdigest()
            for driver in (("intel", "software") if args.driver == "both" else (args.driver,)):
                settings = private / driver
                settings.mkdir()
                messages = settings / "validation.log"
                (settings / "vk_layer_settings.txt").write_text(
                    "khronos_validation.validate_sync = true\n"
                    "khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n"
                    "khronos_validation.report_flags = error,warn,perf\n"
                    "khronos_validation.log_filename = " + str(messages) + "\n")
                env = dict(os.environ, VK_ICD_FILENAMES="/usr/share/vulkan/icd.d/" +
                           ("intel_icd.json" if driver == "intel" else "lvp_icd.json"),
                           VK_LAYER_SETTINGS_PATH=str(settings),
                           VK_LAYER_ENABLES="VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT")
                result = invoke([fixture, vertex, args.original, probe], env)
                result["validation_messages"] = messages.read_text(errors="replace") if messages.exists() else ""
                report["devices"].append({"driver": driver, "run": result})
                combined = result["stderr"] + result["validation_messages"]
                demand("Validation Error" not in combined and "SYNC-HAZARD" not in combined,
                       "actual Vulkan or synchronization validation failure: " + driver)
                demand(result["returncode"] == 0, "actual RP2 equivalence execution failed: " + driver)
                value = json.loads(result["stdout"])
                result["result"] = value
                demand(value["cohorts"] == 4 and value["cases"] == 8 and value["selected_subpass"] == 1
                       and value["legacy_and_rp2"] and value["depth_resolve_input_preserve"]
                       and value["bit_exact_original_observer_restore"] and value["record_bytes"] == 128,
                       "actual whole-cohort same-route construction/target1 observer required")
                demand(value["validation_enabled"] and value["validation_errors"] == 0, "actual validation must be active")
            report["status"], code = "PASS", 0
    except (OSError, ValueError, KeyError, tool.probe.ProbeError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
    finally:
        # invoke() uses subprocess.run: success, failure and timeout children are
        # killed/reaped before leaving. TemporaryDirectory removes only this
        # invocation's generated private cache; evidence remains in the receipt.
        retention_path = Path("/home/claw/gloomhaven_vr/scripts/town_run_retention.py")
        if retention_path.is_file():
            retention_spec = importlib.util.spec_from_file_location("completion_retention", retention_path)
            retention = importlib.util.module_from_spec(retention_spec)
            retention_spec.loader.exec_module(retention)
            report["completion_cleanup"] = retention.cleanup_completed_run(
                "renderpass-cohort-gpu", code, args.record, ROOT, process_reaped=True)
        report["temporary_native_outputs_removed"] = private is None or not private.exists()
        report["production_changed_during_run"] = [path for path, digest in source_hashes.items()
                                                   if hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest]
        if report["production_changed_during_run"]:
            report["status"], code = "FAIL", 1
        try:
            tool.recovery.write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n").encode())
        except (OSError, tool.probe.ProbeError) as error:
            print(str(error), file=sys.stderr)
            code = 1
    print(json.dumps({"status": report["status"], "devices": len(report["devices"]), "passthrough_authorized": False}))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
