## ZDN Format

ZDN (Zero Data Notation) is an **agent-first structured data format** built into the Zero compiler. It is designed for AI agents and LLMs that need to consume compiler output — diagnostics, build results, graph analysis — without the syntactic overhead of JSON.

### Design Philosophy

ZDN is guided by three principles:

1. **One fact per line.** Every line is a self-contained assertion. An agent can find `ok true` by scanning the first few lines without parsing a deeply nested structure.

2. **Indentation is structure.** No closing brackets, no commas, no `]` to match. Reducing indentation implicitly closes the current scope.

3. **Every record has a type.** A ZDN document always starts with a record name (`CheckResult`, `BuildResult`, etc.), so an agent knows what it's reading from the first token.

---

### Syntax Reference

#### Record

A ZDN document is a **record**. A record begins with its name on its own line:

```
CheckResult
  schemaVersion 1
  ok true
```

The record name is an identifier (letters, digits, underscores). It occupies the first line of output.

#### Fields

A field is `fieldName value` on one line:

```
schemaVersion 1
ok true
sourceFile "examples/add.0"
```

| Value type | Example | Quoting required? |
|-----------|---------|-------------------|
| Integer | `count 42` | No |
| Boolean | `ok true` / `ok false` | No |
| Null | `saved null` | No |
| String | `name "hello"` | **Yes** — always `"` delimited |

Strings follow the same escaping rules as JSON:

| Escape | Meaning |
|--------|---------|
| `\"` | Double quote |
| `\\` | Backslash |
| `\n` | Newline |
| `\r` | Carriage return |
| `\t` | Tab |

All other characters — including CJK characters, emoji, and arbitrary UTF-8 — are passed through verbatim.

```
message "shape 'Point' has no field 'y'"
summary "The requested target name is not in the target table."
example "你好，世界！"             ← CJK passes through verbatim
emoji "🎉 test passed 👍"          ← Emoji passes through verbatim
```

#### Nested Objects

An object begins with its name on its own line (no value). Child fields are indented by 2 more spaces:

```
repair
  id "manual-review"
  summary "Run `zero targets` and choose one"
```

No closing marker is needed. When the next line has the same or less indentation, the object is implicitly closed.

#### 4 Levels of Nesting (Real Output)

```
targetFacts
  hostTarget "linux-x64"
  capabilitySupport
    direct
      worldWrite false
```

The indentation count tells the depth:

| Indent | Level | Content |
|--------|-------|---------|
| 0 | Root | `GraphValidation` (record name) |
| 2 | 1 | `targetFacts` (object) |
| 4 | 2 | `hostTarget` (field), `capabilitySupport` (object) |
| 6 | 3 | `direct` (object) |
| 8 | 4 | `worldWrite` (field) |

#### Arrays

An array begins with its `arrayName` on its own line. Items are indented:

```
fixes
  Fix
    id "make-binding-mutable"
    diagnosticCode "TYP009"
  Fix
    id "manual-review"
    diagnosticCode "PAR100"
```

Each array item is introduced by a type tag (`Fix`, `Operation`, `Target`, etc.), which acts as both an item delimiter and a semantic label. Empty arrays are represented by the field name with no following content:

```
fixes                ← empty array (no items following)
```

Array items with a single field can omit the item type tag and place the field inline:

```
sourceFiles
  "examples/main.0"
  "examples/lib.0"
```

---

### Complete Syntax Grammar

```
document   = record-name NL field*
record-name = IDENTIFIER NL
field      = indent IDENTIFIER value NL
           | indent IDENTIFIER NL    (nested object start)
           | indent IDENTIFIER NL field*  (object with children)
           | indent IDENTIFIER NL item*   (array)
           | indent IDENTIFIER value-list (single-line array)

value      = STRING | INTEGER | BOOL | NULL
item       = indent type-tag NL field*  (array item)
           | indent value NL            (value-list item)

value-list = indent value (indent value)*
indent     = 2 * N spaces
```

---

### Comparison: JSON ↔ ZDN

| Aspect | JSON | ZDN | Benefit |
|--------|------|-----|---------|
| Object boundaries | `{...}` | Indentation | No mismatched-bracket errors |
| Array boundaries | `[...]` | Item type tag + indent | Self-documenting items |
| String quoting | Required | Required (same rules) | Familiar to JSON users |
| Record identity | Implicit (from key) | **Explicit first line** | Agent knows type immediately |
| Token efficiency | Full compiler state | Agent-relevant facts only | ~98% less token consumption |
| LLM retrieval | Scan for key in nested `{}` | Grep line prefix | Faster fact extraction |
| UTF-8/CJK/Emoji | Transparent | Transparent (same codec) | Full international support |
| Grammatical ambiguity | None | None | Both are deterministic |

