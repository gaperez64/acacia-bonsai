# Reviewer Guidelines for Artifact Evaluation

## Introduction

This document provides concise instructions to reproduce the main functionality of the artifact. The artifact is organized into three independent components:

- FMCAD 2026 benchmarking scripts (Section 1)
- Benchmark log visualization (Section 2)
- CLI toolchain (Section 3)
- Jupyter environment (Section 4)

The design goal is strict separation of concerns: each component can be validated independently without requiring execution of the others. This reduces setup complexity and isolates failure modes during evaluation.

---

## 1. FMCAD 2026 Benchmarks (Local Execution)

This benchmark suite is designed to run without containerization. The intention is to keep execution transparent and allow inspection of all scripts.

### Source archive

The full benchmark environment is distributed as a compressed archive:

```bash
acacia-bonsai-2.0.17.zip
```

This archive includes solver binaries, benchmark instances, and orchestration scripts.

---

### Step 1: Unpack the archive

Unpacking initializes the working directory structure. No compilation is required at this stage.

```bash
unzip acacia-bonsai-2.0.17.zip
cd acacia-bonsai-2.0.17
```

After extraction, the `benchmarking/` directory contains all execution scripts.

---

### Step 2: Run benchmark script

The main entry point executes a curated subset of benchmarks designed for evaluation efficiency.

```bash
./benchmarking/fmcad26-bench.sh -q best_mona
```

The `best_mona` configuration selects a reduced but representative subset of solver settings. This is intended for artifact evaluation time constraints.

If this flag is omitted, the script executes the full benchmark suite, which significantly increases runtime and is not required for validation.

---

### Optional configuration override

Advanced users can override solver parameters using an INI-style configuration file. This allows controlled experimentation with heuristics and solver strategies.

```bash
./benchmarking/fmcad26-bench.sh -q best_mona -n native_file.ini
```

---


## 2. Benchmark Log Visualization

The artifact includes precomputed benchmark logs:

- `_bm-logs-all-on-2021crit.zip`
- `_bm-logs-top4-on-2024_20s.zip`

These archives contain JSON-encoded experimental traces. Each file corresponds to a single experimental configuration and contains performance metrics over time or instances.

The visualization pipeline converts these raw logs into a uniform intermediate representation before plotting. This ensures consistent scaling and formatting across different experimental batches.

### Step 1: Create output directory

The intermediate directory is required because the plotting tool expects a flat namespace of processed logs.

```bash
mkdir mkplottable
```

### Step 2: Convert raw logs into plottable format

Each JSON file is processed independently. The script extracts relevant fields (e.g., runtime, success rates, instance metadata) and normalizes them.

```bash
for f in _bm-logs/*.json; do
  meson-to-mkplot.sh $(basename "$f" .json) "$f" > mkplottable/$(basename "$f")
done
```

This step is purely deterministic: the transformation is a syntactic reformatting plus metric extraction.

### Step 3: Generate aggregated plot

The final plotting stage aggregates all processed benchmark files. The y-axis is logarithmic to emphasize performance differences across multiple orders of magnitude, which is standard in synthesis benchmarks.

```bash
mkplot.py --lloc='upper left' --ymin=1e-2 --ylog -b pdf --save-to plot.pdf mkplottable/*.json
```

The output `plot.pdf` reproduces the figures used in the paper.

---


## 3. CLI Toolchain (Docker Execution)

The CLI toolchain provides a reproducible synthesis environment. It encapsulates all dependencies, including the Spot model checker and the synthesis backend, ensuring consistent behavior across machines.

---

### Step 1: Load image

The environment is distributed as prebuilt Docker images for different architectures.

```bash
docker load -i docker_image_20260427_2322_acacia_cli_amd64.tar
```

or for ARM systems:

```bash
docker load -i docker_image_20260427_2322_acacia_cli_arm64.tar
```

Loading ensures all dependencies are available locally without compilation.

---

### Alternative: pull from registry

If Docker Hub / GHCR access is available, the image can be fetched directly.

```bash
docker pull ghcr.io/gaperez64/acacia-bonsai:latest
```

---

### Step 2: Verify installation

This step ensures that Docker correctly registered the image.

```bash
docker images
```

Expected output includes:
- `ghcr.io/gaperez64/acacia-bonsai`

---

### Step 3: Start container

The container provides an isolated execution environment with all toolchain components preconfigured.

```bash
docker run --name acacia -it ghcr.io/gaperez64/acacia-bonsai:latest
```

Alternatively, a local image identifier can be used.

```bash
docker run --name acacia -it <IMAGE_ID>
```

---

### Step 4: Compile toolchain inside container

Compilation links solver components and builds optimized binaries.

```bash
./scripts/compile.sh
```

This step is required before executing synthesis tasks.

---

### Example: realizable instance (LTL formula)

This specification encodes a liveness-style property: every request eventually leads to a grant under fairness assumptions.

```bash
./scripts/acacia-bonsai.sh best_mona \
  -f '((G (F (req))) -> (G (F (grant))))' -i req -o grant
```

Expected result:

```text
REALIZABLE
```


### Example: realizable instance (tlsf file)

This example reads a TLSF specification from file.

```bash
cat examples/realizable.tlsf | ./scripts/acacia-bonsai.sh best_decomp_mona --tlsf
REALIZABLE
```

---

### Example: unrealizable instance

This example reads a TLSF specification from file. The instance is constructed to violate realizability conditions under all strategies.

```bash
cat examples/unrealizable.tlsf | ./scripts/acacia-bonsai.sh best_decomp_mona --tlsf
```

Expected result:

```text
UNREALIZABLE
```

---

## 4. Jupyter Environment (Docker Execution)

The Jupyter environment is intended for interactive exploration of synthesis workflows. It provides preinstalled notebooks and a configured kernel with all dependencies resolved.

---

### Step 1: Load image

As with the CLI, separate images exist per architecture.

```bash
docker load -i docker_image_20260427_2322_acacia_boomslang_amd64.tar
```

or:

```bash
docker load -i docker_image_20260427_2322_acacia_boomslang_arm64.tar
```

---

### Step 2: Start Jupyter server

This command exposes port 8888 and launches a notebook server inside the container.

```bash
docker run --rm -p 8888:8888 ghcr.io/gaperez64/acacia-boomslang:latest
```

After startup, the container prints a URL of the form:

```text
http://127.0.0.1:8888/tree?token=...
```

This URL provides authenticated access to the notebook environment.

---

### Available notebooks

The environment contains two primary notebooks:

- `example_simulate.ipynb`  
  Demonstrates simulation of synthesized strategies under trace execution.

- `example_ucb_builder.ipynb`  
  Demonstrates construction and exploration of synthesis configurations using UCB-style heuristics.

These notebooks are intended as reproducibility anchors: they show how CLI-level synthesis integrates into higher-level experimental workflows.

