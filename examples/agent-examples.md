# Agent-oriented CLI examples

Small programs validated with [zero-agent-bench](https://github.com/HKTITAN/zero-agent-bench) (Zero 0.1.3). They show patterns agents need when `std.parse` on argv is unavailable and stdin is not exposed on `World`.

## agent-add.0

Add two non-negative decimal integers from `argv[1]` and `argv[2]`. Parses digits manually from argument strings.

```bash
zero check examples/agent-add.0
zero build --emit exe --out /tmp/agent-add examples/agent-add.0
/tmp/agent-add 6 7
# => 13
```

## agent-sum-from-file.0

Sum whitespace-separated non-negative integers from a file path in `argv[1]` (not stdin).

```bash
echo "6 7" > /tmp/input.txt
zero check examples/agent-sum-from-file.0
# On linux-x64, programs using std.fs.readAll may require object emit + host link:
zero build --emit obj --out /tmp/agent-sum.o examples/agent-sum-from-file.0
cc -o /tmp/agent-sum /tmp/agent-sum.o
/tmp/agent-sum /tmp/input.txt
# => 13
```

## Agent constraints (pilot benchmark)

From benchmarks on [zerolang.ai](https://zerolang.ai) CLI tasks ([full results](https://github.com/HKTITAN/zero-agent-bench/blob/main/results/RESULTS.md), [pilot](https://github.com/HKTITAN/zero-agent-bench/blob/main/results/PILOT.md)):

- Avoid `!` negation and `else if` (use positive branches and nested `if`).
- Prefer `argv[1]` file paths over `world.stdin` when reading input.
- PAR100 dominated failures; community pattern skills: [HKTITAN/zero-skills](https://github.com/HKTITAN/zero-skills).
