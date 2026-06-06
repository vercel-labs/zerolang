#include "program_graph_mir_std.h"

#include "canonical_text.h"
#include "program_graph_format.h"
#include "program_graph_lower.h"
#include "std_source.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *source_ir_grow_tracked_items(IrProgram *ir, void *items, size_t len, size_t *cap, size_t initial, size_t item_size) {
  if (len + 1 > *cap) {
    size_t next_cap = z_grow_capacity(*cap, len + 1, initial);
    void *next_items = z_checked_reallocarray(items, next_cap, item_size);
    if (ir) ir->mir_bytes += next_cap * item_size;
    *cap = next_cap;
    return next_items;
  }
  return items;
}

static bool source_ir_function_is_std_helper(const IrFunction *fun) {
  const char *prefix = "__zero_std_";
  return fun && fun->name && strncmp(fun->name, prefix, strlen(prefix)) == 0;
}

typedef struct {
  unsigned old_offset;
  unsigned new_offset;
} SourceIrDataOffsetMap;

typedef struct {
  unsigned old_index;
  unsigned new_index;
} SourceIrExternalIndexMap;

static size_t source_ir_max_size(size_t left, size_t right) { return left > right ? left : right; }

static bool source_ir_remap_data_offset(unsigned *offset, const SourceIrDataOffsetMap *map, size_t map_len) {
  if (!offset || *offset == 0) return true;
  for (size_t i = 0; i < map_len; i++) {
    if (map[i].old_offset == *offset) {
      *offset = map[i].new_offset;
      return true;
    }
  }
  return true;
}

static bool source_ir_remap_external_index(unsigned *index, const SourceIrExternalIndexMap *map, size_t map_len) {
  if (!index) return true;
  for (size_t i = 0; i < map_len; i++) {
    if (map[i].old_index == *index) {
      *index = map[i].new_index;
      return true;
    }
  }
  return false;
}

static bool source_ir_remap_value_indices(IrValue *value, const unsigned *function_map, size_t function_map_len, const SourceIrDataOffsetMap *data_map, size_t data_map_len, const SourceIrExternalIndexMap *external_map, size_t external_map_len) {
  if (!value) return true;
  if ((value->kind == IR_VALUE_STRING_LITERAL || value->kind == IR_VALUE_ARRAY_BYTE_VIEW) &&
      !source_ir_remap_data_offset(&value->data_offset, data_map, data_map_len)) {
    return false;
  }
  if (value->kind == IR_VALUE_CALL) {
    if (value->external_call) {
      if (!source_ir_remap_external_index(&value->external_index, external_map, external_map_len)) return false;
    } else {
      if (value->callee_index >= function_map_len || function_map[value->callee_index] == UINT_MAX) return false;
      value->callee_index = function_map[value->callee_index];
    }
  }
  if (!source_ir_remap_value_indices(value->index, function_map, function_map_len, data_map, data_map_len, external_map, external_map_len) ||
      !source_ir_remap_value_indices(value->left, function_map, function_map_len, data_map, data_map_len, external_map, external_map_len) ||
      !source_ir_remap_value_indices(value->right, function_map, function_map_len, data_map, data_map_len, external_map, external_map_len)) {
    return false;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (!source_ir_remap_value_indices(value->args[i], function_map, function_map_len, data_map, data_map_len, external_map, external_map_len)) return false;
  }
  return true;
}

static bool source_ir_remap_instr_indices(IrInstr *instrs, size_t len, const unsigned *function_map, size_t function_map_len, const SourceIrDataOffsetMap *data_map, size_t data_map_len, const SourceIrExternalIndexMap *external_map, size_t external_map_len) {
  for (size_t i = 0; i < len; i++) {
    IrInstr *instr = &instrs[i];
    if (!source_ir_remap_value_indices(instr->value, function_map, function_map_len, data_map, data_map_len, external_map, external_map_len) ||
        !source_ir_remap_value_indices(instr->index, function_map, function_map_len, data_map, data_map_len, external_map, external_map_len) ||
        !source_ir_remap_instr_indices(instr->then_instrs, instr->then_len, function_map, function_map_len, data_map, data_map_len, external_map, external_map_len) ||
        !source_ir_remap_instr_indices(instr->else_instrs, instr->else_len, function_map, function_map_len, data_map, data_map_len, external_map, external_map_len)) {
      return false;
    }
  }
  return true;
}

