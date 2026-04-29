
# Acacia-bonsai FMCAD '26 artifact reviewing instructions

In this document you will find instructions for how to review 
our submission to FMCAD 2026. This readme will contain
minimal instructions how to set up and run the artifact,
in order to streamline the reviewing process as much as possible.

The **main artifacts** are
1. The Python interface of Acacia, called Acacia-Boomslang, which is available via pip.
2. A Docker image that provides access to the CLI of Acacia.
3. A Docker image that provides Jupyter notebook access to the Python interface of Acacia.

For **reproducibility purposes** we also provide the full benchmarking
setup as well as the benchmark data used to generate the two
figures in the paper. This is rather an "expert thing", so we
**do not recommend** doing this during artifact review, even though
this is fully possible.

## Artifact 1: Python interface via PyPi.

This first artifact allows one to install Acacia and its Python interface
using `pip`. The artifact also includes two example Python files to demonstrate
the functionality.

The pip packages on PyPi support Linux and macOS out of the box, on
both ARM64 and AMD64 architectures. We support Python 3.13 and 3.14.

Concrete steps:
- Unzip `acacia-bonsai-2.0.17.zip` (part of the artifact files)
- Copy `python_examples/example_real.py` and `python_examples/example_unreal.py` to a (temporary) directory.
- Install Acacia-Boomslang:
  - Create an environment using venv, conda, or UV. (optional)
  - Important: ensure you are using Python 3.13 or 3.14.
  - Run `pip install acacia-boomslang` and wait for the installation to complete.
  - Run `python example_real.py`:
    - Expected: `Solve result: True`, `Region size 1`
  - Run `python example_unreal.py`
    - Expected: `Solve result: False`

## Artifact 2: CLI via Docker

The CLI of Acacia allows one to compute whether an (TODO) is
or realisable or not. For this, we support both LTL formulas as strings,
as well as `tlsf` files.

### Step 1: loading the Docker image

The artifact contains prebuilt Docker images as `tar` files.
Depending on your architecture (AMD64 or ARM64) you can load these
using one of the following two commands:
```bash
docker load -i docker_image_20260427_2322_acacia_cli_amd64.tar
docker load -i docker_image_20260427_2322_acacia_cli_arm64.tar
```

Alternatively, you can also pull the Docker image directly from the internet:
```bash
docker pull ghcr.io/gaperez64/acacia-bonsai:latest
```

### Step 2: verifying the presence of the image on your system

To ensure that the image is present, run `docker images`.
The output should explicitly mention `ghcr.io/gaperez64/acacia-bonsai`.

### Step 3: start container and compilation

First, start the container using the image that was loaded:

```bash
docker run --name acacia -it ghcr.io/gaperez64/acacia-bonsai:latest
```

Then, **inside the container**, compile the Acacia-bonsai tool: 

```bash
./scripts/compile.sh
```

### Step 4: running synthesis inside the container

Once the container is running and compilation has succeeded, 
you can actually verify realisability.

Specifying an LTL formula as a string:
```bash
./scripts/acacia-bonsai.sh best_decomp_mona \
  -f '((G (F (req))) -> (G (F (grant))))' -i req -o grant
```

This should output `REALIZABLE`

Realisable `tlsf` file:
```bash
cat examples/realizable.tlsf | ./scripts/acacia-bonsai.sh best_decomp_mona --tlsf
```

This should output `REALIZABLE`

Unrealisable `tlsf` file:
```bash
cat examples/unrealizable.tlsf | ./scripts/acacia-bonsai.sh best_decomp_mona --tlsf
```

This should output `UNREALIZABLE`

## Artifact 3: Jupyter server via Docker

### Step 1: loading the Docker image

The artifact contains prebuilt Docker images as `tar` files.
Depending on your architecture (AMD64 or ARM64) you can load these
using one of the following two commands:
```bash
docker load -i docker_image_20260427_2322_acacia_boomslang_amd64.tar
docker load -i docker_image_20260427_2322_acacia_boomslang_arm64.tar
```

