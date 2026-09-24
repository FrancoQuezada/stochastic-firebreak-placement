## alpha = 0.01

| Obj | Método | Obj prom | Gap % | Runtime s | OOS exp | OOS tail | Vars | Cons | Proj cuts | lazyCuts |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Expected | FPP-SAA-fpp_base | 105.45 | 0.000 | 2.60 | 108.35 | 252.83 | 36360 | 14263 |  | 0.0 |
| Expected | FPP-SAA-fpp_cut | 105.46 | 0.052 | 8.11 | 108.49 | 252.49 | 28644 | 42447 |  | 0.0 |
| Expected | FPP-Branch-Benders | 105.45 | 0.072 | 7.54 | 108.35 | 252.83 | 460 | 1054 |  | 1052.6 |
| Expected | FPP-Branch-Benders-RootCuts | 105.45 | 0.081 | 7.83 | 108.35 | 252.83 | 460 | 1020 |  | 974.0 |
| Expected | FPP-Branch-Benders-LLBI | 105.45 | 0.067 | 6.27 | 108.35 | 252.83 | 460 | 366 |  | 264.8 |
| Expected | FPP-Branch-Benders-LLBI-RootCuts | 105.45 | 0.074 | 4.87 | 108.35 | 252.83 | 460 | 333 |  | 190.4 |
| Expected | FPP-Branch-Benders-CoverageLLBI | 105.45 | 0.057 | 25.81 | 108.35 | 252.83 | 14502 | 14186 |  | 43.0 |
| Expected | FPP-Branch-Benders-CoverageLLBI-RootCuts | 105.45 | 0.033 | 28.98 | 108.35 | 252.83 | 14502 | 14182 |  | 23.8 |
| Expected | FPP-Branch-Benders-PathLLBI | 105.46 | 0.036 | 51.12 | 109.48 | 254.20 | 14602 | 14851 |  | 0.0 |
| Expected | FPP-Branch-Benders-PathLLBI-RootCuts | 105.46 | 0.036 | 37.52 | 109.48 | 254.20 | 14602 | 14851 |  | 0.0 |
| Expected | FPP-Branch-Benders-CoverageLLBI-PathLLBI | 105.52 | 0.291 | 172.23 | 109.00 | 254.98 | 28644 | 28993 |  | 0.0 |
| Expected | FPP-Branch-Benders-CoverageLLBI-PathLLBI-RootCuts | 107.09 | 1.442 | 190.59 | 110.27 | 256.12 | 28644 | 28993 |  | 0.0 |
| Expected | FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI | 105.45 | 0.040 | 163.15 | 108.81 | 254.25 | 28644 | 29093 |  | 0.0 |
| Expected | FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI-RootCuts | 105.45 | 0.040 | 119.91 | 108.81 | 254.25 | 28644 | 29093 |  | 0.0 |
| Expected | FPP-Branch-Benders-Combinatorial | 105.45 | 0.034 | 10.30 | 108.35 | 252.83 | 460 | 984 |  | 118.2 |
| Expected | FPP-Branch-Benders-ProjectedCoverageLLBI-poly | 105.45 | 0.067 | 2.96 | 108.35 | 252.83 | 460 | 366 | 100.0 | 264.8 |
| Expected | FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts | 105.45 | 0.074 | 2.97 | 108.35 | 252.83 | 460 | 333 | 100.0 | 190.4 |
| Expected | FPP-Branch-Benders-ProjectedPathLLBI-poly | 105.45 | 0.043 | 3.20 | 108.35 | 252.83 | 460 | 372 | 100.0 | 271.4 |
| Expected | FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts | 105.45 | 0.071 | 2.99 | 108.81 | 254.25 | 460 | 285 | 100.0 | 144.0 |
| Expected | FPP-Branch-Benders-ProjectedCoverageLLBI-exp | 105.45 | 0.073 | 3.44 | 108.35 | 252.83 | 460 | 325 | 255.0 | 69.4 |
| Expected | FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts | 105.45 | 0.068 | 3.40 | 108.35 | 252.83 | 460 | 328 | 255.0 | 51.6 |
| Expected | FPP-Branch-Benders-ProjectedPathLLBI-exp | 105.45 | 0.053 | 1.80 | 108.35 | 252.83 | 460 | 275 | 259.4 | 14.6 |
| Expected | FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts | 105.45 | 0.053 | 2.29 | 108.35 | 252.83 | 460 | 275 | 259.4 | 14.6 |
| CVaR | FPP-SAA-fpp_base | 224.08 | 1.111 | 193.92 | 113.49 | 256.62 | 36461 | 14363 |  | 0.0 |
| CVaR | FPP-SAA-fpp_cut | 224.98 | 6.441 | 300.04 | 112.95 | 254.35 | 28745 | 42547 |  | 0.0 |
| CVaR | FPP-Branch-Benders | 223.88 | 0.095 | 6.49 | 113.53 | 256.90 | 561 | 1070 |  | 969.2 |
| CVaR | FPP-Branch-Benders-RootCuts | 223.88 | 0.096 | 8.37 | 113.53 | 256.90 | 561 | 1081 |  | 951.0 |
| CVaR | FPP-Branch-Benders-LLBI | 223.88 | 0.092 | 10.00 | 113.53 | 256.90 | 561 | 339 |  | 138.2 |
| CVaR | FPP-Branch-Benders-LLBI-RootCuts | 223.88 | 0.094 | 9.82 | 113.53 | 256.90 | 561 | 352 |  | 119.8 |
| CVaR | FPP-Branch-Benders-CoverageLLBI | 225.60 | 5.585 | 282.66 | 113.86 | 257.20 | 14603 | 14253 |  | 10.6 |
| CVaR | FPP-Branch-Benders-CoverageLLBI-RootCuts | 225.38 | 5.999 | 293.71 | 111.98 | 253.69 | 14603 | 14258 |  | 6.0 |
| CVaR | FPP-Branch-Benders-PathLLBI | 225.74 | 5.359 | 273.81 | 111.43 | 253.74 | 14703 | 14951 |  | 0.0 |
| CVaR | FPP-Branch-Benders-PathLLBI-RootCuts | 225.74 | 5.307 | 278.99 | 111.43 | 253.74 | 14703 | 14951 |  | 0.0 |
| CVaR | FPP-Branch-Benders-CoverageLLBI-PathLLBI | 281.04 | 10.030 | 302.60 | 126.23 | 285.56 | 28745 | 29093 |  | 0.0 |
| CVaR | FPP-Branch-Benders-CoverageLLBI-PathLLBI-RootCuts | 226.94 | 9.650 | 301.81 | 111.85 | 255.54 | 28745 | 29093 |  | 0.0 |
| CVaR | FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI | 226.44 | 8.006 | 301.61 | 113.73 | 257.54 | 28745 | 29193 |  | 0.0 |
| CVaR | FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI-RootCuts | 245.48 | 13.967 | 301.65 | 118.08 | 267.24 | 28745 | 29193 |  | 0.0 |
| CVaR | FPP-Branch-Benders-Combinatorial | 223.88 | 0.090 | 132.60 | 113.53 | 256.90 | 561 | 2978 |  | 85.0 |
| CVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-poly | 223.88 | 0.092 | 6.48 | 113.53 | 256.90 | 561 | 339 | 100.0 | 138.2 |
| CVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts | 223.88 | 0.094 | 6.29 | 113.53 | 256.90 | 561 | 352 | 100.0 | 119.8 |
| CVaR | FPP-Branch-Benders-ProjectedPathLLBI-poly | 223.88 | 0.094 | 5.67 | 113.53 | 256.90 | 561 | 314 | 100.0 | 113.0 |
| CVaR | FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts | 223.88 | 0.097 | 5.70 | 113.53 | 256.90 | 561 | 309 | 100.0 | 81.4 |
| CVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-exp | 223.88 | 0.094 | 8.25 | 113.53 | 256.90 | 561 | 325 | 150.4 | 73.2 |
| CVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts | 223.88 | 0.088 | 6.81 | 113.53 | 256.90 | 561 | 327 | 150.4 | 58.6 |
| CVaR | FPP-Branch-Benders-ProjectedPathLLBI-exp | 223.88 | 0.085 | 5.13 | 113.53 | 256.90 | 561 | 306 | 160.4 | 45.0 |
| CVaR | FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts | 223.88 | 0.085 | 5.39 | 113.53 | 256.90 | 561 | 310 | 160.4 | 47.0 |
| MeanCVaR | FPP-SAA-fpp_base | 166.89 | 0.094 | 140.15 | 111.97 | 253.83 | 36461 | 14363 |  | 0.0 |
| MeanCVaR | FPP-SAA-fpp_cut | 166.94 | 1.826 | 278.98 | 112.16 | 254.91 | 28745 | 42547 |  | 0.0 |
| MeanCVaR | FPP-Branch-Benders | 166.89 | 0.092 | 8.26 | 111.97 | 253.83 | 561 | 1146 |  | 1045.4 |
| MeanCVaR | FPP-Branch-Benders-RootCuts | 166.89 | 0.097 | 8.53 | 111.97 | 253.83 | 561 | 1177 |  | 1040.8 |
| MeanCVaR | FPP-Branch-Benders-LLBI | 166.89 | 0.097 | 7.96 | 111.97 | 253.83 | 561 | 484 |  | 283.2 |
| MeanCVaR | FPP-Branch-Benders-LLBI-RootCuts | 166.89 | 0.094 | 7.92 | 111.97 | 253.83 | 561 | 434 |  | 196.2 |
| MeanCVaR | FPP-Branch-Benders-CoverageLLBI | 166.97 | 1.802 | 256.27 | 111.85 | 254.18 | 14603 | 14270 |  | 27.4 |
| MeanCVaR | FPP-Branch-Benders-CoverageLLBI-RootCuts | 167.33 | 3.049 | 264.30 | 113.17 | 256.10 | 14603 | 14280 |  | 17.0 |
| MeanCVaR | FPP-Branch-Benders-PathLLBI | 166.89 | 0.718 | 207.64 | 111.97 | 253.83 | 14703 | 14951 |  | 0.0 |
| MeanCVaR | FPP-Branch-Benders-PathLLBI-RootCuts | 166.89 | 0.718 | 216.98 | 111.97 | 253.83 | 14703 | 14951 |  | 0.0 |
| MeanCVaR | FPP-Branch-Benders-CoverageLLBI-PathLLBI | 193.88 | 3.772 | 301.77 | 122.81 | 277.84 | 28745 | 29093 |  | 0.0 |
| MeanCVaR | FPP-Branch-Benders-CoverageLLBI-PathLLBI-RootCuts | 167.94 | 5.595 | 301.34 | 112.74 | 255.47 | 28745 | 29093 |  | 0.0 |
| MeanCVaR | FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI | 178.10 | 9.983 | 301.67 | 116.54 | 263.25 | 28745 | 29193 |  | 0.0 |
| MeanCVaR | FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI-RootCuts | 168.44 | 5.633 | 302.07 | 113.38 | 255.63 | 28745 | 29193 |  | 0.0 |
| MeanCVaR | FPP-Branch-Benders-Combinatorial | 166.89 | 0.073 | 60.88 | 111.97 | 253.83 | 561 | 2581 |  | 110.6 |
| MeanCVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-poly | 166.89 | 0.097 | 5.26 | 111.97 | 253.83 | 561 | 484 | 100.0 | 283.2 |
| MeanCVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts | 166.89 | 0.094 | 6.94 | 111.97 | 253.83 | 561 | 434 | 100.0 | 196.2 |
| MeanCVaR | FPP-Branch-Benders-ProjectedPathLLBI-poly | 166.89 | 0.094 | 6.30 | 111.97 | 253.83 | 561 | 414 | 100.0 | 213.4 |
| MeanCVaR | FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts | 166.89 | 0.094 | 5.38 | 111.97 | 253.83 | 561 | 411 | 100.0 | 178.6 |
| MeanCVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-exp | 166.89 | 0.096 | 5.84 | 111.97 | 253.83 | 561 | 376 | 164.4 | 110.6 |
| MeanCVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts | 166.89 | 0.090 | 6.78 | 111.97 | 253.83 | 561 | 386 | 164.4 | 98.6 |
| MeanCVaR | FPP-Branch-Benders-ProjectedPathLLBI-exp | 166.89 | 0.092 | 4.34 | 111.97 | 253.83 | 561 | 356 | 181.2 | 74.0 |
| MeanCVaR | FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts | 166.89 | 0.092 | 3.80 | 111.97 | 253.83 | 561 | 356 | 181.2 | 74.0 |

