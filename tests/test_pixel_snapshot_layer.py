#!/usr/bin/env python3
"""Run the production pixel layer through the real loader and software Vulkan.

Only owned smoke resources are created. There is no PS_DISTORT draw or selected
capture; an armed negative result must create no snapshot. All HOME/XDG/config
writes occur below a disposable private directory, never the user's profile.
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
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LAYER = "VK_LAYER_GHVR_pixel_snapshot"
EXPORTS = {"frame_pixel_vkGetInstanceProcAddr", "frame_pixel_vkGetDeviceProcAddr",
           "frame_pixel_vkNegotiateLoaderLayerInterfaceVersion"}
MAGIC = "GHVR_PIXEL_SNAPSHOT/1\n"
ARM = "GHVR_PIXEL_SNAPSHOT_ARM/1\n"
CAPTURE_ID = "0123456789abcdef0123456789abcdef"
ORIGINAL_CASES = ("unrelated_valid_config", "inert_missing", "inert_permissions", "inert_contents",
                  "inert_config_parent_symlink", "inert_config_file_symlink", "inert_output_symlink",
                  "inert_output_permissions", "inert_config_hardlink", "implicit_unarmed_gpa",
                  "implicit_armed_direct", "implicit_armed_mixed", "explicit_armed_gpa",
                  "implicit_disable_environment")
STATISTICS_CASES = ("implicit_statistics_query_enabled", "implicit_statistics_query_indexed")


def execute(command, env=None):
    child = subprocess.Popen(command, env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, start_new_session=True)
    try:
        stdout, stderr = child.communicate(timeout=20)
    except BaseException:
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        child.communicate()
        raise
    assert len(stdout) <= 16384 and len(stderr) <= 65536, "Unbounded fixture output"
    return {"command": [str(x) for x in command], "exit_code": child.returncode,
            "stdout": stdout, "stderr": stderr}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def smoke(result, statistics=False):
    assert result["exit_code"] == 0, result
    line = next(x for x in result["stdout"].splitlines() if x.startswith("RESULT="))
    facts = json.loads(line[len("RESULT="):])
    if statistics and facts.get("statistics_skip"):
        assert facts["statistics_skip"] in ("pipelineStatisticsQuery unsupported", "VK_EXT_transform_feedback unavailable"), facts
        assert facts["statistics_feature_requested"] is False and facts["teardown"] is True, facts
        assert facts["matched_capture"] is False and facts["native_frame"] is False and facts["mask_enabled"] is False
        return facts
    for key in ("query_pool", "query_not_ready_native", "image_bind_view", "parallel_image_memory_view", "command_submit_reset", "teardown"):
        assert facts[key] is True, (key, facts)
    assert facts["parallel_lifecycles"] == 72, facts
    assert facts["checks"] >= 80, facts
    assert facts["matched_capture"] is False and facts["native_frame"] is False and facts["mask_enabled"] is False
    return facts


def private_directory(path):
    path.mkdir(parents=True, exist_ok=True)
    path.chmod(0o700)
    return path


def private_file(path, text):
    path.write_text(text)
    path.chmod(0o600)
    return path


def inventory(path):
    return {p.name: {"bytes": p.stat().st_size, "sha256": digest(p)}
            for p in path.iterdir() if p.is_file() and not p.is_symlink()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("production_layer", type=Path)
    parser.add_argument("production_manifest", type=Path)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--retention-checkout", type=Path,
                        help="Optional checkout containing scripts/town_run_retention.py")
    parser.add_argument("--ssbo-definitions", action="store_true",
                        help="Expect the explicitly enabled cold definition observer; no pipeline override")
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--case", action="append", choices=ORIGINAL_CASES + STATISTICS_CASES,
                           help="Run only this named case; may be repeated")
    selection.add_argument("--statistics-only", action="store_true",
                           help="Run only enabled pipeline-statistics/query-wrapper controls")
    args = parser.parse_args()
    if args.case and len(set(args.case)) != len(args.case):
        parser.error("A selected case must not be repeated")
    selected = set(args.case or (STATISTICS_CASES if args.statistics_only else ORIGINAL_CASES))
    layer = args.production_layer.resolve(strict=True)
    fixture = args.fixture.resolve(strict=True)
    manifest = json.loads(args.production_manifest.read_text())
    assert manifest["layer"]["name"] == LAYER
    assert "enable_environment" not in manifest["layer"], "Implicit selection must not need an enable variable"
    assert set(manifest["layer"]["functions"].values()) == EXPORTS
    actual_exports = execute(["nm", "-D", "--defined-only", str(layer)])
    assert actual_exports["exit_code"] == 0
    assert {s.split()[-1] for s in actual_exports["stdout"].splitlines() if s.strip()} == EXPORTS
    icd = next((Path("/usr/share/vulkan/icd.d") / n for n in ("lvp_icd.json", "lvp_icd.x86_64.json")
                if (Path("/usr/share/vulkan/icd.d") / n).is_file()), None)
    assert icd is not None, "Actual Mesa lavapipe is required; no mocked dispatch"
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    report = {"schema": 1, "kind": "production_pixel_layer_real_loader_smoke",
              "production_layer": str(layer), "production_layer_sha256": digest(layer),
              "source_sha256": {str(p.relative_to(ROOT)): digest(p) for p in
                                (ROOT / "tests/test_pixel_snapshot_layer.cpp", Path(__file__).resolve())},
              "fixture_sha256": digest(fixture), "forced_icd": str(icd),
              "private_exports": sorted(EXPORTS), "cases": {},
              "native_loader_and_gpu": True, "matched_capture_tested": False,
              "native_ps_distort_tested": False, "headset_tested": False,
              "decoded_pixel_carriage_proved": False, "passthrough_authorized": False}
    status = 1
    try:
        with tempfile.TemporaryDirectory(prefix="frame-pixel-loader-") as temporary:
            temporary = Path(temporary)
            program = temporary / "vrcompositor"
            unrelated = temporary / "owned_vulkan_smoke"
            shutil.copy2(fixture, program)
            shutil.copy2(fixture, unrelated)

            def case(label, *, mode="gpa", invalid=None, armed=False,
                     enabled=True, explicit=False, unrelated_exe=False):
                if label not in selected:
                    return
                statistics = mode in ("statistics", "statistics-indexed")
                folder = private_directory(temporary / label)
                profile = private_directory(folder / "profile")
                config_parent = private_directory(folder / "setup")
                output = private_directory(folder / "run")
                config = private_file(config_parent / "session.conf", MAGIC + CAPTURE_ID + "\n" + str(output) + "\n")
                if armed:
                    private_file(output / "capture.arm", ARM)
                if invalid == "missing":
                    config.unlink()
                elif invalid == "permissions":
                    config.chmod(0o644)
                elif invalid == "contents":
                    private_file(config, "NOT_A_PIXEL_SNAPSHOT_CONFIGURATION\n")
                elif invalid == "config_parent_symlink":
                    alias = folder / "setup-link"
                    alias.symlink_to(config_parent, target_is_directory=True)
                    config = alias / "session.conf"
                elif invalid == "config_file_symlink":
                    alias = config_parent / "session-link.conf"
                    alias.symlink_to(config)
                    config = alias
                elif invalid == "output_symlink":
                    alias = folder / "output-link"
                    alias.symlink_to(output, target_is_directory=True)
                    private_file(config, MAGIC + CAPTURE_ID + "\n" + str(alias) + "\n")
                elif invalid == "output_permissions":
                    output.chmod(0o755)
                elif invalid == "config_hardlink":
                    os.link(config, config_parent / "second-link")
                env = os.environ.copy()
                for key in list(env):
                    if key.startswith(("VK_", "GHVR_PIXEL_", "FRAME_MASK_TRACE")) or key in ("LD_PRELOAD", "LD_AUDIT"):
                        env.pop(key)
                env.update({"HOME": str(profile), "XDG_DATA_HOME": str(profile / "data"),
                            "XDG_CONFIG_HOME": str(profile / "config"),
                            "XDG_DATA_DIRS": str(profile / "empty-data"),
                            "XDG_CONFIG_DIRS": str(profile / "empty-config"),
                            "XDG_CACHE_HOME": str(profile / "cache"),
                            "VK_ICD_FILENAMES": str(icd), "VK_LOADER_DEBUG": "error,layer",
                            "GHVR_PIXEL_SNAPSHOT_CONFIG": str(config)})
                layer_dir = profile / ("explicit" if explicit else "data/vulkan/implicit_layer.d")
                layer_dir.mkdir(parents=True)
                selected_manifest = json.loads(json.dumps(manifest))
                selected_manifest["layer"]["library_path"] = str(layer)
                (layer_dir / "owned-pixel.json").write_text(json.dumps(selected_manifest))
                if explicit:
                    env["VK_LAYER_PATH"] = str(layer_dir)
                    env["VK_INSTANCE_LAYERS"] = LAYER
                if not enabled:
                    env["GHVR_PIXEL_SNAPSHOT_DISABLE"] = "1"
                active = invalid is None and enabled and not unrelated_exe
                command = [str(unrelated if unrelated_exe else program), mode, str(layer)]
                if active:
                    command += [str(output / "status.jsonl"), '"event":"armed"' if armed else '"event":"prepared"']
                result = execute(command, env)
                # Preserve the actual native facts/status before assertions so
                # an older-library negative control exposes its causal refusal.
                report["cases"][label] = result
                result["facts"] = smoke(result, statistics)
                result["loader_selected_production_library"] = str(layer) in result["stderr"]
                if enabled:
                    assert result["loader_selected_production_library"], (label, result)
                before = inventory(output)
                result["output_inventory"] = before
                if statistics and result["facts"].get("statistics_skip"):
                    assert before == {}, (label, before)
                    result["status"] = []
                elif active:
                    assert set(before) == ({"capture.arm", "launch.claim", "status.jsonl"} if armed else {"launch.claim", "status.jsonl"}), (label, before)
                    records = [json.loads(line) for line in (output / "status.jsonl").read_text().splitlines()]
                    result["status"] = records
                    assert records and records[0]["event"] == "prepared", (label, records)
                    definitions = [r for r in records if r["event"].startswith("ssbo_definition_")]
                    if args.ssbo_definitions:
                        features = [r for r in definitions if r["event"] == "ssbo_definition_feature"]
                        assert len(features) == 1, (label, definitions)
                        feature = features[0]
                        # This smoke deliberately never enables fragment stores.
                        # It exercises refusal/forwarding, not the private pins.
                        assert feature["initialized"] is True and feature["observed"] is statistics, (label, feature)
                        assert feature["enabled"] is False and feature["raw"] == 0, (label, feature)
                        assert feature["contract_reason"] == ("feature_disabled" if statistics else "feature_absent"), (label, feature)
                        for key in ("contract_index", "contract_structure_type"):
                            assert type(feature[key]) is int and 0 <= feature[key] <= 0xffffffff, (label, feature)
                        assert feature["native_compatibility_proven"] is False, (label, feature)
                        shapes = [r for r in definitions if r["event"] == "ssbo_definition_device_shape"]
                        assert len(shapes) == 1 and shapes[0]["observed"] is True, (label, shapes)
                        assert shapes[0]["chain_end"] == "complete" and len(shapes[0]["chain_types"]) <= 32, (label, shapes)
                        assert shapes[0]["clone_admission_changed"] is False, (label, shapes)
                        assert all(r["event"] in ("ssbo_definition_feature", "ssbo_definition_refusals", "ssbo_definition_device_shape")
                                   and r["mask_enabled"] is False for r in definitions), (label, definitions)
                        assert len(definitions) <= 4, (label, definitions)
                    else:
                        assert not definitions, (label, "unexpected optional definition observer", definitions)
                    records = [r for r in records if not r["event"].startswith("ssbo_definition_")]
                    if statistics:
                        facts, prepared = result["facts"], records[0]
                        for key in ("statistics_feature_supported", "statistics_feature_requested", "statistics_feature_enabled",
                                    "statistics_core_query", "statistics_empty_counter_zero", "statistics_queries_available"):
                            assert facts[key] is True, (label, key, facts)
                        indexed = mode == "statistics-indexed"
                        assert facts["statistics_indexed_query"] is indexed, (label, facts)
                        assert facts["other_core_features_enabled"] == 0 and facts["device_extension_count"] == int(indexed), (label, facts)
                        # Check admission before new status fields: the b16
                        # negative must fail here, not on a missing new field.
                        assert prepared["device_admission_complete"] is True, (label, "enabled statistics admission refused", prepared)
                        for key in ("pipeline_statistics_query_observed", "pipeline_statistics_query_enabled", "query_routes_observed"):
                            assert prepared[key] is True, (label, key, prepared)
                        assert prepared["device_admission_reason"] == "none", (label, prepared)
                        # YCbCr is intentionally not requested. Host profile
                        # unavailability is separate from this query gate.
                        assert prepared["sampler_prepare_attempted"] is False, (label, prepared)
                        history = records[1:]
                        assert 1 <= len(history) <= 2, (label, records)
                        masks = []
                        for event in history:
                            assert event["event"] == "sampler_query_history_observed" and event["prior_query_blocks_sampling"] is True, (label, event)
                            mask = event["query_routes_used"]
                            assert type(mask) is int and mask in (1, 2, 3), (label, event)
                            assert not masks or masks[-1] != mask and masks[-1] & mask == masks[-1], (label, history)
                            masks.append(mask)
                        assert masks[-1] == (3 if indexed else 1), (label, history)
                    else:
                        assert [r["event"] for r in records] == (["prepared", "armed"] if armed else ["prepared"]), (label, records)
                    assert all(r["mask_enabled"] is False and r["native_frame_association_proven"] is False for r in records)
                    assert (output / "launch.claim").read_text() == "GHVR_PIXEL_CLAIM/1\n"
                    if armed:
                        # A second process with the very same valid config and
                        # arm must not claim or overwrite an earlier run.
                        second = execute([str(program), mode, str(layer)], env)
                        second["facts"] = smoke(second)
                        assert inventory(output) == before, (label, "stale claim changed output")
                        result["stale_claim_second_process"] = second
                else:
                    assert set(before) == ({"capture.arm"} if armed else set()), (label, before)
                assert not any((output / name).exists() for name in ("snapshot.p010", "snapshot.json", "sampler-components.rgba8")), (label, "unexpected unmatched snapshot")

            case("unrelated_valid_config", unrelated_exe=True, armed=True)
            for invalid in ("missing", "permissions", "contents", "config_parent_symlink",
                            "config_file_symlink", "output_symlink", "output_permissions", "config_hardlink"):
                case("inert_" + invalid, invalid=invalid, armed=True)
            case("implicit_unarmed_gpa")
            case("implicit_armed_direct", mode="direct", armed=True)
            case("implicit_armed_mixed", mode="mixed", armed=True)
            case("explicit_armed_gpa", armed=True, explicit=True)
            case("implicit_disable_environment", armed=True, enabled=False)
            case("implicit_statistics_query_enabled", mode="statistics")
            case("implicit_statistics_query_indexed", mode="statistics-indexed")
            assert set(report["cases"]) == selected, "Selected case not executed"
            assert digest(layer) == report["production_layer_sha256"], "Production library changed during this evidence run"
        report["private_temporary_outputs_removed"] = True
        report["user_home_files_written"] = False
        status = 0
    finally:
        report["exit_code"] = status
        if args.receipt:
            args.receipt.parent.mkdir(parents=True, exist_ok=True)
            args.receipt.write_text(json.dumps(report, indent=2) + "\n")
            if args.retention_checkout:
                sys.path.insert(0, str(args.retention_checkout / "scripts"))
                from town_run_retention import cleanup_completed_run
                log = args.receipt.with_name("completion.log")
                log.write_text("evidence: " + str(args.receipt.parent) + "\n")
                cleanup = cleanup_completed_run("pixel-snapshot-layer", status, log,
                                                args.retention_checkout, process_reaped=True)
                args.receipt.with_name("cleanup.json").write_text(json.dumps(cleanup, indent=2) + "\n")
    print("PASS: " + str(len(report["cases"])) + " production-layer real Vulkan loader cases; "
          "native query/image/command lifecycles preserved; no matched capture or passthrough proof")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
