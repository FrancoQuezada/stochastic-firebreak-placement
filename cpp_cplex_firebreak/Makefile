SHELL := /usr/bin/env bash

CXX ?= g++
PROJECT_DIR := $(CURDIR)
REPO_ROOT := $(abspath $(PROJECT_DIR)/..)
CPLEX_STUDIO_DIR ?= /opt/ibm/ILOG/CPLEX_Studio2211
STUDY_FPP_RISK_OUTPUT_DIR ?= results/computational_study_fpp_risk
STUDY_FPP_RISK_PARALLEL ?= 1
STUDY_FPP_RISK_ARGS ?=

BUILD_DIR := build_gpp
BINARY := $(BUILD_DIR)/firebreak_cpp
CPLEX_EXE := $(BUILD_DIR)/cplex/firebreak_cpp
NOCPLEX_EXE := $(BUILD_DIR)/nocplex/firebreak_cpp
CPLEX_OBJ_DIR := $(BUILD_DIR)/obj/cplex
NOCPLEX_OBJ_DIR := $(BUILD_DIR)/obj/nocplex
TEST_OBJ_DIR := $(BUILD_DIR)/obj/tests

.DEFAULT_GOAL := cplex

COMMON_CPPFLAGS := \
	-I$(PROJECT_DIR)/include \
	-DFIREBREAK_PROJECT_ROOT=\"$(PROJECT_DIR)\" \
	-DFIREBREAK_REPO_ROOT=\"$(REPO_ROOT)\"

COMMON_CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic -MMD -MP

CPLEX_CPPFLAGS := -DFIREBREAK_WITH_CPLEX
CPLEX_LDFLAGS :=

ifneq ($(strip $(CPLEX_STUDIO_DIR)),)
CPLEX_CPPFLAGS += \
	-I$(CPLEX_STUDIO_DIR)/cplex/include \
	-I$(CPLEX_STUDIO_DIR)/concert/include
CPLEX_LDFLAGS += \
	-L$(CPLEX_STUDIO_DIR)/cplex/lib/x86-64_linux/static_pic \
	-L$(CPLEX_STUDIO_DIR)/concert/lib/x86-64_linux/static_pic
endif

ifneq ($(strip $(CPLEX_INCLUDE_DIR)),)
CPLEX_CPPFLAGS += $(foreach dir,$(subst :, ,$(CPLEX_INCLUDE_DIR)),-I$(dir))
endif

ifneq ($(strip $(CPLEX_CONCERT_INCLUDE_DIR)),)
CPLEX_CPPFLAGS += $(foreach dir,$(subst :, ,$(CPLEX_CONCERT_INCLUDE_DIR)),-I$(dir))
endif

ifneq ($(strip $(CPLEX_LIB_DIR)),)
CPLEX_LDFLAGS += $(foreach dir,$(subst :, ,$(CPLEX_LIB_DIR)),-L$(dir))
endif

ifneq ($(strip $(CPLEX_CONCERT_LIB_DIR)),)
CPLEX_LDFLAGS += $(foreach dir,$(subst :, ,$(CPLEX_CONCERT_LIB_DIR)),-L$(dir))
endif

CPLEX_LDFLAGS += -lilocplex -lcplex -lconcert -lm -lpthread -ldl

MAIN_SOURCES := \
	src/analysis/BatchSummaryReporter.cpp \
	src/analysis/GraphDiagnostics.cpp \
	src/analysis/ExperimentAggregator.cpp \
	src/analysis/RuntimeProfiler.cpp \
	src/benders/BendersCut.cpp \
	src/benders/BendersCoefficientCandidateScorer.cpp \
	src/benders/BurnFrequencyCandidateScorer.cpp \
	src/benders/CandidateBoundController.cpp \
	src/benders/CvarTailAwareBendersCandidateScorer.cpp \
	src/benders/CvarTailScoreDiagnostics.cpp \
	src/benders/RestrictedCandidateCutPool.cpp \
	src/benders/RestrictedCandidateManager.cpp \
	src/benders/RestrictedCandidateMaintenanceTracker.cpp \
	src/benders/FppCombinatorialBenders.cpp \
	src/benders/FppProjectedLlbi.cpp \
	src/benders/FppLiftedLowerBound.cpp \
	src/benders/FppScenarioSubproblem.cpp \
	src/benders/FppPersistentScenarioSubproblemManager.cpp \
	src/benders/FppBendersMaster.cpp \
	src/benders/FppBendersSolver.cpp \
	src/benders/FppBranchBendersSolver.cpp \
	src/benders/FppRestrictedCandidateBranchBendersSolver.cpp \
	src/benders/DpvLiftedLowerBound.cpp \
	src/benders/DpvScenarioSubproblem.cpp \
	src/benders/DpvPersistentScenarioSubproblemManager.cpp \
	src/benders/DpvBendersMaster.cpp \
	src/benders/DpvBendersSolver.cpp \
	src/benders/DpvBranchBendersSolver.cpp \
	src/benchmarks/StaticDpvBenchmark.cpp \
	src/benchmarks/StaticDpvMipBenchmark.cpp \
	src/core/FirebreakSolution.cpp \
	src/core/Graph.cpp \
	src/core/Scenario.cpp \
	src/core/Instance.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/core/LandscapeWeightGenerator.cpp \
	src/core/CanonicalLandscape.cpp \
	src/cuts/DominatorCuts.cpp \
	src/cuts/NodeSeparatorMinCut.cpp \
	src/cuts/SeparatorCutSeparator.cpp \
	src/cuts/SeparatorContextCallback.cpp \
	src/eval/BurnedAreaEvaluator.cpp \
	src/eval/FppRecourseEvaluator.cpp \
	src/graph/DinicMaxFlow.cpp \
	src/heuristics/CumulativePropagationGraph.cpp \
	src/heuristics/GreedyMetrics.cpp \
	src/heuristics/GreedyHeuristic.cpp \
	src/heuristics/ReachabilityGreedyWarmStart.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	src/opt/OptimizationInstanceBuilder.cpp \
	src/risk/RiskMeasure.cpp \
	src/solver/CplexEnvironment.cpp \
	src/solver/WarmStart.cpp \
	src/solver/FppWeightedLossUtils.cpp \
	src/solver/FppSaaCplexModel.cpp \
	src/solver/FppCutReachabilityCplexModel.cpp \
	src/solver/DpvSaaCplexModel.cpp \
	src/io/PathUtils.cpp \
	src/io/ScenarioFileUtils.cpp \
	src/io/ScenarioSplitUtils.cpp \
	src/io/SolutionIO.cpp \
	src/io/ExperimentResultWriter.cpp \
	src/io/Cell2FireReader.cpp \
	src/io/ResultWriter.cpp \
	src/experiments/SmokeRunner.cpp \
	src/experiments/NewInstancesSmokeRunner.cpp \
	src/experiments/WeightMapGenerationRunner.cpp \
	src/experiments/LandscapeUniverseBuilder.cpp \
	src/experiments/WeightMapRegistry.cpp \
	src/experiments/WeightMapRegistryRunner.cpp \
	src/experiments/PairedInstanceWeightMapping.cpp \
	src/experiments/WeightedMethodCapability.cpp \
	src/experiments/BatchExperimentConfig.cpp \
	src/experiments/SharedSplitUtils.cpp \
	src/experiments/BatchExperimentRunner.cpp \
	src/experiments/ExperimentManifest.cpp \
	src/experiments/EvaluationRunner.cpp \
	src/experiments/OptInstanceRunner.cpp \
	src/experiments/FppSaaRunner.cpp \
	src/experiments/GraphDiagnosticsRunner.cpp \
	src/experiments/GreedyOutOfSampleRunner.cpp \
	src/experiments/FppMasterLpDiagnosticRunner.cpp \
	src/experiments/FppDominancePreprocessingDiagnosticRunner.cpp \
	src/experiments/FppSaaOutOfSampleRunner.cpp \
	src/experiments/FppBendersOutOfSampleRunner.cpp \
	src/experiments/FppBranchBendersOutOfSampleRunner.cpp \
	src/experiments/FppRestrictedCandidateBranchBendersOutOfSampleRunner.cpp \
	src/experiments/FppLpViDiagnosticRunner.cpp \
	src/experiments/StaticDpvOutOfSampleRunner.cpp \
	src/experiments/DpvSaaOutOfSampleRunner.cpp \
	src/experiments/DpvBendersOutOfSampleRunner.cpp \
	src/experiments/DpvBranchBendersOutOfSampleRunner.cpp \
	src/experiments/ManifestRunner.cpp \
	src/experiments/MethodDispatcher.cpp \
	src/main.cpp

CPLEX_OBJECTS := $(patsubst %.cpp,$(CPLEX_OBJ_DIR)/%.o,$(MAIN_SOURCES))
NOCPLEX_OBJECTS := $(patsubst %.cpp,$(NOCPLEX_OBJ_DIR)/%.o,$(MAIN_SOURCES))

TEST_BURNED_AREA_SOURCES := \
	src/core/FirebreakSolution.cpp \
	src/core/Graph.cpp \
	src/core/Scenario.cpp \
	src/core/Instance.cpp \
	src/eval/BurnedAreaEvaluator.cpp \
	src/risk/RiskMeasure.cpp \
	tests/test_burned_area_evaluator.cpp

TEST_FPP_RECOURSE_EVALUATOR_SOURCES := \
	src/core/FirebreakSolution.cpp \
	src/core/Graph.cpp \
	src/core/Scenario.cpp \
	src/core/Instance.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/eval/BurnedAreaEvaluator.cpp \
	src/eval/FppRecourseEvaluator.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/OptimizationInstanceBuilder.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/risk/RiskMeasure.cpp \
	tests/test_fpp_recourse_evaluator.cpp

TEST_WEIGHTED_FPP_RECOURSE_EVALUATOR_SOURCES := \
	src/core/LandscapeWeightMap.cpp \
	src/core/LandscapeWeightGenerator.cpp \
	src/eval/FppRecourseEvaluator.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	tests/test_weighted_fpp_recourse_evaluator.cpp

TEST_RISK_MEASURE_SOURCES := \
	src/risk/RiskMeasure.cpp \
	tests/test_risk_measure.cpp

TEST_INDEX_MAPPER_SOURCES := \
	src/opt/IndexMapper.cpp \
	tests/test_index_mapper.cpp

TEST_LANDSCAPE_WEIGHT_MAP_SOURCES := \
	src/core/FirebreakSolution.cpp \
	src/core/Graph.cpp \
	src/core/Scenario.cpp \
	src/core/Instance.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/OptimizationInstanceBuilder.cpp \
	src/opt/DpvIndexBuilder.cpp \
	tests/test_landscape_weight_map.cpp

