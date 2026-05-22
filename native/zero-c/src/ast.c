#include "zero.h"

#include <stdlib.h>

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

void z_free_program(Program *program) {
  for (size_t i = 0; i < program->use_imports.len; i++) {
    free(program->use_imports.items[i].module);
    free(program->use_imports.items[i].alias);
  }
  free(program->use_imports.items);
  for (size_t i = 0; i < program->consts.len; i++) {
    ConstDecl *item = &program->consts.items[i];
    free(item->name);
    free(item->type);
    free_expr(item->expr);
  }
  free(program->consts.items);
  for (size_t i = 0; i < program->aliases.len; i++) {
    TypeAlias *item = &program->aliases.items[i];
    free(item->name);
    free(item->target);
  }
  free(program->aliases.items);
  for (size_t i = 0; i < program->interfaces.len; i++) {
    InterfaceDecl *interface = &program->interfaces.items[i];
    free(interface->name);
    for (size_t param_index = 0; param_index < interface->type_params.len; param_index++) {
      free(interface->type_params.items[param_index].name);
      free(interface->type_params.items[param_index].type);
    }
    free(interface->type_params.items);
    for (size_t method_index = 0; method_index < interface->methods.len; method_index++) {
      Function *method = &interface->methods.items[method_index];
      free(method->name);
      free(method->return_type);
      for (size_t type_param_index = 0; type_param_index < method->type_params.len; type_param_index++) {
        free(method->type_params.items[type_param_index].name);
        free(method->type_params.items[type_param_index].type);
      }
      free(method->type_params.items);
      for (size_t error_index = 0; error_index < method->errors.len; error_index++) {
        free(method->errors.items[error_index].name);
        free(method->errors.items[error_index].type);
      }
      free(method->errors.items);
      for (size_t param_index = 0; param_index < method->params.len; param_index++) {
        free(method->params.items[param_index].name);
        free(method->params.items[param_index].type);
      }
      free(method->params.items);
      free_stmt_vec(&method->body);
    }
    free(interface->methods.items);
  }
  free(program->interfaces.items);
  for (size_t i = 0; i < program->shapes.len; i++) {
    Shape *shape = &program->shapes.items[i];
    free(shape->name);
    free(shape->layout);
    for (size_t param_index = 0; param_index < shape->type_params.len; param_index++) {
      free(shape->type_params.items[param_index].name);
      free(shape->type_params.items[param_index].type);
    }
    free(shape->type_params.items);
    for (size_t field_index = 0; field_index < shape->fields.len; field_index++) {
      free(shape->fields.items[field_index].name);
      free(shape->fields.items[field_index].type);
      free_expr(shape->fields.items[field_index].default_value);
    }
    free(shape->fields.items);
    for (size_t method_index = 0; method_index < shape->methods.len; method_index++) {
      Function *method = &shape->methods.items[method_index];
      free(method->name);
      free(method->return_type);
      for (size_t type_param_index = 0; type_param_index < method->type_params.len; type_param_index++) {
        free(method->type_params.items[type_param_index].name);
        free(method->type_params.items[type_param_index].type);
      }
      free(method->type_params.items);
      for (size_t error_index = 0; error_index < method->errors.len; error_index++) {
        free(method->errors.items[error_index].name);
        free(method->errors.items[error_index].type);
      }
      free(method->errors.items);
      for (size_t param_index = 0; param_index < method->params.len; param_index++) {
        free(method->params.items[param_index].name);
        free(method->params.items[param_index].type);
      }
      free(method->params.items);
      free_stmt_vec(&method->body);
    }
    free(shape->methods.items);
  }
  free(program->shapes.items);
  for (size_t i = 0; i < program->enums.len; i++) {
    EnumDecl *item = &program->enums.items[i];
    free(item->name);
    free(item->type);
    for (size_t case_index = 0; case_index < item->cases.len; case_index++) {
      free(item->cases.items[case_index].name);
      free(item->cases.items[case_index].type);
    }
    free(item->cases.items);
  }
  free(program->enums.items);
  for (size_t i = 0; i < program->choices.len; i++) {
    Choice *item = &program->choices.items[i];
    free(item->name);
    for (size_t case_index = 0; case_index < item->cases.len; case_index++) {
      free(item->cases.items[case_index].name);
      free(item->cases.items[case_index].type);
    }
    free(item->cases.items);
  }
  free(program->choices.items);
  for (size_t i = 0; i < program->c_imports.len; i++) {
    free(program->c_imports.items[i].header);
    free(program->c_imports.items[i].alias);
  }
  free(program->c_imports.items);
  for (size_t i = 0; i < program->functions.len; i++) {
    Function *fun = &program->functions.items[i];
    free(fun->name);
    free(fun->test_name);
    free(fun->return_type);
    for (size_t type_param_index = 0; type_param_index < fun->type_params.len; type_param_index++) {
      free(fun->type_params.items[type_param_index].name);
      free(fun->type_params.items[type_param_index].type);
    }
    free(fun->type_params.items);
    for (size_t error_index = 0; error_index < fun->errors.len; error_index++) {
      free(fun->errors.items[error_index].name);
      free(fun->errors.items[error_index].type);
    }
    free(fun->errors.items);
    for (size_t param_index = 0; param_index < fun->params.len; param_index++) {
      free(fun->params.items[param_index].name);
      free(fun->params.items[param_index].type);
    }
    free(fun->params.items);
    free_stmt_vec(&fun->body);
  }
  free(program->functions.items);
  program->functions.items = NULL;
  program->functions.len = 0;
  program->functions.cap = 0;
}
