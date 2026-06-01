#include "zero.h"

#include <stdlib.h>
#include <string.h>

static void free_expr(Expr *expr) {
  if (!expr) return;
  free(expr->text);
  free(expr->resolved_type);
  free_expr(expr->left);
  free_expr(expr->right);
  for (size_t i = 0; i < expr->args.len; i++) free_expr(expr->args.items[i]);
  free(expr->args.items);
  for (size_t i = 0; i < expr->type_args.len; i++) free(expr->type_args.items[i].type);
  free(expr->type_args.items);
  for (size_t i = 0; i < expr->checked_type_args.len; i++) free(expr->checked_type_args.items[i].type);
  free(expr->checked_type_args.items);
  for (size_t i = 0; i < expr->fields.len; i++) {
    free(expr->fields.items[i].name);
    free_expr(expr->fields.items[i].value);
  }
  free(expr->fields.items);
  free(expr);
}

static void free_stmt_vec(StmtVec *vec) {
  for (size_t i = 0; i < vec->len; i++) {
    Stmt *stmt = vec->items[i];
    free(stmt->name);
    free(stmt->type);
    free(stmt->resolved_type);
    free_expr(stmt->target);
    free_expr(stmt->expr);
    free_expr(stmt->range_end);
    free_stmt_vec(&stmt->then_body);
    free_stmt_vec(&stmt->else_body);
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      free(stmt->match_arms.items[arm_index].case_name);
      free(stmt->match_arms.items[arm_index].range_end);
      free(stmt->match_arms.items[arm_index].payload_name);
      free_expr(stmt->match_arms.items[arm_index].guard);
      free_stmt_vec(&stmt->match_arms.items[arm_index].body);
    }
    free(stmt->match_arms.items);
    free(stmt);
  }
  free(vec->items);
}

static void free_param_vec(ParamVec *vec) {
  for (size_t i = 0; i < vec->len; i++) {
    free(vec->items[i].name);
    free(vec->items[i].type);
    free_expr(vec->items[i].default_value);
  }
  free(vec->items);
}

static void free_function(Function *fun) {
  free(fun->name);
  free(fun->test_name);
  free(fun->return_type);
  free_param_vec(&fun->type_params);
  free_param_vec(&fun->errors);
  free_param_vec(&fun->params);
  free_stmt_vec(&fun->body);
}

static void free_function_vec(FunctionVec *vec) {
  for (size_t i = 0; i < vec->len; i++) free_function(&vec->items[i]);
  free(vec->items);
}

static void free_use_import_vec(UseImportVec *vec) {
  for (size_t i = 0; i < vec->len; i++) {
    free(vec->items[i].module);
    free(vec->items[i].alias);
  }
  free(vec->items);
}

static void free_c_import_vec(CImportVec *vec) {
  for (size_t i = 0; i < vec->len; i++) {
    free(vec->items[i].header);
    free(vec->items[i].resolved_header);
    free(vec->items[i].alias);
  }
  free(vec->items);
}

static void free_const_vec(ConstVec *vec) {
  for (size_t i = 0; i < vec->len; i++) {
    free(vec->items[i].name);
    free(vec->items[i].type);
    free_expr(vec->items[i].expr);
  }
  free(vec->items);
}

static void free_alias_vec(TypeAliasVec *vec) {
  for (size_t i = 0; i < vec->len; i++) {
    free(vec->items[i].name);
    free(vec->items[i].target);
  }
  free(vec->items);
}

static void free_interface_vec(InterfaceVec *vec) {
  for (size_t i = 0; i < vec->len; i++) {
    free(vec->items[i].name);
    free_param_vec(&vec->items[i].type_params);
    free_function_vec(&vec->items[i].methods);
  }
  free(vec->items);
}

static void free_shape_vec(ShapeVec *vec) {
  for (size_t i = 0; i < vec->len; i++) {
    free(vec->items[i].name);
    free(vec->items[i].layout);
    free_param_vec(&vec->items[i].type_params);
    free_param_vec(&vec->items[i].fields);
    free_function_vec(&vec->items[i].methods);
  }
  free(vec->items);
}

static void free_enum_vec(EnumVec *vec) {
  for (size_t i = 0; i < vec->len; i++) {
    free(vec->items[i].name);
    free(vec->items[i].type);
    free_param_vec(&vec->items[i].cases);
  }
  free(vec->items);
}

static void free_choice_vec(ChoiceVec *vec) {
  for (size_t i = 0; i < vec->len; i++) {
    free(vec->items[i].name);
    free_param_vec(&vec->items[i].cases);
  }
  free(vec->items);
}

void z_free_program(Program *program) {
  if (!program) return;
  free_use_import_vec(&program->use_imports);
  free_c_import_vec(&program->c_imports);
  free_const_vec(&program->consts);
  free_alias_vec(&program->aliases);
  free_interface_vec(&program->interfaces);
  free_shape_vec(&program->shapes);
  free_enum_vec(&program->enums);
  free_choice_vec(&program->choices);
  free_function_vec(&program->functions);
  memset(program, 0, sizeof(*program));
}
