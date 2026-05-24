# Zero Rosetta Tasks

This directory contains hand-authored Zero witnesses for Rosetta Code-style tasks.
Each `.0` file is a small executable program with a deterministic success output recorded in `manifest.json`.

The repository checks these tasks with `pnpm run rosetta:local`. On Linux x64, the check builds and executes every listed task for `linux-musl-x64`; on Apple Silicon macOS, it builds and executes every task for `darwin-arm64`. Other hosts still verify buildability for the default target. Pass `--target <target>` or set `ZERO_ROSETTA_TARGET` to check a specific supported target.

Current manifest: 154 tasks.
