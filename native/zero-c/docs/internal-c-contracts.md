# Internal C Contracts

## Purpose

This document records source-backed contracts for internal C APIs in
`native/zero-c`.

Scope:

- Documents current behavior only.
- Makes no semantic changes.
- Makes no signature changes.
- Makes no claim that is not supported by current source inspection.
- Captures unresolved contract debt where source evidence is incomplete.

This is a documentation contract. It does not authorize refactors, symbol IDs,
IR representation changes, parser changes, checker changes, lowering changes,
emitter changes, or compiler semantic changes.

## Contract Vocabulary

Use these tags in boundary comments:

- `in`: borrowed input. Callee may read during the call.
- `inout`: borrowed input/output. Callee may read and mutate during the call.
- `out`: borrowed output slot. Callee writes the result as documented.
- `opt out`: nullable output slot. Callee writes only when pointer is not
  `NULL`.
- `sink`: callee takes ownership from caller.
- `return owned`: caller receives ownership and must release it with named free
  function or `free()`.
- `return borrowed`: caller must not free returned pointer.
- `retains`: callee stores pointer or data beyond call return.
- `no-retain`: callee does not store pointer beyond call return.
- `nullable`: pointer may be `NULL`.
- `non-null`: pointer must not be `NULL`.
- `alias ok`: parameters may alias.
- `no alias`: parameters must not alias.

`const T *` means read-only through that pointer. It does not by itself define
nullability, retention, ownership, mutation through nested pointers, or aliasing.

## Comment Pattern

Preferred source comment shape:

```c
// contract:
// - input: in, non-null, read-only, no-retain
// - out: out, non-null, written on success
// - diag: opt out, no-retain
// - returns: false on validation failure, true on success
```

If source evidence is incomplete, do not guess. Record a debt ID in this
document.

## Pointer Effects

Pointer effects are read as follows:

- Pointer parameters without an `out` or `inout` tag are borrowed inputs.
- Output slots are caller-owned storage passed to the callee for writes.
- A function that appends to `ZBuf *out` uses `inout`: the buffer object and its
  owned `data` field may change.
- A destructor or free function consumes contents of an object but usually does
  not consume the object pointer itself. It is modeled as `inout` and leaves the
  object reset where source proves reset behavior.
- `retains` requires source evidence of pointer storage beyond return. Without
  that evidence, document `no-retain` or unresolved status.
- `sink` requires source evidence that the callee takes ownership of a caller
  allocation without duplicating it.

## Boundary Sections

### Diagnostics

Diagnostic boundary type: `ZDiag`.

Observed source:

- `diag_io` in `fs.c` writes `diag->path = path` without duplicating `path`.
- `z_resolve_package_metadata` stores duplicated `manifest_path` in some
  diagnostic paths with `z_strdup`.
- `z_map_source_diag` rewrites `diag->path` and borrow trace declaration paths
  to pointers from `SourceInput.source_line_paths`.
- Emitter and checker helpers write diagnostic struct fields and do not retain
  the `ZDiag *` pointer after return.

Contract:

- `ZDiag *diag` is an output channel at parser, checker, lowering, filesystem,
  and emitter boundaries.
- Most public declarations currently use `diag` as `out, non-null`.
- Some helper paths guard `diag == NULL`; nullability is not proven uniformly at
  public boundaries.
- `diag->path` and `ZBorrowTrace.binding_decl_path` are borrowed unless a local
  producer documents ownership.
- Caller must not infer that diagnostic path pointers are heap-owned.

Unresolved:

- `CONTRACT-DIAG-001` tracks public `diag` nullability and diagnostic pointer
  ownership consistency.

### Parser And Row Syntax

Parser boundary files are `row_syntax.c` and `ast.c`.

Observed source:

- `z_row_tokenize` returns `ZRowTokenVec` by value. Token text is allocated with
  `z_strdup`.
- `z_free_row_tokens` frees each token text and token array, then resets vector
  fields.
- `z_row_analyze_layout` zeroes `ZRowSyntaxFacts` when the pointer is present.
- `z_row_parse_layout` zeroes caller-provided `ZRowTree` and fills owned node
  and trivia arrays.
