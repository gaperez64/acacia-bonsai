# Thermal annotation

Clean-leg median package throttle rate: 24.222 events/s (5 completed legs with >=80% row-progress coverage); anomaly threshold: 30.277 events/s.
Coverage is (last sampled row - first sampled row + 1)/1,524; it measures the span observed, not the fraction of rows sampled individually.

| Run | Samples | Coverage | Median package °C | Package throttle events/s | Minimum available MiB | Thermal label |
|---|---:|---:|---:|---:|---:|---|
| arm-1 | 1070 | 99.9% | 96.0 | 36.064 | 4614 | anomalous |
| arm-2 | 673 | 99.8% | 95.0 | 17.580 | 4442 | clean |
| arm-3 | 1074 | 99.9% | 96.0 | 24.222 | 3143 | clean |
| arm-4 | 851 | 100.0% | 95.0 | 21.962 | 4530 | clean |
| arm-5 | 73 | 12.0% | 97.0 | 63.171 | 3968 | thermal unknown |
| arm-6 | 677 | 99.6% | 97.0 | 55.992 | 4389 | anomalous |
| arm-7 | 379 | 79.2% | 96.0 | 48.014 | 5563 | thermal unknown |

B epoch noise: per original instance, absolute difference between the two 60 s PAR-2 contributions (solve time or 120 s). Median=0.004653 s; 95th percentile=0.130004 s. A non-unique fastest credit qualifies when removing that arm increases the subset contribution by more than that instance's noise.
Top-3 4-arm subsets: real:small:backward,real:small:forward,unreal:formula:spot-guarded-sparse,real:gr1:oxidd; real:small:forward,unreal:formula:spot-guarded-sparse,unreal:automaton:forward,real:gr1:oxidd; real:small:backward,unreal:formula:spot-guarded-sparse,unreal:automaton:forward,real:gr1:oxidd
Top-3 5-arm subsets: real:small:backward,real:small:forward,unreal:formula:spot-guarded-sparse,unreal:automaton:forward,real:gr1:oxidd; real:small:backward,real:small:forward,unreal:formula:spot-guarded-sparse,real:gr1:oxidd,unreal:gr1:oxidd; real:small:forward,unreal:formula:spot-guarded-sparse,unreal:automaton:forward,real:gr1:oxidd,unreal:gr1:oxidd
arm-1: selection-relevant set=741, estimated 4.98 h from prior capped runtimes (upper bound 12.35 h); thermal label=anomalous.
arm-5: selection-relevant set=200, estimated 2.20 h from prior capped runtimes (upper bound 3.33 h); thermal label=thermal unknown.
arm-6: selection-relevant set=167, estimated 2.24 h from prior capped runtimes (upper bound 2.78 h); thermal label=anomalous.
arm-7: selection-relevant set=0, estimated 0.00 h from prior capped runtimes (upper bound 0.00 h); thermal label=thermal unknown.
obf-60: near-cap rerun assessment pending complete CSV.
obf-17: near-cap rerun assessment pending complete CSV.
