#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${PROJECT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build_gpp"
CXX="${CXX:-g++}"
WITH_CPLEX=0

for arg in "$@"; do
  case "${arg}" in
    --with-cplex)
      WITH_CPLEX=1
      ;;
    --help|-h)
      echo "Usage: ./build_gpp.sh [--with-cplex]"
      echo
      echo "Default builds without CPLEX."
      echo "With CPLEX, set CPLEX_STUDIO_DIR or explicit include/lib env vars."
      exit 0
      ;;
    *)
      echo "Unknown argument: ${arg}" >&2
      exit 2
      ;;
  esac
done

mkdir -p "${BUILD_DIR}"

COMMON_FLAGS=(
  -std=c++17
  -Wall
  -Wextra
  -pedantic
  "-I${PROJECT_DIR}/include"
  "-DFIREBREAK_PROJECT_ROOT=\"${PROJECT_DIR}\""
  "-DFIREBREAK_REPO_ROOT=\"${REPO_ROOT}\""
)

CPLEX_LINK_FLAGS=()
if [[ "${WITH_CPLEX}" -eq 1 ]]; then
  COMMON_FLAGS+=("-DFIREBREAK_WITH_CPLEX")

  if [[ -n "${CPLEX_STUDIO_DIR:-}" ]]; then
    COMMON_FLAGS+=(
      "-I${CPLEX_STUDIO_DIR}/cplex/include"
      "-I${CPLEX_STUDIO_DIR}/concert/include"
    )
    CPLEX_LINK_FLAGS+=(
      "-L${CPLEX_STUDIO_DIR}/cplex/lib/x86-64_linux/static_pic"
      "-L${CPLEX_STUDIO_DIR}/concert/lib/x86-64_linux/static_pic"
    )
  fi

  for include_var in CPLEX_INCLUDE_DIR CPLEX_CONCERT_INCLUDE_DIR; do
    if [[ -n "${!include_var:-}" ]]; then
      IFS=':' read -r -a include_dirs <<< "${!include_var}"
      for include_dir in "${include_dirs[@]}"; do
        COMMON_FLAGS+=("-I${include_dir}")
      done
    fi
  done

  for lib_var in CPLEX_LIB_DIR CPLEX_CONCERT_LIB_DIR; do
    if [[ -n "${!lib_var:-}" ]]; then
      IFS=':' read -r -a lib_dirs <<< "${!lib_var}"
      for lib_dir in "${lib_dirs[@]}"; do
        CPLEX_LINK_FLAGS+=("-L${lib_dir}")
      done
    fi
  done

  CPLEX_LINK_FLAGS+=("-lilocplex" "-lcplex" "-lconcert" "-lm" "-lpthread" "-ldl")
fi

