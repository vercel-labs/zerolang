#include "call_resolve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void z_call_resolution_init(ZCallResolution *resolution) {
  if (!resolution) return;
  *resolution = (ZCallResolution){0};
}

void z_call_resolution_free(ZCallResolution *resolution) {
  if (!resolution) return;
  for (size_t i = 0; i < resolution->binding_len; i++) {
    free(resolution->bindings[i].name);
    free(resolution->bindings[i].type);
    free(resolution->bindings[i].static_type);
  }
  free(resolution->bindings);
  for (size_t i = 0; i < resolution->arg_len; i++) {
    free(resolution->args[i].expected_type);
    free(resolution->args[i].actual_type);
  }
  free(resolution->args);
  for (size_t i = 0; i < resolution->error_len; i++) {
    free(resolution->errors[i].name);
  }
  free(resolution->errors);
  free(resolution->callee_name);
  free(resolution->return_type);
  free(resolution->effect_summary_key);
  *resolution = (ZCallResolution){0};
}

void z_call_resolution_set_callee_name(ZCallResolution *resolution, const char *name) {
  if (!resolution) return;
  if (resolution->callee_name == name) return;
  char *copy = name ? z_strdup(name) : NULL;
  free(resolution->callee_name);
  resolution->callee_name = copy;
}

void z_call_resolution_set_return_type(ZCallResolution *resolution, const char *return_type) {
  if (!resolution) return;
  if (resolution->return_type == return_type) return;
  char *copy = z_strdup(return_type ? return_type : "Unknown");
  free(resolution->return_type);
  resolution->return_type = copy;
}

void z_call_resolution_set_effect_summary_key(ZCallResolution *resolution, const char *key) {
  if (!resolution) return;
  if (resolution->effect_summary_key == key) return;
  char *copy = key ? z_strdup(key) : NULL;
  free(resolution->effect_summary_key);
  resolution->effect_summary_key = copy;
}

size_t z_call_resolution_expected_arg_count(const ZCallResolution *resolution) {
  if (!resolution || resolution->param_len < resolution->param_offset) return 0;
  return resolution->param_len - resolution->param_offset;
}

void z_call_resolution_add_arg(ZCallResolution *resolution, size_t param_index, const Expr *arg_expr, const char *expected_type, const char *actual_type) {
  if (!resolution) return;
  for (size_t i = 0; i < resolution->arg_len; i++) {
    if (resolution->args[i].param_index == param_index) {
      resolution->args[i].arg_expr = arg_expr;
      char *expected_copy = z_strdup(expected_type ? expected_type : "Unknown");
      char *actual_copy = z_strdup(actual_type ? actual_type : "Unknown");
      free(resolution->args[i].expected_type);
      free(resolution->args[i].actual_type);
      resolution->args[i].expected_type = expected_copy;
      resolution->args[i].actual_type = actual_copy;
      return;
    }
  }
  if (resolution->arg_len == resolution->arg_cap) {
    size_t next_cap = resolution->arg_cap ? resolution->arg_cap * 2 : 4;
    resolution->args = z_checked_reallocarray(resolution->args, next_cap, sizeof(ZCallArgument));
    for (size_t i = resolution->arg_cap; i < next_cap; i++) {
      resolution->args[i] = (ZCallArgument){0};
    }
    resolution->arg_cap = next_cap;
  }
  ZCallArgument *arg = &resolution->args[resolution->arg_len++];
  arg->param_index = param_index;
  arg->arg_expr = arg_expr;
  arg->expected_type = z_strdup(expected_type ? expected_type : "Unknown");
  arg->actual_type = z_strdup(actual_type ? actual_type : "Unknown");
}

const char *z_call_resolution_param_type(const ZCallResolution *resolution, size_t param_index) {
  if (!resolution) return NULL;
  for (size_t i = 0; i < resolution->arg_len; i++) {
    if (resolution->args[i].param_index == param_index) return resolution->args[i].expected_type;
  }
  return NULL;
}

