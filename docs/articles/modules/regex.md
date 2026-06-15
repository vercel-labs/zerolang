## When To Use std.regex

In Zerolang, use `std.regex` to match text against a documented ECMA-262-leaning
regular expression subset, such as JSON Schema `pattern` checks.

Supported syntax: literals, `.`, character classes with negation, ranges, and
`\d \D \w \W \s \S`, anchors `^` `$` and word boundaries `\b` `\B`, greedy
quantifiers `*` `+` `?` `{m}` `{m,}` `{m,n}`, alternation `|`, and capturing or
`(?:...)` non-capturing groups (matching only; no capture extraction). Matching
is by Unicode codepoint over UTF-8 text and searches anywhere in the text unless
the pattern is anchored, like ECMAScript `RegExp.prototype.test`. When multiple
matches start at the same byte, span-returning helpers use the longest end
position, so `a|ab` finds `ab` in `ab`.

Unsupported constructs never misparse silently. Compilation fails with a
structured status code: `1` backreference, `2` lookahead, `3` lookbehind,
`4` named group, `5` lazy quantifier, `6` group modifier or inline flags,
`7` unicode property escape, `8` invalid syntax, `9` invalid quantifier range,
`10` program over the buffer or 2048-byte limit, `11` pattern is not valid
UTF-8, `12` group nesting over depth 32.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.regex.compile(buffer, pattern)` | `Maybe<Span<u8>>` | Compiles a pattern into a caller-owned buffer; returns the compiled program span or `null` on any compile failure. |
| `std.regex.compileStatus(buffer, pattern)` | `u32` | Compiles and returns `0` or the structured status code for diagnostics. |
| `std.regex.compileErrorOffset(buffer, pattern)` | `Maybe<usize>` | Returns the pattern byte offset for a compile failure, or `null` when the pattern compiles. |
| `std.regex.statusName(status)` | `String` | Names a status code, such as `unsupported backreference`. |
| `std.regex.isMatch(program, text)` | `Bool` | Tests text against a compiled program. Compile once, then match many times. |
| `std.regex.matches(pattern, text)` | `Maybe<Bool>` | One-shot compile and match with an internal 1024-byte program buffer; returns `null` when the pattern does not compile. |
| `std.regex.contains(pattern, text)` | `Maybe<Bool>` | Alias-shaped one-shot search helper; returns `null` when the pattern does not compile. |
| `std.regex.findIndex(pattern, text)` | `Maybe<usize>` | Returns the first matching byte index, the input length when absent, or `null` when the pattern does not compile. |
| `std.regex.find(pattern, text)` | `Maybe<Span<u8>>` | Borrows the first matching span, or returns `null` when absent or invalid. |
| `std.regex.findCount(pattern, text)` | `Maybe<usize>` | Counts non-overlapping matches, or returns `null` when the pattern does not compile. |
| `std.regex.findNth(pattern, text, index)` | `Maybe<Span<u8>>` | Borrows the zero-based non-overlapping match at `index`, or returns `null` when absent or invalid. |
| `std.regex.findNthIndex(pattern, text, index)` | `Maybe<usize>` | Returns the byte index of the zero-based non-overlapping match, the input length when absent, or `null` when invalid. |
| `std.regex.replace(buffer, pattern, text, replacement)` | `Maybe<Span<u8>>` | Replaces non-overlapping matches with literal replacement bytes into caller storage. |
| `std.regex.splitCount(pattern, text)` | `Maybe<usize>` | Counts fields separated by non-empty regex matches, or returns `null` when the pattern does not compile. |
| `std.regex.split(pattern, text, index)` | `Maybe<Span<u8>>` | Borrows the zero-based field separated by non-empty regex matches, or returns `null` when absent or invalid. |

## Example

```zero
pub fn main(world: World) -> Void raises {
    var storage: [512]u8 = [0; 512]
    let buffer: MutSpan<u8> = storage
    let compiled: Maybe<Span<u8>> = std.regex.compile(buffer, "^[a-z]+-\\d{2,4}$")
    if !compiled.has {
        return
    }
    let program: Span<u8> = compiled.value
    let quick: Maybe<Bool> = std.regex.matches("^(cat|dog)s?$", "dogs")
    let first: Maybe<Span<u8>> = std.regex.find("\\d+", "build-2048")
    let second: Maybe<Span<u8>> = std.regex.findNth("\\d+", "a1 b22 c333", 1)
    var replaced_storage: [16]u8 = [0; 16]
    let replaced: Maybe<Span<u8>> = std.regex.replace(replaced_storage, "\\d+", "a1 b22", "#")
    let fields: Maybe<usize> = std.regex.splitCount("[,;]", "red,green;blue")
    let middle: Maybe<Span<u8>> = std.regex.split("[,;]", "red,green;blue", 1)
    if std.regex.isMatch(program, "build-2048") && !std.regex.isMatch(program, "build-1") && (quick.has && quick.value) && first.has && std.mem.eql(first.value, "2048") && second.has && std.mem.eql(second.value, "22") && replaced.has && std.mem.eql(replaced.value, "a# b#") && fields.has && fields.value == 3 && middle.has && std.mem.eql(middle.value, "green") {
        check world.out.write("regex ok\n")
    }
}
```

Diagnosing a rejected pattern:

```zero
pub fn main(world: World) -> Void raises {
    var storage: [128]u8 = [0; 128]
    let buffer: MutSpan<u8> = storage
    let status: u32 = std.regex.compileStatus(buffer, "(?=lookahead)")
    let offset: Maybe<usize> = std.regex.compileErrorOffset(buffer, "(?=lookahead)")
    if status != 0 {
        check world.out.write(std.regex.statusName(status))
        check world.out.write("\n")
    }
}
```

Effects: none.

Allocation behavior: `compile` and `compileStatus` write the caller buffer;
`isMatch` and `matches` allocate nothing on the heap.

Error behavior: `compile` returns `null`, `compileStatus` returns a status code
naming the construct, and `compileErrorOffset` returns the byte offset for a
failed compile. One-shot helpers return `null` for invalid patterns; `isMatch`
returns `false` for malformed program spans or invalid UTF-8 text.

`find`, `findNth`, `replace`, `split`, and their index/count variants use the
leftmost start and longest end for each match. `split` and `splitCount` use
non-empty regex matches as separators. Zero-length matches are ignored as
separators so callers get deterministic field traversal without a cursor object.

Target support: current compiler targets.