MAIN_SOURCES=(
  "${PROJECT_DIR}/src/analysis/BatchSummaryReporter.cpp"
  "${PROJECT_DIR}/src/analysis/GraphDiagnostics.cpp"
  "${PROJECT_DIR}/src/analysis/ExperimentAggregator.cpp"
  "${PROJECT_DIR}/src/analysis/RuntimeProfiler.cpp"
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/BendersCoefficientCandidateScorer.cpp"
  "${PROJECT_DIR}/src/benders/BurnFrequencyCandidateScorer.cpp"
  "${PROJECT_DIR}/src/benders/CandidateBoundController.cpp"
  "${PROJECT_DIR}/src/benders/CvarTailAwareBendersCandidateScorer.cpp"
  "${PROJECT_DIR}/src/benders/CvarTailScoreDiagnostics.cpp"
  "${PROJECT_DIR}/src/benders/RestrictedCandidateCutPool.cpp"
  "${PROJECT_DIR}/src/benders/RestrictedCandidateManager.cpp"
  "${PROJECT_DIR}/src/benders/RestrictedCandidateMaintenanceTracker.cpp"
  "${PROJECT_DIR}/src/benders/FppCombinatorialBenders.cpp"
  "${PROJECT_DIR}/src/benders/FppProjectedLlbi.cpp"
  "${PROJECT_DIR}/src/benders/FppLiftedLowerBound.cpp"
  "${PROJECT_DIR}/src/benders/FppScenarioSubproblem.cpp"
  "${PROJECT_DIR}/src/benders/FppPersistentScenarioSubproblemManager.cpp"
  "${PROJECT_DIR}/src/benders/FppBendersMaster.cpp"
  "${PROJECT_DIR}/src/benders/FppBendersSolver.cpp"
  "${PROJECT_DIR}/src/benders/FppBranchBendersSolver.cpp"
  "${PROJECT_DIR}/src/benders/FppRestrictedCandidateBranchBendersSolver.cpp"
  "${PROJECT_DIR}/src/benders/DpvLiftedLowerBound.cpp"
  "${PROJECT_DIR}/src/benders/DpvScenarioSubproblem.cpp"
  "${PROJECT_DIR}/src/benders/DpvPersistentScenarioSubproblemManager.cpp"
  "${PROJECT_DIR}/src/benders/DpvBendersMaster.cpp"
  "${PROJECT_DIR}/src/benders/DpvBendersSolver.cpp"
  "${PROJECT_DIR}/src/benders/DpvBranchBendersSolver.cpp"
  "${PROJECT_DIR}/src/benchmarks/StaticDpvBenchmark.cpp"
  "${PROJECT_DIR}/src/benchmarks/StaticDpvMipBenchmark.cpp"
  "${PROJECT_DIR}/src/core/FirebreakSolution.cpp"
  "${PROJECT_DIR}/src/core/Graph.cpp"
  "${PROJECT_DIR}/src/core/Scenario.cpp"
  "${PROJECT_DIR}/src/core/Instance.cpp"
  "${PROJECT_DIR}/src/cuts/DominatorCuts.cpp"
  "${PROJECT_DIR}/src/cuts/NodeSeparatorMinCut.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorCutSeparator.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorContextCallback.cpp"
  "${PROJECT_DIR}/src/eval/BurnedAreaEvaluator.cpp"
  "${PROJECT_DIR}/src/eval/FppRecourseEvaluator.cpp"
  "${PROJECT_DIR}/src/graph/DinicMaxFlow.cpp"
  "${PROJECT_DIR}/src/heuristics/CumulativePropagationGraph.cpp"
  "${PROJECT_DIR}/src/heuristics/GreedyMetrics.cpp"
  "${PROJECT_DIR}/src/heuristics/GreedyHeuristic.cpp"
  "${PROJECT_DIR}/src/heuristics/ReachabilityGreedyWarmStart.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/opt/DpvIndexBuilder.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstanceBuilder.cpp"
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/src/solver/WarmStart.cpp"
  "${PROJECT_DIR}/src/solver/FppSaaCplexModel.cpp"
  "${PROJECT_DIR}/src/solver/FppCutReachabilityCplexModel.cpp"
  "${PROJECT_DIR}/src/solver/DpvSaaCplexModel.cpp"
  "${PROJECT_DIR}/src/io/PathUtils.cpp"
  "${PROJECT_DIR}/src/io/ScenarioFileUtils.cpp"
  "${PROJECT_DIR}/src/io/ScenarioSplitUtils.cpp"
  "${PROJECT_DIR}/src/io/SolutionIO.cpp"
  "${PROJECT_DIR}/src/io/ExperimentResultWriter.cpp"
  "${PROJECT_DIR}/src/io/Cell2FireReader.cpp"
  "${PROJECT_DIR}/src/io/ResultWriter.cpp"
  "${PROJECT_DIR}/src/experiments/SmokeRunner.cpp"
  "${PROJECT_DIR}/src/experiments/NewInstancesSmokeRunner.cpp"
  "${PROJECT_DIR}/src/experiments/BatchExperimentConfig.cpp"
  "${PROJECT_DIR}/src/experiments/SharedSplitUtils.cpp"
  "${PROJECT_DIR}/src/experiments/BatchExperimentRunner.cpp"
  "${PROJECT_DIR}/src/experiments/ExperimentManifest.cpp"
  "${PROJECT_DIR}/src/experiments/EvaluationRunner.cpp"
  "${PROJECT_DIR}/src/experiments/OptInstanceRunner.cpp"
  "${PROJECT_DIR}/src/experiments/FppSaaRunner.cpp"
  "${PROJECT_DIR}/src/experiments/FppLpViDiagnosticRunner.cpp"
  "${PROJECT_DIR}/src/experiments/GraphDiagnosticsRunner.cpp"
  "${PROJECT_DIR}/src/experiments/GreedyOutOfSampleRunner.cpp"
  "${PROJECT_DIR}/src/experiments/FppMasterLpDiagnosticRunner.cpp"
  "${PROJECT_DIR}/src/experiments/FppDominancePreprocessingDiagnosticRunner.cpp"
  "${PROJECT_DIR}/src/experiments/FppSaaOutOfSampleRunner.cpp"
  "${PROJECT_DIR}/src/experiments/FppBendersOutOfSampleRunner.cpp"
  "${PROJECT_DIR}/src/experiments/FppBranchBendersOutOfSampleRunner.cpp"
  "${PROJECT_DIR}/src/experiments/FppRestrictedCandidateBranchBendersOutOfSampleRunner.cpp"
  "${PROJECT_DIR}/src/experiments/StaticDpvOutOfSampleRunner.cpp"
  "${PROJECT_DIR}/src/experiments/DpvSaaOutOfSampleRunner.cpp"
  "${PROJECT_DIR}/src/experiments/DpvBendersOutOfSampleRunner.cpp"
  "${PROJECT_DIR}/src/experiments/DpvBranchBendersOutOfSampleRunner.cpp"
  "${PROJECT_DIR}/src/experiments/ManifestRunner.cpp"
  "${PROJECT_DIR}/src/experiments/MethodDispatcher.cpp"
  "${PROJECT_DIR}/src/main.cpp"
)

TEST_SOURCES=(
  "${PROJECT_DIR}/src/core/FirebreakSolution.cpp"
  "${PROJECT_DIR}/src/core/Graph.cpp"
  "${PROJECT_DIR}/src/core/Scenario.cpp"
  "${PROJECT_DIR}/src/core/Instance.cpp"
  "${PROJECT_DIR}/src/eval/BurnedAreaEvaluator.cpp"
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/tests/test_burned_area_evaluator.cpp"
)

