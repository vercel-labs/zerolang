import { readdir, readFile } from "node:fs/promises";

const LARGE_FUNCTION_REPORT_THRESHOLD = 80;
const NEW_LARGE_FUNCTION_LIMIT = 120;
const STRCMP_CALL_PATTERN = /\bstrcmp\s*\(/g;

const sourceFileDirs = [
  "native/zero-c/include",
  "native/zero-c/src",
];

type CScanState = {
  blockComment: boolean;
  quote: "\"" | "'" | null;
};

const fileBudgets = {
  "native/zero-c/include/zero.h": { maxLines: 909, maxStrcmpCalls: 0 },
  "native/zero-c/include/zero_runtime.h": { maxLines: 100, maxStrcmpCalls: 0 },
  "native/zero-c/src/checker.c": { maxLines: 9395, maxStrcmpCalls: 403 },
  "native/zero-c/src/main.c": { maxLines: 9979, maxStrcmpCalls: 456 },
  "native/zero-c/src/ir.c": { maxLines: 3700, maxStrcmpCalls: 224 },
  "native/zero-c/src/row_syntax.c": { maxLines: 2150, maxStrcmpCalls: 11 },
  "native/zero-c/src/ast.c": { maxLines: 250, maxStrcmpCalls: 0 },
  "native/zero-c/src/buildability.c": { maxLines: 260, maxStrcmpCalls: 2 },
  "native/zero-c/src/buildability.h": { maxLines: 20, maxStrcmpCalls: 0 },
  "native/zero-c/src/buildability_internal.h": { maxLines: 40, maxStrcmpCalls: 0 },
  "native/zero-c/src/buildability_context.c": { maxLines: 180, maxStrcmpCalls: 1 },
  "native/zero-c/src/buildability_targets.c": { maxLines: 155, maxStrcmpCalls: 0 },
  "native/zero-c/src/call_resolve.c": { maxLines: 200, maxStrcmpCalls: 2 },
  "native/zero-c/src/call_resolve.h": { maxLines: 100, maxStrcmpCalls: 0 },
  "native/zero-c/src/coff_format.c": { maxLines: 370, maxStrcmpCalls: 0 },
  "native/zero-c/src/coff_format.h": { maxLines: 100, maxStrcmpCalls: 0 },
  "native/zero-c/src/coff_emit_state.c": { maxLines: 150, maxStrcmpCalls: 0 },
  "native/zero-c/src/coff_emit_state.h": { maxLines: 70, maxStrcmpCalls: 0 },
  "native/zero-c/src/elf_format.c": { maxLines: 220, maxStrcmpCalls: 0 },
  "native/zero-c/src/elf_format.h": { maxLines: 60, maxStrcmpCalls: 0 },
  "native/zero-c/src/elf_emit_state.c": { maxLines: 160, maxStrcmpCalls: 0 },
  "native/zero-c/src/elf_emit_state.h": { maxLines: 90, maxStrcmpCalls: 0 },
  "native/zero-c/src/macho_format.c": { maxLines: 470, maxStrcmpCalls: 0 },
  "native/zero-c/src/macho_format.h": { maxLines: 90, maxStrcmpCalls: 0 },
  "native/zero-c/src/aarch64_emit.c": { maxLines: 320, maxStrcmpCalls: 0 },
  "native/zero-c/src/aarch64_emit.h": { maxLines: 80, maxStrcmpCalls: 0 },
  "native/zero-c/src/emit_macho64.c": { maxLines: 1400, maxStrcmpCalls: 2 },
  "native/zero-c/src/macho_emit_state.c": { maxLines: 210, maxStrcmpCalls: 0 },
  "native/zero-c/src/macho_emit_state.h": { maxLines: 90, maxStrcmpCalls: 0 },
  "native/zero-c/src/emit_elf64.c": { maxLines: 2075, maxStrcmpCalls: 3 },
  "native/zero-c/src/emit_elf_aarch64.c": { maxLines: 205, maxStrcmpCalls: 1 },
  "native/zero-c/src/emit_coff.c": { maxLines: 905, maxStrcmpCalls: 1 },
  "native/zero-c/src/fs.c": { maxLines: 1250, maxStrcmpCalls: 32 },
  "native/zero-c/src/mir_verify.c": { maxLines: 1300, maxStrcmpCalls: 0 },
  "native/zero-c/src/mir_verify.h": { maxLines: 50, maxStrcmpCalls: 0 },
  "native/zero-c/src/specialize.c": { maxLines: 150, maxStrcmpCalls: 2 },
  "native/zero-c/src/specialize.h": { maxLines: 50, maxStrcmpCalls: 0 },
  "native/zero-c/src/std_sig.c": { maxLines: 180, maxStrcmpCalls: 2 },
  "native/zero-c/src/std_sig.h": { maxLines: 40, maxStrcmpCalls: 0 },
  "native/zero-c/src/target_backend.c": { maxLines: 200, maxStrcmpCalls: 19 },
  "native/zero-c/src/target.c": { maxLines: 465, maxStrcmpCalls: 15 },
  "native/zero-c/src/type_core.c": { maxLines: 900, maxStrcmpCalls: 8 },
  "native/zero-c/src/type_core.h": { maxLines: 150, maxStrcmpCalls: 0 },
  "native/zero-c/src/unify.c": { maxLines: 500, maxStrcmpCalls: 14 },
  "native/zero-c/src/unify.h": { maxLines: 75, maxStrcmpCalls: 0 },
  "native/zero-c/src/x64_emit.c": { maxLines: 765, maxStrcmpCalls: 0 },
  "native/zero-c/src/x64_emit.h": { maxLines: 112, maxStrcmpCalls: 0 },
};

const knownLargeFunctionLimits = new Map([
  ["native/zero-c/src/ir.c|static bool ir_lower_expr(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *expr, IrValue **out) {", 1484],
  ["native/zero-c/src/checker.c|static bool check_expr_expected(CheckContext *ctx, const Program *program, const Expr *expr, Scope *scope, ZDiag *diag, const char *expected) {", 1170],
  ["native/zero-c/src/emit_elf64.c|static bool elf_emit_value(ZBuf *code, const IrFunction *fun, const IrValue *value, ElfEmitContext *ctx, ZDiag *diag) {", 770],
  ["native/zero-c/src/main.c|int main(int argc, char **argv) {", 924],
  ["native/zero-c/src/emit_macho64.c|bool z_emit_macho64_object_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {", 157],
  ["native/zero-c/src/emit_elf64.c|bool z_emit_elf64_object_from_ir(const IrProgram *ir, ZBuf *out, ZDiag *diag) {", 125],
  ["native/zero-c/src/main.c|static void append_graph_json(ZBuf *buf, SourceInput *input, Program *program, const ZTargetInfo *target, const Command *command) {", 374],
  ["native/zero-c/src/emit_elf64.c|static bool elf_emit_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, ElfEmitContext *ctx, ZDiag *diag) {", 285],
  ["native/zero-c/src/emit_macho64.c|static bool macho_emit_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {", 275],
  ["native/zero-c/src/checker.c|static bool check_stmt(CheckContext *ctx, const Program *program, const Function *fun, const Stmt *stmt, Scope *scope, ZDiag *diag, int loop_depth) {", 259],
  ["native/zero-c/src/checker.c|bool z_check_program(const Program *program, ZDiag *diag) {", 213],
  ["native/zero-c/src/checker.c|static const char *expr_type(CheckContext *ctx, const Program *program, const Expr *expr, Scope *scope) {", 205],
  ["native/zero-c/src/emit_macho64.c|static bool macho_emit_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, bool restore_process_args, MachOEmitContext *ctx, ZDiag *diag) {", 193],
  ["native/zero-c/src/checker.c|static bool collect_return_value_provenance_from_stmt_vec(CheckContext *ctx, const Program *program, const Function *fun, const StmtVec *body, Scope *scope, GenericBinding *bindings, size_t binding_len, ValueProvenance *out, bool *may_return, bool *complete) {", 192],
  ["native/zero-c/src/emit_coff.c|static bool coff_emit_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {", 155],
  ["native/zero-c/src/row_syntax.c|ZRowTokenVec z_row_tokenize(const char *source, ZDiag *diag) {", 177],
  ["native/zero-c/src/ir.c|static bool ir_lower_stmt_to_vec(const Program *program, IrProgram *ir, IrFunction *mir_fun, const Stmt *stmt, IrInstr **out_items, size_t *out_len, size_t *out_cap, bool *saw_return) {", 172],
  ["native/zero-c/src/emit_coff.c|static bool coff_emit_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {", 155],
  ["native/zero-c/src/checker.c|static bool expr_reference_provenance(CheckContext *ctx, const Program *program, const Expr *expr, Scope *scope, ValueProvenance *origins) {", 152],
  ["native/zero-c/src/main.c|static int run_tests_direct(const Command *command, const SourceInput *input, const Program *program, const ZTargetInfo *target) {", 151],
  ["native/zero-c/src/emit_elf64.c|static bool elf_emit_read_all_or_raise_to_local(ZBuf *text, const IrFunction *fun, const IrInstr *instr, ElfEmitContext *ctx, ZDiag *diag) {", 145],
  ["native/zero-c/src/ast.c|void z_free_program(Program *program) {", 143],
  ["native/zero-c/src/checker.c|static const char *std_call_arg_type(const char *name, size_t index) {", 139],
  ["native/zero-c/src/mir_verify.c|static bool mir_verify_direct_value_kind_contract(IrProgram *ir, const IrFunction *fun, const MirVerifierState *state, const IrValue *value, MirHelperRequirements *requirements) {", 134],
  ["native/zero-c/src/row_syntax.c|static Stmt *row_parse_statement(const ZRowTokenVec *tokens, const ZRowTree *tree, size_t row_index, ZDiag *diag) {", 132],
  ["native/zero-c/src/row_syntax.c|Program z_parse_row(const ZRowTokenVec *tokens, const ZRowTree *tree, ZDiag *diag) {", 130],
  ["native/zero-c/src/ir.c|static bool ir_lower_byte_view(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *expr, IrValue **out) {", 124],
  ["native/zero-c/src/main.c|static bool test_eval_expr(const Program *program, TestEnv *env, const Expr *expr, TestValue *out, TestRunFailure *failure) {", 124],
  ["native/zero-c/src/row_syntax.c|bool z_row_parse_layout(const ZRowTokenVec *tokens, ZRowTree *tree, ZDiag *diag) {", 123],
]);

const knownReturnTypeDivergences = new Map([
  ["std.mem.get", {
    helperReturnType: "Maybe<T>",
    checkerReturnType: "Unknown",
    reason: "checker resolves the element-specific Maybe<T> return in a dedicated std.mem.get path",
  }],
]);

const allowedHelpersWithSpecialArgTypeChecks = [
  "std.mem.eqlBytes",
  "std.mem.len",
];

async function nativeSourceFiles() {
  const groups = await Promise.all(sourceFileDirs.map(async (dir) => {
    const entries = await readdir(dir, { withFileTypes: true });
    return entries
      .filter((entry) => entry.isFile() && /\.[ch]$/.test(entry.name))
      .map((entry) => `${dir}/${entry.name}`);
  }));
  return groups.flat().sort((a, b) => a.localeCompare(b));
}

function countMatches(text, pattern) {
  return [...text.matchAll(pattern)].length;
}

function lineCount(text) {
  if (text.length === 0) return 0;
  return text.endsWith("\n") ? text.split("\n").length - 1 : text.split("\n").length;
}

function createCScanState(): CScanState {
  return { blockComment: false, quote: null };
}

function cCodeLine(line: string, state: CScanState): string {
  let out = "";
  for (let index = 0; index < line.length; index++) {
    const ch = line[index];
    const next = line[index + 1];
    if (state.blockComment) {
      if (ch === "*" && next === "/") {
        out += "  ";
        index++;
        state.blockComment = false;
      } else {
        out += " ";
      }
      continue;
    }
    if (state.quote) {
      if (ch === "\\" && index + 1 < line.length) {
        out += "  ";
        index++;
        continue;
      }
      out += " ";
      if (ch === state.quote) state.quote = null;
      continue;
    }
    if (ch === "/" && next === "*") {
      out += "  ";
      index++;
      state.blockComment = true;
      continue;
    }
    if (ch === "/" && next === "/") {
      out += " ".repeat(line.length - index);
      break;
    }
    if (ch === "\"" || ch === "'") {
      out += " ";
      state.quote = ch;
      continue;
    }
    out += ch;
  }
  return out;
}

function cCodeText(text: string): string {
  const state = createCScanState();
  return text.split("\n").map((line) => cCodeLine(line, state)).join("\n");
}

function cTextWithoutComments(text: string): string {
  let out = "";
  let blockComment = false;
  let quote: "\"" | "'" | null = null;
  for (let index = 0; index < text.length; index++) {
    const ch = text[index];
    const next = text[index + 1];
    if (blockComment) {
      if (ch === "*" && next === "/") {
        out += "  ";
        index++;
        blockComment = false;
      } else {
        out += ch === "\n" ? "\n" : " ";
      }
      continue;
    }
    if (quote) {
      out += ch;
      if (ch === "\\" && index + 1 < text.length) {
        out += text[index + 1];
        index++;
        continue;
      }
      if (ch === quote) quote = null;
      continue;
    }
    if (ch === "/" && next === "*") {
      out += "  ";
      index++;
      blockComment = true;
      continue;
    }
    if (ch === "/" && next === "/") {
      const newline = text.indexOf("\n", index + 2);
      const end = newline < 0 ? text.length : newline;
      out += " ".repeat(end - index);
      index = end - 1;
      continue;
    }
    if (ch === "\"" || ch === "'") quote = ch;
    out += ch;
  }
  return out;
}

function cCodeChar(text: string, index: number, state: CScanState): { ch: string; index: number } {
  const ch = text[index];
  const next = text[index + 1];
  if (state.blockComment) {
    if (ch === "*" && next === "/") {
      state.blockComment = false;
      return { ch: " ", index: index + 1 };
    }
    return { ch: " ", index };
  }
  if (state.quote) {
    if (ch === "\\" && index + 1 < text.length) return { ch: " ", index: index + 1 };
    if (ch === state.quote) state.quote = null;
    return { ch: " ", index };
  }
  if (ch === "/" && next === "*") {
    state.blockComment = true;
    return { ch: " ", index: index + 1 };
  }
  if (ch === "/" && next === "/") {
    const newline = text.indexOf("\n", index + 2);
    return { ch: " ", index: newline < 0 ? text.length - 1 : newline - 1 };
  }
  if (ch === "\"" || ch === "'") {
    state.quote = ch;
    return { ch: " ", index };
  }
  return { ch, index };
}

function updateBraceDepth(line, depth) {
  for (const ch of line) {
    if (ch === "{") depth++;
    else if (ch === "}") depth--;
  }
  return depth;
}

function largeFunctions(path, text) {
  const lines = text.split("\n");
  const results = [];
  let depth = 0;
  let current = null;
  const cState = createCScanState();
  const functionStart = /^([A-Za-z_][A-Za-z0-9_]*|static)[A-Za-z0-9_ \t*]+[A-Za-z_][A-Za-z0-9_]*\([^;]*\)[ \t]*\{/;
  for (let index = 0; index < lines.length; index++) {
    const line = lines[index];
    const codeLine = cCodeLine(line, cState);
    if (!current && depth === 0 && functionStart.test(codeLine)) {
      current = { path, line: index + 1, signature: line.trim() };
    }
    depth = updateBraceDepth(codeLine, depth);
    if (current && depth === 0) {
      const size = index + 1 - current.line + 1;
      if (size >= LARGE_FUNCTION_REPORT_THRESHOLD) results.push({ ...current, lines: size });
      current = null;
    }
  }
  return results;
}

function namesFromRegex(text, pattern) {
  return [...text.matchAll(pattern)].map((match) => match[1]).sort();
}

function sortedMapKeys(map) {
  return [...map.keys()].sort((a, b) => a.localeCompare(b));
}

function duplicates(items) {
  const counts = new Map();
  for (const item of items) counts.set(item, (counts.get(item) ?? 0) + 1);
  return [...counts.entries()]
    .filter(([, count]) => count > 1)
    .map(([name, count]) => ({ name, count }))
    .sort((a, b) => a.name.localeCompare(b.name));
}

function missingFrom(left, right) {
  const rightSet = new Set(right);
  return [...new Set(left)].filter((item) => !rightSet.has(item)).sort();
}

function largeFunctionKey(item) {
  return `${item.path}|${item.signature}`;
}

function cBlock(text, marker) {
  const markerIndex = text.indexOf(marker);
  if (markerIndex < 0) return "";
  let openIndex = -1;
  let scan = createCScanState();
  for (let index = markerIndex; index < text.length; index++) {
    const code = cCodeChar(text, index, scan);
    index = code.index;
    if (code.ch === "{") {
      openIndex = index;
      break;
    }
  }
  if (openIndex < 0) return "";
  scan = createCScanState();
  let depth = 0;
  for (let index = openIndex; index < text.length; index++) {
    const code = cCodeChar(text, index, scan);
    const ch = code.ch;
    index = code.index;
    if (ch === "{") depth++;
    else if (ch === "}") {
      depth--;
      if (depth === 0) return text.slice(openIndex + 1, index);
    }
  }
  return "";
}

function parseStdHelpers(text) {
  const block = cTextWithoutComments(cBlock(text, "const ZStdHelperInfo z_std_helpers[] ="));
  return [...block.matchAll(/\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*(-?\d+)\s*,/g)]
    .map((match) => ({
      name: match[1],
      returnType: match[2],
      argCount: Number(match[3]),
    }))
    .sort((a, b) => a.name.localeCompare(b.name));
}

function parseStdHttpErrorNames(text) {
  const block = cTextWithoutComments(cBlock(text, "static int std_http_error_code"));
  return namesFromRegex(block, /strcmp\(name,\s+"(std\.[^"]+)"\)\s*==\s*0\)\s*return\s*\d+/g);
}

function checkerReturnTypeUsesStdHttpErrorCode(block: string): boolean {
  return /std_http_error_code\s*\(\s*name\.data\s*\)\s*>=\s*0\s*\)\s*result\s*=\s*"HttpError"/.test(block);
}

function checkerArgCountUsesStdHttpErrorCode(block: string): boolean {
  return /std_http_error_code\s*\(\s*name\s*\)\s*>=\s*0\s*\)\s*return\s*0\b/.test(block);
}

