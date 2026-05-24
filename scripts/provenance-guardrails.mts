#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import { existsSync, readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "..");
const checkerPath = path.join(repoRoot, "native/zero-c/src/checker.c");
const callResolvePath = path.join(repoRoot, "native/zero-c/src/call_resolve.h");
const matrixPath = path.join(repoRoot, "conformance/provenance-surface.json");
const conformancePath = path.join(repoRoot, "conformance/run.mjs");

const checker = readFileSync(checkerPath, "utf8");
const callResolve = readFileSync(callResolvePath, "utf8");
const surfaceSpec = JSON.parse(readFileSync(matrixPath, "utf8"));
const conformance = readFileSync(conformancePath, "utf8");

const failures = [];

function fail(message) {
  failures.push(message);
}

function assertIncludes(label, text, needle) {
  if (!text.includes(needle)) fail(`${label}: missing ${needle}`);
}

function assertNotIncludes(label, text, needle) {
  if (text.includes(needle)) fail(`${label}: unexpected ${needle}`);
}

function assertBefore(label, text, first, second) {
  const firstIndex = text.indexOf(first);
  const secondIndex = text.indexOf(second);
  if (firstIndex < 0) fail(`${label}: missing ${first}`);
  if (secondIndex < 0) fail(`${label}: missing ${second}`);
  if (firstIndex >= 0 && secondIndex >= 0 && firstIndex > secondIndex) fail(`${label}: ${first} must appear before ${second}`);
}

function assertMatches(label, text, pattern) {
  if (!pattern.test(text)) fail(`${label}: missing ${pattern}`);
}

function assertFixtureCoverage(label, fixture) {
  const fullPath = path.join(repoRoot, fixture);
  if (!existsSync(fullPath)) {
    fail(`${label}: fixture '${fixture}' does not exist`);
    return;
  }
  const basename = path.basename(fixture);
  if (!conformance.includes(fixture) && !conformance.includes(basename)) {
    fail(`${label}: fixture '${fixture}' is not run by conformance/run.mjs`);
  }
}

function sliceBetween(text, start, end) {
  const startIndex = text.indexOf(start);
  if (startIndex < 0) return "";
  const endIndex = text.indexOf(end, startIndex + start.length);
  if (endIndex < 0) return text.slice(startIndex);
  return text.slice(startIndex, endIndex);
}

