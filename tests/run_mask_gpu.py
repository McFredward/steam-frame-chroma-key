#!/usr/bin/env python3
"""Execute the generated explicit-mask fragment shader on a real Vulkan driver."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="frame-mask-gpu-") as tmp:
        folder = Path(tmp)
        vertex = folder / "vertex.spv"
        baseline = folder / "native_fixture.spv"
        subprocess.run(["spirv-as", "--target-env", "vulkan1.1",
                        str(ROOT / "tests/fixtures/mask_scene.spvasm"), "-o", str(baseline)], check=True)
        subprocess.run(["glslc", "--target-env=vulkan1.1", str(ROOT / "tests/mask_gpu.vert"),
                        "-o", str(vertex)], check=True)
        env = os.environ.copy()
        # Test headlessly and deterministically; do not accidentally select a busy GPU.
        software = Path("/usr/share/vulkan/icd.d/lvp_icd.x86_64.json")
        if not software.exists():
            software = Path("/usr/share/vulkan/icd.d/lvp_icd.json")
        if software.exists():
            env["VK_ICD_FILENAMES"] = str(software)
        for convention in ("straight", "premultiplied"):
            fragment = folder / (convention + ".spv")
            subprocess.run([sys.executable, str(ROOT / "tools/gen_mask_spv.py"),
                            str(ROOT / "tests/fixtures/mask_scene.spvasm"), str(fragment),
                            "--rgb-convention", convention], check=True)
            subprocess.run([sys.argv[1], str(vertex), str(fragment), convention], env=env,
                           check=True, timeout=30)
            subprocess.run([sys.argv[2], "deny", str(fragment), str(baseline)], check=True)
            stripped = folder / (convention + "-stripped.spv")
            subprocess.run(["spirv-opt", "--strip-debug", str(fragment), "-o", str(stripped)], check=True)
            subprocess.run([sys.argv[2], "deny", str(stripped), str(baseline)], check=True)
        # The safeguard must accept existing valid rect/chroma overrides.
        source = folder / "legacy_fixture.spvasm"
        source.write_text((ROOT / "tests/fixtures/mask_scene.spvasm").read_text().replace(
            "%float = OpTypeFloat 32", "%bool = OpTypeBool\n%float = OpTypeFloat 32"))
        for mode, arguments in (("rect", ["0.3", "0.3", "0.7", "0.7"]),
                                ("chroma", ["0", "1", "0", "0.18", "0.10", "0.5"])):
            original = folder / (mode + ".spv")
            subprocess.run([sys.executable, str(ROOT / "tools/gen_key_spv.py"), str(source),
                            str(original), mode, *arguments], check=True)
            subprocess.run([sys.argv[2], "allow", str(original), str(baseline)], check=True)


if __name__ == "__main__":
    main()