function parseCheckerReturnTypes(text) {
  const map = new Map();
  const names = [];
  const block = cTextWithoutComments(cBlock(text, "static const char *std_call_return_type"));
  for (const match of block.matchAll(/strcmp\(name\.data,\s+"(std\.[^"]+)"\)\s*==\s*0\)\s*result\s*=\s*"([^"]+)"/g)) {
    names.push(match[1]);
    if (!map.has(match[1])) map.set(match[1], match[2]);
  }
  if (checkerReturnTypeUsesStdHttpErrorCode(block)) {
    for (const name of parseStdHttpErrorNames(text)) {
      names.push(name);
      if (!map.has(name)) map.set(name, "HttpError");
    }
  }
  return {
    map,
    duplicates: duplicates(names),
  };
}

function checkerUsesSharedReturnTypes(text) {
  const block = cTextWithoutComments(cBlock(text, "static const char *std_call_return_type"));
  return /\bz_std_helper_find\s*\(/.test(block) && /->return_type\b/.test(block);
}

function parseCheckerArgCounts(text) {
  const map = new Map();
  const names = [];
  const block = cTextWithoutComments(cBlock(text, "static int std_call_arg_count"));
  for (const match of block.matchAll(/strcmp\(name,\s+"(std\.[^"]+)"\)\s*==\s*0\)\s*return\s*(-?\d+)/g)) {
    names.push(match[1]);
    if (!map.has(match[1])) map.set(match[1], Number(match[2]));
  }
  if (checkerArgCountUsesStdHttpErrorCode(block)) {
    for (const name of parseStdHttpErrorNames(text)) {
      names.push(name);
      if (!map.has(name)) map.set(name, 0);
    }
  }
  return {
    map,
    duplicates: duplicates(names),
  };
}

function checkerUsesSharedArgCounts(text) {
  const block = cTextWithoutComments(cBlock(text, "static int std_call_arg_count"));
  return /\bz_std_helper_find\s*\(/.test(block) && /->arg_count\b/.test(block);
}

function parseCheckerArgTypeNames(text) {
  const block = cTextWithoutComments(cBlock(text, "static const char *std_call_arg_type"));
  return namesFromRegex(block, /strcmp\(name,\s+"(std\.[^"]+)"/g);
}

function helperReturnTypeMismatches(helpers, checkerReturnTypes) {
  return helpers
    .filter((helper) => checkerReturnTypes.has(helper.name) && checkerReturnTypes.get(helper.name) !== helper.returnType)
    .map((helper) => ({
      name: helper.name,
      helperReturnType: helper.returnType,
      checkerReturnType: checkerReturnTypes.get(helper.name),
    }))
    .sort((a, b) => a.name.localeCompare(b.name));
}

function helperArgCountMismatches(helpers, checkerArgCounts) {
  return helpers
    .filter((helper) => checkerArgCounts.has(helper.name) && checkerArgCounts.get(helper.name) !== helper.argCount)
    .map((helper) => ({
      name: helper.name,
      helperArgCount: helper.argCount,
      checkerArgCount: checkerArgCounts.get(helper.name),
    }))
    .sort((a, b) => a.name.localeCompare(b.name));
}

function knownReturnTypeDivergenceMatches(mismatch) {
  const known = knownReturnTypeDivergences.get(mismatch.name);
  return known &&
    known.helperReturnType === mismatch.helperReturnType &&
    known.checkerReturnType === mismatch.checkerReturnType;
}

function budgetViolations(files, allLargeFunctions, stdlib, backendFormats) {
  const violations = [];
  for (const path of Object.keys(files).sort()) {
    if (!fileBudgets[path]) {
      violations.push({ kind: "missing-file-budget", path });
    }
  }
  for (const [path, budget] of Object.entries(fileBudgets)) {
    const metrics = files[path];
    if (!metrics) {
      violations.push({ kind: "missing-file-metrics", path });
      continue;
    }
    if (metrics.lines > budget.maxLines) {
      violations.push({
        kind: "file-line-budget",
        path,
        actual: metrics.lines,
        limit: budget.maxLines,
      });
    }
    if (metrics.strcmpCalls > budget.maxStrcmpCalls) {
      violations.push({
        kind: "strcmp-budget",
        path,
        actual: metrics.strcmpCalls,
        limit: budget.maxStrcmpCalls,
      });
    }
  }
  for (const item of allLargeFunctions) {
    const key = largeFunctionKey(item);
    const knownLimit = knownLargeFunctionLimits.get(key);
    if (knownLimit !== undefined) {
      if (item.lines > knownLimit) {
        violations.push({
          kind: "known-large-function-growth",
          path: item.path,
          signature: item.signature,
          actual: item.lines,
          limit: knownLimit,
        });
      }
    } else if (item.lines > NEW_LARGE_FUNCTION_LIMIT) {
      violations.push({
        kind: "new-large-function",
        path: item.path,
        signature: item.signature,
        actual: item.lines,
        limit: NEW_LARGE_FUNCTION_LIMIT,
      });
    }
  }
  if (stdlib.duplicateMainHelpers.length > 0) {
    violations.push({
      kind: "duplicate-stdlib-helper",
      helpers: stdlib.duplicateMainHelpers,
    });
  }
  if (stdlib.duplicateCheckerReturnTypes.length > 0) {
    violations.push({
      kind: "duplicate-checker-stdlib-return-type",
      helpers: stdlib.duplicateCheckerReturnTypes,
    });
  }
  if (stdlib.checkerReturnsMissingFromMainHelpers.length > 0) {
    violations.push({
      kind: "stdlib-checker-return-extra",
      names: stdlib.checkerReturnsMissingFromMainHelpers,
    });
  }
  if (stdlib.mainHelpersMissingFromCheckerReturns.length > 0) {
    violations.push({
      kind: "stdlib-helper-return-missing",
      names: stdlib.mainHelpersMissingFromCheckerReturns,
    });
  }
  const unexpectedReturnTypeMismatches = stdlib.returnTypeMismatches.filter((mismatch) => !knownReturnTypeDivergenceMatches(mismatch));
  if (unexpectedReturnTypeMismatches.length > 0) {
    violations.push({
      kind: "stdlib-helper-return-type-mismatch",
      mismatches: unexpectedReturnTypeMismatches,
    });
  }
  const staleReturnTypeDivergences = [...knownReturnTypeDivergences.keys()]
    .filter((name) => !stdlib.returnTypeMismatches.some((mismatch) => knownReturnTypeDivergenceMatches(mismatch) && mismatch.name === name))
    .sort((a, b) => a.localeCompare(b));
  if (staleReturnTypeDivergences.length > 0) {
    violations.push({
      kind: "stale-stdlib-return-type-divergence-allowlist",
      names: staleReturnTypeDivergences,
    });
  }
  if (stdlib.checkerArgCountsMissingFromMainHelpers.length > 0) {
    violations.push({
      kind: "stdlib-checker-arg-count-extra",
      names: stdlib.checkerArgCountsMissingFromMainHelpers,
    });
  }
  if (stdlib.duplicateCheckerArgCounts.length > 0) {
    violations.push({
      kind: "duplicate-checker-stdlib-arg-count",
      helpers: stdlib.duplicateCheckerArgCounts,
    });
  }
  if (stdlib.mainHelpersMissingFromCheckerArgCounts.length > 0) {
    violations.push({
      kind: "stdlib-helper-arg-count-missing",
      names: stdlib.mainHelpersMissingFromCheckerArgCounts,
    });
  }
  if (stdlib.argCountMismatches.length > 0) {
    violations.push({
      kind: "stdlib-helper-arg-count-mismatch",
      mismatches: stdlib.argCountMismatches,
    });
  }
  if (stdlib.checkerArgTypesMissingFromMainHelpers.length > 0) {
    violations.push({
      kind: "stdlib-checker-arg-type-extra",
      names: stdlib.checkerArgTypesMissingFromMainHelpers,
    });
  }
  const unexpectedArgTypeGaps = missingFrom(
    stdlib.nonzeroArgHelpersMissingFromCheckerArgTypes,
    allowedHelpersWithSpecialArgTypeChecks,
  );
  if (unexpectedArgTypeGaps.length > 0) {
    violations.push({
      kind: "stdlib-helper-arg-type-missing",
      names: unexpectedArgTypeGaps,
    });
  }
  const staleArgTypeAllowlist = missingFrom(
    allowedHelpersWithSpecialArgTypeChecks,
    stdlib.nonzeroArgHelpersMissingFromCheckerArgTypes,
  );
  if (staleArgTypeAllowlist.length > 0) {
    violations.push({
      kind: "stale-stdlib-helper-arg-type-allowlist",
      names: staleArgTypeAllowlist,
    });
  }
  if (!backendFormats.directTarget.ruleMatrix ||
      !backendFormats.directTarget.executableUsesRuleMatrix ||
      !backendFormats.directTarget.descriptorTable ||
      backendFormats.directTarget.executableTargetNameChecks > 0 ||
      backendFormats.directTarget.mainExecutableEmitterStringChecks > 0 ||
      backendFormats.directTarget.mainObjectEmitterStringChecks > 0 ||
      backendFormats.directTarget.mainRuntimeCacheKeyStringChecks > 0 ||
      backendFormats.directTarget.mainDirectBackendDiagStringHelpers > 0 ||
      backendFormats.directTarget.mainBackendFromEmitterCalls > 0 ||
      backendFormats.directTarget.mainDirectPathFromEmitterHelpers > 0 ||
      backendFormats.directTarget.mainObjectFormatSymbolChecks > 0) {
    violations.push({
      kind: "direct-target-backend-matrix",
      directTarget: backendFormats.directTarget,
    });
  }
  if (!backendFormats.elf.sharedWriter ||
      !backendFormats.elf.x86ObjectUsesSharedWriter ||
      !backendFormats.elf.x86ExecutableUsesSharedWriter ||
      !backendFormats.elf.aarch64ObjectUsesSharedWriter ||
      !backendFormats.elf.aarch64ExecutableUsesSharedWriter) {
    violations.push({
      kind: "elf-format-writer-split",
      elf: backendFormats.elf,
    });
  }
  if (backendFormats.elf.archFilesWithLocalSectionWriters.length > 0) {
    violations.push({
      kind: "elf-section-writer-in-architecture-file",
      paths: backendFormats.elf.archFilesWithLocalSectionWriters,
    });
  }
  if (!backendFormats.elf.patchStateModule ||
      !backendFormats.elf.x86UsesPatchStateModule) {
    violations.push({
      kind: "elf-patch-state-split",
      elf: backendFormats.elf,
    });
  }
  if (backendFormats.elf.archFilesWithLocalPatchState.length > 0) {
    violations.push({
      kind: "elf-patch-state-in-architecture-file",
      paths: backendFormats.elf.archFilesWithLocalPatchState,
    });
  }
  if (!backendFormats.coff.sharedWriter ||
      !backendFormats.coff.objectUsesSharedWriter ||
      !backendFormats.coff.executableUsesSharedWriter) {
    violations.push({
      kind: "coff-format-writer-split",
      coff: backendFormats.coff,
    });
  }
  if (backendFormats.coff.archFilesWithLocalContainerWriters.length > 0) {
    violations.push({
      kind: "coff-container-writer-in-architecture-file",
      paths: backendFormats.coff.archFilesWithLocalContainerWriters,
    });
  }
  if (!backendFormats.coff.patchStateModule ||
      !backendFormats.coff.x64UsesPatchStateModule) {
    violations.push({
      kind: "coff-patch-state-split",
      coff: backendFormats.coff,
    });
  }
  if (backendFormats.coff.archFilesWithLocalPatchState.length > 0) {
    violations.push({
      kind: "coff-patch-state-in-architecture-file",
      paths: backendFormats.coff.archFilesWithLocalPatchState,
    });
  }
  if (!backendFormats.macho.sharedWriter ||
      !backendFormats.macho.objectUsesSharedWriter ||
      !backendFormats.macho.executableUsesSharedWriter) {
    violations.push({
      kind: "macho-format-writer-split",
      macho: backendFormats.macho,
    });
  }
  if (backendFormats.macho.archFilesWithLocalContainerWriters.length > 0) {
    violations.push({
      kind: "macho-container-writer-in-architecture-file",
      paths: backendFormats.macho.archFilesWithLocalContainerWriters,
    });
  }
  if (!backendFormats.macho.patchStateModule ||
      !backendFormats.macho.archFileUsesPatchStateModule) {
    violations.push({
      kind: "macho-patch-state-split",
      macho: backendFormats.macho,
    });
  }
  if (backendFormats.macho.archFilesWithLocalPatchState.length > 0) {
    violations.push({
      kind: "macho-patch-state-in-architecture-file",
      paths: backendFormats.macho.archFilesWithLocalPatchState,
    });
  }
  if (!backendFormats.x64.sharedEncodingPrimitives ||
      !backendFormats.x64.elfUsesSharedEncodingPrimitives ||
      !backendFormats.x64.coffUsesSharedEncodingPrimitives) {
    violations.push({
      kind: "x64-encoding-primitives-split",
      x64: backendFormats.x64,
    });
  }
  if (backendFormats.x64.formatFilesWithLocalEncodingPrimitives.length > 0) {
    violations.push({
      kind: "x64-encoding-primitive-in-format-file",
      paths: backendFormats.x64.formatFilesWithLocalEncodingPrimitives,
    });
  }
  if (backendFormats.x64.formatFilesWithRawStackRegisterBytes.length > 0) {
    violations.push({
      kind: "x64-stack-register-byte-in-format-file",
      paths: backendFormats.x64.formatFilesWithRawStackRegisterBytes,
    });
  }
  if (backendFormats.x64.formatFilesWithRawRegisterImmediateBytes.length > 0) {
    violations.push({
      kind: "x64-register-immediate-byte-in-format-file",
      paths: backendFormats.x64.formatFilesWithRawRegisterImmediateBytes,
    });
  }
  if (backendFormats.x64.formatFilesWithRawArithmeticBytes.length > 0) {
    violations.push({
      kind: "x64-arithmetic-byte-in-format-file",
      paths: backendFormats.x64.formatFilesWithRawArithmeticBytes,
    });
  }
  if (backendFormats.x64.formatFilesWithRawCompareTestBytes.length > 0) {
    violations.push({
      kind: "x64-compare-test-byte-in-format-file",
      paths: backendFormats.x64.formatFilesWithRawCompareTestBytes,
    });
  }
  if (backendFormats.x64.formatFilesWithRawIndexedMemoryBytes.length > 0) {
    violations.push({
      kind: "x64-indexed-memory-byte-in-format-file",
      paths: backendFormats.x64.formatFilesWithRawIndexedMemoryBytes,
    });
  }
  if (backendFormats.x64.formatFilesWithRawPointerMemoryBytes.length > 0) {
    violations.push({
      kind: "x64-pointer-memory-byte-in-format-file",
      paths: backendFormats.x64.formatFilesWithRawPointerMemoryBytes,
    });
  }
  if (!backendFormats.aarch64.sharedEncodingPrimitives ||
      !backendFormats.aarch64.elfUsesSharedEncodingPrimitives ||
      !backendFormats.aarch64.machoUsesSharedEncodingPrimitives) {
    violations.push({
      kind: "aarch64-encoding-primitives-split",
      aarch64: backendFormats.aarch64,
    });
  }
  if (backendFormats.aarch64.formatFilesWithLocalEncodingPrimitives.length > 0) {
    violations.push({
      kind: "aarch64-encoding-primitive-in-format-file",
      paths: backendFormats.aarch64.formatFilesWithLocalEncodingPrimitives,
    });
  }
  return violations;
}

const sourceFiles = await nativeSourceFiles();
const texts = new Map();
for (const path of sourceFiles) {
  texts.set(path, await readFile(path, "utf8"));
}

const files = Object.fromEntries([...texts.entries()].map(([path, text]) => [path, {
  lines: lineCount(text),
  strcmpCalls: countMatches(cCodeText(text), STRCMP_CALL_PATTERN),
  unsupportedMarkers: countMatches(text, /Unknown|unsupported|currently|MVP|direct backend/g),
}]));

const checker = texts.get("native/zero-c/src/checker.c") ?? "";
const main = texts.get("native/zero-c/src/main.c") ?? "";
const ir = texts.get("native/zero-c/src/ir.c") ?? "";
const stdSig = texts.get("native/zero-c/src/std_sig.c") ?? "";
const targetBackendRaw = texts.get("native/zero-c/src/target_backend.c") ?? "";
const targetBackendSource = cCodeText(targetBackendRaw);
const directExeBackendBody = cCodeText(cBlock(targetBackendRaw, "ZDirectBackend z_direct_exe_backend"));

const stdHelpers = parseStdHelpers(stdSig);
const checkerReturnTypeInfo = parseCheckerReturnTypes(checker);
const checkerArgCountInfo = parseCheckerArgCounts(checker);
const checkerReturnTypes = checkerReturnTypeInfo.map;
const checkerArgCounts = checkerArgCountInfo.map;
const checkerReturnTypesUseSharedTable = checkerUsesSharedReturnTypes(checker);
const checkerArgCountsUseSharedTable = checkerUsesSharedArgCounts(checker);
if (checkerReturnTypesUseSharedTable) {
  for (const helper of stdHelpers) {
    checkerReturnTypes.set(helper.name, helper.returnType);
  }
  if (checkerReturnTypes.has("std.mem.get")) checkerReturnTypes.set("std.mem.get", "Unknown");
}
if (checkerArgCountsUseSharedTable) {
  for (const helper of stdHelpers) {
    checkerArgCounts.set(helper.name, helper.argCount);
  }
}
const checkerArgTypeNames = parseCheckerArgTypeNames(checker);
const checkerKnownStdNames = namesFromRegex(cTextWithoutComments(checker), /"(std\.[^"]+)"/g);
const checkerReturnNames = sortedMapKeys(checkerReturnTypes);
const checkerArgCountNames = sortedMapKeys(checkerArgCounts);
const mainHelperNames = stdHelpers.map((helper) => helper.name);
const irStdNames = namesFromRegex(ir, /strcmp\(callee_name,\s+"(std\.[^"]+)"/g);
const nonzeroArgHelperNames = stdHelpers
  .filter((helper) => helper.argCount > 0)
  .map((helper) => helper.name);

const allLargeFunctions = [...texts.entries()]
  .flatMap(([path, text]) => largeFunctions(path, text))
  .sort((a, b) => b.lines - a.lines);

const stdlib = {
  checkerReturnCount: new Set(checkerReturnNames).size,
  checkerKnownStdNameCount: new Set(checkerKnownStdNames).size,
  checkerArgCountCount: new Set(checkerArgCountNames).size,
  checkerArgTypeCount: new Set(checkerArgTypeNames).size,
  mainHelperCount: new Set(mainHelperNames).size,
  irDirectStdCallCount: new Set(irStdNames).size,
  duplicateMainHelpers: duplicates(mainHelperNames),
  duplicateCheckerReturnTypes: checkerReturnTypeInfo.duplicates,
  duplicateCheckerArgCounts: checkerArgCountInfo.duplicates,
  returnNamesMissingFromMainHelpers: missingFrom(checkerReturnNames, mainHelperNames),
  checkerReturnsMissingFromMainHelpers: missingFrom(checkerReturnNames, mainHelperNames),
  mainHelpersMissingFromCheckerReturns: missingFrom(mainHelperNames, checkerReturnNames),
  returnTypeMismatches: helperReturnTypeMismatches(stdHelpers, checkerReturnTypes),
  checkerArgCountsMissingFromMainHelpers: missingFrom(checkerArgCountNames, mainHelperNames),
  mainHelpersMissingFromCheckerArgCounts: missingFrom(mainHelperNames, checkerArgCountNames),
  argCountMismatches: helperArgCountMismatches(stdHelpers, checkerArgCounts),
  checkerArgTypesMissingFromMainHelpers: missingFrom(checkerArgTypeNames, mainHelperNames),
  nonzeroArgHelpersMissingFromCheckerArgTypes: missingFrom(nonzeroArgHelperNames, checkerArgTypeNames),
  mainHelpersMissingFromCheckerKnownNames: checkerReturnTypesUseSharedTable && checkerArgCountsUseSharedTable ? [] : missingFrom(mainHelperNames, checkerKnownStdNames),
  sharedSignatureLookup: {
    checkerReturnTypes: checkerReturnTypesUseSharedTable,
    checkerArgCounts: checkerArgCountsUseSharedTable,
  },
};
const elfFormatSource = texts.get("native/zero-c/src/elf_format.c") ?? "";
const elfEmitStateSource = cCodeText(texts.get("native/zero-c/src/elf_emit_state.c") ?? "");
const coffFormatSource = texts.get("native/zero-c/src/coff_format.c") ?? "";
const coffEmitStateSource = cCodeText(texts.get("native/zero-c/src/coff_emit_state.c") ?? "");
const machoFormatSource = texts.get("native/zero-c/src/macho_format.c") ?? "";
const machoEmitStateSource = cCodeText(texts.get("native/zero-c/src/macho_emit_state.c") ?? "");
const aarch64EmitSource = texts.get("native/zero-c/src/aarch64_emit.c") ?? "";
const x64EmitSource = texts.get("native/zero-c/src/x64_emit.c") ?? "";
const elfX64Source = cCodeText(texts.get("native/zero-c/src/emit_elf64.c") ?? "");
const elfAarch64Source = cCodeText(texts.get("native/zero-c/src/emit_elf_aarch64.c") ?? "");
const coffX64Source = cCodeText(texts.get("native/zero-c/src/emit_coff.c") ?? "");
const machoArm64Source = cCodeText(texts.get("native/zero-c/src/emit_macho64.c") ?? "");
const rawX64RegisterImmediateOpcode = /\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0xb[8-9a-f]\s*\)/i;
const rawX64RegisterImmediateC7 = /(?:\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x4[0-9a-f]\s*\)\s*;\s*)?\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0xc7\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0xc[0-7]\s*\)\s*;\s*\bz_x64_append_u32\s*\(/is;
const rawX64RegisterImmediateHelperPrefix = /\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x4[0-9a-f]\s*\)\s*;\s*\bz_x64_emit_mov_eax_u32\s*\(/is;
const hasRawX64RegisterImmediateBytes = (text: string) =>
  rawX64RegisterImmediateOpcode.test(text) ||
  rawX64RegisterImmediateC7.test(text) ||
  rawX64RegisterImmediateHelperPrefix.test(text);
const rawX64ArithmeticRegReg = /(?:\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x4[0-9a-f]\s*\)\s*;\s*)?\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x(?:01|29|31)\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x[c-f][0-9a-f]\s*\)/is;
const rawX64ArithmeticImm8 = /(?:\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x4[0-9a-f]\s*\)\s*;\s*)?\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x83\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x(?:c[0-7]|e[0-7])\s*\)/is;
const rawX64ArithmeticImulImm32 = /(?:\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x4[0-9a-f]\s*\)\s*;\s*)?\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x69\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x[c-f][0-9a-f]\s*\)\s*;\s*\bz_x64_append_u32\s*\(/is;
const rawX64ArithmeticGroupImm32 = /(?:\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x4[0-9a-f]\s*\)\s*;\s*)?\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x81\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x(?:c[0-7]|e[0-7]|e[8-f])\s*\)\s*;\s*\bz_x64_append_u32\s*\(/is;
const rawX64ArithmeticAccumulatorImm32 = /(?:\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x48\s*\)\s*;\s*)?\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x(?:05|2d)\s*\)\s*;\s*\bz_x64_append_u32\s*\(/is;
const rawX64ArithmeticUnary = /(?:\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x4[0-9a-f]\s*\)\s*;\s*)?\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x(?:d1|f7|c1)\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x(?:d[8-f]|e[0-9a-f])\s*\)/is;
const hasRawX64ArithmeticBytes = (text: string) =>
  rawX64ArithmeticRegReg.test(text) ||
  rawX64ArithmeticImm8.test(text) ||
  rawX64ArithmeticImulImm32.test(text) ||
  rawX64ArithmeticGroupImm32.test(text) ||
  rawX64ArithmeticAccumulatorImm32.test(text) ||
  rawX64ArithmeticUnary.test(text);