bool z_program_graph_steal_source_std_helpers(IrProgram *dest, IrProgram *source) {
  if (!dest || !source || source->function_len == 0) return true;
  unsigned *function_map = z_checked_malloc(sizeof(unsigned) * source->function_len);
  for (size_t i = 0; i < source->function_len; i++) function_map[i] = UINT_MAX;

  SourceIrDataOffsetMap *data_map = NULL;
  size_t data_map_len = 0;
  size_t data_map_cap = 0;
  SourceIrExternalIndexMap *external_map = NULL;
  size_t external_map_len = 0;
  size_t external_map_cap = 0;
  for (size_t i = 0; i < source->data_segment_len; i++) {
    unsigned new_offset = 0;
    if (!z_program_graph_ir_add_readonly_data(dest, source->data_segments[i].bytes, source->data_segments[i].len, 1, 1, &new_offset)) {
      free(function_map);
      free(data_map);
      free(external_map);
      return false;
    }
    data_map = source_ir_grow_tracked_items(dest, data_map, data_map_len, &data_map_cap, 4, sizeof(SourceIrDataOffsetMap));
    data_map[data_map_len++] = (SourceIrDataOffsetMap){.old_offset = source->data_segments[i].offset, .new_offset = new_offset};
  }

  for (size_t i = 0; i < source->external_function_len; i++) {
    dest->external_functions = source_ir_grow_tracked_items(dest, dest->external_functions, dest->external_function_len, &dest->external_function_cap, 4, sizeof(IrExternalFunction));
    unsigned new_index = (unsigned)dest->external_function_len;
    dest->external_functions[dest->external_function_len++] = source->external_functions[i];
    source->external_functions[i] = (IrExternalFunction){0};
    external_map = source_ir_grow_tracked_items(dest, external_map, external_map_len, &external_map_cap, 4, sizeof(SourceIrExternalIndexMap));
    external_map[external_map_len++] = (SourceIrExternalIndexMap){.old_index = (unsigned)i, .new_index = new_index};
  }

  size_t helper_start = dest->function_len;
  for (size_t i = 0; i < source->function_len; i++) {
    if (!source_ir_function_is_std_helper(&source->functions[i])) continue;
    dest->functions = source_ir_grow_tracked_items(dest, dest->functions, dest->function_len, &dest->function_cap, 4, sizeof(IrFunction));
    function_map[i] = (unsigned)dest->function_len;
    dest->functions[dest->function_len++] = source->functions[i];
    source->functions[i] = (IrFunction){0};
  }

  for (size_t i = helper_start; i < dest->function_len; i++) {
    if (!source_ir_remap_instr_indices(dest->functions[i].instrs, dest->functions[i].instr_len, function_map, source->function_len, data_map, data_map_len, external_map, external_map_len)) {
      free(function_map);
      free(data_map);
      free(external_map);
      return false;
    }
  }
  dest->direct_allocator_helper_count = source_ir_max_size(dest->direct_allocator_helper_count, source->direct_allocator_helper_count);
  dest->direct_buffer_helper_count = source_ir_max_size(dest->direct_buffer_helper_count, source->direct_buffer_helper_count);
  dest->direct_runtime_helper_count = source_ir_max_size(dest->direct_runtime_helper_count, source->direct_runtime_helper_count);
  dest->direct_host_runtime_import_count = source_ir_max_size(dest->direct_host_runtime_import_count, source->direct_host_runtime_import_count);
  dest->direct_http_runtime_import_count = source_ir_max_size(dest->direct_http_runtime_import_count, source->direct_http_runtime_import_count);
  dest->direct_c_import_call_count += source->direct_c_import_call_count;
  free(function_map);
  free(data_map);
  free(external_map);
  return true;
}

static char *source_expr_callee_name(const Expr *expr) {
  if (!expr) return z_strdup("");
  if (expr->kind == EXPR_IDENT) return z_strdup(expr->text ? expr->text : "");
  if (expr->kind == EXPR_MEMBER) {
    char *left = source_expr_callee_name(expr->left);
    size_t left_len = strlen(left);
    size_t text_len = strlen(expr->text ? expr->text : "");
    char *name = z_checked_malloc(left_len + text_len + 2);
    snprintf(name, left_len + text_len + 2, "%s.%s", left, expr->text ? expr->text : "");
    free(left);
    return name;
  }
  return z_strdup("");
}