---

### Token Cost Comparison

Measured on `zero check examples/add.0`:

```
JSON beautified:  8380 bytes  ~2095 tokens
JSON compact:     5842 bytes  ~1460 tokens
ZDN:               130 bytes   ~32  tokens
```

ZDN is deliberately **less verbose**. JSON includes every internal compiler data structure (`metaCache`, `compileTime`, `interfaceFingerprints`, `compilerPhases`, `compilerCaches`, `incrementalInvalidation`, `selfHostRouting`, etc.). ZDN includes only the facts an agent needs: the success/failure status, source file, target, and diagnostics.

If an agent needs deeper compiler internals, ZDN output can be combined with `zero explain --zdn <code>` for targeted diagnostic information.

---

### All ZDN Record Types

#### Compilation Commands

| Command | Record | Key Fields |
|---------|--------|------------|
| `zero check --zdn` | `CheckResult` | `ok`, `sourceFile`, `target`, `diagnostics` |
| `zero build --zdn` | `BuildResult` | `ok`, `sourceFile`, `target`, `artifact`, `bytes` |
| `zero run --zdn` | Error only (diagnostic) | — |
| `zero ship --zdn` | `ShipResult` | `ok`, `artifact`, `checksum`, `bytes` |
| `zero size --zdn` | `SizeResult` | `ok`, `sourceFile`, `target`, `artifactBytes` |

#### Analysis Commands

| Command | Record | Key Fields |
|---------|--------|------------|
| `zero test --zdn` | `TestResult` | `ok`, `passedTests`, `failedTests`, `durationMs` |
| `zero doc --zdn` | `DocResult` | `sourceFile`, `target` |
| `zero graph --zdn` | `GraphResult` | `sourceFile`, `target` |
| `zero graph validate --zdn` | `GraphValidation` | `ok`, `graphHash`, `counts`, `validation` |
| `zero graph view --zdn` | `GraphView` | `ok`, `graphHash`, `view` |
| `zero graph check --zdn` | `GraphCheck` | `ok`, `graphHash`, `check` |
| `zero graph patch --zdn` | `GraphPatch` | `ok`, `originalGraphHash`, `operationCount`, `operations` |
| `zero graph roundtrip --zdn` | `GraphRoundtrip` | `ok`, `semanticStable`, `counts`, `comparison` |
| `zero tokens --zdn` | `TokensResult` | `sourceFile` |
| `zero parse --zdn` | `ParseResult` | `sourceFile` |
| `zero explain --zdn` | `ExplainResult` | `code`, `title`, `summary`, `repair` |
| `zero fix --plan --zdn` | `FixPlanResult` | `ok`, `diagnostics`, `fixes` |
| `zero fix --patch --zdn` | `FixPatchResult` | `ok`, `mode`, `patches` |

#### System Commands

| Command | Record | Key Fields |
|---------|--------|------------|
| `zero --version --zdn` | `VersionResult` | `version`, `host` |
| `zero doctor --zdn` | `DoctorResult` | `status`, `host`, `nativeCCompiler`, `targetCCompiler` |
| `zero skills list --zdn` | `SkillsList` | `count`, `skills` |
| `zero targets --zdn` | `TargetsResult` | `host`, `targetCount`, `targets` |
| `zero abi dump --zdn` | `AbiDump` | `sourceFile`, `target`, `shapeCount` |
| `zero abi check --zdn` | `AbiCheck` | `ok`, `sourceFile`, `target` |

#### Time & Memory

| Command | Record | Key Fields |
|---------|--------|------------|
| `zero time --zdn` | `TimeResult` | `sourceFile`, `elapsedMs` |
| `zero dev --zdn` | `DevResult` | `sourceFile`, `target` |
| `zero mem --zdn` | `MemResult` | `sourceFile`, `target` |

---

### Working with ZDN as an Agent

#### Quick Status Check

The most common agent query is "did it work?" Every ZDN record with a boolean outcome has `ok true` or `ok false` on the second or third line. An agent can locate it by scanning lines starting with `  ok`:

```
CheckResult
  schemaVersion 1
  ok true              ← success indicator, always at indent level 1
```

#### Reading Diagnostics

When a command fails, `ok false` is followed by a `diagnostics` array:

```
CheckResult
  schemaVersion 1
  ok false
  diagnostics
    severity "error"
    code "NAM003"
    message "unknown identifier 'nonexistent_name'"
    path "src/main.0"
    line 7
    column 5
    repair
      id "remove-duplicate-definition"
      summary "Remove duplicate definition"
```

Key lines for an agent:
- Line 3: `ok false` — overall failure
- Line 6: `code "NAM003"` — error code for `zero explain --zdn`
- Line 7: `message "..."` — human-readable problem
- Lines 12-13: `repair\n  id "..."` — suggested fix

#### Chaining ZDN Commands

An agent can chain ZDN commands to drill into a problem:

```sh
# Step 1: check
zero check --zdn src/main.0

# Step 2: if failed, get explanation
zero explain --zdn NAM003

# Step 3: request fix plan
zero fix --plan --zdn src/main.0
```

Each step returns a ZDN record. The agent passes the error code from step 1 into step 2, and the source file into step 3.

---

### Output Examples

#### `zero check --zdn` (success)

```
CheckResult
  schemaVersion 1
  ok true
  sourceFile "examples/add.0"
  hostTarget "linux-x64"
  target "linux-x64"
  diagnostics
```

#### `zero check --zdn` (failure with diagnostics)

```
CheckResult
  schemaVersion 1
  ok false
  diagnostics
    severity "error"
    code "NAM003"
    message "unknown identifier 'nonexistent_name'"
    path "conformance/zdn/fail/unknown-name.0"
    line 2
    column 3
    length 1
    expected "visible local, parameter, function, or builtin"
    actual "no matching visible symbol"
    help "declare the name before using it"
    fixSafety "unknown"
    repair
      id "remove-duplicate-definition"
      summary "Remove duplicate definition"
    related
```

#### `zero test --zdn` (pass)

```
TestResult
  schemaVersion 1
  ok true
  sourceFile "conformance/native/pass/test-blocks.0"
  target "linux-x64"
  testBackend "direct-frontend"
  selectedTests 1
  discoveredTests 1
  passedTests 1
  failedTests 0
  expectedFailures 0
  unexpectedPasses 0
  durationMs 0
  exitCode "0"
  stdout "1 test(s) ok\n"
  stderr ""
  testDiscovery
    mode "single-file"
    sourceFileCount 1
    moduleCount 1
  fixtures
    sourceFiles
      "conformance/native/pass/test-blocks.0"
    goldenOutput "1 test(s) ok\n"
    snapshotKey "zero-test-direct-frontend-v1"
  targetFacts
    hostTarget "linux-x64"
```

#### `zero graph validate --zdn`

```
GraphValidation
  ok true
  artifact ".zero/out/test.graph"
  moduleIdentity "module:point"
  graphHash "graph:8cca0b30647a9c3e"
  counts
    nodes 35
    edges 34
  validation
    state "shape-valid"
    ok true
  saved null
```

#### `zero explain --zdn`

```
ExplainResult
  code "TAR001"
  category "target"
  title "Unknown target"
  summary "The requested target name is not in the native bootstrap target table."
  why "Zero keeps target names explicit so cross builds do not silently use host assumptions."
  repair
    id "manual-review"
    summary "Run `zero targets` and choose one of the listed names."
  examples
    bad "zero check --target not-a-target examples/hello.0"
    good "zero check --target linux-musl-x64 examples/hello.0"
```

#### `zero fix --plan --zdn` (with diagnostic)

```
FixPlanResult
  schemaVersion 1
  ok false
  mode "plan"
  appliesEdits false
  input "examples/does-not-exist.0"
  selfHostRepairPolicy
    unsupportedFeatureSafety "requires-human-review"
    compatibilityFallback "removed"
    directFallback "never-c-bridge"
  diagnostics
    code "PAR100"
    message "failed to read 'examples/does-not-exist.0': No such file or directory"
    path "examples/does-not-exist.0"
    line 1
    column 1
    length 1
    fixSafety "requires-human-review"
    repair
      id "manual-review"
      summary "Inspect the diagnostic fields and choose a repair manually."
  fixes
    Fix
    id "manual-review"
    diagnosticCode "PAR100"
    safety "requires-human-review"
    summary "Inspect the diagnostic fields and choose a repair manually."
    appliesEdits false
```