- `z_free_row_tree` frees row tree arrays and resets fields.
- `z_parse_row` returns `Program` by value. Parser-built AST strings are
  duplicated or builder-owned.
- `z_free_program` releases owned AST strings, expression trees, statement
  trees, and vector storage.
- `z_format_row_layout` returns an owned string from `ZBuf` data or `z_strdup`.

Contract:

- Source text and token/tree inputs are `in, non-null, read-only, no-retain`
  unless a specific declaration states nullable behavior.
- `ZRowSyntaxFacts *facts` is `out, nullable` by source behavior.
- `ZRowTree *tree` is `out, non-null`; tree content is owned by caller after
  success and released with `z_free_row_tree`.
- `ZRowTokenVec` and `Program` return values are `return owned`.
- `z_format_row_layout` is `return owned`; caller frees with `free()`.
- Parser APIs do not prove aliasing safety between token/tree inputs and output
  slots.

Unresolved:

- `CONTRACT-PARSE-001` tracks failure-state guarantees for partially filled
  parser output slots.

### Checker

Checker boundary file is `checker.c`.

Observed source:

- `z_check_program` takes `const Program *program`.
- Checker internals cast selected `const Expr *` and `const Stmt *` values to
  mutate semantic annotations.
- Mutated fields include `Expr.resolved_type`, `Expr.checked_type_args`,
  `Expr.text`, `Expr.bool_value`, `Expr.moves_ownership`, and
  `Stmt.resolved_type`.
- Checker scope and provenance structures duplicate strings for internal
  storage and free them before return.
- `z_set_check_target` stores a borrowed `const ZTargetInfo *` in static
  process state.

Contract:

- `z_check_program` treats `program` as `inout` at the semantic level, even
  though public C type is `const Program *`.
- Checker does not retain `Program *` beyond the call.
- Checker may retain the target pointer set by `z_set_check_target` until a
  later call replaces it.
- `diag` is `out, non-null` at the public checker boundary.

Unresolved:

- `CONTRACT-CHECK-001` tracks whether checker AST annotation should remain a
  documented exception or be reflected in future API types.

### IR Lowering

IR boundary file is `ir.c`.

Observed source:

- `z_lower_program_with_source` returns `IrProgram` by value.
- Lowering clones retained `Program` strings with `z_strdup` and clones AST
  expression/statement trees.
- Lowering copies read-only data segment bytes into `IrDataSegment.bytes`.
- `z_lower_program` delegates to `z_lower_program_with_source(program, NULL)`.
- `z_free_ir_program` releases direct IR allocations and calls
  `z_free_program(&program->program)`.
- Lookup helpers return borrowed pointers into input `Program` or `IrProgram`.

Contract:

- `program`: `in, non-null, read-only during lowering, no-retain`.
- `input`: `in, nullable, read-only during lowering, no-retain`.
- Return value is `return owned`; release with `z_free_ir_program`.
- IR-owned strings and cloned AST strings are duplicated where source uses
  `z_strdup`.
- IR does not prove string interning or canonical symbolic identity.

Unresolved:

- `CONTRACT-IR-001` tracks incomplete helper-by-helper ownership comments for
  internal lowering helpers that allocate temporary strings.

### Emitters

Emitter boundary files currently include target-specific files such as
`emit_elf64.c`, `emit_elf_aarch64.c`, `emit_macho64.c`, and `emit_coff.c`.
There is no `native/zero-c/src/emit.c` file in the inspected tree.

Observed source:

- Public emitter functions are named `z_emit_*_from_ir`.
- Emitters validate `IrProgram` metadata and append object or executable bytes
  to `ZBuf *out`.
- Emitters write diagnostics and return `false` on failure.

Contract:

- `program`: `in, non-null, read-only, no-retain`.
- `out`: `inout, non-null`; emitter appends bytes to caller-owned `ZBuf`.
- `diag`: `out, non-null` unless a specific emitter helper proves nullable
  support.
- Stack query helpers return sizes and do not mutate the program.

Unresolved:

