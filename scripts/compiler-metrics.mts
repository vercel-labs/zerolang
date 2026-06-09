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
  "native/zero-c/include/zero.h": { maxLines: 1178, maxStrcmpCalls: 0 },
  "native/zero-c/include/zero_contracts.h": { maxLines: 20, maxStrcmpCalls: 0 },
  "native/zero-c/include/zero_runtime.h": { maxLines: 226, maxStrcmpCalls: 0 },
  "native/zero-c/src/abi_report.c": { maxLines: 360, maxStrcmpCalls: 2 },
  "native/zero-c/src/abi_report.h": { maxLines: 18, maxStrcmpCalls: 0 },
  "native/zero-c/src/checker.c": { maxLines: 11789, maxStrcmpCalls: 287 },
  "native/zero-c/src/cli_help.c": { maxLines: 125, maxStrcmpCalls: 1 },
  "native/zero-c/src/cli_help.h": { maxLines: 8, maxStrcmpCalls: 0 },
  "native/zero-c/src/http_listen_runner.c": { maxLines: 399, maxStrcmpCalls: 0 },
  "native/zero-c/src/http_listen_runner.h": { maxLines: 22, maxStrcmpCalls: 0 },
  "native/zero-c/src/init_template.c": { maxLines: 310, maxStrcmpCalls: 13 },
  "native/zero-c/src/init_template.h": { maxLines: 15, maxStrcmpCalls: 0 },
  "native/zero-c/src/main.c": { maxLines: 14896, maxStrcmpCalls: 440 },
  "native/zero-c/src/ir.c": { maxLines: 5508, maxStrcmpCalls: 272 },
  "native/zero-c/src/llvm_backend_metadata.c": { maxLines: 80, maxStrcmpCalls: 0 },
  "native/zero-c/src/llvm_toolchain.c": { maxLines: 335, maxStrcmpCalls: 19 },
  "native/zero-c/src/manifest_toml.c": { maxLines: 430, maxStrcmpCalls: 4 },
  "native/zero-c/src/manifest_toml.h": { maxLines: 8, maxStrcmpCalls: 0 },
  "native/zero-c/src/mir_binary.c": { maxLines: 1285, maxStrcmpCalls: 1 },
  "native/zero-c/src/mir_binary.h": { maxLines: 37, maxStrcmpCalls: 0 },
  "native/zero-c/src/ast.c": { maxLines: 250, maxStrcmpCalls: 0 },
  "native/zero-c/src/backend_family.c": { maxLines: 75, maxStrcmpCalls: 5 },
  "native/zero-c/src/buildability.c": { maxLines: 352, maxStrcmpCalls: 2 },
  "native/zero-c/src/buildability_value_support.c": { maxLines: 210, maxStrcmpCalls: 0 },
  "native/zero-c/src/buildability.h": { maxLines: 20, maxStrcmpCalls: 0 },
  "native/zero-c/src/buildability_internal.h": { maxLines: 40, maxStrcmpCalls: 0 },
  "native/zero-c/src/buildability_context.c": { maxLines: 215, maxStrcmpCalls: 1 },
  "native/zero-c/src/buildability_targets.c": { maxLines: 180, maxStrcmpCalls: 0 },
  "native/zero-c/src/buildability_value_targets.c": { maxLines: 560, maxStrcmpCalls: 0 },
  "native/zero-c/src/c_import.c": { maxLines: 750, maxStrcmpCalls: 51 },
  "native/zero-c/src/c_import.h": { maxLines: 40, maxStrcmpCalls: 0 },
  "native/zero-c/src/call_resolve.c": { maxLines: 200, maxStrcmpCalls: 2 },
  "native/zero-c/src/call_resolve.h": { maxLines: 100, maxStrcmpCalls: 0 },
  "native/zero-c/src/capability_names.c": { maxLines: 150, maxStrcmpCalls: 1 },
  "native/zero-c/src/capability_summary.c": { maxLines: 190, maxStrcmpCalls: 0 },
  "native/zero-c/src/capability_summary.h": { maxLines: 26, maxStrcmpCalls: 0 },
  "native/zero-c/src/canonical_text.c": { maxLines: 1508, maxStrcmpCalls: 0 },
  "native/zero-c/src/canonical_text_format.c": { maxLines: 354, maxStrcmpCalls: 0 },
  "native/zero-c/src/canonical_text_program.c": { maxLines: 1493, maxStrcmpCalls: 0 },
  "native/zero-c/src/canonical_text_write.c": { maxLines: 604, maxStrcmpCalls: 2 },
  "native/zero-c/src/canonical_text.h": { maxLines: 80, maxStrcmpCalls: 0 },
  "native/zero-c/src/coff_format.c": { maxLines: 370, maxStrcmpCalls: 0 },
  "native/zero-c/src/coff_format.h": { maxLines: 100, maxStrcmpCalls: 0 },
  "native/zero-c/src/coff_emit_state.c": { maxLines: 168, maxStrcmpCalls: 0 },
  "native/zero-c/src/coff_emit_state.h": { maxLines: 84, maxStrcmpCalls: 0 },
  "native/zero-c/src/direct_emit.c": { maxLines: 35, maxStrcmpCalls: 0 },
  "native/zero-c/src/direct_emit.h": { maxLines: 30, maxStrcmpCalls: 0 },
  "native/zero-c/src/direct_metrics.c": { maxLines: 35, maxStrcmpCalls: 0 },
  "native/zero-c/src/elf_format.c": { maxLines: 220, maxStrcmpCalls: 0 },
  "native/zero-c/src/elf_format.h": { maxLines: 60, maxStrcmpCalls: 0 },
  "native/zero-c/src/elf_emit_state.c": { maxLines: 176, maxStrcmpCalls: 0 },
  "native/zero-c/src/elf_emit_state.h": { maxLines: 106, maxStrcmpCalls: 0 },
  "native/zero-c/src/macho_format.c": { maxLines: 470, maxStrcmpCalls: 0 },
  "native/zero-c/src/macho_format.h": { maxLines: 90, maxStrcmpCalls: 0 },
  "native/zero-c/src/aarch64_direct.c": { maxLines: 1653, maxStrcmpCalls: 1 },
  "native/zero-c/src/aarch64_direct.h": { maxLines: 64, maxStrcmpCalls: 0 },
  "native/zero-c/src/aarch64_emit.c": { maxLines: 445, maxStrcmpCalls: 0 },
  "native/zero-c/src/aarch64_emit.h": { maxLines: 87, maxStrcmpCalls: 0 },
  "native/zero-c/src/emit_macho64.c": { maxLines: 2737, maxStrcmpCalls: 2 },
  "native/zero-c/src/emit_macho_x64.c": { maxLines: 2090, maxStrcmpCalls: 1 },
  "native/zero-c/src/macho_emit_state.c": { maxLines: 227, maxStrcmpCalls: 0 },
  "native/zero-c/src/macho_emit_state.h": { maxLines: 110, maxStrcmpCalls: 0 },
  "native/zero-c/src/emit_elf64.c": { maxLines: 3256, maxStrcmpCalls: 3 },
  "native/zero-c/src/emit_elf_aarch64.c": { maxLines: 458, maxStrcmpCalls: 1 },
  "native/zero-c/src/emit_llvm_ir.c": { maxLines: 944, maxStrcmpCalls: 9 },
  "native/zero-c/src/emit_coff.c": { maxLines: 1974, maxStrcmpCalls: 1 },
  "native/zero-c/src/emit_coff_aarch64.c": { maxLines: 490, maxStrcmpCalls: 0 },
  "native/zero-c/src/fs.c": { maxLines: 1364, maxStrcmpCalls: 36 },
  "native/zero-c/src/mir_verify.c": { maxLines: 2306, maxStrcmpCalls: 0 },
  "native/zero-c/src/mir_verify.h": { maxLines: 50, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph.c": { maxLines: 40, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_build.c": { maxLines: 60, maxStrcmpCalls: 8 },
  "native/zero-c/src/program_graph_build.h": { maxLines: 25, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_c_import.c": { maxLines: 96, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_c_import.h": { maxLines: 10, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_c_import_metadata.c": { maxLines: 120, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_c_import_metadata.h": { maxLines: 12, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_command.c": { maxLines: 175, maxStrcmpCalls: 2 },
  "native/zero-c/src/program_graph_command.h": { maxLines: 30, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_compile.c": { maxLines: 124, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_contracts.c": { maxLines: 280, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_contracts.h": { maxLines: 13, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_effect_contracts.c": { maxLines: 220, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_memory_contracts.c": { maxLines: 190, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_borrow_contracts.c": { maxLines: 220, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_compare.c": { maxLines: 570, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_compare.h": { maxLines: 25, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_format.c": { maxLines: 1085, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_format.h": { maxLines: 21, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_std_deps.c": { maxLines: 90, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_std_deps.h": { maxLines: 10, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_std_merge.c": { maxLines: 380, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph.h": { maxLines: 135, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_identity.c": { maxLines: 500, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_import.c": { maxLines: 496, maxStrcmpCalls: 4 },
  "native/zero-c/src/program_graph_import.h": { maxLines: 8, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_lower.c": { maxLines: 1219, maxStrcmpCalls: 4 },
  "native/zero-c/src/program_graph_lower.h": { maxLines: 10, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_manifest.c": { maxLines: 240, maxStrcmpCalls: 8 },
  "native/zero-c/src/program_graph_manifest.h": { maxLines: 15, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_manifest_identity.c": { maxLines: 92, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_mir.c": { maxLines: 4687, maxStrcmpCalls: 5 },
  "native/zero-c/src/program_graph_query.c": { maxLines: 350, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_query.h": { maxLines: 10, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_query_internal.h": { maxLines: 20, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_query_refs.c": { maxLines: 180, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_query_text.c": { maxLines: 150, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_order.c": { maxLines: 80, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_order.h": { maxLines: 10, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_node_id.c": { maxLines: 350, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_report.c": { maxLines: 80, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_report.h": { maxLines: 20, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_validate.c": { maxLines: 537, maxStrcmpCalls: 5 },
  "native/zero-c/src/program_graph_patch_builders.c": { maxLines: 375, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_patch_builders.h": { maxLines: 15, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_patch_body.c": { maxLines: 660, maxStrcmpCalls: 20 },
  "native/zero-c/src/program_graph_patch_body.h": { maxLines: 10, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_patch_examples.c": { maxLines: 35, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_patch_ops.c": { maxLines: 1740, maxStrcmpCalls: 13 },
  "native/zero-c/src/program_graph_patch.c": { maxLines: 860, maxStrcmpCalls: 46 },
  "native/zero-c/src/program_graph_patch.h": { maxLines: 69, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_projection.c": { maxLines: 465, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_projection.h": { maxLines: 25, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_projection_validate.c": { maxLines: 465, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_reconcile.c": { maxLines: 400, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_reconcile.h": { maxLines: 30, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_reconcile_apply.c": { maxLines: 478, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_reconcile_apply.h": { maxLines: 25, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_repository.c": { maxLines: 896, maxStrcmpCalls: 12 },
  "native/zero-c/src/program_graph_repository.h": { maxLines: 19, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_repository_input.c": { maxLines: 245, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_repository_input.h": { maxLines: 12, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_repository_merge.c": { maxLines: 864, maxStrcmpCalls: 3 },
  "native/zero-c/src/program_graph_repository_merge.h": { maxLines: 35, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_repository_repair.c": { maxLines: 75, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_repository_repair.h": { maxLines: 20, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_store_binary.c": { maxLines: 706, maxStrcmpCalls: 3 },
  "native/zero-c/src/program_graph_store_binary.h": { maxLines: 10, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_store.c": { maxLines: 1282, maxStrcmpCalls: 6 },
  "native/zero-c/src/program_graph_store_prune.c": { maxLines: 168, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_store_prune.h": { maxLines: 10, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_store_tables.c": { maxLines: 220, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_store_tables.h": { maxLines: 40, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_store.h": { maxLines: 49, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_test.c": { maxLines: 700, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_test_caps.c": { maxLines: 230, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_test_caps.h": { maxLines: 10, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_test.h": { maxLines: 20, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_resolve.c": { maxLines: 1404, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_resolve.h": { maxLines: 35, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_semantics.c": { maxLines: 1348, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_semantics.h": { maxLines: 15, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_roundtrip.c": { maxLines: 55, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_roundtrip.h": { maxLines: 15, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_size.c": { maxLines: 251, maxStrcmpCalls: 2 },
  "native/zero-c/src/program_graph_size.h": { maxLines: 10, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_source_map.c": { maxLines: 460, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_source_map.h": { maxLines: 20, maxStrcmpCalls: 0 },
  "native/zero-c/src/program_graph_view.c": { maxLines: 841, maxStrcmpCalls: 1 },
  "native/zero-c/src/program_graph_view.h": { maxLines: 8, maxStrcmpCalls: 0 },
  "native/zero-c/src/safety_contract.c": { maxLines: 70, maxStrcmpCalls: 0 },
  "native/zero-c/src/safety_contract.h": { maxLines: 30, maxStrcmpCalls: 0 },
  "native/zero-c/src/specialize.c": { maxLines: 150, maxStrcmpCalls: 2 },
  "native/zero-c/src/specialize.h": { maxLines: 50, maxStrcmpCalls: 0 },
  "native/zero-c/src/std_sig.c": { maxLines: 451, maxStrcmpCalls: 2 },
  "native/zero-c/src/std_sig.h": { maxLines: 61, maxStrcmpCalls: 0 },
  "native/zero-c/src/std_source.c": { maxLines: 344, maxStrcmpCalls: 2 },
  "native/zero-c/src/std_source.h": { maxLines: 30, maxStrcmpCalls: 0 },
  "native/zero-c/src/target_backend.c": { maxLines: 392, maxStrcmpCalls: 1 },
  "native/zero-c/src/target.c": { maxLines: 517, maxStrcmpCalls: 1 },
  "native/zero-c/src/type_core.c": { maxLines: 900, maxStrcmpCalls: 8 },
  "native/zero-c/src/type_core.h": { maxLines: 150, maxStrcmpCalls: 0 },
  "native/zero-c/src/unify.c": { maxLines: 500, maxStrcmpCalls: 14 },
  "native/zero-c/src/unify.h": { maxLines: 75, maxStrcmpCalls: 0 },
  "native/zero-c/src/x64_emit.c": { maxLines: 850, maxStrcmpCalls: 0 },
  "native/zero-c/src/x64_emit.h": { maxLines: 116, maxStrcmpCalls: 0 },
};

const knownLargeFunctionLimits = new Map([
  ["native/zero-c/src/ir.c|static bool ir_lower_expr(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *expr, IrValue **out) {", 1641],
  ["native/zero-c/src/checker.c|static bool check_expr_expected(CheckContext *ctx, const Program *program, const Expr *expr, Scope *scope, ZDiag *diag, const char *expected) {", 567],
  ["native/zero-c/src/main.c|int main(int argc, char **argv) {", 1016],
  ["native/zero-c/src/main.c|static void append_graph_json(ZBuf *buf, SourceInput *input, Program *program, const ZTargetInfo *target, const Command *command) {", 386],
  ["native/zero-c/src/checker.c|static bool check_stmt(CheckContext *ctx, const Program *program, const Function *fun, const Stmt *stmt, Scope *scope, ZDiag *diag, int loop_depth) {", 279],
  ["native/zero-c/src/checker.c|static bool check_program_internal(const Program *program, bool require_entrypoint, ZDiag *diag) {", 213],
  ["native/zero-c/src/checker.c|static const char *expr_type(CheckContext *ctx, const Program *program, const Expr *expr, Scope *scope) {", 205],
  ["native/zero-c/src/checker.c|static bool collect_return_value_provenance_from_stmt_vec(CheckContext *ctx, const Program *program, const Function *fun, const StmtVec *body, Scope *scope, GenericBinding *bindings, size_t binding_len, ValueProvenance *out, bool *may_return, bool *complete) {", 192],
  ["native/zero-c/src/ir.c|static bool ir_lower_stmt_to_vec(const Program *program, IrProgram *ir, IrFunction *mir_fun, const Stmt *stmt, IrInstr **out_items, size_t *out_len, size_t *out_cap, bool *saw_return) {", 166],
  ["native/zero-c/src/checker.c|static bool expr_reference_provenance(CheckContext *ctx, const Program *program, const Expr *expr, Scope *scope, ValueProvenance *origins) {", 175],
  ["native/zero-c/src/main.c|static int run_tests_direct(const Command *command, const SourceInput *input, const Program *program, const ZTargetInfo *target) {", 151],
  ["native/zero-c/src/checker.c|static bool check_scalar_match(CheckContext *ctx, const Program *program, const Function *fun, const Stmt *stmt, Scope *scope, ZDiag *diag, int loop_depth, const char *match_type) {", 127],
  ["native/zero-c/src/emit_macho64.c|static bool macho_emit_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {", 159],
  ["native/zero-c/src/program_graph_mir.c|static bool ir_graph_lower_std_byte_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {", 616],
  ["native/zero-c/src/program_graph_mir.c|static bool ir_graph_lower_std_fs_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {", 225],
  ["native/zero-c/src/mir_verify.c|static bool mir_verify_direct_helper_value_contract(IrProgram *ir, const IrFunction *fun, const MirVerifierState *state, const IrValue *value, MirHelperRequirements *requirements) {", 252],
  ["native/zero-c/src/main.c|static void runtime_import_audit_value(const IrValue *value, RuntimeImportAudit *audit) {", 171],
  ["native/zero-c/src/program_graph_mir.c|static bool ir_graph_lower_stmt(const ZProgramGraph *graph, IrProgram *ir, IrFunction *fun, const ZProgramGraphNode *stmt, IrInstr **out_items, size_t *out_len, size_t *out_cap, bool *saw_return) {", 220],
  ["native/zero-c/src/emit_elf64.c|static bool elf_emit_str_runtime_value(ZBuf *code, const IrFunction *fun, const IrValue *value, ElfEmitContext *ctx, ZDiag *diag) {", 159],
  ["native/zero-c/src/emit_macho_x64.c|static bool machx64_emit_str_runtime_value(ZBuf *text, const IrFunction *fun, const IrValue *value, MachOEmitContext *ctx, ZDiag *diag) {", 159],
  ["native/zero-c/src/mir_verify.c|static bool mir_verify_math_runtime_contract(IrProgram *ir, const IrValue *value, MirHelperRequirements *requirements) {", 142],
  ["native/zero-c/src/mir_verify.c|static bool mir_verify_direct_value_kind_contract(IrProgram *ir, const IrFunction *fun, const MirVerifierState *state, const IrValue *value, MirHelperRequirements *requirements) {", 146],
  ["native/zero-c/src/emit_llvm_ir.c|static bool llvm_emit_value(LlvmEmit *emit, const IrValue *value, LlvmValue *out, ZDiag *diag) {", 129],
  ["native/zero-c/src/aarch64_direct.c|static bool a64_emit_str_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {", 124],
  ["native/zero-c/src/emit_macho64.c|static bool macho_emit_str_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {", 124],
  ["native/zero-c/src/emit_coff.c|bool z_emit_coff_x64_object_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {", 123],
]);

const knownReturnTypeDivergences = new Map();

const allowedHelpersWithSpecialArgTypeChecks = [];

const expectedStdHelperKinds = new Map([
  ["std.collections.append", "Z_STD_HELPER_KIND_COLLECTIONS_APPEND"],
  ["std.collections.contains", "Z_STD_HELPER_KIND_COLLECTIONS_LEN_VALUE"],
  ["std.collections.count", "Z_STD_HELPER_KIND_COLLECTIONS_LEN_VALUE"],
  ["std.collections.moveToFront", "Z_STD_HELPER_KIND_COLLECTIONS_LEN_INDEX"],
  ["std.collections.push", "Z_STD_HELPER_KIND_COLLECTIONS_PUSH"],
  ["std.collections.removeSwap", "Z_STD_HELPER_KIND_COLLECTIONS_LEN_INDEX"],
  ["std.collections.view", "Z_STD_HELPER_KIND_COLLECTIONS_VIEW"],
  ["std.mem.len", "Z_STD_HELPER_KIND_MEM_LEN"],
  ["std.mem.get", "Z_STD_HELPER_KIND_MEM_GET"],
  ["std.mem.eqlBytes", "Z_STD_HELPER_KIND_MEM_EQL_BYTES"],
  ["std.mem.copyItems", "Z_STD_HELPER_KIND_MEM_COPY_ITEMS"],
  ["std.mem.fillItems", "Z_STD_HELPER_KIND_MEM_FILL_ITEMS"],
  ["std.mem.contains", "Z_STD_HELPER_KIND_MEM_CONTAINS"],
  ["std.mem.isEmpty", "Z_STD_HELPER_KIND_MEM_IS_EMPTY"],
  ["std.mem.prefix", "Z_STD_HELPER_KIND_MEM_SLICE"],
  ["std.mem.dropPrefix", "Z_STD_HELPER_KIND_MEM_SLICE"],
  ["std.mem.allocBytes", "Z_STD_HELPER_KIND_MEM_ALLOC_BYTES"],
  ["std.mem.byteBuf", "Z_STD_HELPER_KIND_MEM_BYTE_BUF"],
  ["std.search.indexOf", "Z_STD_HELPER_KIND_SEARCH_INDEX"],
  ["std.search.lastIndexOf", "Z_STD_HELPER_KIND_SEARCH_INDEX"],
  ["std.fs.read", "Z_STD_HELPER_KIND_FS_READ"],
  ["std.fs.readAll", "Z_STD_HELPER_KIND_FS_READ_ALL"],
  ["std.fs.readAllOrRaise", "Z_STD_HELPER_KIND_FS_READ_ALL_OR_RAISE"],
  ["std.http.listen", "Z_STD_HELPER_KIND_HTTP_LISTEN"],
  ["std.json.parse", "Z_STD_HELPER_KIND_JSON_PARSE"],
  ["std.json.parseBytes", "Z_STD_HELPER_KIND_JSON_PARSE_BYTES"],
]);

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
  const resolver = cTextWithoutComments(cBlock(text, "static bool resolve_stdlib_callee"));
  return /\bz_std_helper_find\s*\(/.test(resolver) &&
    /\bz_call_resolution_set_return_type\s*\([^;]*helper->return_type/.test(resolver);
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
  const code = cTextWithoutComments(text);
  return (/\bz_std_helper_find\s*\(/.test(block) && /->arg_count\b/.test(block)) ||
    (/param_len\s*=\s*helper->arg_count/.test(code) && /\bz_call_resolution_expected_arg_count\s*\(/.test(code));
}

function parseCheckerArgTypeNames(text) {
  const block = cTextWithoutComments(cBlock(text, "static const char *std_call_arg_type"));
  return namesFromRegex(block, /strcmp\(name,\s+"(std\.[^"]+)"/g);
}

function parseCheckerArgTypeInfo(text) {
  const names = parseCheckerArgTypeNames(text);
  return {
    names,
    map: new Map(names.map((name) => [name, []])),
    duplicates: duplicates(names),
  };
}

function parseStdHelperArgTypes(text) {
  const block = cTextWithoutComments(cBlock(text, "const ZStdHelperInfo z_std_helpers[] ="));
  const names = [];
  const map = new Map();
  for (const match of block.matchAll(/\{\s*"([^"]+)"\s*,\s*"[^"]+"\s*,\s*-?\d+\s*,\s*\{([^}]*)\}/g)) {
    const args = match[2]
      .split(",")
      .map((arg) => arg.trim())
      .filter((arg) => arg.length > 0)
      .map((arg) => arg === "NULL" ? null : arg.replace(/^"|"$/g, ""));
    if (!args.some((arg) => arg !== null)) continue;
    names.push(match[1]);
    if (!map.has(match[1])) map.set(match[1], args);
  }
  return {
    names: names.sort((a, b) => a.localeCompare(b)),
    map,
    duplicates: duplicates(names),
  };
}

function parseStdHelperErrors(text) {
  const block = cTextWithoutComments(cBlock(text, "const ZStdHelperInfo z_std_helpers[] ="));
  const names = [];
  const map = new Map();
  for (const match of block.matchAll(/\{\s*"([^"]+)"\s*,\s*"[^"]+"\s*,\s*-?\d+\s*,\s*\{[^}]*\}\s*,\s*\{([^}]*)\}/g)) {
    const errors = match[2]
      .split(",")
      .map((error) => error.trim())
      .filter((error) => error.length > 0)
      .map((error) => error === "NULL" ? null : error.replace(/^"|"$/g, ""))
      .filter((error) => error !== null);
    if (errors.length === 0) continue;
    names.push(match[1]);
    if (!map.has(match[1])) map.set(match[1], errors);
  }
  return {
    names: names.sort((a, b) => a.localeCompare(b)),
    map,
    duplicates: duplicates(names),
  };
}

function stdHelperKindMismatches(text) {
  const block = cTextWithoutComments(cBlock(text, "const ZStdHelperInfo z_std_helpers[] ="));
  const actualKinds = new Map();
  for (const line of block.split("\n")) {
    const match = line.match(/\{\s*"([^"]+)".*(Z_STD_HELPER_KIND_[A-Z_]+)\s*\},/);
    if (match) actualKinds.set(match[1], match[2]);
  }
  const mismatches = [];
  for (const [name, expectedKind] of expectedStdHelperKinds) {
    const actualKind = actualKinds.get(name);
    if (actualKind !== expectedKind) {
      mismatches.push({ name, expectedKind, actualKind: actualKind ?? "<missing>" });
    }
  }
  for (const [name, actualKind] of actualKinds) {
    if (actualKind !== "Z_STD_HELPER_KIND_TABLE" && expectedStdHelperKinds.get(name) !== actualKind) {
      mismatches.push({ name, expectedKind: expectedStdHelperKinds.get(name) ?? "Z_STD_HELPER_KIND_TABLE", actualKind });
    }
  }
  return mismatches.sort((a, b) => a.name.localeCompare(b.name));
}

function checkerUsesSharedArgTypes(text) {
  const resolver = cTextWithoutComments(cBlock(text, "static bool resolve_stdlib_callee"));
  return /\bhelper->arg_types\s*\[/.test(resolver) &&
    /\bz_call_resolution_add_arg\s*\(/.test(resolver);
}

function checkerUsesSharedStdlibFallibility(text) {
  const code = cTextWithoutComments(text);
  const resolver = cTextWithoutComments(cBlock(text, "static bool resolve_stdlib_callee"));
  return /\bresolve_stdlib_fallible_call\s*\(/.test(code) &&
    /\bfunction_error_sets_include_stdlib_resolution\s*\(/.test(code) &&
    /\bz_call_resolution_error_set_text\s*\(/.test(code) &&
    /\bz_call_resolution_add_error\s*\(/.test(resolver) &&
    /\bz_std_helper_error_name\s*\(/.test(resolver) &&
    !/\bis_builtin_fallible_call\b/.test(code) &&
    !/\bbuiltin_fallible_return_type\b/.test(code);
}

function checkerUsesSharedStdlibHelperClassification(text) {
  const knownCall = cTextWithoutComments(cBlock(text, "static bool check_stdlib_known_call_expected"));
  const memGetProvenance = cTextWithoutComments(cBlock(text, "static bool std_mem_get_value_provenance"));
  return /\bz_std_helper_kind\s*\(/.test(knownCall) &&
    /\bswitch\s*\(\s*kind\s*\)/.test(knownCall) &&
    !/\bstrcmp\s*\(/.test(knownCall) &&
    /\bz_std_helper_kind\s*\(/.test(memGetProvenance) &&
    !/\bstrcmp\s*\(/.test(memGetProvenance);
}

function mainUsesSharedStdlibFallibility(text) {
  const block = cTextWithoutComments(cBlock(text, "static const char *helper_error_behavior"));
  return /\bz_std_helper_is_fallible\s*\(/.test(block) &&
    /\bz_std_helper_error_set_text\s*\(/.test(block) &&
    !/\bOrRaise\b/.test(block);
}

function usesSharedStdlibCapabilityLookup(mainText, graphReportText, capabilityNamesText, capabilitySummaryHeaderText) {
  const mainCode = cTextWithoutComments(mainText);
  const graphReportCode = cTextWithoutComments(graphReportText);
  const capabilityNamesCode = cTextWithoutComments(capabilityNamesText);
  const capabilitySummaryHeaderCode = cTextWithoutComments(capabilitySummaryHeaderText);
  return /\bz_capability_summary_collect_std_name\s*\(/.test(mainCode) &&
    /\bz_capability_summary_set\s*\(/.test(mainCode) &&
    /\bz_capability_summary_collect_std_name\s*\(/.test(graphReportCode) &&
    /\bz_capability_summary_collect_std_name\s*\(/.test(capabilitySummaryHeaderCode) &&
    /\bz_capability_summary_set\s*\(/.test(capabilitySummaryHeaderCode) &&
    /\bz_std_helper_find\s*\(/.test(capabilityNamesCode) &&
    !/\bstatic\s+void\s+collect_capabilities_from_std_name\s*\(/.test(mainCode) &&
    !/\bstatic\s+void\s+graph_report_collect_capabilities_from_std_name\s*\(/.test(graphReportCode) &&
    !/\bstatic\s+void\s+graph_report_capability_set\s*\(/.test(graphReportCode);
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

function helperArgTypeArityMismatches(helpers, checkerArgTypes) {
  return helpers
    .filter((helper) => checkerArgTypes.has(helper.name) && checkerArgTypes.get(helper.name).length !== helper.argCount)
    .map((helper) => ({
      name: helper.name,
      helperArgCount: helper.argCount,
      checkerArgTypeCount: checkerArgTypes.get(helper.name).length,
    }))
    .sort((a, b) => a.name.localeCompare(b.name));
}

function helperArgTypeNullGaps(helpers, checkerArgTypes) {
  return helpers
    .filter((helper) => checkerArgTypes.has(helper.name))
    .flatMap((helper) => checkerArgTypes.get(helper.name)
      .slice(0, helper.argCount)
      .map((type, index) => ({ name: helper.name, index, type }))
      .filter((entry) => entry.type === null)
      .map((entry) => ({ name: entry.name, index: entry.index })))
    .sort((a, b) => a.name.localeCompare(b.name) || a.index - b.index);
}

function knownReturnTypeDivergenceMatches(mismatch) {
  const known = knownReturnTypeDivergences.get(mismatch.name);
  return known &&
    known.helperReturnType === mismatch.helperReturnType &&
    known.checkerReturnType === mismatch.checkerReturnType;
}

function budgetViolations(files, allLargeFunctions, stdlib, backendFormats, programGraph) {
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
  if (stdlib.duplicateCheckerArgTypes.length > 0) {
    violations.push({
      kind: "duplicate-checker-stdlib-arg-type",
      helpers: stdlib.duplicateCheckerArgTypes,
    });
  }
  if (stdlib.duplicateStdHelperErrors.length > 0) {
    violations.push({
      kind: "duplicate-stdlib-helper-error",
      helpers: stdlib.duplicateStdHelperErrors,
    });
  }
  if (stdlib.argTypeArityMismatches.length > 0) {
    violations.push({
      kind: "stdlib-helper-arg-type-arity-mismatch",
      mismatches: stdlib.argTypeArityMismatches,
    });
  }
  if (stdlib.argTypeNullGaps.length > 0) {
    violations.push({
      kind: "stdlib-helper-arg-type-null-gap",
      gaps: stdlib.argTypeNullGaps,
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
  if (stdlib.orRaiseHelpersMissingFallibleErrors.length > 0) {
    violations.push({
      kind: "stdlib-or-raise-helper-error-missing",
      names: stdlib.orRaiseHelpersMissingFallibleErrors,
    });
  }
  if (!stdlib.sharedSignatureLookup.checkerReturnTypes ||
      !stdlib.sharedSignatureLookup.checkerArgCounts ||
      !stdlib.sharedSignatureLookup.checkerArgTypes) {
    violations.push({
      kind: "stdlib-shared-signature-lookup",
      sharedSignatureLookup: stdlib.sharedSignatureLookup,
    });
  }
  if (!stdlib.sharedFallibilityLookup.checker || !stdlib.sharedFallibilityLookup.graph) {
    violations.push({
      kind: "stdlib-shared-fallibility-lookup",
      sharedFallibilityLookup: stdlib.sharedFallibilityLookup,
    });
  }
  if (!stdlib.sharedCapabilityLookup) {
    violations.push({
      kind: "stdlib-shared-capability-lookup",
      sharedCapabilityLookup: stdlib.sharedCapabilityLookup,
    });
  }
  if (!stdlib.sharedHelperClassification.checker) {
    violations.push({
      kind: "stdlib-shared-helper-classification",
      sharedHelperClassification: stdlib.sharedHelperClassification,
    });
  }
  if (stdlib.specializedHelperKindMismatches.length > 0) {
    violations.push({
      kind: "stdlib-specialized-helper-kind-mismatch",
      mismatches: stdlib.specializedHelperKindMismatches,
    });
  }
  if (programGraph.mainRawGraphCommandOutWrites > 0) {
    violations.push({
      kind: "program-graph-raw-command-output-write",
      programGraph,
    });
  }
  if (!programGraph.sourceCommandGraphMirFallbackRemoved ||
      !programGraph.sourceCommandGraphProgramPrepRemoved ||
      !programGraph.optedInRepositoryGraphClaimRemoved) {
    violations.push({
      kind: "program-graph-source-command-compiler-path",
      programGraph,
    });
  }
  if (!programGraph.repositoryStoreCommandPolicyCentralized) {
    violations.push({
      kind: "program-graph-repository-store-command-policy",
      programGraph,
    });
  }
  if (!programGraph.repositoryStoreCompilerTables ||
      !programGraph.repositoryStoreMetadataSerialized ||
      !programGraph.repositoryStoreMetadataValidated ||
      !programGraph.repositoryStatusCompilerStoreFacts ||
      !programGraph.repositoryStatusProjectionValidity) {
    violations.push({
      kind: "program-graph-repository-store-compiler-tables",
      programGraph,
    });
  }
  if (!programGraph.repositoryGraphCheckNative ||
      !programGraph.repositoryGraphCheckNoProgramLowering ||
      !programGraph.repositoryGraphCheckNoLegacyChecker ||
      !programGraph.repositoryGraphCheckReadinessNoProgramReconstruction ||
      !programGraph.repositoryGraphCheckReportsSemanticFacts ||
      !programGraph.repositoryGraphCheckReportsGraphMirState ||
      !programGraph.repositoryGraphCheckStoredTypedFactsAuthority ||
      !programGraph.artifactGraphCheckNative ||
      !programGraph.artifactGraphCheckNoProgramLowering ||
      !programGraph.artifactGraphCheckNoLegacyChecker ||
      !programGraph.artifactGraphCheckReportsSemanticFacts ||
      !programGraph.artifactGraphCheckReportsGraphMirState ||
      !programGraph.repositoryGraphCheckDefaultReadiness ||
      !programGraph.repositoryGraphCheckPerformanceBudget ||
      !programGraph.repositoryGraphCacheKeyFacts) {
    violations.push({
      kind: "program-graph-repository-native-check-path",
      programGraph,
    });
  }
  if (!programGraph.repositoryGraphMirPrepMappedFinalMir ||
      !programGraph.repositoryGraphMirPrepImmediateCacheHit ||
      !programGraph.artifactGraphMirPrepMappedFinalMir ||
      !programGraph.artifactGraphMirPrepImmediateCacheHit ||
      !programGraph.repositoryGraphMirCacheFacts ||
      !programGraph.repositoryGraphMirPrepSourceFreeFirst ||
      !programGraph.repositoryGraphMirPrepNoStdHelperBridge ||
      !programGraph.repositoryGraphMirPrepReportsUnsupportedFacts ||
      !programGraph.repositoryGraphMirPrepNoInvalidMirProgramFallback ||
      !programGraph.artifactGraphMirPrepNoInvalidMirProgramFallback ||
      !programGraph.artifactGraphSizeNoProgramReconstruction ||
      !programGraph.artifactGraphProgramPrepRemoved) {
    violations.push({
      kind: "program-graph-repository-mir-prep",
      programGraph,
    });
  }
  if (!programGraph.graphTestNativeRunner ||
      !programGraph.graphTestNoProgramLowering ||
      !programGraph.graphTestSemanticContracts ||
      !programGraph.graphTestRepositoryStoreInput ||
      !programGraph.graphTestMutableControlFlow) {
    violations.push({
      kind: "program-graph-native-test-runner",
      programGraph,
    });
  }
  if (!programGraph.repositoryCompilerInputSourceFree ||
      !programGraph.repositoryCompilerInputProjectionStatus ||
      !programGraph.repositoryGraphSourceLocationsNotSemanticMatch ||
      !programGraph.repositoryProjectionValidationIgnoresSourceLocation ||
      !programGraph.repositoryArtifactProjectionState) {
    violations.push({
      kind: "program-graph-repository-source-free-input",
      programGraph,
    });
  }
  if (!backendFormats.directTarget.ruleMatrix ||
      !backendFormats.directTarget.executableUsesRuleMatrix ||
      !backendFormats.directTarget.descriptorTable ||
      !backendFormats.directTarget.emitKindParser ||
      backendFormats.directTarget.executableTargetNameChecks > 0 ||
      backendFormats.directTarget.mainExecutableEmitterStringChecks > 0 ||
      backendFormats.directTarget.mainObjectEmitterStringChecks > 0 ||
      backendFormats.directTarget.mainRuntimeCacheKeyStringChecks > 0 ||
      backendFormats.directTarget.mainDirectBackendDiagStringHelpers > 0 ||
      backendFormats.directTarget.mainBackendFromEmitterCalls > 0 ||
      backendFormats.directTarget.mainDirectPathFromEmitterHelpers > 0 ||
      backendFormats.directTarget.mainObjectFormatSymbolChecks > 0 ||
      backendFormats.directTarget.mainManualDirectToolchainJson > 0 ||
      backendFormats.directTarget.mainDirectMetricMachoChecks > 0 ||
      backendFormats.directTarget.mainDirectBackendNameHelpers > 0 ||
      backendFormats.directTarget.mainDirectEmitSelectionHelpers > 0 ||
      backendFormats.directTarget.mainDirectReleaseTargetHelpers > 0 ||
      backendFormats.directTarget.mainDirectRuntimeLinkBlockers > 0 ||
      backendFormats.directTarget.mainDirectObjectSelectionHelpers > 0 ||
      backendFormats.directTarget.mainDirectBuildToolchainSelectionHelpers > 0 ||
      backendFormats.directTarget.mainDirectExecutableSelectionHelpers > 0 ||
      backendFormats.directTarget.mainDirectEmitDispatchHelpers > 0 ||
      backendFormats.directTarget.mainDirectBuildArtifactSelectionHelpers > 0 ||
      backendFormats.directTarget.mainDirectReadinessSelectionHelpers > 0) {
    violations.push({
      kind: "direct-target-backend-matrix",
      directTarget: backendFormats.directTarget,
    });
  }
  if (!backendFormats.targetManifest.exactKeyMatcher ||
      !backendFormats.targetManifest.exactListMatcher ||
      !backendFormats.targetManifest.noAliasSubstringLookup ||
      !backendFormats.targetManifest.noCapabilitySubstringLookup ||
      !backendFormats.targetManifest.linkerLabelHelper) {
    violations.push({
      kind: "target-manifest-exact-matching",
      targetManifest: backendFormats.targetManifest,
    });
  }
  if (!backendFormats.abiReport.primitiveTable ||
      !backendFormats.abiReport.primitiveLookup ||
      !backendFormats.abiReport.primitiveJsonIteratesTable ||
      backendFormats.abiReport.rawPrimitiveStringChecks > 0) {
    violations.push({
      kind: "abi-report-primitive-table",
      abiReport: backendFormats.abiReport,
    });
  }
  if (!backendFormats.manifestToml.fieldBindingTable ||
      !backendFormats.manifestToml.sharedFieldAssignment ||
      !backendFormats.manifestToml.sharedParsedLineCleanup ||
      !backendFormats.manifestToml.sharedDiagnostics ||
      !backendFormats.manifestToml.arrayParserChecksSkippedString ||
      backendFormats.manifestToml.rawFieldDispatchStringChecks > 0) {
    violations.push({
      kind: "manifest-toml-field-dispatch",
      manifestToml: backendFormats.manifestToml,
    });
  }
  if (!backendFormats.buildability.valueSupportSeparatedModule ||
      !backendFormats.buildability.valueSupportBackendSplit ||
      !backendFormats.buildability.sharedHostedRuntimePredicate ||
      !backendFormats.buildability.sharedJsonPredicates ||
      !backendFormats.buildability.sharedByteRuntimePredicate ||
      !backendFormats.buildability.sharedX64ByteViewCheckers ||
      !backendFormats.buildability.legacyDuplicatedX64ByteViewCheckersRemoved ||
      backendFormats.buildability.dispatcherLines > 8 ||
      !backendFormats.buildability.targetValueBackendSplit ||
      !backendFormats.buildability.targetValueSharedPairCheck ||
      backendFormats.buildability.targetValueDispatcherLines > 26) {
    violations.push({
      kind: "buildability-value-support-regression",
      buildability: backendFormats.buildability,
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
      !backendFormats.x64.coffUsesSharedEncodingPrimitives ||
      !backendFormats.x64.machoUsesSharedEncodingPrimitives) {
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
      !backendFormats.aarch64.directLayerUsesSharedEncodingPrimitives ||
      !backendFormats.aarch64.elfUsesSharedEncodingPrimitives ||
      !backendFormats.aarch64.coffUsesSharedEncodingPrimitives ||
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
const capabilityNames = texts.get("native/zero-c/src/capability_names.c") ?? "";
const capabilitySummaryHeader = texts.get("native/zero-c/src/capability_summary.h") ?? "";
const programGraphReport = texts.get("native/zero-c/src/program_graph_report.c") ?? "";
const targetRaw = texts.get("native/zero-c/src/target.c") ?? "";
const targetSource = cCodeText(targetRaw);
const targetBackendRaw = texts.get("native/zero-c/src/target_backend.c") ?? "";
const targetBackendSource = cCodeText(targetBackendRaw);
const directExeBackendBody = cCodeText(cBlock(targetBackendRaw, "ZDirectBackend z_direct_exe_backend"));
const abiReportRaw = texts.get("native/zero-c/src/abi_report.c") ?? "";
const abiReportSource = cCodeText(abiReportRaw);
const abiReportText = cTextWithoutComments(abiReportRaw);
const manifestTomlRaw = texts.get("native/zero-c/src/manifest_toml.c") ?? "";
const manifestTomlSource = cCodeText(manifestTomlRaw);
const manifestTomlText = cTextWithoutComments(manifestTomlRaw);
const buildabilityRaw = texts.get("native/zero-c/src/buildability.c") ?? "";
const buildabilitySource = cCodeText(buildabilityRaw);
const buildabilityValueSupportRaw = texts.get("native/zero-c/src/buildability_value_support.c") ?? "";
const buildabilityValueSupportSource = cCodeText(buildabilityValueSupportRaw);
const buildValueSupportedBody = cBlock(buildabilityValueSupportRaw, "bool z_build_value_supported(const ZBuildability *ctx");
const buildabilityTargetsRaw = texts.get("native/zero-c/src/buildability_targets.c") ?? "";
const buildabilityTargetsSource = cCodeText(buildabilityTargetsRaw);
const buildabilityValueTargetsRaw = texts.get("native/zero-c/src/buildability_value_targets.c") ?? "";
const buildabilityValueTargetsSource = cCodeText(buildabilityValueTargetsRaw);
const buildTargetValueBody = cBlock(buildabilityValueTargetsRaw, "bool z_build_check_target_value(const ZBuildability *ctx");

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
}
if (checkerArgCountsUseSharedTable) {
  for (const helper of stdHelpers) {
    checkerArgCounts.set(helper.name, helper.argCount);
  }
}
const stdHelperArgTypeInfo = parseStdHelperArgTypes(stdSig);
const stdHelperErrorInfo = parseStdHelperErrors(stdSig);
const checkerArgTypesUseSharedTable = checkerUsesSharedArgTypes(checker);
const checkerArgTypeInfo = checkerArgTypesUseSharedTable ? stdHelperArgTypeInfo : parseCheckerArgTypeInfo(checker);
const checkerArgTypeNames = checkerArgTypeInfo.names;
const checkerKnownStdNames = namesFromRegex(cTextWithoutComments(checker), /"(std\.[^"]+)"/g);
const checkerReturnNames = sortedMapKeys(checkerReturnTypes);
const checkerArgCountNames = sortedMapKeys(checkerArgCounts);
const mainHelperNames = stdHelpers.map((helper) => helper.name);
const fallibleHelperNames = stdHelperErrorInfo.names;
const orRaiseHelperNames = mainHelperNames.filter((name) => name.endsWith("OrRaise"));
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
  duplicateCheckerArgTypes: checkerArgTypeInfo.duplicates,
  duplicateStdHelperErrors: stdHelperErrorInfo.duplicates,
  specializedHelperKindMismatches: stdHelperKindMismatches(stdSig),
  returnNamesMissingFromMainHelpers: missingFrom(checkerReturnNames, mainHelperNames),
  checkerReturnsMissingFromMainHelpers: missingFrom(checkerReturnNames, mainHelperNames),
  mainHelpersMissingFromCheckerReturns: missingFrom(mainHelperNames, checkerReturnNames),
  returnTypeMismatches: helperReturnTypeMismatches(stdHelpers, checkerReturnTypes),
  checkerArgCountsMissingFromMainHelpers: missingFrom(checkerArgCountNames, mainHelperNames),
  mainHelpersMissingFromCheckerArgCounts: missingFrom(mainHelperNames, checkerArgCountNames),
  argCountMismatches: helperArgCountMismatches(stdHelpers, checkerArgCounts),
  checkerArgTypesMissingFromMainHelpers: missingFrom(checkerArgTypeNames, mainHelperNames),
  argTypeArityMismatches: helperArgTypeArityMismatches(stdHelpers, checkerArgTypeInfo.map),
  argTypeNullGaps: helperArgTypeNullGaps(stdHelpers, checkerArgTypeInfo.map),
  nonzeroArgHelpersMissingFromCheckerArgTypes: missingFrom(nonzeroArgHelperNames, checkerArgTypeNames),
  fallibleHelperCount: new Set(fallibleHelperNames).size,
  orRaiseHelpersMissingFallibleErrors: missingFrom(orRaiseHelperNames, fallibleHelperNames),
  mainHelpersMissingFromCheckerKnownNames: checkerReturnTypesUseSharedTable && checkerArgCountsUseSharedTable ? [] : missingFrom(mainHelperNames, checkerKnownStdNames),
  sharedSignatureLookup: {
    checkerReturnTypes: checkerReturnTypesUseSharedTable,
    checkerArgCounts: checkerArgCountsUseSharedTable,
    checkerArgTypes: checkerArgTypesUseSharedTable,
  },
  sharedFallibilityLookup: {
    checker: checkerUsesSharedStdlibFallibility(checker),
    graph: mainUsesSharedStdlibFallibility(main),
  },
  sharedCapabilityLookup: usesSharedStdlibCapabilityLookup(main, programGraphReport, capabilityNames, capabilitySummaryHeader),
  sharedHelperClassification: {
    checker: checkerUsesSharedStdlibHelperClassification(checker),
  },
};
const elfFormatSource = texts.get("native/zero-c/src/elf_format.c") ?? "";
const elfEmitStateSource = cCodeText(texts.get("native/zero-c/src/elf_emit_state.c") ?? "");
const coffFormatSource = texts.get("native/zero-c/src/coff_format.c") ?? "";
const coffEmitStateSource = cCodeText(texts.get("native/zero-c/src/coff_emit_state.c") ?? "");
const machoFormatSource = texts.get("native/zero-c/src/macho_format.c") ?? "";
const machoEmitStateSource = cCodeText(texts.get("native/zero-c/src/macho_emit_state.c") ?? "");
const aarch64EmitSource = texts.get("native/zero-c/src/aarch64_emit.c") ?? "";
const aarch64DirectSource = cCodeText(texts.get("native/zero-c/src/aarch64_direct.c") ?? "");
const x64EmitSource = texts.get("native/zero-c/src/x64_emit.c") ?? "";
const elfX64Source = cCodeText(texts.get("native/zero-c/src/emit_elf64.c") ?? "");
const elfAarch64Source = cCodeText(texts.get("native/zero-c/src/emit_elf_aarch64.c") ?? "");
const coffX64Source = cCodeText(texts.get("native/zero-c/src/emit_coff.c") ?? "");
const coffAarch64Source = cCodeText(texts.get("native/zero-c/src/emit_coff_aarch64.c") ?? "");
const machoArm64Source = cCodeText(texts.get("native/zero-c/src/emit_macho64.c") ?? "");
const machoX64Source = cCodeText(texts.get("native/zero-c/src/emit_macho_x64.c") ?? "");
const programGraphCompileSource = cCodeText(texts.get("native/zero-c/src/program_graph_compile.c") ?? "");
const programGraphMirRaw = texts.get("native/zero-c/src/program_graph_mir.c") ?? "";
const programGraphBuildRaw = texts.get("native/zero-c/src/program_graph_build.c") ?? "";
const programGraphBuildHeaderRaw = texts.get("native/zero-c/src/program_graph_build.h") ?? "";
const programGraphCommandRaw = texts.get("native/zero-c/src/program_graph_command.c") ?? "";
const programGraphCommandHeaderRaw = texts.get("native/zero-c/src/program_graph_command.h") ?? "";
const programGraphStoreRaw = texts.get("native/zero-c/src/program_graph_store.c") ?? "";
const programGraphStoreTablesRaw = texts.get("native/zero-c/src/program_graph_store_tables.c") ?? "";
const programGraphRepositoryRaw = texts.get("native/zero-c/src/program_graph_repository.c") ?? "";
const programGraphRepositoryInputRaw = texts.get("native/zero-c/src/program_graph_repository_input.c") ?? "";
const programGraphProjectionValidateRaw = texts.get("native/zero-c/src/program_graph_projection_validate.c") ?? "";
const programGraphTestRaw = texts.get("native/zero-c/src/program_graph_test.c") ?? "";
const programGraphCommandSource = cCodeText(programGraphCommandRaw);
const programGraphTestSource = cCodeText(programGraphTestRaw);
const programGraphStoreSource = cCodeText(programGraphStoreRaw);
const programGraphStoreTablesSource = cCodeText(programGraphStoreTablesRaw);
const programGraphRepositorySource = cCodeText(programGraphRepositoryRaw);
const programGraphRepositoryInputSource = cCodeText(programGraphRepositoryInputRaw);
const programGraphProjectionValidateSource = cCodeText(programGraphProjectionValidateRaw);
const artifactGraphCheckBody = cCodeText(cBlock(main, "static int run_graph_check_command"));
const artifactGraphCheckJsonRawBody = cBlock(main, "static void append_graph_check_json");
const artifactGraphReadinessBody = cCodeText(cBlock(main, "static bool append_graph_artifact_target_readiness_json"));
const artifactGraphSizeBody = cCodeText(cBlock(main, "static int run_graph_size_command"));
const repositoryGraphCheckBody = cCodeText(cBlock(main, "static int run_repository_graph_check_command"));
const graphNativeCompilerInputBody = cCodeText(cBlock(main, "static bool graph_native_compiler_input_ok"));
const repositoryGraphTargetReadinessBody = cCodeText(cBlock(main, "static bool append_repository_graph_target_readiness_json(ZBuf *buf, SourceInput *input, const ZProgramGraphStore *store, const ZProgramGraphResolutionFacts *resolution, const ZTargetInfo *target, const Command *command, long long *lower_ms_out, bool *graph_mir_used_out) {"));
const repositoryGraphCheckJsonRawBody = cBlock(main, "static void append_repository_graph_compiler_path_json");
const repositoryGraphCheckJsonBody = cCodeText(cBlock(main, "static void append_repository_graph_compiler_path_json"));
const repositoryGraphDefaultReadinessRawBody = cBlock(main, "static void append_repository_graph_default_readiness_json");
const directManifestGraphInputBody = cCodeText(cBlock(main, "static int resolve_direct_command_manifest_graph_input"));
const artifactGraphMirPrepRawBody = cTextWithoutComments(cBlock(programGraphMirRaw, "bool z_program_graph_prepare_artifact_mir_input"));
const artifactGraphMirPrepBody = cCodeText(cBlock(programGraphMirRaw, "bool z_program_graph_prepare_artifact_mir_input"));
const repositoryGraphMirPrepRawBody = cTextWithoutComments(cBlock(programGraphMirRaw, "bool z_program_graph_prepare_repository_store_mir_input"));
const repositoryGraphMirPrepBody = cCodeText(cBlock(programGraphMirRaw, "bool z_program_graph_prepare_repository_store_mir_input"));
const repositoryStateBody = cCodeText(cBlock(programGraphRepositoryRaw, "static RepositoryGraphState repo_graph_state"));
const repositoryStatusBody = cCodeText(cBlock(programGraphRepositoryRaw, "int z_repository_graph_status_command"));
const repositoryVerifyProjectionBody = cCodeText(cBlock(programGraphRepositoryRaw, "int z_repository_graph_verify_projection_command"));
const repositoryNeedsSourceGraphBody = cTextWithoutComments(cBlock(programGraphRepositoryRaw, "bool z_repository_graph_needs_source_graph"));
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
  targetManifest: {
    exactKeyMatcher: /\bmanifest_key_equals\s*\(/.test(targetSource),
    exactListMatcher: /\bmanifest_list_contains_token\s*\(/.test(targetSource),
    noAliasSubstringLookup: !/strstr\s*\(\s*targets\s*\[\s*i\s*\]\s*\.\s*aliases/.test(targetSource),
    noCapabilitySubstringLookup: !/strstr\s*\(\s*target\s*->\s*capabilities/.test(targetSource),
    linkerLabelHelper: /\btarget_linker_label\s*\(/.test(targetSource),
  },
  abiReport: {
    primitiveTable: /\bstatic\s+const\s+ZAbiPrimitive\s+abi_primitives\[\]/.test(abiReportSource),
    primitiveLookup: /\babi_report_primitive_for_type\s*\(/.test(abiReportSource),
    primitiveJsonIteratesTable: /\bappend_abi_primitives_json\s*\(/.test(abiReportSource) &&
      /\binclude_in_layout\b/.test(abiReportSource) &&
      /\babi_primitives_len\b/.test(abiReportSource),
    rawPrimitiveStringChecks: countMatches(
      abiReportText,
      /strcmp\s*\(\s*(?:type|zero_type)\s*,\s*"(?:Void|Bool|char|u8|i8|u16|i16|u32|i32|u64|i64|usize|isize|f32|f64)"\s*\)/g,
    ),
  },
  manifestToml: {
    fieldBindingTable: /\bTomlManifestFieldBinding\b/.test(manifestTomlSource),
    sharedFieldAssignment: /\btoml_assign_bound_field\s*\(/.test(manifestTomlSource),
    sharedParsedLineCleanup: /\btoml_parsed_line_free\s*\(/.test(manifestTomlSource),
    sharedDiagnostics: /\btoml_set_diag\s*\(/.test(manifestTomlSource),
    arrayParserChecksSkippedString: /\bconst\s+char\s+\*after_item\s*=\s*toml_skip_string\s*\(\s*cursor\s*\)\s*;\s*if\s*\(\s*!after_item\s*\)\s*break\s*;\s*cursor\s*=\s*toml_skip_ws\s*\(\s*after_item\s*\)/s.test(manifestTomlSource),
    rawFieldDispatchStringChecks: countMatches(
      manifestTomlText,
      /strcmp\s*\([^,]+,\s*"(?:package\.name|package\.version|targets\.cli\.(?:main|graph|kind)|path|version|targets|target|headers|include|lib|link|mode|pkg_config|pkgConfig)"\s*\)/g,
    ),
  },
  buildability: {
    valueSupportSeparatedModule: /\bz_build_value_supported\s*\(/.test(buildabilitySource) &&
      !/\bbuild_value_supported_generic\s*\(/.test(buildabilitySource),
    valueSupportBackendSplit: /\bbuild_value_supported_aarch64\s*\(/.test(buildabilityValueSupportSource) &&
      /\bbuild_value_supported_macho_x64\s*\(/.test(buildabilityValueSupportSource) &&
      /\bbuild_value_supported_generic\s*\(/.test(buildabilityValueSupportSource),
    sharedHostedRuntimePredicate: /\bbuild_backend_supports_hosted_runtime\s*\(/.test(buildabilityValueSupportSource),
    sharedJsonPredicates: /\bbuild_backend_supports_json_parse\s*\(/.test(buildabilityValueSupportSource) &&
      /\bbuild_backend_supports_json_validate\s*\(/.test(buildabilityValueSupportSource),
    sharedByteRuntimePredicate: /\bbuild_backend_has_byte_runtime\s*\(/.test(buildabilityValueSupportSource),
    sharedX64ByteViewCheckers: /\bbuild_check_x64_byte_view_ptr\s*\(/.test(buildabilityTargetsSource) &&
      /\bbuild_check_x64_byte_view_len\s*\(/.test(buildabilityTargetsSource) &&
      /\bBuildX64ByteViewDiagText\b/.test(buildabilityTargetsSource),
    legacyDuplicatedX64ByteViewCheckersRemoved: !/\bbuild_check_coff_byte_view_ptr\s*\(/.test(buildabilityTargetsSource) &&
      !/\bbuild_check_macho_x64_byte_view_ptr\s*\(/.test(buildabilityTargetsSource),
    dispatcherLines: lineCount(buildValueSupportedBody),
    targetValueBackendSplit: /\bbuild_check_linear_byte_view_target\s*\(/.test(buildabilityValueTargetsSource) &&
      /\bbuild_check_macho64_target_value\s*\(/.test(buildabilityValueTargetsSource) &&
      /\bbuild_aarch64_byte_operation\s*\(/.test(buildabilityValueTargetsSource),
    targetValueSharedPairCheck: /\bbuild_check_two_byte_views\s*\(/.test(buildabilityValueTargetsSource),
    targetValueDispatcherLines: lineCount(buildTargetValueBody),
  },
  directTarget: {
    ruleMatrix: /\bdirect_backend_rules\[\]/.test(targetBackendSource),
    executableUsesRuleMatrix: /return\s+direct_backend_for_target\s*\(\s*target\s*,\s*true\s*\)/.test(targetBackendSource),
    descriptorTable: /\bdirect_backend_descriptors\[\]/.test(targetBackendSource),
    emitKindParser: /\bZDirectEmitKind\b/.test(targetBackendSource) &&
      /\bdirect_emit_kind_from_text\s*\(/.test(targetBackendSource),
    executableTargetNameChecks: countMatches(directExeBackendBody, /target\s*->\s*name/g),
    mainExecutableEmitterStringChecks: countMatches(cCodeText(main), /zero-(?:elf64|elf-aarch64|macho64|coff-x64)-exe/g),
    mainObjectEmitterStringChecks: countMatches(cCodeText(main), /"zero-(?:elf64|elf-aarch64|macho64|coff-x64)"/g),
    mainRuntimeCacheKeyStringChecks: countMatches(cCodeText(main), /direct-(?:elf64|macho64)-object-runtime-link/g),
    mainDirectBackendDiagStringHelpers: countMatches(cCodeText(main), /static const char \*target_backend_(?:expected|help)\s*\(/g),
    mainBackendFromEmitterCalls: countMatches(cCodeText(main), /z_direct_backend_from_emitter\s*\(/g),
    mainDirectPathFromEmitterHelpers: countMatches(cCodeText(main), /direct_(?:object_path|linker_flavor)_for_emitter\s*\(/g),
    mainObjectFormatSymbolChecks: countMatches(cCodeText(main), /strcmp\s*\(\s*object_format\s*,\s*"coff"\s*\)/g),
    mainManualDirectToolchainJson: countMatches(cTextWithoutComments(main), /selectionSource\\":\\"direct-backend/g),
    mainDirectMetricMachoChecks: countMatches(cCodeText(main), /z_(?:direct_object_backend|direct_exe_backend)\s*\([^)]*\)\s*==\s*Z_DIRECT_BACKEND_MACHO64/g),
    mainDirectBackendNameHelpers: countMatches(cCodeText(main), /static const char \*(?:backend_blocker_backend_name|target_readiness_backend)\s*\(/g),
    mainDirectEmitSelectionHelpers: countMatches(cCodeText(main), /static (?:ZDirectBackend|const char \*)direct_emit_(?:backend|emitter)\s*\(/g),
    mainDirectReleaseTargetHelpers: countMatches(cCodeText(main), /static const char \*release_artifact_kind_for_emit\s*\(|selected_backend\s*==\s*Z_DIRECT_BACKEND_NONE\s*&&\s*selected_executable/g),
    mainDirectRuntimeLinkBlockers: countMatches(cCodeText(main), /runtime helpers currently require the Mach-O or ELF64 object link plan|HTTP runtime provider is host-only for direct executable links/g),
    mainDirectObjectSelectionHelpers: countMatches(cCodeText(main), /metadata_only_direct|runtime_linked_exe|direct_executable_artifact/g),
    mainDirectBuildToolchainSelectionHelpers: countMatches(cCodeText(main), /ZDirectBackend\s+direct_backend\s*=\s*z_direct_backend_for_emit_kind/g),
    mainDirectExecutableSelectionHelpers: countMatches(cCodeText(main), /z_direct_exe_backend\s*\(\s*target\s*\)|z_direct_backend_is_request_name\s*\(\s*command\.backend\s*\)|z_direct_requested_backend_matches\s*\(\s*command\.backend/g),
    mainDirectEmitDispatchHelpers: countMatches(cCodeText(main), /switch\s*\(\s*(?:object_backend|exe_backend)\s*\)|object_backend\s*==\s*Z_DIRECT_BACKEND_MACHO64/g),
    mainDirectBuildArtifactSelectionHelpers: countMatches(cCodeText(main), /z_direct_object_backend\s*\(\s*target\s*\)|z_direct_backend_artifact_path\s*\([^)]*\)|z_direct_backend_runtime_object_cache_key\s*\([^)]*\)/g),
    mainDirectReadinessSelectionHelpers: countMatches(cCodeText(main), /const char \*emitter\s*=\s*z_direct_object_emitter\s*\(\s*target\s*\)|z_direct_runtime_link_blocker\s*\(\s*target/g),
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
    objectUsesSharedWriter: /\bz_coff_write_object\s*\(\s*out\s*,\s*&image\s*\)/.test(coffX64Source) &&
      /\bz_coff_write_object\s*\(\s*out\s*,\s*&image\s*\)/.test(coffAarch64Source),
    executableUsesSharedWriter: /\bz_coff_write_pe64_executable\s*\(\s*out\s*,\s*&image\s*\)/.test(coffX64Source) &&
      /\bz_coff_write_pe64_executable\s*\(\s*out\s*,\s*&image\s*\)/.test(coffAarch64Source),
    archFilesWithLocalContainerWriters: [
      ["native/zero-c/src/emit_coff.c", coffX64Source],
      ["native/zero-c/src/emit_coff_aarch64.c", coffAarch64Source],
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
    objectUsesSharedWriter: /\bz_macho_write_object64\s*\(\s*out\s*,\s*&image\s*\)/.test(machoArm64Source) &&
      /\bz_macho_write_object64\s*\(\s*out\s*,\s*&image\s*\)/.test(machoX64Source),
    executableUsesSharedWriter: /\bz_macho_write_executable64\s*\(\s*out\s*,\s*&image\s*\)/.test(machoArm64Source) &&
      /\bz_macho_write_executable64\s*\(\s*out\s*,\s*&image\s*\)/.test(machoX64Source),
    archFilesWithLocalContainerWriters: [
      ["native/zero-c/src/emit_macho64.c", machoArm64Source],
      ["native/zero-c/src/emit_macho_x64.c", machoX64Source],
    ]
      .filter(([, text]) => /\bappend_fixed\s*\(|\bmacho_append_code_signature\s*\(|\bmacho_sha256_hash\s*\(|\bpatch_bytes\s*\(|0xfeedfacf|0x80000022/.test(text))
      .map(([path]) => path),
    patchStateModule: /\bz_macho_record_value_runtime_patch\s*\(/.test(machoEmitStateSource) &&
      /\bz_macho_record_instr_runtime_patch\s*\(/.test(machoEmitStateSource) &&
      /\bz_macho_append_runtime_relocations\s*\(/.test(machoEmitStateSource),
    archFileUsesPatchStateModule: /\bz_macho_record_value_runtime_patch\s*\(/.test(machoArm64Source) &&
      /\bz_macho_append_runtime_relocations\s*\(/.test(machoArm64Source) &&
      /\bz_macho_has_unsupported_exe_runtime_patches\s*\(/.test(machoArm64Source) &&
      /\bz_macho_record_call_patch\s*\(/.test(machoX64Source) &&
      /\bz_macho_record_data_patch\s*\(/.test(machoX64Source) &&
      /\bz_macho_record_instr_runtime_patch\s*\(/.test(machoX64Source) &&
      /\bz_macho_append_runtime_relocations\s*\(/.test(machoX64Source) &&
      /\bz_macho_has_unsupported_exe_runtime_patches\s*\(/.test(machoX64Source),
    archFilesWithLocalPatchState: [
      ["native/zero-c/src/emit_macho64.c", machoArm64Source],
      ["native/zero-c/src/emit_macho_x64.c", machoX64Source],
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
    elfUsesSharedEncodingPrimitives: /\bz_x64_emit_prologue\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_epilogue\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_mov_eax_u32\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_jcc32_placeholder\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_syscall\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_byte_copy_min_loop\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_byte_fill_loop\s*\(/.test(elfX64Source) &&
      /\bz_x64_emit_byte_eq_loop\s*\(/.test(elfX64Source) &&
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
      /\bz_x64_emit_mov_eax_from_ecx\s*\(/.test(coffX64Source) &&
      (/\bz_x64_emit_shl_rcx_imm8\s*\(/.test(coffX64Source) || /\bz_x64_emit_shl_reg_imm8\s*\(/.test(coffX64Source)) &&
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
    machoUsesSharedEncodingPrimitives: /\bz_x64_append_u8\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_rbp_disp_reg\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_jcc32_placeholder\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_jmp32_placeholder\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_prologue\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_epilogue\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_mov_eax_u32\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_mov_rcx_from_rax\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_mov_reg_u32\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_add_rax_rcx\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_sub_rax_rcx\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_imul_rax_rcx\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_div_rax_rcx\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_cmp_rax_rcx_to_bool\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_call32_placeholder\s*\(/.test(machoX64Source) &&
      /\bz_x64_emit_syscall\s*\(/.test(machoX64Source) &&
      /\bz_x64_patch_rel32\s*\(/.test(machoX64Source),
    formatFilesWithLocalEncodingPrimitives: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_coff.c", coffX64Source],
      ["native/zero-c/src/emit_macho_x64.c", machoX64Source],
    ]
      .filter(([, text]) => /\bstatic\s+(?:void|size_t)\s+(?:z_x64_|elf_append_u(?:8|32|64)|elf_append_bytes|elf_append_zeros|elf_align|elf_pad_to|append_u8|append_u32le|append_bytes)\b/.test(text))
      .map(([path]) => path),
    formatFilesWithRawStackRegisterBytes: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_coff.c", coffX64Source],
      ["native/zero-c/src/emit_macho_x64.c", machoX64Source],
    ]
      .filter(([, text]) => /\bz_x64_append_u8\s*\(\s*(?:code|text)\s*,\s*0x5[0-9a-f]\s*\)/i.test(text))
      .map(([path]) => path),
    formatFilesWithRawRegisterImmediateBytes: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_coff.c", coffX64Source],
      ["native/zero-c/src/emit_macho_x64.c", machoX64Source],
    ]
      .filter(([, text]) => hasRawX64RegisterImmediateBytes(text))
      .map(([path]) => path),
    formatFilesWithRawArithmeticBytes: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_coff.c", coffX64Source],
      ["native/zero-c/src/emit_macho_x64.c", machoX64Source],
    ]
      .filter(([, text]) => hasRawX64ArithmeticBytes(text))
      .map(([path]) => path),
    formatFilesWithRawCompareTestBytes: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_coff.c", coffX64Source],
      ["native/zero-c/src/emit_macho_x64.c", machoX64Source],
    ]
      .filter(([, text]) => hasRawX64CompareTestBytes(text))
      .map(([path]) => path),
    formatFilesWithRawIndexedMemoryBytes: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_coff.c", coffX64Source],
      ["native/zero-c/src/emit_macho_x64.c", machoX64Source],
    ]
      .filter(([, text]) => hasRawX64IndexedMemoryBytes(text))
      .map(([path]) => path),
    formatFilesWithRawPointerMemoryBytes: [
      ["native/zero-c/src/emit_elf64.c", elfX64Source],
      ["native/zero-c/src/emit_coff.c", coffX64Source],
      ["native/zero-c/src/emit_macho_x64.c", machoX64Source],
    ]
      .filter(([, text]) => hasRawX64PointerMemoryBytes(text))
      .map(([path]) => path),
  },
  aarch64: {
    sharedEncodingPrimitives: /\bz_aarch64_emit_movz_w\s*\(/.test(aarch64EmitSource) &&
      /\bz_aarch64_emit_bl_placeholder\s*\(/.test(aarch64EmitSource) &&
      /\bz_aarch64_patch_branch26\s*\(/.test(aarch64EmitSource),
    directLayerUsesSharedEncodingPrimitives: /\bz_aarch64_emit_literal_return\s*\(/.test(aarch64DirectSource) &&
      /\bz_aarch64_emit_byte_eq_loop\s*\(/.test(aarch64DirectSource) &&
      /\bz_aarch64_patch_branch26\s*\(/.test(aarch64DirectSource),
    elfUsesSharedEncodingPrimitives: /\bz_aarch64_direct_emit_function_text\s*\(/.test(elfAarch64Source) &&
      /\bz_aarch64_direct_stack_bytes_from_ir\s*\(/.test(elfAarch64Source),
    coffUsesSharedEncodingPrimitives: /\bz_aarch64_direct_emit_function_text\s*\(/.test(coffAarch64Source) &&
      /\bz_aarch64_direct_find_main\s*\(/.test(coffAarch64Source),
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
const programGraph = {
  mainRawGraphCommandOutWrites: countMatches(
    cCodeText(main),
    /\bz_write_file\s*\(\s*command->out\s*,\s*graph\.data/g,
  ),
  sourceCommandGraphMirFallbackRemoved: !/z_program_graph_prepare_source_mir_input\s*\(/.test(cCodeText(cBlock(main, "int main(int argc, char **argv)"))) &&
    !/z_program_graph_prepare_source_mir_input\s*\(/.test(programGraphCompileSource) &&
    !/z_program_graph_source_command_uses_graph_mir\s*\(/.test(programGraphCompileSource),
  sourceCommandGraphProgramPrepRemoved: !/z_program_graph_lower_to_program_with_source\s*\(/.test(programGraphCompileSource) &&
    !/\*\s*program\s*=\s*graph_program\s*;/.test(programGraphCompileSource),
  optedInRepositoryGraphClaimRemoved: !/ready-for-opted-in-repository-graph-input/.test(cCodeText(main)) &&
    !/ready-for-opted-in-repository-graph-input/.test(programGraphRepositoryRaw),
  repositoryStoreCommandPolicyCentralized: /repository_store_input/.test(programGraphCommandSource) &&
    /z_program_graph_command_can_use_repository_store\s*\(/.test(programGraphCommandSource) &&
    /z_program_graph_command_can_use_repository_store\s*\(/.test(programGraphCommandHeaderRaw) &&
    /z_program_graph_command_can_use_repository_store\s*\(\s*command->kind\s*\)/.test(cCodeText(main)) &&
    !/static\s+bool\s+graph_command_can_use_repository_store\s*\(/.test(cCodeText(main)),
  repositoryStoreCompilerTables: /z_program_graph_store_table_counts_for_graph\s*\(/.test(programGraphStoreTablesSource) &&
    /ZProgramGraphStoreTableCounts/.test(programGraphStoreTablesSource),
  repositoryStoreMetadataSerialized: /compilerStore schemaVersion:1/.test(programGraphStoreTablesRaw) &&
    /compilerTables schema:/.test(programGraphStoreTablesRaw) &&
    /compilerHashInputs graphHashExcludes:/.test(programGraphStoreTablesRaw),
  repositoryStoreMetadataValidated: /z_program_graph_store_compiler_metadata_matches\s*\(/.test(programGraphStoreSource),
  repositoryStatusCompilerStoreFacts: /compilerStore/.test(programGraphRepositoryRaw) &&
    /sourceFreeInspection/.test(programGraphRepositoryRaw) &&
    /z_program_graph_store_append_table_counts_json\s*\(/.test(programGraphRepositorySource),
  repositoryStatusProjectionValidity: /repo_projection_validity_label\s*\(/.test(programGraphRepositorySource) &&
    /projectionValidity/.test(programGraphRepositoryRaw),
  repositoryStatusSourceGraphFree:
    !/\bsource_graph\b/.test(repositoryStatusBody) &&
    !/\bsource_graph_diag\b/.test(repositoryStatusBody) &&
    !/z_program_graph_store_graph_matches_source\s*\(/.test(repositoryStateBody) &&
    !/\bgraph_checked\b|\bgraph_current\b|\bgraph_error\b/.test(programGraphRepositorySource),
  repositoryVerifyProjectionSourceFree: /z_program_graph_projection_sources_match\s*\(/.test(repositoryVerifyProjectionBody) &&
    !/z_program_graph_store_graph_matches_source\s*\(/.test(repositoryVerifyProjectionBody) &&
    !/source_graph\s*&&/.test(repositoryVerifyProjectionBody) &&
    /REPO_GRAPH_REPAIR_IMPORT_OR_EXPORT/.test(repositoryVerifyProjectionBody) &&
    /REPO_KIND\s*\(\s*kind,\s*"import"\s*\)\)\s*return\s+true/.test(repositoryNeedsSourceGraphBody) &&
    !/REPO_KIND\s*\(\s*kind,\s*"verify-projection"\s*\)/.test(repositoryNeedsSourceGraphBody) &&
    !/REPO_KIND\s*\(\s*kind,\s*"status"\s*\)/.test(repositoryNeedsSourceGraphBody),
  repositoryGraphCheckNative: /z_repository_graph_require_compiler_store\s*\(/.test(repositoryGraphCheckBody) &&
    /z_program_graph_store_load_path\s*\(/.test(repositoryGraphCheckBody) &&
    /z_program_graph_collect_resolution_facts\s*\(/.test(repositoryGraphCheckBody) &&
    /print_repository_graph_check_json_success\s*\(/.test(repositoryGraphCheckBody),
  repositoryGraphCheckNoProgramLowering: !/z_program_graph_lower_to_program_with_source\s*\(/.test(repositoryGraphCheckBody) &&
    !/z_program_graph_prepare_source_mir_input\s*\(/.test(repositoryGraphCheckBody),
  repositoryGraphCheckNoLegacyChecker: !/z_check_program\s*\(/.test(repositoryGraphCheckBody) &&
    !/load_graph_from_checked_current_source\s*\(/.test(repositoryGraphCheckBody),
  repositoryGraphCheckStoredTypedFactsAuthority: /graph_stored_compiler_input_ok\s*\(/.test(repositoryGraphCheckBody) &&
    /semanticDiagnosticsEnforced\\":false/.test(repositoryGraphCheckJsonRawBody) &&
    /semanticDiagnosticsAuthority\\":\\"stored-typed-graph-facts/.test(repositoryGraphCheckJsonRawBody),
  repositoryGraphCheckReadinessNoProgramReconstruction:
    /z_program_graph_prepare_repository_store_mir_input[\s\S]*command\s*\?\s*command->backend\s*:\s*NULL[\s\S]*false[\s\S]*&readiness_program/.test(repositoryGraphTargetReadinessBody) &&
    !/z_program_graph_lower_to_program_with_source\s*\(/.test(repositoryGraphTargetReadinessBody) &&
    !/z_check_program\s*\(/.test(repositoryGraphTargetReadinessBody),
  repositoryGraphCheckReportsSemanticFacts: /z_program_graph_append_semantics_json\s*\(/.test(repositoryGraphCheckJsonBody),
  repositoryGraphCheckReportsGraphMirState: /graphNativeCheckerUsed\\":true/.test(repositoryGraphCheckJsonRawBody) &&
    /graphHirToMirUsed/.test(repositoryGraphCheckJsonRawBody) &&
    /graphMir/.test(repositoryGraphDefaultReadinessRawBody),
  artifactGraphCheckNative: /z_program_graph_load\s*\(/.test(artifactGraphCheckBody) &&
    /z_program_graph_name_contracts_ok\s*\(/.test(artifactGraphCheckBody) &&
    /z_program_graph_collect_resolution_facts\s*\(/.test(artifactGraphCheckBody) &&
    /graph_check_resolution_ok\s*\(/.test(artifactGraphCheckBody) &&
    /graph_check_target_capabilities_ok\s*\(/.test(artifactGraphCheckBody),
  artifactGraphCheckNoProgramLowering: !/z_program_graph_lower_to_program_with_source\s*\(/.test(artifactGraphCheckBody) &&
    !/z_program_graph_prepare_source_mir_input\s*\(/.test(artifactGraphCheckBody),
  artifactGraphCheckNoLegacyChecker: !/z_check_program\s*\(/.test(artifactGraphCheckBody) &&
    !/load_graph_input_for_read\s*\(/.test(artifactGraphCheckBody),
  artifactGraphCheckReportsSemanticFacts: /z_program_graph_append_semantics_json\s*\(/.test(artifactGraphCheckJsonRawBody),
  artifactGraphCheckReportsGraphMirState: /graphNativeCheckerUsed/.test(artifactGraphCheckJsonRawBody) &&
    /graphHirToMirUsed/.test(artifactGraphCheckJsonRawBody) &&
    /z_lower_program_graph_with_source\s*\(/.test(artifactGraphReadinessBody),
  graphTestNativeRunner: /z_program_graph_run_tests_direct\s*\(/.test(main) &&
    /testBackend\\": \\"direct-program-graph/.test(programGraphTestRaw),
  graphTestNoProgramLowering: !/z_program_graph_lower_to_program_with_source\s*\(/.test(programGraphTestSource) &&
    !/ir_graph_lower_checked_program\s*\(/.test(programGraphTestSource) &&
    !/z_check_program\s*\(/.test(programGraphTestSource),
  graphTestSemanticContracts: /z_program_graph_collect_resolution_facts\s*\(/.test(programGraphTestSource) &&
    /z_program_graph_semantic_contracts_ok\s*\(/.test(programGraphTestSource) &&
    /pgt_target_capabilities_ok\s*\(/.test(programGraphTestSource),
  graphTestRepositoryStoreInput: /z_program_graph_store_load_path\s*\(/.test(programGraphTestSource) &&
    /sourceProjectionState/.test(programGraphTestRaw),
  graphTestMutableControlFlow: /Z_PROGRAM_GRAPH_NODE_ASSIGNMENT/.test(programGraphTestSource) &&
    /Z_PROGRAM_GRAPH_NODE_WHILE/.test(programGraphTestSource) &&
    /pgt_env_assign\s*\(/.test(programGraphTestSource),
  repositoryGraphCheckDefaultReadiness: /defaultReadiness/.test(main) &&
    /compilerInputReady/.test(repositoryGraphDefaultReadinessRawBody) &&
    /sourceFreeCompile/.test(repositoryGraphDefaultReadinessRawBody) &&
    /targetReadinessOk/.test(repositoryGraphDefaultReadinessRawBody),
  repositoryGraphCheckPerformanceBudget: /cacheMs/.test(repositoryGraphDefaultReadinessRawBody) &&
    /validateMs/.test(repositoryGraphDefaultReadinessRawBody) &&
    /validationInLoad/.test(repositoryGraphDefaultReadinessRawBody) &&
    /withinBudget/.test(repositoryGraphDefaultReadinessRawBody),
  repositoryGraphCacheKeyFacts: /graphKeyInputs/.test(main) &&
    /parserArtifactsInKey/.test(main) &&
    /nodeHashes/.test(main) &&
    /typeFacts/.test(main) &&
    /symbolFacts/.test(main) &&
    /modulePaths/.test(main) &&
    /importPaths/.test(main) &&
    !/GRAPH_CACHE_INPUTS_(?:PARSE|CHECK|SPECIALIZATION|OBJECT|AGGREGATE)\s*=\s*"\[[^\]]*sourceFiles/.test(main),
  repositoryGraphMirPrepMappedFinalMir: /z_mir_binary_load_path\s*\(/.test(repositoryGraphMirPrepBody) &&
    /z_mir_binary_write_path\s*\(/.test(repositoryGraphMirPrepBody) &&
    /z_lower_program_graph_with_source\s*\(/.test(repositoryGraphMirPrepBody) &&
    /source\s*->\s*lowering\s*=\s*"mapped-final-mir"/.test(repositoryGraphMirPrepRawBody) &&
    /ir_graph_set_mapped_mir_cache_facts\s*\(/.test(repositoryGraphMirPrepRawBody),
  repositoryGraphMirPrepImmediateCacheHit: /require_checked_program/.test(repositoryGraphMirPrepRawBody) &&
    /bool\s+checked\s*=\s*ir_graph_lower_checked_program\s*\(/.test(repositoryGraphMirPrepRawBody) &&
    /ir_graph_set_mapped_mir_cache_facts\s*\(\s*input,\s*&mir_cache,\s*true,\s*false,\s*!require_checked_program,\s*require_checked_program\s*\)/.test(repositoryGraphMirPrepRawBody),
  artifactGraphMirPrepMappedFinalMir: /z_mir_binary_load_path\s*\(/.test(artifactGraphMirPrepBody) &&
    /z_mir_binary_write_path\s*\(/.test(artifactGraphMirPrepBody) &&
    /z_lower_program_graph_with_source\s*\(/.test(artifactGraphMirPrepBody) &&
    /source\s*->\s*lowering\s*=\s*"mapped-final-mir"/.test(artifactGraphMirPrepRawBody) &&
    /ir_graph_set_mapped_mir_cache_facts\s*\(/.test(artifactGraphMirPrepRawBody),
  artifactGraphMirPrepImmediateCacheHit: !/require_checked_program/.test(artifactGraphMirPrepRawBody) &&
    !/ir_graph_lower_checked_program\s*\(/.test(artifactGraphMirPrepRawBody) &&
    /ir_graph_set_mapped_mir_cache_facts\s*\(\s*input,\s*&mir_cache,\s*true,\s*false,\s*true,\s*false\s*\)/.test(artifactGraphMirPrepRawBody),
  repositoryGraphMirCacheFacts: /mappedFinalMir/.test(main) &&
    /borrowedStorage/.test(main) &&
    /memoryMapped/.test(main) &&
    /mapped_mir_cache_written/.test(main) &&
    /codegenImmediate/.test(main) &&
    /programReconstructed/.test(main),
  repositoryGraphMirPrepSourceFreeFirst: repositoryGraphMirPrepBody.indexOf("z_mir_binary_load_path(") >= 0 &&
    repositoryGraphMirPrepBody.indexOf("ir_graph_lower_checked_program(&store.graph") >= 0 &&
    repositoryGraphMirPrepBody.indexOf("z_mir_binary_load_path(") < repositoryGraphMirPrepBody.indexOf("ir_graph_lower_checked_program(&store.graph") &&
    /IrProgram graph_ir = z_lower_program_graph_with_source\(&store\.graph, input, target\)/.test(repositoryGraphMirPrepBody),
  repositoryGraphMirPrepNoStdHelperBridge: !/z_program_graph_append_source_std_functions/.test(repositoryGraphMirPrepRawBody) &&
    !/z_program_graph_steal_source_std/.test(repositoryGraphMirPrepRawBody) &&
    /source\s*->\s*lowering\s*=\s*"mapped-final-mir"/.test(repositoryGraphMirPrepRawBody),
  repositoryGraphMirPrepReportsUnsupportedFacts: /ir_graph_init_lowering_diag\s*\(/.test(repositoryGraphMirPrepBody) &&
    /graph_ir\.mir_valid/.test(repositoryGraphMirPrepBody),
  repositoryGraphMirPrepNoInvalidMirProgramFallback:
    (repositoryGraphMirPrepRawBody.match(/ir_graph_lower_checked_program\s*\(&store\.graph/g) ?? []).length === 2 &&
    /else\s*\{\s*if\s*\(\s*diag\s*&&\s*diag->code\s*==\s*0\s*\)\s*ir_graph_init_lowering_diag/.test(repositoryGraphMirPrepRawBody),
  artifactGraphMirPrepNoInvalidMirProgramFallback:
    !/ir_graph_lower_checked_program\s*\(/.test(artifactGraphMirPrepRawBody) &&
    /else\s*\{\s*if\s*\(\s*diag\s*&&\s*diag->code\s*==\s*0\s*\)\s*ir_graph_init_lowering_diag/.test(artifactGraphMirPrepRawBody),
  artifactGraphSizeNoProgramReconstruction:
    /z_program_graph_prepare_artifact_mir_input\s*\(/.test(artifactGraphSizeBody) &&
    !/z_program_graph_lower_to_program_with_source\s*\(/.test(artifactGraphSizeBody) &&
    !/z_check_program\s*\(/.test(artifactGraphSizeBody) &&
    !/z_program_graph_from_program\s*\(/.test(artifactGraphSizeBody) &&
    !/load_graph_input_for_read\s*\(/.test(artifactGraphSizeBody) &&
    !/graph_size_artifact_command\s*&&\s*diag\.code\s*==\s*2004/.test(main),
  artifactGraphProgramPrepRemoved:
    !/z_program_graph_prepare_artifact_input\s*\(/.test(main) &&
    !/z_program_graph_prepare_artifact_input\s*\(/.test(programGraphBuildRaw) &&
    !/z_program_graph_prepare_artifact_input\s*\(/.test(programGraphBuildHeaderRaw) &&
    !/\bdirect_graph_source_command\b/.test(main) &&
    !/\bgraph_test_command\b/.test(main),
  repositoryCompilerInputSourceFree: /z_repository_graph_verify_compiler_input\s*\(\s*command->input\s*,\s*target\s*,\s*command->json\s*,\s*&store_path\s*\)/.test(directManifestGraphInputBody) &&
    !/load_graph_from_checked_current_source\s*\(/.test(directManifestGraphInputBody) &&
    !/SourceInput\s+source_input/.test(directManifestGraphInputBody),
  repositoryCompilerInputProjectionStatus: /projectionState/.test(programGraphRepositoryInputRaw) &&
    /projectionValidity/.test(programGraphRepositoryInputRaw) &&
    /source-missing/.test(programGraphRepositoryInputRaw) &&
    /z_program_graph_projection_state_label\s*\(/.test(programGraphRepositoryInputSource),
  repositoryGraphSourceLocationsNotSemanticMatch: !/store_source_locations_match_graph\s*\(/.test(programGraphStoreSource) &&
    /store_source_paths_match_graph\s*\(/.test(programGraphStoreSource),
  repositoryProjectionValidationIgnoresSourceLocation: !/expected->line\s*!=\s*actual->line|expected->column\s*!=\s*actual->column|actual->line\s*==\s*expected->line|actual->column\s*==\s*expected->column/.test(programGraphProjectionValidateSource),
  repositoryArtifactProjectionState: /sourceProjectionState/.test(main) &&
    /source_projection_state/.test(programGraphMirRaw),
};
const violations = budgetViolations(files, allLargeFunctions, stdlib, backendFormats, programGraph);

const report = {
  schema: 1,
  files,
  largeFunctions: allLargeFunctions.slice(0, 25),
  stdlib,
  programGraph,
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