RISK_MEASURE_TEST_SOURCES=(
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/tests/test_risk_measure.cpp"
)

FPP_RECOURSE_TEST_SOURCES=(
  "${PROJECT_DIR}/src/core/FirebreakSolution.cpp"
  "${PROJECT_DIR}/src/core/Graph.cpp"
  "${PROJECT_DIR}/src/core/Scenario.cpp"
  "${PROJECT_DIR}/src/core/Instance.cpp"
  "${PROJECT_DIR}/src/eval/BurnedAreaEvaluator.cpp"
  "${PROJECT_DIR}/src/eval/FppRecourseEvaluator.cpp"
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/src/cuts/DominatorCuts.cpp"
  "${PROJECT_DIR}/src/cuts/NodeSeparatorMinCut.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorCutSeparator.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorContextCallback.cpp"
  "${PROJECT_DIR}/src/graph/DinicMaxFlow.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/opt/DpvIndexBuilder.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstanceBuilder.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/src/solver/FppSaaCplexModel.cpp"
  "${PROJECT_DIR}/tests/test_fpp_recourse_evaluator.cpp"
)

REACHABILITY_GREEDY_TEST_SOURCES=(
  "${PROJECT_DIR}/src/cuts/DominatorCuts.cpp"
  "${PROJECT_DIR}/src/cuts/NodeSeparatorMinCut.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorCutSeparator.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorContextCallback.cpp"
  "${PROJECT_DIR}/src/eval/FppRecourseEvaluator.cpp"
  "${PROJECT_DIR}/src/graph/DinicMaxFlow.cpp"
  "${PROJECT_DIR}/src/heuristics/ReachabilityGreedyWarmStart.cpp"
  "${PROJECT_DIR}/src/io/PathUtils.cpp"
  "${PROJECT_DIR}/src/io/SolutionIO.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/src/solver/WarmStart.cpp"
  "${PROJECT_DIR}/src/solver/FppSaaCplexModel.cpp"
  "${PROJECT_DIR}/tests/test_reachability_greedy_warm_start.cpp"
)

INDEX_MAPPER_TEST_SOURCES=(
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/tests/test_index_mapper.cpp"
)

DPV_INDEX_TEST_SOURCES=(
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/opt/DpvIndexBuilder.cpp"
  "${PROJECT_DIR}/tests/test_dpv_index_builder.cpp"
)

FPP_SAA_STRUCTURE_TEST_SOURCES=(
  "${PROJECT_DIR}/src/cuts/DominatorCuts.cpp"
  "${PROJECT_DIR}/src/cuts/NodeSeparatorMinCut.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorCutSeparator.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorContextCallback.cpp"
  "${PROJECT_DIR}/src/graph/DinicMaxFlow.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/src/solver/FppSaaCplexModel.cpp"
  "${PROJECT_DIR}/tests/test_fpp_saa_model_structure.cpp"
)

FPP_CUT_REACHABILITY_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/FppCombinatorialBenders.cpp"
  "${PROJECT_DIR}/src/benders/FppProjectedLlbi.cpp"
  "${PROJECT_DIR}/src/cuts/DominatorCuts.cpp"
  "${PROJECT_DIR}/src/cuts/NodeSeparatorMinCut.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorCutSeparator.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorContextCallback.cpp"
  "${PROJECT_DIR}/src/eval/FppRecourseEvaluator.cpp"
  "${PROJECT_DIR}/src/experiments/BatchExperimentConfig.cpp"
  "${PROJECT_DIR}/src/graph/DinicMaxFlow.cpp"
  "${PROJECT_DIR}/src/heuristics/ReachabilityGreedyWarmStart.cpp"
  "${PROJECT_DIR}/src/io/PathUtils.cpp"
  "${PROJECT_DIR}/src/io/SolutionIO.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/src/solver/WarmStart.cpp"
  "${PROJECT_DIR}/src/solver/FppSaaCplexModel.cpp"
  "${PROJECT_DIR}/src/solver/FppCutReachabilityCplexModel.cpp"
  "${PROJECT_DIR}/tests/test_fpp_cut_reachability_model.cpp"
)

DOMINATOR_CUT_TEST_SOURCES=(
  "${PROJECT_DIR}/src/cuts/DominatorCuts.cpp"
  "${PROJECT_DIR}/src/cuts/NodeSeparatorMinCut.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorCutSeparator.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorContextCallback.cpp"
  "${PROJECT_DIR}/src/eval/FppRecourseEvaluator.cpp"
  "${PROJECT_DIR}/src/graph/DinicMaxFlow.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/src/solver/FppSaaCplexModel.cpp"
  "${PROJECT_DIR}/src/solver/FppCutReachabilityCplexModel.cpp"
  "${PROJECT_DIR}/tests/test_dominator_cuts.cpp"
)