TEST_LANDSCAPE_WEIGHT_GENERATOR_SOURCES := \
	src/core/LandscapeWeightMap.cpp \
	src/core/LandscapeWeightGenerator.cpp \
	src/opt/IndexMapper.cpp \
	tests/test_landscape_weight_generator.cpp

TEST_WEIGHT_CANONICAL_LANDSCAPE_SOURCES := \
	src/core/LandscapeWeightMap.cpp \
	src/core/LandscapeWeightGenerator.cpp \
	src/core/CanonicalLandscape.cpp \
	src/opt/IndexMapper.cpp \
	tests/test_weight_canonical_landscape.cpp

TEST_WEIGHT_MAP_REGISTRY_SOURCES := \
	src/core/LandscapeWeightMap.cpp \
	src/core/LandscapeWeightGenerator.cpp \
	src/core/CanonicalLandscape.cpp \
	src/opt/IndexMapper.cpp \
	src/experiments/WeightMapRegistry.cpp \
	tests/test_weight_map_registry.cpp

TEST_WEIGHT_PAIRED_INSTANCE_MAPPING_SOURCES := \
	src/core/LandscapeWeightMap.cpp \
	src/opt/IndexMapper.cpp \
	src/experiments/PairedInstanceWeightMapping.cpp \
	tests/test_weight_paired_instance_mapping.cpp

TEST_WEIGHT_CAPABILITY_MATRIX_SOURCES := \
	src/experiments/WeightedMethodCapability.cpp \
	tests/test_weight_capability_matrix.cpp

TEST_WEIGHTED_BATCH_CONFIG_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/FppCombinatorialBenders.cpp \
	src/benders/FppProjectedLlbi.cpp \
	src/experiments/BatchExperimentConfig.cpp \
	src/experiments/ExperimentManifest.cpp \
	src/io/PathUtils.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	tests/test_weighted_batch_config.cpp

TEST_WEIGHTED_SUBSET_ATTACHMENT_SOURCES := \
	src/core/FirebreakSolution.cpp \
	src/core/Graph.cpp \
	src/core/Instance.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/core/Scenario.cpp \
	src/io/PathUtils.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/OptimizationInstanceBuilder.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/risk/RiskMeasure.cpp \
	src/solver/FppWeightedLossUtils.cpp \
	tests/test_weighted_subset_attachment.cpp

TEST_WEIGHTED_CAPABILITY_CROSS_CHECK_SOURCES := \
	src/experiments/WeightedMethodCapability.cpp \
	tests/test_weighted_capability_cross_check.cpp

TEST_DPV_INDEX_SOURCES := \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	tests/test_dpv_index_builder.cpp

TEST_WEIGHTED_FPP_DPV_SOURCES := \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	tests/test_weighted_fpp_dpv.cpp

TEST_WEIGHTED_FPP_DPV_VARIANTS_SOURCES := \
	src/benchmarks/StaticDpvBenchmark.cpp \
	src/benchmarks/StaticDpvMipBenchmark.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	tests/test_weighted_fpp_dpv_variants.cpp

TEST_WEIGHTED_FPP_DPV_CACHE_SOURCES := \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	tests/test_weighted_fpp_dpv_cache.cpp

TEST_WEIGHTED_FPP_DPV_DIAGNOSTICS_SOURCES := \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	tests/test_weighted_fpp_dpv_diagnostics.cpp

TEST_WEIGHTED_STATIC_DPV_HEURISTIC_SOURCES := \
	src/benchmarks/StaticDpvBenchmark.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	tests/test_weighted_static_dpv_heuristic.cpp

TEST_WEIGHTED_STATIC_DPV_MIP_SOURCES := \
	src/benchmarks/StaticDpvMipBenchmark.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	tests/test_weighted_static_dpv_mip.cpp

TEST_WEIGHTED_GREEDY_DPV2_SOURCES := \
	src/heuristics/CumulativePropagationGraph.cpp \
	src/heuristics/GreedyMetrics.cpp \
	src/heuristics/GreedyHeuristic.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	tests/test_weighted_greedy_dpv2.cpp

TEST_WEIGHTED_GREEDY_DPV3_SOURCES := \
	src/heuristics/CumulativePropagationGraph.cpp \
	src/heuristics/GreedyMetrics.cpp \
	src/heuristics/GreedyHeuristic.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	tests/test_weighted_greedy_dpv3.cpp

TEST_WEIGHTED_DPV_HEURISTIC_OOS_SOURCES := \
	src/analysis/GraphDiagnostics.cpp \
	src/benchmarks/StaticDpvBenchmark.cpp \
	src/benchmarks/StaticDpvMipBenchmark.cpp \
	src/core/FirebreakSolution.cpp \
	src/core/Graph.cpp \
	src/core/Scenario.cpp \
	src/core/Instance.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/eval/BurnedAreaEvaluator.cpp \
	src/eval/FppRecourseEvaluator.cpp \
	src/experiments/StaticDpvOutOfSampleRunner.cpp \
	src/experiments/GreedyOutOfSampleRunner.cpp \
	src/heuristics/CumulativePropagationGraph.cpp \
	src/heuristics/GreedyMetrics.cpp \
	src/heuristics/GreedyHeuristic.cpp \
	src/io/Cell2FireReader.cpp \
	src/io/ExperimentResultWriter.cpp \
	src/io/PathUtils.cpp \
	src/io/ScenarioFileUtils.cpp \
	src/io/ScenarioSplitUtils.cpp \
	src/io/SolutionIO.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/OptimizationInstanceBuilder.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	src/risk/RiskMeasure.cpp \
	src/solver/FppWeightedLossUtils.cpp \
	tests/test_weighted_dpv_heuristic_oos.cpp

TEST_WEIGHTED_DPV_HEURISTIC_REPORTING_SOURCES := \
	src/io/ExperimentResultWriter.cpp \
	src/io/PathUtils.cpp \
	tests/test_weighted_dpv_heuristic_reporting.cpp

TEST_FPP_STRUCTURE_SOURCES := \
	src/core/LandscapeWeightMap.cpp \
	src/io/PathUtils.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	src/solver/CplexEnvironment.cpp \
	src/solver/FppWeightedLossUtils.cpp \
	src/solver/FppSaaCplexModel.cpp \
	tests/test_fpp_saa_model_structure.cpp

TEST_WEIGHTED_FPP_SAA_MODEL_SOURCES := \
	src/core/LandscapeWeightMap.cpp \
	src/cuts/DominatorCuts.cpp \
	src/cuts/NodeSeparatorMinCut.cpp \
	src/cuts/SeparatorCutSeparator.cpp \
	src/cuts/SeparatorContextCallback.cpp \
	src/eval/FppRecourseEvaluator.cpp \
	src/graph/DinicMaxFlow.cpp \
	src/io/PathUtils.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	src/solver/CplexEnvironment.cpp \
	src/solver/FppWeightedLossUtils.cpp \
	src/solver/FppSaaCplexModel.cpp \
	src/solver/FppCutReachabilityCplexModel.cpp \
	tests/test_weighted_fpp_saa_model.cpp

TEST_WEIGHTED_STANDARD_LLBI_SOURCES := \
	src/benders/FppLiftedLowerBound.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/io/PathUtils.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	src/solver/FppWeightedLossUtils.cpp \
	tests/test_weighted_standard_llbi.cpp

TEST_WEIGHTED_STANDARD_LLBI_VALIDITY_SOURCES := \
	$(filter-out tests/test_weighted_standard_llbi.cpp,$(TEST_WEIGHTED_STANDARD_LLBI_SOURCES)) \
	tests/test_weighted_standard_llbi_validity.cpp

TEST_WEIGHTED_COVERAGE_LLBI_SOURCES := \
	src/benders/FppLiftedLowerBound.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/io/PathUtils.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	src/solver/FppWeightedLossUtils.cpp \
	tests/test_weighted_coverage_llbi.cpp

TEST_WEIGHTED_COVERAGE_LLBI_VALIDITY_SOURCES := \
	$(filter-out tests/test_weighted_coverage_llbi.cpp,$(TEST_WEIGHTED_COVERAGE_LLBI_SOURCES)) \
	tests/test_weighted_coverage_llbi_validity.cpp

TEST_WEIGHTED_PROJECTED_COVERAGE_LLBI_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/FppLiftedLowerBound.cpp \
	src/benders/FppProjectedLlbi.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/io/PathUtils.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	src/solver/FppWeightedLossUtils.cpp \
	tests/test_weighted_projected_coverage_llbi.cpp

TEST_WEIGHTED_PROJECTED_COVERAGE_LLBI_VALIDITY_SOURCES := \
	$(filter-out tests/test_weighted_projected_coverage_llbi.cpp,$(TEST_WEIGHTED_PROJECTED_COVERAGE_LLBI_SOURCES)) \
	tests/test_weighted_projected_coverage_llbi_validity.cpp

TEST_WEIGHTED_PROJECTED_PATH_LLBI_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/FppLiftedLowerBound.cpp \
	src/benders/FppProjectedLlbi.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/io/PathUtils.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	src/solver/FppWeightedLossUtils.cpp \
	tests/test_weighted_projected_path_llbi.cpp

TEST_WEIGHTED_PROJECTED_PATH_LLBI_VALIDITY_SOURCES := \
	$(filter-out tests/test_weighted_projected_path_llbi.cpp,$(TEST_WEIGHTED_PROJECTED_PATH_LLBI_SOURCES)) \
	tests/test_weighted_projected_path_llbi_validity.cpp

TEST_WEIGHTED_PROJECTED_PATH_LLBI_SEPARATION_SOURCES := \
	$(filter-out tests/test_weighted_projected_path_llbi.cpp,$(TEST_WEIGHTED_PROJECTED_PATH_LLBI_SOURCES)) \
	tests/test_weighted_projected_path_llbi_separation.cpp

TEST_WEIGHTED_FPP_COMBINATORIAL_CUT_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/FppCombinatorialBenders.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	tests/test_weighted_fpp_combinatorial_cut.cpp

TEST_WEIGHTED_FPP_COMBINATORIAL_CUT_VALIDITY_SOURCES := \
	$(filter-out tests/test_weighted_fpp_combinatorial_cut.cpp,$(TEST_WEIGHTED_FPP_COMBINATORIAL_CUT_SOURCES)) \
	tests/test_weighted_fpp_combinatorial_cut_validity.cpp

TEST_WEIGHTED_FPP_COMBINATORIAL_CALLBACK_SOURCES := \
	$(filter-out tests/test_weighted_fpp_combinatorial_cut.cpp,$(TEST_WEIGHTED_FPP_COMBINATORIAL_CUT_SOURCES)) \
	tests/test_weighted_fpp_combinatorial_callback.cpp

TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_SOURCES := \
	$(filter-out tests/test_weighted_fpp_combinatorial_cut.cpp,$(TEST_WEIGHTED_FPP_COMBINATORIAL_CUT_SOURCES)) \
	src/core/LandscapeWeightMap.cpp \
	src/io/PathUtils.cpp \
	tests/test_weighted_fpp_combinatorial_lifting.cpp

TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_VALIDITY_SOURCES := \
	$(filter-out tests/test_weighted_fpp_combinatorial_lifting.cpp,$(TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_SOURCES)) \
	tests/test_weighted_fpp_combinatorial_lifting_validity.cpp

TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_COUNTEREXAMPLES_SOURCES := \
	$(filter-out tests/test_weighted_fpp_combinatorial_lifting.cpp,$(TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_SOURCES)) \
	tests/test_weighted_fpp_combinatorial_lifting_counterexamples.cpp

TEST_WEIGHTED_FPP_COMBINATORIAL_INITIAL_CUTS_SOURCES := \
	$(filter-out tests/test_weighted_fpp_combinatorial_lifting.cpp,$(TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_SOURCES)) \
	tests/test_weighted_fpp_combinatorial_initial_cuts.cpp

TEST_WEIGHTED_FPP_COMBINATORIAL_FRACTIONAL_VALIDITY_SOURCES := \
	$(filter-out tests/test_weighted_fpp_combinatorial_lifting.cpp,$(TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_SOURCES)) \
	tests/test_weighted_fpp_combinatorial_fractional_validity.cpp

TEST_WEIGHTED_FPP_COMBINATORIAL_SCENARIO_ORDERING_SOURCES := \
	$(filter-out tests/test_weighted_fpp_combinatorial_cut.cpp,$(TEST_WEIGHTED_FPP_COMBINATORIAL_CUT_SOURCES)) \
	tests/test_weighted_fpp_combinatorial_scenario_ordering.cpp

TEST_WEIGHTED_FPP_COMBINATORIAL_SAMPLING_SOURCES := \
	$(filter-out tests/test_weighted_fpp_combinatorial_cut.cpp,$(TEST_WEIGHTED_FPP_COMBINATORIAL_CUT_SOURCES)) \
	tests/test_weighted_fpp_combinatorial_sampling.cpp

TEST_WEIGHTED_FPP_COMBINATORIAL_SAMPLING_EXACTNESS_SOURCES := \
	$(filter-out tests/test_weighted_fpp_combinatorial_cut.cpp,$(TEST_WEIGHTED_FPP_COMBINATORIAL_CUT_SOURCES)) \
	tests/test_weighted_fpp_combinatorial_sampling_exactness.cpp

TEST_WEIGHTED_FPP_COMBINATORIAL_CPLEX_SOURCES = \
	$(filter-out tests/test_weighted_fpp_branch_benders_cplex.cpp,$(TEST_WEIGHTED_FPP_BRANCH_BENDERS_CPLEX_SOURCES)) \
	tests/test_weighted_fpp_combinatorial_cplex.cpp

TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_CPLEX_SOURCES = \
	$(filter-out tests/test_weighted_fpp_combinatorial_cplex.cpp,$(TEST_WEIGHTED_FPP_COMBINATORIAL_CPLEX_SOURCES)) \
	tests/test_weighted_fpp_combinatorial_lifting_cplex.cpp

TEST_WEIGHTED_FPP_COMBINATORIAL_SAMPLING_CPLEX_SOURCES = \
	$(filter-out tests/test_weighted_fpp_combinatorial_cplex.cpp,$(TEST_WEIGHTED_FPP_COMBINATORIAL_CPLEX_SOURCES)) \
	tests/test_weighted_fpp_combinatorial_sampling_cplex.cpp

TEST_WEIGHTED_PATH_LLBI_SOURCES := \
	src/benders/FppLiftedLowerBound.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/io/PathUtils.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	src/solver/FppWeightedLossUtils.cpp \
	tests/test_weighted_path_llbi.cpp

TEST_WEIGHTED_PATH_LLBI_VALIDITY_SOURCES := \
	$(filter-out tests/test_weighted_path_llbi.cpp,$(TEST_WEIGHTED_PATH_LLBI_SOURCES)) \
	tests/test_weighted_path_llbi_validity.cpp

TEST_DPV_STRUCTURE_SOURCES := \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	src/solver/CplexEnvironment.cpp \
	src/solver/DpvSaaCplexModel.cpp \
	tests/test_dpv_saa_model_structure.cpp

TEST_GRAPH_DIAGNOSTICS_SOURCES := \
	src/analysis/GraphDiagnostics.cpp \
	src/core/Graph.cpp \
	src/core/Scenario.cpp \
	tests/test_graph_diagnostics.cpp

TEST_CELL2FIRE_READER_SOURCES := \
	src/core/Graph.cpp \
	src/core/Scenario.cpp \
	src/core/Instance.cpp \
	src/io/Cell2FireReader.cpp \
	src/io/ScenarioFileUtils.cpp \
	tests/test_cell2fire_reader_new_instances.cpp

TEST_SOLUTION_IO_SOURCES := \
	src/io/ExperimentResultWriter.cpp \
	src/io/SolutionIO.cpp \
	src/io/PathUtils.cpp \
	tests/test_solution_io.cpp

TEST_SCENARIO_SPLIT_SOURCES := \
	src/io/ScenarioFileUtils.cpp \
	src/io/ScenarioSplitUtils.cpp \
	src/io/PathUtils.cpp \
	tests/test_scenario_split_utils.cpp

TEST_STATIC_DPV_SOURCES := \
	src/benchmarks/StaticDpvBenchmark.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	tests/test_static_dpv_benchmark.cpp

TEST_STATIC_DPV_MIP_SOURCES := \
	src/benchmarks/StaticDpvBenchmark.cpp \
	src/benchmarks/StaticDpvMipBenchmark.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	tests/test_static_dpv_mip_benchmark.cpp

TEST_CUMULATIVE_GRAPH_SOURCES := \
	src/heuristics/CumulativePropagationGraph.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	tests/test_cumulative_propagation_graph.cpp

TEST_GREEDY_SOURCES := \
	src/heuristics/CumulativePropagationGraph.cpp \
	src/heuristics/GreedyMetrics.cpp \
	src/heuristics/GreedyHeuristic.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/WeightedDpvScoring.cpp \
	tests/test_greedy_heuristics.cpp

TEST_WARM_START_SOURCES := \
	src/io/PathUtils.cpp \
	src/io/SolutionIO.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/solver/WarmStart.cpp \
	tests/test_warm_start.cpp

TEST_FPP_COMBINATORIAL_BENDERS_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/FppCombinatorialBenders.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	tests/test_fpp_combinatorial_benders.cpp

TEST_BATCH_CONFIG_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/FppCombinatorialBenders.cpp \
	src/benders/FppProjectedLlbi.cpp \
	src/experiments/BatchExperimentConfig.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	tests/test_batch_experiment_config.cpp

TEST_AGGREGATOR_SOURCES := \
	src/analysis/ExperimentAggregator.cpp \
	src/io/PathUtils.cpp \
	tests/test_experiment_aggregator.cpp

TEST_MANIFEST_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/FppCombinatorialBenders.cpp \
	src/benders/FppProjectedLlbi.cpp \
	src/experiments/BatchExperimentConfig.cpp \
	src/experiments/ExperimentManifest.cpp \
	src/io/PathUtils.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	tests/test_experiment_manifest.cpp

TEST_REPORTER_SOURCES := \
	src/analysis/BatchSummaryReporter.cpp \
	src/io/PathUtils.cpp \
	tests/test_batch_summary_reporter.cpp

TEST_RUNTIME_PROFILER_SOURCES := \
	src/analysis/RuntimeProfiler.cpp \
	src/io/PathUtils.cpp \
	tests/test_runtime_profiler.cpp

TEST_SHARED_SPLITS_SOURCES := \
	src/experiments/SharedSplitUtils.cpp \
	src/io/ScenarioSplitUtils.cpp \
	src/io/PathUtils.cpp \
	tests/test_shared_splits.cpp

TEST_BENDERS_CUT_SOURCES := \
	src/benders/BendersCut.cpp \
	tests/test_benders_cut.cpp

TEST_BURN_FREQUENCY_CANDIDATE_SCORER_SOURCES := \
	src/benders/BurnFrequencyCandidateScorer.cpp \
	src/benders/RestrictedCandidateManager.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	tests/test_burn_frequency_candidate_scorer.cpp

TEST_WEIGHTED_BURN_FREQUENCY_CANDIDATE_SCORER_SOURCES := \
	src/benders/BurnFrequencyCandidateScorer.cpp \
	src/benders/RestrictedCandidateManager.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	tests/test_weighted_burn_frequency_candidate_scorer.cpp

TEST_BENDERS_COEFFICIENT_CANDIDATE_SCORER_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/BendersCoefficientCandidateScorer.cpp \
	src/benders/RestrictedCandidateManager.cpp \
	tests/test_benders_coefficient_candidate_scorer.cpp

TEST_WEIGHTED_BENDERS_COEFFICIENT_CANDIDATE_SCORER_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/BendersCoefficientCandidateScorer.cpp \
	src/benders/RestrictedCandidateManager.cpp \
	tests/test_weighted_benders_coefficient_candidate_scorer.cpp

TEST_CVAR_TAIL_SCORE_DIAGNOSTICS_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/BendersCoefficientCandidateScorer.cpp \
	src/benders/CvarTailAwareBendersCandidateScorer.cpp \
	src/benders/CvarTailScoreDiagnostics.cpp \
	src/benders/RestrictedCandidateManager.cpp \
	src/io/ExperimentResultWriter.cpp \
	src/io/PathUtils.cpp \
	tests/test_cvar_tail_score_diagnostics.cpp

TEST_CVAR_TAIL_AWARE_BENDERS_CANDIDATE_SCORER_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/BendersCoefficientCandidateScorer.cpp \
	src/benders/CvarTailAwareBendersCandidateScorer.cpp \
	tests/test_cvar_tail_aware_benders_candidate_scorer.cpp

TEST_WEIGHTED_CVAR_TAIL_AWARE_CANDIDATE_SCORER_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/BendersCoefficientCandidateScorer.cpp \
	src/benders/CvarTailAwareBendersCandidateScorer.cpp \
	tests/test_weighted_cvar_tail_aware_candidate_scorer.cpp

TEST_RESTRICTED_CANDIDATE_CUT_POOL_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/RestrictedCandidateCutPool.cpp \
	tests/test_restricted_candidate_cut_pool.cpp

TEST_RESTRICTED_CANDIDATE_MANAGER_SOURCES := \
	src/benders/RestrictedCandidateManager.cpp \
	tests/test_restricted_candidate_manager.cpp