- Emitter aliasing between `program`, `out`, and `diag` is not documented by
  source comments. This is covered by `CONTRACT-ALIAS-001`.

### Buffers And Output Builders

Buffer boundary type: `ZBuf`.

Observed source:

- `zbuf_init` initializes `data = NULL`, `len = 0`, and `cap = 0`.
- `zbuf_append_char`, `zbuf_append`, and `zbuf_appendf` grow `buf->data`.
- `zbuf_append` reads from `text` until NUL.
- `zbuf_free` frees `buf->data` and resets fields.
- Several helpers return `buf.data` directly as an owned string.

Contract:

- `ZBuf *buf`: `inout, non-null`.
- Append inputs are `in, non-null, read-only, no-retain`.
- `ZBuf.data` is owned by the buffer after initialization and append calls.
- Returning `buf.data` transfers ownership to the caller as `return owned`.
- `zbuf_free` releases owned buffer data and resets the buffer.

Unresolved:

- Alias behavior when `text` aliases `buf->data` is not proven.

### Source Input Lifetime

Source boundary type: `SourceInput`.

Observed source:

- `z_resolve_package_metadata` fills `SourceInput metadata` with duplicated
  package and dependency strings, assigns it to `*out`, and expects
  `z_free_source` to release fields.
- `z_free_source` frees source strings, path arrays, import arrays, symbol
  arrays, dependency fields, and related vectors.
- `z_map_source_diag` assigns diagnostic paths from `SourceInput` arrays without
  duplicating them.
- `ir_stable_id_for_source_function` reads `SourceInput.symbol_names`,
  `symbol_kinds`, and `symbol_modules` to build an owned stable ID string.

Contract:

- `SourceInput *out` produced by metadata resolution is `out, non-null` and
  owned by caller after success.
- `SourceInput *input` passed to diagnostic mapping and lowering is
  `in, nullable, read-only, no-retain`.
- Diagnostic paths mapped from `SourceInput` are borrowed and valid only while
  corresponding `SourceInput` storage remains alive.

Unresolved:

- `CONTRACT-SRC-001` tracks exact ownership for all `SourceInput` arrays when
  populated outside `z_resolve_package_metadata`.

## Contract Coverage Table