SEPARATOR_CONTEXT_TEST_SOURCES=(
  "${PROJECT_DIR}/src/cuts/DominatorCuts.cpp"
  "${PROJECT_DIR}/src/cuts/NodeSeparatorMinCut.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorCutSeparator.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorContextCallback.cpp"
  "${PROJECT_DIR}/src/eval/FppRecourseEvaluator.cpp"
  "${PROJECT_DIR}/src/graph/DinicMaxFlow.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/src/solver/FppSaaCplexModel.cpp"
  "${PROJECT_DIR}/src/solver/FppCutReachabilityCplexModel.cpp"
  "${PROJECT_DIR}/tests/test_separator_context_callback.cpp"
)

NODE_SEPARATOR_TEST_SOURCES=(
  "${PROJECT_DIR}/src/cuts/NodeSeparatorMinCut.cpp"
  "${PROJECT_DIR}/src/graph/DinicMaxFlow.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/tests/test_node_separator_min_cut.cpp"
)

DPV_SAA_STRUCTURE_TEST_SOURCES=(
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/opt/DpvIndexBuilder.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/src/solver/DpvSaaCplexModel.cpp"
  "${PROJECT_DIR}/tests/test_dpv_saa_model_structure.cpp"
)

GRAPH_DIAGNOSTICS_TEST_SOURCES=(
  "${PROJECT_DIR}/src/analysis/GraphDiagnostics.cpp"
  "${PROJECT_DIR}/src/core/Graph.cpp"
  "${PROJECT_DIR}/src/core/Scenario.cpp"
  "${PROJECT_DIR}/tests/test_graph_diagnostics.cpp"
)

SOLUTION_IO_TEST_SOURCES=(
  "${PROJECT_DIR}/src/io/ExperimentResultWriter.cpp"
  "${PROJECT_DIR}/src/io/SolutionIO.cpp"
  "${PROJECT_DIR}/src/io/PathUtils.cpp"
  "${PROJECT_DIR}/tests/test_solution_io.cpp"
)

SCENARIO_SPLIT_TEST_SOURCES=(
  "${PROJECT_DIR}/src/io/ScenarioFileUtils.cpp"
  "${PROJECT_DIR}/src/io/ScenarioSplitUtils.cpp"
  "${PROJECT_DIR}/src/io/PathUtils.cpp"
  "${PROJECT_DIR}/tests/test_scenario_split_utils.cpp"
)

STATIC_DPV_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benchmarks/StaticDpvBenchmark.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/opt/DpvIndexBuilder.cpp"
  "${PROJECT_DIR}/tests/test_static_dpv_benchmark.cpp"
)

STATIC_DPV_MIP_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benchmarks/StaticDpvBenchmark.cpp"
  "${PROJECT_DIR}/src/benchmarks/StaticDpvMipBenchmark.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/opt/DpvIndexBuilder.cpp"
  "${PROJECT_DIR}/tests/test_static_dpv_mip_benchmark.cpp"
)

CUMULATIVE_GRAPH_TEST_SOURCES=(
  "${PROJECT_DIR}/src/heuristics/CumulativePropagationGraph.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/tests/test_cumulative_propagation_graph.cpp"
)

GREEDY_HEURISTIC_TEST_SOURCES=(
  "${PROJECT_DIR}/src/heuristics/CumulativePropagationGraph.cpp"
  "${PROJECT_DIR}/src/heuristics/GreedyMetrics.cpp"
  "${PROJECT_DIR}/src/heuristics/GreedyHeuristic.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/tests/test_greedy_heuristics.cpp"
)

WARM_START_TEST_SOURCES=(
  "${PROJECT_DIR}/src/io/PathUtils.cpp"
  "${PROJECT_DIR}/src/io/SolutionIO.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/solver/WarmStart.cpp"
  "${PROJECT_DIR}/tests/test_warm_start.cpp"
)

BATCH_CONFIG_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/FppCombinatorialBenders.cpp"
  "${PROJECT_DIR}/src/benders/FppProjectedLlbi.cpp"
  "${PROJECT_DIR}/src/experiments/BatchExperimentConfig.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/tests/test_batch_experiment_config.cpp"
)

EXPERIMENT_AGGREGATOR_TEST_SOURCES=(
  "${PROJECT_DIR}/src/analysis/ExperimentAggregator.cpp"
  "${PROJECT_DIR}/src/io/PathUtils.cpp"
  "${PROJECT_DIR}/tests/test_experiment_aggregator.cpp"
)

EXPERIMENT_MANIFEST_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/FppCombinatorialBenders.cpp"
  "${PROJECT_DIR}/src/benders/FppProjectedLlbi.cpp"
  "${PROJECT_DIR}/src/experiments/BatchExperimentConfig.cpp"
  "${PROJECT_DIR}/src/experiments/ExperimentManifest.cpp"
  "${PROJECT_DIR}/src/io/PathUtils.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/tests/test_experiment_manifest.cpp"
)

BATCH_SUMMARY_REPORTER_TEST_SOURCES=(
  "${PROJECT_DIR}/src/analysis/BatchSummaryReporter.cpp"
  "${PROJECT_DIR}/src/io/PathUtils.cpp"
  "${PROJECT_DIR}/tests/test_batch_summary_reporter.cpp"
)

RUNTIME_PROFILER_TEST_SOURCES=(
  "${PROJECT_DIR}/src/analysis/RuntimeProfiler.cpp"
  "${PROJECT_DIR}/src/io/PathUtils.cpp"
  "${PROJECT_DIR}/tests/test_runtime_profiler.cpp"
)

