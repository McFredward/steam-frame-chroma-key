#!/usr/bin/env python3
"""Exercise passive tracing through the real Vulkan loader, without a headset.

Original mask-fixture rendering checks every output pixel while this layer is
loaded. Temporary shaders, executables and bounded logs are removed on exit.
"""
import json
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from probe_trace_loader import SESSION, validate_manifest


def fingerprint(path):
    value = 14695981039346656037
    for byte in path.read_bytes():
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value


def main():
    executable = Path(sys.argv[1]).resolve()
    manifest_dir = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="frame-mask-passive-") as temporary:
        folder = Path(temporary)
        compositor = folder / "vrcompositor"
        shutil.copy2(executable, compositor)
        vertex, fragment = folder / "vertex.spv", folder / "fragment.spv"
        subprocess.run(["glslc", "--target-env=vulkan1.1",
                        str(ROOT / "tests/mask_gpu.vert"), "-o", str(vertex)], check=True)
        subprocess.run([sys.executable, str(ROOT / "tools/gen_mask_spv.py"),
                        str(ROOT / "tests/fixtures/mask_scene.spvasm"), str(fragment),
                        "--rgb-convention", "straight"], check=True)
        base = os.environ.copy()
        base["VK_LAYER_PATH"] = str(manifest_dir)
        base["VK_INSTANCE_LAYERS"] = "VK_LAYER_FRAME_MASK_trace"
        implicit_home = folder / "implicit-home"
        implicit_folder = implicit_home / "vulkan/implicit_layer.d"
        implicit_folder.mkdir(parents=True)
        session = SESSION.Session(folder / "deployment-home", folder / "runtime")
        session.folder.mkdir(parents=True)
        shutil.copy2(manifest_dir / "libVkLayer_frame_mask_trace.so",
                     session.folder / "libVkLayer_frame_mask_trace.so")
        # Deployment/probe bounds apply to the stripped reviewed artifact.
        # RelWithDebInfo's debug sections are test build metadata, not runtime.
        subprocess.run(["strip", "--strip-debug",
                        str(session.folder / "libVkLayer_frame_mask_trace.so")], check=True)
        # The actual deployment helper, not the separate CMake manifest, must
        # survive discovery, negotiation, instance creation and submitted draws.
        manifest_bytes = session.contents()[session.manifest]
        manifest = json.loads(manifest_bytes)
        validate_manifest(manifest)
        cmake_manifest = json.loads((manifest_dir / "VkLayer_frame_mask_trace.json").read_text())
        validate_manifest(cmake_manifest)
        for key in ("name", "type", "api_version", "implementation_version",
                    "description", "enable_environment", "disable_environment"):
            assert manifest["layer"][key] == cmake_manifest["layer"][key], key
        implicit_manifest = implicit_folder / "frame_mask_trace.json"
        implicit_manifest.write_bytes(manifest_bytes)
        subprocess.run([sys.executable, str(ROOT / "tools/probe_trace_loader.py"),
                        str(session.folder / "libVkLayer_frame_mask_trace.so"), "--sha256",
                        hashlib.sha256((session.folder / "libVkLayer_frame_mask_trace.so").read_bytes()).hexdigest()],
                       check=True, timeout=10)
        for name in ("FRAME_MASK_TRACE_FRAGMENT_HASH", "FRAME_MASK_TRACE_DISABLE", "FRAME_MASK_TRACE_BINDINGS",
                     "FRAME_MASK_TRACE_BINDINGS_ARM_FILE"):
            base.pop(name, None)
        for name in ("lvp_icd.x86_64.json", "lvp_icd.json"):
            software = Path("/usr/share/vulkan/icd.d") / name
            if software.exists():
                base["VK_ICD_FILENAMES"] = str(software)
                break
        for mode, program, enabled in (("disabled", compositor, "0"),
                                       ("explicit-disable", compositor, "1"),
                                       ("wrong-process", executable, "1"),
                                       ("invalid-filter", compositor, "1"),
                                       ("filtered-miss", compositor, "1"),
                                       ("filtered-match", compositor, "1"),
                                       ("bindings-enabled", compositor, "1"),
                                       ("bindings-limit", compositor, "1"),
                                       ("bindings-unarmed", compositor, "1"),
                                       ("bindings-armed", compositor, "1"),
                                       ("bindings-bad-arm", compositor, "1"),
                                       ("implicit-enabled", compositor, "1"),
                                       ("implicit-numeric-version", compositor, "1"),
                                       ("enabled", compositor, "1")):
            output = folder / (mode + ".jsonl")
            env = base.copy()
            env["FRAME_MASK_TRACE"] = enabled
            env["FRAME_MASK_TRACE_OUTPUT"] = str(output)
            if mode.startswith("bindings-"):
                env["FRAME_MASK_TRACE_BINDINGS"] = "1"
                env["FRAME_MASK_TRACE_FRAGMENT_HASH"] = f"{fingerprint(fragment):016x}"
                if mode in ("bindings-unarmed", "bindings-armed", "bindings-bad-arm"):
                    marker = folder / (mode + ".marker")
                    env["FRAME_MASK_TRACE_BINDINGS_ARM_FILE"] = str(marker)
                    if mode != "bindings-unarmed":
                        marker.write_bytes(b"FRAME_MASK_TRACE_BINDINGS/1\n" if mode == "bindings-armed" else b"wrong\n")
                        marker.chmod(0o600)
            if mode.startswith("implicit-"):
                env.pop("VK_INSTANCE_LAYERS", None)
                env.pop("VK_LAYER_PATH", None)
                env["XDG_DATA_HOME"] = str(implicit_home)
            if mode == "implicit-numeric-version":
                invalid_manifest = json.loads(manifest_bytes)
                invalid_manifest["layer"]["implementation_version"] = 1
                try:
                    validate_manifest(invalid_manifest)
                except RuntimeError:
                    pass
                else:
                    raise AssertionError("Numeric deployment version was accepted by preflight")
                implicit_manifest.write_text(json.dumps(invalid_manifest))
            if mode == "explicit-disable":
                env["FRAME_MASK_TRACE_DISABLE"] = "1"
            elif mode == "invalid-filter":
                env["FRAME_MASK_TRACE_FRAGMENT_HASH"] = "invalid"
            elif mode == "filtered-miss":
                env["FRAME_MASK_TRACE_FRAGMENT_HASH"] = "0000000000000001"
            elif mode == "filtered-match":
                env["FRAME_MASK_TRACE_FRAGMENT_HASH"] = f"{fingerprint(fragment):016x}"
            convention = "trace-stress" if mode == "bindings-limit" else "trace-arm" if mode == "bindings-armed" else "straight"
            subprocess.run([str(program), str(vertex), str(fragment), convention],
                           env=env, check=True, timeout=30)
            if mode == "implicit-numeric-version":
                # Current Frame loader rejects this manifest, but older/future
                # loaders may tolerate it. Neither enumeration nor successful
                # rendering is deployment acceptance; strict preflight rejects
                # it independently. Observe real instance behavior without
                # hardcoding a vendor/version's parser policy.
                print("Numeric-version loader behavior:", "trace-created" if output.exists() else "trace-rejected")
                implicit_manifest.write_bytes(manifest_bytes)
                if not output.exists():
                    continue
            if mode in ("disabled", "explicit-disable", "wrong-process", "invalid-filter"):
                assert not output.exists(), "An inactive trace wrote a file"
                continue
            assert 0 < output.stat().st_size <= 1024 * 1024, "Trace escaped its byte cap"
            records = [json.loads(line) for line in output.read_text().splitlines()]
            assert len(records) <= 2050, "Trace escaped its event cap"
            assert records[0]["event"] == "header"
            assert records[0]["deadline_seconds"] == (120 if mode.startswith(("filtered-", "bindings-")) else 30), records[0]
            assert records[-1]["event"] == "footer"
            footer = records[-1]
            assert footer["io_ok"] and not footer["byte_cap_reached"], footer
            assert footer["dropped_events"] == 0 and footer["lost_state"] == 0, footer
            assert footer["lost_binding_state"] == 0, footer
            events = records[1:-1]
            assert [event["serial"] for event in events] == list(range(1, footer["emitted"] + 1)), \
                "Footer count or event serials do not match the complete writer drain"
            expected = {1: (fingerprint(vertex), vertex.stat().st_size),
                        16: (fingerprint(fragment), fragment.stat().st_size)}
            draws = [record for record in records if record["event"] == "draw_recorded"]
            submits = [record for record in records if record["event"] == "submit_accepted"]
            resources = [record for record in records if record["event"] == "bindings_recorded"]
            if mode in ("bindings-unarmed", "bindings-bad-arm"):
                assert records[0]["binding_arm_required"] and not footer["binding_capture_armed"]
                assert not resources and footer["binding_snapshots_attempted"] == 0, "Pre-stream Home draws spent the resource budget"
                continue
            if mode.startswith("bindings-"):
                assert records[0]["binding_metadata_version"] == 1
                assert records[0]["limit_binding_snapshots"] == 32
                assert 0 < len(resources) <= 32, "No bounded target resource snapshots were observed"
                assert footer["binding_capture_armed"] and footer["binding_snapshots_attempted"] == len(resources)
                if mode == "bindings-limit":
                    assert len(resources) == 32 and len(draws) == 48, "Resource budget failed to close while native rendering continued"
                for resource in resources:
                    assert resource["complete"], resource
                    binding = resource["bindings"]
                    assert binding["version"] == 1 and binding["source_complete"], binding
                    assert not binding["gpu_consumed_coherent"] and not binding["video_frame_associated"], binding
                    assert binding["scope"] == "recording_time_api_scalars"
                    assert binding["layout_has_binding31"] == "present", binding
                    assert binding["pipeline_layout"]["handle"] == resource["layout"]
                    assert binding["command"]["handle"] == resource["command"]
                    assert binding["set"]["generation"] > 0 and binding["set_revision"] > 0
                    targets = {target["binding"]: target for target in binding["targets"]}
                    assert set(targets) == {0, 2, 3, 4, 6, 9, 25, 31}
                    target = targets[31]
                    assert target["state"] == "written" and target["type"] == 1 and target["count"] == 1
                    assert target["image"]["extent"] == [4, 1, 1]
                    assert target["view"]["image"] == target["image"]["identity"]
                    assert target["image"]["memory"] == target["image"]["allocation"]["identity"]
                    assert target["sampler"]["complete"] and target["sampler"]["identity"]["generation"] > 0
                    assert all(targets[number]["state"] == "absent" for number in (0, 2, 3, 4, 6, 9, 25))
                    assert any(submit["complete"] and submit["device"] == resource["device"] and
                               submit["command"] == resource["command"] and
                               submit["command_generation"] == resource["command_generation"] and
                               submit["serial"] > resource["serial"] for submit in submits), resource
            else:
                assert records[0]["binding_metadata_version"] == 0 and not resources, "Resource tracing was not opt-in"
            if mode == "filtered-miss":
                assert not draws and not submits, "Unrelated rendering consumed the filtered draw budget"
                continue
            assert draws and submits, "Created pipelines alone do not show submitted draws"
            for draw in draws:
                assert draw["complete"], draw
                stages = draw["stages"]
                assert len(stages) == len(expected), stages
                for stage in stages:
                    assert stage["complete"], stage
                    assert (int(stage["fnv1a64"], 16), stage["bytes"]) == expected[stage["stage"]]
                assert any(submit["complete"] and submit["device"] == draw["device"] and
                           submit["command"] == draw["command"] and
                           submit["command_generation"] == draw["command_generation"] and
                           submit["serial"] > draw["serial"]
                           for submit in submits), "Recorded draw was never submitted"
            # Opening an existing output must not overwrite it or affect rendering.
            before = output.read_bytes()
            subprocess.run([str(compositor), str(vertex), str(fragment), "straight"],
                           env=env, check=True, timeout=30)
            assert output.read_bytes() == before, "Existing evidence was overwritten"
        print("PASS: exact deployment manifest in real implicit instance chain, rejected numeric preflight, "
              "exact stage fingerprints, submitted draws, bounded opt-in resource bindings and unchanged GPU pixels")


if __name__ == "__main__":
    main()