static bool source_program_has_function(const Program *program, const char *name) {
  for (size_t i = 0; program && name && i < program->functions.len; i++) {
    if (strcmp(program->functions.items[i].name ? program->functions.items[i].name : "", name) == 0) return true;
  }
  return false;
}

static Function *source_program_find_function(Program *program, const char *name) {
  for (size_t i = 0; program && name && i < program->functions.len; i++) {
    if (strcmp(program->functions.items[i].name ? program->functions.items[i].name : "", name) == 0) return &program->functions.items[i];
  }
  return NULL;
}

static void source_push_function(Program *program, Function *fun) {
  if (!program || !fun) return;
  if (program->functions.len == program->functions.cap) {
    size_t next_cap = z_grow_capacity(program->functions.cap, program->functions.len + 1, 8);
    program->functions.items = z_checked_reallocarray(program->functions.items, next_cap, sizeof(Function));
    program->functions.cap = next_cap;
  }
  program->functions.items[program->functions.len++] = *fun;
  *fun = (Function){0};
}

static void source_std_diag(ZDiag *diag, const ZStdSourceModule *module, const char *target_name) {
  if (!diag) return;
  memset(diag, 0, sizeof(*diag));
  diag->code = 3011;
  diag->path = module ? module->path : NULL;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "graph-backed std helper '%s' is missing", target_name ? target_name : "");
  snprintf(diag->expected, sizeof(diag->expected), "embedded std helper function");
  snprintf(diag->actual, sizeof(diag->actual), "missing helper implementation");
  snprintf(diag->help, sizeof(diag->help), "keep std_source.c target mappings aligned with embedded stdlib source");
}

static bool source_append_std_function(Program *program, const ZStdSourceModule *module, const char *target_name, size_t *appended, ZDiag *diag);
static bool source_append_stmt_vec_std_functions(Program *program, const StmtVec *body, const ZStdSourceModule *current_module, size_t *appended, ZDiag *diag);

static bool source_append_expr_std_functions(Program *program, const Expr *expr, const ZStdSourceModule *current_module, size_t *appended, ZDiag *diag) {
  if (!expr || (diag && diag->code != 0)) return !diag || diag->code == 0;
  if (expr->kind == EXPR_CALL) {
    char *callee = source_expr_callee_name(expr->left);
    const ZStdSourceModule *callee_module = z_std_source_module_for_public_call(callee);
    const char *target_name = z_std_source_target_for_public_call(callee);
    bool ok = true;
    if (callee_module && target_name) {
      ok = source_append_std_function(program, callee_module, target_name, appended, diag);
    } else if (current_module && strncmp(callee ? callee : "", "__zero_", strlen("__zero_")) == 0) {
      ok = source_append_std_function(program, current_module, callee, appended, diag);
    }
    free(callee);
    if (!ok) return false;
  }
  if (!source_append_expr_std_functions(program, expr->left, current_module, appended, diag) ||
      !source_append_expr_std_functions(program, expr->right, current_module, appended, diag)) return false;
  for (size_t i = 0; i < expr->args.len; i++) {
    if (!source_append_expr_std_functions(program, expr->args.items[i], current_module, appended, diag)) return false;
  }
  for (size_t i = 0; i < expr->fields.len; i++) {
    if (!source_append_expr_std_functions(program, expr->fields.items[i].value, current_module, appended, diag)) return false;
  }
  return true;
}

static bool source_append_stmt_std_functions(Program *program, const Stmt *stmt, const ZStdSourceModule *current_module, size_t *appended, ZDiag *diag) {
  if (!stmt || (diag && diag->code != 0)) return !diag || diag->code == 0;
  if (!source_append_expr_std_functions(program, stmt->target, current_module, appended, diag) ||
      !source_append_expr_std_functions(program, stmt->expr, current_module, appended, diag) ||
      !source_append_expr_std_functions(program, stmt->range_end, current_module, appended, diag) ||
      !source_append_stmt_vec_std_functions(program, &stmt->then_body, current_module, appended, diag) ||
      !source_append_stmt_vec_std_functions(program, &stmt->else_body, current_module, appended, diag)) return false;
  for (size_t i = 0; i < stmt->match_arms.len; i++) {
    if (!source_append_expr_std_functions(program, stmt->match_arms.items[i].guard, current_module, appended, diag) ||
        !source_append_stmt_vec_std_functions(program, &stmt->match_arms.items[i].body, current_module, appended, diag)) return false;
  }
  return true;
}