| API or struct | Contract status | Ownership status | Mutation status | Retention status | Source evidence | Unresolved questions |
| --- | --- | --- | --- | --- | --- | --- |
| `ZBuf` | Documented | Owns `data` after init/append; freed by `zbuf_free` | `inout` builder | No pointer retention for append text | `fs.c` `zbuf_init`, append, free | Alias behavior when appended text aliases buffer data |
| `ZDiag` | Partial | Path fields are borrowed unless producer duplicates | `out` diagnostic record | Producers do not retain `ZDiag *` | `fs.c` `diag_io`, `z_map_source_diag`; checker/emitter writes | `CONTRACT-DIAG-001` |
| `ZRowTokenVec` | Documented | Return owned; token text freed by `z_free_row_tokens` | Tokenizer fills returned value | No-retain for source text | `row_syntax.c` `z_row_tokenize`, `z_free_row_tokens` | Failure-state token ownership on partial diagnostics |
| `ZRowTree` | Documented | Caller owns arrays after layout parse | `out` tree slot reset and filled | No-retain for token vector | `row_syntax.c` `z_row_parse_layout`, `z_free_row_tree` | `CONTRACT-PARSE-001` |
| `ZRowSyntaxFacts` | Documented | Caller-owned struct | `out, nullable`; zeroed when present | No-retain | `row_syntax.c` `z_row_analyze_layout` | None known |
| `Program` | Documented | Parser returns owned AST; `z_free_program` frees nested strings and nodes | Checker annotates selected nested fields | Parser does not retain token/tree inputs | `row_syntax.c` `z_parse_row`; `ast.c` `z_free_program`; `checker.c` casts | `CONTRACT-CHECK-001` |
| `SourceInput` | Partial | Resolver-produced value is caller-owned and released by `z_free_source` | Metadata resolver writes `out`; diag mapper reads input and mutates diag | `z_map_source_diag` returns borrowed paths through diag | `fs.c` `z_resolve_package_metadata`, `z_free_source`, `z_map_source_diag` | `CONTRACT-SRC-001` |
| `ZManifest` | Documented | Parser fills owned strings and arrays; freed by `z_free_manifest` | `out` is zeroed then filled | No-retain for manifest JSON text | `fs.c` `z_parse_manifest_json`, `z_free_manifest` | Failure cleanup expectations if parse stops early |
| `IrValue` | Documented | Owned by containing instruction or value tree | Mutated during construction only | No borrowed child ownership | `zero.h` comments; `ir.c` `ir_free_value` | None known |
| `IrInstr` | Documented | Owned by containing `IrFunction` instruction array | Mutated during construction only | Owns nested instruction arrays | `zero.h` comments; `ir.c` `ir_free_instrs` | None known |
| `IrLocal` | Documented | Owned by containing `IrFunction`; name and shape name duplicated | Filled during lowering | No-retain for source AST names | `ir.c` `ir_function_push_local`, `z_free_ir_program` | None known |
| `IrDataSegment` | Documented | Owns copied bytes | Filled during lowering | No-retain for source literal bytes | `ir.c` `ir_add_readonly_data`, `z_free_ir_program` | None known |
| `IrFunction` | Documented | Owned by `IrProgram`; strings duplicated or built | Filled during lowering | No-retain for source function pointers | `ir.c` `ir_program_push_function`, `z_free_ir_program` | None known |
| `IrProgram` | Documented | Return owned from `z_lower_program*`; freed by `z_free_ir_program` | Lowering fills program and MIR fields | No-retain for caller `Program` or `SourceInput` | `ir.c` `z_lower_program_with_source`, `z_free_ir_program` | `CONTRACT-IR-001` |
| `z_check_program` | Partial | Does not own caller `Program`; owns temporary checker state | Semantic `inout` on nested AST annotations | No-retain for `Program`; target pointer may be retained separately | `checker.c` `set_expr_resolved_type`, `set_expr_checked_type_args`, `mark_owned_move_if_needed` | `CONTRACT-CHECK-001` |
| `z_set_check_target` | Documented | Does not own target pointer | Mutates static checker state | Retains borrowed pointer until replaced | `checker.c` assignment to `configured_check_target` | Lifetime responsibility for caller |
| `z_lower_program*` | Documented | Return owned `IrProgram` | Writes returned IR only | No-retain for inputs | `ir.c` lowering clone paths | `CONTRACT-IR-001` |
| `z_emit_*_from_ir` | Documented | Caller owns output buffer | `out` is `inout`; program read-only | No-retain for IR | target-specific emitter source | `CONTRACT-ALIAS-001` |

## IR String Equality

Current `ir.c` string comparisons are value comparisons. They use `strcmp`,
`strncmp`, or byte equality with `memcmp`. They do not prove interning. They do
not prove canonical symbolic identity. They compare string contents or byte
contents available at the call site.

Important string and byte equality sites:

| Site | Compared values | Equality kind | Observed origin | Ownership or lifetime |
| --- | --- | --- | --- | --- |
| `ir_type_kind` | Type text against built-in type spellings such as `Void`, `Bool`, `u8`, `String`, `Maybe<u8>` | String value equality | Parser type strings and checker `resolved_type` strings | Borrowed input; not retained |
| `ir_maybe_scalar_element_type` | `Maybe<` prefix and trailing `>` | Prefix/value shape check | Checker/parser type string | Borrowed input; temporary inner string is duplicated and freed |
| `ir_error_code_for_name` | Error names `NotFound`, `TooLarge`, `Io` | String value equality | Statement error name from AST | Borrowed input; not retained |
| `ir_find_shape` | `program->shapes.items[i].name` against lookup name | String value equality | Parser AST shape names, cloned in IR program as needed | Returns borrowed `Shape` from input `Program` |
| `ir_shape_arg_for_param` | Shape type parameter names against lookup name | String value equality | Parser AST shape type parameter names | Returns borrowed type arg string |
| `ir_find_enum` | `program->enums.items[i].name` against lookup name | String value equality | Parser AST enum names | Returns borrowed `EnumDecl` |
| `ir_enum_case_value` | Enum case names against case name | String value equality | Parser AST enum case names and expression member text | Borrowed inputs; writes optional scalar output |
| `ir_shape_field_info` | Shape field names against field name | String value equality | Parser AST field names and expression member text | Borrowed field metadata; optional out slots |
| `ir_shape_field_storage_info` | Shape field names against field name | String value equality | Parser AST field names and expression member text | Borrowed field metadata; optional out slots |
| `ir_function_find_local` | MIR local names against lookup name | String value equality | IR local names duplicated with `z_strdup` from parser/checker AST names | Returns borrowed `IrLocal` |
| `ir_find_source_function` | Source function names against lookup name | String value equality | Parser AST function names | Returns borrowed `Function` |
| `ir_find_function_index` | IR function names against lookup name | String value equality | IR function names duplicated from source or specialization names | Writes optional index on success |
| `ir_function_order_compare` | Stable ID strings | String value equality for sort ordering | `SourceInput` symbol metadata or `main.` fallback | Stable ID strings are owned temporaries during ordering |
| `ir_stable_id_for_source_function` | `SourceInput.symbol_names` against function name and kind against `function` | String value equality | Source metadata arrays and parser AST function name | Returns owned stable ID string |
| `ir_add_readonly_data` | Data segment bytes against incoming bytes | Byte value equality with `memcmp` | String literals, byte array literals, or lowered byte views | Segment stores copied bytes |
| `ir_expr_is_byte_view_source` | Callee names such as `std.mem.span` and `std.mem.bufBytes`; resolved type text against byte-view type map | String value equality | Temporary callee name built from expression text; checker type annotation | Temporary callee string is owned and freed |
| `ir_expr_is_mutable_byte_view_dest` | Member text `value`; local name lookup | String value equality | Expression member/identifier text and IR local names | Borrowed expression text; local result borrowed |
| `ir_is_hosted_world_main` | Function name `main`, param type `World`, return type `Void` | String value equality | Parser/checker function signature strings | Borrowed source function fields |
| `ir_is_world_stream_write` | Member text `write`, stream member text, world param name | String value equality | Expression member text and IR function world parameter name | Borrowed expression text; world param duplicated in IR function |
| `ir_lower_byte_view` | String literal handling, member names `span`/`bufBytes`, callee names such as `std.mem.span`, `std.io.bufferedReader` | String value equality and length by `strlen` | Expression text and temporary callee names | Literal bytes copied into IR data segment when retained |
| `ir_binary_op` | Operator text `+`, `-`, `*`, `/`, `%`, `&&`, `||` | String value equality | Parser expression operator text | Borrowed input; not retained |
| `ir_compare_op` | Operator text `==`, `!=`, `<`, `<=`, `>`, `>=` | String value equality | Parser expression operator text | Borrowed input; not retained |
| `ir_substitute_type_param` | Function type parameter names against type text | String value equality | Parser AST type parameter names and type arg strings | Returns borrowed input/type arg pointer |
| `ir_expr_callee_name` | Builds dotted names from identifier/member text for later comparison | String construction, not equality | Parser expression text | Returns owned string that caller frees |
| `ir_lower_expr` std dispatch | `callee_name` against `std.codec.*`, `std.parse.*`, `std.mem.*`, `std.time.*`, `std.rand.*`, `std.proc.*`, `std.crypto.*`, `std.json.*`, `std.net.*`, `std.http.*`, `std.args.*`, `std.env.*`, `std.fs.*`, `std.io.*` | String value equality and `std.parse.` prefix match | Temporary callee name from expression tree | Temporary callee string is owned and freed in each path |
| `ir_lower_expr` member dispatch | Member text `has`, `value`, enum case names, record fields | String value equality | Parser member text, checker resolved type, parser enum/shape fields | Borrowed source strings; no interning claimed |
| `ir_shape_literal_find_field` | Shape literal field names against expected field name | String value equality | Parser AST field initializer names and shape fields | Returns borrowed `FieldInit` |
| `ir_collect_function_locals` | Hosted world param type `World` | String value equality | Parser/checker param type string | Borrowed source param field |
| `ir_collect_stmt_locals` | Local statement type `MutSpan<u8>` and type-map lookups | String value equality | Checker `Stmt.resolved_type` or parser `Stmt.type` | Duplicates names into IR locals |
| `ir_function_vec_has_name` | Cloned function names against specialization name | String value equality | Cloned parser function names and generated specialization names | Borrowed vector fields; specialization temp freed by caller |
| `ir_collect_generic_specializations_from_expr` | Callee lookup by expression identifier text | String value equality through `ir_find_source_function` and specialization plan | Parser expression/function names and type args | Specialized functions are cloned; temp names freed |

