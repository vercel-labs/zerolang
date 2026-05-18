## Install Zero

Install the latest Zero release:

```sh
curl -fsSL https://zerolang.ai/install.sh | bash
export PATH="$HOME/.zero/bin:$PATH"
zero --version
```

The installer downloads the latest matching binary from
`github.com/vercel-labs/zero`, verifies it against the release checksum file,
verifies the signed trust list, and writes it to `$HOME/.zero/bin/zero`. The
trust list is stored under `$HOME/.zero/trust`; set `ZERO_TRUSTED_KEYS_DIR` to
choose a different trust directory. Set `ZERO_INSTALL_DIR` to choose a
different install directory. On Linux it installs the static musl build by
default; set `ZERO_LINUX_FLAVOR=gnu` to install the glibc-targeted build.

Use `zero doctor` to check the local environment:

```sh
zero doctor
zero doctor --json
```

Supported native executable builds use direct emitters, so a C compiler is not
required for the normal path.

`zero doctor` still checks the pieces that affect real builds:

- PATH health
- workspace write access
- bundled target support
- target SDK/sysroot readiness
- interop tool readiness

`zero doctor --json` includes `targetToolchains`, a per-target readiness matrix
for relevant tools.

The `--emit wasm --target wasm32-wasi` path writes a minimal WebAssembly module
directly and does not require an external C toolchain. It supports the
direct-wasm MVP subset.

```sh
zero build --emit exe --target linux-musl-x64 examples/hello.0 --out .zero/out/hello
```

To build the compiler from a local checkout instead, use the repository wrapper:

```sh
npm install
make -C native/zero-c
bin/zero --version
```

The repository validation commands are:

```sh
npm run conformance
npm run native:test
npm run docs:test
ZERO_BENCH_RUNS=1 npm run bench
```