function checkerFunctionNames(source) {
  const names = new Set();
  for (const line of source.split("\n")) {
    if (line.trim().endsWith(";")) continue;
    const match = line.match(/^static\b[^{;]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\(/);
    if (match) names.add(match[1]);
  }
  return names;
}

function currentFunctionByLine(source) {
  const current = [];
  let name = "<top-level>";
  const lines = source.split("\n");
  for (const line of lines) {
    if (!line.trim().endsWith(";")) {
      const match = line.match(/^static\b[^{;]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\(/);
      if (match) name = match[1];
    }
    current.push(name);
  }
  return current;
}

function surfaceFixturePaths(fixtures) {
  if (Array.isArray(fixtures)) return fixtures;
  if (!fixtures || typeof fixtures !== "object") return [];
  const paths = [];
  for (const group of ["fail", "pass", "other"]) {
    if (fixtures[group] === undefined) continue;
    if (!Array.isArray(fixtures[group])) {
      fail(`provenance surface spec: fixtures.${group} must be an array`);
      continue;
    }
    paths.push(...fixtures[group]);
  }
  return paths;
}

function parseSurfaceRows(spec) {
  const rows = [];
  if (!spec || typeof spec !== "object") {
    fail("provenance surface spec: expected JSON object");
    return rows;
  }
  if (spec.schemaVersion !== 1) fail("provenance surface spec: schemaVersion must be 1");
  if (spec.kind !== "zero-provenance-surface-matrix") fail("provenance surface spec: unexpected kind");
  if (!Array.isArray(spec.surfaces)) {
    fail("provenance surface spec: surfaces must be an array");
    return rows;
  }
  const seen = new Set();
  for (let index = 0; index < spec.surfaces.length; index++) {
    const row = spec.surfaces[index];
    if (!row || typeof row !== "object") {
      fail(`provenance surface spec: surface ${index + 1} must be an object`);
      continue;
    }
    if (!row.surface || typeof row.surface !== "string") fail(`provenance surface spec: surface ${index + 1} has no name`);
    if (!row.action || typeof row.action !== "string") fail(`provenance surface spec: '${row.surface ?? index + 1}' has no action`);
    if (!Array.isArray(row.owners)) fail(`provenance surface spec: '${row.surface ?? index + 1}' owners must be an array`);
    if (row.surface && seen.has(row.surface)) fail(`provenance surface spec: duplicate surface '${row.surface}'`);
    if (row.surface) seen.add(row.surface);
    rows.push({
      surface: row.surface,
      action: row.action,
      owners: Array.isArray(row.owners) ? row.owners : [],
      fixtures: surfaceFixturePaths(row.fixtures),
    });
  }
  return rows;
}

const functionNames = checkerFunctionNames(checker);
const rows = parseSurfaceRows(surfaceSpec);
const rowsBySurface = new Map(rows.map((row) => [row.surface, row]));

const requiredSurfaces = [
  "Identifier reads",
  "Field reads",
  "Index reads",
  "Direct borrows",
  "Mutable borrows",
  "Shape literals",
  "Array literals",
  "Maybe<T> values",
  "Choice payloads",
  "check unwraps",
  "rescue values",
  "Primitive casts",
  "Plain calls",
  "Generic calls",
  "Shape namespace calls",
  "Receiver calls",
  "Static interface calls",
  "Incomplete summaries",
  "Let bindings",
  "Assignments",
  "Field assignments",
  "Index assignments",
  "Returns",
  "If joins",
  "Match joins",
  "Loop-carried values",
  "Short-circuit expressions",
  "Early returns",
  "Mutref aliases",
];

for (const surface of requiredSurfaces) {
  if (!rowsBySurface.has(surface)) fail(`provenance matrix: missing surface row '${surface}'`);
}

for (const row of rows) {
  if (row.owners.length === 0) fail(`provenance matrix: '${row.surface}' has no checker owner`);
  for (const owner of row.owners) {
    if (!functionNames.has(owner)) fail(`provenance matrix: owner '${owner}' for '${row.surface}' is not a checker function`);
  }
  if (row.fixtures.length === 0) fail(`provenance matrix: '${row.surface}' has no fixture coverage`);
  for (const fixture of row.fixtures) {
    assertFixtureCoverage(`provenance matrix '${row.surface}'`, fixture);
  }
}

const negativeEscapeClasses = [
  ["direct local return", "conformance/native/fail/borrow-return-local.0"],
  ["shape local return", "conformance/native/fail/return-shape-reference-escape.0"],
  ["array local return", "conformance/native/fail/return-array-reference-escape.0"],
  ["nested call return", "conformance/native/fail/shape-field-reference-call-return-escape.0"],
  ["generic call return", "conformance/native/fail/generic-reference-return-escape.0"],
  ["static interface return", "conformance/native/fail/static-interface-return-reference-origin.0"],
  ["mutref stores local ref", "conformance/native/fail/function-mutref-local-reference-escape.0"],
  ["mutref stores local shape", "conformance/native/fail/function-mutref-local-shape-reference-escape.0"],
  ["mutref stores local array", "conformance/native/fail/function-mutref-local-array-reference-escape.0"],
  ["receiver stores local ref", "conformance/native/fail/receiver-method-local-reference-escape.0"],
  ["generic mutref storage", "conformance/native/fail/nested-generic-mutref-reference-origin.0"],
  ["static interface mutref storage", "conformance/native/fail/static-interface-mutref-reference-origin.0"],
  ["mutref alias storage", "conformance/native/fail/mutref-alias-assignment-reference-origin.0"],
  ["safe and unsafe branch merge", "conformance/native/fail/aggregate-if-reference-origin.0"],
  ["two-origin branch merge", "conformance/native/fail/borrow-branch-origin-merge.0"],
  ["check preserves provenance", "conformance/native/fail/check-maybe-reference-origin.0"],
  ["rescue preserves provenance", "conformance/native/fail/rescue-reference-origin.0"],
  ["choice constructor local return", "conformance/native/fail/choice-payload-local-reference-escape.0"],
  ["choice match payload borrow", "conformance/native/fail/choice-match-payload-reference-origin.0"],
  ["choice match payload local return", "conformance/native/fail/choice-match-payload-return-escape.0"],
  ["indexed assignment preserves siblings", "conformance/native/fail/index-reference-assignment-preserves-other-origin.0"],
  ["reachable shared reference", "conformance/native/fail/borrow-assign-while-borrowed.0"],
  ["mutable alias incompatibility", "conformance/native/fail/mutref-alias-assignment-reference-origin.0"],
];

for (const [label, fixture] of negativeEscapeClasses) {
  assertFixtureCoverage(`negative provenance class '${label}'`, fixture);
}

const positivePrecisionClasses = [
  ["direct same-origin mutable reassignment", "conformance/native/pass/borrow-aggregate-reassignment-same-origin.0"],
  ["alias same-origin mutable reassignment", "conformance/native/pass/mutref-alias-assignment-same-origin.0"],
  ["field overwrite clears old origin", "conformance/native/pass/shape-field-reference-reassignment-clears-origin.0"],
  ["disjoint field assignment", "conformance/native/pass/borrow-field-independent-assignment.0"],
  ["caller-owned return ref", "conformance/native/pass/borrow-return-param-ref.0"],
  ["caller-owned field return ref", "conformance/native/pass/borrow-return-param-field-subpath.0"],
  ["plain mutref stores caller-owned ref", "conformance/native/pass/function-mutref-reference-store.0"],
  ["generic mutref stores caller-owned ref", "conformance/native/pass/generic-mutref-reference-store.0"],
  ["receiver stores caller-owned ref", "conformance/native/pass/receiver-method-reference-store.0"],
  ["static interface stores caller-owned ref", "conformance/native/pass/static-interface-mutref-reference-store.0"],
  ["static interface returns caller-owned ref", "conformance/native/pass/static-interface-return-reference-origin.0"],
  ["choice payload returns caller-owned ref", "conformance/native/pass/choice-payload-reference-return.0"],
  ["choice match payload preserves caller-owned ref", "conformance/native/pass/choice-match-payload-reference-origin.0"],
  ["choice match payload returns caller-owned ref", "conformance/native/pass/choice-match-payload-return-origin.0"],
  ["branches merge same origin", "conformance/native/pass/borrow-branch-reassignment.0"],
  ["branches overwrite away unsafe origin", "conformance/native/pass/branch-overwrite-away-reference-origin.0"],
  ["indexed assignment clears overwritten origin", "conformance/native/pass/index-reference-assignment-clears-origin.0"],
];

for (const [label, fixture] of positivePrecisionClasses) {
  assertFixtureCoverage(`positive provenance class '${label}'`, fixture);
}

const canonicalPlaceClasses = [
  ["direct field write", "conformance/native/pass/function-mutref-reference-store.0"],
  ["mutref alias field write", "conformance/native/pass/mutref-alias-assignment-clears-old-origin.0"],
  ["receiver self field write", "conformance/native/pass/receiver-method-reference-store.0"],
  ["generic mutref write", "conformance/native/pass/generic-mutref-reference-store.0"],
  ["constrained interface mutref write", "conformance/native/pass/static-interface-mutref-reference-store.0"],
  ["precise indexed write clears target", "conformance/native/pass/index-reference-assignment-clears-origin.0"],
  ["precise indexed write preserves sibling", "conformance/native/fail/index-reference-assignment-preserves-other-origin.0"],
  ["widened array read overlaps target", "conformance/native/fail/shape-array-reference-index-origin.0"],
];

for (const [label, fixture] of canonicalPlaceClasses) {
  assertFixtureCoverage(`canonical place class '${label}'`, fixture);
}

const requiredFunctions = [
  "expr_value_provenance",
  "expr_reference_provenance",
  "resolve_provenance_call",
  "call_result_value_provenance",
  "function_provenance_summary",
  "function_return_value_provenance",
  "function_storage_effect_summary",
  "choice_constructor_value_provenance",
  "register_match_payload_binding_provenance",
  "apply_checked_call_storage_effects",
  "check_call_expr_expected",
  "check_named_function_call_expected",
  "check_stdlib_table_arg_range_expected",
  "check_receiver_shape_call_expected",
  "finish_shape_method_provenance_call",
  "resolve_shape_namespace_provenance_call",
  "resolve_concrete_constrained_shape_provenance_call",
  "resolve_named_provenance_call",
  "resolve_constrained_interface_provenance_call",
  "resolve_receiver_shape_provenance_call",
  "call_resolution_record_bindings",
  "call_resolution_record_param_facts",
  "call_resolution_param_type_text",
  "resolved_call_param_type_text",
  "apply_provenance_storage_effect",
  "collect_effect_target_places",
  "collect_assignment_target_places",
  "expr_static_index_segment",
  "origin_path_is_definitely_within",
  "update_borrow_assignment",
  "assignment_provenance_snapshot_clear",
  "provenance_scope_snapshot_restore_union",
];

for (const name of requiredFunctions) {
  if (!functionNames.has(name)) fail(`checker provenance foundation: missing function '${name}'`);
}

for (const kind of [
  "Z_CALL_FUNCTION",
  "Z_CALL_STDLIB",
  "Z_CALL_SHAPE_NAMESPACE",
  "Z_CALL_RECEIVER",
  "Z_CALL_CONSTRAINED_INTERFACE",
  "Z_CALL_CONCRETE_CONSTRAINED_SHAPE",
  "Z_CALL_CHOICE_CONSTRUCTOR",
]) {
  assertIncludes("shared call resolver", callResolve, kind);
  assertIncludes("checker provenance call resolver", checker, kind);
}

for (const needle of [
  "ZCallArgument",
  "ZCallBinding",
  "z_call_resolution_add_arg",
  "z_call_resolution_param_type",
  "z_call_resolution_add_binding",
  "z_call_resolution_binding_type",
]) {
  assertIncludes("shared call argument facts", callResolve, needle);
}

const callResultBody = sliceBetween(checker, "static bool call_result_value_provenance", "static bool expr_reference_provenance");
assertIncludes("call result provenance", callResultBody, "resolve_provenance_call");
assertIncludes("call result provenance", callResultBody, "function_return_value_provenance");
assertIncludes("call result provenance", callResultBody, "instantiate_call_provenance_entry");
assertIncludes("call result provenance", callResultBody, "resolved_call_param_type_text");

const shapeMethodProvenanceBody = sliceBetween(checker, "static bool finish_shape_method_provenance_call", "static bool resolve_shape_namespace_provenance_call");
assertIncludes("shape method provenance facts", shapeMethodProvenanceBody, "build_shape_method_bindings");
assertIncludes("shape method provenance facts", shapeMethodProvenanceBody, "provenance_context_substitute_bindings");
assertIncludes("shape method provenance facts", shapeMethodProvenanceBody, "call_resolution_record_bindings");
assertIncludes("shape method provenance facts", shapeMethodProvenanceBody, "call_resolution_record_param_facts");

const shapeNamespaceProvenanceBody = sliceBetween(checker, "static bool resolve_shape_namespace_provenance_call", "static bool resolve_concrete_constrained_shape_provenance_call");
assertIncludes("shape namespace provenance resolver", shapeNamespaceProvenanceBody, "resolve_shape_namespace_call");
assertIncludes("shape namespace provenance resolver", shapeNamespaceProvenanceBody, "finish_shape_method_provenance_call");

const concreteConstrainedShapeProvenanceBody = sliceBetween(checker, "static bool resolve_concrete_constrained_shape_provenance_call", "static bool resolve_named_provenance_call");
assertIncludes("concrete constrained-shape provenance resolver", concreteConstrainedShapeProvenanceBody, "resolve_concrete_constrained_shape_call");
assertIncludes("concrete constrained-shape provenance resolver", concreteConstrainedShapeProvenanceBody, "finish_shape_method_provenance_call");

const namedProvenanceBody = sliceBetween(checker, "static bool resolve_named_provenance_call", "static bool resolve_constrained_interface_provenance_call");
assertIncludes("named provenance resolver", namedProvenanceBody, "resolve_named_function_call");
assertIncludes("named provenance resolver", namedProvenanceBody, "generic_call_bindings_from_checked_call");
assertIncludes("named provenance resolver", namedProvenanceBody, "provenance_context_substitute_bindings");
assertIncludes("named provenance resolver", namedProvenanceBody, "call_resolution_record_bindings");
assertIncludes("named provenance resolver", namedProvenanceBody, "call_resolution_record_param_facts");

const constrainedInterfaceProvenanceBody = sliceBetween(checker, "static bool resolve_constrained_interface_provenance_call", "static bool resolve_receiver_shape_provenance_call");
assertIncludes("constrained interface provenance resolver", constrainedInterfaceProvenanceBody, "resolve_constrained_interface_call");
assertIncludes("constrained interface provenance resolver", constrainedInterfaceProvenanceBody, "build_constrained_interface_method_bindings");
assertIncludes("constrained interface provenance resolver", constrainedInterfaceProvenanceBody, "call_resolution_record_bindings");
assertIncludes("constrained interface provenance resolver", constrainedInterfaceProvenanceBody, "call_resolution_record_param_facts");

const receiverProvenanceBody = sliceBetween(checker, "static bool resolve_receiver_shape_provenance_call", "static bool resolve_provenance_call");
assertIncludes("receiver provenance resolver", receiverProvenanceBody, "resolve_receiver_shape_call");
assertIncludes("receiver provenance resolver", receiverProvenanceBody, "build_receiver_shape_method_bindings");
assertIncludes("receiver provenance resolver", receiverProvenanceBody, "call_resolution_record_bindings");
assertIncludes("receiver provenance resolver", receiverProvenanceBody, "call_resolution_record_param_facts");

const provenanceCallBody = sliceBetween(checker, "static bool resolve_provenance_call", "static bool function_return_value_provenance");
assertIncludes("provenance call dispatcher", provenanceCallBody, "resolve_named_provenance_call");
assertIncludes("provenance call dispatcher", provenanceCallBody, "resolve_shape_namespace_provenance_call");
assertIncludes("provenance call dispatcher", provenanceCallBody, "resolve_concrete_constrained_shape_provenance_call");
assertIncludes("provenance call dispatcher", provenanceCallBody, "resolve_constrained_interface_provenance_call");
assertIncludes("provenance call dispatcher", provenanceCallBody, "resolve_receiver_shape_provenance_call");
assertBefore("provenance call resolver order", provenanceCallBody, "resolve_concrete_constrained_shape_provenance_call", "resolve_constrained_interface_provenance_call");

const namedCallBody = sliceBetween(checker, "static bool build_named_call_bindings_expected", "static bool check_stdlib_table_arg_range_expected");
assertIncludes("named call checking argument facts", namedCallBody, "resolve_named_function_call");
assertIncludes("named call checking argument facts", namedCallBody, "build_named_call_bindings_expected");
assertIncludes("named call checking argument facts", namedCallBody, "check_named_call_args_expected");
assertIncludes("named call checking argument facts", namedCallBody, "check_call_resolution_args_expected");
assertIncludes("named call checking argument facts", namedCallBody, "check_named_call_fallibility_expected");
assertIncludes("named call checking argument facts", namedCallBody, "prepare_named_call_return_and_storage_expected");
assertIncludes("named call checking argument facts", namedCallBody, "finish_named_call_return_expected");
assertIncludes("named call checking argument facts", namedCallBody, "call_resolution_record_param_facts");
assertIncludes("named call checking argument facts", namedCallBody, "call_resolution_param_type_text");
assertIncludes("named call checking storage effects", namedCallBody, "apply_checked_call_storage_effects");

const stdlibTableCallBody = sliceBetween(checker, "static bool check_stdlib_table_arg_range_expected", "static bool check_stdlib_allocator_arg");
assertIncludes("stdlib table call checking argument facts", stdlibTableCallBody, "z_call_resolution_add_arg");
assertIncludes("stdlib table call checking argument facts", stdlibTableCallBody, "call_resolution_param_type_text");
assertIncludes("stdlib table call checking argument facts", stdlibTableCallBody, "std_call_arg_type");

const stdlibTableDispatchBody = sliceBetween(checker, "static bool check_stdlib_table_call_expected", "static bool check_stdlib_known_call_expected");
assertIncludes("stdlib table call checking argument facts", stdlibTableDispatchBody, "check_stdlib_table_arg_range_expected");

const stdlibKnownCallBody = sliceBetween(checker, "static bool check_stdlib_known_call_expected", "static bool check_stdlib_call_expected");
assertIncludes("stdlib known call checking argument facts", stdlibKnownCallBody, "ZCallResolution *resolution");
assertIncludes("stdlib known call checking argument facts", stdlibKnownCallBody, "check_stdlib_mem_get_call_expected");
assertIncludes("stdlib known call checking argument facts", stdlibKnownCallBody, "check_stdlib_table_call_expected");

const choiceCallBody = sliceBetween(checker, "static bool check_choice_constructor_call_expected", "static bool check_shape_namespace_call_expected");
assertIncludes("choice call checking argument facts", choiceCallBody, "resolve_choice_constructor_call");
assertIncludes("choice call checking argument facts", choiceCallBody, "z_call_resolution_add_arg(&choice_resolution");
assertIncludes("choice call checking argument facts", choiceCallBody, "call_resolution_param_type_text(&choice_resolution");

const shapeNamespaceCallBody = sliceBetween(checker, "static bool check_shape_namespace_call_expected", "static bool receiver_member_call_should_resolve");
assertIncludes("shape namespace call checking argument facts", shapeNamespaceCallBody, "call_resolution_record_param_facts");
assertIncludes("shape namespace call checking argument facts", shapeNamespaceCallBody, "check_call_resolution_args_expected");
assertIncludes("shape namespace call checking storage effects", shapeNamespaceCallBody, "apply_checked_call_storage_effects");

const receiverCallBody = sliceBetween(checker, "static bool check_receiver_method_receiver_access", "static bool check_constrained_interface_call_expected");
assertIncludes("receiver call checking argument facts", receiverCallBody, "resolve_receiver_shape_call");
assertIncludes("receiver call checking argument facts", receiverCallBody, "call_resolution_record_param_facts");
assertIncludes("receiver call checking argument facts", receiverCallBody, "check_call_resolution_args_expected");
assertIncludes("receiver call checking storage effects", receiverCallBody, "apply_checked_call_storage_effects");

const constrainedInterfaceCallBody = sliceBetween(checker, "static bool check_constrained_interface_call_expected", "static bool check_world_stream_write_call_expected");
assertIncludes("constrained interface call checking argument facts", constrainedInterfaceCallBody, "call_resolution_record_param_facts");
assertIncludes("constrained interface call checking argument facts", constrainedInterfaceCallBody, "check_call_resolution_args_expected");
assertIncludes("constrained interface call checking storage effects", constrainedInterfaceCallBody, "apply_checked_call_storage_effects");

const callExprBody = sliceBetween(checker, "static bool check_call_expr_expected", "static bool check_expr_expected");
assertIncludes("call expression checking dispatch", callExprBody, "check_choice_constructor_call_expected");
assertIncludes("call expression checking dispatch", callExprBody, "check_shape_namespace_call_expected");
assertIncludes("call expression checking dispatch", callExprBody, "check_receiver_shape_call_expected");
assertIncludes("call expression checking dispatch", callExprBody, "check_constrained_interface_call_expected");
assertIncludes("call expression checking dispatch", callExprBody, "check_named_function_call_expected");
assertIncludes("call expression checking dispatch", callExprBody, "check_stdlib_call_expected");

const stdlibCallBody = sliceBetween(checker, "static bool check_stdlib_call_expected", "static bool check_choice_constructor_call_expected");
assertIncludes("stdlib call checking dispatch", stdlibCallBody, "resolve_stdlib_call");
assertIncludes("stdlib call checking dispatch", stdlibCallBody, "check_stdlib_known_call_expected");
assertIncludes("stdlib call checking dispatch", stdlibCallBody, "z_call_resolution_free");

const callCalleeBody = sliceBetween(checker, "static bool check_call_callee", "static bool build_named_call_bindings_expected");
assertIncludes("call callee precheck", callCalleeBody, "member_call_skips_callee_expr_check");
assertNotIncludes("call callee precheck", callCalleeBody, "resolve_shape_namespace_call");
assertNotIncludes("call callee precheck", callCalleeBody, "resolve_constrained_interface_call");
assertNotIncludes("call callee precheck", callCalleeBody, "resolve_stdlib_call");

const checkCallBody = sliceBetween(
  checker,
  "static bool check_expr_expected(CheckContext *ctx, const Program *program, const Expr *expr, Scope *scope, ZDiag *diag, const char *expected) {",
  "case EXPR_CAST:"
);
assertIncludes("call checking dispatch", checkCallBody, "check_call_expr_expected");

const checkedCallBody = sliceBetween(checker, "static bool apply_checked_call_storage_effects", "static bool apply_resolved_call_storage_effects");
assertIncludes("checked call storage effects", checkedCallBody, "resolve_provenance_call");
assertIncludes("checked call storage effects", checkedCallBody, "apply_provenance_call_storage_effects");

const summaryBody = sliceBetween(checker, "static bool function_provenance_summary", "static bool function_return_value_provenance");
assertIncludes("function provenance summary", summaryBody, "collect_return_value_provenance_from_stmt_vec");
assertIncludes("function provenance summary", summaryBody, "body_complete");
assertIncludes("function provenance summary", summaryBody, "seed_param_storage_value_provenance");
assertIncludes("function provenance summary", summaryBody, "provenance_storage_effect_vec_add");
assertIncludes("function provenance summary", summaryBody, "return summary->return_complete && summary->effect_complete");
assertIncludes("choice type provenance", checker, "const Choice *choice = find_choice(program, type)");
assertIncludes("choice constructor provenance", checker, "choice_constructor_value_provenance(ctx, program, expr, scope, origins)");
const choiceConstructorBody = sliceBetween(checker, "static bool choice_constructor_value_provenance", "static void register_match_payload_binding_provenance");
assertIncludes("choice constructor provenance", choiceConstructorBody, "resolve_choice_constructor_call");
assertIncludes("choice constructor provenance", choiceConstructorBody, "z_call_resolution_add_arg");
assertIncludes("choice match payload provenance", checker, "register_match_payload_binding_provenance(ctx, program, stmt->expr, scope, &arm_scope, arm->payload_name, arm->case_name)");

const storageSummaryBody = sliceBetween(checker, "static bool function_storage_effect_summary", "static bool apply_provenance_storage_effect");
assertIncludes("storage-effect summary", storageSummaryBody, "function_provenance_summary");

const returnSummaryBody = sliceBetween(checker, "static bool function_return_value_provenance", "static const Expr *call_actual_for_param");
assertIncludes("return summary", returnSummaryBody, "function_provenance_summary");

const expressionEffectBody = sliceBetween(checker, "static bool apply_expr_call_storage_effects", "static bool check_assignment_not_borrowed");
assertIncludes("expression storage effects", expressionEffectBody, "if (expr->kind == EXPR_CALL) return apply_resolved_call_storage_effects");
assertIncludes("expression storage effects", expressionEffectBody, "provenance_scope_snapshot_restore_optional_branch");
assertIncludes("array literal provenance", checker, "value_provenance_add_all_with_prefix(origins, &item_origins, element_path)");
assertIncludes("assignment path clearing", checker, "origin_path_is_definitely_within(entry->value_path, path)");

const checkExprExpectedBody = sliceBetween(
  checker,
  "static bool check_expr_expected(CheckContext *ctx, const Program *program, const Expr *expr, Scope *scope, ZDiag *diag, const char *expected) {",
  "static bool check_expr(CheckContext *ctx, const Program *program, const Expr *expr, Scope *scope, ZDiag *diag) {"
);
const callCase = sliceBetween(checkExprExpectedBody, "case EXPR_CALL:", "case EXPR_CAST:");
assertIncludes("call checking dispatcher", callCase, "check_call_expr_expected");
const callCaseStorageApplications = (callCase.match(/apply_checked_call_storage_effects\(ctx, program, expr, scope, diag\)/g) ?? []).length;
const namedCallStorageApplications = (namedCallBody.match(/apply_checked_call_storage_effects\(ctx, program, expr, scope, diag\)/g) ?? []).length;
const shapeNamespaceCallStorageApplications = (shapeNamespaceCallBody.match(/apply_checked_call_storage_effects\(ctx, program, expr, scope, diag\)/g) ?? []).length;
const receiverCallStorageApplications = (receiverCallBody.match(/apply_checked_call_storage_effects\(ctx, program, expr, scope, diag\)/g) ?? []).length;
const constrainedInterfaceCallStorageApplications = (constrainedInterfaceCallBody.match(/apply_checked_call_storage_effects\(ctx, program, expr, scope, diag\)/g) ?? []).length;
const totalCallStorageApplications =
  callCaseStorageApplications +
  namedCallStorageApplications +
  shapeNamespaceCallStorageApplications +
  receiverCallStorageApplications +
  constrainedInterfaceCallStorageApplications;
if (totalCallStorageApplications < 4) {
  fail(`EXPR_CALL provenance: expected storage-effect application for all user call forms, found ${totalCallStorageApplications}`);
}

const lines = checker.split("\n");
const currentFunction = currentFunctionByLine(checker);
const mutationAllowlist = new Map([
  ["scope_set_value_provenance(", new Set(["scope_set_value_provenance", "register_borrow_binding", "seed_param_storage_value_provenance", "register_match_payload_binding_provenance"])],
  ["scope_set_value_provenance_path_in_scope(", new Set(["scope_set_value_provenance_path_in_scope", "update_borrow_assignment", "assignment_provenance_snapshot_clear", "assignment_provenance_snapshot_restore", "apply_provenance_storage_effect"])],
  ["scope_borrow_counts_for_place(", new Set(["scope_borrow_counts_for_place", "check_borrow_conflict_at", "check_read_not_mutably_borrowed", "check_assignment_not_borrowed"])],
]);

for (let index = 0; index < lines.length; index++) {
  const line = lines[index];
  if (line.trimStart().startsWith("static ")) continue;
  for (const [needle, allowed] of mutationAllowlist) {
    if (!line.includes(needle)) continue;
    const owner = currentFunction[index];
    if (!allowed.has(owner)) {
      fail(`checker provenance bypass: '${needle}' at ${checkerPath}:${index + 1} is in '${owner}', not ${[...allowed].join(", ")}`);
    }
  }
}

assertMatches(
  "callee-local storage rejection",
  checker,
  /cannot store a reference to a callee-local binding through a mutable parameter/
);
assertMatches(
  "incomplete mutref summary rejection",
  checker,
  /cannot verify provenance effects for mutable parameter call/
);
assertMatches(
  "shorter-lived storage rejection",
  checker,
  /cannot assign a reference to a shorter-lived binding/
);

if (failures.length > 0) {
  console.error("provenance guardrails failed:");
  for (const failure of failures) console.error(`- ${failure}`);
  process.exit(1);
}

console.log(`provenance guardrails ok (${rows.length} surfaces)`);