### IR String Origin Summary

Observed origins:

- Parser-created AST names, operators, type strings, string literals, and member
  text.
- Checker annotations such as `Expr.resolved_type`, `Stmt.resolved_type`, and
  `Expr.checked_type_args`.
- Lowering-generated temporary strings such as dotted callee names and stable
  IDs.
- `SourceInput` symbol metadata when available for stable IDs.
- Copied data segment bytes derived from string literals or byte array literals.

Observed retention:

- IR retains many AST-derived names and types by duplicating them with
  `z_strdup`.
- Lookup helpers usually borrow input strings and return borrowed source
  objects.
- Temporary callee names and specialization names are owned by the caller and
  freed in `ir.c`.
- Read-only data segment bytes are copied into IR-owned storage.

Unproven:

- No source proves global string interning.
- No source proves canonical symbolic identity.
- No source proves pointer identity is valid for type, name, field, function,
  or callee comparisons.

## Why IR Currently Deals With Strings

Observed source shows that current lowering consumes parser and checker text:

- Parser AST nodes carry names, type strings, operator text, member names, and
  string literal bytes.
- Checker annotates AST nodes with resolved type strings and checked type
  argument strings.
- IR lowering maps those strings into direct backend MIR categories with
  `ir_type_kind`, function/local lookup helpers, field lookup helpers, and
  string-based standard library dispatch.
- `SourceInput` symbol strings are used to derive stable function ordering IDs
  when present.
- String literal text and byte array literals become copied read-only data
  segments.

Current practical role:

- Type strings select `IrTypeKind`.
- Name strings resolve functions, locals, fields, enum cases, and shapes.
- Callee strings select supported direct backend standard helper lowering.
- Operator strings select binary and comparison operations.
- Source metadata strings help produce stable emitted function order.

Future symbol or type-handle designs are possible follow-up questions. Current
source does not require a rewrite, does not introduce symbol IDs, and does not
prove that IR can stop accepting strings without separate design work.

## Unresolved Contract Debt

### CONTRACT-DIAG-001

Question: Which public diagnostic APIs permit `diag == NULL`, and what owns
`ZDiag.path` after each producer writes it?

Current source evidence: Some helpers guard null diagnostics. Public parser,
checker, filesystem, and emitter functions often dereference `diag` directly or
pass it to helpers that write fields. `diag_io` borrows `path`; metadata
resolution sometimes duplicates paths; `z_map_source_diag` borrows
`SourceInput` paths.

Why unresolved: Ownership and nullability are not uniform across producers and
are not documented per function.

Recommended future resolution path: Audit each public function that accepts
`ZDiag *`, document `out` versus `opt out`, and document each diagnostic path
producer as borrowed or owned. Prefer a single diagnostic path ownership rule if
API compatibility allows.

### CONTRACT-PARSE-001

Question: What exact output state is guaranteed after parser/tokenizer failure?

Current source evidence: Row tokenizer may return partially filled tokens after
diagnostics. Layout parse zeroes `ZRowTree` before filling it. Program parsing
returns a by-value `Program` that may contain partial allocations before an
early diagnostic return.

Why unresolved: Source shows cleanup functions exist, but boundary comments do
not yet state whether callers must always free partial outputs after failure.

Recommended future resolution path: Document failure cleanup obligations for
`ZRowTokenVec`, `ZRowTree`, and `Program`; add focused tests only under a later
test-authorized directive.

### CONTRACT-CHECK-001