TEST_RESTRICTED_CANDIDATE_MAINTENANCE_TRACKER_SOURCES := \
	src/benders/RestrictedCandidateManager.cpp \
	src/benders/RestrictedCandidateMaintenanceTracker.cpp \
	tests/test_restricted_candidate_maintenance_tracker.cpp

TEST_WEIGHTED_RESTRICTED_CANDIDATE_MAINTENANCE_SOURCES := \
	src/benders/RestrictedCandidateManager.cpp \
	src/benders/RestrictedCandidateMaintenanceTracker.cpp \
	tests/test_weighted_restricted_candidate_maintenance.cpp

TEST_WEIGHTED_RESTRICTED_CUT_POOL_PRIORITIZATION_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/RestrictedCandidateCutPool.cpp \
	tests/test_weighted_restricted_cut_pool_prioritization.cpp

TEST_WEIGHTED_RESTRICTED_CUT_REACTIVATION_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/RestrictedCandidateCutPool.cpp \
	src/benders/RestrictedCandidateManager.cpp \
	tests/test_weighted_restricted_cut_reactivation.cpp

TEST_WEIGHTED_RESTRICTED_TAIL_DIAGNOSTICS_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/BendersCoefficientCandidateScorer.cpp \
	src/benders/CvarTailScoreDiagnostics.cpp \
	tests/test_weighted_restricted_tail_diagnostics.cpp

TEST_WEIGHTED_RESTRICTED_CANDIDATE_BOUNDS_SOURCES := \
	src/benders/CandidateBoundController.cpp \
	src/benders/RestrictedCandidateManager.cpp \
	tests/test_weighted_restricted_candidate_bounds.cpp

TEST_WEIGHTED_RESTRICTED_BOUND_COUNTEREXAMPLES_SOURCES := \
	src/core/LandscapeWeightMap.cpp \
	src/eval/FppRecourseEvaluator.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	tests/test_weighted_restricted_bound_counterexamples.cpp

TEST_WEIGHTED_GLOBAL_CANDIDATE_DOMINANCE_SOURCES := \
	src/core/LandscapeWeightMap.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	tests/test_weighted_global_candidate_dominance.cpp

TEST_WEIGHTED_CONDITIONAL_ZERO_BENEFIT_FIXING_SOURCES := \
	src/core/LandscapeWeightMap.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	tests/test_weighted_conditional_zero_benefit_fixing.cpp

TEST_CANDIDATE_BOUND_CONTROLLER_SOURCES := \
	src/benders/CandidateBoundController.cpp \
	src/benders/RestrictedCandidateManager.cpp \
	tests/test_candidate_bound_controller.cpp

TEST_FPP_PROJECTED_LLBI_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/FppCombinatorialBenders.cpp \
	src/benders/FppProjectedLlbi.cpp \
	src/experiments/BatchExperimentConfig.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	tests/test_fpp_projected_llbi.cpp

TEST_FPP_PERSISTENT_SCENARIO_SUBPROBLEM_MANAGER_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/FppScenarioSubproblem.cpp \
	src/benders/FppPersistentScenarioSubproblemManager.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/io/PathUtils.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	src/solver/CplexEnvironment.cpp \
	src/solver/FppWeightedLossUtils.cpp \
	tests/test_fpp_persistent_scenario_subproblem_manager.cpp

TEST_FPP_RESTRICTED_CANDIDATE_BRANCH_BENDERS_SMALL_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/BendersCoefficientCandidateScorer.cpp \
	src/benders/BurnFrequencyCandidateScorer.cpp \
	src/benders/CandidateBoundController.cpp \
	src/benders/CvarTailAwareBendersCandidateScorer.cpp \
	src/benders/CvarTailScoreDiagnostics.cpp \
	src/benders/RestrictedCandidateCutPool.cpp \
	src/benders/RestrictedCandidateManager.cpp \
	src/benders/RestrictedCandidateMaintenanceTracker.cpp \
	src/benders/FppCombinatorialBenders.cpp \
	src/benders/FppProjectedLlbi.cpp \
	src/benders/FppLiftedLowerBound.cpp \
	src/benders/FppScenarioSubproblem.cpp \
	src/benders/FppPersistentScenarioSubproblemManager.cpp \
	src/benders/FppBendersMaster.cpp \
	src/benders/FppBendersSolver.cpp \
	src/benders/FppBranchBendersSolver.cpp \
	src/benders/FppRestrictedCandidateBranchBendersSolver.cpp \
	src/cuts/DominatorCuts.cpp \
	src/cuts/NodeSeparatorMinCut.cpp \
	src/cuts/SeparatorCutSeparator.cpp \
	src/cuts/SeparatorContextCallback.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/eval/FppRecourseEvaluator.cpp \
	src/graph/DinicMaxFlow.cpp \
	src/io/PathUtils.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	src/solver/CplexEnvironment.cpp \
	src/solver/FppWeightedLossUtils.cpp \
	src/solver/FppSaaCplexModel.cpp \
	tests/test_fpp_restricted_candidate_branch_benders_small.cpp

TEST_WEIGHTED_FPP_RESTRICTED_BRANCH_BENDERS_CPLEX_SOURCES := \
	$(filter-out tests/test_fpp_restricted_candidate_branch_benders_small.cpp,$(TEST_FPP_RESTRICTED_CANDIDATE_BRANCH_BENDERS_SMALL_SOURCES)) \
	tests/test_weighted_fpp_restricted_branch_benders_cplex.cpp

TEST_WEIGHTED_FPP_BENDERS_CPLEX_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/FppLiftedLowerBound.cpp \
	src/benders/FppScenarioSubproblem.cpp \
	src/benders/FppBendersMaster.cpp \
	src/benders/FppBendersSolver.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/cuts/DominatorCuts.cpp \
	src/cuts/NodeSeparatorMinCut.cpp \
	src/cuts/SeparatorCutSeparator.cpp \
	src/cuts/SeparatorContextCallback.cpp \
	src/eval/FppRecourseEvaluator.cpp \
	src/graph/DinicMaxFlow.cpp \
	src/io/PathUtils.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	src/solver/CplexEnvironment.cpp \
	src/solver/FppWeightedLossUtils.cpp \
	src/solver/FppSaaCplexModel.cpp \
	tests/test_weighted_fpp_benders_cplex.cpp

TEST_WEIGHTED_FPP_BRANCH_BENDERS_CPLEX_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/FppCombinatorialBenders.cpp \
	src/benders/FppProjectedLlbi.cpp \
	src/benders/FppLiftedLowerBound.cpp \
	src/benders/FppScenarioSubproblem.cpp \
	src/benders/FppPersistentScenarioSubproblemManager.cpp \
	src/benders/FppBendersMaster.cpp \
	src/benders/FppBendersSolver.cpp \
	src/benders/FppBranchBendersSolver.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/cuts/DominatorCuts.cpp \
	src/cuts/NodeSeparatorMinCut.cpp \
	src/cuts/SeparatorCutSeparator.cpp \
	src/cuts/SeparatorContextCallback.cpp \
	src/eval/FppRecourseEvaluator.cpp \
	src/graph/DinicMaxFlow.cpp \
	src/io/PathUtils.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/risk/RiskMeasure.cpp \
	src/solver/CplexEnvironment.cpp \
	src/solver/FppWeightedLossUtils.cpp \
	src/solver/FppSaaCplexModel.cpp \
	tests/test_weighted_fpp_branch_benders_cplex.cpp

TEST_WEIGHTED_GLOBAL_CANDIDATE_DOMINANCE_CPLEX_SOURCES := \
	$(filter-out tests/test_weighted_fpp_branch_benders_cplex.cpp,$(TEST_WEIGHTED_FPP_BRANCH_BENDERS_CPLEX_SOURCES)) \
	tests/test_weighted_global_candidate_dominance_cplex.cpp

TEST_WEIGHTED_CONDITIONAL_ZERO_BENEFIT_FIXING_CPLEX_SOURCES := \
	$(filter-out tests/test_weighted_fpp_restricted_branch_benders_cplex.cpp,$(TEST_WEIGHTED_FPP_RESTRICTED_BRANCH_BENDERS_CPLEX_SOURCES)) \
	tests/test_weighted_conditional_zero_benefit_fixing_cplex.cpp

TEST_WEIGHTED_COVERAGE_LLBI_CPLEX_SOURCES := \
	$(filter-out tests/test_weighted_fpp_branch_benders_cplex.cpp,$(TEST_WEIGHTED_FPP_BRANCH_BENDERS_CPLEX_SOURCES)) \
	tests/test_weighted_coverage_llbi_cplex.cpp

TEST_WEIGHTED_PROJECTED_COVERAGE_LLBI_CPLEX_SOURCES := \
	$(filter-out tests/test_weighted_fpp_branch_benders_cplex.cpp,$(TEST_WEIGHTED_FPP_BRANCH_BENDERS_CPLEX_SOURCES)) \
	tests/test_weighted_projected_coverage_llbi_cplex.cpp

TEST_WEIGHTED_PROJECTED_PATH_LLBI_CPLEX_SOURCES := \
	$(filter-out tests/test_weighted_fpp_branch_benders_cplex.cpp,$(TEST_WEIGHTED_FPP_BRANCH_BENDERS_CPLEX_SOURCES)) \
	tests/test_weighted_projected_path_llbi_cplex.cpp

TEST_WEIGHTED_PATH_LLBI_CPLEX_SOURCES := \
	$(filter-out tests/test_weighted_fpp_branch_benders_cplex.cpp,$(TEST_WEIGHTED_FPP_BRANCH_BENDERS_CPLEX_SOURCES)) \
	tests/test_weighted_path_llbi_cplex.cpp

TEST_DPV_SUBPROBLEM_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/DpvLiftedLowerBound.cpp \
	src/benders/DpvScenarioSubproblem.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	src/solver/CplexEnvironment.cpp \
	tests/test_dpv_subproblem_structure.cpp

TEST_DPV_BENDERS_SMALL_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/DpvLiftedLowerBound.cpp \
	src/benders/DpvScenarioSubproblem.cpp \
	src/benders/DpvBendersMaster.cpp \
	src/benders/DpvBendersSolver.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	src/solver/CplexEnvironment.cpp \
	src/solver/DpvSaaCplexModel.cpp \
	tests/test_dpv_benders_small.cpp

TEST_DPV_BRANCH_BENDERS_SMALL_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/DpvLiftedLowerBound.cpp \
	src/benders/DpvScenarioSubproblem.cpp \
	src/benders/DpvPersistentScenarioSubproblemManager.cpp \
	src/benders/DpvBendersMaster.cpp \
	src/benders/DpvBendersSolver.cpp \
	src/benders/DpvBranchBendersSolver.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	src/solver/CplexEnvironment.cpp \
	src/solver/DpvSaaCplexModel.cpp \
	tests/test_dpv_branch_benders_small.cpp

