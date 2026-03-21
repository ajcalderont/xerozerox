# XeroZerox

XeroZerox is a CUDA memory analysis and optimization prototype aimed at systems where CPU and GPU can benefit from the same physical memory pool, especially NVIDIA Jetson-class platforms.

The project follows a two-stage workflow:

1. `analysis`
   Build and run an instrumented version of the target program. XeroZerox intercepts memory-related calls such as `malloc`, `free`, `cudaMalloc`, `cudaFree`, `cudaMallocHost`, `cudaFreeHost`, and `cudaMemcpy`, then records allocation sites, transfer patterns, and a memory usage trace.
2. `optimization`
   Rebuild the program using the generated profile. XeroZerox attempts to replace profiled host/device allocations with regions taken from a shared preallocated pool, while keeping conservative fallbacks when the real execution diverges from the profile.

## Layout

- `src/xzx_analyzer.cpp`, `src/xzx_analyzer.h`
  Analyzer instrumentation.
- `src/xzx_optimizer.cpp`, `src/xzx_optimizer.h`
  Profile-driven optimizer and CUDA wrappers.
- `src/allocator_status.cpp`, `src/allocator_status.h`
  Allocator model used by the analyzer report.
- `tests/`
  CUDA test programs, including the integration-style `test.cu` and focused smoke tests.
- `Makefile`
  Main Linux-centric build entry point.
- `src/Makefile`
  Compatibility wrapper that forwards to the root `Makefile`.
- `src/info.cfg`
  Allocator configuration file used by analysis reports.

## Build And Run

The main workflow is driven from the repository root. Builds and generated artifacts go to `out/<test>/`.

Examples:

```bash
make TEST=test_cyclic baseline
make TEST=test_cyclic analysis
make TEST=test_cyclic optimization
make TEST=test_cyclic run-optimization
make TEST=test_cyclic benchmark
make benchmark-all
```

- `baseline`
  Builds and runs the uninstrumented executable and stores timing/output.
- `analysis`
  Builds and runs the analyzer-enabled executable and stores timing/output plus profile/report files.
- `optimization`
  Builds the optimizer-enabled executable.
- `run-optimization`
  Runs the optimizer-enabled executable and stores timing/output.
- `benchmark`
  Runs baseline, analysis, and optimization in sequence for one test.
- `benchmark-all`
  Runs the configured small benchmark matrix.

## Outputs

Running the analyzer produces:

- `out/<test>/xzx_data.csv`
  Time-ordered memory trace used for visualization or manual inspection.
- `out/<test>/xzx_report.txt`
  Summary report with peaks, pool creation/deletion counts, transfer counts, leaks, and non-dynamic memory used in GPU transfers.
- `out/<test>/optimization_profile.txt`
  Human-readable profile summary.
- `out/<test>/optimization_profile.cfg`
  Binary profile consumed by the optimizer.

Running the optimizer produces:

- `out/<test>/xzx_optimization_report.txt`
  Summary of how much of the execution actually used the optimization pool versus runtime fallbacks.

Running `make TEST=<name> benchmark` also produces:

- `out/<test>/baseline_output.txt`
- `out/<test>/baseline_metrics.txt`
- `out/<test>/analysis_output.txt`
- `out/<test>/analysis_metrics.txt`
- `out/<test>/optimization_output.txt`
- `out/<test>/optimization_metrics.txt`
- `out/<test>/benchmark_summary.txt`

Running `make benchmark-all` also produces:

- `out/benchmark_all_summary.txt`
  Aggregated timing summary across the configured benchmark test set.

## Optimization Profile Format

The binary profile is versioned.

- header fields:
  - `magic`
  - `version`
  - `requested_size`
  - `pairs_count`
- row fields:
  - `cyclic`
  - `device_file_hash`
  - `device_line`
  - `host_file_hash`
  - `host_line`
  - `max_size`
  - `count`

The optimizer validates the header before loading the profile.

## Evaluation Methodology

Recommended flow:

1. Run a baseline build.
2. Run analysis and inspect the generated profile.
3. Run optimization with that profile.
4. Compare correctness and reported metrics.

The simplest reproducible path is:

```bash
make TEST=test_offsets benchmark
```

This leaves baseline, analysis, and optimization outputs plus timing summaries under `out/test_offsets/`.

To execute the current small regression/benchmark matrix in one go:

```bash
make benchmark-all
```

Use baseline to establish:

- correct output
- execution time
- optional CUDA profiler data if you collect it externally

Use analysis to answer:
- what allocation sites are paired between host and device
- what the estimated optimization pool size should be
- how much dynamic memory is involved in GPU-related activity
- how much non-dynamic host memory appears in transfers

Main analysis files:

- `out/<test>/xzx_report.txt`
- `out/<test>/optimization_profile.txt`
- `out/<test>/xzx_data.csv`

Useful metrics from `xzx_report.txt`:

- `Max total GPU-related dynamic memory`
- `Max pageable memory not related to GPU`
- `Total non-dynamic memory used in GPU transfers`
- `Device pools created` / `deleted`
- `Pinned pools created` / `deleted`
- `Total number of h2d transfers`
- `Total number of d2h transfers`

Use optimization to answer:

- how many allocations actually reused the pool
- how many operations still needed conservative fallback
- how much traffic used the optimized memcpy path

Main optimization file:

- `out/<test>/xzx_optimization_report.txt`

Useful metrics from `xzx_optimization_report.txt`:

- `Optimization pool size`
- `Optimized host allocations`
- `Fallback host allocations`
- `Optimized pinned allocations`
- `Fallback pinned allocations`
- `Optimized device allocations`
- `Fallback device allocations`
- `Optimized memcpy calls` / bytes
- `Fallback memcpy calls` / bytes
- `Optimized memset calls` / bytes
- `Fallback memset calls` / bytes

## Known Limitations

- The optimizer is profile-guided, so behavior may diverge from the analyzed execution.
- Host and pinned allocations currently use a conservative fallback-by-default policy to preserve correctness.
- The included tests are still synthetic and should not be treated as final benchmarks.
- The project assumes CUDA programs that tolerate wrapper-based interception through the provided macros.

## Debugging

Both analyzer and optimizer support optional debug traces when compiled with `-DXZX_DEBUG`.

This is useful for inspecting:

- how allocation pairs are formed
- how `cudaMemcpy` calls are matched
- when the optimizer uses the shared pool
- when the optimizer falls back to the CUDA runtime