SHARED_SPLITS_TEST_SOURCES=(
  "${PROJECT_DIR}/src/experiments/SharedSplitUtils.cpp"
  "${PROJECT_DIR}/src/io/ScenarioSplitUtils.cpp"
  "${PROJECT_DIR}/src/io/PathUtils.cpp"
  "${PROJECT_DIR}/tests/test_shared_splits.cpp"
)

BENDERS_CUT_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/tests/test_benders_cut.cpp"
)

BURN_FREQUENCY_CANDIDATE_SCORER_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BurnFrequencyCandidateScorer.cpp"
  "${PROJECT_DIR}/src/benders/RestrictedCandidateManager.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/tests/test_burn_frequency_candidate_scorer.cpp"
)

BENDERS_COEFFICIENT_CANDIDATE_SCORER_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/BendersCoefficientCandidateScorer.cpp"
  "${PROJECT_DIR}/src/benders/RestrictedCandidateManager.cpp"
  "${PROJECT_DIR}/tests/test_benders_coefficient_candidate_scorer.cpp"
)

CVAR_TAIL_SCORE_DIAGNOSTICS_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/BendersCoefficientCandidateScorer.cpp"
  "${PROJECT_DIR}/src/benders/CvarTailAwareBendersCandidateScorer.cpp"
  "${PROJECT_DIR}/src/benders/CvarTailScoreDiagnostics.cpp"
  "${PROJECT_DIR}/src/benders/RestrictedCandidateManager.cpp"
  "${PROJECT_DIR}/src/io/ExperimentResultWriter.cpp"
  "${PROJECT_DIR}/src/io/PathUtils.cpp"
  "${PROJECT_DIR}/tests/test_cvar_tail_score_diagnostics.cpp"
)

CVAR_TAIL_AWARE_BENDERS_CANDIDATE_SCORER_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/BendersCoefficientCandidateScorer.cpp"
  "${PROJECT_DIR}/src/benders/CvarTailAwareBendersCandidateScorer.cpp"
  "${PROJECT_DIR}/tests/test_cvar_tail_aware_benders_candidate_scorer.cpp"
)

RESTRICTED_CANDIDATE_CUT_POOL_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/RestrictedCandidateCutPool.cpp"
  "${PROJECT_DIR}/tests/test_restricted_candidate_cut_pool.cpp"
)

RESTRICTED_CANDIDATE_MANAGER_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/RestrictedCandidateManager.cpp"
  "${PROJECT_DIR}/tests/test_restricted_candidate_manager.cpp"
)

RESTRICTED_CANDIDATE_MAINTENANCE_TRACKER_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/RestrictedCandidateManager.cpp"
  "${PROJECT_DIR}/src/benders/RestrictedCandidateMaintenanceTracker.cpp"
  "${PROJECT_DIR}/tests/test_restricted_candidate_maintenance_tracker.cpp"
)

CANDIDATE_BOUND_CONTROLLER_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/CandidateBoundController.cpp"
  "${PROJECT_DIR}/src/benders/RestrictedCandidateManager.cpp"
  "${PROJECT_DIR}/tests/test_candidate_bound_controller.cpp"
)

DPV_SUBPROBLEM_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/DpvLiftedLowerBound.cpp"
  "${PROJECT_DIR}/src/benders/DpvScenarioSubproblem.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/opt/DpvIndexBuilder.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/tests/test_dpv_subproblem_structure.cpp"
)

DPV_BENDERS_SMALL_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/DpvLiftedLowerBound.cpp"
  "${PROJECT_DIR}/src/benders/DpvScenarioSubproblem.cpp"
  "${PROJECT_DIR}/src/benders/DpvBendersMaster.cpp"
  "${PROJECT_DIR}/src/benders/DpvBendersSolver.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/opt/DpvIndexBuilder.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/src/solver/DpvSaaCplexModel.cpp"
  "${PROJECT_DIR}/tests/test_dpv_benders_small.cpp"
)

DPV_BRANCH_BENDERS_SMALL_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/DpvLiftedLowerBound.cpp"
  "${PROJECT_DIR}/src/benders/DpvScenarioSubproblem.cpp"
  "${PROJECT_DIR}/src/benders/DpvPersistentScenarioSubproblemManager.cpp"
  "${PROJECT_DIR}/src/benders/DpvBendersMaster.cpp"
  "${PROJECT_DIR}/src/benders/DpvBendersSolver.cpp"
  "${PROJECT_DIR}/src/benders/DpvBranchBendersSolver.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/opt/DpvIndexBuilder.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/src/solver/DpvSaaCplexModel.cpp"
  "${PROJECT_DIR}/tests/test_dpv_branch_benders_small.cpp"
)