const rawX64CompareRegReg = /(?:\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x4[0-9a-f]\s*\)\s*;\s*)?\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x39\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x[c-f][0-9a-f]\s*\)/is;
const rawX64CompareImm8 = /(?:\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x4[0-9a-f]\s*\)\s*;\s*)?\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x83\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0xf[8-f]\s*\)/is;
const rawX64TestRegReg = /(?:\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x4[0-9a-f]\s*\)\s*;\s*)?\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x85\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x[c-f][0-9a-f]\s*\)/is;
const rawX64SetccToBool = /\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x0f\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x9[0-9a-f]\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0xc0\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x0f\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0xb6\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0xc0\s*\)/is;
const hasRawX64CompareTestBytes = (text: string) =>
  rawX64CompareRegReg.test(text) ||
  rawX64CompareImm8.test(text) ||
  rawX64TestRegReg.test(text) ||
  rawX64SetccToBool.test(text);
const rawX64SibMemoryModRm = "0x[0-9a-b][4c]";
const rawX64SibCmpImm8ModRm = "0x(?:3c|7c|bc)";
const rawX64IndexedLeaOrLoad = new RegExp(String.raw`(?:\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x4[0-9a-f]\s*\)\s*;\s*)?\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x(?:8b|8d)\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*${rawX64SibMemoryModRm}\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x[0-9a-f]{2}\s*\)`, "is");
const rawX64IndexedCmpImm8 = new RegExp(String.raw`(?:\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x4[0-9a-f]\s*\)\s*;\s*)?\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x80\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*${rawX64SibCmpImm8ModRm}\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x[0-9a-f]{2}\s*\)`, "is");
const hasRawX64IndexedMemoryBytes = (text: string) =>
  rawX64IndexedLeaOrLoad.test(text) ||
  rawX64IndexedCmpImm8.test(text);
