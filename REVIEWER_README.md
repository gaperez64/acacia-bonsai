# Reviewer Guidelines for Artifact Evaluation

## Introduction

This document provides concise instructions to reproduce the main functionality of the artifact. This readme contains the following four sections:

- Section 1: FMCAD 2026 benchmarking scripts
- Section 2: Benchmark log visualization
- Section 3: CLI toolchain
- Section 4: Jupyter environment

---

## 1. FMCAD 2026 Benchmarks (Local Execution)

This benchmark suite is designed to run without containerization. All commands can be done on your local computer.

Dependencies:
- ZShell
- GCC-14 (`gcc-14` and `g++-14` packages on Ubuntu)

### Source archive

The full benchmark environment is distributed as a compressed archive:

```bash
acacia-bonsai-2.0.17.zip
```

---

### Step 1: Unpack the archive

You first have to extract the zip archive.

```bash
unzip acacia-bonsai-2.0.17.zip
cd acacia-bonsai-2.0.17
```

---

### Step 2: Run benchmark script

The script `fmcad26-bench.sh` located in the `benchmarking` directory provides the entry point for the benchmarks. The `-q best_mona` argument allows you to quickly run a small subset for the benchmarks. The full benchmarks can take multiple hours to run.

```bash
./benchmarking/fmcad26-bench.sh -q best_mona
```

---

### Optional configuration override

Advanced users can provide a "meson-native" configuration file. This allows one to specify, e.g., the desired compiler and location of dependencies, as described in the [Meson documentation](https://mesonbuild.com/Native-environments.html).

```bash
./benchmarking/fmcad26-bench.sh -q best_mona -n native_file.ini
```

---

### Step 3: expected results

A folder `_bm-logs-fmcad26-quick-best_mona` will have been created with an output file `best_mona.json`. In the root of the project you will now find a file `fmcad26-quick-best_mona.pdf` with a visualisation of the output.


## 2. Benchmark Log Visualization

The artifact includes precomputed benchmark logs:

- `_bm-logs-all-on-2021crit.zip`
- `_bm-logs-top4-on-2024_20s.zip`

These archives contain JSON-encoded experimental traces. Each file corresponds to a single experimental configuration and contains performance metrics. Below you will find information on how to convert these JSON files into PDF files with the visualisations. All figures are also present in the paper.

### Step 1: Create output directory

The intermediate directory is required because the plotting tool expects a flat namespace of processed logs.

```bash
mkdir mkplottable
```

### Step 2: Convert raw logs into plottable format

Each JSON file is processed independently. The script extracts relevant fields and turns it into a format that `mkplot.py` can read.

```bash
for f in _bm-logs/*.json; do
  meson-to-mkplot.sh $(basename "$f" .json) "$f" > mkplottable/$(basename "$f")
done
```

### Step 3: Generate aggregated plot

Next, we need to use `mkplot.py` to visualise the data.

```bash
mkplot.py --lloc='upper left' --ymin=1e-2 --ylog -b pdf --save-to plot.pdf mkplottable/*.json
```

The output `plot.pdf` reproduces the figures used in the paper.

---


## 3. CLI Toolchain (Docker Execution)

Aside from the source code and benchmarking scripts, we also provide a Docker container that contains Acacia-Bonsai as a CLI tool.

---

### Step 1: Load image

We provide prebuilt Docker images for different architectures.

```bash
docker load -i docker_image_20260427_2322_acacia_cli_amd64.tar
```

or for ARM systems:

```bash
docker load -i docker_image_20260427_2322_acacia_cli_arm64.tar
```

---

### Alternative: pull from registry

If Docker Hub / GHCR access is available, then the image can be fetched directly.

```bash
docker pull ghcr.io/gaperez64/acacia-bonsai:latest
```

---

### Step 2: Verify installation

Once the image is loaded/pulled, you can verify that it is present on your system:

```bash
docker images
```

Expected output: the image `ghcr.io/gaperez64/acacia-bonsai` should be present.

---

### Step 3: Start container

The container can be started as follows:

```bash
docker run --name acacia -it ghcr.io/gaperez64/acacia-bonsai:latest
```

---

### Step 4: Compile toolchain inside container

Once the container is started, the Acacia-bonsai tool has to be compiled:

```bash
./scripts/compile.sh
```

This step is required before executing synthesis tasks.

---

### Example: realizable instance (LTL formula)

We will now provide a few examples to use within the container.

It is possible to call Acacia using an LTL formula:

```bash
./scripts/acacia-bonsai.sh best_decomp_mona \
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
```

Expected result:

```text
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

Acacia-Bonsai also comes with a Python interface (Acacia-Boomslang). To provide easy
access to the Python interface, we provide a Docker image that contains a Jupyter server.

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

Alternatively, you can pull the image

```bash
docker pull ghcr.io/gaperez64/acacia-boomslang:latest
```


Once the image is loaded/pulled, you can verify that it is present on your system:

```bash
docker images
```

Expected output: the image `ghcr.io/gaperez64/acacia-boomslang` should be present.

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
- `example_ucb_builder.ipynb`  