FPP_BENDERS_SMALL_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/FppLiftedLowerBound.cpp"
  "${PROJECT_DIR}/src/benders/FppScenarioSubproblem.cpp"
  "${PROJECT_DIR}/src/benders/FppBendersMaster.cpp"
  "${PROJECT_DIR}/src/benders/FppBendersSolver.cpp"
  "${PROJECT_DIR}/src/cuts/DominatorCuts.cpp"
  "${PROJECT_DIR}/src/cuts/NodeSeparatorMinCut.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorCutSeparator.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorContextCallback.cpp"
  "${PROJECT_DIR}/src/eval/FppRecourseEvaluator.cpp"
  "${PROJECT_DIR}/src/graph/DinicMaxFlow.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/src/solver/FppSaaCplexModel.cpp"
  "${PROJECT_DIR}/tests/test_fpp_benders_small.cpp"
)

FPP_BRANCH_BENDERS_SMALL_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/FppCombinatorialBenders.cpp"
  "${PROJECT_DIR}/src/benders/FppProjectedLlbi.cpp"
  "${PROJECT_DIR}/src/benders/FppLiftedLowerBound.cpp"
  "${PROJECT_DIR}/src/benders/FppScenarioSubproblem.cpp"
  "${PROJECT_DIR}/src/benders/FppPersistentScenarioSubproblemManager.cpp"
  "${PROJECT_DIR}/src/benders/FppBendersMaster.cpp"
  "${PROJECT_DIR}/src/benders/FppBendersSolver.cpp"
  "${PROJECT_DIR}/src/benders/FppBranchBendersSolver.cpp"
  "${PROJECT_DIR}/src/cuts/DominatorCuts.cpp"
  "${PROJECT_DIR}/src/cuts/NodeSeparatorMinCut.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorCutSeparator.cpp"
  "${PROJECT_DIR}/src/cuts/SeparatorContextCallback.cpp"
  "${PROJECT_DIR}/src/eval/FppRecourseEvaluator.cpp"
  "${PROJECT_DIR}/src/graph/DinicMaxFlow.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/src/solver/FppSaaCplexModel.cpp"
  "${PROJECT_DIR}/tests/test_fpp_branch_benders_small.cpp"
)

FPP_PERSISTENT_SCENARIO_SUBPROBLEM_MANAGER_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/FppScenarioSubproblem.cpp"
  "${PROJECT_DIR}/src/benders/FppPersistentScenarioSubproblemManager.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/tests/test_fpp_persistent_scenario_subproblem_manager.cpp"
)

FPP_STRENGTHENING_TEST_SOURCES=(
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/tests/test_fpp_strengthening.cpp"
)

FPP_PROJECTED_LLBI_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/FppCombinatorialBenders.cpp"
  "${PROJECT_DIR}/src/benders/FppProjectedLlbi.cpp"
  "${PROJECT_DIR}/src/experiments/BatchExperimentConfig.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/tests/test_fpp_projected_llbi.cpp"
)

FPP_COMBINATORIAL_BENDERS_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/FppCombinatorialBenders.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/tests/test_fpp_combinatorial_benders.cpp"
)

FPP_RESTRICTED_CANDIDATE_BRANCH_BENDERS_SMALL_TEST_SOURCES=(
  "${PROJECT_DIR}/src/benders/BendersCut.cpp"
  "${PROJECT_DIR}/src/benders/BendersCoefficientCandidateScorer.cpp"
  "${PROJECT_DIR}/src/benders/BurnFrequencyCandidateScorer.cpp"
  "${PROJECT_DIR}/src/benders/CandidateBoundController.cpp"
  "${PROJECT_DIR}/src/benders/CvarTailAwareBendersCandidateScorer.cpp"
  "${PROJECT_DIR}/src/benders/CvarTailScoreDiagnostics.cpp"
  "${PROJECT_DIR}/src/benders/RestrictedCandidateCutPool.cpp"
  "${PROJECT_DIR}/src/benders/RestrictedCandidateManager.cpp"
  "${PROJECT_DIR}/src/benders/RestrictedCandidateMaintenanceTracker.cpp"
  "${PROJECT_DIR}/src/benders/FppCombinatorialBenders.cpp"
  "${PROJECT_DIR}/src/benders/FppProjectedLlbi.cpp"
  "${PROJECT_DIR}/src/benders/FppLiftedLowerBound.cpp"
  "${PROJECT_DIR}/src/benders/FppScenarioSubproblem.cpp"
  "${PROJECT_DIR}/src/benders/FppPersistentScenarioSubproblemManager.cpp"
  "${PROJECT_DIR}/src/benders/FppBranchBendersSolver.cpp"
  "${PROJECT_DIR}/src/benders/FppRestrictedCandidateBranchBendersSolver.cpp"
  "${PROJECT_DIR}/src/opt/IndexMapper.cpp"
  "${PROJECT_DIR}/src/opt/OptimizationInstance.cpp"
  "${PROJECT_DIR}/src/solver/CplexEnvironment.cpp"
  "${PROJECT_DIR}/src/risk/RiskMeasure.cpp"
  "${PROJECT_DIR}/tests/test_fpp_restricted_candidate_branch_benders_small.cpp"
)

