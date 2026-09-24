# Weighted Landscapes — Phase 8A

Canonical weight-map registry, physical landscape identity, paired reduced/reburn
mapping, deterministic seed derivation, capability matrix, manifest/run-identity/worker
contract. Phase 8A builds **infrastructure only**: it does not launch large experiments,
does not reimplement any completed weighted solver, and does not touch the mathematical
FPP/DPV formulations.

The central guarantee: **every method operating on the same physical landscape uses
exactly the same canonical weight map**, identical across methods, objectives, risk
parameters, scenario counts, train/test splits, solver seeds, worker processes, and
reduced/reburn evaluations. Map generation is independent of the method and of any
experiment row.

---

## 1. Audit of the existing experiment architecture

### 1.1 Weight infrastructure that already exists (do not reimplement)

- `core::LandscapeWeightMap` (`include/core/LandscapeWeightMap.hpp`,
  `src/core/LandscapeWeightMap.cpp`): weight-by-original-cell-id maps, statistics, an
  FNV-1a-64 `deterministic_hash` over the sorted `(cell_id, final_weight)` list
  (`fnv1a64:...`), strict CSV load/write (`cell_id,raw_weight,normalized_weight,cluster_id`),
  `validate_landscape_weight_map` (optionally against an exact expected original-ID set),
  and `build_compact_weight_vector(map, index_mapper)`.
- `core::LandscapeWeightGenerator` (`include/core/LandscapeWeightGenerator.hpp`,
  `src/core/LandscapeWeightGenerator.cpp`): `LandscapeCellUniverse` (source, rows, cols,
  `WeightedLandscapeCell{original_cell_id,row,column}`), `LandscapeWeightGenerationConfig`,
  homogeneous / heterogeneous / clustered generation, mean-one normalization,
  `generator_version = 1`, deterministic `std::mt19937_64` seeded generation, and
  `write_landscape_weight_generation_metadata_json`.
- `experiments::WeightMapGenerationRunner`
  (`src/experiments/WeightMapGenerationRunner.cpp`): builds a `LandscapeCellUniverse`
  from `io::Cell2FireReader::ForestInfo` (using `available_nodes` when known, else all
  IDs `1..n_cells`), generates a map, writes `weights.csv` + `metadata.json`. This is the
  only code that materializes the **full physical** universe with grid coordinates.
- CLI: `firebreak_cpp generate-weight-map` and `--weight-map-file` on the `evaluate`,
  `solve-fpp-saa`, and every `run-*-oos` command.
- Result schema: `io::StandardExperimentResult` already carries `weight_profile`,
  `weight_map_file`, `weight_map_hash`, `weight_normalized`, `weight_mean/min/max/total`,
  per-component `*_weight_map_hash` fields, and (uncommitted Phase 7B2) the `dpv_*`
  weighted block. `train_ids` / `test_ids` are persisted in both result JSON and CSV.

### 1.2 The gap Phase 8A closes

- Neither the Python manifest generator
  (`scripts/generate_fpp_new_instances_scaling_manifests.py`) nor its worker
  (`scripts/run_fpp_new_instances_scaling_manifest_worker.py`) emit or consume any weight
  column; the worker's existing paired-reburn `evaluate` call passes **no**
  `--weight-map-file`, so reburn evaluation silently used the default homogeneous map.
- The C++ batch path (`BatchExperimentConfig` → `BatchExperimentRunner` →
  `MethodDispatcher`) never populates `MethodDispatchRequest::weight_map_file` (the field
  exists but is left empty).
- There was no canonical *physical landscape identity*, no *registry* keyed on that
  identity, no *deterministic generation-seed derivation*, no *reduced/reburn mapping
  validation*, and no *machine-readable capability matrix*.

### 1.3 Instance / pairing facts (from `config/fpp_new_instances_scaling_instances.csv`)