static bool source_append_stmt_vec_std_functions(Program *program, const StmtVec *body, const ZStdSourceModule *current_module, size_t *appended, ZDiag *diag) {
  for (size_t i = 0; body && i < body->len; i++) {
    if (!source_append_stmt_std_functions(program, body->items[i], current_module, appended, diag)) return false;
  }
  return !diag || diag->code == 0;
}

static bool source_append_function_std_functions(Program *program, const Function *fun, const ZStdSourceModule *current_module, size_t *appended, ZDiag *diag) {
  for (size_t i = 0; fun && i < fun->type_params.len; i++) {
    if (!source_append_expr_std_functions(program, fun->type_params.items[i].default_value, current_module, appended, diag)) return false;
  }
  for (size_t i = 0; fun && i < fun->params.len; i++) {
    if (!source_append_expr_std_functions(program, fun->params.items[i].default_value, current_module, appended, diag)) return false;
  }
  for (size_t i = 0; fun && i < fun->errors.len; i++) {
    if (!source_append_expr_std_functions(program, fun->errors.items[i].default_value, current_module, appended, diag)) return false;
  }
  return source_append_stmt_vec_std_functions(program, fun ? &fun->body : NULL, current_module, appended, diag);
}

static bool source_append_std_function(Program *program, const ZStdSourceModule *module, const char *target_name, size_t *appended, ZDiag *diag) {
  if (!module || !target_name || !target_name[0] || source_program_has_function(program, target_name)) return true;
  char *graph_text = z_std_source_module_copy_graph(module);
  ZProgramGraph graph = {0};
  Program module_program = {0};
  ZDiag parse_diag = {0};
  bool parsed = z_program_graph_parse_dump(graph_text ? graph_text : "", &graph, &parse_diag) &&
                z_program_graph_lower_to_program(&graph, &module_program, &parse_diag);
  if (!parsed) {
    if (diag) {
      *diag = parse_diag;
      if (!diag->path) diag->path = module->path;
    }
    z_program_graph_free(&graph);
    free(graph_text);
    return false;
  }
  Function *fun = source_program_find_function(&module_program, target_name);
  if (!fun) {
    source_std_diag(diag, module, target_name);
    z_program_graph_free(&graph);
    z_free_program(&module_program);
    free(graph_text);
    return false;
  }
  source_push_function(program, fun);
  if (appended) (*appended)++;
  Function moved = program->functions.items[program->functions.len - 1];
  bool ok = source_append_function_std_functions(program, &moved, module, appended, diag);
  z_program_graph_free(&graph);
  z_free_program(&module_program);
  free(graph_text);
  return ok && (!diag || diag->code == 0);
}

bool z_program_graph_append_source_std_functions(Program *program, size_t *appended, ZDiag *diag) {
  for (size_t i = 0; program && i < program->consts.len; i++) {
    if (!source_append_expr_std_functions(program, program->consts.items[i].expr, NULL, appended, diag)) return false;
  }
  for (size_t i = 0; program && i < program->functions.len; i++) {
    Function current = program->functions.items[i];
    if (!source_append_function_std_functions(program, &current, NULL, appended, diag)) return false;
  }
  for (size_t i = 0; program && i < program->shapes.len; i++) {
    for (size_t method = 0; method < program->shapes.items[i].methods.len; method++) {
      if (!source_append_function_std_functions(program, &program->shapes.items[i].methods.items[method], NULL, appended, diag)) return false;
    }
  }
  for (size_t i = 0; program && i < program->interfaces.len; i++) {
    for (size_t method = 0; method < program->interfaces.items[i].methods.len; method++) {
      if (!source_append_function_std_functions(program, &program->interfaces.items[i].methods.items[method], NULL, appended, diag)) return false;
    }
  }
  return !diag || diag->code == 0;
}
