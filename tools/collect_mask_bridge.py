#!/usr/bin/env python3
"""Collect bounded, local-only Steam Frame shader/interface diagnostics.

The public report contains selected metadata only. Valve shader disassembly is
kept in private/, excluded from Git, and must not be redistributed. This tool
does not load Vulkan, attach to processes, read Steam configuration/logs, inspect
environment variables, change settings, or transmit anything.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import tempfile
from typing import Any

MAX_SHADER_BYTES = 8 * 1024 * 1024
MAX_ASSEMBLY_BYTES = 16 * 1024 * 1024
MAX_METADATA_BYTES = 64 * 1024
MAX_EXECUTABLE_BYTES = 256 * 1024 * 1024
MAX_RECORDS = 128
DEFAULT_SHADER = "resources/shaders/vulkan/distort_ps_achromatic_se2.spv"
IDENTIFIER = re.compile(r"^%[A-Za-z0-9_]+$")


class CollectionError(Exception):
    """A safe, actionable collector error without untrusted file contents."""


def limited_read(path: Path, limit: int) -> bytes:
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise CollectionError("Input exceeds the collector size limit.")
    return data


def file_metadata(path: Path, *, limit: int = MAX_EXECUTABLE_BYTES) -> dict[str, Any]:
    """Hash regular files without retaining their contents or full paths."""
    if not path.is_file():
        return {"present": False}
    size = path.stat().st_size
    result: dict[str, Any] = {"present": True, "bytes": size}
    if size > limit:
        result["sha256"] = "omitted: size limit"
        return result
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        total = 0
        while chunk := stream.read(1024 * 1024):
            total += len(chunk)
            if total > limit:
                result["sha256"] = "omitted: size limit"
                return result
            digest.update(chunk)
    result["sha256"] = digest.hexdigest()
    return result


def run_bounded(command: list[str], *, timeout: float = 20.0,
                output_limit: int = MAX_METADATA_BYTES) -> tuple[int, bytes]:
    """Run a read-only tool with bounded disk output and no shell expansion."""
    def restrict_output() -> None:
        # Steam Frame is Linux. Cap external tools as well as Python reads so a
        # changed executable cannot fill the user's disk with diagnostics.
        import resource
        resource.setrlimit(resource.RLIMIT_FSIZE, (output_limit, output_limit))

    with tempfile.TemporaryFile() as output:
        with subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=output,
                              stderr=subprocess.DEVNULL,
                              preexec_fn=restrict_output if os.name == "posix" else None) as process:
            try:
                process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise CollectionError("Diagnostic tool exceeded its time limit.") from None
        output.seek(0)
        data = output.read(output_limit + 1)
        if len(data) > output_limit:
            raise CollectionError("Diagnostic tool exceeded its output limit.")
        return process.returncode, data


def executable_metadata(path: Path) -> dict[str, Any]:
    result = file_metadata(path)
    if (result["present"] and result.get("sha256") != "omitted: size limit"
            and shutil.which("readelf")):
        code, output = run_bounded(["readelf", "-n", str(path)])
        match = re.search(rb"Build ID:\s*([0-9a-fA-F]{8,128})\b", output)
        if code == 0 and match:
            result["elf_build_id"] = match[1].decode("ascii").lower()
    return result


def summarize_shader(assembly: str) -> dict[str, Any]:
    """Extract interface/type metadata; never copy instruction bodies or names."""
    definitions: dict[str, tuple[str, list[str]]] = {}
    decorations: dict[str, dict[str, Any]] = {}
    entry_points = []
    samples = []
    instruction_count = 0
    for line in assembly.splitlines():
        line = line.split(";", 1)[0].strip()
        if not line:
            continue
        try:
            tokens = shlex.split(line)
        except ValueError:
            continue
        if not tokens:
            continue
        result_id = None
        if len(tokens) > 2 and tokens[1] == "=" and IDENTIFIER.fullmatch(tokens[0]):
            result_id, opcode, operands = tokens[0], tokens[2], tokens[3:]
            definitions[result_id] = opcode, operands
        else:
            opcode, operands = tokens[0], tokens[1:]
        if not re.fullmatch(r"Op[A-Za-z0-9]+", opcode):
            continue
        instruction_count += 1
        if opcode == "OpDecorate" and len(operands) >= 3:
            target, decoration, value = operands[:3]
            if decoration in {"DescriptorSet", "Binding", "Location"} and value.isdecimal():
                decorations.setdefault(target, {})[decoration] = int(value)
            elif decoration == "BuiltIn" and re.fullmatch(r"[A-Za-z0-9_]+", value):
                decorations.setdefault(target, {})[decoration] = value
        elif opcode == "OpEntryPoint" and len(operands) >= 3:
            # Entry point names are intentionally excluded: arbitrary OpName /
            # OpString payloads are not safe metadata for a shareable report.
            if re.fullmatch(r"[A-Za-z0-9_]+", operands[0]) and IDENTIFIER.fullmatch(operands[1]):
                entry_points.append({"execution_model": operands[0], "id": operands[1]})
        elif opcode.startswith("OpImageSample") and len(operands) >= 3:
            if IDENTIFIER.fullmatch(operands[1]) and IDENTIFIER.fullmatch(operands[2]):
                samples.append({"opcode": opcode, "sampled_image_id": operands[1],
                                "coordinate_id": operands[2], "result_id": result_id})

    def type_summary(type_id: str) -> dict[str, Any]:
        opcode, operands = definitions.get(type_id, ("unknown", []))
        summary: dict[str, Any] = {"id": type_id, "kind": opcode}
        if opcode == "OpTypePointer" and len(operands) == 2:
            summary["storage_class"] = operands[0]
            nested, nested_operands = definitions.get(operands[1], ("unknown", []))
            summary["pointee_id"] = operands[1]
            summary["pointee_kind"] = nested
            if nested == "OpTypeSampledImage" and len(nested_operands) == 1:
                summary["image_type_id"] = nested_operands[0]
        return summary

    descriptors, interfaces = [], []
    for result_id, (opcode, operands) in definitions.items():
        if opcode != "OpVariable" or len(operands) < 2:
            continue
        decoration = decorations.get(result_id, {})
        record = {"id": result_id, "storage_class": operands[1],
                  "type": type_summary(operands[0]), **decoration}
        if "DescriptorSet" in decoration or "Binding" in decoration:
            descriptors.append(record)
        elif operands[1] in {"Input", "Output", "PushConstant"}:
            interfaces.append(record)
    markers = {"original_store": assembly.count("OpStore %504 %994") == 1,
               "original_function": assembly.count("%4 = OpFunction %void None %3") == 1,
               "glsl_import": assembly.count('%1 = OpExtInstImport "GLSL.std.450"') == 1}
    return {"instruction_count": instruction_count,
            "entry_points": entry_points[:MAX_RECORDS],
            "descriptors": descriptors[:MAX_RECORDS],
            "interfaces": interfaces[:MAX_RECORDS],
            "sample_operations": samples[:MAX_RECORDS],
            "truncated": any(len(records) > MAX_RECORDS for records in
                             (entry_points, descriptors, interfaces, samples)),
            "upstream_patch_markers": markers,
            "coordinate_mapping_proven": False,
            "decoded_frame_identity_proven": False}


def collect_icds(directories: list[Path]) -> list[dict[str, Any]]:
    records = []
    for directory in directories:
        if not directory.is_dir():
            continue
        for path in sorted(directory.glob("*.json")):
            if len(records) >= MAX_RECORDS:
                return records
            record: dict[str, Any] = {"file": path.name}
            try:
                value = json.loads(limited_read(path, MAX_METADATA_BYTES))
                driver = value.get("ICD", {})
                api_version = driver.get("api_version", "")
                library = driver.get("library_path", "")
                if isinstance(api_version, str) and re.fullmatch(r"\d+\.\d+\.\d+", api_version):
                    record["api_version"] = api_version
                if isinstance(library, str):
                    basename = Path(library).name
                    if re.fullmatch(r"[A-Za-z0-9_.+-]{1,128}", basename):
                        record["library"] = basename
            except (OSError, ValueError, AttributeError, CollectionError):
                record["status"] = "unreadable or unsupported metadata"
            records.append(record)
    return records


def os_metadata(path: Path) -> dict[str, str]:
    try:
        content = limited_read(path, MAX_METADATA_BYTES).decode("utf-8")
    except (OSError, UnicodeError, CollectionError):
        return {}
    result = {}
    for line in content.splitlines():
        key, sep, value = line.partition("=")
        if sep and key in {"ID", "VERSION_ID", "BUILD_ID", "VARIANT_ID"}:
            value = value.strip('"\'')
            if re.fullmatch(r"[A-Za-z0-9_.+-]{1,128}", value):
                result[key] = value
    return result


def ensure_private_output(path: Path) -> None:
    if path.is_symlink():
        raise CollectionError("Output directory must not be a symbolic link.")
    if path.exists() and (not path.is_dir() or any(path.iterdir())):
        raise CollectionError("Choose a new or empty output directory.")
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    path.chmod(0o700)
    (path / "private").mkdir(mode=0o700)
    # Even an output directory accidentally chosen within a checkout must not
    # encourage publishing disassembled proprietary runtime code.
    (path / ".gitignore").write_text("*\n", encoding="utf-8")


def collect(output: Path, steamvr_root: Path, *, vrlink: Path | None = None,
            icd_dirs: list[Path] | None = None, os_release: Path = Path("/etc/os-release")) -> dict[str, Any]:
    ensure_private_output(output)
    report: dict[str, Any] = {"schema": 1,
                             "scope": "read-only metadata; private shader assembly kept locally",
                             "os": os_metadata(os_release), "executables": {},
                             "build_metadata": {}, "warnings": []}
    for relative in ("bin/linuxarm64/vrcompositor", "bin/linuxarm64/vrserver",
                     "bin/linux64/vrcompositor", "bin/linux64/vrserver"):
        path = steamvr_root / relative
        if path.is_file():
            report["executables"][relative] = executable_metadata(path)
    if vrlink:
        report["executables"]["vrlink (explicit input)"] = executable_metadata(vrlink)
    for relative in ("version.txt", "bin/version.txt", "steamvr.version.txt"):
        path = steamvr_root / relative
        if path.is_file():
            # Hash-only: these locations vary by runtime build. Do not copy
            # arbitrary file content into a report if an unrecognized build
            # uses a different format here.
            report["build_metadata"][relative] = file_metadata(path, limit=MAX_METADATA_BYTES)
    report["vulkan_icds"] = collect_icds(icd_dirs or [Path("/etc/vulkan/icd.d"),
                                                     Path("/usr/share/vulkan/icd.d")])
    shader = steamvr_root / DEFAULT_SHADER
    report["shader"] = {"relative_path": DEFAULT_SHADER, **file_metadata(shader, limit=MAX_SHADER_BYTES)}
    if shader.is_file():
        try:
            shader_bytes = limited_read(shader, MAX_SHADER_BYTES)
            if len(shader_bytes) < 20 or len(shader_bytes) % 4:
                raise CollectionError("Shader size is not a complete SPIR-V module.")
            header = struct.unpack("<5I", shader_bytes[:20])
            if header[0] != 0x07230203:
                raise CollectionError("Shader is not a little-endian SPIR-V module.")
            report["shader"]["spirv_version"] = f"{header[1] >> 16}.{(header[1] >> 8) & 255}"
            tool = shutil.which("spirv-dis")
            if not tool:
                raise CollectionError("spirv-dis is missing; install spirv-tools on the Frame.")
            code, assembly = run_bounded([tool, str(shader)], output_limit=MAX_ASSEMBLY_BYTES)
            if code != 0:
                raise CollectionError("spirv-dis rejected the installed shader.")
            if file_metadata(shader, limit=MAX_SHADER_BYTES).get("sha256") != hashlib.sha256(shader_bytes).hexdigest():
                raise CollectionError("The installed shader changed during collection; retry after the update.")
            decoded = assembly.decode("utf-8")
            if not decoded.strip():
                raise CollectionError("spirv-dis returned no shader assembly.")
            private_path = output / "private" / "distort_ps_achromatic_se2.spvasm"
            private_path.write_text(decoded, encoding="utf-8")
            private_path.chmod(0o600)
            report["shader"]["interface"] = summarize_shader(decoded)
            report["shader"]["disassembly"] = "private/distort_ps_achromatic_se2.spvasm (do not publish)"
        except (OSError, UnicodeError, CollectionError) as error:
            report["warnings"].append(str(error) if isinstance(error, CollectionError)
                                      else "Shader diagnostics unavailable due to a read/tool failure.")
    else:
        report["warnings"].append("The expected installed SteamVR shader was not found.")
    report["bridge_status"] = {"ready_for_stream_mask_binding": False,
                               "missing": ["encoder/decoder surface identity correlation",
                                           "actual per-eye packing/foveation/reprojection mapping",
                                           "descriptor binding and GPU lifetime verification"]}
    (output / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (output / "report.md").write_text(
        "# Steam Frame mask bridge metadata\n\n"
        "Share `report.json` only. `private/` contains Valve shader disassembly; keep it on the headset.\n\n"
        "No settings, process memory, logs, camera frames or credentials were collected. "
        "Shader descriptors and sample coordinate IDs are evidence for investigation, "
        "not proof of a correct decoded-frame identity or coordinate mapping.\n", encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True, help="new or empty local directory")
    parser.add_argument("--steamvr-root", type=Path, default=Path("/opt/steamvr"))
    parser.add_argument("--vrlink", type=Path, help="optional known vrlink executable; metadata/hash only")
    parser.add_argument("--icd-dir", action="append", type=Path, help="optional ICD search directory")
    args = parser.parse_args()
    try:
        report = collect(args.output_dir, args.steamvr_root, vrlink=args.vrlink, icd_dirs=args.icd_dir)
    except (OSError, CollectionError) as error:
        parser.exit(1, (str(error) if isinstance(error, CollectionError)
                        else "Unable to create or read the diagnostic output.") + "\n")
    print("Collected local metadata. Share report.json only; keep private/ on the headset.")
    for warning in report["warnings"]:
        print("Warning:", warning)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