"${CXX}" "${COMMON_FLAGS[@]}" "${MAIN_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/firebreak_cpp"
"${CXX}" "${COMMON_FLAGS[@]}" "${TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_burned_area_evaluator"
"${CXX}" "${COMMON_FLAGS[@]}" "${RISK_MEASURE_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_risk_measure"
"${CXX}" "${COMMON_FLAGS[@]}" "${FPP_RECOURSE_TEST_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/test_fpp_recourse_evaluator"
"${CXX}" "${COMMON_FLAGS[@]}" "${REACHABILITY_GREEDY_TEST_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/test_reachability_greedy_warm_start"
"${CXX}" "${COMMON_FLAGS[@]}" "${INDEX_MAPPER_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_index_mapper"
"${CXX}" "${COMMON_FLAGS[@]}" "${DPV_INDEX_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_dpv_index_builder"
"${CXX}" "${COMMON_FLAGS[@]}" "${FPP_SAA_STRUCTURE_TEST_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/test_fpp_saa_model_structure"
"${CXX}" "${COMMON_FLAGS[@]}" "${FPP_CUT_REACHABILITY_TEST_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/test_fpp_cut_reachability_model"
"${CXX}" "${COMMON_FLAGS[@]}" "${DOMINATOR_CUT_TEST_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/test_dominator_cuts"
"${CXX}" "${COMMON_FLAGS[@]}" "${SEPARATOR_CONTEXT_TEST_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/test_separator_context_callback"
"${CXX}" "${COMMON_FLAGS[@]}" "${NODE_SEPARATOR_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_node_separator_min_cut"
"${CXX}" "${COMMON_FLAGS[@]}" "${DPV_SAA_STRUCTURE_TEST_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/test_dpv_saa_model_structure"
"${CXX}" "${COMMON_FLAGS[@]}" "${GRAPH_DIAGNOSTICS_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_graph_diagnostics"
"${CXX}" "${COMMON_FLAGS[@]}" "${SOLUTION_IO_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_solution_io"
"${CXX}" "${COMMON_FLAGS[@]}" "${SCENARIO_SPLIT_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_scenario_split_utils"
"${CXX}" "${COMMON_FLAGS[@]}" "${STATIC_DPV_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_static_dpv_benchmark"
"${CXX}" "${COMMON_FLAGS[@]}" "${STATIC_DPV_MIP_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_static_dpv_mip_benchmark"
"${CXX}" "${COMMON_FLAGS[@]}" "${CUMULATIVE_GRAPH_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_cumulative_propagation_graph"
"${CXX}" "${COMMON_FLAGS[@]}" "${GREEDY_HEURISTIC_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_greedy_heuristics"
"${CXX}" "${COMMON_FLAGS[@]}" "${WARM_START_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_warm_start"
"${CXX}" "${COMMON_FLAGS[@]}" "${BATCH_CONFIG_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_batch_experiment_config"
"${CXX}" "${COMMON_FLAGS[@]}" "${EXPERIMENT_AGGREGATOR_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_experiment_aggregator"
"${CXX}" "${COMMON_FLAGS[@]}" "${EXPERIMENT_MANIFEST_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_experiment_manifest"
"${CXX}" "${COMMON_FLAGS[@]}" "${BATCH_SUMMARY_REPORTER_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_batch_summary_reporter"
"${CXX}" "${COMMON_FLAGS[@]}" "${RUNTIME_PROFILER_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_runtime_profiler"
"${CXX}" "${COMMON_FLAGS[@]}" "${SHARED_SPLITS_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_shared_splits"
"${CXX}" "${COMMON_FLAGS[@]}" "${BENDERS_CUT_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_benders_cut"
"${CXX}" "${COMMON_FLAGS[@]}" "${BURN_FREQUENCY_CANDIDATE_SCORER_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_burn_frequency_candidate_scorer"
"${CXX}" "${COMMON_FLAGS[@]}" "${BENDERS_COEFFICIENT_CANDIDATE_SCORER_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_benders_coefficient_candidate_scorer"
"${CXX}" "${COMMON_FLAGS[@]}" "${CVAR_TAIL_AWARE_BENDERS_CANDIDATE_SCORER_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_cvar_tail_aware_benders_candidate_scorer"
"${CXX}" "${COMMON_FLAGS[@]}" "${CVAR_TAIL_SCORE_DIAGNOSTICS_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_cvar_tail_score_diagnostics"
"${CXX}" "${COMMON_FLAGS[@]}" "${RESTRICTED_CANDIDATE_CUT_POOL_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_restricted_candidate_cut_pool"
"${CXX}" "${COMMON_FLAGS[@]}" "${RESTRICTED_CANDIDATE_MANAGER_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_restricted_candidate_manager"
"${CXX}" "${COMMON_FLAGS[@]}" "${RESTRICTED_CANDIDATE_MAINTENANCE_TRACKER_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_restricted_candidate_maintenance_tracker"
"${CXX}" "${COMMON_FLAGS[@]}" "${CANDIDATE_BOUND_CONTROLLER_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_candidate_bound_controller"
"${CXX}" "${COMMON_FLAGS[@]}" "${DPV_SUBPROBLEM_TEST_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/test_dpv_subproblem_structure"
"${CXX}" "${COMMON_FLAGS[@]}" "${DPV_BENDERS_SMALL_TEST_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/test_dpv_benders_small"
"${CXX}" "${COMMON_FLAGS[@]}" "${DPV_BRANCH_BENDERS_SMALL_TEST_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/test_dpv_branch_benders_small"
"${CXX}" "${COMMON_FLAGS[@]}" "${FPP_BENDERS_SMALL_TEST_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/test_fpp_benders_small"
"${CXX}" "${COMMON_FLAGS[@]}" "${FPP_BRANCH_BENDERS_SMALL_TEST_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/test_fpp_branch_benders_small"
"${CXX}" "${COMMON_FLAGS[@]}" "${FPP_PERSISTENT_SCENARIO_SUBPROBLEM_MANAGER_TEST_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/test_fpp_persistent_scenario_subproblem_manager"
"${CXX}" "${COMMON_FLAGS[@]}" "${FPP_STRENGTHENING_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_fpp_strengthening"
"${CXX}" "${COMMON_FLAGS[@]}" "${FPP_PROJECTED_LLBI_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_fpp_projected_llbi"
"${CXX}" "${COMMON_FLAGS[@]}" "${FPP_COMBINATORIAL_BENDERS_TEST_SOURCES[@]}" -o "${BUILD_DIR}/test_fpp_combinatorial_benders"
"${CXX}" "${COMMON_FLAGS[@]}" "${FPP_RESTRICTED_CANDIDATE_BRANCH_BENDERS_SMALL_TEST_SOURCES[@]}" "${CPLEX_LINK_FLAGS[@]}" -o "${BUILD_DIR}/test_fpp_restricted_candidate_branch_benders_small"

