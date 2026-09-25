#!/usr/bin/env python3
"""Phase 8A self-test for the weight-aware manifest generator.

Covers (without a binary): weighted run-identity determinism / non-collision and the
capability filter. Optionally (when build_gpp/firebreak_cpp exists): a small end-to-end
manifest generation against the new20x20 / new20x20_reburn pair verifying weight columns,
run-id uniqueness, distinct per-profile hashes, and the shared canonical landscape id.

Usage: python3 scripts/test_weight_manifest_generation.py
"""

from __future__ import annotations

import csv
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parent.parent
GEN_PATH = ROOT / "scripts" / "generate_fpp_new_instances_scaling_manifests.py"


def load_generator_module():
    spec = importlib.util.spec_from_file_location("weight_manifest_gen", GEN_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_capability_filter(gen) -> None:
    # Standard methods supported on all three profiles and objectives.
    for method in ["FPP-SAA", "FPP-Branch-Benders-RootCuts", "FPP-SAA-CVaR",
                   "FPP-Branch-Benders-Combinatorial"]:
        for profile in ["homogeneous", "heterogeneous", "clustered"]:
            ok, _ = gen.weighted_method_supported(method, profile, "expected")
            assert ok, (method, profile)

    # Restricted + combinatorial is never supported.
    ok, reason = gen.weighted_method_supported(
        "FPP-Restricted-Branch-Benders-Combinatorial", "homogeneous", "expected")
    assert not ok and "Restricted-candidate combinatorial" in reason

    # Non-homogeneous combinatorial + LLBI unsupported; homogeneous allowed.
    ok, _ = gen.weighted_method_supported(
        "FPP-Branch-Benders-Combinatorial-CoverageLLBI", "clustered", "expected")
    assert not ok
    ok, _ = gen.weighted_method_supported(
        "FPP-Branch-Benders-Combinatorial-CoverageLLBI", "homogeneous", "expected")
    assert ok
    print("capability filter: OK")


def test_run_identity(gen) -> None:
    entry_a = {"weight_map_hash": "fnv1a64:1111111122223333"}
    entry_b = {"weight_map_hash": "fnv1a64:aaaaaaaabbbbcccc"}
    base = "new20x20_case00_train100_test1000_alpha0p02_fpp_saa"

    id_a = gen.weighted_run_id(base, entry_a, "heterogeneous", 0)
    # Deterministic.
    assert id_a == gen.weighted_run_id(base, entry_a, "heterogeneous", 0)
    # Different hash / profile / replicate -> different id.
    assert id_a != gen.weighted_run_id(base, entry_b, "heterogeneous", 0)
    assert id_a != gen.weighted_run_id(base, entry_a, "clustered", 0)
    assert id_a != gen.weighted_run_id(base, entry_a, "heterogeneous", 1)
    # Legacy (no entry) keeps the base id and cannot collide with a weighted id.
    legacy = gen.weighted_run_id(base, None, "homogeneous", 0)
    assert legacy == base
    assert legacy != id_a
    print("run identity: OK")


def test_filtered_legacy_pair_resolution(gen) -> None:
    """A reburn partner is evaluation metadata, never an optimization filter member.

    This is deliberately a no-binary/no-registry unit test so the legacy homogeneous
    campaign path cannot accidentally regress behind weighted-registry setup.
    """
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        instance_config = tmp_path / "instances.csv"
        instance_config.write_text(
            "instance_id,folder_name,landscape,forest_path,results_path,declared_cells,instance_type,enabled,requires_strict_metadata\n"
            "new20x20,20x20,new20x20,new_instances/20x20,new_instances/20x20,400,shortest_path,true,true\n"
            "new20x20_reburn,20x20_reburn,new20x20_reburn,new_instances/20x20_reburn,new_instances/20x20_reburn,400,reburn,true,true\n",
            encoding="utf-8",
        )
        args = SimpleNamespace(
            instance_config=instance_config,
            instance_filter="new20x20",
            preflight_csv=None,
            instances_root=Path("new_instances"),
            output_dir=tmp_path / "output",
            train_counts="100",
            alphas="0.02",
            num_cases=1,
            test_count=1000,
            seed_base=20260529,
            time_limit="1800",
            mip_gap="0.001",
            threads=1,
            cvar_beta="0.9",
            mean_cvar_lambda="0.5",
            projected_llbi_root_rounds=100,
            projected_llbi_max_cuts_per_round=100,
            projected_llbi_violation_tolerance="1e-3",
            projected_llbi_cut_density_limit="0",
            projected_poly_max_cuts="100000",
            training_pool_min=1,
            training_pool_max=9000,
            test_pool_min=9001,
            test_pool_max=10000,
            weight_profiles="homogeneous",
            weight_replicates="0",
            weight_registry=None,
            paired_reburn_evaluation=True,
            no_capability_filter=False,
        )

        enabled = gen.enabled_instance_rows(args)
        selected = gen.selected_instances(args, enabled)
        gen.validate_requested_reburn_pairs(args, selected, enabled)
        assert {row["instance_id"] for row in enabled} == {"new20x20", "new20x20_reburn"}
        assert [row["instance_id"] for row in selected] == ["new20x20"]

        split_dir = tmp_path / "splits"
        split_index = {
            ("new20x20", 100, 0): (
                split_dir / "train.csv",
                split_dir / "test.csv",
                gen.split_seed(20260529, 0, 0, 100),
                list(range(1, 101)),
                list(range(9001, 10001)),
            )
        }
        rows, filtered = gen.build_rows(
            args,
            ["FPP-SAA"],
            selected,
            split_dir,
            split_index,
            enabled_instance_ids={row["instance_id"] for row in enabled},
        )
        assert filtered == 0
        assert len(rows) == 1
        row = rows[0]
        assert row["instance_id"] == "new20x20"
        assert row["paired_reburn_instance_id"] == "new20x20_reburn"
        assert row["paired_evaluation_enabled"] == "true"
        assert row["weight_profile"] == "homogeneous"
        assert row["weight_map_path"] == ""
        assert row["instance_config_path"] == str(instance_config)
        assert not any(item["instance_id"] == "new20x20_reburn" for item in rows)

        # Optional chunking can place one method in each worker, which is used by
        # the parallel one-case manuscript sanity run.  The default above remains
        # the legacy one-worker-per-controlled-block behavior.
        args.methods_per_worker = 1
        split_rows, split_filtered = gen.build_rows(
            args,
            ["FPP-SAA", "FPP-SAA-CVaR"],
            selected,
            split_dir,
            split_index,
            enabled_instance_ids={item["instance_id"] for item in enabled},
        )
        assert split_filtered == 0
        assert len(split_rows) == 2
        assert len({item["worker_id"] for item in split_rows}) == 2
        assert {item["task_id"].rsplit("_", 1)[-1] for item in split_rows} == {"000"}

        mismatched = [dict(item) for item in enabled]
        next(item for item in mismatched if item["instance_id"] == "new20x20_reburn")["declared_cells"] = "401"
        try:
            gen.validate_requested_reburn_pairs(args, selected, mismatched)
        except RuntimeError as exc:
            assert "cell-count mismatch" in str(exc)
        else:
            raise AssertionError("paired cell-count mismatch was not rejected")
    print("filtered legacy paired-reburn resolution: OK")


def test_end_to_end(gen) -> None:
    binary = ROOT / "build_gpp" / "firebreak_cpp"
    if not binary.exists():
        print("end-to-end: SKIPPED (build_gpp/firebreak_cpp not built)")
        return
    with tempfile.TemporaryDirectory() as tmp:
        out_dir = Path(tmp) / "manifest"
        registry = Path(tmp) / "weight_maps"
        cmd = [
            sys.executable, str(GEN_PATH),
            "--output-dir", str(out_dir),
            "--instance-filter", "new20x20,new20x20_reburn",
            "--train-counts", "8",
            "--alphas", "0.02",
            "--num-cases", "1",
            "--test-count", "4",
            "--training-pool-min", "1", "--training-pool-max", "20",
            "--test-pool-min", "21", "--test-pool-max", "24",
            "--method-file", str(ROOT / "config" / "fpp_new_instances_scaling_methods.txt"),
            "--weight-profiles", "homogeneous,heterogeneous,clustered",
            "--weight-replicates", "0",
            "--weight-registry", str(registry),
            "--generate-missing-weight-maps",
            "--binary", str(binary),
            "--paired-reburn-evaluation",
        ]
        subprocess.run(cmd, check=True, cwd=str(ROOT), capture_output=True, text=True)

        full = out_dir / "manifests" / "full_task_manifest.csv"
        with full.open(newline="", encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))
        assert rows, "no rows generated"

        run_ids = [row["run_id"] for row in rows]
        assert len(set(run_ids)) == len(run_ids), "duplicate run_id"

        # Every row carries weight metadata and a resolvable map path.
        hashes_by_profile: dict[str, set] = {}
        canonical_by_family: dict[str, set] = {}
        for row in rows:
            assert row["weight_profile"] in {"homogeneous", "heterogeneous", "clustered"}
            assert row["weight_map_hash"], "empty weight_map_hash"
            assert Path(row["weight_map_path"]).exists(), row["weight_map_path"]
            assert row["canonical_landscape_id"], "empty canonical_landscape_id"
            hashes_by_profile.setdefault(row["weight_profile"], set()).add(row["weight_map_hash"])
            fam = row["instance_id"].replace("_reburn", "")
            canonical_by_family.setdefault(fam, set()).add(row["canonical_landscape_id"])

        # Distinct hashes across the three profiles.
        all_hashes = {h for hs in hashes_by_profile.values() for h in hs}
        assert len(all_hashes) >= 3, f"expected >=3 distinct hashes, got {all_hashes}"

        # Reduced and reburn share exactly one canonical landscape id per family.
        for fam, ids in canonical_by_family.items():
            assert len(ids) == 1, f"family {fam} has multiple canonical ids: {ids}"

        # Verify-only pass succeeds on the generated manifest.
        subprocess.run(
            [sys.executable, str(GEN_PATH), "--output-dir", str(out_dir), "--verify-only"],
            check=True, cwd=str(ROOT), capture_output=True, text=True)
    print("end-to-end: OK")


def main() -> int:
    gen = load_generator_module()
    test_capability_filter(gen)
    test_run_identity(gen)
    test_filtered_legacy_pair_resolution(gen)
    test_end_to_end(gen)
    print("All weight manifest generation self-tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
