## Install Zero

Install the latest Zero release:

```sh
curl -fsSL https://zerolang.ai/install.sh | bash
export PATH="$HOME/.zero/bin:$PATH"
zero --version
```

The installer downloads the latest matching binary from
`github.com/vercel-labs/zero`, verifies it against the release checksum file,
and writes it to `$HOME/.zero/bin/zero`. Set `ZERO_INSTALL_DIR` to choose a
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
for relevant tools. Native direct emitters are the current artifact path.

```sh
zero build --emit exe --target linux-musl-x64 examples/hello.0 --out .zero/out/hello
```

To build the compiler from a local checkout instead, use the repository wrapper:

```sh
pnpm install
make -C native/zero-c
bin/zero --version
```

The repository validation commands are:

```sh
pnpm run conformance
pnpm run native:test
pnpm run docs:test
ZERO_BENCH_RUNS=1 pnpm run bench
```