#### `zero doctor --zdn`

```
DoctorResult
  status "warning"
  host "linux-x64"
  nativeCCompiler true
  targetCCompiler true
  writeAccess true
  docsPresent true
```

#### `zero targets --zdn`

```
TargetsResult
  host "linux-x64"
  targetCount 8
  targets
    Target
      name "darwin-arm64"
    Target
      name "darwin-x64"
    Target
      name "linux-musl-x64"
    Target
      name "linux-musl-arm64"
    Target
      name "linux-x64"
    Target
      name "linux-arm64"
    Target
      name "win32-x64.exe"
    Target
      name "win32-arm64.exe"
```

#### `zero graph patch --zdn` (with operations)

```
GraphPatch
  schemaVersion 1
  ok true
  artifact ".zero/test.graph"
  patch "patch.txt"
  originalGraphHash "graph:f76987e99677f1b3"
  patchedGraphHash "graph:a1b2c3d4e5f67890"
  operationCount 1
  operations
    Operation
      index 0
      line 3
      op "set"
      ok true
      node "node:000013"
      field "value"
      value "hello patched\n"
```

#### `zero graph roundtrip --zdn`

```
GraphRoundtrip
  schemaVersion 1
  ok true
  artifact ".zero/test.graph"
  semanticStable true
  lowering "direct-program-graph"
  moduleIdentity "module:add"
  roundtripModuleIdentity "module:add"
  originalGraphHash "graph:8cca0b30647a9c3e"
  roundtripGraphHash "graph:8cca0b30647a9c3e"
  counts
    original
      nodes 35
      edges 34
    roundtrip
      nodes 35
      edges 34
  semanticCounts
    original
      nodes 35
      edges 34
    roundtrip
      nodes 35
      edges 34
  comparison
    ok true
```

---

### Parsing ZDN Programmatically

A minimal ZDN parser can be implemented in any language by following these rules:

1. Split input into lines
2. Count leading spaces to determine nesting depth (`depth = spaces / 2`)
3. First non-empty line is the record name
4. For each subsequent line:
   - If it has a value after the key (separated by space), it's a `field: key value`
   - If it has no value and the next line is indented deeper, it's an object or array start
   - If it has a quoted string value starting with `"`, it's a string
   - If the value is `true`, `false`, or `null`, it's a boolean/null
   - If the value is all digits, it's an integer
5. Decreasing indentation closes the current scope

#### Python Parser Example

```python
def parse_zdn(text):
    """Parse ZDN text into a nested dict/list structure."""
    lines = text.strip().split("\n")
    if not lines:
        return None
    
    record_name = lines[0].strip()
    stack = [{"_type": record_name}]
    prev_depth = -1
    
    for line in lines[1:]:
        stripped = line.lstrip(" ")
        depth = (len(line) - len(stripped)) // 2
        
        while depth <= prev_depth and len(stack) > 1:
            stack.pop()
            prev_depth -= 1
        
        if " " in stripped:
            key, _, raw_val = stripped.partition(" ")
            parsed = parse_zdn_value(raw_val)
            stack[-1][key] = parsed
        else:
            new_obj = {"_type": stripped}
            if isinstance(stack[-1], dict):
                key = stripped.lower()
                if key not in stack[-1]:
                    stack[-1][key] = []
                stack[-1][key].append(new_obj)
            stack.append(new_obj)
        
        prev_depth = depth
    
    return stack[0]

def parse_zdn_value(raw):
    if raw.startswith('"'):
        s = raw[1:]
        buf = []
        i = 0
        while i < len(s) and s[i] != '"':
            if s[i] == '\\' and i + 1 < len(s):
                esc = {"n": "\n", "r": "\r", "t": "\t", '"': '"', "\\": "\\"}
                buf.append(esc.get(s[i+1], s[i+1]))
                i += 2
            else:
                buf.append(s[i])
                i += 1
        return "".join(buf)
    if raw == "true": return True
    if raw == "false": return False
    if raw == "null": return None
    try: return int(raw)
    except ValueError: return raw

# Example
zdn = '''CheckResult
  schemaVersion 1
  ok true
  sourceFile "examples/add.0"
  diagnostics'''
result = parse_zdn(zdn)
# → {'_type': 'CheckResult', 'schemaVersion': 1, 'ok': True,
#    'sourceFile': 'examples/add.0', 'diagnostics': []}
```
