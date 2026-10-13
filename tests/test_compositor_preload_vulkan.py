#!/usr/bin/env python3
"""Real Vulkan preload + implicit-layer ABI regression; no Frame/Ready seal.

Build the unchanged legacy interface as a negative control. The private-export
candidate is the CMake-built layer-only fixture, with no native entry veneer,
compositor preparation, import scope or invented production admission.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LAYER = "VK_LAYER_FRAME_MASK_trace"
FUNCTIONS = {name: "frame_compositor_raw_" + name for name in (
    "vkGetInstanceProcAddr", "vkGetDeviceProcAddr", "vkNegotiateLoaderLayerInterfaceVersion")}


def execute(command, *, env=None, timeout=20):
    process = subprocess.Popen(command, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except BaseException:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.communicate()
        raise
    return {"status": process.returncode, "stdout": stdout, "stderr": stderr}


def success(result):
    assert result["status"] == 0, result
    line = next(line for line in result["stdout"].splitlines() if line.startswith("RESULT="))
    facts = json.loads(line[len("RESULT="):])
    assert facts["query_pool"] and facts["image_bind_view"] and facts["teardown"], facts
    assert facts["native_frame"] is False and facts["checks"] >= 41, facts
    return facts


def exports(path):
    result = execute(["nm", "-D", "--defined-only", str(path)])
    assert result["status"] == 0, result
    return {line.split()[-1] for line in result["stdout"].splitlines() if line.strip()}


def trace(path):
    assert 0 < path.stat().st_size <= 1024 * 1024
    records = [json.loads(line) for line in path.read_text().splitlines()]
    assert records[0]["event"] == "header" and records[-1]["event"] == "footer"
    footer = records[-1]
    assert footer["io_ok"] and not footer["byte_cap_reached"], footer
    assert not footer["lost_state"] and not footer["lost_binding_state"], footer
    assert not footer["dropped_events"], footer
    assert footer["binding_journal"]["published"] == footer["binding_journal"]["replayed"] >= 8, footer
    return {"bytes": path.stat().st_size,
            "journal_published": footer["binding_journal"]["published"],
            "journal_replayed": footer["binding_journal"]["replayed"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("private_layer", type=Path)
    parser.add_argument("private_manifest", type=Path)
    parser.add_argument("--receipt", type=Path)
    args = parser.parse_args()
    compiler = shutil.which("c++")
    assert compiler, "Host C++ compiler required for real legacy negative control"
    icd = next((Path("/usr/share/vulkan/icd.d") / name for name in (
        "lvp_icd.json", "lvp_icd.x86_64.json") if (Path("/usr/share/vulkan/icd.d") / name).is_file()), None)
    assert icd, "Mesa software Vulkan ICD required; not a stubbed dispatch test"
    # Every child inherits this bound, including deliberately unsafe controls.
    # Nothing changes the user's system core policy or touches Frame services.
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    layer = args.private_layer.resolve()
    assert exports(layer) == set(FUNCTIONS.values()), "Layer-only fixture exposes a standard/global Vulkan symbol"
    manifest = json.loads(args.private_manifest.read_text())
    assert manifest["layer"]["functions"] == FUNCTIONS
    assert manifest["layer"]["library_path"] == str(layer)
    report = {"schema": 1, "kind": "real_vulkan_preload_implicit_layer_abi",
              "candidate_sha256": hashlib.sha256(layer.read_bytes()).hexdigest(),
              "candidate_public_exports": sorted(exports(layer)), "cases": {},
              "real_loader_and_mesa": True, "production_cold_admission": False,
              "native_frame": False, "exact_frame_query_pool_error_reproduced": False,
              "mask_enabled": False, "core_dumps_disabled": True}
    with tempfile.TemporaryDirectory(prefix="frame-preload-vulkan-") as temporary:
        folder = Path(temporary)
        program = folder / "vrcompositor"
        shutil.copy2(args.fixture, program)
        legacy = folder / "legacy.so"
        built = execute([compiler, "-std=c++17", "-O2", "-pthread", "-fPIC", "-shared",
                         "-fvisibility=hidden", "-Wl,-Bsymbolic-functions",
                         str(ROOT / "layer/passive_pipeline_trace.cpp"), "-o", str(legacy)], timeout=60)
        assert built["status"] == 0, built
        assert set(FUNCTIONS) <= exports(legacy)
        report["legacy_sha256"] = hashlib.sha256(legacy.read_bytes()).hexdigest()
        for label, selected, preload, mode, active, functions in (
            ("legacy_manifest_only", legacy, False, "global", True, False),
            ("legacy_preload_negative", legacy, True, "global", True, False),
            ("legacy_mixed_gdpa", legacy, True, "mixed", True, False),
            ("private_manifest_only", layer, False, "global", True, True),
            ("private_preload_global", layer, True, "global", True, True),
            ("private_preload_loader", layer, True, "loader", True, True),
            ("private_preload_direct", layer, True, "direct", True, True),
            ("private_preload_mixed", layer, True, "mixed", True, True),
            ("private_preload_disabled", layer, True, "global", False, True),
            ("private_missing_negotiate_map", layer, True, "global", True, False),
            ("private_wrong_interface_map", layer, True, "global", True, True),
        ):
            home = folder / label
            implicit = home / "vulkan/implicit_layer.d/frame.json"
            implicit.parent.mkdir(parents=True)
            m = json.loads(json.dumps(manifest))
            m["layer"]["library_path"] = str(selected)
            if not functions:
                m["layer"].pop("functions", None)
            if label == "private_wrong_interface_map":
                m["layer"]["functions"] = {key: value + "_missing" for key, value in FUNCTIONS.items()}
            implicit.write_text(json.dumps(m))
            output = home / "metadata.jsonl"
            env = os.environ.copy()
            for key in ("LD_PRELOAD", "LD_AUDIT", "VK_LAYER_PATH", "VK_INSTANCE_LAYERS",
                        "VK_ADD_LAYER_PATH", "VK_ADD_IMPLICIT_LAYER_PATH", "VK_LOADER_LAYERS_ENABLE",
                        "VK_LOADER_LAYERS_DISABLE", "FRAME_MASK_TRACE_DISABLE", "FRAME_MASK_TRACE_FRAGMENT_HASH",
                        "FRAME_MASK_TRACE_BINDINGS_ARM_FILE", "GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE",
                        "GHVR_FRAME_COMPOSITOR_RAW_OUTPUT"):
                env.pop(key, None)
            env.update({"XDG_DATA_HOME": str(home), "VK_ICD_FILENAMES": str(icd),
                        "FRAME_MASK_TRACE": "1", "FRAME_MASK_TRACE_BINDINGS": "1",
                        "FRAME_MASK_TRACE_OUTPUT": str(output), "VK_LOADER_DEBUG": "error"})
            if preload:
                env["LD_PRELOAD"] = str(selected)
            if not active:
                env["FRAME_MASK_TRACE_DISABLE"] = "1"
            result = execute([str(program), mode], env=env)
            if label == "legacy_preload_negative":
                assert result["status"] != 0 and "FAIL stage=create-instance" in result["stderr"], result
                assert "GIPA_SOURCE=" + str(legacy) in result["stdout"], result
            else:
                facts = success(result)
                result["facts"] = facts
                if label == "legacy_mixed_gdpa":
                    assert "GDPA_SOURCE=" + str(legacy) in result["stdout"], result
                else:
                    for name in ("GIPA_SOURCE", "GDPA_SOURCE"):
                        source = next(line.split("=", 1)[1] for line in result["stdout"].splitlines() if line.startswith(name + "="))
                        assert "libvulkan.so" in Path(source).name, (label, name, source)
                if label in ("private_preload_disabled", "private_missing_negotiate_map", "private_wrong_interface_map"):
                    assert not output.exists(), "Inactive or undiscoverable layer emitted trace data"
                else:
                    result["trace"] = trace(output)
            report["cases"][label] = result
    report["temporary_outputs_removed"] = True
    if args.receipt:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: 11 real Vulkan loader/Mesa cases; legacy preload symbol collision reproduced; "
          "private exports preserve global, direct and mixed dispatch, query pools and image lifecycles")


if __name__ == "__main__":
    main()