void z_call_resolution_add_binding(ZCallResolution *resolution, const char *name, const char *type, bool is_static, const char *static_type) {
  if (!resolution || !name) return;
  for (size_t i = 0; i < resolution->binding_len; i++) {
    if (resolution->bindings[i].name && strcmp(resolution->bindings[i].name, name) == 0) {
      char *type_copy = z_strdup(type ? type : "Unknown");
      char *static_type_copy = static_type ? z_strdup(static_type) : NULL;
      free(resolution->bindings[i].type);
      free(resolution->bindings[i].static_type);
      resolution->bindings[i].type = type_copy;
      resolution->bindings[i].is_static = is_static;
      resolution->bindings[i].static_type = static_type_copy;
      return;
    }
  }
  if (resolution->binding_len == resolution->binding_cap) {
    size_t next_cap = resolution->binding_cap ? resolution->binding_cap * 2 : 4;
    resolution->bindings = z_checked_reallocarray(resolution->bindings, next_cap, sizeof(ZCallBinding));
    for (size_t i = resolution->binding_cap; i < next_cap; i++) {
      resolution->bindings[i] = (ZCallBinding){0};
    }
    resolution->binding_cap = next_cap;
  }
  ZCallBinding *binding = &resolution->bindings[resolution->binding_len++];
  binding->name = z_strdup(name);
  binding->type = z_strdup(type ? type : "Unknown");
  binding->is_static = is_static;
  binding->static_type = static_type ? z_strdup(static_type) : NULL;
}

const char *z_call_resolution_binding_type(const ZCallResolution *resolution, const char *name) {
  if (!resolution || !name) return NULL;
  for (size_t i = 0; i < resolution->binding_len; i++) {
    if (resolution->bindings[i].name && strcmp(resolution->bindings[i].name, name) == 0) return resolution->bindings[i].type;
  }
  return NULL;
}

void z_call_resolution_add_error(ZCallResolution *resolution, const char *name) {
  if (!resolution || !name) return;
  if (resolution->error_len == resolution->error_cap) {
    size_t next_cap = resolution->error_cap ? resolution->error_cap * 2 : 4;
    resolution->errors = z_checked_reallocarray(resolution->errors, next_cap, sizeof(ZCallError));
    for (size_t i = resolution->error_cap; i < next_cap; i++) {
      resolution->errors[i] = (ZCallError){0};
    }
    resolution->error_cap = next_cap;
  }
  resolution->errors[resolution->error_len++].name = z_strdup(name);
  resolution->fallible = true;
}

void z_call_resolution_error_set_text(const ZCallResolution *resolution, char *buf, size_t cap) {
  if (!buf || cap == 0) return;
  snprintf(buf, cap, "![");
  size_t used = strlen(buf);
  for (size_t i = 0; resolution && i < resolution->error_len; i++) {
    const char *name = resolution->errors[i].name;
    if (!name) continue;
    if (used >= cap - 1) break;
    snprintf(buf + used, cap - used, "%s%s", used > 2 ? " " : "", name);
    used = strlen(buf);
  }
  if (used < cap - 1) snprintf(buf + used, cap - used, "]");
  else buf[cap - 1] = '\0';
}

const char *z_call_kind_name(ZCallKind kind) {
  switch (kind) {
    case Z_CALL_FUNCTION: return "function";
    case Z_CALL_STDLIB: return "stdlib";
    case Z_CALL_RECEIVER: return "receiver";
    case Z_CALL_SHAPE_NAMESPACE: return "shape_namespace";
    case Z_CALL_CONSTRAINED_INTERFACE: return "constrained_interface";
    case Z_CALL_CONCRETE_CONSTRAINED_SHAPE: return "concrete_constrained_shape";
    case Z_CALL_CHOICE_CONSTRUCTOR: return "choice_constructor";
    case Z_CALL_UNKNOWN:
    default:
      return "unknown";
  }
}
