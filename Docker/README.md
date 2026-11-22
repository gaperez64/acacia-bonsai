# Docker

Run host commands from the repository root. The CLI image contains sources and
build dependencies; Boomslang includes the Python module, Spot and Jupyter.

## Build and run the CLI

On the host:

```sh
docker pull ghcr.io/gaperez64/acacia-bonsai:latest
docker run --name acacia -it ghcr.io/gaperez64/acacia-bonsai:latest
```

In the container, compile Spot and the `docker_default` configurations, then run:

```sh
./scripts/compile.sh
./scripts/acacia-bonsai.sh -f 'G F req -> G F grant' -i req -o grant
./scripts/acacia-bonsai.sh --tlsf < examples/realizable.tlsf
```

Compilation targets the local CPU and uses all available cores for Spot.
Keep the named container to reuse builds; re-enter from the host with:

```sh
docker start -ai acacia
```

The wrapper defaults to the first `docker_default` member. Pass another member
as its first argument; the [configuration guide](../docs/configuration.md#inspect-a-preset)
describes the slots. For example, select the forward-inclusive second slot:

```sh
preset=$(python3 scripts/acacia-config.py list-group docker_default | sed -n '2p')
./scripts/acacia-bonsai.sh "$preset" --tlsf < examples/realizable.tlsf
```

## Write a controller

Inside the container, send the ASCII AIGER controller to stdout and diagnostics
to stderr:

```sh
./scripts/acacia-synthesis.sh --tlsf < examples/realizable.tlsf > controller.aag
```

To save it on the host after compilation, with the container running:

```sh
docker exec -i acacia ./scripts/acacia-synthesis.sh --tlsf \
  < examples/realizable.tlsf > controller.aag
```

`acacia-synthesis.sh` forwards to `acacia-bonsai.sh --synthesis`; do not add `-s`.
The binary uses `-s FILE`, while wrappers use `--tlsf` without a filename for stdin.
Both preserve the solver's exit status, including 1 for `UNREALIZABLE`.

## Use Python notebooks

On the host:

```sh
docker run --rm -p 127.0.0.1:8888:8888 ghcr.io/gaperez64/acacia-boomslang:latest
```

Open the localhost URL with Jupyter's printed token. It starts in
`python_examples/` with Spot and Acacia-Bonsai on the Python path.
`--rm` deletes the container when it stops; mount examples to keep edits:

```sh
docker run --rm -p 127.0.0.1:8888:8888 \
  -v "$PWD/python_examples:/notebooks" -e NOTEBOOK_DIR=/notebooks \
  ghcr.io/gaperez64/acacia-boomslang:latest
```

Image definitions: [CLI](../Dockerfile.cd), [Boomslang](../Dockerfile.boomslang).
Entry points: [compile](../scripts/compile.sh), [CLI wrapper](../scripts/acacia-bonsai.sh),
[Jupyter](../scripts/launch-jupyter.sh).
