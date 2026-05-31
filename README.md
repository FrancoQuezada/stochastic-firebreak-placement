# Stochastic Firebreak Placement - C++ Implementation

This repository publishes the active C++ implementation of the stochastic
firebreak placement project.

The legacy Python prototypes, Cell2Fire sample data, generated results, logs,
build outputs, cache files, and development notes are intentionally excluded
from version control.

## Repository Layout

- `cpp_cplex_firebreak/src/`: C++ implementation.
- `cpp_cplex_firebreak/include/`: public project headers.
- `cpp_cplex_firebreak/tests/`: C++ unit and regression tests.
- `cpp_cplex_firebreak/config/`: experiment and method configuration files.
- `cpp_cplex_firebreak/scripts/`: reproducibility and batch-run helper scripts.
- `cpp_cplex_firebreak/README.md`: detailed build, CPLEX, CLI, and experiment
  documentation.

## Build

From the C++ project directory:

```bash
cd cpp_cplex_firebreak
mkdir -p build
cd build
cmake ..
cmake --build .
```

The Makefile workflow is also available:

```bash
cd cpp_cplex_firebreak
make help
make build
make test
```

Some solver paths require IBM ILOG CPLEX. See
`cpp_cplex_firebreak/README.md` for the CPLEX configuration options.

## Data And Outputs

Large local inputs and generated outputs are not tracked. In particular,
`sample_test/`, `cpp_cplex_firebreak/results/`, `cpp_cplex_firebreak/logs/`,
and build directories are ignored by Git.