| instance_id | folder | landscape | cells | instance_type |
|---|---|---|---|---|
| new20x20 | 20x20 | new20x20 | 400 | shortest_path |
| new20x20_reburn | 20x20_reburn | new20x20_reburn | 400 | reburn |
| new40x40 / _reburn | 40x40[_reburn] | … | 1600 | shortest_path / reburn |
| new100x100 / _reburn | 100x100[_reburn] | … | 10000 | shortest_path / reburn |

- Pairing is by `instance_id` + `_reburn` suffix (Python `paired_reburn_instance_id`).
- `new_instances/*` have no `fuels.asc` / `fbp_lookup_table.csv`; they load as
  `Cell2FireLayout::NewInstances` with `available_nodes = 1..NCells`, size inferred from
  `run.log` (`InstanceTif: …/instance_20x20.tif`) or the folder name. Messages use
  original Cell2Fire IDs directly (e.g. `201 -> 221` on a 20-wide grid).
- **Key consequence:** for these instances the physical universe (`1..NCells`), the
  full eligible set, and the compact `IndexMapper` universe all coincide. A single
  canonical CSV over `1..NCells` is therefore accepted verbatim by
  `solver::attach_weight_map_to_optimization_instance` (which validates against
  `node_mapper.original_nodes()` with exact equality) for **both** the reduced and reburn
  members of a pair. The shared-cell weight invariant holds *by construction* — both
  members read the same physical CSV keyed by original ID.

---

## 2. Canonical physical landscape identity

Implemented in `core::CanonicalLandscape` (`include/core/CanonicalLandscape.hpp`,
`src/core/CanonicalLandscape.cpp`). Identity is derived from stable physical metadata
and is independent of method, risk objective, scenario count, train/test split, compact
node indices, and result directory.

`CanonicalLandscapeIdentity`:

- `landscape_family` — the instance id with a trailing `_reburn` removed
  (`landscape_family_from_instance`); `new20x20` and `new20x20_reburn` share the family
  `new20x20`.
- `base_landscape_id` — equals `landscape_family` (the reduced/base member name).
- `grid_rows`, `grid_cols`, `cell_count`.
- `universe_hash` — FNV-1a-64 over the sorted physical universe: for every cell, the
  bytes of `original_cell_id "," row "," column "\n"`, prefixed with `rows "x" cols`.
  Independent of iteration order and of any scenario sample.
- `canonical_landscape_id` — `"<family>__<rows>x<cols>__<universe_hash_suffix>"`
  (the 16 hex digits of the universe hash). Deterministic and collision-resistant across
  distinct physical universes.
- Pairing: `reduced_instance_id`, `reburn_instance_id` (`<family>_reburn`),
  `paired_landscape_id`.

Because both pair members produce the **same** physical universe (same `NCells`, same
grid), they resolve to the **same** `canonical_landscape_id` and the same `universe_hash`.

### 2.1 Canonical physical universe

`LandscapeCellUniverse` is built over the complete physical landscape (original Cell2Fire
IDs `1..NCells`, or `available_nodes` when fuel data is present), including cells absent
from some scenario graphs and non-candidate cells. `validate_landscape_cell_universe`
already rejects duplicate original IDs and out-of-range coordinates. Universes are never
built from the first training scenario, the reduced compact graph, candidate-only nodes,
or currently-burned cells. `experiments::build_landscape_cell_universe(forest_info)`
factors the construction previously inlined in `WeightMapGenerationRunner`.

### 2.2 Deterministic seed derivation

`derive_weight_generation_seed(global_weight_seed, canonical_landscape_id,
weight_profile, weight_replicate, generator_version)` folds all inputs into an FNV-1a-64
digest (a stable, explicit, process-independent algorithm — never `std::hash`,
`std::random_device`, Python `hash()`, or wall-clock). Repeated processes derive the same
seed; different profile/replicate/landscape/version give (with overwhelming probability)
distinct seeds. `homogeneous` seeds are derived identically but are unused by the
homogeneous generator (which is deterministic regardless).

### 2.3 Run identity

