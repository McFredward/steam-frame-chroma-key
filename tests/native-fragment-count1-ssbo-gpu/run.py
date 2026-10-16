#!/usr/bin/env python3
"""Private count1 single-color SSBO proof against the unchanged original3 GPU."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/"tools"))
import native_fragment_ssbo_profile as profile


def invoke(command, env=None):
    result = subprocess.run([str(value) for value in command], capture_output=True, text=True, timeout=90, env=env, check=False)
    return {"command": [str(value) for value in command], "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--driver", choices=("both", "intel", "software"), default="both")
    args = parser.parse_args(argv)
    report = {"schema": "private-count1-ssbo-gpu-v1", "status": "FAIL", "steps": [], "devices": [],
              "native_creation_proven": False, "native_same_draw_eye_frame_binding_proven": False, "passthrough_authorized": False}
    code = 1
    try:
        profile.require(not args.record.exists() and not args.record.is_symlink(), "exclusive receipt required")
        original = profile.ssbo.recovery.read_owned(args.original, profile.ssa.MAX_BYTES)
        profile.original_interface(original)
        report["original_sha256"] = profile.ssa.ORIGINAL_SHA256
        sources = [Path(__file__), Path(__file__).with_name("fixture.cpp"), ROOT/"tests/native-fragment-ssbo-gpu/fixture.cpp",
                   ROOT/"tools/native_fragment_ssbo_profile.py", ROOT/"tests/native-fragment-ssa-gpu/fixture.cpp"]
        report["source_sha256"] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
        with tempfile.TemporaryDirectory(prefix="owned-count1-ssbo-gpu-") as directory:
            private = Path(directory);fixture, vertex = private/"fixture", private/"vertex.spv"
            # Reuse all approved allocation, setup, execution, record canary and
            # cleanup logic without editing its shared source or copying a game
            # shader into the repository. Only its terminal main is excluded.
            existing = (ROOT/"tests/native-fragment-ssbo-gpu/fixture.cpp").read_text()
            profile.require(existing.count("\nint main(int argc,char** argv){") == 1, "one reviewed helper/main boundary")
            prefix = existing.split("\nint main(int argc,char** argv){")[0]
            prefix = prefix.replace('#include "../native-fragment-ssa-gpu/fixture.cpp"', '#include "'+str(ROOT/"tests/native-fragment-ssa-gpu/fixture.cpp")+'"')
            (private/"owned_ssbo_fixture_helpers.inc").write_text(prefix)
            for command in (["g++", "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-O2", "-I"+str(private), Path(__file__).with_name("fixture.cpp"), "-lvulkan", "-o", fixture],
                            ["glslc", "--target-env=vulkan1.1", ROOT/"tests/native-fragment-ssa-gpu/fixture.vert", "-o", vertex]):
                step = invoke(command);report["steps"].append(step);profile.require(step["returncode"] == 0, "owned count1 fixture compilation")
            array3 = profile.canonical_ssbo(original);array3_path = private/"array3.spv";array3_path.write_bytes(array3)
            modules = {}
            for first in (False, True):
                mode = "first-covered" if first else "pixel0"
                binary, proof = profile.transform(original, first);path = private/(mode+".spv");path.write_bytes(binary)
                step = invoke(["spirv-val", "--target-env", "vulkan1.1", path]);report["steps"].append(step)
                profile.require(step["returncode"] == 0, "specialized count1 SSBO validates")
                report[mode+"_profile"] = proof;modules[mode] = path
            for driver in (("intel", "software") if args.driver == "both" else (args.driver,)):
                device = {"driver": driver, "runs": {}};report["devices"].append(device)
                for mode, path in modules.items():
                    settings = private/(driver+"-"+mode);settings.mkdir();messages = settings/"messages.log"
                    (settings/"vk_layer_settings.txt").write_text("khronos_validation.validate_sync = true\nkhronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\nkhronos_validation.report_flags = error,warn,perf\nkhronos_validation.log_filename = "+str(messages)+"\n")
                    env = dict(os.environ, VK_ICD_FILENAMES="/usr/share/vulkan/icd.d/"+("intel_icd.json" if driver == "intel" else "lvp_icd.json"),
                        VK_LAYER_SETTINGS_PATH=str(settings), VK_LAYER_ENABLES="VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT")
                    result = invoke([fixture, vertex, args.original, array3_path, path], env)
                    result["validation_messages"] = messages.read_text(errors="replace") if messages.exists() else ""
                    device["runs"][mode] = result
                    profile.require(result["returncode"] == 0, "actual GPU count1 versus original3 proof: "+driver+"/"+mode)
                    profile.require("Validation Error" not in result["stderr"]+result["validation_messages"] and "SYNC-HAZARD" not in result["validation_messages"], "actual validation/synchronization clean")
                    result["result"] = json.loads(result["stdout"])
                    profile.require(result["result"]["validation_enabled"] and result["result"]["validation_errors"] == 0, "actual validation active")
            report["status"] = "PASS";code = 0
    except (OSError, ValueError, profile.ssbo.probe.ProbeError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
    finally:
        profile.ssbo.recovery.write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True)+"\n").encode())
    print(json.dumps({"status": report["status"], "devices": len(report["devices"]), "passthrough_authorized": False}))
    return code


if __name__ == "__main__": raise SystemExit(main())