Alternatively, you can also pull the Docker image directly from the internet:
```bash
docker pull ghcr.io/gaperez64/acacia-boomslang:latest
```

### Step 2: verifying the presence of the image on your system

To ensure that the image is present, run `docker images`.
The output should explicitly mention `ghcr.io/gaperez64/acacia-boomslang`.

### Step 3: start container

Now, we can start the Docker container and launch the 
Jupyter server.

```bash

```

Once the container is fully started, you will see a URL like the following:
```bash
http://127.0.0.1:8888/tree?token=...
```

This URL should be opened in the browser. **Make sure to copy the `token` argument correctly!**

### Step 4: running notebooks

In the browser you will see the following two notebooks in the filesystem:
- `example_simulate.ipynb`
- `example_ucb_builder.ipynb`

These can be opened and run.

## Reproducibility 1: Limited benchmark

**NOTE: this is an expert step, and we only provide this for full reproducibility of the paper. Our main contributions
are the Acacia-Bonsai CLI and Acacia-Boomslang Python interface.**

This step requires compiling and running Acacia-bonsai locally.
We ran this ourselves on Fedora and Ubuntu. We require ZShell, and GCC14, as well as the meson build system.
The GCC14 compiler MUST be set as the default, and needs to be invoked upon calling `gcc` and `g++`.   

Once these conditions are met, the following command will compile and execute the benchmarks:

```bash
./benchmarking/fmcad26-bench.sh -q best_mona
```

A file `fmcad26-quick-best_mona.pdf` will appear in the root directory of the project, and this contains the plots.
A folder `_bm-logs-fmcad26-quick-best_mona` will appear in the root directory of the project, and this contains `JSON` files with benchmark data.


It is technically possible to run this system on macOS, but then the following are required:
- GCC installation via Homebrew. Apple Clang is NOT supported.
- Compiling and installing Spot using Homebrew GCC
- Creating a [meson-native](https://mesonbuild.com/Native-environments.html) `.ini` file specifying GCC as the compiler and 
the Spot installation directory so that pkgconfig can find it. This can then be passed
to Acacia:
```bash
./benchmarking/fmcad26-bench.sh -q best_mona -n native_file.ini
```

## Reproducibility 2: Full benchmark

**NOTE: this is an expert step, and we only provide this for full reproducibility of the paper. Our main contributions
are the Acacia-Bonsai CLI and Acacia-Boomslang Python interface.**

**WE DO NOT RECOMMEND RUNNING THE FULL BENCHMARK.** As mentioned in
the paper, we ran the benchmarks using a system with
Intel(R) Core(TM)324 i7-11850H (icelake x86_64) and 16GB RAM.
This took between 9 and 12 hours.

To do this, Acacia-bonsai needs to be extracted `acacia-bonsai-2.0.17.zip
` and compiled, same as in the "limited benchmark".

```bash
./benchmarking/fmcad26-bench.sh
```

Note the absence of the `-q best_mona` argument.

Again, macOS is "technically" supported and this requires a [meson-native file](https://mesonbuild.com/Native-environments.html):
```bash
`./benchmarking/fmcad26-bench.sh` -n native_file.ini
```

## Reproducibility 3: Plotting provided benchmark data

**NOTE: this is an expert step, and we only provide this for full reproducibility of the paper. Our main contributions
are the Acacia-Bonsai CLI and Acacia-Boomslang Python interface.**

The artifact zip contains two zip files:
- `_bm-logs-all-on-2021crit.zip`: contains the data for the first plot
- `_bm-logs-top4-on-2024_20s.zip`: contains the data for the second plot

This can be plotted using the instructions in `/benchmarking/README.md`.

## Reproducibility 4: Synthcomp benchmarks

The Synthcomp benchmarks we used are located
in `tests/ltl/synthcomp21` and `tests/ltl/synthcomp24`.