Question: Should checker AST annotation be represented in the public C type
instead of using a `const Program *` boundary?

Current source evidence: Checker casts through `const` to update expression and
statement annotations, including resolved types, checked type args, meta result
text, boolean values, and ownership move markers.

Why unresolved: Changing the signature or AST model would be a public API and
semantic design change, which is outside this documentation pass.

Recommended future resolution path: Decide whether `z_check_program` remains a
documented semantic `inout` exception or changes to `Program *` in a dedicated
API migration.

### CONTRACT-IR-001

Question: Which internal IR helpers return borrowed pointers, return owned
strings, or duplicate strings for retention?

Current source evidence: Several helpers have source comments. Examples:
lookup helpers return borrowed objects, specialization and callee-name helpers
return owned strings, and clone/lowering paths use `z_strdup`.

Why unresolved: The helper set is large and not every allocation helper has a
local comment. Full helper-by-helper comments risk noisy churn.

Recommended future resolution path: Add comments only when a helper crosses a
boundary, returns ownership, or stores data beyond the call.

### CONTRACT-IR-STRING-001

Question: Should IR keep using string value equality for names and types, or
should a future design introduce symbols/type handles?

Current source evidence: `ir.c` uses `strcmp`, `strncmp`, and `memcmp` for type
mapping, helper dispatch, function/local/field lookup, stable ordering, and
data deduplication. No current source proves interning or canonical identity.

Why unresolved: Symbol IDs and type handles would alter IR representation and
compiler internals. That is outside this documentation-only pass.

Recommended future resolution path: Open a dedicated design issue for symbol
identity and type representation. Keep any migration separate from contract
documentation.

### CONTRACT-SRC-001

Question: Which `SourceInput` arrays own strings in all producer paths?

Current source evidence: `z_resolve_package_metadata` constructs owned metadata
and `z_free_source` frees many arrays. Other producer paths may populate source
metadata in `main.c` and should be audited before broad claims.

Why unresolved: This pass inspected core ownership and free behavior but did
not exhaustively prove every producer of every `SourceInput` field.

Recommended future resolution path: Audit all assignments to `SourceInput`
fields and annotate producer ownership near each population boundary.

### CONTRACT-ALIAS-001

Question: Which APIs explicitly allow or forbid aliasing between input pointers,
output slots, diagnostics, and buffers?

Current source evidence: Many APIs use separate pointers but do not document
alias handling. `ZBuf` append behavior reads from `text` while mutating buffer
storage. Emitters append to `out` while reading `program`.

Why unresolved: Source does not contain explicit alias checks or comments for
most public boundaries.

Recommended future resolution path: Add `alias ok` or `no alias` only where
source behavior is tested or structurally obvious. Treat all other aliasing
claims as unresolved.

## Out Of Scope

This contract pass does not:

- Change executable behavior.
- Change signatures.
- Refactor checker mutation.
- Add symbol IDs.
- Change IR representation.
- Change string equality behavior.
- Change parser, checker, lowering, emitter, or codegen semantics.
- Add tests or generated artifacts.

## Acceptance Coverage

- Pointer parameter effects are documented through shared vocabulary and
  boundary comments.
- Ownership and lifetime are documented where source proves them.
- Nullable versus non-null status is documented where source proves it and
  captured as unresolved where not proven.
- Borrowed, retained, duplicated, and owned string behavior is documented for
  parser, checker, IR, diagnostics, source input, and buffers.
- Output slot behavior is documented for row syntax, manifest parsing, source
  metadata, diagnostics, IR lowering, and emitters.
- Aliasing expectations are stated only where source-backed. Unknown aliasing is
  captured under `CONTRACT-ALIAS-001`.
- Parser, checker, IR, diagnostic, emitter, buffer, and source input boundaries
  are covered.
- IR string equality expectations are listed with comparison sites, compared
  values, origin, equality kind, and ownership status.
- Current reasons IR deals with strings are explained from observed source.
- Future symbol/type-handle design is listed only as a possible follow-up
  question.
- Core header and source comments demonstrate the convention without changing
  executable C code.
- Unresolved items are captured with stable debt IDs without overclaiming.