echo "Built ${BUILD_DIR}/firebreak_cpp"
echo "Built ${BUILD_DIR}/test_burned_area_evaluator"
echo "Built ${BUILD_DIR}/test_risk_measure"
echo "Built ${BUILD_DIR}/test_fpp_recourse_evaluator"
echo "Built ${BUILD_DIR}/test_reachability_greedy_warm_start"
echo "Built ${BUILD_DIR}/test_index_mapper"
echo "Built ${BUILD_DIR}/test_dpv_index_builder"
echo "Built ${BUILD_DIR}/test_fpp_saa_model_structure"
echo "Built ${BUILD_DIR}/test_fpp_cut_reachability_model"
echo "Built ${BUILD_DIR}/test_dominator_cuts"
echo "Built ${BUILD_DIR}/test_separator_context_callback"
echo "Built ${BUILD_DIR}/test_node_separator_min_cut"
echo "Built ${BUILD_DIR}/test_dpv_saa_model_structure"
echo "Built ${BUILD_DIR}/test_graph_diagnostics"
echo "Built ${BUILD_DIR}/test_solution_io"
echo "Built ${BUILD_DIR}/test_scenario_split_utils"
echo "Built ${BUILD_DIR}/test_static_dpv_benchmark"
echo "Built ${BUILD_DIR}/test_static_dpv_mip_benchmark"
echo "Built ${BUILD_DIR}/test_cumulative_propagation_graph"
echo "Built ${BUILD_DIR}/test_greedy_heuristics"
echo "Built ${BUILD_DIR}/test_warm_start"
echo "Built ${BUILD_DIR}/test_batch_experiment_config"
echo "Built ${BUILD_DIR}/test_experiment_aggregator"
echo "Built ${BUILD_DIR}/test_experiment_manifest"
echo "Built ${BUILD_DIR}/test_batch_summary_reporter"
echo "Built ${BUILD_DIR}/test_runtime_profiler"
echo "Built ${BUILD_DIR}/test_shared_splits"
echo "Built ${BUILD_DIR}/test_benders_cut"
echo "Built ${BUILD_DIR}/test_burn_frequency_candidate_scorer"
echo "Built ${BUILD_DIR}/test_benders_coefficient_candidate_scorer"
echo "Built ${BUILD_DIR}/test_cvar_tail_aware_benders_candidate_scorer"
echo "Built ${BUILD_DIR}/test_cvar_tail_score_diagnostics"
echo "Built ${BUILD_DIR}/test_restricted_candidate_cut_pool"
echo "Built ${BUILD_DIR}/test_restricted_candidate_manager"
echo "Built ${BUILD_DIR}/test_restricted_candidate_maintenance_tracker"
echo "Built ${BUILD_DIR}/test_candidate_bound_controller"
echo "Built ${BUILD_DIR}/test_dpv_subproblem_structure"
echo "Built ${BUILD_DIR}/test_dpv_benders_small"
echo "Built ${BUILD_DIR}/test_dpv_branch_benders_small"
echo "Built ${BUILD_DIR}/test_fpp_benders_small"
echo "Built ${BUILD_DIR}/test_fpp_branch_benders_small"
echo "Built ${BUILD_DIR}/test_fpp_persistent_scenario_subproblem_manager"
echo "Built ${BUILD_DIR}/test_fpp_strengthening"
echo "Built ${BUILD_DIR}/test_fpp_projected_llbi"
echo "Built ${BUILD_DIR}/test_fpp_combinatorial_benders"
echo "Built ${BUILD_DIR}/test_fpp_restricted_candidate_branch_benders_small"
if [[ "${WITH_CPLEX}" -eq 1 ]]; then
  echo "CPLEX support requested."
else
  echo "Built without CPLEX support."
fi
