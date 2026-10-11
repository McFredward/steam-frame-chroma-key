#!/usr/bin/env python3
"""Exercise passive tracing through the real Vulkan loader, without a headset.

Original mask-fixture rendering checks every output pixel while this layer is
loaded. Temporary shaders, executables and bounded logs are removed on exit.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


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
        for name in ("FRAME_MASK_TRACE_FRAGMENT_HASH", "FRAME_MASK_TRACE_DISABLE"):
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
                                       ("enabled", compositor, "1")):
            output = folder / (mode + ".jsonl")
            env = base.copy()
            env["FRAME_MASK_TRACE"] = enabled
            env["FRAME_MASK_TRACE_OUTPUT"] = str(output)
            if mode == "explicit-disable":
                env["FRAME_MASK_TRACE_DISABLE"] = "1"
            elif mode == "invalid-filter":
                env["FRAME_MASK_TRACE_FRAGMENT_HASH"] = "invalid"
            elif mode == "filtered-miss":
                env["FRAME_MASK_TRACE_FRAGMENT_HASH"] = "0000000000000001"
            elif mode == "filtered-match":
                env["FRAME_MASK_TRACE_FRAGMENT_HASH"] = f"{fingerprint(fragment):016x}"
            subprocess.run([str(program), str(vertex), str(fragment), "straight"],
                           env=env, check=True, timeout=30)
            if mode in ("disabled", "explicit-disable", "wrong-process", "invalid-filter"):
                assert not output.exists(), "An inactive trace wrote a file"
                continue
            assert 0 < output.stat().st_size <= 1024 * 1024, "Trace escaped its byte cap"
            records = [json.loads(line) for line in output.read_text().splitlines()]
            assert len(records) <= 2050, "Trace escaped its event cap"
            assert records[0]["event"] == "header"
            assert records[-1]["event"] == "footer"
            footer = records[-1]
            assert footer["io_ok"] and not footer["byte_cap_reached"], footer
            assert footer["dropped_events"] == 0 and footer["lost_state"] == 0, footer
            expected = {1: (fingerprint(vertex), vertex.stat().st_size),
                        16: (fingerprint(fragment), fragment.stat().st_size)}
            draws = [record for record in records if record["event"] == "draw_recorded"]
            submits = [record for record in records if record["event"] == "submit_accepted"]
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
                assert any(submit["command"] == draw["command"] and
                           submit["command_generation"] == draw["command_generation"]
                           for submit in submits), "Recorded draw was never submitted"
            # Opening an existing output must not overwrite it or affect rendering.
            before = output.read_bytes()
            subprocess.run([str(compositor), str(vertex), str(fragment), "straight"],
                           env=env, check=True, timeout=30)
            assert output.read_bytes() == before, "Existing evidence was overwritten"
        print("PASS: passive loader, exact stage fingerprints, submitted draws, opt-in/process gates and unchanged GPU pixels")


if __name__ == "__main__":
    main()
