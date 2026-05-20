#include "specialize.h"

#include <stdlib.h>
#include <string.h>

static void z_specialization_type_args_free(TypeArgVec *args) {
  if (!args) return;
  for (size_t i = 0; i < args->len; i++) free(args->items[i].type);
  free(args->items);
  *args = (TypeArgVec){0};
}

static void z_specialization_type_args_push_clone(TypeArgVec *vec, const TypeArg *arg) {
  if (vec->len + 1 > vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(TypeArg));
  }
  vec->items[vec->len++] = (TypeArg){
    .type = arg && arg->type ? z_strdup(arg->type) : NULL,
    .line = arg ? arg->line : 0,
    .column = arg ? arg->column : 0
  };
}

static TypeArgVec z_specialization_type_args_clone(const TypeArgVec *source) {
  TypeArgVec copy = {0};
  for (size_t i = 0; source && i < source->len; i++) z_specialization_type_args_push_clone(&copy, &source->items[i]);
  return copy;
}

static bool z_specialization_type_args_equal(const TypeArgVec *left, const TypeArgVec *right) {
  size_t left_len = left ? left->len : 0;
  size_t right_len = right ? right->len : 0;
  if (left_len != right_len) return false;
  for (size_t i = 0; i < left_len; i++) {
    const char *left_type = left->items[i].type ? left->items[i].type : "";
    const char *right_type = right->items[i].type ? right->items[i].type : "";
    if (strcmp(left_type, right_type) != 0) return false;
  }
  return true;
}

void z_specialization_plan_init(ZSpecializationPlan *plan, size_t limit) {
  if (!plan) return;
  *plan = (ZSpecializationPlan){.limit = limit};
}

void z_specialization_plan_free(ZSpecializationPlan *plan) {
  if (!plan) return;
  for (size_t i = 0; i < plan->len; i++) {
    free(plan->items[i].name);
    z_specialization_type_args_free(&plan->items[i].type_args);
  }
  free(plan->items);
  *plan = (ZSpecializationPlan){0};
}

char *z_specialized_function_name(const Function *fun, const TypeArgVec *type_args) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, fun && fun->name ? fun->name : "");
  for (size_t i = 0; type_args && i < type_args->len; i++) {
    zbuf_append(&buf, "__");
    const char *type = type_args->items[i].type ? type_args->items[i].type : "Unknown";
    for (const char *p = type; *p; p++) {
      char c = *p;
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') zbuf_append_char(&buf, c);
      else zbuf_append_char(&buf, '_');
    }
  }
  return buf.data;
}

ZSpecializationAddResult z_specialization_plan_add(ZSpecializationPlan *plan, const Function *source, const TypeArgVec *type_args, char **out_name) {
  char *name = z_specialized_function_name(source, type_args);
  if (out_name) *out_name = z_strdup(name);
  if (plan) {
    for (size_t i = 0; i < plan->len; i++) {
      if (plan->items[i].source == source && z_specialization_type_args_equal(&plan->items[i].type_args, type_args)) {
        free(name);
        return Z_SPECIALIZATION_ADD_PRESENT;
      }
      if (plan->items[i].name && strcmp(plan->items[i].name, name) == 0) {
        free(name);
        return Z_SPECIALIZATION_ADD_NAME_COLLISION;
      }
    }
  }
  if (plan && plan->limit > 0 && plan->len >= plan->limit) {
    plan->hit_limit = true;
    free(name);
    return Z_SPECIALIZATION_ADD_LIMIT;
  }
  if (!plan) {
    free(name);
    return Z_SPECIALIZATION_ADD_LIMIT;
  }
  if (plan->len + 1 > plan->cap) {
    plan->cap = z_grow_capacity(plan->cap, plan->len + 1, 8);
    plan->items = z_checked_reallocarray(plan->items, plan->cap, sizeof(ZSpecializationEntry));
  }
  plan->items[plan->len++] = (ZSpecializationEntry){
    .source = source,
    .type_args = z_specialization_type_args_clone(type_args),
    .name = name
  };
  return Z_SPECIALIZATION_ADD_ADDED;
}