TEST_WEIGHTED_DPV_OPTIMIZATION_SOURCES := \
	src/benders/BendersCut.cpp \
	src/benders/DpvLiftedLowerBound.cpp \
	src/benders/DpvScenarioSubproblem.cpp \
	src/benders/DpvPersistentScenarioSubproblemManager.cpp \
	src/benders/DpvBendersMaster.cpp \
	src/benders/DpvBendersSolver.cpp \
	src/benders/DpvBranchBendersSolver.cpp \
	src/core/LandscapeWeightMap.cpp \
	src/opt/IndexMapper.cpp \
	src/opt/OptimizationInstance.cpp \
	src/opt/DpvIndexBuilder.cpp \
	src/opt/WeightedDpvScoring.cpp \
	src/solver/CplexEnvironment.cpp \
	src/solver/DpvSaaCplexModel.cpp \
	tests/test_weighted_dpv_optimization.cpp

test_obj = $(patsubst %.cpp,$(TEST_OBJ_DIR)/%.o,$(1))

TEST_EXES := \
	$(BUILD_DIR)/test_burned_area_evaluator \
	$(BUILD_DIR)/test_risk_measure \
	$(BUILD_DIR)/test_index_mapper \
	$(BUILD_DIR)/test_landscape_weight_map \
	$(BUILD_DIR)/test_landscape_weight_generator \
	$(BUILD_DIR)/test_weight_canonical_landscape \
	$(BUILD_DIR)/test_weight_map_registry \
	$(BUILD_DIR)/test_weight_paired_instance_mapping \
	$(BUILD_DIR)/test_weight_capability_matrix \
	$(BUILD_DIR)/test_weighted_batch_config \
	$(BUILD_DIR)/test_weighted_subset_attachment \
	$(BUILD_DIR)/test_weighted_capability_cross_check \
	$(BUILD_DIR)/test_dpv_index_builder \
	$(BUILD_DIR)/test_weighted_fpp_dpv \
	$(BUILD_DIR)/test_weighted_fpp_dpv_variants \
	$(BUILD_DIR)/test_weighted_fpp_dpv_cache \
	$(BUILD_DIR)/test_weighted_fpp_dpv_diagnostics \
	$(BUILD_DIR)/test_weighted_static_dpv_heuristic \
	$(BUILD_DIR)/test_weighted_static_dpv_mip \
	$(BUILD_DIR)/test_weighted_greedy_dpv2 \
	$(BUILD_DIR)/test_weighted_greedy_dpv3 \
	$(BUILD_DIR)/test_weighted_dpv_heuristic_oos \
	$(BUILD_DIR)/test_weighted_dpv_heuristic_reporting \
	$(BUILD_DIR)/test_fpp_recourse_evaluator \
	$(BUILD_DIR)/test_weighted_fpp_recourse_evaluator \
	$(BUILD_DIR)/test_fpp_saa_model_structure \
	$(BUILD_DIR)/test_weighted_fpp_saa_model \
	$(BUILD_DIR)/test_weighted_standard_llbi \
	$(BUILD_DIR)/test_weighted_standard_llbi_validity \
	$(BUILD_DIR)/test_weighted_coverage_llbi \
	$(BUILD_DIR)/test_weighted_coverage_llbi_validity \
	$(BUILD_DIR)/test_weighted_projected_coverage_llbi \
	$(BUILD_DIR)/test_weighted_projected_coverage_llbi_validity \
	$(BUILD_DIR)/test_weighted_projected_path_llbi \
	$(BUILD_DIR)/test_weighted_projected_path_llbi_validity \
	$(BUILD_DIR)/test_weighted_projected_path_llbi_separation \
	$(BUILD_DIR)/test_weighted_fpp_combinatorial_cut \
	$(BUILD_DIR)/test_weighted_fpp_combinatorial_cut_validity \
	$(BUILD_DIR)/test_weighted_fpp_combinatorial_callback \
	$(BUILD_DIR)/test_weighted_fpp_combinatorial_lifting \
	$(BUILD_DIR)/test_weighted_fpp_combinatorial_lifting_validity \
	$(BUILD_DIR)/test_weighted_fpp_combinatorial_lifting_counterexamples \
	$(BUILD_DIR)/test_weighted_fpp_combinatorial_initial_cuts \
	$(BUILD_DIR)/test_weighted_fpp_combinatorial_fractional_validity \
	$(BUILD_DIR)/test_weighted_fpp_combinatorial_scenario_ordering \
	$(BUILD_DIR)/test_weighted_fpp_combinatorial_sampling \
	$(BUILD_DIR)/test_weighted_fpp_combinatorial_sampling_exactness \
	$(BUILD_DIR)/test_weighted_path_llbi \
	$(BUILD_DIR)/test_weighted_path_llbi_validity \
	$(BUILD_DIR)/test_weighted_dpv_saa \
	$(BUILD_DIR)/test_weighted_dpv_benders_subproblem \
	$(BUILD_DIR)/test_weighted_dpv_benders_cut \
	$(BUILD_DIR)/test_weighted_dpv_benders_cut_validity \
	$(BUILD_DIR)/test_weighted_dpv_llbi \
	$(BUILD_DIR)/test_weighted_dpv_llbi_validity \
	$(BUILD_DIR)/test_weighted_dpv_reporting \
	$(BUILD_DIR)/test_dpv_saa_model_structure \
	$(BUILD_DIR)/test_graph_diagnostics \
	$(BUILD_DIR)/test_cell2fire_reader_new_instances \
	$(BUILD_DIR)/test_solution_io \
	$(BUILD_DIR)/test_scenario_split_utils \
	$(BUILD_DIR)/test_static_dpv_benchmark \
	$(BUILD_DIR)/test_static_dpv_mip_benchmark \
	$(BUILD_DIR)/test_cumulative_propagation_graph \
	$(BUILD_DIR)/test_greedy_heuristics \
	$(BUILD_DIR)/test_warm_start \
	$(BUILD_DIR)/test_fpp_combinatorial_benders \
	$(BUILD_DIR)/test_batch_experiment_config \
	$(BUILD_DIR)/test_experiment_aggregator \
	$(BUILD_DIR)/test_experiment_manifest \
	$(BUILD_DIR)/test_batch_summary_reporter \
	$(BUILD_DIR)/test_runtime_profiler \
	$(BUILD_DIR)/test_shared_splits \
	$(BUILD_DIR)/test_benders_cut \
	$(BUILD_DIR)/test_burn_frequency_candidate_scorer \
	$(BUILD_DIR)/test_weighted_burn_frequency_candidate_scorer \
	$(BUILD_DIR)/test_benders_coefficient_candidate_scorer \
	$(BUILD_DIR)/test_weighted_benders_coefficient_candidate_scorer \
	$(BUILD_DIR)/test_cvar_tail_aware_benders_candidate_scorer \
	$(BUILD_DIR)/test_weighted_cvar_tail_aware_candidate_scorer \
	$(BUILD_DIR)/test_cvar_tail_score_diagnostics \
	$(BUILD_DIR)/test_restricted_candidate_cut_pool \
	$(BUILD_DIR)/test_restricted_candidate_manager \
	$(BUILD_DIR)/test_restricted_candidate_maintenance_tracker \
	$(BUILD_DIR)/test_weighted_restricted_candidate_maintenance \
	$(BUILD_DIR)/test_weighted_restricted_cut_pool_prioritization \
	$(BUILD_DIR)/test_weighted_restricted_cut_reactivation \
	$(BUILD_DIR)/test_weighted_restricted_tail_diagnostics \
	$(BUILD_DIR)/test_weighted_restricted_candidate_bounds \
	$(BUILD_DIR)/test_weighted_restricted_bound_counterexamples \
	$(BUILD_DIR)/test_weighted_global_candidate_dominance \
	$(BUILD_DIR)/test_weighted_conditional_zero_benefit_fixing \
	$(BUILD_DIR)/test_candidate_bound_controller \
	$(BUILD_DIR)/test_fpp_projected_llbi \
	$(BUILD_DIR)/test_fpp_persistent_scenario_subproblem_manager \
	$(BUILD_DIR)/test_fpp_restricted_candidate_branch_benders_small \
	$(BUILD_DIR)/test_dpv_subproblem_structure \
	$(BUILD_DIR)/test_dpv_benders_small \
	$(BUILD_DIR)/test_dpv_branch_benders_small

.PHONY: help build cplex require-binary test clean \
	phase11-debug phase12-small phase12-exact phase12-alpha02 \
	aggregate-phase11-debug aggregate-phase12-small \
	study-fpp-risk-smoke study-fpp-risk-generate-splits \
	study-fpp-risk-generate-commands study-fpp-risk-dry-run \
	study-fpp-risk-run-exact study-fpp-risk-run-heuristics \
	study-fpp-risk-run-all study-fpp-risk-aggregate

help:
	@echo "Targets:"
	@echo "  make                        Incremental build with CPLEX; same as make cplex"
	@echo "  make build                  Incremental build without CPLEX"
	@echo "  make cplex                  Incremental build with CPLEX using CPLEX_STUDIO_DIR=$(CPLEX_STUDIO_DIR)"
	@echo "  make test                   Incremental test build and run all non-solver tests"
	@echo "  make phase11-debug          Run the Phase 11 debug manifest using the existing binary"
	@echo "  make phase12-small          Run the Phase 12 small calibration manifest using the existing binary"
	@echo "  make phase12-exact          Run the guarded Phase 12 exact calibration script using the existing binary"
	@echo "  make phase12-alpha02        Run the guarded Phase 12 alpha02 methods script using the existing binary"
	@echo "  make aggregate-phase11-debug Aggregate the Phase 11 debug batch CSV using the existing binary"
	@echo "  make aggregate-phase12-small Aggregate the Phase 12 small calibration batch CSV using the existing binary"
	@echo "  make study-fpp-risk-smoke   Smoke-test FPP risk-study scripts without solving models"
	@echo "  make study-fpp-risk-generate-splits Generate shared FPP risk-study splits"
	@echo "  make study-fpp-risk-generate-commands Generate FPP risk-study command manifests"
	@echo "  make study-fpp-risk-dry-run Dry-run FPP risk-study command generation"
	@echo "  make study-fpp-risk-run-exact Run exact FPP risk-study commands with skip-existing"
	@echo "  make study-fpp-risk-run-heuristics Run restricted heuristic study commands with skip-existing"
	@echo "  make study-fpp-risk-run-all Run the full FPP risk-study manifest; requires CONFIRM_LONG_RUN=1"
	@echo "  make study-fpp-risk-aggregate Aggregate FPP risk-study JSON outputs"
	@echo "  make clean                  Remove local build directories"
	@echo
	@echo "For guarded larger runs, pass CONFIRM_LONG_RUN=1, for example:"
	@echo "  CONFIRM_LONG_RUN=1 make phase12-exact"