## alpha = 0.02

| Obj | Método | Obj prom | Gap % | Runtime s | OOS exp | OOS tail | Vars | Cons | Proj cuts | lazyCuts |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Expected | FPP-SAA-fpp_base | 82.76 | 0.038 | 20.30 | 92.81 | 214.30 | 36360 | 14263 |  | 0.0 |
| Expected | FPP-SAA-fpp_cut | 82.76 | 0.011 | 47.17 | 92.81 | 214.30 | 28644 | 42447 |  | 0.0 |
| Expected | FPP-Branch-Benders | 82.78 | 0.431 | 189.25 | 92.73 | 213.53 | 460 | 4398 |  | 4397.2 |
| Expected | FPP-Branch-Benders-RootCuts | 82.76 | 0.355 | 135.00 | 92.79 | 213.96 | 460 | 3687 |  | 3621.2 |
| Expected | FPP-Branch-Benders-LLBI | 82.76 | 0.100 | 110.80 | 92.81 | 214.30 | 460 | 2871 |  | 2769.8 |
| Expected | FPP-Branch-Benders-LLBI-RootCuts | 82.76 | 0.100 | 75.01 | 92.81 | 214.30 | 460 | 2117 |  | 1950.0 |
| Expected | FPP-Branch-Benders-CoverageLLBI | 82.76 | 0.074 | 72.25 | 92.81 | 214.30 | 14502 | 14204 |  | 61.6 |
| Expected | FPP-Branch-Benders-CoverageLLBI-RootCuts | 82.76 | 0.041 | 86.06 | 92.81 | 214.30 | 14502 | 14204 |  | 36.2 |
| Expected | FPP-Branch-Benders-PathLLBI | 82.76 | 0.021 | 72.55 | 92.81 | 214.30 | 14602 | 14851 |  | 0.0 |
| Expected | FPP-Branch-Benders-PathLLBI-RootCuts | 82.76 | 0.021 | 74.37 | 92.81 | 214.30 | 14602 | 14851 |  | 0.0 |
| Expected | FPP-Branch-Benders-CoverageLLBI-PathLLBI | 86.97 | 5.159 | 283.15 | 95.67 | 220.05 | 28644 | 28993 |  | 0.0 |
| Expected | FPP-Branch-Benders-CoverageLLBI-PathLLBI-RootCuts | 87.74 | 5.774 | 266.54 | 95.98 | 222.37 | 28644 | 28993 |  | 0.0 |
| Expected | FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI | 98.43 | 15.394 | 302.34 | 105.36 | 242.76 | 28644 | 29093 |  | 0.0 |
| Expected | FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI-RootCuts | 93.34 | 10.262 | 299.29 | 101.19 | 233.36 | 28644 | 29093 |  | 0.0 |
| Expected | FPP-Branch-Benders-Combinatorial | 82.76 | 0.021 | 50.70 | 92.81 | 214.30 | 460 | 15861 |  | 176.6 |
| Expected | FPP-Branch-Benders-ProjectedCoverageLLBI-poly | 82.76 | 0.100 | 141.31 | 92.81 | 214.30 | 460 | 2871 | 100.0 | 2769.8 |
| Expected | FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts | 82.76 | 0.100 | 56.46 | 92.81 | 214.30 | 460 | 2117 | 100.0 | 1950.0 |
| Expected | FPP-Branch-Benders-ProjectedPathLLBI-poly | 82.76 | 0.100 | 61.26 | 92.81 | 214.30 | 460 | 2727 | 100.0 | 2625.8 |
| Expected | FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts | 82.76 | 0.099 | 41.73 | 92.81 | 214.30 | 460 | 2112 | 100.0 | 1947.8 |
| Expected | FPP-Branch-Benders-ProjectedCoverageLLBI-exp | 82.76 | 0.096 | 8.29 | 92.81 | 214.30 | 460 | 766 | 521.8 | 243.6 |
| Expected | FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts | 82.76 | 0.098 | 7.85 | 92.81 | 214.30 | 460 | 730 | 521.8 | 172.0 |
| Expected | FPP-Branch-Benders-ProjectedPathLLBI-exp | 82.76 | 0.088 | 5.08 | 92.81 | 214.30 | 460 | 648 | 518.6 | 128.6 |
| Expected | FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts | 82.76 | 0.088 | 4.43 | 92.81 | 214.30 | 460 | 658 | 518.6 | 127.6 |
| CVaR | FPP-SAA-fpp_base | 173.30 | 15.486 | 300.02 | 95.70 | 217.33 | 36461 | 14363 |  | 0.0 |
| CVaR | FPP-SAA-fpp_cut | 169.88 | 15.779 | 300.04 | 95.25 | 216.23 | 28745 | 42547 |  | 0.0 |
| CVaR | FPP-Branch-Benders | 167.96 | 9.512 | 300.46 | 94.57 | 217.15 | 561 | 2931 |  | 2830.0 |
| CVaR | FPP-Branch-Benders-RootCuts | 169.30 | 10.175 | 300.47 | 96.60 | 218.27 | 561 | 2578 |  | 2428.0 |
| CVaR | FPP-Branch-Benders-LLBI | 165.86 | 7.512 | 300.79 | 95.29 | 216.54 | 561 | 1283 |  | 1081.6 |
| CVaR | FPP-Branch-Benders-LLBI-RootCuts | 165.62 | 5.845 | 300.54 | 94.85 | 217.91 | 561 | 1066 |  | 814.6 |
| CVaR | FPP-Branch-Benders-CoverageLLBI | 168.96 | 15.969 | 301.15 | 94.65 | 215.28 | 14603 | 14258 |  | 15.2 |
| CVaR | FPP-Branch-Benders-CoverageLLBI-RootCuts | 170.10 | 16.707 | 301.23 | 96.80 | 219.20 | 14603 | 14271 |  | 14.4 |
| CVaR | FPP-Branch-Benders-PathLLBI | 171.32 | 17.104 | 301.09 | 95.26 | 214.74 | 14703 | 14951 |  | 0.0 |
| CVaR | FPP-Branch-Benders-PathLLBI-RootCuts | 171.54 | 17.222 | 301.45 | 94.73 | 215.03 | 14703 | 14951 |  | 0.0 |
| CVaR | FPP-Branch-Benders-CoverageLLBI-PathLLBI | 232.54 | 17.413 | 301.90 | 112.22 | 254.36 | 28745 | 29093 |  | 0.0 |
| CVaR | FPP-Branch-Benders-CoverageLLBI-PathLLBI-RootCuts | 213.94 | 31.120 | 301.85 | 106.88 | 242.29 | 28745 | 29093 |  | 0.0 |
| CVaR | FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI | 190.10 | 23.594 | 302.24 | 100.67 | 229.46 | 28745 | 29193 |  | 0.0 |
| CVaR | FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI-RootCuts | 170.08 | 16.663 | 301.61 | 96.60 | 218.74 | 28745 | 29193 |  | 0.0 |
| CVaR | FPP-Branch-Benders-Combinatorial | 174.82 | 16.579 | 315.79 | 95.88 | 217.52 | 561 | 16590 |  | 121.0 |
| CVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-poly | 165.68 | 5.728 | 300.49 | 95.16 | 216.31 | 561 | 1351 | 100.0 | 1149.8 |
| CVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts | 165.48 | 4.493 | 286.69 | 94.76 | 216.17 | 561 | 1120 | 100.0 | 869.2 |
| CVaR | FPP-Branch-Benders-ProjectedPathLLBI-poly | 165.84 | 5.349 | 293.53 | 95.71 | 217.60 | 561 | 1332 | 100.0 | 1131.0 |
| CVaR | FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts | 166.06 | 4.803 | 286.36 | 95.14 | 215.80 | 561 | 1228 | 100.0 | 978.6 |
| CVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-exp | 165.82 | 3.733 | 275.95 | 95.05 | 216.74 | 561 | 927 | 382.6 | 443.2 |
| CVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts | 165.48 | 3.111 | 264.08 | 94.92 | 216.36 | 561 | 812 | 382.6 | 298.8 |
| CVaR | FPP-Branch-Benders-ProjectedPathLLBI-exp | 165.74 | 2.634 | 241.15 | 95.22 | 216.14 | 561 | 787 | 393.4 | 292.8 |
| CVaR | FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts | 165.56 | 2.539 | 249.00 | 95.99 | 216.14 | 561 | 825 | 393.4 | 320.8 |
| MeanCVaR | FPP-SAA-fpp_base | 129.38 | 8.229 | 300.02 | 95.73 | 216.49 | 36461 | 14363 |  | 0.0 |
| MeanCVaR | FPP-SAA-fpp_cut | 127.71 | 9.190 | 300.05 | 93.80 | 213.97 | 28745 | 42547 |  | 0.0 |
| MeanCVaR | FPP-Branch-Benders | 129.60 | 8.155 | 300.45 | 95.00 | 216.55 | 561 | 5253 |  | 5151.8 |
| MeanCVaR | FPP-Branch-Benders-RootCuts | 127.89 | 5.155 | 300.34 | 94.23 | 217.00 | 561 | 4185 |  | 4023.8 |
| MeanCVaR | FPP-Branch-Benders-LLBI | 127.07 | 5.151 | 300.38 | 93.34 | 214.73 | 561 | 2791 |  | 2590.2 |
| MeanCVaR | FPP-Branch-Benders-LLBI-RootCuts | 126.90 | 3.937 | 300.45 | 93.93 | 216.17 | 561 | 2777 |  | 2511.6 |
| MeanCVaR | FPP-Branch-Benders-CoverageLLBI | 127.69 | 8.312 | 300.55 | 94.35 | 215.35 | 14603 | 14289 |  | 45.8 |
| MeanCVaR | FPP-Branch-Benders-CoverageLLBI-RootCuts | 127.70 | 8.173 | 300.64 | 93.44 | 212.02 | 14603 | 14302 |  | 29.6 |
| MeanCVaR | FPP-Branch-Benders-PathLLBI | 127.76 | 8.334 | 300.79 | 93.60 | 213.81 | 14703 | 14951 |  | 0.0 |
| MeanCVaR | FPP-Branch-Benders-PathLLBI-RootCuts | 127.76 | 8.290 | 300.92 | 93.60 | 213.81 | 14703 | 14951 |  | 0.0 |
| MeanCVaR | FPP-Branch-Benders-CoverageLLBI-PathLLBI | 129.38 | 10.895 | 300.94 | 93.34 | 212.41 | 28745 | 29093 |  | 0.0 |
| MeanCVaR | FPP-Branch-Benders-CoverageLLBI-PathLLBI-RootCuts | 129.38 | 11.022 | 301.27 | 93.34 | 212.41 | 28745 | 29093 |  | 0.0 |
| MeanCVaR | FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI | 129.01 | 10.628 | 301.17 | 94.53 | 215.88 | 28745 | 29193 |  | 0.0 |
| MeanCVaR | FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI-RootCuts | 129.01 | 10.535 | 301.30 | 94.53 | 215.88 | 28745 | 29193 |  | 0.0 |
| MeanCVaR | FPP-Branch-Benders-Combinatorial | 130.22 | 9.389 | 303.65 | 93.98 | 216.21 | 561 | 21839 |  | 212.6 |
| MeanCVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-poly | 127.12 | 5.123 | 300.53 | 93.39 | 214.70 | 561 | 2825 | 100.0 | 2624.2 |
| MeanCVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts | 126.90 | 3.643 | 300.58 | 93.93 | 216.17 | 561 | 2789 | 100.0 | 2523.4 |
| MeanCVaR | FPP-Branch-Benders-ProjectedPathLLBI-poly | 126.98 | 4.032 | 300.55 | 93.42 | 214.34 | 561 | 2533 | 100.0 | 2332.4 |
| MeanCVaR | FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts | 127.07 | 3.326 | 300.54 | 94.62 | 216.26 | 561 | 2523 | 100.0 | 2260.2 |
| MeanCVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-exp | 126.80 | 1.437 | 254.94 | 94.01 | 215.79 | 561 | 1352 | 422.0 | 829.2 |
| MeanCVaR | FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts | 126.80 | 1.415 | 270.71 | 94.01 | 215.79 | 561 | 1431 | 422.0 | 872.6 |
| MeanCVaR | FPP-Branch-Benders-ProjectedPathLLBI-exp | 126.80 | 0.857 | 211.27 | 94.01 | 215.79 | 561 | 1207 | 422.6 | 683.2 |
| MeanCVaR | FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts | 126.86 | 0.730 | 188.21 | 94.84 | 215.61 | 561 | 1155 | 422.6 | 623.8 |
