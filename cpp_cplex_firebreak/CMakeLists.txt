cmake_minimum_required(VERSION 3.16)

project(cpp_cplex_firebreak LANGUAGES CXX)
enable_testing()

option(FIREBREAK_WITH_CPLEX "Build CPLEX-backed solver components" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(configure_cplex_if_enabled target_name)
    if (FIREBREAK_WITH_CPLEX)
        target_compile_definitions(${target_name} PRIVATE FIREBREAK_WITH_CPLEX)

        if (DEFINED ENV{CPLEX_STUDIO_DIR})
            target_include_directories(${target_name} PRIVATE
                "$ENV{CPLEX_STUDIO_DIR}/cplex/include"
                "$ENV{CPLEX_STUDIO_DIR}/concert/include"
            )
            target_link_directories(${target_name} PRIVATE
                "$ENV{CPLEX_STUDIO_DIR}/cplex/lib/x86-64_linux/static_pic"
                "$ENV{CPLEX_STUDIO_DIR}/concert/lib/x86-64_linux/static_pic"
            )
        endif()

        if (DEFINED ENV{CPLEX_INCLUDE_DIR})
            target_include_directories(${target_name} PRIVATE "$ENV{CPLEX_INCLUDE_DIR}")
        endif()
        if (DEFINED ENV{CPLEX_CONCERT_INCLUDE_DIR})
            target_include_directories(${target_name} PRIVATE "$ENV{CPLEX_CONCERT_INCLUDE_DIR}")
        endif()
        if (DEFINED ENV{CPLEX_LIB_DIR})
            target_link_directories(${target_name} PRIVATE "$ENV{CPLEX_LIB_DIR}")
        endif()
        if (DEFINED ENV{CPLEX_CONCERT_LIB_DIR})
            target_link_directories(${target_name} PRIVATE "$ENV{CPLEX_CONCERT_LIB_DIR}")
        endif()

        target_link_libraries(${target_name} PRIVATE ilocplex cplex concert)
        if (UNIX)
            target_link_libraries(${target_name} PRIVATE pthread dl m)
        endif()
    endif()
endfunction()

add_executable(firebreak_cpp
    src/analysis/BatchSummaryReporter.cpp
    src/analysis/GraphDiagnostics.cpp
    src/analysis/ExperimentAggregator.cpp
    src/analysis/RuntimeProfiler.cpp
    src/benders/BendersCut.cpp
    src/benders/BendersCoefficientCandidateScorer.cpp
    src/benders/BurnFrequencyCandidateScorer.cpp
    src/benders/CandidateBoundController.cpp
    src/benders/CvarTailAwareBendersCandidateScorer.cpp
    src/benders/CvarTailScoreDiagnostics.cpp
    src/benders/RestrictedCandidateCutPool.cpp
    src/benders/RestrictedCandidateManager.cpp
    src/benders/RestrictedCandidateMaintenanceTracker.cpp
    src/benders/FppCombinatorialBenders.cpp
    src/benders/FppLiftedLowerBound.cpp
    src/benders/FppScenarioSubproblem.cpp
    src/benders/FppPersistentScenarioSubproblemManager.cpp
    src/benders/FppBendersMaster.cpp
    src/benders/FppBendersSolver.cpp
    src/benders/FppBranchBendersSolver.cpp
    src/benders/FppRestrictedCandidateBranchBendersSolver.cpp
    src/benders/DpvLiftedLowerBound.cpp
    src/benders/DpvScenarioSubproblem.cpp
    src/benders/DpvPersistentScenarioSubproblemManager.cpp
    src/benders/DpvBendersMaster.cpp
    src/benders/DpvBendersSolver.cpp
    src/benders/DpvBranchBendersSolver.cpp
    src/benchmarks/StaticDpvBenchmark.cpp
    src/benchmarks/StaticDpvMipBenchmark.cpp
    src/core/FirebreakSolution.cpp
    src/core/Graph.cpp
    src/core/Scenario.cpp
    src/core/Instance.cpp
    src/cuts/DominatorCuts.cpp
    src/cuts/NodeSeparatorMinCut.cpp
    src/cuts/SeparatorCutSeparator.cpp
    src/cuts/SeparatorContextCallback.cpp
    src/eval/BurnedAreaEvaluator.cpp
    src/eval/FppRecourseEvaluator.cpp
    src/graph/DinicMaxFlow.cpp
    src/heuristics/CumulativePropagationGraph.cpp
    src/heuristics/GreedyMetrics.cpp
    src/heuristics/GreedyHeuristic.cpp
    src/heuristics/ReachabilityGreedyWarmStart.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/opt/DpvIndexBuilder.cpp
    src/opt/OptimizationInstanceBuilder.cpp
    src/risk/RiskMeasure.cpp
    src/solver/CplexEnvironment.cpp
    src/solver/WarmStart.cpp
    src/solver/FppSaaCplexModel.cpp
    src/solver/FppCutReachabilityCplexModel.cpp
    src/solver/DpvSaaCplexModel.cpp
    src/io/PathUtils.cpp
    src/io/ScenarioFileUtils.cpp
    src/io/ScenarioSplitUtils.cpp
    src/io/SolutionIO.cpp
    src/io/ExperimentResultWriter.cpp
    src/io/Cell2FireReader.cpp
    src/io/ResultWriter.cpp
    src/experiments/SmokeRunner.cpp
    src/experiments/BatchExperimentConfig.cpp
    src/experiments/SharedSplitUtils.cpp
    src/experiments/BatchExperimentRunner.cpp
    src/experiments/ExperimentManifest.cpp
    src/experiments/EvaluationRunner.cpp
    src/experiments/OptInstanceRunner.cpp
    src/experiments/FppSaaRunner.cpp
    src/experiments/FppMasterLpDiagnosticRunner.cpp
    src/experiments/FppDominancePreprocessingDiagnosticRunner.cpp
    src/experiments/FppLpViDiagnosticRunner.cpp
    src/experiments/GraphDiagnosticsRunner.cpp
    src/experiments/GreedyOutOfSampleRunner.cpp
    src/experiments/FppSaaOutOfSampleRunner.cpp
    src/experiments/FppBendersOutOfSampleRunner.cpp
    src/experiments/FppBranchBendersOutOfSampleRunner.cpp
    src/experiments/FppRestrictedCandidateBranchBendersOutOfSampleRunner.cpp
    src/experiments/StaticDpvOutOfSampleRunner.cpp
    src/experiments/DpvSaaOutOfSampleRunner.cpp
    src/experiments/DpvBendersOutOfSampleRunner.cpp
    src/experiments/DpvBranchBendersOutOfSampleRunner.cpp
    src/experiments/ManifestRunner.cpp
    src/experiments/MethodDispatcher.cpp
    src/main.cpp
)

target_include_directories(firebreak_cpp PRIVATE include)

target_compile_definitions(firebreak_cpp PRIVATE
    FIREBREAK_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    FIREBREAK_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

if (MSVC)
    target_compile_options(firebreak_cpp PRIVATE /W4)
else()
    target_compile_options(firebreak_cpp PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(firebreak_cpp)

add_executable(test_burned_area_evaluator
    src/core/FirebreakSolution.cpp
    src/core/Graph.cpp
    src/core/Scenario.cpp
    src/core/Instance.cpp
    src/eval/BurnedAreaEvaluator.cpp
    src/risk/RiskMeasure.cpp
    tests/test_burned_area_evaluator.cpp
)

target_include_directories(test_burned_area_evaluator PRIVATE include)

if (MSVC)
    target_compile_options(test_burned_area_evaluator PRIVATE /W4)
else()
    target_compile_options(test_burned_area_evaluator PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME burned_area_evaluator COMMAND test_burned_area_evaluator)

add_executable(test_risk_measure
    src/risk/RiskMeasure.cpp
    tests/test_risk_measure.cpp
)

target_include_directories(test_risk_measure PRIVATE include)

if (MSVC)
    target_compile_options(test_risk_measure PRIVATE /W4)
else()
    target_compile_options(test_risk_measure PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME risk_measure COMMAND test_risk_measure)

add_executable(test_fpp_recourse_evaluator
    src/core/FirebreakSolution.cpp
    src/core/Graph.cpp
    src/core/Scenario.cpp
    src/core/Instance.cpp
    src/eval/BurnedAreaEvaluator.cpp
    src/eval/FppRecourseEvaluator.cpp
    src/risk/RiskMeasure.cpp
    src/cuts/DominatorCuts.cpp
    src/cuts/NodeSeparatorMinCut.cpp
    src/cuts/SeparatorCutSeparator.cpp
    src/cuts/SeparatorContextCallback.cpp
    src/graph/DinicMaxFlow.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/opt/DpvIndexBuilder.cpp
    src/opt/OptimizationInstanceBuilder.cpp
    src/solver/CplexEnvironment.cpp
    src/solver/FppSaaCplexModel.cpp
    tests/test_fpp_recourse_evaluator.cpp
)

target_include_directories(test_fpp_recourse_evaluator PRIVATE include)

if (MSVC)
    target_compile_options(test_fpp_recourse_evaluator PRIVATE /W4)
else()
    target_compile_options(test_fpp_recourse_evaluator PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(test_fpp_recourse_evaluator)

add_test(NAME fpp_recourse_evaluator COMMAND test_fpp_recourse_evaluator)

add_executable(test_reachability_greedy_warm_start
    src/cuts/DominatorCuts.cpp
    src/cuts/NodeSeparatorMinCut.cpp
    src/cuts/SeparatorCutSeparator.cpp
    src/cuts/SeparatorContextCallback.cpp
    src/eval/FppRecourseEvaluator.cpp
    src/graph/DinicMaxFlow.cpp
    src/heuristics/ReachabilityGreedyWarmStart.cpp
    src/io/PathUtils.cpp
    src/io/SolutionIO.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/risk/RiskMeasure.cpp
    src/solver/CplexEnvironment.cpp
    src/solver/WarmStart.cpp
    src/solver/FppSaaCplexModel.cpp
    tests/test_reachability_greedy_warm_start.cpp
)

target_include_directories(test_reachability_greedy_warm_start PRIVATE include)

if (MSVC)
    target_compile_options(test_reachability_greedy_warm_start PRIVATE /W4)
else()
    target_compile_options(test_reachability_greedy_warm_start PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(test_reachability_greedy_warm_start)

add_test(NAME reachability_greedy_warm_start COMMAND test_reachability_greedy_warm_start)

add_executable(test_index_mapper
    src/opt/IndexMapper.cpp
    tests/test_index_mapper.cpp
)

target_include_directories(test_index_mapper PRIVATE include)

if (MSVC)
    target_compile_options(test_index_mapper PRIVATE /W4)
else()
    target_compile_options(test_index_mapper PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME index_mapper COMMAND test_index_mapper)

add_executable(test_dpv_index_builder
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/opt/DpvIndexBuilder.cpp
    tests/test_dpv_index_builder.cpp
)

target_include_directories(test_dpv_index_builder PRIVATE include)

if (MSVC)
    target_compile_options(test_dpv_index_builder PRIVATE /W4)
else()
    target_compile_options(test_dpv_index_builder PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME dpv_index_builder COMMAND test_dpv_index_builder)

add_executable(test_fpp_saa_model_structure
    src/cuts/DominatorCuts.cpp
    src/cuts/NodeSeparatorMinCut.cpp
    src/cuts/SeparatorCutSeparator.cpp
    src/cuts/SeparatorContextCallback.cpp
    src/graph/DinicMaxFlow.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/risk/RiskMeasure.cpp
    src/solver/CplexEnvironment.cpp
    src/solver/FppSaaCplexModel.cpp
    tests/test_fpp_saa_model_structure.cpp
)

target_include_directories(test_fpp_saa_model_structure PRIVATE include)

if (MSVC)
    target_compile_options(test_fpp_saa_model_structure PRIVATE /W4)
else()
    target_compile_options(test_fpp_saa_model_structure PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(test_fpp_saa_model_structure)

add_test(NAME fpp_saa_model_structure COMMAND test_fpp_saa_model_structure)

add_executable(test_fpp_cut_reachability_model
    src/benders/BendersCut.cpp
    src/benders/FppCombinatorialBenders.cpp
    src/cuts/DominatorCuts.cpp
    src/cuts/NodeSeparatorMinCut.cpp
    src/cuts/SeparatorCutSeparator.cpp
    src/cuts/SeparatorContextCallback.cpp
    src/eval/FppRecourseEvaluator.cpp
    src/experiments/BatchExperimentConfig.cpp
    src/graph/DinicMaxFlow.cpp
    src/heuristics/ReachabilityGreedyWarmStart.cpp
    src/io/PathUtils.cpp
    src/io/SolutionIO.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/risk/RiskMeasure.cpp
    src/solver/CplexEnvironment.cpp
    src/solver/WarmStart.cpp
    src/solver/FppSaaCplexModel.cpp
    src/solver/FppCutReachabilityCplexModel.cpp
    tests/test_fpp_cut_reachability_model.cpp
)

target_include_directories(test_fpp_cut_reachability_model PRIVATE include)

if (MSVC)
    target_compile_options(test_fpp_cut_reachability_model PRIVATE /W4)
else()
    target_compile_options(test_fpp_cut_reachability_model PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(test_fpp_cut_reachability_model)

add_test(NAME fpp_cut_reachability_model COMMAND test_fpp_cut_reachability_model)

add_executable(test_dominator_cuts
    src/cuts/DominatorCuts.cpp
    src/cuts/NodeSeparatorMinCut.cpp
    src/cuts/SeparatorCutSeparator.cpp
    src/cuts/SeparatorContextCallback.cpp
    src/eval/FppRecourseEvaluator.cpp
    src/graph/DinicMaxFlow.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/risk/RiskMeasure.cpp
    src/solver/CplexEnvironment.cpp
    src/solver/FppSaaCplexModel.cpp
    src/solver/FppCutReachabilityCplexModel.cpp
    tests/test_dominator_cuts.cpp
)

target_include_directories(test_dominator_cuts PRIVATE include)

if (MSVC)
    target_compile_options(test_dominator_cuts PRIVATE /W4)
else()
    target_compile_options(test_dominator_cuts PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(test_dominator_cuts)

add_test(NAME dominator_cuts COMMAND test_dominator_cuts)

add_executable(test_separator_context_callback
    src/cuts/DominatorCuts.cpp
    src/cuts/NodeSeparatorMinCut.cpp
    src/cuts/SeparatorCutSeparator.cpp
    src/cuts/SeparatorContextCallback.cpp
    src/eval/FppRecourseEvaluator.cpp
    src/graph/DinicMaxFlow.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/risk/RiskMeasure.cpp
    src/solver/CplexEnvironment.cpp
    src/solver/FppSaaCplexModel.cpp
    src/solver/FppCutReachabilityCplexModel.cpp
    tests/test_separator_context_callback.cpp
)

target_include_directories(test_separator_context_callback PRIVATE include)

if (MSVC)
    target_compile_options(test_separator_context_callback PRIVATE /W4)
else()
    target_compile_options(test_separator_context_callback PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(test_separator_context_callback)

add_test(NAME separator_context_callback COMMAND test_separator_context_callback)

add_executable(test_node_separator_min_cut
    src/cuts/NodeSeparatorMinCut.cpp
    src/graph/DinicMaxFlow.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    tests/test_node_separator_min_cut.cpp
)

target_include_directories(test_node_separator_min_cut PRIVATE include)

if (MSVC)
    target_compile_options(test_node_separator_min_cut PRIVATE /W4)
else()
    target_compile_options(test_node_separator_min_cut PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME node_separator_min_cut COMMAND test_node_separator_min_cut)

add_executable(test_dpv_saa_model_structure
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/opt/DpvIndexBuilder.cpp
    src/solver/CplexEnvironment.cpp
    src/solver/DpvSaaCplexModel.cpp
    tests/test_dpv_saa_model_structure.cpp
)

target_include_directories(test_dpv_saa_model_structure PRIVATE include)

if (MSVC)
    target_compile_options(test_dpv_saa_model_structure PRIVATE /W4)
else()
    target_compile_options(test_dpv_saa_model_structure PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(test_dpv_saa_model_structure)

add_test(NAME dpv_saa_model_structure COMMAND test_dpv_saa_model_structure)

add_executable(test_graph_diagnostics
    src/analysis/GraphDiagnostics.cpp
    src/core/Graph.cpp
    src/core/Scenario.cpp
    tests/test_graph_diagnostics.cpp
)

target_include_directories(test_graph_diagnostics PRIVATE include)

if (MSVC)
    target_compile_options(test_graph_diagnostics PRIVATE /W4)
else()
    target_compile_options(test_graph_diagnostics PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME graph_diagnostics COMMAND test_graph_diagnostics)

add_executable(test_solution_io
    src/io/ExperimentResultWriter.cpp
    src/io/SolutionIO.cpp
    src/io/PathUtils.cpp
    tests/test_solution_io.cpp
)

target_include_directories(test_solution_io PRIVATE include)
target_compile_definitions(test_solution_io PRIVATE
    FIREBREAK_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    FIREBREAK_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

if (MSVC)
    target_compile_options(test_solution_io PRIVATE /W4)
else()
    target_compile_options(test_solution_io PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME solution_io COMMAND test_solution_io)

add_executable(test_scenario_split_utils
    src/io/ScenarioFileUtils.cpp
    src/io/ScenarioSplitUtils.cpp
    src/io/PathUtils.cpp
    tests/test_scenario_split_utils.cpp
)

target_include_directories(test_scenario_split_utils PRIVATE include)
target_compile_definitions(test_scenario_split_utils PRIVATE
    FIREBREAK_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    FIREBREAK_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

if (MSVC)
    target_compile_options(test_scenario_split_utils PRIVATE /W4)
else()
    target_compile_options(test_scenario_split_utils PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME scenario_split_utils COMMAND test_scenario_split_utils)

add_executable(test_static_dpv_benchmark
    src/benchmarks/StaticDpvBenchmark.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/opt/DpvIndexBuilder.cpp
    tests/test_static_dpv_benchmark.cpp
)

target_include_directories(test_static_dpv_benchmark PRIVATE include)

if (MSVC)
    target_compile_options(test_static_dpv_benchmark PRIVATE /W4)
else()
    target_compile_options(test_static_dpv_benchmark PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME static_dpv_benchmark COMMAND test_static_dpv_benchmark)

add_executable(test_static_dpv_mip_benchmark
    src/benchmarks/StaticDpvBenchmark.cpp
    src/benchmarks/StaticDpvMipBenchmark.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/opt/DpvIndexBuilder.cpp
    tests/test_static_dpv_mip_benchmark.cpp
)

target_include_directories(test_static_dpv_mip_benchmark PRIVATE include)

if (MSVC)
    target_compile_options(test_static_dpv_mip_benchmark PRIVATE /W4)
else()
    target_compile_options(test_static_dpv_mip_benchmark PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME static_dpv_mip_benchmark COMMAND test_static_dpv_mip_benchmark)

add_executable(test_cumulative_propagation_graph
    src/heuristics/CumulativePropagationGraph.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    tests/test_cumulative_propagation_graph.cpp
)

target_include_directories(test_cumulative_propagation_graph PRIVATE include)

if (MSVC)
    target_compile_options(test_cumulative_propagation_graph PRIVATE /W4)
else()
    target_compile_options(test_cumulative_propagation_graph PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME cumulative_propagation_graph COMMAND test_cumulative_propagation_graph)

add_executable(test_greedy_heuristics
    src/heuristics/CumulativePropagationGraph.cpp
    src/heuristics/GreedyMetrics.cpp
    src/heuristics/GreedyHeuristic.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    tests/test_greedy_heuristics.cpp
)

target_include_directories(test_greedy_heuristics PRIVATE include)

if (MSVC)
    target_compile_options(test_greedy_heuristics PRIVATE /W4)
else()
    target_compile_options(test_greedy_heuristics PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME greedy_heuristics COMMAND test_greedy_heuristics)

add_executable(test_warm_start
    src/io/PathUtils.cpp
    src/io/SolutionIO.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/solver/WarmStart.cpp
    tests/test_warm_start.cpp
)

target_include_directories(test_warm_start PRIVATE include)
target_compile_definitions(test_warm_start PRIVATE
    FIREBREAK_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    FIREBREAK_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

if (MSVC)
    target_compile_options(test_warm_start PRIVATE /W4)
else()
    target_compile_options(test_warm_start PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME warm_start COMMAND test_warm_start)

add_executable(test_batch_experiment_config
    src/benders/BendersCut.cpp
    src/benders/FppCombinatorialBenders.cpp
    src/experiments/BatchExperimentConfig.cpp
    src/opt/IndexMapper.cpp
    src/risk/RiskMeasure.cpp
    tests/test_batch_experiment_config.cpp
)

target_include_directories(test_batch_experiment_config PRIVATE include)

if (MSVC)
    target_compile_options(test_batch_experiment_config PRIVATE /W4)
else()
    target_compile_options(test_batch_experiment_config PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME batch_experiment_config COMMAND test_batch_experiment_config)

add_executable(test_experiment_aggregator
    src/analysis/ExperimentAggregator.cpp
    src/io/PathUtils.cpp
    tests/test_experiment_aggregator.cpp
)

target_include_directories(test_experiment_aggregator PRIVATE include)
target_compile_definitions(test_experiment_aggregator PRIVATE
    FIREBREAK_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    FIREBREAK_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

if (MSVC)
    target_compile_options(test_experiment_aggregator PRIVATE /W4)
else()
    target_compile_options(test_experiment_aggregator PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME experiment_aggregator COMMAND test_experiment_aggregator)

add_executable(test_experiment_manifest
    src/benders/BendersCut.cpp
    src/benders/FppCombinatorialBenders.cpp
    src/experiments/BatchExperimentConfig.cpp
    src/experiments/ExperimentManifest.cpp
    src/io/PathUtils.cpp
    src/opt/IndexMapper.cpp
    src/risk/RiskMeasure.cpp
    tests/test_experiment_manifest.cpp
)

target_include_directories(test_experiment_manifest PRIVATE include)
target_compile_definitions(test_experiment_manifest PRIVATE
    FIREBREAK_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    FIREBREAK_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

if (MSVC)
    target_compile_options(test_experiment_manifest PRIVATE /W4)
else()
    target_compile_options(test_experiment_manifest PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME experiment_manifest COMMAND test_experiment_manifest)

add_executable(test_batch_summary_reporter
    src/analysis/BatchSummaryReporter.cpp
    src/io/PathUtils.cpp
    tests/test_batch_summary_reporter.cpp
)

target_include_directories(test_batch_summary_reporter PRIVATE include)
target_compile_definitions(test_batch_summary_reporter PRIVATE
    FIREBREAK_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    FIREBREAK_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

if (MSVC)
    target_compile_options(test_batch_summary_reporter PRIVATE /W4)
else()
    target_compile_options(test_batch_summary_reporter PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME batch_summary_reporter COMMAND test_batch_summary_reporter)

add_executable(test_runtime_profiler
    src/analysis/RuntimeProfiler.cpp
    src/io/PathUtils.cpp
    tests/test_runtime_profiler.cpp
)

target_include_directories(test_runtime_profiler PRIVATE include)
target_compile_definitions(test_runtime_profiler PRIVATE
    FIREBREAK_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    FIREBREAK_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

if (MSVC)
    target_compile_options(test_runtime_profiler PRIVATE /W4)
else()
    target_compile_options(test_runtime_profiler PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME runtime_profiler COMMAND test_runtime_profiler)

add_executable(test_shared_splits
    src/experiments/SharedSplitUtils.cpp
    src/io/ScenarioSplitUtils.cpp
    src/io/PathUtils.cpp
    tests/test_shared_splits.cpp
)

target_include_directories(test_shared_splits PRIVATE include)
target_compile_definitions(test_shared_splits PRIVATE
    FIREBREAK_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    FIREBREAK_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

if (MSVC)
    target_compile_options(test_shared_splits PRIVATE /W4)
else()
    target_compile_options(test_shared_splits PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME shared_splits COMMAND test_shared_splits)

add_executable(test_benders_cut
    src/benders/BendersCut.cpp
    tests/test_benders_cut.cpp
)

target_include_directories(test_benders_cut PRIVATE include)

if (MSVC)
    target_compile_options(test_benders_cut PRIVATE /W4)
else()
    target_compile_options(test_benders_cut PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME benders_cut COMMAND test_benders_cut)

add_executable(test_burn_frequency_candidate_scorer
    src/benders/BurnFrequencyCandidateScorer.cpp
    src/benders/RestrictedCandidateManager.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    tests/test_burn_frequency_candidate_scorer.cpp
)

target_include_directories(test_burn_frequency_candidate_scorer PRIVATE include)

if (MSVC)
    target_compile_options(test_burn_frequency_candidate_scorer PRIVATE /W4)
else()
    target_compile_options(test_burn_frequency_candidate_scorer PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME burn_frequency_candidate_scorer COMMAND test_burn_frequency_candidate_scorer)

add_executable(test_benders_coefficient_candidate_scorer
    src/benders/BendersCut.cpp
    src/benders/BendersCoefficientCandidateScorer.cpp
    src/benders/RestrictedCandidateManager.cpp
    tests/test_benders_coefficient_candidate_scorer.cpp
)

target_include_directories(test_benders_coefficient_candidate_scorer PRIVATE include)

if (MSVC)
    target_compile_options(test_benders_coefficient_candidate_scorer PRIVATE /W4)
else()
    target_compile_options(test_benders_coefficient_candidate_scorer PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME benders_coefficient_candidate_scorer COMMAND test_benders_coefficient_candidate_scorer)

add_executable(test_cvar_tail_score_diagnostics
    src/benders/BendersCut.cpp
    src/benders/BendersCoefficientCandidateScorer.cpp
    src/benders/CvarTailAwareBendersCandidateScorer.cpp
    src/benders/CvarTailScoreDiagnostics.cpp
    src/benders/RestrictedCandidateManager.cpp
    src/io/ExperimentResultWriter.cpp
    src/io/PathUtils.cpp
    tests/test_cvar_tail_score_diagnostics.cpp
)

target_include_directories(test_cvar_tail_score_diagnostics PRIVATE include)

if (MSVC)
    target_compile_options(test_cvar_tail_score_diagnostics PRIVATE /W4)
else()
    target_compile_options(test_cvar_tail_score_diagnostics PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME cvar_tail_score_diagnostics COMMAND test_cvar_tail_score_diagnostics)

add_executable(test_cvar_tail_aware_benders_candidate_scorer
    src/benders/BendersCut.cpp
    src/benders/BendersCoefficientCandidateScorer.cpp
    src/benders/CvarTailAwareBendersCandidateScorer.cpp
    tests/test_cvar_tail_aware_benders_candidate_scorer.cpp
)

target_include_directories(test_cvar_tail_aware_benders_candidate_scorer PRIVATE include)

if (MSVC)
    target_compile_options(test_cvar_tail_aware_benders_candidate_scorer PRIVATE /W4)
else()
    target_compile_options(test_cvar_tail_aware_benders_candidate_scorer PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME cvar_tail_aware_benders_candidate_scorer COMMAND test_cvar_tail_aware_benders_candidate_scorer)

add_executable(test_restricted_candidate_cut_pool
    src/benders/BendersCut.cpp
    src/benders/RestrictedCandidateCutPool.cpp
    tests/test_restricted_candidate_cut_pool.cpp
)

target_include_directories(test_restricted_candidate_cut_pool PRIVATE include)

if (MSVC)
    target_compile_options(test_restricted_candidate_cut_pool PRIVATE /W4)
else()
    target_compile_options(test_restricted_candidate_cut_pool PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME restricted_candidate_cut_pool COMMAND test_restricted_candidate_cut_pool)

add_executable(test_restricted_candidate_manager
    src/benders/RestrictedCandidateManager.cpp
    tests/test_restricted_candidate_manager.cpp
)

target_include_directories(test_restricted_candidate_manager PRIVATE include)

if (MSVC)
    target_compile_options(test_restricted_candidate_manager PRIVATE /W4)
else()
    target_compile_options(test_restricted_candidate_manager PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME restricted_candidate_manager COMMAND test_restricted_candidate_manager)

add_executable(test_restricted_candidate_maintenance_tracker
    src/benders/RestrictedCandidateManager.cpp
    src/benders/RestrictedCandidateMaintenanceTracker.cpp
    tests/test_restricted_candidate_maintenance_tracker.cpp
)

target_include_directories(test_restricted_candidate_maintenance_tracker PRIVATE include)

if (MSVC)
    target_compile_options(test_restricted_candidate_maintenance_tracker PRIVATE /W4)
else()
    target_compile_options(test_restricted_candidate_maintenance_tracker PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME restricted_candidate_maintenance_tracker COMMAND test_restricted_candidate_maintenance_tracker)

add_executable(test_candidate_bound_controller
    src/benders/CandidateBoundController.cpp
    src/benders/RestrictedCandidateManager.cpp
    tests/test_candidate_bound_controller.cpp
)

target_include_directories(test_candidate_bound_controller PRIVATE include)

if (MSVC)
    target_compile_options(test_candidate_bound_controller PRIVATE /W4)
else()
    target_compile_options(test_candidate_bound_controller PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME candidate_bound_controller COMMAND test_candidate_bound_controller)

add_executable(test_fpp_benders_small
    src/benders/BendersCut.cpp
    src/benders/FppLiftedLowerBound.cpp
    src/benders/FppScenarioSubproblem.cpp
    src/benders/FppBendersMaster.cpp
    src/benders/FppBendersSolver.cpp
    src/cuts/DominatorCuts.cpp
    src/cuts/NodeSeparatorMinCut.cpp
    src/cuts/SeparatorCutSeparator.cpp
    src/cuts/SeparatorContextCallback.cpp
    src/eval/FppRecourseEvaluator.cpp
    src/graph/DinicMaxFlow.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/risk/RiskMeasure.cpp
    src/solver/CplexEnvironment.cpp
    src/solver/FppSaaCplexModel.cpp
    tests/test_fpp_benders_small.cpp
)

target_include_directories(test_fpp_benders_small PRIVATE include)

if (MSVC)
    target_compile_options(test_fpp_benders_small PRIVATE /W4)
else()
    target_compile_options(test_fpp_benders_small PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(test_fpp_benders_small)

add_test(NAME fpp_benders_small COMMAND test_fpp_benders_small)

add_executable(test_fpp_branch_benders_small
    src/benders/BendersCut.cpp
    src/benders/FppCombinatorialBenders.cpp
    src/benders/FppLiftedLowerBound.cpp
    src/benders/FppScenarioSubproblem.cpp
    src/benders/FppPersistentScenarioSubproblemManager.cpp
    src/benders/FppBendersMaster.cpp
    src/benders/FppBendersSolver.cpp
    src/benders/FppBranchBendersSolver.cpp
    src/cuts/DominatorCuts.cpp
    src/cuts/NodeSeparatorMinCut.cpp
    src/cuts/SeparatorCutSeparator.cpp
    src/cuts/SeparatorContextCallback.cpp
    src/eval/FppRecourseEvaluator.cpp
    src/graph/DinicMaxFlow.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/risk/RiskMeasure.cpp
    src/solver/CplexEnvironment.cpp
    src/solver/FppSaaCplexModel.cpp
    tests/test_fpp_branch_benders_small.cpp
)

target_include_directories(test_fpp_branch_benders_small PRIVATE include)

if (MSVC)
    target_compile_options(test_fpp_branch_benders_small PRIVATE /W4)
else()
    target_compile_options(test_fpp_branch_benders_small PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(test_fpp_branch_benders_small)

add_test(NAME fpp_branch_benders_small COMMAND test_fpp_branch_benders_small)

add_executable(test_fpp_persistent_scenario_subproblem_manager
    src/benders/BendersCut.cpp
    src/benders/FppScenarioSubproblem.cpp
    src/benders/FppPersistentScenarioSubproblemManager.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/solver/CplexEnvironment.cpp
    tests/test_fpp_persistent_scenario_subproblem_manager.cpp
)

target_include_directories(test_fpp_persistent_scenario_subproblem_manager PRIVATE include)

if (MSVC)
    target_compile_options(test_fpp_persistent_scenario_subproblem_manager PRIVATE /W4)
else()
    target_compile_options(test_fpp_persistent_scenario_subproblem_manager PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(test_fpp_persistent_scenario_subproblem_manager)

add_test(NAME fpp_persistent_scenario_subproblem_manager COMMAND test_fpp_persistent_scenario_subproblem_manager)

add_executable(test_fpp_strengthening
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    tests/test_fpp_strengthening.cpp
)

target_include_directories(test_fpp_strengthening PRIVATE include)

if (MSVC)
    target_compile_options(test_fpp_strengthening PRIVATE /W4)
else()
    target_compile_options(test_fpp_strengthening PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME fpp_strengthening COMMAND test_fpp_strengthening)

add_executable(test_fpp_combinatorial_benders
    src/benders/BendersCut.cpp
    src/benders/FppCombinatorialBenders.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    tests/test_fpp_combinatorial_benders.cpp
)

target_include_directories(test_fpp_combinatorial_benders PRIVATE include)

if (MSVC)
    target_compile_options(test_fpp_combinatorial_benders PRIVATE /W4)
else()
    target_compile_options(test_fpp_combinatorial_benders PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME fpp_combinatorial_benders COMMAND test_fpp_combinatorial_benders)

add_executable(test_fpp_restricted_candidate_branch_benders_small
    src/benders/BendersCut.cpp
    src/benders/BendersCoefficientCandidateScorer.cpp
    src/benders/BurnFrequencyCandidateScorer.cpp
    src/benders/CandidateBoundController.cpp
    src/benders/CvarTailAwareBendersCandidateScorer.cpp
    src/benders/CvarTailScoreDiagnostics.cpp
    src/benders/RestrictedCandidateCutPool.cpp
    src/benders/RestrictedCandidateManager.cpp
    src/benders/RestrictedCandidateMaintenanceTracker.cpp
    src/benders/FppCombinatorialBenders.cpp
    src/benders/FppLiftedLowerBound.cpp
    src/benders/FppScenarioSubproblem.cpp
    src/benders/FppPersistentScenarioSubproblemManager.cpp
    src/benders/FppBranchBendersSolver.cpp
    src/benders/FppRestrictedCandidateBranchBendersSolver.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/solver/CplexEnvironment.cpp
    src/risk/RiskMeasure.cpp
    tests/test_fpp_restricted_candidate_branch_benders_small.cpp
)

target_include_directories(test_fpp_restricted_candidate_branch_benders_small PRIVATE include)

if (MSVC)
    target_compile_options(test_fpp_restricted_candidate_branch_benders_small PRIVATE /W4)
else()
    target_compile_options(test_fpp_restricted_candidate_branch_benders_small PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(test_fpp_restricted_candidate_branch_benders_small)

add_test(NAME fpp_restricted_candidate_branch_benders_small COMMAND test_fpp_restricted_candidate_branch_benders_small)

add_executable(test_dpv_subproblem_structure
    src/benders/BendersCut.cpp
    src/benders/DpvLiftedLowerBound.cpp
    src/benders/DpvScenarioSubproblem.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/opt/DpvIndexBuilder.cpp
    src/solver/CplexEnvironment.cpp
    tests/test_dpv_subproblem_structure.cpp
)

target_include_directories(test_dpv_subproblem_structure PRIVATE include)

if (MSVC)
    target_compile_options(test_dpv_subproblem_structure PRIVATE /W4)
else()
    target_compile_options(test_dpv_subproblem_structure PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(test_dpv_subproblem_structure)

add_test(NAME dpv_subproblem_structure COMMAND test_dpv_subproblem_structure)

add_executable(test_dpv_benders_small
    src/benders/BendersCut.cpp
    src/benders/DpvLiftedLowerBound.cpp
    src/benders/DpvScenarioSubproblem.cpp
    src/benders/DpvBendersMaster.cpp
    src/benders/DpvBendersSolver.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/opt/DpvIndexBuilder.cpp
    src/solver/CplexEnvironment.cpp
    src/solver/DpvSaaCplexModel.cpp
    tests/test_dpv_benders_small.cpp
)

target_include_directories(test_dpv_benders_small PRIVATE include)

if (MSVC)
    target_compile_options(test_dpv_benders_small PRIVATE /W4)
else()
    target_compile_options(test_dpv_benders_small PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(test_dpv_benders_small)

add_test(NAME dpv_benders_small COMMAND test_dpv_benders_small)

add_executable(test_dpv_branch_benders_small
    src/benders/BendersCut.cpp
    src/benders/DpvLiftedLowerBound.cpp
    src/benders/DpvScenarioSubproblem.cpp
    src/benders/DpvPersistentScenarioSubproblemManager.cpp
    src/benders/DpvBendersMaster.cpp
    src/benders/DpvBendersSolver.cpp
    src/benders/DpvBranchBendersSolver.cpp
    src/opt/IndexMapper.cpp
    src/opt/OptimizationInstance.cpp
    src/opt/DpvIndexBuilder.cpp
    src/solver/CplexEnvironment.cpp
    src/solver/DpvSaaCplexModel.cpp
    tests/test_dpv_branch_benders_small.cpp
)

target_include_directories(test_dpv_branch_benders_small PRIVATE include)

if (MSVC)
    target_compile_options(test_dpv_branch_benders_small PRIVATE /W4)
else()
    target_compile_options(test_dpv_branch_benders_small PRIVATE -Wall -Wextra -pedantic)
endif()

configure_cplex_if_enabled(test_dpv_branch_benders_small)

add_test(NAME dpv_branch_benders_small COMMAND test_dpv_branch_benders_small)