cplex: $(CPLEX_EXE)
	@mkdir -p $(dir $(BINARY))
	@if ! cmp -s "$(CPLEX_EXE)" "$(BINARY)"; then \
		cp "$(CPLEX_EXE)" "$(BINARY).tmp" && mv -f "$(BINARY).tmp" "$(BINARY)"; \
		echo "Installed CPLEX binary: $(BINARY)"; \
	else \
		echo "CPLEX binary is up to date: $(BINARY)"; \
	fi

build: $(NOCPLEX_EXE)
	@mkdir -p $(dir $(BINARY))
	@if ! cmp -s "$(NOCPLEX_EXE)" "$(BINARY)"; then \
		cp "$(NOCPLEX_EXE)" "$(BINARY).tmp" && mv -f "$(BINARY).tmp" "$(BINARY)"; \
		echo "Installed non-CPLEX binary: $(BINARY)"; \
	else \
		echo "Non-CPLEX binary is up to date: $(BINARY)"; \
	fi

$(CPLEX_EXE): $(CPLEX_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $^ $(CPLEX_LDFLAGS) -o $@

$(NOCPLEX_EXE): $(NOCPLEX_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $^ -o $@

$(CPLEX_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) -c $< -o $@

$(NOCPLEX_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(COMMON_CXXFLAGS) -c $< -o $@

$(TEST_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(COMMON_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/test_burned_area_evaluator: $(call test_obj,$(TEST_BURNED_AREA_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_fpp_recourse_evaluator: $(call test_obj,$(TEST_FPP_RECOURSE_EVALUATOR_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_recourse_evaluator: $(call test_obj,$(TEST_WEIGHTED_FPP_RECOURSE_EVALUATOR_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_risk_measure: $(call test_obj,$(TEST_RISK_MEASURE_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_index_mapper: $(call test_obj,$(TEST_INDEX_MAPPER_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_landscape_weight_map: $(call test_obj,$(TEST_LANDSCAPE_WEIGHT_MAP_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_landscape_weight_generator: $(call test_obj,$(TEST_LANDSCAPE_WEIGHT_GENERATOR_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weight_canonical_landscape: $(call test_obj,$(TEST_WEIGHT_CANONICAL_LANDSCAPE_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weight_map_registry: $(call test_obj,$(TEST_WEIGHT_MAP_REGISTRY_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weight_paired_instance_mapping: $(call test_obj,$(TEST_WEIGHT_PAIRED_INSTANCE_MAPPING_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weight_capability_matrix: $(call test_obj,$(TEST_WEIGHT_CAPABILITY_MATRIX_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_batch_config: $(call test_obj,$(TEST_WEIGHTED_BATCH_CONFIG_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_subset_attachment: $(call test_obj,$(TEST_WEIGHTED_SUBSET_ATTACHMENT_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_capability_cross_check: $(call test_obj,$(TEST_WEIGHTED_CAPABILITY_CROSS_CHECK_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_dpv_index_builder: $(call test_obj,$(TEST_DPV_INDEX_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_dpv: $(call test_obj,$(TEST_WEIGHTED_FPP_DPV_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_dpv_variants: $(call test_obj,$(TEST_WEIGHTED_FPP_DPV_VARIANTS_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_dpv_cache: $(call test_obj,$(TEST_WEIGHTED_FPP_DPV_CACHE_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_dpv_diagnostics: $(call test_obj,$(TEST_WEIGHTED_FPP_DPV_DIAGNOSTICS_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_static_dpv_heuristic: $(call test_obj,$(TEST_WEIGHTED_STATIC_DPV_HEURISTIC_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_static_dpv_mip: $(call test_obj,$(TEST_WEIGHTED_STATIC_DPV_MIP_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_greedy_dpv2: $(call test_obj,$(TEST_WEIGHTED_GREEDY_DPV2_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_greedy_dpv3: $(call test_obj,$(TEST_WEIGHTED_GREEDY_DPV3_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_dpv_heuristic_oos: $(call test_obj,$(TEST_WEIGHTED_DPV_HEURISTIC_OOS_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_dpv_heuristic_reporting: $(call test_obj,$(TEST_WEIGHTED_DPV_HEURISTIC_REPORTING_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_fpp_saa_model_structure: $(call test_obj,$(TEST_FPP_STRUCTURE_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_saa_model: $(call test_obj,$(TEST_WEIGHTED_FPP_SAA_MODEL_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_standard_llbi: $(call test_obj,$(TEST_WEIGHTED_STANDARD_LLBI_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_standard_llbi_validity: $(call test_obj,$(TEST_WEIGHTED_STANDARD_LLBI_VALIDITY_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_coverage_llbi: $(call test_obj,$(TEST_WEIGHTED_COVERAGE_LLBI_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_coverage_llbi_validity: $(call test_obj,$(TEST_WEIGHTED_COVERAGE_LLBI_VALIDITY_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_projected_coverage_llbi: $(call test_obj,$(TEST_WEIGHTED_PROJECTED_COVERAGE_LLBI_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_projected_coverage_llbi_validity: $(call test_obj,$(TEST_WEIGHTED_PROJECTED_COVERAGE_LLBI_VALIDITY_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_projected_path_llbi: $(call test_obj,$(TEST_WEIGHTED_PROJECTED_PATH_LLBI_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_projected_path_llbi_validity: $(call test_obj,$(TEST_WEIGHTED_PROJECTED_PATH_LLBI_VALIDITY_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_projected_path_llbi_separation: $(call test_obj,$(TEST_WEIGHTED_PROJECTED_PATH_LLBI_SEPARATION_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_combinatorial_cut: $(call test_obj,$(TEST_WEIGHTED_FPP_COMBINATORIAL_CUT_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_combinatorial_cut_validity: $(call test_obj,$(TEST_WEIGHTED_FPP_COMBINATORIAL_CUT_VALIDITY_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_combinatorial_callback: $(call test_obj,$(TEST_WEIGHTED_FPP_COMBINATORIAL_CALLBACK_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_combinatorial_lifting: $(call test_obj,$(TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_combinatorial_lifting_validity: $(call test_obj,$(TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_VALIDITY_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_combinatorial_lifting_counterexamples: $(call test_obj,$(TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_COUNTEREXAMPLES_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_combinatorial_initial_cuts: $(call test_obj,$(TEST_WEIGHTED_FPP_COMBINATORIAL_INITIAL_CUTS_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_combinatorial_fractional_validity: $(call test_obj,$(TEST_WEIGHTED_FPP_COMBINATORIAL_FRACTIONAL_VALIDITY_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_combinatorial_scenario_ordering: $(call test_obj,$(TEST_WEIGHTED_FPP_COMBINATORIAL_SCENARIO_ORDERING_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_combinatorial_sampling: $(call test_obj,$(TEST_WEIGHTED_FPP_COMBINATORIAL_SAMPLING_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_combinatorial_sampling_exactness: $(call test_obj,$(TEST_WEIGHTED_FPP_COMBINATORIAL_SAMPLING_EXACTNESS_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_path_llbi: $(call test_obj,$(TEST_WEIGHTED_PATH_LLBI_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_path_llbi_validity: $(call test_obj,$(TEST_WEIGHTED_PATH_LLBI_VALIDITY_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_dpv_saa: $(call test_obj,$(TEST_WEIGHTED_DPV_OPTIMIZATION_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_dpv_benders_subproblem: $(call test_obj,$(TEST_WEIGHTED_DPV_OPTIMIZATION_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_dpv_benders_cut: $(call test_obj,$(TEST_WEIGHTED_DPV_OPTIMIZATION_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_dpv_benders_cut_validity: $(call test_obj,$(TEST_WEIGHTED_DPV_OPTIMIZATION_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_dpv_llbi: $(call test_obj,$(TEST_WEIGHTED_DPV_OPTIMIZATION_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_dpv_llbi_validity: $(call test_obj,$(TEST_WEIGHTED_DPV_OPTIMIZATION_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_dpv_reporting: $(call test_obj,$(TEST_WEIGHTED_DPV_OPTIMIZATION_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_fpp_saa_cplex: $(TEST_WEIGHTED_FPP_SAA_MODEL_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_dpv_benders_cplex: $(TEST_WEIGHTED_DPV_OPTIMIZATION_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_dpv_branch_benders_cplex: $(TEST_WEIGHTED_DPV_OPTIMIZATION_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_fpp_benders_subproblem: $(TEST_WEIGHTED_FPP_BENDERS_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_fpp_benders_cut: $(TEST_WEIGHTED_FPP_BENDERS_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_fpp_benders_cplex: $(TEST_WEIGHTED_FPP_BENDERS_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_fpp_branch_benders_callback: $(TEST_WEIGHTED_FPP_BRANCH_BENDERS_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_fpp_branch_benders_root_cuts: $(TEST_WEIGHTED_FPP_BRANCH_BENDERS_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_fpp_branch_benders_cplex: $(TEST_WEIGHTED_FPP_BRANCH_BENDERS_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_standard_llbi_cplex: $(TEST_WEIGHTED_FPP_BRANCH_BENDERS_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_fpp_restricted_branch_benders_cplex: $(TEST_WEIGHTED_FPP_RESTRICTED_BRANCH_BENDERS_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_restricted_candidate_activation: $(TEST_WEIGHTED_FPP_RESTRICTED_BRANCH_BENDERS_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_restricted_cut_pool: $(TEST_WEIGHTED_FPP_RESTRICTED_BRANCH_BENDERS_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_restricted_candidate_scoring_cplex: $(TEST_WEIGHTED_FPP_RESTRICTED_BRANCH_BENDERS_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_restricted_maintenance_cplex: $(TEST_WEIGHTED_FPP_RESTRICTED_BRANCH_BENDERS_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_restricted_bounds_cplex: $(TEST_WEIGHTED_FPP_RESTRICTED_BRANCH_BENDERS_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_global_candidate_dominance_cplex: $(TEST_WEIGHTED_GLOBAL_CANDIDATE_DOMINANCE_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_conditional_zero_benefit_fixing_cplex: $(TEST_WEIGHTED_CONDITIONAL_ZERO_BENEFIT_FIXING_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_coverage_llbi_cplex: $(TEST_WEIGHTED_COVERAGE_LLBI_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_projected_coverage_llbi_cplex: $(TEST_WEIGHTED_PROJECTED_COVERAGE_LLBI_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_projected_path_llbi_cplex: $(TEST_WEIGHTED_PROJECTED_PATH_LLBI_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_fpp_combinatorial_cplex: $(TEST_WEIGHTED_FPP_COMBINATORIAL_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_fpp_combinatorial_lifting_cplex: $(TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_fpp_combinatorial_sampling_cplex: $(TEST_WEIGHTED_FPP_COMBINATORIAL_SAMPLING_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_weighted_path_llbi_cplex: $(TEST_WEIGHTED_PATH_LLBI_CPLEX_SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_CPPFLAGS) $(CPLEX_CPPFLAGS) $(COMMON_CXXFLAGS) $^ $(CPLEX_LDFLAGS) -o $@

$(BUILD_DIR)/test_dpv_saa_model_structure: $(call test_obj,$(TEST_DPV_STRUCTURE_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_graph_diagnostics: $(call test_obj,$(TEST_GRAPH_DIAGNOSTICS_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_cell2fire_reader_new_instances: $(call test_obj,$(TEST_CELL2FIRE_READER_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_solution_io: $(call test_obj,$(TEST_SOLUTION_IO_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_scenario_split_utils: $(call test_obj,$(TEST_SCENARIO_SPLIT_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_static_dpv_benchmark: $(call test_obj,$(TEST_STATIC_DPV_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_static_dpv_mip_benchmark: $(call test_obj,$(TEST_STATIC_DPV_MIP_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_cumulative_propagation_graph: $(call test_obj,$(TEST_CUMULATIVE_GRAPH_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_greedy_heuristics: $(call test_obj,$(TEST_GREEDY_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_warm_start: $(call test_obj,$(TEST_WARM_START_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_fpp_combinatorial_benders: $(call test_obj,$(TEST_FPP_COMBINATORIAL_BENDERS_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_batch_experiment_config: $(call test_obj,$(TEST_BATCH_CONFIG_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_experiment_aggregator: $(call test_obj,$(TEST_AGGREGATOR_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_experiment_manifest: $(call test_obj,$(TEST_MANIFEST_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_batch_summary_reporter: $(call test_obj,$(TEST_REPORTER_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_runtime_profiler: $(call test_obj,$(TEST_RUNTIME_PROFILER_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_shared_splits: $(call test_obj,$(TEST_SHARED_SPLITS_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_benders_cut: $(call test_obj,$(TEST_BENDERS_CUT_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_burn_frequency_candidate_scorer: $(call test_obj,$(TEST_BURN_FREQUENCY_CANDIDATE_SCORER_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_burn_frequency_candidate_scorer: $(call test_obj,$(TEST_WEIGHTED_BURN_FREQUENCY_CANDIDATE_SCORER_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_benders_coefficient_candidate_scorer: $(call test_obj,$(TEST_BENDERS_COEFFICIENT_CANDIDATE_SCORER_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_benders_coefficient_candidate_scorer: $(call test_obj,$(TEST_WEIGHTED_BENDERS_COEFFICIENT_CANDIDATE_SCORER_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_cvar_tail_aware_benders_candidate_scorer: $(call test_obj,$(TEST_CVAR_TAIL_AWARE_BENDERS_CANDIDATE_SCORER_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_cvar_tail_aware_candidate_scorer: $(call test_obj,$(TEST_WEIGHTED_CVAR_TAIL_AWARE_CANDIDATE_SCORER_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_cvar_tail_score_diagnostics: $(call test_obj,$(TEST_CVAR_TAIL_SCORE_DIAGNOSTICS_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_restricted_candidate_cut_pool: $(call test_obj,$(TEST_RESTRICTED_CANDIDATE_CUT_POOL_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_restricted_candidate_manager: $(call test_obj,$(TEST_RESTRICTED_CANDIDATE_MANAGER_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_restricted_candidate_maintenance_tracker: $(call test_obj,$(TEST_RESTRICTED_CANDIDATE_MAINTENANCE_TRACKER_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_restricted_candidate_maintenance: $(call test_obj,$(TEST_WEIGHTED_RESTRICTED_CANDIDATE_MAINTENANCE_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_restricted_cut_pool_prioritization: $(call test_obj,$(TEST_WEIGHTED_RESTRICTED_CUT_POOL_PRIORITIZATION_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_restricted_cut_reactivation: $(call test_obj,$(TEST_WEIGHTED_RESTRICTED_CUT_REACTIVATION_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_restricted_tail_diagnostics: $(call test_obj,$(TEST_WEIGHTED_RESTRICTED_TAIL_DIAGNOSTICS_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_restricted_candidate_bounds: $(call test_obj,$(TEST_WEIGHTED_RESTRICTED_CANDIDATE_BOUNDS_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_restricted_bound_counterexamples: $(call test_obj,$(TEST_WEIGHTED_RESTRICTED_BOUND_COUNTEREXAMPLES_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_global_candidate_dominance: $(call test_obj,$(TEST_WEIGHTED_GLOBAL_CANDIDATE_DOMINANCE_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_weighted_conditional_zero_benefit_fixing: $(call test_obj,$(TEST_WEIGHTED_CONDITIONAL_ZERO_BENEFIT_FIXING_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_candidate_bound_controller: $(call test_obj,$(TEST_CANDIDATE_BOUND_CONTROLLER_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_fpp_projected_llbi: $(call test_obj,$(TEST_FPP_PROJECTED_LLBI_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_fpp_persistent_scenario_subproblem_manager: $(call test_obj,$(TEST_FPP_PERSISTENT_SCENARIO_SUBPROBLEM_MANAGER_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_fpp_restricted_candidate_branch_benders_small: $(call test_obj,$(TEST_FPP_RESTRICTED_CANDIDATE_BRANCH_BENDERS_SMALL_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_dpv_subproblem_structure: $(call test_obj,$(TEST_DPV_SUBPROBLEM_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_dpv_benders_small: $(call test_obj,$(TEST_DPV_BENDERS_SMALL_SOURCES))
	$(CXX) $^ -o $@

$(BUILD_DIR)/test_dpv_branch_benders_small: $(call test_obj,$(TEST_DPV_BRANCH_BENDERS_SMALL_SOURCES))
	$(CXX) $^ -o $@

require-binary:
	@test -x "$(BINARY)" || { \
		echo "Missing $(BINARY). Run 'make build' or 'make cplex' first."; \
		exit 2; \
	}

test: $(TEST_EXES)
	./build_gpp/test_index_mapper
	./build_gpp/test_landscape_weight_map
	./build_gpp/test_landscape_weight_generator
	./build_gpp/test_weight_canonical_landscape
	./build_gpp/test_weight_map_registry
	./build_gpp/test_weight_paired_instance_mapping
	./build_gpp/test_weight_capability_matrix
	./build_gpp/test_weighted_batch_config
	./build_gpp/test_weighted_subset_attachment
	./build_gpp/test_weighted_capability_cross_check
	./build_gpp/test_dpv_index_builder
	./build_gpp/test_weighted_fpp_dpv
	./build_gpp/test_weighted_fpp_dpv_variants
	./build_gpp/test_weighted_fpp_dpv_cache
	./build_gpp/test_weighted_fpp_dpv_diagnostics
	./build_gpp/test_weighted_static_dpv_heuristic
	./build_gpp/test_weighted_static_dpv_mip
	./build_gpp/test_weighted_greedy_dpv2
	./build_gpp/test_weighted_greedy_dpv3
	./build_gpp/test_weighted_dpv_heuristic_oos
	./build_gpp/test_weighted_dpv_heuristic_reporting
	./build_gpp/test_burned_area_evaluator
	./build_gpp/test_fpp_recourse_evaluator
	./build_gpp/test_weighted_fpp_recourse_evaluator
	./build_gpp/test_fpp_saa_model_structure
	./build_gpp/test_weighted_fpp_saa_model
	./build_gpp/test_weighted_standard_llbi
	./build_gpp/test_weighted_standard_llbi_validity
	./build_gpp/test_weighted_coverage_llbi
	./build_gpp/test_weighted_coverage_llbi_validity
	./build_gpp/test_weighted_projected_coverage_llbi
	./build_gpp/test_weighted_projected_coverage_llbi_validity
	./build_gpp/test_weighted_projected_path_llbi
	./build_gpp/test_weighted_projected_path_llbi_validity
	./build_gpp/test_weighted_projected_path_llbi_separation
	./build_gpp/test_weighted_fpp_combinatorial_cut
	./build_gpp/test_weighted_fpp_combinatorial_cut_validity
	./build_gpp/test_weighted_fpp_combinatorial_callback
	./build_gpp/test_weighted_fpp_combinatorial_lifting
	./build_gpp/test_weighted_fpp_combinatorial_lifting_validity
	./build_gpp/test_weighted_fpp_combinatorial_lifting_counterexamples
	./build_gpp/test_weighted_fpp_combinatorial_initial_cuts
	./build_gpp/test_weighted_fpp_combinatorial_fractional_validity
	./build_gpp/test_weighted_fpp_combinatorial_scenario_ordering
	./build_gpp/test_weighted_fpp_combinatorial_sampling
	./build_gpp/test_weighted_fpp_combinatorial_sampling_exactness
	./build_gpp/test_weighted_path_llbi
	./build_gpp/test_weighted_path_llbi_validity
	./build_gpp/test_graph_diagnostics
	./build_gpp/test_cell2fire_reader_new_instances
	./build_gpp/test_solution_io
	./build_gpp/test_scenario_split_utils
	./build_gpp/test_dpv_saa_model_structure
	./build_gpp/test_static_dpv_benchmark
	./build_gpp/test_static_dpv_mip_benchmark
	./build_gpp/test_cumulative_propagation_graph
	./build_gpp/test_greedy_heuristics
	./build_gpp/test_warm_start
	./build_gpp/test_fpp_combinatorial_benders
	./build_gpp/test_batch_experiment_config
	./build_gpp/test_experiment_aggregator
	./build_gpp/test_experiment_manifest
	./build_gpp/test_batch_summary_reporter
	./build_gpp/test_runtime_profiler
	./build_gpp/test_shared_splits
	./build_gpp/test_benders_cut
	./build_gpp/test_burn_frequency_candidate_scorer
	./build_gpp/test_weighted_burn_frequency_candidate_scorer
	./build_gpp/test_benders_coefficient_candidate_scorer
	./build_gpp/test_weighted_benders_coefficient_candidate_scorer
	./build_gpp/test_cvar_tail_aware_benders_candidate_scorer
	./build_gpp/test_weighted_cvar_tail_aware_candidate_scorer
	./build_gpp/test_cvar_tail_score_diagnostics
	./build_gpp/test_restricted_candidate_cut_pool
	./build_gpp/test_restricted_candidate_manager
	./build_gpp/test_restricted_candidate_maintenance_tracker
	./build_gpp/test_weighted_restricted_candidate_maintenance
	./build_gpp/test_weighted_restricted_cut_pool_prioritization
	./build_gpp/test_weighted_restricted_cut_reactivation
	./build_gpp/test_weighted_restricted_tail_diagnostics
	./build_gpp/test_weighted_restricted_candidate_bounds
	./build_gpp/test_weighted_restricted_bound_counterexamples
	./build_gpp/test_weighted_global_candidate_dominance
	./build_gpp/test_weighted_conditional_zero_benefit_fixing
	./build_gpp/test_candidate_bound_controller
	./build_gpp/test_fpp_projected_llbi
	./build_gpp/test_fpp_persistent_scenario_subproblem_manager
	./build_gpp/test_fpp_restricted_candidate_branch_benders_small
	./build_gpp/test_dpv_subproblem_structure
	./build_gpp/test_dpv_benders_small
	./build_gpp/test_dpv_branch_benders_small

phase11-debug: require-binary
	./scripts/run_phase11_debug.sh

phase12-small: require-binary
	./scripts/run_phase12_small_calibration.sh

phase12-exact: require-binary
	./scripts/run_phase12_exact_calibration.sh

phase12-alpha02: require-binary
	./scripts/run_phase12_alpha02_methods_calibration.sh

aggregate-phase11-debug: require-binary
	./scripts/aggregate_batch.sh \
		results/batch/phase11_sub20_debug/batch_results.csv \
		results/batch/phase11_sub20_debug/summary

aggregate-phase12-small: require-binary
	./scripts/aggregate_batch.sh \
		results/batch/phase12_sub20_small_test_calibration/batch_results.csv \
		results/batch/phase12_sub20_small_test_calibration/summary

study-fpp-risk-smoke:
	python3 scripts/computational_study/smoke_fpp_risk_study_scripts.py

study-fpp-risk-generate-splits:
	python3 scripts/computational_study/generate_fpp_risk_splits.py \
		--output-dir $(STUDY_FPP_RISK_OUTPUT_DIR) $(STUDY_FPP_RISK_ARGS)

study-fpp-risk-generate-commands:
	python3 scripts/computational_study/generate_fpp_risk_command_manifest.py \
		--output-dir $(STUDY_FPP_RISK_OUTPUT_DIR) $(STUDY_FPP_RISK_ARGS)

study-fpp-risk-dry-run:
	python3 scripts/computational_study/generate_fpp_risk_command_manifest.py \
		--output-dir $(STUDY_FPP_RISK_OUTPUT_DIR) $(STUDY_FPP_RISK_ARGS) --dry-run

study-fpp-risk-run-exact: require-binary
	python3 scripts/computational_study/run_command_manifest.py \
		--manifest $(STUDY_FPP_RISK_OUTPUT_DIR)/commands/full_command_manifest.csv \
		--method-filter fpp-saa,fpp-bb \
		--parallel $(STUDY_FPP_RISK_PARALLEL) \
		--skip-existing

study-fpp-risk-run-heuristics: require-binary
	python3 scripts/computational_study/run_command_manifest.py \
		--manifest $(STUDY_FPP_RISK_OUTPUT_DIR)/commands/full_command_manifest.csv \
		--method-filter restricted \
		--parallel $(STUDY_FPP_RISK_PARALLEL) \
		--skip-existing

study-fpp-risk-run-all: require-binary
	@if [[ "$(CONFIRM_LONG_RUN)" != "1" ]]; then \
		echo "Set CONFIRM_LONG_RUN=1 to run the full FPP risk-study manifest."; \
		exit 2; \
	fi
	python3 scripts/computational_study/run_command_manifest.py \
		--manifest $(STUDY_FPP_RISK_OUTPUT_DIR)/commands/full_command_manifest.csv \
		--parallel $(STUDY_FPP_RISK_PARALLEL) \
		--skip-existing

study-fpp-risk-aggregate:
	python3 scripts/computational_study/aggregate_fpp_risk_results.py \
		--output-dir $(STUDY_FPP_RISK_OUTPUT_DIR)

clean:
	rm -rf build build_gpp

-include $(CPLEX_OBJECTS:.o=.d)
-include $(NOCPLEX_OBJECTS:.o=.d)
-include $(patsubst %.cpp,$(TEST_OBJ_DIR)/%.d,$(sort \
	$(TEST_BURNED_AREA_SOURCES) \
	$(TEST_FPP_RECOURSE_EVALUATOR_SOURCES) \
	$(TEST_WEIGHTED_FPP_RECOURSE_EVALUATOR_SOURCES) \
	$(TEST_RISK_MEASURE_SOURCES) \
	$(TEST_INDEX_MAPPER_SOURCES) \
	$(TEST_LANDSCAPE_WEIGHT_MAP_SOURCES) \
	$(TEST_LANDSCAPE_WEIGHT_GENERATOR_SOURCES) \
	$(TEST_DPV_INDEX_SOURCES) \
	$(TEST_WEIGHTED_FPP_DPV_SOURCES) \
	$(TEST_WEIGHTED_FPP_DPV_VARIANTS_SOURCES) \
	$(TEST_WEIGHTED_FPP_DPV_CACHE_SOURCES) \
	$(TEST_WEIGHTED_FPP_DPV_DIAGNOSTICS_SOURCES) \
	$(TEST_FPP_STRUCTURE_SOURCES) \
	$(TEST_DPV_STRUCTURE_SOURCES) \
	$(TEST_GRAPH_DIAGNOSTICS_SOURCES) \
	$(TEST_CELL2FIRE_READER_SOURCES) \
	$(TEST_SOLUTION_IO_SOURCES) \
	$(TEST_SCENARIO_SPLIT_SOURCES) \
	$(TEST_STATIC_DPV_SOURCES) \
	$(TEST_STATIC_DPV_MIP_SOURCES) \
	$(TEST_CUMULATIVE_GRAPH_SOURCES) \
	$(TEST_GREEDY_SOURCES) \
	$(TEST_WARM_START_SOURCES) \
	$(TEST_FPP_COMBINATORIAL_BENDERS_SOURCES) \
	$(TEST_BATCH_CONFIG_SOURCES) \
	$(TEST_AGGREGATOR_SOURCES) \
	$(TEST_MANIFEST_SOURCES) \
	$(TEST_REPORTER_SOURCES) \
	$(TEST_RUNTIME_PROFILER_SOURCES) \
	$(TEST_SHARED_SPLITS_SOURCES) \
	$(TEST_BENDERS_CUT_SOURCES) \
	$(TEST_BURN_FREQUENCY_CANDIDATE_SCORER_SOURCES) \
	$(TEST_WEIGHTED_BURN_FREQUENCY_CANDIDATE_SCORER_SOURCES) \
	$(TEST_BENDERS_COEFFICIENT_CANDIDATE_SCORER_SOURCES) \
	$(TEST_WEIGHTED_BENDERS_COEFFICIENT_CANDIDATE_SCORER_SOURCES) \
	$(TEST_CVAR_TAIL_AWARE_BENDERS_CANDIDATE_SCORER_SOURCES) \
	$(TEST_WEIGHTED_CVAR_TAIL_AWARE_CANDIDATE_SCORER_SOURCES) \
	$(TEST_CVAR_TAIL_SCORE_DIAGNOSTICS_SOURCES) \
	$(TEST_RESTRICTED_CANDIDATE_CUT_POOL_SOURCES) \
	$(TEST_RESTRICTED_CANDIDATE_MANAGER_SOURCES) \
	$(TEST_RESTRICTED_CANDIDATE_MAINTENANCE_TRACKER_SOURCES) \
	$(TEST_WEIGHTED_RESTRICTED_CANDIDATE_MAINTENANCE_SOURCES) \
	$(TEST_WEIGHTED_RESTRICTED_CUT_POOL_PRIORITIZATION_SOURCES) \
	$(TEST_WEIGHTED_RESTRICTED_CUT_REACTIVATION_SOURCES) \
	$(TEST_WEIGHTED_RESTRICTED_TAIL_DIAGNOSTICS_SOURCES) \
	$(TEST_WEIGHTED_RESTRICTED_CANDIDATE_BOUNDS_SOURCES) \
	$(TEST_WEIGHTED_RESTRICTED_BOUND_COUNTEREXAMPLES_SOURCES) \
	$(TEST_WEIGHTED_PROJECTED_COVERAGE_LLBI_SOURCES) \
	$(TEST_WEIGHTED_PROJECTED_COVERAGE_LLBI_VALIDITY_SOURCES) \
	$(TEST_WEIGHTED_PROJECTED_COVERAGE_LLBI_CPLEX_SOURCES) \
	$(TEST_WEIGHTED_PROJECTED_PATH_LLBI_SOURCES) \
	$(TEST_WEIGHTED_PROJECTED_PATH_LLBI_VALIDITY_SOURCES) \
	$(TEST_WEIGHTED_PROJECTED_PATH_LLBI_SEPARATION_SOURCES) \
	$(TEST_WEIGHTED_PROJECTED_PATH_LLBI_CPLEX_SOURCES) \
	$(TEST_WEIGHTED_FPP_COMBINATORIAL_CUT_SOURCES) \
	$(TEST_WEIGHTED_FPP_COMBINATORIAL_CUT_VALIDITY_SOURCES) \
	$(TEST_WEIGHTED_FPP_COMBINATORIAL_CALLBACK_SOURCES) \
	$(TEST_WEIGHTED_FPP_COMBINATORIAL_CPLEX_SOURCES) \
	$(TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_SOURCES) \
	$(TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_VALIDITY_SOURCES) \
	$(TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_COUNTEREXAMPLES_SOURCES) \
	$(TEST_WEIGHTED_FPP_COMBINATORIAL_INITIAL_CUTS_SOURCES) \
	$(TEST_WEIGHTED_FPP_COMBINATORIAL_FRACTIONAL_VALIDITY_SOURCES) \
	$(TEST_WEIGHTED_FPP_COMBINATORIAL_LIFTING_CPLEX_SOURCES) \
	$(TEST_WEIGHTED_GLOBAL_CANDIDATE_DOMINANCE_SOURCES) \
	$(TEST_WEIGHTED_CONDITIONAL_ZERO_BENEFIT_FIXING_SOURCES) \
	$(TEST_WEIGHTED_GLOBAL_CANDIDATE_DOMINANCE_CPLEX_SOURCES) \
	$(TEST_WEIGHTED_CONDITIONAL_ZERO_BENEFIT_FIXING_CPLEX_SOURCES) \
	$(TEST_CANDIDATE_BOUND_CONTROLLER_SOURCES) \
	$(TEST_FPP_PROJECTED_LLBI_SOURCES) \
	$(TEST_FPP_PERSISTENT_SCENARIO_SUBPROBLEM_MANAGER_SOURCES) \
	$(TEST_FPP_RESTRICTED_CANDIDATE_BRANCH_BENDERS_SMALL_SOURCES) \
	$(TEST_DPV_SUBPROBLEM_SOURCES) \
	$(TEST_DPV_BENDERS_SMALL_SOURCES) \
	$(TEST_DPV_BRANCH_BENDERS_SMALL_SOURCES)))
