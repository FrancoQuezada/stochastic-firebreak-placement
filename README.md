# Optimisation procedures using fire simulator C2F 
The codes found here correlates to specific versions of the simulator and can be outdated. 

## Contents
### Proccesing
Proccesing folder contains the codes to procces the results obtained from C2F simulations.
  - read_data: read forest and simulations data to feed optimization procedures.
  - main: call functions.
  - optimization: stochastic optimization model using gurobi.
### Sample test
Canadian forest and simulation results for testing purposes

## C++ CPLEX implementation

The active C++ implementation is in `cpp_cplex_firebreak/`. It supports the
FPP and DPV exact/heuristic experiment pipeline, including:

- direct `FPP-SAA`, `FPP-SAA-CVaR`, and `FPP-SAA-MeanCVaR`;
- explicit-loop `FPP-Benders` and `DPV-Benders`;
- callback `FPP-Branch-Benders` and `DPV-Branch-Benders`;
- restricted-candidate `FPP-Restricted-Branch-Benders`;
- new combinatorial FPP Branch-and-Benders variants:
  `FPP-Branch-Benders-Combinatorial` and
  `FPP-Restricted-Branch-Benders-Combinatorial`, with `CVaR` and `MeanCVaR`
  labels.

See `cpp_cplex_firebreak/README.md` for build instructions, CLI flags,
manifest labels, and reporting fields.