const rawX64MemoryModRm = "0x[0-9a-b][0-9a-f]";
const rawX64PointerMemoryReg = new RegExp(String.raw`(?:\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x4[0-9a-f]\s*\)\s*;\s*)?\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x(?:88|89|8a|8b|3a|3b|c6)\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*${rawX64MemoryModRm}\s*\)`, "is");
const rawX64PointerMemoryMovzx = new RegExp(String.raw`(?:\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x4[0-9a-f]\s*\)\s*;\s*)?\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x0f\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0xb[67]\s*\)\s*;\s*\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*${rawX64MemoryModRm}\s*\)`, "is");
const hasRawX64PointerMemoryBytes = (text: string) =>
  rawX64PointerMemoryReg.test(text) ||
  rawX64PointerMemoryMovzx.test(text);
const backendFormats = {
  directTarget: {
    ruleMatrix: /\bdirect_backend_rules\[\]/.test(targetBackendSource),
    executableUsesRuleMatrix: /return\s+direct_backend_for_target\s*\(\s*target\s*,\s*true\s*\)/.test(targetBackendSource),
    descriptorTable: /\bdirect_backend_descriptors\[\]/.test(targetBackendSource),
    executableTargetNameChecks: countMatches(directExeBackendBody, /target\s*->\s*name/g),
    mainExecutableEmitterStringChecks: countMatches(cCodeText(main), /zero-(?:elf64|elf-aarch64|macho64|coff-x64)-exe/g),
    mainObjectEmitterStringChecks: countMatches(cCodeText(main), /"zero-(?:elf64|elf-aarch64|macho64|coff-x64)"/g),
    mainRuntimeCacheKeyStringChecks: countMatches(cCodeText(main), /direct-(?:elf64|macho64)-object-runtime-link/g),
    mainDirectBackendDiagStringHelpers: countMatches(cCodeText(main), /static const char \*target_backend_(?:expected|help)\s*\(/g),
    mainBackendFromEmitterCalls: countMatches(cCodeText(main), /z_direct_backend_from_emitter\s*\(/g),
    mainDirectPathFromEmitterHelpers: countMatches(cCodeText(main), /direct_(?:object_path|linker_flavor)_for_emitter\s*\(/g),
    mainObjectFormatSymbolChecks: countMatches(cCodeText(main), /strcmp\s*\(\s*object_format\s*,\s*"coff"\s*\)/g),
  },
  elf: {
    sharedWriter: /\bz_elf_write_object64\s*\(/.test(elfFormatSource) && /\bz_elf_write_executable64\s*\(/.test(elfFormatSource),
    x86ObjectUsesSharedWriter: /\bz_elf_write_object64\s*\(\s*out\s*,\s*&image\s*\)/.test(elfX64Source),
    x86ExecutableUsesSharedWriter: /\bz_elf_write_executable64\s*\(\s*out\s*,\s*&image\s*\)/.test(elfX64Source),
    aarch64ObjectUsesSharedWriter: /\bz_elf_write_object64\s*\(\s*out\s*,\s*&image\s*\)/.test(elfAarch64Source),
    aarch64ExecutableUsesSharedWriter: /\bz_elf_write_executable64\s*\(\s*out\s*,\s*&image\s*\)/.test(elfAarch64Source),
    archFilesWithLocalSectionWriters: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_elf_aarch64.c", elfAarch64Source],
    ]
      .filter(([, text]) => /\bappend_section_header\s*\(/.test(text))
      .map(([path]) => path),
    patchStateModule: /\bz_elf_record_value_runtime_patch\s*\(/.test(elfEmitStateSource) &&
      /\bz_elf_append_runtime_relocations\s*\(/.test(elfEmitStateSource) &&
      /\bz_elf_patch_rodata_patches\s*\(/.test(elfEmitStateSource),
    x86UsesPatchStateModule: /\bz_elf_record_value_runtime_patch\s*\(/.test(elfX64Source) &&
      /\bz_elf_append_runtime_relocations\s*\(/.test(elfX64Source) &&
      /\bz_elf_has_runtime_patches\s*\(/.test(elfX64Source),
    archFilesWithLocalPatchState: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
    ]
      .filter(([, text]) => /\bElfRuntimePatch\b|(?:\.|->)runtime_[A-Za-z0-9_]+_patch_len\b|(?:\.|->)runtime_[A-Za-z0-9_]+_patches\b|\bstatic\s+bool\s+elf_record_(?:call_patch|rodata_patch|runtime_)\b|\bstatic\s+void\s+elf_patch_(?:call_patches|rodata_patches)\b/.test(text))
      .map(([path]) => path),
  },
  coff: {
    sharedWriter: /\bz_coff_write_object\s*\(/.test(coffFormatSource) && /\bz_coff_write_pe64_executable\s*\(/.test(coffFormatSource),
    objectUsesSharedWriter: /\bz_coff_write_object\s*\(\s*out\s*,\s*&image\s*\)/.test(coffX64Source),
    executableUsesSharedWriter: /\bz_coff_write_pe64_executable\s*\(\s*out\s*,\s*&image\s*\)/.test(coffX64Source),
    archFilesWithLocalContainerWriters: [
      ["native/zero-c/src/emit_coff.c", coffX64Source],
    ]
      .filter(([, text]) => /\bappend_coff_name\s*\(|\bcoff_append_import_table\s*\(|\bPE32\+|\bIMAGE_FILE_MACHINE_AMD64/.test(text))
      .map(([path]) => path),
    patchStateModule: /\bz_coff_record_instr_runtime_patch\s*\(/.test(coffEmitStateSource) &&
      /\bz_coff_append_runtime_relocations\s*\(/.test(coffEmitStateSource) &&
      /\bz_coff_patch_runtime_patches\s*\(/.test(coffEmitStateSource),
    x64UsesPatchStateModule: /\bz_coff_record_instr_runtime_patch\s*\(/.test(coffX64Source) &&
      /\bz_coff_append_runtime_relocations\s*\(/.test(coffX64Source) &&
      /\bz_coff_patch_runtime_patches\s*\(/.test(coffX64Source),
    archFilesWithLocalPatchState: [
      ["native/zero-c/src/emit_coff.c", coffX64Source],
    ]
      .filter(([, text]) => /\bCoff(?:CallPatch|WorldWritePatch)\b|(?:\.|->)world_write_patch_len\b|(?:\.|->)world_write_patches\b|\bstatic\s+bool\s+coff_record_(?:call_patch|rodata_patch|world_write_patch)\b/.test(text))
      .map(([path]) => path),
  },
  macho: {
    sharedWriter: /\bz_macho_write_object64\s*\(/.test(machoFormatSource) && /\bz_macho_write_executable64\s*\(/.test(machoFormatSource),
    objectUsesSharedWriter: /\bz_macho_write_object64\s*\(\s*out\s*,\s*&image\s*\)/.test(machoArm64Source),
    executableUsesSharedWriter: /\bz_macho_write_executable64\s*\(\s*out\s*,\s*&image\s*\)/.test(machoArm64Source),
    archFilesWithLocalContainerWriters: [
      ["native/zero-c/src/emit_macho64.c", machoArm64Source],
    ]
      .filter(([, text]) => /\bappend_fixed\s*\(|\bmacho_append_code_signature\s*\(|\bmacho_sha256_hash\s*\(|\bpatch_bytes\s*\(|0xfeedfacf|0x80000022/.test(text))
      .map(([path]) => path),
    patchStateModule: /\bz_macho_record_value_runtime_patch\s*\(/.test(machoEmitStateSource) &&
      /\bz_macho_record_instr_runtime_patch\s*\(/.test(machoEmitStateSource) &&
      /\bz_macho_append_runtime_relocations\s*\(/.test(machoEmitStateSource),
    archFileUsesPatchStateModule: /\bz_macho_record_value_runtime_patch\s*\(/.test(machoArm64Source) &&
      /\bz_macho_append_runtime_relocations\s*\(/.test(machoArm64Source) &&
      /\bz_macho_has_unsupported_exe_runtime_patches\s*\(/.test(machoArm64Source),
    archFilesWithLocalPatchState: [
      ["native/zero-c/src/emit_macho64.c", machoArm64Source],
    ]
      .filter(([, text]) => /\bMachO(?:WorldWrite|Runtime)[A-Za-z]*Patch\b|(?:\.|->)(?:world_write_patch_len|runtime_[A-Za-z0-9_]+_patch_len|world_write_patches|runtime_[A-Za-z0-9_]+_patches)\b|\bstatic\s+bool\s+macho_record_(?:call_patch|data_patch|world_write|runtime_)\b|\bstatic\s+void\s+macho_append_(?:call_relocations|data_relocations|world_write|runtime_)\b/.test(text))
      .map(([path]) => path),
  },
  x64: {
    sharedEncodingPrimitives: /\bz_x64_append_u8\s*\(/.test(x64EmitSource) &&
      /\bz_x64_append_u32\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_rbp_disp_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_load_rsp_offset_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_store_rsp_offset_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_lea_rsp_offset_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_rsp_offset_u32\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_inc_rsp_offset64\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_add_rax_rsp_offset\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_cmp_rax_rsp_offset\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_cmp_reg_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_cmp_reg_i8\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_reg_from_rax\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_reg_from_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_reg_u32\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_reg_i32\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_reg_u64\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_xor_reg_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_add_reg_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_sub_reg_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_xor_reg_from_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_add_reg_i8\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_and_reg_i8\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_and_reg_u32\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_neg_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_shr_reg_one\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_shl_reg_imm8\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_shr_reg_imm8\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_imul_reg_i32\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_add_rax_u32\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_sub_rax_u32\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_load_reg8_base_index\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_movzx_reg32_base_index_u8\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_store_base_index_reg8\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_cmp_base_index_reg8\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_cmp_base_index_u8\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_load_base_index_scale_disp_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_lea_base_index_scale_disp_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_xor_r8d_r8d\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_jcc32_placeholder\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_prologue\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_epilogue\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_eax_u32\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_ud2\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_syscall\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_push_rax\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_pop_rax\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_rcx_from_rax\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_r9_from_rax\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_rax_from_rcx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_rdx_from_rax\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_rdi_from_rax\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_rsi_from_rax\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_rsi_from_rsp\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_rax_from_rdx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_rax_from_rdi\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_eax_from_ecx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_rax_u64_patchable\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_mov_rax_u64\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_xor_eax_eax\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_xor_ecx_ecx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_xor_rdi_rdi\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_xor_rax_rax\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_inc_ecx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_inc_rcx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_inc_r8\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_dec_r8d\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_add_rax_rcx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_sub_rax_rcx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_imul_rax_rcx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_and_rax_rcx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_or_rax_rcx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_add_rdx_rcx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_shl_rcx_imm8\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_shr_rcx_imm8\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_movzx_reg32_ptr_reg_u8\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_load_reg_ptr_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_store_ptr_reg8_from_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_store_ptr_reg_from_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_cmp_reg_ptr_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_div_rax_rcx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_test_rax_rax\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_test_ecx_ecx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_test_reg_reg\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_cmp_rax_rcx\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_setcc_al_to_bool\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_cmp_rax_rcx_to_bool\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_bool_from_nonnegative_rax\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_call32_placeholder\s*\(/.test(x64EmitSource) &&
      /\bz_x64_emit_call_rip32_placeholder\s*\(/.test(x64EmitSource) &&
      /\bz_x64_patch_rel32\s*\(/.test(x64EmitSource),
    elfUsesSharedEncodingPrimitives: /\bz_x64_append_u8\s*\(/.test(elfX64Source) &&
      /\bz_x64_append_u32\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_rbp_disp_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_load_rsp_offset_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_store_rsp_offset_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_lea_rsp_offset_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_rsp_offset_u32\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_inc_rsp_offset64\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_add_rax_rsp_offset\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_cmp_rax_rsp_offset\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_cmp_reg_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_cmp_reg_i8\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_reg_from_rax\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_reg_from_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_reg_u32\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_reg_i32\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_reg_u64\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_xor_reg_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_add_reg_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_sub_reg_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_xor_reg_from_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_add_reg_i8\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_and_reg_i8\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_and_reg_u32\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_neg_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_shr_reg_one\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_shl_reg_imm8\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_imul_reg_i32\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_add_rax_u32\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_sub_rax_u32\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_load_reg8_base_index\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_movzx_reg32_base_index_u8\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_store_base_index_reg8\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_cmp_base_index_reg8\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_cmp_base_index_u8\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_load_base_index_scale_disp_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_lea_base_index_scale_disp_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_xor_r8d_r8d\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_jcc32_placeholder\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_prologue\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_epilogue\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_eax_u32\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_ud2\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_push_rax\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_pop_rax\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_rcx_from_rax\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_r9_from_rax\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_rax_from_rcx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_rdx_from_rax\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_rdi_from_rax\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_rsi_from_rax\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_rsi_from_rsp\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_rax_from_rdx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_rax_from_rdi\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_eax_from_ecx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_rax_u64_patchable\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_rax_u64\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_xor_eax_eax\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_xor_ecx_ecx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_xor_rdi_rdi\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_xor_rax_rax\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_inc_ecx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_inc_rcx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_inc_r8\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_dec_r8d\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_add_rax_rcx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_sub_rax_rcx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_imul_rax_rcx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_and_rax_rcx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_or_rax_rcx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_add_rdx_rcx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_shr_rcx_imm8\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_movzx_reg32_ptr_reg_u8\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_load_reg_ptr_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_store_ptr_reg8_from_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_store_ptr_reg_from_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_cmp_reg_ptr_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_div_rax_rcx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_test_rax_rax\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_test_ecx_ecx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_test_reg_reg\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_cmp_rax_rcx\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_setcc_al_to_bool\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_cmp_rax_rcx_to_bool\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_bool_from_nonnegative_rax\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_call32_placeholder\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_syscall\s*\(/.test(elfX64Source) &&
      /\bz_x64_patch_rel32\s*\(/.test(elfX64Source),
    coffUsesSharedEncodingPrimitives: /\bz_x64_append_u8\s*\(/.test(coffX64Source) &&
      /\bz_x64_append_u32\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_rbp_disp_reg\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_load_rsp_offset_reg\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_store_rsp_offset_reg\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_lea_rsp_offset_reg\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_mov_rsp_offset_u32\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_jcc32_placeholder\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_prologue\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_epilogue\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_mov_eax_u32\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_ud2\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_push_rax\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_pop_rax\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_mov_rcx_from_rax\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_mov_reg_from_rax\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_mov_reg_from_reg\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_mov_reg_u32\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_add_reg_reg\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_add_reg_i8\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_cmp_reg_i8\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_add_rax_u32\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_mov_eax_from_ecx\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_shl_rcx_imm8\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_movzx_reg32_ptr_reg_u8\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_load_reg_ptr_reg\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_store_ptr_reg8_from_reg\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_store_ptr_reg_from_reg\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_mov_rax_u64_patchable\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_xor_eax_eax\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_add_rax_rcx\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_sub_rax_rcx\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_imul_rax_rcx\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_add_rdx_rcx\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_test_rax_rax\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_cmp_rax_rcx\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_cmp_rax_rcx_to_bool\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_call32_placeholder\s*\(/.test(coffX64Source) &&
      /\bz_x64_emit_call_rip32_placeholder\s*\(/.test(coffX64Source) &&
      /\bz_x64_patch_rel32\s*\(/.test(coffX64Source),
    formatFilesWithLocalEncodingPrimitives: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_coff.c", coffX64Source],
    ]
      .filter(([, text]) => /\bstatic\s+(?:void|size_t)\s+(?:z_x64_|elf_append_u(?:8|32|64)|elf_append_bytes|elf_append_zeros|elf_align|elf_pad_to|append_u8|append_u32le|append_bytes)\b/.test(text))
      .map(([path]) => path),
    formatFilesWithRawStackRegisterBytes: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_coff.c", coffX64Source],
    ]
      .filter(([, text]) => /\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x5[0-9a-f]\s*\)/i.test(text))
      .map(([path]) => path),
    formatFilesWithRawRegisterImmediateBytes: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_coff.c", coffX64Source],
    ]
      .filter(([, text]) => hasRawX64RegisterImmediateBytes(text))
      .map(([path]) => path),
    formatFilesWithRawArithmeticBytes: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_coff.c", coffX64Source],
    ]
      .filter(([, text]) => hasRawX64ArithmeticBytes(text))
      .map(([path]) => path),
    formatFilesWithRawCompareTestBytes: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_coff.c", coffX64Source],
    ]
      .filter(([, text]) => hasRawX64CompareTestBytes(text))
      .map(([path]) => path),
    formatFilesWithRawIndexedMemoryBytes: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_coff.c", coffX64Source],
    ]
      .filter(([, text]) => hasRawX64IndexedMemoryBytes(text))
      .map(([path]) => path),
    formatFilesWithRawPointerMemoryBytes: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_coff.c", coffX64Source],
    ]
      .filter(([, text]) => hasRawX64PointerMemoryBytes(text))
      .map(([path]) => path),
  },
  aarch64: {
    sharedEncodingPrimitives: /\bz_aarch64_emit_movz_w\s*\(/.test(aarch64EmitSource) &&
      /\bz_aarch64_emit_bl_placeholder\s*\(/.test(aarch64EmitSource) &&
      /\bz_aarch64_patch_branch26\s*\(/.test(aarch64EmitSource),
    elfUsesSharedEncodingPrimitives: /\bz_aarch64_emit_literal_return\s*\(/.test(elfAarch64Source) &&
      /\bz_aarch64_emit_bl_placeholder\s*\(/.test(elfAarch64Source) &&
      /\bz_aarch64_patch_branch26\s*\(/.test(elfAarch64Source),
    machoUsesSharedEncodingPrimitives: /\bz_aarch64_emit_movz_w\s*\(/.test(machoArm64Source) &&
      /\bz_aarch64_emit_bl_placeholder\s*\(/.test(machoArm64Source) &&
      /\bz_aarch64_patch_branch26\s*\(/.test(machoArm64Source),
    formatFilesWithLocalEncodingPrimitives: [
      ["native/zero-c/src/emit_elf_aarch64.c", elfAarch64Source],
      ["native/zero-c/src/emit_macho64.c", machoArm64Source],
    ]
      .filter(([, text]) => /\bstatic\s+(?:void|size_t)\s+(?:z_aarch64_|a64_(?:append|emit|patch|pad|align)|macho_emit_(?:add_sp_imm|add_x_sp_imm|nop|movz|mov_[wx]|add_[wx]_imm|sub_w_imm|div_reg|msub_reg|cmp_[wx]|ldrb_w|ldr_x_imm|strb_w|add_x_reg|add_x_reg_lsl|bl_placeholder|b_placeholder|b_cond_placeholder|cbz_w_placeholder)|macho_patch_(?:branch26|cond19|adrp_add))\b/.test(text))
      .map(([path]) => path),
  },
};
const violations = budgetViolations(files, allLargeFunctions, stdlib, backendFormats);

const report = {
  schema: 1,
  files,
  largeFunctions: allLargeFunctions.slice(0, 25),
  stdlib,
  backendFormats,
  budget: {
    ok: violations.length === 0,
    newLargeFunctionLimit: NEW_LARGE_FUNCTION_LIMIT,
    reportThreshold: LARGE_FUNCTION_REPORT_THRESHOLD,
    sourceFileCount: sourceFiles.length,
    knownReturnTypeDivergences: Object.fromEntries(knownReturnTypeDivergences),
    allowedHelpersWithSpecialArgTypeChecks,
    violations,
  },
};

console.log(JSON.stringify(report, null, 2));
if (violations.length > 0) {
  process.exitCode = 1;
}