`weighted_run_identity(RunIdentityInputs)` returns a deterministic FNV-1a-64 run key over
canonical landscape, instance, method, objective, risk parameters, scenario count,
alpha/budget, a hash of the sorted train IDs, weight profile, weight replicate, and
weight-map hash. Identical logical inputs → identical id; any weight-config change → a
different id; legacy homogeneous rows (empty weight fields) cannot collide with weighted
rows because the profile/replicate/hash components differ.

---

## 3. Weight-map registry

Implemented in `experiments::WeightMapRegistry`
(`include/experiments/WeightMapRegistry.hpp`, `src/experiments/WeightMapRegistry.cpp`).

### 3.1 Logical identity and directory layout

Logical key: `(canonical_landscape_id, weight_profile, weight_replicate,
weight_generator_version)`. Layout under a registry root (default `weight_maps/`):

```
weight_maps/
  <canonical_landscape_id>/
    <profile>/
      replicate_<r>/
        weights.csv       # canonical map over the full physical universe
        metadata.json     # the authoritative registry record
```

No method or objective ever appears in a map path. The per-entry `metadata.json` is the
registry record; lookup is a deterministic path computation, so there is no mutable global
index to corrupt.

### 3.2 Registry record (`metadata.json`)

`canonical_landscape_id`, `weight_profile`, `weight_replicate`, `weight_generation_seed`,
`weight_generator_version`, `weight_map_path` (relative), `weight_map_hash`,
`source_universe_hash`, `cell_count`, `normalization_mode` (`mean_one`), `mean_weight`,
`minimum_weight`, `maximum_weight`, `cluster_count`, `cluster_fraction`,
`cluster_multiplier` (clustered `cluster_max`), `background_multiplier`
(clustered `background_max`), and a `generation_parameters` block (the full
`LandscapeWeightGenerationConfig` used).

### 3.3 Idempotent generation and validation

`WeightMapRegistry::ensure(request)`:

1. Compute the logical path.
2. If `metadata.json` **and** `weights.csv` exist: load the record, re-derive the
   expected seed, and require that the stored `weight_generator_version`,
   `weight_generation_seed`, `source_universe_hash`, and every field of
   `generation_parameters` match the request. Reload `weights.csv` and require its hash to
   equal the stored `weight_map_hash`. Any discrepancy is a hard error (**no silent
   overwrite, no regeneration**). Return the validated record.
3. Else, if `allow_generate` is set: derive the seed, generate the map over the physical
   universe, write `weights.csv` and `metadata.json` to temporary files and `rename`
   atomically, then return the record.
4. Else (map missing, generation not allowed — the worker contract): hard error.

`WeightMapRegistry::load(...)` is the read-only worker entry point: it is exactly step 2
with generation disabled.

### 3.4 Homogeneous policy

`weight_profile = homogeneous` is a first-class registry entry with a **real physical
map file** (all weights `1.0` over the full universe) and a real deterministic hash. There
is no ambiguous "empty hash" homogeneous state. Legacy manifests that carry no weight
fields resolve to homogeneous behavior at the worker with an explicit
`weight_resolution = legacy_homogeneous` marker; new manifests always name
`weight_profile = homogeneous` explicitly with the registry hash.

### 3.5 Concurrency policy

Phase 8A uses **single-process pre-generation, read-only workers**
(`ensure-weight-map` before launch; workers call `load`). Writes are still atomic
(temp-file + `rename`) so a partially written entry is never observed, but concurrent
generation of the *same* logical entry is not a supported workflow — pre-generate first.

---

## 4. Reduced / reburn mapping

Implemented in `experiments::PairedInstanceWeightMapping`
(`include/experiments/PairedInstanceWeightMapping.hpp`, `.cpp`).

`map_weight_map_to_instance(map, index_mapper)` builds the compact weight vector for an
instance and reports `mapped_count`, `missing_count`, `missing_original_ids`,
`mapping_method` (`original_cell_id`), and a `mapping_hash` (FNV-1a-64 over the emitted
`(compact_index, original_id, weight)` triples). A missing original ID is a hard error.

