# Internal C Contracts

This document defines the first-pass contract vocabulary for internal C APIs in
`native/zero-c`. It documents current behavior where source inspection supports
it. It does not change signatures, ownership, IR representation, parser,
checker, lowering, or emitter behavior.

## Vocabulary

Use these tags in comments near boundary structs and functions:

- `in`: borrowed input. Callee may read during the call.
- `inout`: borrowed input/output. Callee may read and mutate during the call.
- `out`: borrowed output slot. Callee writes the result as documented.
- `opt out`: nullable output slot. Callee writes only when the pointer is not
  `NULL`.
- `sink`: callee takes ownership from caller.
- `return owned`: caller receives ownership and must release it with the named
  free function or `free()`.
- `return borrowed`: caller must not free the returned pointer.
- `retains`: callee stores the pointer or data beyond the call.
- `no-retain`: callee does not store the pointer beyond return.
- `nullable`: pointer may be `NULL`.
- `non-null`: pointer must not be `NULL`.
- `alias ok`: parameters may alias.
- `no alias`: parameters must not alias.

`const T *` means read-only through that pointer. It does not, by itself,
document nullability, retention, ownership, or aliasing.

## Comment Pattern

Prefer short blocks near public/internal boundaries:

```c
// contract:
// - input: in, non-null, no-retain
// - out: out, non-null, written on success
// - diag: opt out, no-retain
// - returns: false on validation failure, true on success
```

When current behavior is unclear, prefer listing it in the follow-up section of
this document. Use `TBD(contract)` in source comments only when local reasoning
would otherwise be misleading.

## Diagnostics

Diagnostic pointers such as `ZDiag *diag` are diagnostic output channels unless
a local comment says otherwise.

- `diag`: normally `out` or `opt out` depending on implementation support.
- Diagnostic buffers are written by the callee when reporting failure details.
- Retention of diagnostic pointer arguments should be documented at each
  boundary. Do not infer it from the type alone.

## Parser And Row Syntax Boundary

Observed from `row_syntax.c` and `ast.c`:

- `z_row_tokenize` returns a `ZRowTokenVec` by value.
- `z_free_row_tokens` releases token vector storage and token text.
- `z_row_parse_layout` fills a caller-provided `ZRowTree`.
- `z_free_row_tree` releases row tree storage.
- `z_parse_row` returns a `Program` by value.
- `z_free_program` releases `Program` storage and owned strings allocated by the
  parser.

Boundary convention for these APIs:

- Source text and token/tree inputs are `in`, `non-null`, and read-only unless a
  function-specific comment says otherwise.
- Returned `ZRowTokenVec` and `Program` values are `return owned`.
- Caller-provided `ZRowTree *`, `ZRowSyntaxFacts *`, and similar result slots
  are `out` or `inout` as documented on the prototype.

## Checker Boundary

Observed from `checker.c`:

- `z_check_program` takes `const Program *program`, but checker internals cast
  selected expression nodes to annotate fields such as `Expr.resolved_type`,
  `Expr.text`, `Expr.bool_value`, and `Expr.moves_ownership`.
- The checker should therefore be treated as validating and annotating the AST,
  not as a purely read-only pass, until this contract is changed.

Boundary convention:

- `program`: `inout` at the semantic level, `non-null`, no-retain. The C type is
  `const Program *`, but current implementation mutates selected nested AST
  fields.
- `diag`: `out`, non-null in current callers unless a specific call path proves
  nullable support.
- `returns`: `true` when validation succeeds, `false` when a diagnostic is
  reported.

## IR Boundary

Observed from `ir.c`:

- `z_lower_program_with_source` returns `IrProgram` by value.
- It duplicates many parser/checker strings with `z_strdup` while constructing
  `ir.program`, `IrFunction`, and `IrLocal` values. It also copies read-only
  data segment bytes.
- `z_lower_program` delegates to `z_lower_program_with_source(program, NULL)`.
- `z_free_ir_program` releases direct IR allocations and then calls
  `z_free_program(&program->program)`.

Boundary convention:

- `program`: `in`, non-null, read during lowering. Current lowering clones many
  retained AST strings instead of retaining the caller's `Program` pointer.
- `input`: `in`, nullable, read during lowering for source metadata when
  present.
- `return owned`: returned `IrProgram` must be released with
  `z_free_ir_program`.

## IR String Value Matching

Current observed behavior in `ir.c`:

- Type and symbol decisions use `strcmp` and `strncmp` in helpers such as
  `ir_type_kind`, `ir_std_http_error_code`, `ir_error_code_for_name`,
  `ir_find_shape`, `ir_find_enum`, `ir_shape_field_info`,
  `ir_shape_field_storage_info`, `ir_function_find_local`,
  `ir_find_source_function`, and `ir_find_function_index`.
- These call sites perform string value matching. They do not check pointer
  identity.
- This document does not claim that strings are interned, canonicalized, or
  globally owned. Source comments should make only local, source-backed claims.

## Emitter Boundary

Emitter functions named `z_emit_*_from_ir` take an IR program and write output
buffers.

Boundary convention:

- `program`: `in`, non-null, read-only, no-retain.
- `out`: `inout`, non-null. Emitters append bytes to caller-owned `ZBuf`.
- `diag`: `out`, non-null unless a function-specific comment proves nullable
  behavior.
- `returns`: `true` on emission success, `false` on failure with diagnostic
  detail.

## Follow-Up Questions

- Which diagnostic APIs permit `diag == NULL`, and which require non-null?
- Which output slots are written on failure versus success only?
- Should checker AST annotation be reflected in the public C type of
  `z_check_program`, or documented as an intentional exception?
- Which `SourceInput` arrays own their strings, and which point into other
  storage?
- Which internal helpers retain strings, duplicate them, or return borrowed
  pointers?
- Should future IR use canonical symbol or type handles instead of repeated
  string value matching?
- Which APIs have aliasing restrictions between input and output parameters?
