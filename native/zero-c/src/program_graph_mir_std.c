#include "program_graph_mir_std.h"

#include "canonical_text.h"
#include "std_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  snprintf(diag->message, sizeof(diag->message), "source-backed std helper '%s' is missing", target_name ? target_name : "");
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
  char *source = z_std_source_module_copy_source(module);
  Program module_program = {0};
  ZDiag parse_diag = {0};
  bool parsed = z_parse_canonical_text_program_source(source ? source : "", &module_program, &parse_diag);
  if (!parsed) {
    if (diag) {
      *diag = parse_diag;
      if (!diag->path) diag->path = module->path;
    }
    free(source);
    return false;
  }
  Function *fun = source_program_find_function(&module_program, target_name);
  if (!fun) {
    source_std_diag(diag, module, target_name);
    z_free_program(&module_program);
    free(source);
    return false;
  }
  source_push_function(program, fun);
  if (appended) (*appended)++;
  Function *moved = &program->functions.items[program->functions.len - 1];
  bool ok = source_append_function_std_functions(program, moved, module, appended, diag);
  z_free_program(&module_program);
  free(source);
  return ok && (!diag || diag->code == 0);
}

bool z_program_graph_append_source_std_functions(Program *program, size_t *appended, ZDiag *diag) {
  for (size_t i = 0; program && i < program->consts.len; i++) {
    if (!source_append_expr_std_functions(program, program->consts.items[i].expr, NULL, appended, diag)) return false;
  }
  for (size_t i = 0; program && i < program->functions.len; i++) {
    if (!source_append_function_std_functions(program, &program->functions.items[i], NULL, appended, diag)) return false;
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