`compare_reduced_reburn(map, reduced_original_ids, reburn_original_ids)` returns the
mandated report: `canonical_cell_count`, `reduced_cell_count`, `reburn_cell_count`,
`shared_cell_count`, `reduced_mapped_count`, `reburn_mapped_count`,
`reduced_missing_count`, `reburn_missing_count`, `duplicate_original_id_count`,
`shared_weight_mismatch_count`, `mapping_method`, `mapping_hash`. The invariant
`shared_weight_mismatch_count == 0` is checked (and holds by construction, since both
members read the same canonical map keyed by original ID). Duplicate original IDs in
either member are detected and rejected.

### 4.1 Coordinate fallback

`map_weight_map_to_instance_by_coordinate(map, coord_of_original_id, cells)` is an
**explicit** fallback used only when stable original IDs are unavailable. It uses an exact
`(row, column)` key, rejects duplicate coordinates, ambiguous matches, and missing
coordinates, does no fuzzy nearest-neighbor matching, and stamps `mapping_method =
coordinate`. The code never silently falls back from IDs to coordinates.

---

## 5. Capability matrix

Implemented in `experiments::WeightedMethodCapability`
(`include/experiments/WeightedMethodCapability.hpp`, `.cpp`) as the central,
machine-readable source of truth. `weighted_method_capability(method, profile, objective,
options)` returns `{supported, unsupported_reason}` for the combination of method,
`{homogeneous,heterogeneous,clustered}`, `{expected,cvar,mean_cvar}`, and the
strengthening options (restricted / LLBI families / combinatorial / dominance /
paired_reburn). It encodes the Phase 9 known-limitations (e.g. restricted-candidate +
combinatorial Benders is unsupported; combinatorial + LLBI/dominance/conditional-fixing is
restricted). Manifest generation filters unsupported rows out; the dispatcher guards
remain authoritative at execution. Tests assert the matrix and the solver guards agree.

---

## 6. Manifest, run-identity, and worker contract

Additive, backward-compatible extensions to
`scripts/generate_fpp_new_instances_scaling_manifests.py` and
`scripts/run_fpp_new_instances_scaling_manifest_worker.py`.

- Generator options: `--weight-profiles homogeneous,heterogeneous,clustered`,
  `--weight-replicates 0`, `--weight-seed-base <n>`, `--weight-registry <path>`,
  `--generate-missing-weight-maps`, `--paired-reburn-evaluation`.
- New manifest columns (see script `FIELDS`): `canonical_landscape_id`,
  `paired_landscape_id`, `weight_profile`, `weight_replicate`, `weight_generation_seed`,
  `weight_generator_version`, `weight_map_path`, `weight_map_hash`,
  `weight_source_universe_hash`, `weight_normalization_mode`,
  `weight_mapping_method`, `paired_reburn_instance_id`, `paired_evaluation_enabled`.
- `run_id` gains a deterministic weight suffix (`…_wp<profile>_wr<replicate>_wh<hash8>`)
  so different maps never collide and weighted rows never collide with legacy rows.
- Worker: reads the weight columns, resolves `weight_map_path` from the registry
  **without regenerating**, passes `--weight-map-file` to the solver **and** to the
  paired reburn `evaluate` call (same canonical CSV for both members), and records the
  resolved weight metadata into the worker CSV. A missing required map is a hard failure.

The C++ `MethodDispatchRequest::weight_map_file` already threads the map into every
method; the same file is used for training, in-sample validation, out-of-sample, and
paired reburn evaluation, so no test-specific or reburn-specific map is ever created.

---

## 7. Tests

C++ (Makefile `test` target, non-CPLEX):

- `test_weight_canonical_landscape` — universe-hash determinism, canonical-id determinism,
  family stripping, seed derivation stability and distinctness, run-identity behavior.
- `test_weight_map_registry` — idempotent ensure (identical path/weights/hash/metadata),
  parameter-mismatch rejection, worker `load` fails when the map is missing (no
  regeneration), distinct replicates give distinct hashes.
- `test_weight_paired_instance_mapping` — reduced compact mapping, reburn shared-cell
  equality (`shared_weight_mismatch_count == 0`), reburn-only cells present, missing-ID and
  duplicate-ID failures, coordinate fallback exactness and ambiguity rejection.
