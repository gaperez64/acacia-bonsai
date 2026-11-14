# Docker usage

Run host commands from the repository root. The CLI image contains source
code and build dependencies. The Boomslang image contains the Python module,
Spot's Python bindings, and Jupyter.

## Build and run the CLI

On the host, pull the image and create a named container:

```sh
docker pull ghcr.io/gaperez64/acacia-bonsai:latest
docker run --name acacia -it ghcr.io/gaperez64/acacia-bonsai:latest
```

In the container's shell, compile Spot and the configurations in
`docker_default`, then run a formula or pipe in a TLSF file:

```sh
./scripts/compile.sh
./scripts/acacia-bonsai.sh -f 'G F req -> G F grant' -i req -o grant
./scripts/acacia-bonsai.sh --tlsf < examples/realizable.tlsf
```

Compilation targets the local CPU. The compile script chooses its own job
count, including all available cores for Spot. Keep the named container to
reuse its builds. From the host, re-enter it with:

```sh
docker start -ai acacia
```

The wrapper defaults to the first configuration in `docker_default`. To
select another, pass a name from that group as the first argument. See
[presets](../docs/configuration.md#inspect-a-preset) for the list.

## Write a controller

In the container, the synthesis wrapper sends an ASCII AIGER controller to
stdout and sends the solver verdict and diagnostics to stderr:

```sh
./scripts/acacia-synthesis.sh --tlsf < examples/realizable.tlsf > controller.aag
```

The file is inside the container. After compilation, you can instead run
from the host and save it in the host's current directory:

```sh
docker exec -i acacia ./scripts/acacia-synthesis.sh --tlsf \
  < examples/realizable.tlsf > controller.aag
```

The container must be running. `acacia-synthesis.sh` forwards to
`acacia-bonsai.sh --synthesis`; do not add `-s` to either form. The binary
itself uses `-s FILE`, while the wrapper uses `--tlsf` without a filename to
read stdin. Both return the solver's exit status, including 1 for
`UNREALIZABLE`.

## Use Python notebooks

On the host:

```sh
docker run --rm -p 127.0.0.1:8888:8888 ghcr.io/gaperez64/acacia-boomslang:latest
```

Open the localhost URL with the token printed by Jupyter. The server starts
in `python_examples/` with Spot and Acacia-Bonsai on the Python search path.
The `--rm` container is deleted when it stops. To keep edited notebooks,
mount the local examples directory:

```sh
docker run --rm -p 127.0.0.1:8888:8888 \
  -v "$PWD/python_examples:/notebooks" -e NOTEBOOK_DIR=/notebooks \
  ghcr.io/gaperez64/acacia-boomslang:latest
```

The image definitions are [Dockerfile.cd](../Dockerfile.cd) and
[Dockerfile.boomslang](../Dockerfile.boomslang). Their entry points are
[compile.sh](../scripts/compile.sh), the
[CLI wrapper](../scripts/acacia-bonsai.sh), and
[launch-jupyter.sh](../scripts/launch-jupyter.sh).
