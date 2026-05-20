#include "call_resolve.h"

#include <stdlib.h>

void z_call_resolution_init(ZCallResolution *resolution) {
  if (!resolution) return;
  *resolution = (ZCallResolution){0};
}

void z_call_resolution_free(ZCallResolution *resolution) {
  if (!resolution) return;
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