- `test_weight_capability_matrix` — supported combinations true, unsupported combinations
  false with a reason, all three profiles for standard methods.

Python: `scripts/test_weight_manifest_generation.py` self-test covering run-identity
determinism, weight-vs-legacy non-collision, and capability filtering.

---

## 8. Paired smoke

`scripts/weighted_phase8a_paired_smoke.sh` runs, for the `new20x20` / `new20x20_reburn`
pair and each of homogeneous / one heterogeneous / one clustered map: ensure one canonical
map, map it to the reduced and reburn instances, verify zero shared mismatches, solve one
small supported method, evaluate on reburn, and confirm a single canonical hash throughout.
Results are recorded in `docs/` / the smoke output; **no full experiment matrix is run.**

---

## 9. Backward compatibility

Legacy manifests without weight metadata resolve to homogeneous mode with no random
generation and an explicit `legacy_homogeneous` marker in results; existing homogeneous
numerical behavior is unchanged; old scripts continue to work; weighted run IDs cannot
collide with legacy homogeneous run IDs.

---

## 10. Remaining Phase 8B work

- Batch workers and full script integration end-to-end.
- Paired reburn *execution* at scale (Phase 8A implements the metadata, lookup, and
  validation infrastructure and the worker wiring, not the batch execution).
- Robust resume/retry, experiment-family runners, small end-to-end batch validation.
- Subset-tolerant `attach_weight_map` for instances whose compact universe is a strict
  subset of the physical universe (not needed for the `new_instances` family, whose
  compact universe equals `1..NCells`).
- Wiring the canonical registry into the C++ `BatchExperimentConfig` manifest path.

---

## 11. Validation results

Commands executed:

- `make build` — non-CPLEX build: PASS.
- `make cplex` (`CPLEX_STUDIO_DIR=/opt/ibm/ILOG/CPLEX_Studio2211`) — CPLEX build: PASS.
- `make test` — full suite: PASS (exit 0). The four new focused tests pass:
  `test_weight_canonical_landscape`, `test_weight_map_registry`,
  `test_weight_paired_instance_mapping`, `test_weight_capability_matrix`.
- `git diff --check` and `git diff --cached --check` — no whitespace errors.
- `python3 -m py_compile` on both modified scripts and the new self-test — PASS.
- `python3 scripts/test_weight_manifest_generation.py` — PASS (capability filter, run
  identity, and an end-to-end generation against the real binary).

Paired smoke (`scripts/weighted_phase8a_paired_smoke.sh`) — PASS for all three profiles:

| field | homogeneous | heterogeneous | clustered |
|---|---|---|---|
| canonical_landscape_id | `new20x20__20x20__a43d93884168abc8` | (same) | (same) |
| reduced_instance_id | new20x20 | new20x20 | new20x20 |
| reburn_instance_id | new20x20_reburn | new20x20_reburn | new20x20_reburn |
| weight_map_hash | `fnv1a64:e7afcf304d8904e9` | `fnv1a64:504d85f057318518` | `fnv1a64:c9ff7cc7c2899aa4` |
| canonical_cell_count | 400 | 400 | 400 |
| reduced_cell_count | 400 | 400 | 400 |
| reburn_cell_count | 400 | 400 | 400 |
| shared_cell_count | 400 | 400 | 400 |
| shared_weight_mismatch_count | 0 | 0 | 0 |
| reduced_missing_count / reburn_missing_count | 0 / 0 | 0 / 0 | 0 / 0 |
| solver_method | Static-DPV | Static-DPV | Static-DPV |
| solver_status | NotApplicable (heuristic) | NotApplicable | NotApplicable |
| paired_evaluation_status | ok | ok | ok |
| single canonical hash throughout | yes | yes | yes |

The reduced (`new20x20`) and reburn (`new20x20_reburn`) members resolve to the same
`canonical_landscape_id` and the same per-profile weight-map hash; the reduced solve and
the reburn evaluation both load that single hash. Zero shared-weight mismatches.
