#include "mir_verify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *mir_type_kind_name(IrTypeKind type) {
  switch (type) {
    case IR_TYPE_VOID: return "Void";
    case IR_TYPE_BOOL: return "Bool";
    case IR_TYPE_U8: return "u8";
    case IR_TYPE_U16: return "u16";
    case IR_TYPE_USIZE: return "usize";
    case IR_TYPE_I32: return "i32";
    case IR_TYPE_U32: return "u32";
    case IR_TYPE_I64: return "i64";
    case IR_TYPE_U64: return "u64";
    case IR_TYPE_BYTE_VIEW: return "ByteView";
    case IR_TYPE_ALLOC: return "Alloc";
    case IR_TYPE_VEC: return "Vec";
    case IR_TYPE_MAYBE_BYTE_VIEW: return "Maybe<ByteView>";
    case IR_TYPE_MAYBE_SCALAR: return "Maybe<scalar>";
    case IR_TYPE_RECORD: return "record";
    case IR_TYPE_UNSUPPORTED:
    default:
      return "unsupported";
  }
}

static bool mir_type_is_value(IrTypeKind type) {
  return type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_I32 || type == IR_TYPE_U32 || type == IR_TYPE_I64 || type == IR_TYPE_U64;
}

static bool mir_type_is_direct_abi(IrTypeKind type) {
  return type == IR_TYPE_BOOL || mir_type_is_value(type);
}

static bool mir_type_is_direct_param_abi(IrTypeKind type) {
  return mir_type_is_direct_abi(type) || type == IR_TYPE_BYTE_VIEW;
}

static bool mir_type_is_direct_return_abi(IrTypeKind type) {
  return mir_type_is_direct_abi(type) || type == IR_TYPE_BYTE_VIEW || type == IR_TYPE_MAYBE_BYTE_VIEW || type == IR_TYPE_MAYBE_SCALAR;
}

static bool mir_type_is_direct_fallible_value(IrTypeKind type) {
  return type == IR_TYPE_VOID || type == IR_TYPE_BOOL || type == IR_TYPE_U8 ||
         type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_I32 ||
         type == IR_TYPE_U32;
}

static bool mir_type_is_integer_value(IrTypeKind type) {
  return type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE ||
         type == IR_TYPE_I32 || type == IR_TYPE_U32 || type == IR_TYPE_I64 ||
         type == IR_TYPE_U64;
}

static IrTypeKind mir_view_element_or_u8(IrTypeKind type) {
  return type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : type;
}

static unsigned mir_type_byte_size(IrTypeKind type) {
  switch (type) {
    case IR_TYPE_BOOL:
    case IR_TYPE_U8: return 1;
    case IR_TYPE_U16: return 2;
    case IR_TYPE_I32:
    case IR_TYPE_USIZE:
    case IR_TYPE_U32: return 4;
    case IR_TYPE_I64:
    case IR_TYPE_U64: return 8;
    default: return 0;
  }
}

static unsigned mir_type_local_slot_min_byte_size(IrTypeKind type) {
  unsigned byte_size = mir_type_byte_size(type);
  if (byte_size > 0 && byte_size < 4) return 4;
  return byte_size;
}

static unsigned mir_type_storage_min_byte_size(const IrLocal *local) {
  if (!local) return 0;
  if (local->is_array) {
    unsigned element_size = mir_type_byte_size(local->element_type);
    if (element_size == 0 || local->array_len == 0) return 0;
    if (local->array_len > (unsigned)(~0u) / element_size) return 0;
    return element_size * local->array_len;
  }
  if (local->is_record) return 1;
  switch (local->type) {
    case IR_TYPE_BYTE_VIEW:
    case IR_TYPE_ALLOC:
    case IR_TYPE_VEC:
    case IR_TYPE_MAYBE_SCALAR:
      return 16;
    case IR_TYPE_MAYBE_BYTE_VIEW:
      return 24;
    default:
      return mir_type_local_slot_min_byte_size(local->type);
  }
}

static void mir_verify_mark_unsupported(IrProgram *ir, const char *message, int line, int column, const char *actual) {
  if (!ir || !ir->mir_valid) return;
  ir->mir_valid = false;
  ir->mir_line = line > 0 ? line : 1;
  ir->mir_column = column > 0 ? column : 1;
  snprintf(ir->mir_message, sizeof(ir->mir_message), "%s", message ? message : "MIR verification failed");
  snprintf(ir->mir_expected, sizeof(ir->mir_expected), "direct backend MIR contract");
  snprintf(ir->mir_actual, sizeof(ir->mir_actual), "%s", actual ? actual : "invalid MIR");
  snprintf(ir->mir_help, sizeof(ir->mir_help), "report this compiler bug with the source program that produced it");
  z_backend_blocker_set(&ir->backend_blocker, NULL, NULL, NULL, "lower", ir->mir_actual);
}

static bool mir_verify_local_index(IrProgram *ir, const IrFunction *fun, unsigned index, int line, int column, const char *message) {
  if (!ir || !ir->mir_valid || !fun) return false;
  if (index < fun->local_len) return true;
  char actual[160];
  snprintf(actual, sizeof(actual), "local index %u with %zu local(s) on %s", index, fun->local_len, fun->name ? fun->name : "<unnamed>");
  mir_verify_mark_unsupported(ir, message, line, column, actual);
  return false;
}

static bool mir_value_can_initialize_local(const IrLocal *local, const IrValue *value) {
  if (!local || !value) return false;
  if (value->type == local->type) return true;
  return local->type == IR_TYPE_MAYBE_SCALAR && value->type == IR_TYPE_I64;
}

static bool mir_verify_local_initializer_kind(IrProgram *ir, const IrLocal *local, const IrValue *value, int line, int column) {
  if (!ir || !ir->mir_valid || !local || !value) return false;
  switch (local->type) {
    case IR_TYPE_ALLOC:
      if (value->kind == IR_VALUE_FIXED_BUF_ALLOC) return true;
      break;
    case IR_TYPE_VEC:
      if (value->kind == IR_VALUE_VEC_INIT) return true;
      break;
    case IR_TYPE_MAYBE_BYTE_VIEW:
      if (value->kind == IR_VALUE_ALLOC_BYTES ||
          value->kind == IR_VALUE_CALL ||
          value->kind == IR_VALUE_MAYBE_BYTE_VIEW_LITERAL ||
          value->kind == IR_VALUE_ARGS_GET ||
          value->kind == IR_VALUE_ARGS_VALUE_AFTER ||
          value->kind == IR_VALUE_ENV_GET ||
          value->kind == IR_VALUE_FS_READ_ALL ||
          value->kind == IR_VALUE_FS_TEMP_NAME ||
          value->kind == IR_VALUE_HTTP_REQUEST_METHOD_NAME ||
          value->kind == IR_VALUE_HTTP_REQUEST_PATH ||
          value->kind == IR_VALUE_HTTP_REQUEST_BODY_WITHIN ||
          value->kind == IR_VALUE_HTTP_WRITE_JSON_RESPONSE ||
          value->kind == IR_VALUE_STR_RUNTIME ||
          value->kind == IR_VALUE_FMT_BOOL ||
          value->kind == IR_VALUE_FMT_HEX_U32 ||
          value->kind == IR_VALUE_FMT_I32 ||
          value->kind == IR_VALUE_FMT_U32 ||
          value->kind == IR_VALUE_FMT_USIZE) {
        return true;
      }
      break;
    default:
      return true;
  }
  char actual[192];
  snprintf(actual, sizeof(actual), "local %s has %s initializer kind %d", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type), (int)value->kind);
  mir_verify_mark_unsupported(ir, "MIR verifier found invalid local initializer", line, column, actual);
  return false;
}

typedef struct {
  size_t count;
  int line;
  int column;
  const char *reason;
} MirCountRequirement;

typedef struct {
  MirCountRequirement allocator_helpers;
  MirCountRequirement buffer_helpers;
  MirCountRequirement runtime_helpers;
  MirCountRequirement host_runtime_imports;
  MirCountRequirement http_runtime_imports;
} MirHelperRequirements;

typedef struct {
  bool *mutable_maybe_bytes;
  size_t len;
} MirVerifierState;

static void mir_require_count(MirCountRequirement *req, size_t count, int line, int column, const char *reason) {
  if (!req || count <= req->count) return;
  req->count = count;
  req->line = line;
  req->column = column;
  req->reason = reason;
}

static bool mir_state_init(IrProgram *ir, MirVerifierState *state, size_t len, int line, int column) {
  if (!state) return false;
  *state = (MirVerifierState){.len = len};
  if (len == 0) return true;
  state->mutable_maybe_bytes = calloc(len, sizeof(bool));
  if (state->mutable_maybe_bytes) return true;
  mir_verify_mark_unsupported(ir, "MIR verifier could not allocate control-flow state", line, column, "out of memory while tracking Maybe byte storage");
  return false;
}

static bool mir_state_clone(IrProgram *ir, MirVerifierState *dest, const MirVerifierState *source, int line, int column) {
  if (!dest || !source) return false;
  if (!mir_state_init(ir, dest, source->len, line, column)) return false;
  if (source->len > 0) memcpy(dest->mutable_maybe_bytes, source->mutable_maybe_bytes, source->len * sizeof(bool));
  return true;
}

static void mir_state_free(MirVerifierState *state) {
  if (!state) return;
  free(state->mutable_maybe_bytes);
  *state = (MirVerifierState){0};
}

static bool mir_state_has_mutable_maybe_byte_payload(const MirVerifierState *state, unsigned local_index) {
  return state && local_index < state->len && state->mutable_maybe_bytes && state->mutable_maybe_bytes[local_index];
}

static bool mir_state_intersect_from(MirVerifierState *state, const MirVerifierState *left, const MirVerifierState *right) {
  if (!state || !left || !right || state->len != left->len || state->len != right->len) return false;
  for (size_t i = 0; i < state->len; i++) {
    state->mutable_maybe_bytes[i] = left->mutable_maybe_bytes[i] && right->mutable_maybe_bytes[i];
  }
  return true;
}

static bool mir_verify_direct_function_contract(IrProgram *ir, const IrFunction *fun) {
  if (!ir || !ir->mir_valid || !fun) return false;
  if (fun->param_count > fun->local_len) {
    char actual[128];
    snprintf(actual, sizeof(actual), "%zu parameter(s) with %zu local(s)", fun->param_count, fun->local_len);
    mir_verify_mark_unsupported(ir, "MIR verifier found parameter count outside the local table", fun->line, fun->column, actual);
    return false;
  }
  for (size_t i = 0; i < fun->param_count; i++) {
    const IrLocal *local = &fun->locals[i];
    if (!local->is_param || local->index != i) {
      char actual[160];
      snprintf(actual, sizeof(actual), "parameter slot %zu maps to local index %u", i, local->index);
      mir_verify_mark_unsupported(ir, "MIR verifier found invalid parameter local layout", local->line, local->column, actual);
      return false;
    }
    if (!mir_type_is_direct_param_abi(local->type)) {
      char actual[160];
      snprintf(actual, sizeof(actual), "parameter %s has %s", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type));
      mir_verify_mark_unsupported(ir, "MIR verifier found non-ABI parameter type", local->line, local->column, actual);
      return false;
    }
  }
  for (size_t i = 0; i < fun->local_len; i++) {
    const IrLocal *local = &fun->locals[i];
    if (local->index != i) {
      char actual[160];
      snprintf(actual, sizeof(actual), "local %s has index %u at slot %zu", local->name ? local->name : "<unnamed>", local->index, i);
      mir_verify_mark_unsupported(ir, "MIR verifier found local table index mismatch", local->line, local->column, actual);
      return false;
    }
    if (local->byte_size == 0 || local->alignment == 0) {
      char actual[160];
      snprintf(actual, sizeof(actual), "local %s has size %u alignment %u", local->name ? local->name : "<unnamed>", local->byte_size, local->alignment);
      mir_verify_mark_unsupported(ir, "MIR verifier found invalid local storage layout", local->line, local->column, actual);
      return false;
    }
    unsigned min_byte_size = mir_type_storage_min_byte_size(local);
    if (min_byte_size == 0 || local->byte_size < min_byte_size) {
      char actual[192];
      snprintf(actual, sizeof(actual), "local %s has size %u but needs at least %u", local->name ? local->name : "<unnamed>", local->byte_size, min_byte_size);
      mir_verify_mark_unsupported(ir, "MIR verifier found invalid local storage layout", local->line, local->column, actual);
      return false;
    }
    if (local->frame_offset < local->byte_size || local->frame_offset > fun->frame_bytes) {
      char actual[192];
      snprintf(actual, sizeof(actual), "local %s offset %u size %u frame %zu", local->name ? local->name : "<unnamed>", local->frame_offset, local->byte_size, fun->frame_bytes);
      mir_verify_mark_unsupported(ir, "MIR verifier found local outside the stack frame", local->line, local->column, actual);
      return false;
    }
    unsigned frame_start = local->frame_offset - local->byte_size;
    if (frame_start % local->alignment != 0) {
      char actual[192];
      snprintf(actual, sizeof(actual), "local %s starts at %u with alignment %u", local->name ? local->name : "<unnamed>", frame_start, local->alignment);
      mir_verify_mark_unsupported(ir, "MIR verifier found misaligned local storage", local->line, local->column, actual);
      return false;
    }
    if (i > 0 && frame_start < fun->locals[i - 1].frame_offset) {
      char actual[192];
      snprintf(actual, sizeof(actual), "local %s starts at %u before previous local ends at %u", local->name ? local->name : "<unnamed>", frame_start, fun->locals[i - 1].frame_offset);
      mir_verify_mark_unsupported(ir, "MIR verifier found overlapping local storage", local->line, local->column, actual);
      return false;
    }
  }
  if (fun->raises) {
    if (fun->return_type != IR_TYPE_I64 || !mir_type_is_direct_fallible_value(fun->value_return_type)) {
      char actual[160];
      snprintf(actual, sizeof(actual), "fallible return %s value %s", mir_type_kind_name(fun->return_type), mir_type_kind_name(fun->value_return_type));
      mir_verify_mark_unsupported(ir, "MIR verifier found invalid fallible return representation", fun->line, fun->column, actual);
      return false;
    }
  } else if (fun->return_type != IR_TYPE_VOID && !mir_type_is_direct_return_abi(fun->return_type)) {
    char actual[128];
    snprintf(actual, sizeof(actual), "return %s", mir_type_kind_name(fun->return_type));
    mir_verify_mark_unsupported(ir, "MIR verifier found non-ABI return type", fun->line, fun->column, actual);
    return false;
  } else if (!fun->raises && fun->return_type == IR_TYPE_BYTE_VIEW && !mir_type_is_direct_abi(mir_view_element_or_u8(fun->return_element_type))) {
    char actual[128];
    snprintf(actual, sizeof(actual), "return element %s", mir_type_kind_name(fun->return_element_type));
    mir_verify_mark_unsupported(ir, "MIR verifier found non-ABI byte-view return element type", fun->line, fun->column, actual);
    return false;
  } else if (!fun->raises && fun->return_type == IR_TYPE_MAYBE_SCALAR && !mir_type_is_direct_abi(fun->return_element_type)) {
    char actual[128];
    snprintf(actual, sizeof(actual), "return element %s", mir_type_kind_name(fun->return_element_type));
    mir_verify_mark_unsupported(ir, "MIR verifier found non-ABI Maybe scalar return element type", fun->line, fun->column, actual);
    return false;
  }
  return true;
}

static bool mir_verify_direct_call_contract(IrProgram *ir, const IrValue *value) {
  if (!ir || !ir->mir_valid || !value || value->kind != IR_VALUE_CALL) return ir && ir->mir_valid;
  if (value->external_call) {
    if (value->external_index >= ir->external_function_len) {
      char actual[128];
      snprintf(actual, sizeof(actual), "external index %u with %zu external function(s)", value->external_index, ir->external_function_len);
      mir_verify_mark_unsupported(ir, "MIR verifier found external call target outside the import table", value->line, value->column, actual);
      return false;
    }
    const IrExternalFunction *callee = &ir->external_functions[value->external_index];
    if (value->arg_len != callee->param_len) {
      char actual[160];
      snprintf(actual, sizeof(actual), "%zu argument(s) for %zu parameter(s) on %s", value->arg_len, callee->param_len, callee->symbol ? callee->symbol : "<external>");
      mir_verify_mark_unsupported(ir, "MIR verifier found external call arity mismatch", value->line, value->column, actual);
      return false;
    }
    if (value->type != callee->return_type || value->element_type != callee->return_type) {
      char actual[192];
      snprintf(actual, sizeof(actual), "call returns %s value %s but external returns %s", mir_type_kind_name(value->type), mir_type_kind_name(value->element_type), mir_type_kind_name(callee->return_type));
      mir_verify_mark_unsupported(ir, "MIR verifier found external call return mismatch", value->line, value->column, actual);
      return false;
    }
    for (size_t i = 0; i < value->arg_len; i++) {
      const IrValue *arg = value->args[i];
      if (!arg || arg->type != callee->param_types[i]) {
        char actual[192];
        snprintf(actual, sizeof(actual), "argument %zu has %s but external parameter has %s", i, arg ? mir_type_kind_name(arg->type) : "missing", mir_type_kind_name(callee->param_types[i]));
        mir_verify_mark_unsupported(ir, "MIR verifier found external call argument ABI mismatch", arg ? arg->line : value->line, arg ? arg->column : value->column, actual);
        return false;
      }
    }
    return true;
  }
  if (value->callee_index >= ir->function_len) {
    char actual[128];
    snprintf(actual, sizeof(actual), "callee index %u with %zu MIR function(s)", value->callee_index, ir->function_len);
    mir_verify_mark_unsupported(ir, "MIR verifier found direct call target outside the function table", value->line, value->column, actual);
    return false;
  }
  const IrFunction *callee = &ir->functions[value->callee_index];
  if (value->arg_len != callee->param_count) {
    char actual[160];
    snprintf(actual, sizeof(actual), "%zu argument(s) for %zu parameter(s) on %s", value->arg_len, callee->param_count, callee->name ? callee->name : "<unnamed>");
    mir_verify_mark_unsupported(ir, "MIR verifier found direct call arity mismatch", value->line, value->column, actual);
    return false;
  }
  IrTypeKind expected_call_type = callee->raises ? IR_TYPE_I64 : callee->return_type;
  IrTypeKind actual_value_type = value->element_type;
  IrTypeKind expected_value_type = callee->value_return_type;
  if (expected_call_type == IR_TYPE_BYTE_VIEW) {
    actual_value_type = mir_view_element_or_u8(value->element_type);
    expected_value_type = mir_view_element_or_u8(callee->return_element_type);
  } else if (expected_call_type == IR_TYPE_MAYBE_SCALAR) {
    expected_value_type = callee->return_element_type;
  }
  bool element_matches = actual_value_type == expected_value_type;
  if (value->type != expected_call_type || !element_matches) {
    char actual[192];
    snprintf(actual, sizeof(actual), "call returns %s value %s but callee returns %s value %s", mir_type_kind_name(value->type), mir_type_kind_name(actual_value_type), mir_type_kind_name(expected_call_type), mir_type_kind_name(expected_value_type));
    mir_verify_mark_unsupported(ir, "MIR verifier found direct call return mismatch", value->line, value->column, actual);
    return false;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    const IrValue *arg = value->args[i];
    const IrLocal *param = &callee->locals[i];
    if (!arg) {
      char actual[128];
      snprintf(actual, sizeof(actual), "missing argument %zu for %s", i, callee->name ? callee->name : "<unnamed>");
      mir_verify_mark_unsupported(ir, "MIR verifier found null direct call argument", value->line, value->column, actual);
      return false;
    }
    if (!param->is_param || arg->type != param->type) {
      char actual[192];
      snprintf(actual, sizeof(actual), "argument %zu has %s but parameter has %s", i, mir_type_kind_name(arg->type), mir_type_kind_name(param->type));
      mir_verify_mark_unsupported(ir, "MIR verifier found direct call argument ABI mismatch", arg->line, arg->column, actual);
      return false;
    }
    if (arg->type == IR_TYPE_BYTE_VIEW) {
      IrTypeKind actual_element = mir_view_element_or_u8(arg->element_type);
      IrTypeKind expected_element = mir_view_element_or_u8(param->element_type);
      if (actual_element != expected_element) {
        char actual[192];
        snprintf(actual, sizeof(actual), "argument %zu element %s but parameter element is %s", i, mir_type_kind_name(actual_element), mir_type_kind_name(expected_element));
        mir_verify_mark_unsupported(ir, "MIR verifier found direct call byte-view argument element mismatch", arg->line, arg->column, actual);
        return false;
      }
    }
  }
  return true;
}

static bool mir_verify_value_type(IrProgram *ir, const IrValue *value, IrTypeKind expected, const char *message, const char *role) {
  if (!ir || !ir->mir_valid) return false;
  if (value && value->type == expected) return true;
  char actual[160];
  snprintf(actual, sizeof(actual), "%s is %s but expected %s", role ? role : "value", value ? mir_type_kind_name(value->type) : "missing", mir_type_kind_name(expected));
  mir_verify_mark_unsupported(ir, message, value ? value->line : 1, value ? value->column : 1, actual);
  return false;
}

static bool mir_verify_helper_result_type(IrProgram *ir, const IrValue *value, IrTypeKind expected, const char *role) {
  return mir_verify_value_type(ir, value, expected, "MIR verifier found helper result type mismatch", role);
}

static bool mir_verify_value_is_integer(IrProgram *ir, const IrValue *value, const char *message, const char *role) {
  if (!ir || !ir->mir_valid) return false;
  if (value && mir_type_is_integer_value(value->type)) return true;
  char actual[160];
  snprintf(actual, sizeof(actual), "%s is %s", role ? role : "value", value ? mir_type_kind_name(value->type) : "missing");
  mir_verify_mark_unsupported(ir, message, value ? value->line : 1, value ? value->column : 1, actual);
  return false;
}

static bool mir_verify_byte_view_len_result(IrProgram *ir, const IrValue *value) {
  if (!ir || !ir->mir_valid) return false;
  if (value && (value->type == IR_TYPE_USIZE || value->type == IR_TYPE_U32)) return true;
  char actual[160];
  snprintf(actual, sizeof(actual), "byte-view length is %s but expected usize or u32", value ? mir_type_kind_name(value->type) : "missing");
  mir_verify_mark_unsupported(ir, "MIR verifier found byte-view length result type mismatch", value ? value->line : 1, value ? value->column : 1, actual);
  return false;
}

static bool mir_verify_local_value_kind(IrProgram *ir, const IrFunction *fun, unsigned index, IrTypeKind expected, int line, int column, const char *message, const char *role) {
  if (!mir_verify_local_index(ir, fun, index, line, column, message)) return false;
  const IrLocal *local = &fun->locals[index];
  if (local->type == expected) return true;
  char actual[192];
  snprintf(actual, sizeof(actual), "%s local %s has %s but expected %s", role ? role : "helper", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type), mir_type_kind_name(expected));
  mir_verify_mark_unsupported(ir, message, line, column, actual);
  return false;
}

static bool mir_verify_mutable_local_value_kind(IrProgram *ir, const IrFunction *fun, unsigned index, IrTypeKind expected, int line, int column, const char *message, const char *role) {
  if (!mir_verify_local_value_kind(ir, fun, index, expected, line, column, message, role)) return false;
  const IrLocal *local = &fun->locals[index];
  if (local->is_mutable) return true;
  char actual[192];
  snprintf(actual, sizeof(actual), "%s local %s has %s and is immutable", role ? role : "helper", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type));
  mir_verify_mark_unsupported(ir, message, line, column, actual);
  return false;
}

static bool mir_verify_record_field_span(IrProgram *ir, const IrLocal *local, unsigned field_offset, IrTypeKind type, int line, int column, const char *message) {
  if (!ir || !ir->mir_valid || !local) return false;
  unsigned byte_size = type == IR_TYPE_BYTE_VIEW ? 16 : mir_type_byte_size(type);
  unsigned storage_size = local->is_record_ref ? local->ref_byte_size : local->byte_size;
  if (byte_size > 0 && field_offset <= storage_size && byte_size <= storage_size - field_offset) return true;
  char actual[192];
  snprintf(actual, sizeof(actual), "field offset %u width %u in local size %u", field_offset, byte_size, storage_size);
  mir_verify_mark_unsupported(ir, message, line, column, actual);
  return false;
}

static bool mir_value_produces_mutable_byte_payload(const IrValue *value) {
  return value && value->type == IR_TYPE_MAYBE_BYTE_VIEW &&
         (value->kind == IR_VALUE_ALLOC_BYTES ||
          value->kind == IR_VALUE_FS_READ_ALL ||
          value->kind == IR_VALUE_FS_TEMP_NAME);
}

static bool mir_verify_mutable_byte_storage(IrProgram *ir, const IrFunction *fun, const MirVerifierState *state, const IrValue *value, const char *message, const char *role) {
  if (!mir_verify_value_type(ir, value, IR_TYPE_BYTE_VIEW, message, role)) return false;
  if (value->kind == IR_VALUE_LOCAL) {
    if (!mir_verify_local_index(ir, fun, value->local_index, value->line, value->column, message)) return false;
    const IrLocal *local = &fun->locals[value->local_index];
    if (local->type == IR_TYPE_BYTE_VIEW && local->is_mutable) return true;
    char actual[192];
    snprintf(actual, sizeof(actual), "%s local %s has %s and is %s", role ? role : "storage", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type), local->is_mutable ? "mutable" : "immutable");
    mir_verify_mark_unsupported(ir, message, value->line, value->column, actual);
    return false;
  }
  if (value->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (!mir_verify_local_index(ir, fun, value->array_index, value->line, value->column, message)) return false;
    const IrLocal *local = &fun->locals[value->array_index];
    if (local->is_array && local->element_type == IR_TYPE_U8 && local->is_mutable) return true;
    if ((local->is_record || local->is_record_ref) && local->is_mutable && value->element_type == IR_TYPE_U8 &&
        value->field_offset <= (local->is_record_ref ? local->ref_byte_size : local->byte_size) &&
        value->data_len <= (local->is_record_ref ? local->ref_byte_size : local->byte_size) - value->field_offset) {
      return true;
    }
    char actual[192];
    snprintf(actual, sizeof(actual), "%s local %s is %s byte storage and is %s", role ? role : "storage", local->name ? local->name : "<unnamed>", (local->is_array && local->element_type == IR_TYPE_U8) || (local->is_record && value->element_type == IR_TYPE_U8) ? "a" : "not a", local->is_mutable ? "mutable" : "immutable");
    mir_verify_mark_unsupported(ir, message, value->line, value->column, actual);
    return false;
  }
  if (value->kind == IR_VALUE_BYTE_SLICE) {
    if (value->index && !mir_verify_value_is_integer(ir, value->index, message, "slice start")) return false;
    if (value->right && !mir_verify_value_is_integer(ir, value->right, message, "slice end")) return false;
    return mir_verify_mutable_byte_storage(ir, fun, state, value->left, message, role);
  }
  if (value->kind == IR_VALUE_MAYBE_VALUE) {
    if (!mir_verify_local_index(ir, fun, value->local_index, value->line, value->column, message)) return false;
    const IrLocal *local = &fun->locals[value->local_index];
    if (local->type == IR_TYPE_MAYBE_BYTE_VIEW &&
        mir_state_has_mutable_maybe_byte_payload(state, value->local_index)) {
      return true;
    }
    char actual[192];
    snprintf(actual, sizeof(actual), "%s Maybe local %s has %s and is not known mutable storage", role ? role : "storage", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type));
    mir_verify_mark_unsupported(ir, message, value->line, value->column, actual);
    return false;
  }
  char actual[160];
  snprintf(actual, sizeof(actual), "%s is not backed by mutable local byte storage", role ? role : "storage");
  mir_verify_mark_unsupported(ir, message, value->line, value->column, actual);
  return false;
}

static bool mir_verify_mutable_typed_span_storage(IrProgram *ir, const IrFunction *fun, const MirVerifierState *state, const IrValue *value, IrTypeKind expected_element, const char *message, const char *role) {
  if (!mir_verify_value_type(ir, value, IR_TYPE_BYTE_VIEW, message, role)) return false;
  if (value->element_type != expected_element) {
    char actual[160];
    snprintf(actual, sizeof(actual), "%s element is %s but expected %s", role ? role : "span", mir_type_kind_name(value->element_type), mir_type_kind_name(expected_element));
    mir_verify_mark_unsupported(ir, message, value->line, value->column, actual);
    return false;
  }
  if (value->kind == IR_VALUE_LOCAL) {
    if (!mir_verify_local_index(ir, fun, value->local_index, value->line, value->column, message)) return false;
    const IrLocal *local = &fun->locals[value->local_index];
    if (local->type == IR_TYPE_BYTE_VIEW && local->is_mutable && local->element_type == expected_element) return true;
    char actual[192];
    snprintf(actual, sizeof(actual), "%s local %s has %s/%s and is %s", role ? role : "span", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type), mir_type_kind_name(local->element_type), local->is_mutable ? "mutable" : "immutable");
    mir_verify_mark_unsupported(ir, message, value->line, value->column, actual);
    return false;
  }
  if (value->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (!mir_verify_local_index(ir, fun, value->array_index, value->line, value->column, message)) return false;
    const IrLocal *local = &fun->locals[value->array_index];
    if (local->is_array && local->element_type == expected_element && local->is_mutable) return true;
    if ((local->is_record || local->is_record_ref) && local->is_mutable && value->element_type == expected_element &&
        mir_verify_record_field_span(ir, local, value->field_offset, expected_element, value->line, value->column, message)) {
      return true;
    }
    char actual[192];
    snprintf(actual, sizeof(actual), "%s local %s is %s/%s and is %s", role ? role : "span", local->name ? local->name : "<unnamed>", local->is_array ? "array" : (local->is_record ? "record" : "not array"), mir_type_kind_name(local->element_type), local->is_mutable ? "mutable" : "immutable");
    mir_verify_mark_unsupported(ir, message, value->line, value->column, actual);
    return false;
  }
  if (value->kind == IR_VALUE_BYTE_SLICE) {
    if (value->index && !mir_verify_value_is_integer(ir, value->index, message, "slice start")) return false;
    if (value->right && !mir_verify_value_is_integer(ir, value->right, message, "slice end")) return false;
    return mir_verify_mutable_typed_span_storage(ir, fun, state, value->left, expected_element, message, role);
  }
  (void)state;
  char actual[160];
  snprintf(actual, sizeof(actual), "%s is not backed by mutable typed span storage", role ? role : "span");
  mir_verify_mark_unsupported(ir, message, value->line, value->column, actual);
  return false;
}

static bool mir_verify_http_request_matches_contract(IrProgram *ir, const IrValue *value, MirHelperRequirements *requirements) {
  mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, "std.http.requestMatches");
  mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, "std.http.requestMatches");
  if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "HTTP request matcher result")) return false;
  if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid HTTP request matcher input", "HTTP request")) return false;
  if (!mir_verify_value_type(ir, value->index, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid HTTP request matcher method", "HTTP method")) return false;
  return mir_verify_value_type(ir, value->right, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid HTTP request matcher path", "HTTP path");
}

static bool mir_verify_http_request_span_contract(IrProgram *ir, const IrValue *value, MirHelperRequirements *requirements) {
  const char *helper = value->kind == IR_VALUE_HTTP_REQUEST_METHOD_NAME ? "std.http.requestMethodName" : "std.http.requestPath";
  mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, helper);
  mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, helper);
  if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_BYTE_VIEW, "HTTP request span result")) return false;
  return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid HTTP request helper input", "HTTP request");
}

static bool mir_verify_http_request_body_contract(IrProgram *ir, const IrValue *value, MirHelperRequirements *requirements) {
  const char *helper = value->int_value ? "std.http.requestJsonBodyWithin" : "std.http.requestBodyWithin";
  mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, helper);
  mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, helper);
  if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_BYTE_VIEW, "HTTP request body result")) return false;
  if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid HTTP request body input", "HTTP request")) return false;
  return mir_verify_value_type(ir, value->index, IR_TYPE_USIZE, "MIR verifier found invalid HTTP request body limit", "HTTP body limit");
}

static bool mir_verify_direct_helper_value_contract(IrProgram *ir, const IrFunction *fun, const MirVerifierState *state, const IrValue *value, MirHelperRequirements *requirements) {
  if (!ir || !ir->mir_valid || !value) return ir && ir->mir_valid;
  switch (value->kind) {
    case IR_VALUE_FIXED_BUF_ALLOC:
      mir_require_count(&requirements->allocator_helpers, 1, value->line, value->column, "std.mem.fixedBufAlloc");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_ALLOC, "FixedBufAlloc result")) return false;
      return mir_verify_mutable_byte_storage(ir, fun, state, value->left, "MIR verifier found invalid FixedBufAlloc helper value", "allocator storage");
    case IR_VALUE_ALLOC_BYTES:
      mir_require_count(&requirements->allocator_helpers, 2, value->line, value->column, "std.mem.allocBytes");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_BYTE_VIEW, "allocation result")) return false;
      if (!mir_verify_mutable_local_value_kind(ir, fun, value->local_index, IR_TYPE_ALLOC, value->line, value->column, "MIR verifier found invalid allocation helper target", "allocator")) return false;
      return mir_verify_value_is_integer(ir, value->left, "MIR verifier found invalid allocation helper length", "allocation length");
    case IR_VALUE_VEC_INIT:
      mir_require_count(&requirements->buffer_helpers, 1, value->line, value->column, "std.mem.vec");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_VEC, "Vec result")) return false;
      return mir_verify_mutable_byte_storage(ir, fun, state, value->left, "MIR verifier found invalid Vec helper value", "Vec storage");
    case IR_VALUE_VEC_PUSH:
      mir_require_count(&requirements->buffer_helpers, 2, value->line, value->column, "std.mem.vecPush");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "Vec push result")) return false;
      if (!mir_verify_mutable_local_value_kind(ir, fun, value->local_index, IR_TYPE_VEC, value->line, value->column, "MIR verifier found invalid Vec helper target", "Vec")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_U8, "MIR verifier found invalid Vec push value", "Vec item");
    case IR_VALUE_VEC_LEN:
    case IR_VALUE_VEC_CAPACITY:
      mir_require_count(&requirements->buffer_helpers, 3, value->line, value->column, value->kind == IR_VALUE_VEC_LEN ? "std.mem.vecLen" : "std.mem.vecCapacity");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, value->kind == IR_VALUE_VEC_LEN ? "Vec length result" : "Vec capacity result")) return false;
      return mir_verify_local_value_kind(ir, fun, value->local_index, IR_TYPE_VEC, value->line, value->column, "MIR verifier found invalid Vec helper target", "Vec");
    case IR_VALUE_VEC_BYTES:
      mir_require_count(&requirements->buffer_helpers, 4, value->line, value->column, "std.mem.vecBytes");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BYTE_VIEW, "Vec bytes result")) return false;
      return mir_verify_local_value_kind(ir, fun, value->local_index, IR_TYPE_VEC, value->line, value->column, "MIR verifier found invalid Vec helper target", "Vec");
    case IR_VALUE_VEC_GET:
      mir_require_count(&requirements->buffer_helpers, 5, value->line, value->column, "std.mem.vecGet");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_SCALAR, "Vec get result")) return false;
      if (value->element_type != IR_TYPE_U8) {
        mir_verify_mark_unsupported(ir, "MIR verifier found invalid Vec get element type", value->line, value->column, mir_type_kind_name(value->element_type));
        return false;
      }
      if (!mir_verify_local_value_kind(ir, fun, value->local_index, IR_TYPE_VEC, value->line, value->column, "MIR verifier found invalid Vec helper target", "Vec")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_USIZE, "MIR verifier found invalid Vec get index", "Vec index");
    case IR_VALUE_VEC_SET:
      mir_require_count(&requirements->buffer_helpers, 5, value->line, value->column, "std.mem.vecSet");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "Vec set result")) return false;
      if (value->element_type != IR_TYPE_U8) {
        mir_verify_mark_unsupported(ir, "MIR verifier found invalid Vec set element type", value->line, value->column, mir_type_kind_name(value->element_type));
        return false;
      }
      if (!mir_verify_mutable_local_value_kind(ir, fun, value->local_index, IR_TYPE_VEC, value->line, value->column, "MIR verifier found invalid Vec helper target", "Vec")) return false;
      if (!mir_verify_value_type(ir, value->left, IR_TYPE_USIZE, "MIR verifier found invalid Vec set index", "Vec index")) return false;
      return mir_verify_value_type(ir, value->right, IR_TYPE_U8, "MIR verifier found invalid Vec set value", "Vec item");
    case IR_VALUE_VEC_REMOVE_SWAP:
      mir_require_count(&requirements->buffer_helpers, 5, value->line, value->column, "std.mem.vecRemoveSwap");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "Vec swap-remove result")) return false;
      if (value->element_type != IR_TYPE_U8) {
        mir_verify_mark_unsupported(ir, "MIR verifier found invalid Vec swap-remove element type", value->line, value->column, mir_type_kind_name(value->element_type));
        return false;
      }
      if (!mir_verify_mutable_local_value_kind(ir, fun, value->local_index, IR_TYPE_VEC, value->line, value->column, "MIR verifier found invalid Vec helper target", "Vec")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_USIZE, "MIR verifier found invalid Vec swap-remove index", "Vec index");
    case IR_VALUE_VEC_INDEX:
      mir_require_count(&requirements->buffer_helpers, 6, value->line, value->column, "std.mem.vecIndex");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "Vec index result")) return false;
      if (value->element_type != IR_TYPE_U8) {
        mir_verify_mark_unsupported(ir, "MIR verifier found invalid Vec index element type", value->line, value->column, mir_type_kind_name(value->element_type));
        return false;
      }
      if (!mir_verify_local_value_kind(ir, fun, value->local_index, IR_TYPE_VEC, value->line, value->column, "MIR verifier found invalid Vec helper target", "Vec")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_U8, "MIR verifier found invalid Vec index value", "Vec item");
    case IR_VALUE_VEC_CONTAINS:
      mir_require_count(&requirements->buffer_helpers, 6, value->line, value->column, "std.mem.vecContains");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "Vec contains result")) return false;
      if (value->element_type != IR_TYPE_U8) {
        mir_verify_mark_unsupported(ir, "MIR verifier found invalid Vec contains element type", value->line, value->column, mir_type_kind_name(value->element_type));
        return false;
      }
      if (!mir_verify_local_value_kind(ir, fun, value->local_index, IR_TYPE_VEC, value->line, value->column, "MIR verifier found invalid Vec helper target", "Vec")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_U8, "MIR verifier found invalid Vec contains value", "Vec item");
    case IR_VALUE_VEC_INSERT_UNIQUE:
      mir_require_count(&requirements->buffer_helpers, 6, value->line, value->column, "std.mem.vecInsertUnique");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "Vec insert-unique result")) return false;
      if (value->element_type != IR_TYPE_U8) {
        mir_verify_mark_unsupported(ir, "MIR verifier found invalid Vec insert-unique element type", value->line, value->column, mir_type_kind_name(value->element_type));
        return false;
      }
      if (!mir_verify_mutable_local_value_kind(ir, fun, value->local_index, IR_TYPE_VEC, value->line, value->column, "MIR verifier found invalid Vec helper target", "Vec")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_U8, "MIR verifier found invalid Vec insert-unique value", "Vec item");
    case IR_VALUE_VEC_REMOVE_VALUE:
      mir_require_count(&requirements->buffer_helpers, 6, value->line, value->column, "std.mem.vecRemoveValue");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "Vec remove-value result")) return false;
      if (value->element_type != IR_TYPE_U8) {
        mir_verify_mark_unsupported(ir, "MIR verifier found invalid Vec remove-value element type", value->line, value->column, mir_type_kind_name(value->element_type));
        return false;
      }
      if (!mir_verify_mutable_local_value_kind(ir, fun, value->local_index, IR_TYPE_VEC, value->line, value->column, "MIR verifier found invalid Vec helper target", "Vec")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_U8, "MIR verifier found invalid Vec remove-value value", "Vec item");
    case IR_VALUE_VEC_CLEAR:
      mir_require_count(&requirements->buffer_helpers, 4, value->line, value->column, "std.mem.vecClear");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "Vec clear result")) return false;
      return mir_verify_mutable_local_value_kind(ir, fun, value->local_index, IR_TYPE_VEC, value->line, value->column, "MIR verifier found invalid Vec helper target", "Vec");
    case IR_VALUE_VEC_POP:
      mir_require_count(&requirements->buffer_helpers, 4, value->line, value->column, "std.mem.vecPop");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "Vec pop result")) return false;
      return mir_verify_mutable_local_value_kind(ir, fun, value->local_index, IR_TYPE_VEC, value->line, value->column, "MIR verifier found invalid Vec helper target", "Vec");
    case IR_VALUE_VEC_TRUNCATE:
      mir_require_count(&requirements->buffer_helpers, 4, value->line, value->column, "std.mem.vecTruncate");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "Vec truncate result")) return false;
      if (!mir_verify_mutable_local_value_kind(ir, fun, value->local_index, IR_TYPE_VEC, value->line, value->column, "MIR verifier found invalid Vec helper target", "Vec")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_USIZE, "MIR verifier found invalid Vec truncate length", "Vec length");
    case IR_VALUE_JSON_PARSE_BYTES:
      mir_require_count(&requirements->allocator_helpers, 2, value->line, value->column, "std.json.parseBytes");
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, "std.json.parseBytes");
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, "std.json.parseBytes");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_I64, "JSON parse result")) return false;
      if (!mir_verify_mutable_local_value_kind(ir, fun, value->local_index, IR_TYPE_ALLOC, value->line, value->column, "MIR verifier found invalid JSON parse allocator", "allocator")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid JSON runtime helper input", "JSON bytes");
    case IR_VALUE_JSON_VALIDATE_BYTES:
    case IR_VALUE_JSON_STREAM_TOKENS_BYTES:
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, value->kind == IR_VALUE_JSON_VALIDATE_BYTES ? "std.json.validateBytes" : "std.json.streamTokensBytes");
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, value->kind == IR_VALUE_JSON_VALIDATE_BYTES ? "std.json.validateBytes" : "std.json.streamTokensBytes");
      if (!mir_verify_helper_result_type(ir, value, value->kind == IR_VALUE_JSON_VALIDATE_BYTES ? IR_TYPE_BOOL : IR_TYPE_USIZE, value->kind == IR_VALUE_JSON_VALIDATE_BYTES ? "JSON validate result" : "JSON token count result")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid JSON runtime helper input", "JSON bytes");
    case IR_VALUE_HTTP_FETCH:
      mir_require_count(&requirements->runtime_helpers, 2, value->line, value->column, "std.http.fetch");
      mir_require_count(&requirements->host_runtime_imports, 2, value->line, value->column, "std.http.fetch");
      mir_require_count(&requirements->http_runtime_imports, 1, value->line, value->column, "std.http.fetch");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_U64, "HTTP fetch result")) return false;
      if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid HTTP fetch request", "HTTP request")) return false;
      if (!mir_verify_mutable_byte_storage(ir, fun, state, value->right, "MIR verifier found invalid HTTP fetch response buffer", "HTTP response buffer")) return false;
      return mir_verify_value_type(ir, value->index, IR_TYPE_I64, "MIR verifier found invalid HTTP fetch timeout", "HTTP timeout");
    case IR_VALUE_HTTP_RESULT_OK:
    case IR_VALUE_HTTP_RESULT_STATUS:
    case IR_VALUE_HTTP_RESULT_BODY_LEN:
    case IR_VALUE_HTTP_RESULT_ERROR:
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, "std.http result helper");
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, "std.http result helper");
      if (!mir_verify_helper_result_type(ir, value, value->kind == IR_VALUE_HTTP_RESULT_OK ? IR_TYPE_BOOL : (value->kind == IR_VALUE_HTTP_RESULT_STATUS ? IR_TYPE_U16 : (value->kind == IR_VALUE_HTTP_RESULT_BODY_LEN ? IR_TYPE_USIZE : IR_TYPE_U32)), "HTTP result helper result")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_U64, "MIR verifier found invalid HTTP result helper input", "HTTP result");
    case IR_VALUE_HTTP_RESPONSE_LEN:
    case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN:
    case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET:
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, "std.http response helper");
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, "std.http response helper");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "HTTP response helper result")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid HTTP response helper input", "HTTP response");
    case IR_VALUE_HTTP_HEADER_VALUE:
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, "std.http.headerValue");
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, "std.http.headerValue");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_U64, "HTTP header value result")) return false;
      if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid HTTP header helper input", "HTTP headers")) return false;
      return mir_verify_value_type(ir, value->right, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid HTTP header helper input", "HTTP header name");
    case IR_VALUE_HTTP_HEADER_FOUND:
    case IR_VALUE_HTTP_HEADER_OFFSET:
    case IR_VALUE_HTTP_HEADER_LEN:
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, "std.http header result helper");
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, "std.http header result helper");
      if (!mir_verify_helper_result_type(ir, value, value->kind == IR_VALUE_HTTP_HEADER_FOUND ? IR_TYPE_BOOL : IR_TYPE_USIZE, "HTTP header result helper result")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_U64, "MIR verifier found invalid HTTP header result helper input", "HTTP header result");
    case IR_VALUE_HTTP_REQUEST_METHOD_NAME: case IR_VALUE_HTTP_REQUEST_PATH:
      return mir_verify_http_request_span_contract(ir, value, requirements);
    case IR_VALUE_HTTP_REQUEST_MATCHES: return mir_verify_http_request_matches_contract(ir, value, requirements);
    case IR_VALUE_HTTP_REQUEST_BODY_WITHIN: return mir_verify_http_request_body_contract(ir, value, requirements);
    case IR_VALUE_HTTP_WRITE_JSON_RESPONSE:
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, "std.http.writeJsonResponse");
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, "std.http.writeJsonResponse");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_BYTE_VIEW, "HTTP JSON response write result")) return false;
      if (!mir_verify_mutable_byte_storage(ir, fun, state, value->left, "MIR verifier found invalid HTTP response write buffer", "HTTP response buffer")) return false;
      if (!mir_verify_value_type(ir, value->index, IR_TYPE_U16, "MIR verifier found invalid HTTP response status", "HTTP status")) return false;
      return mir_verify_value_type(ir, value->right, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid HTTP response body", "HTTP response body");
    case IR_VALUE_HTTP_STATUS_CLASS:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "HTTP status predicate result")) return false;
      if (!mir_verify_value_type(ir, value->left, IR_TYPE_U16, "MIR verifier found invalid HTTP status predicate input", "HTTP status")) return false;
      if (value->int_value >= value->data_len || value->data_len > 1000) {
        mir_verify_mark_unsupported(ir, "MIR verifier found invalid HTTP status predicate bounds", value->line, value->column, "invalid status class bounds");
        return false;
      }
      return true;
    case IR_VALUE_PARSE_I32:
    case IR_VALUE_PARSE_U32: {
      bool signed_parse = value->kind == IR_VALUE_PARSE_I32;
      IrTypeKind element_type = signed_parse ? IR_TYPE_I32 : IR_TYPE_U32;
      const char *name = signed_parse ? "std.parse.parseI32" : "std.parse.parseU32";
      const char *role = signed_parse ? "parseI32 result" : "parseU32 result";
      const char *input = signed_parse ? "parseI32 text" : "parseU32 text";
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, name);
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, name);
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_SCALAR, role)) return false;
      if (value->element_type != element_type) {
        mir_verify_mark_unsupported(ir, signed_parse ? "MIR verifier found parseI32 element type mismatch" : "MIR verifier found parseU32 element type mismatch", value->line, value->column, mir_type_kind_name(value->element_type));
        return false;
      }
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, signed_parse ? "MIR verifier found invalid parseI32 input" : "MIR verifier found invalid parseU32 input", input);
    }
    case IR_VALUE_PARSE_RUNTIME: {
      IrTypeKind result_type = IR_TYPE_USIZE;
      IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
      const char *name = "std.parse helper";
      switch ((IrParseOp)value->int_value) {
        case IR_PARSE_OP_IS_ASCII_DIGIT:
        case IR_PARSE_OP_IS_ASCII_ALPHA:
        case IR_PARSE_OP_IS_IDENTIFIER_START:
        case IR_PARSE_OP_IS_WHITESPACE:
          result_type = IR_TYPE_BOOL;
          break;
        case IR_PARSE_OP_SCAN_DIGITS:
        case IR_PARSE_OP_SCAN_IDENTIFIER:
        case IR_PARSE_OP_SCAN_UNTIL_BYTE:
        case IR_PARSE_OP_SCAN_WHITESPACE:
          result_type = IR_TYPE_USIZE;
          break;
        case IR_PARSE_OP_PARSE_BOOL:
          result_type = IR_TYPE_MAYBE_SCALAR;
          element_type = IR_TYPE_BOOL;
          break;
        case IR_PARSE_OP_PARSE_U8:
          result_type = IR_TYPE_MAYBE_SCALAR;
          element_type = IR_TYPE_U8;
          break;
        case IR_PARSE_OP_PARSE_U16:
          result_type = IR_TYPE_MAYBE_SCALAR;
          element_type = IR_TYPE_U16;
          break;
        case IR_PARSE_OP_PARSE_USIZE:
          result_type = IR_TYPE_MAYBE_SCALAR;
          element_type = IR_TYPE_USIZE;
          name = "std.parse.parseUsize";
          break;
        default:
          mir_verify_mark_unsupported(ir, "MIR verifier found unknown std.parse runtime op", value->line, value->column, "invalid std.parse op");
          return false;
      }
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, name);
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, name);
      if (!mir_verify_helper_result_type(ir, value, result_type, "std.parse runtime result")) return false;
      if (result_type == IR_TYPE_MAYBE_SCALAR && value->element_type != element_type) {
        mir_verify_mark_unsupported(ir, "MIR verifier found std.parse runtime element type mismatch", value->line, value->column, mir_type_kind_name(value->element_type));
        return false;
      }
      if (value->arg_len < 1 || value->arg_len > 2) {
        mir_verify_mark_unsupported(ir, "MIR verifier found std.parse runtime arity mismatch", value->line, value->column, "invalid std.parse arity");
        return false;
      }
      if (!mir_verify_value_type(ir, value->args[0], IR_TYPE_BYTE_VIEW, "MIR verifier found invalid std.parse runtime input", "std.parse input")) return false;
      if (value->arg_len == 2) return mir_verify_value_type(ir, value->args[1], IR_TYPE_U8, "MIR verifier found invalid std.parse runtime byte argument", "std.parse byte argument");
      return true;
    }
    case IR_VALUE_ARGS_PARSE_U32:
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, "std.args.parseU32");
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, "std.args.parseU32");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_SCALAR, "args parseU32 result")) return false;
      if (value->element_type != IR_TYPE_U32) {
        mir_verify_mark_unsupported(ir, "MIR verifier found args parseU32 element type mismatch", value->line, value->column, mir_type_kind_name(value->element_type));
        return false;
      }
      return mir_verify_value_is_integer(ir, value->left, "MIR verifier found invalid args parseU32 index", "args parseU32 index");
    case IR_VALUE_ARGS_FIND:
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, "std.args.find");
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, "std.args.find");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_SCALAR, "args find result")) return false;
      if (value->element_type != IR_TYPE_USIZE) {
        mir_verify_mark_unsupported(ir, "MIR verifier found args find element type mismatch", value->line, value->column, mir_type_kind_name(value->element_type));
        return false;
      }
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid args find name", "args find name");
    case IR_VALUE_ARGS_CONTAINS:
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, "std.cli.hasFlag");
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, "std.cli.hasFlag");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "args contains result")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid args contains name", "args contains name");
    case IR_VALUE_ARGS_VALUE_AFTER:
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, "std.args.valueAfter");
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, "std.args.valueAfter");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_BYTE_VIEW, "args valueAfter result")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid args valueAfter name", "args valueAfter name");
    case IR_VALUE_ARGS_VALUE_AFTER_OR:
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, "std.cli.optionValueOr");
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, "std.cli.optionValueOr");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BYTE_VIEW, "args option fallback result")) return false;
      if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid args option name", "args option name")) return false;
      return mir_verify_value_type(ir, value->right, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid args option fallback", "args option fallback");
    case IR_VALUE_ARGS_VALUE_AFTER_PARSE_U32:
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, "std.cli.optionU32");
      mir_require_count(&requirements->host_runtime_imports, 2, value->line, value->column, "std.cli.optionU32");
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_SCALAR, "args optionU32 result")) return false;
      if (value->element_type != IR_TYPE_U32) {
        mir_verify_mark_unsupported(ir, "MIR verifier found args optionU32 element type mismatch", value->line, value->column, mir_type_kind_name(value->element_type));
        return false;
      }
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid args optionU32 name", "args optionU32 name");
    case IR_VALUE_FMT_BOOL:
    case IR_VALUE_FMT_HEX_U32:
    case IR_VALUE_FMT_I32:
    case IR_VALUE_FMT_U32:
    case IR_VALUE_FMT_USIZE: {
      IrTypeKind number_type = IR_TYPE_U32;
      const char *name = "std.fmt.u32";
      const char *role = "fmt.u32 result";
      const char *buffer = "fmt.u32 buffer";
      const char *number = "fmt.u32 value";
      if (value->kind == IR_VALUE_FMT_BOOL) {
        number_type = IR_TYPE_BOOL;
        name = "std.fmt.bool";
        role = "fmt.bool result";
        buffer = "fmt.bool buffer";
        number = "fmt.bool value";
      } else if (value->kind == IR_VALUE_FMT_HEX_U32) {
        name = "std.fmt.hexLowerU32";
        role = "fmt.hexLowerU32 result";
        buffer = "fmt.hexLowerU32 buffer";
        number = "fmt.hexLowerU32 value";
      } else if (value->kind == IR_VALUE_FMT_I32) {
        number_type = IR_TYPE_I32;
        name = "std.fmt.i32";
        role = "fmt.i32 result";
        buffer = "fmt.i32 buffer";
        number = "fmt.i32 value";
      } else if (value->kind == IR_VALUE_FMT_USIZE) {
        number_type = IR_TYPE_USIZE;
        name = "std.fmt.usize";
        role = "fmt.usize result";
        buffer = "fmt.usize buffer";
        number = "fmt.usize value";
      }
      mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, name);
      mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, name);
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_BYTE_VIEW, role)) return false;
      if (!mir_verify_mutable_byte_storage(ir, fun, state, value->left, "MIR verifier found invalid fmt output buffer", buffer)) return false;
      return mir_verify_value_type(ir, value->right, number_type, "MIR verifier found invalid fmt value", number);
    }
    default:
      return true;
  }
}

static bool mir_verify_array_load_contract(IrProgram *ir, const IrFunction *fun, const IrValue *value) {
  if (!mir_verify_local_index(ir, fun, value->array_index, value->line, value->column, "MIR verifier found array load outside the local table")) return false;
  const IrLocal *local = &fun->locals[value->array_index];
  if (!local->is_array) {
    char actual[160];
    snprintf(actual, sizeof(actual), "local %s is %s", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type));
    mir_verify_mark_unsupported(ir, "MIR verifier found array load from a non-array local", value->line, value->column, actual);
    return false;
  }
  if (!mir_verify_value_is_integer(ir, value->index, "MIR verifier found invalid array load index", "array index")) return false;
  if (value->type != local->element_type) {
    char actual[160];
    snprintf(actual, sizeof(actual), "array load has %s but element is %s", mir_type_kind_name(value->type), mir_type_kind_name(local->element_type));
    mir_verify_mark_unsupported(ir, "MIR verifier found array load type mismatch", value->line, value->column, actual);
    return false;
  }
  return true;
}

static bool mir_verify_array_byte_view_contract(IrProgram *ir, const IrFunction *fun, const IrValue *value) {
  if (!mir_verify_value_type(ir, value, IR_TYPE_BYTE_VIEW, "MIR verifier found byte-view result type mismatch", "array byte view result")) return false;
  if (!mir_verify_local_index(ir, fun, value->array_index, value->line, value->column, "MIR verifier found array byte view outside the local table")) return false;
  const IrLocal *local = &fun->locals[value->array_index];
  if (local->is_array) {
    if ((!mir_type_is_value(local->element_type) && local->element_type != IR_TYPE_BOOL) || value->field_offset != 0) {
      char actual[160];
      snprintf(actual, sizeof(actual), "local %s is %s array element %s with field offset %u", local->name ? local->name : "<unnamed>", local->is_array ? "an" : "not an", mir_type_kind_name(local->element_type), value->field_offset);
      mir_verify_mark_unsupported(ir, "MIR verifier found array byte view from an unsupported array local", value->line, value->column, actual);
      return false;
    }
    if (value->element_type != local->element_type) {
      char actual[160];
      snprintf(actual, sizeof(actual), "view element %s but array element is %s", mir_type_kind_name(value->element_type), mir_type_kind_name(local->element_type));
      mir_verify_mark_unsupported(ir, "MIR verifier found array byte view element mismatch", value->line, value->column, actual);
      return false;
    }
    if (value->data_len != local->array_len) {
      char actual[128];
      snprintf(actual, sizeof(actual), "byte view length %u but array length is %u", value->data_len, local->array_len);
      mir_verify_mark_unsupported(ir, "MIR verifier found array byte view length mismatch", value->line, value->column, actual);
      return false;
    }
    return true;
  }
  if (local->is_record || local->is_record_ref) {
    if (!mir_type_is_value(value->element_type) && value->element_type != IR_TYPE_BOOL) {
      char actual[160];
      snprintf(actual, sizeof(actual), "record field view element is %s", mir_type_kind_name(value->element_type));
      mir_verify_mark_unsupported(ir, "MIR verifier found array byte view from an unsupported record field", value->line, value->column, actual);
      return false;
    }
    unsigned element_size = mir_type_byte_size(value->element_type);
    if (element_size == 0 || value->data_len > (unsigned)(~0u) / element_size) {
      char actual[128];
      snprintf(actual, sizeof(actual), "record field view length %u element %s", value->data_len, mir_type_kind_name(value->element_type));
      mir_verify_mark_unsupported(ir, "MIR verifier found array byte view record field size overflow", value->line, value->column, actual);
      return false;
    }
    unsigned storage_size = local->is_record_ref ? local->ref_byte_size : local->byte_size;
    unsigned byte_len = value->data_len * element_size;
    if (value->field_offset > storage_size || byte_len > storage_size - value->field_offset) {
      char actual[160];
      snprintf(actual, sizeof(actual), "record field offset %u width %u in local size %u", value->field_offset, byte_len, storage_size);
      mir_verify_mark_unsupported(ir, "MIR verifier found array byte view record field outside local storage", value->line, value->column, actual);
      return false;
    }
    return true;
  }
  {
    char actual[160];
    snprintf(actual, sizeof(actual), "local %s is %s array element %s", local->name ? local->name : "<unnamed>", local->is_array ? "an" : "not an", mir_type_kind_name(local->element_type));
    mir_verify_mark_unsupported(ir, "MIR verifier found array byte view from an unsupported array local", value->line, value->column, actual);
    return false;
  }
}

static bool mir_verify_maybe_value_contract(IrProgram *ir, const IrFunction *fun, const IrValue *value) {
  if (!mir_verify_local_index(ir, fun, value->local_index, value->line, value->column, "MIR verifier found maybe helper outside the local table")) return false;
  const IrLocal *local = &fun->locals[value->local_index];
  if (value->kind == IR_VALUE_MAYBE_HAS) {
    if (value->type != IR_TYPE_BOOL) {
      char actual[128];
      snprintf(actual, sizeof(actual), "Maybe.has result is %s", mir_type_kind_name(value->type));
      mir_verify_mark_unsupported(ir, "MIR verifier found maybe helper result type mismatch", value->line, value->column, actual);
      return false;
    }
    if (local->type == IR_TYPE_MAYBE_BYTE_VIEW || local->type == IR_TYPE_MAYBE_SCALAR) return true;
  } else if (value->kind == IR_VALUE_MAYBE_VALUE) {
    if (local->type == IR_TYPE_MAYBE_BYTE_VIEW) {
      return mir_verify_value_type(ir, value, IR_TYPE_BYTE_VIEW, "MIR verifier found maybe value type mismatch", "Maybe byte-view value");
    }
    if (local->type == IR_TYPE_MAYBE_SCALAR) {
      if (value->type == IR_TYPE_BOOL) return true;
      return mir_verify_value_is_integer(ir, value, "MIR verifier found maybe scalar value type mismatch", "Maybe scalar value");
    }
  }
  char actual[160];
  snprintf(actual, sizeof(actual), "local %s has %s", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type));
  mir_verify_mark_unsupported(ir, "MIR verifier found maybe helper for a non-Maybe local", value->line, value->column, actual);
  return false;
}

static bool mir_verify_mutable_item_storage(IrProgram *ir, const IrFunction *fun, const MirVerifierState *state, const IrValue *value, IrTypeKind element_type, const char *message, const char *role) {
  if (!mir_verify_value_type(ir, value, IR_TYPE_BYTE_VIEW, message, role)) return false;
  if (value->kind == IR_VALUE_LOCAL) {
    if (!mir_verify_local_index(ir, fun, value->local_index, value->line, value->column, message)) return false;
    const IrLocal *local = &fun->locals[value->local_index];
    if (local->type == IR_TYPE_BYTE_VIEW && local->is_mutable &&
        (local->element_type == IR_TYPE_UNSUPPORTED || local->element_type == element_type)) {
      return true;
    }
    char actual[192];
    snprintf(actual, sizeof(actual), "%s local %s has %s element %s and is %s", role ? role : "storage", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type), mir_type_kind_name(local->element_type), local->is_mutable ? "mutable" : "immutable");
    mir_verify_mark_unsupported(ir, message, value->line, value->column, actual);
    return false;
  }
  if (value->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (!mir_verify_local_index(ir, fun, value->array_index, value->line, value->column, message)) return false;
    const IrLocal *local = &fun->locals[value->array_index];
    if (local->is_array && local->element_type == element_type && local->is_mutable) return true;
    if ((local->is_record || local->is_record_ref) && local->is_mutable && value->element_type == element_type) return true;
    char actual[192];
    snprintf(actual, sizeof(actual), "%s local %s is %s %s storage and is %s", role ? role : "storage", local->name ? local->name : "<unnamed>", (local->is_array && local->element_type == element_type) || (local->is_record && value->element_type == element_type) ? "a" : "not a", mir_type_kind_name(element_type), local->is_mutable ? "mutable" : "immutable");
    mir_verify_mark_unsupported(ir, message, value->line, value->column, actual);
    return false;
  }
  if (value->kind == IR_VALUE_BYTE_SLICE) {
    if (value->index && !mir_verify_value_is_integer(ir, value->index, message, "slice start")) return false;
    if (value->right && !mir_verify_value_is_integer(ir, value->right, message, "slice end")) return false;
    return mir_verify_mutable_item_storage(ir, fun, state, value->left, element_type, message, role);
  }
  if (value->kind == IR_VALUE_MAYBE_VALUE) {
    if (!mir_verify_local_index(ir, fun, value->local_index, value->line, value->column, message)) return false;
    const IrLocal *local = &fun->locals[value->local_index];
    if (local->type == IR_TYPE_MAYBE_BYTE_VIEW &&
        (value->element_type == IR_TYPE_UNSUPPORTED || value->element_type == element_type) &&
        mir_state_has_mutable_maybe_byte_payload(state, value->local_index)) {
      return true;
    }
  }
  char actual[160];
  snprintf(actual, sizeof(actual), "%s is %s", role ? role : "storage", value ? "an unsupported value kind" : "missing");
  mir_verify_mark_unsupported(ir, message, value ? value->line : 1, value ? value->column : 1, actual);
  return false;
}

static bool mir_verify_byte_view_value_contract(IrProgram *ir, const IrValue *value) {
  if (!mir_verify_value_type(ir, value, IR_TYPE_BYTE_VIEW, "MIR verifier found byte-view result type mismatch", "byte-view result")) return false;
  if (value->kind == IR_VALUE_BYTE_SLICE) {
    if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid byte slice base", "slice base")) return false;
    if (value->index && !mir_verify_value_is_integer(ir, value->index, "MIR verifier found invalid byte slice start", "slice start")) return false;
    if (value->right && !mir_verify_value_is_integer(ir, value->right, "MIR verifier found invalid byte slice end", "slice end")) return false;
  }
  return true;
}

static bool mir_verify_byte_mutation_value_contract(IrProgram *ir, const IrFunction *fun, const MirVerifierState *state, const IrValue *value) {
  if (!ir || !ir->mir_valid || !value) return false;
  if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, value->kind == IR_VALUE_BYTE_COPY ? "byte copy result" : "byte fill result")) return false;
  if (value->kind == IR_VALUE_BYTE_COPY) {
    if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid byte copy source", "byte copy source")) return false;
    return mir_verify_mutable_byte_storage(ir, fun, state, value->right, "MIR verifier found invalid byte copy destination", "byte copy destination");
  }
  if (value->kind == IR_VALUE_BYTE_FILL) {
    if (!mir_verify_value_type(ir, value->left, IR_TYPE_U8, "MIR verifier found invalid byte fill value", "byte fill value")) return false;
    return mir_verify_mutable_byte_storage(ir, fun, state, value->right, "MIR verifier found invalid byte fill destination", "byte fill destination");
  }
  return true;
}

static bool mir_verify_item_element_type(IrProgram *ir, IrTypeKind type, const IrValue *site, const char *role) {
  if (type == IR_TYPE_BOOL || mir_type_is_value(type)) return true;
  char actual[160];
  snprintf(actual, sizeof(actual), "%s has %s", role ? role : "item", mir_type_kind_name(type));
  mir_verify_mark_unsupported(ir, "MIR verifier found unsupported item helper element type", site ? site->line : 1, site ? site->column : 1, actual);
  return false;
}

static bool mir_verify_item_helper_value_contract(IrProgram *ir, const IrFunction *fun, const MirVerifierState *state, const IrValue *value) {
  if (!ir || !ir->mir_valid || !value) return false;
  if (!mir_verify_item_element_type(ir, value->element_type, value, "item helper element")) return false;
  if (value->kind == IR_VALUE_ITEM_COPY) {
    if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "item copy result")) return false;
    if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid item copy source", "item copy source")) return false;
    if (value->left->element_type != IR_TYPE_UNSUPPORTED && value->left->element_type != value->element_type) {
      mir_verify_mark_unsupported(ir, "MIR verifier found item copy source element mismatch", value->line, value->column, "source element does not match copy element");
      return false;
    }
    if (!mir_verify_mutable_item_storage(ir, fun, state, value->right, value->element_type, "MIR verifier found invalid item copy destination", "item copy destination")) return false;
    if (value->right && value->right->element_type != IR_TYPE_UNSUPPORTED && value->right->element_type != value->element_type) {
      mir_verify_mark_unsupported(ir, "MIR verifier found item copy destination element mismatch", value->line, value->column, "destination element does not match copy element");
      return false;
    }
    return true;
  }
  if (value->kind == IR_VALUE_ITEM_FILL) {
    if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "item fill result")) return false;
    if (!mir_verify_value_type(ir, value->left, value->element_type, "MIR verifier found invalid item fill value", "item fill value")) return false;
    if (!mir_verify_mutable_item_storage(ir, fun, state, value->right, value->element_type, "MIR verifier found invalid item fill destination", "item fill destination")) return false;
    if (value->right && value->right->element_type != IR_TYPE_UNSUPPORTED && value->right->element_type != value->element_type) {
      mir_verify_mark_unsupported(ir, "MIR verifier found item fill destination element mismatch", value->line, value->column, "destination element does not match fill element");
      return false;
    }
    return true;
  }
  if (value->kind == IR_VALUE_ITEM_CONTAINS) {
    if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "item contains result")) return false;
    if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid item contains input", "item contains input")) return false;
    if (value->left && value->left->element_type != IR_TYPE_UNSUPPORTED && value->left->element_type != value->element_type) {
      mir_verify_mark_unsupported(ir, "MIR verifier found item contains input element mismatch", value->line, value->column, "input element does not match contains element");
      return false;
    }
    return mir_verify_value_type(ir, value->right, value->element_type, "MIR verifier found invalid item contains needle", "item contains needle");
  }
  return true;
}

static bool mir_verify_fallible_flow_value_contract(IrProgram *ir, const IrFunction *fun, const IrValue *value) {
  if (!ir || !ir->mir_valid || !value) return false;
  if (value->kind == IR_VALUE_CHECK) {
    if (!value->left || value->left->type != IR_TYPE_I64) {
      char actual[160];
      snprintf(actual, sizeof(actual), "check input is %s", value->left ? mir_type_kind_name(value->left->type) : "missing");
      mir_verify_mark_unsupported(ir, "MIR verifier found invalid check input", value->line, value->column, actual);
      return false;
    }
    if (value->type != value->left->element_type) {
      char actual[160];
      snprintf(actual, sizeof(actual), "check result is %s but fallible value carries %s", mir_type_kind_name(value->type), mir_type_kind_name(value->left->element_type));
      mir_verify_mark_unsupported(ir, "MIR verifier found check result type mismatch", value->line, value->column, actual);
      return false;
    }
    if (!fun || (!fun->raises && !fun->world_param_name)) {
      mir_verify_mark_unsupported(ir, "MIR verifier found check in a non-fallible function", value->line, value->column, fun && fun->name ? fun->name : "<unnamed>");
      return false;
    }
    return true;
  }
  if (value->kind == IR_VALUE_RESCUE) {
    if (!value->left || !value->right || value->left->type != IR_TYPE_I64) {
      char actual[160];
      snprintf(actual, sizeof(actual), "rescue input is %s and fallback is %s", value->left ? mir_type_kind_name(value->left->type) : "missing", value->right ? mir_type_kind_name(value->right->type) : "missing");
      mir_verify_mark_unsupported(ir, "MIR verifier found invalid rescue input", value->line, value->column, actual);
      return false;
    }
    if (value->type != value->left->element_type || value->right->type != value->type) {
      char actual[192];
      snprintf(actual, sizeof(actual), "rescue result %s, fallible value %s, fallback %s", mir_type_kind_name(value->type), mir_type_kind_name(value->left->element_type), mir_type_kind_name(value->right->type));
      mir_verify_mark_unsupported(ir, "MIR verifier found rescue type mismatch", value->line, value->column, actual);
      return false;
    }
  }
  return true;
}

static bool mir_verify_direct_primitive_value(IrProgram *ir, const IrValue *value, const char *message, const char *role) {
  if (!ir || !ir->mir_valid) return false;
  if (value && (value->type == IR_TYPE_BOOL || mir_type_is_integer_value(value->type))) return true;
  char actual[160];
  snprintf(actual, sizeof(actual), "%s is %s", role ? role : "value", value ? mir_type_kind_name(value->type) : "missing");
  mir_verify_mark_unsupported(ir, message, value ? value->line : 1, value ? value->column : 1, actual);
  return false;
}

static bool mir_verify_same_type_operands(IrProgram *ir, const IrValue *value, const char *message, const char *role) {
  if (!ir || !ir->mir_valid || !value) return false;
  if (value->left && value->right && value->left->type == value->type && value->right->type == value->type) return true;
  char actual[192];
  snprintf(actual, sizeof(actual), "%s result %s left %s right %s",
           role ? role : "value",
           mir_type_kind_name(value->type),
           value->left ? mir_type_kind_name(value->left->type) : "missing",
           value->right ? mir_type_kind_name(value->right->type) : "missing");
  mir_verify_mark_unsupported(ir, message, value->line, value->column, actual);
  return false;
}

static bool mir_verify_cast_value_contract(IrProgram *ir, const IrValue *value) {
  if (!mir_verify_direct_primitive_value(ir, value, "MIR verifier found cast result type mismatch", "cast result")) return false;
  return mir_verify_direct_primitive_value(ir, value ? value->left : NULL, "MIR verifier found cast input type mismatch", "cast input");
}

static bool mir_verify_binary_value_contract(IrProgram *ir, const IrValue *value) {
  if (!mir_verify_direct_primitive_value(ir, value, "MIR verifier found binary result type mismatch", "binary result")) return false;
  if ((value->binary_op == IR_BIN_ADD || value->binary_op == IR_BIN_SUB ||
       value->binary_op == IR_BIN_MUL || value->binary_op == IR_BIN_DIV ||
       value->binary_op == IR_BIN_MOD) &&
      value->type == IR_TYPE_BOOL) {
    mir_verify_mark_unsupported(ir, "MIR verifier found invalid boolean arithmetic", value->line, value->column, "arithmetic result is Bool");
    return false;
  }
  return mir_verify_same_type_operands(ir, value, "MIR verifier found binary operand type mismatch", "binary");
}

static bool mir_verify_compare_value_contract(IrProgram *ir, const IrValue *value) {
  if (!mir_verify_value_type(ir, value, IR_TYPE_BOOL, "MIR verifier found compare result type mismatch", "compare result")) return false;
  if (!value->left || !value->right || value->left->type != value->right->type ||
      !(value->left->type == IR_TYPE_BOOL || mir_type_is_integer_value(value->left->type))) {
    char actual[192];
    snprintf(actual, sizeof(actual), "compare left %s right %s",
             value->left ? mir_type_kind_name(value->left->type) : "missing",
             value->right ? mir_type_kind_name(value->right->type) : "missing");
    mir_verify_mark_unsupported(ir, "MIR verifier found compare operand type mismatch", value->line, value->column, actual);
    return false;
  }
  return true;
}

static bool mir_verify_maybe_scalar_result(IrProgram *ir, const IrValue *value, IrTypeKind element_type, const char *message, const char *role) {
  if (!ir || !ir->mir_valid || !value) return false;
  if (value->type == IR_TYPE_MAYBE_SCALAR && value->element_type == element_type) return true;
  char actual[192];
  snprintf(actual, sizeof(actual), "%s result %s value %s but expected Maybe<%s>",
           role ? role : "helper",
           mir_type_kind_name(value->type),
           mir_type_kind_name(value->element_type),
           mir_type_kind_name(element_type));
  mir_verify_mark_unsupported(ir, message, value->line, value->column, actual);
  return false;
}

static bool mir_verify_maybe_scalar_or_fallible_result(IrProgram *ir, const IrValue *value, IrTypeKind element_type, const char *message, const char *role) {
  if (!ir || !ir->mir_valid || !value) return false;
  if ((value->type == IR_TYPE_MAYBE_SCALAR || value->type == IR_TYPE_I64) && value->element_type == element_type) return true;
  char actual[192];
  snprintf(actual, sizeof(actual), "%s result %s value %s but expected Maybe<%s> or fallible %s",
           role ? role : "helper",
           mir_type_kind_name(value->type),
           mir_type_kind_name(value->element_type),
           mir_type_kind_name(element_type),
           mir_type_kind_name(element_type));
  mir_verify_mark_unsupported(ir, message, value->line, value->column, actual);
  return false;
}

static bool mir_verify_file_local(IrProgram *ir, const IrFunction *fun, const IrValue *value, const char *message, const char *role) {
  return mir_verify_local_value_kind(ir, fun, value ? value->local_index : 0, IR_TYPE_I32, value ? value->line : 1, value ? value->column : 1, message, role);
}

static bool mir_verify_byte_view_pair(IrProgram *ir, const IrValue *value, const char *message, const char *left_role, const char *right_role) {
  if (!mir_verify_value_type(ir, value ? value->left : NULL, IR_TYPE_BYTE_VIEW, message, left_role)) return false;
  return mir_verify_value_type(ir, value ? value->right : NULL, IR_TYPE_BYTE_VIEW, message, right_role);
}

static IrTypeKind mir_verify_view_element_type(const IrValue *value) {
  return value ? mir_view_element_or_u8(value->element_type) : IR_TYPE_U8;
}

static bool mir_verify_byte_view_pair_same_element(IrProgram *ir, const IrValue *value, const char *message) {
  if (!mir_verify_byte_view_pair(ir, value, message, "byte-view equality left", "byte-view equality right")) return false;
  IrTypeKind left = mir_verify_view_element_type(value ? value->left : NULL);
  IrTypeKind right = mir_verify_view_element_type(value ? value->right : NULL);
  if (left == right) return true;
  char actual[160];
  snprintf(actual, sizeof(actual), "left element %s but right element %s", mir_type_kind_name(left), mir_type_kind_name(right));
  mir_verify_mark_unsupported(ir, "MIR verifier found byte-view equality element mismatch", value ? value->line : 1, value ? value->column : 1, actual);
  return false;
}

static bool mir_verify_fs_value_contract(IrProgram *ir, const IrFunction *fun, const MirVerifierState *state, const IrValue *value) {
  if (!ir || !ir->mir_valid || !value) return false;
  switch (value->kind) {
    case IR_VALUE_FS_HOST:
      return mir_verify_helper_result_type(ir, value, IR_TYPE_I32, "filesystem host result");
    case IR_VALUE_FS_OPEN:
    case IR_VALUE_FS_CREATE:
      if (!mir_verify_maybe_scalar_or_fallible_result(ir, value, IR_TYPE_I32, "MIR verifier found filesystem open result type mismatch", "filesystem open")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid filesystem path", "filesystem path");
    case IR_VALUE_FS_READ_PATH:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "filesystem read result")) return false;
      if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid filesystem read path", "filesystem read path")) return false;
      return mir_verify_mutable_byte_storage(ir, fun, state, value->right, "MIR verifier found invalid filesystem read buffer", "filesystem read buffer");
    case IR_VALUE_FS_READ_BYTES_PATH:
      if (!mir_verify_maybe_scalar_result(ir, value, IR_TYPE_USIZE, "MIR verifier found filesystem read result type mismatch", "filesystem read bytes")) return false;
      if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid filesystem read path", "filesystem read path")) return false;
      return mir_verify_mutable_byte_storage(ir, fun, state, value->right, "MIR verifier found invalid filesystem read buffer", "filesystem read buffer");
    case IR_VALUE_FS_READ_BYTES_AT_PATH:
      if (!mir_verify_maybe_scalar_result(ir, value, IR_TYPE_USIZE, "MIR verifier found filesystem read result type mismatch", "filesystem read bytes at")) return false;
      if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid filesystem read path", "filesystem read path")) return false;
      if (!mir_verify_value_type(ir, value->index, IR_TYPE_USIZE, "MIR verifier found invalid filesystem read offset", "filesystem read offset")) return false;
      return mir_verify_mutable_byte_storage(ir, fun, state, value->right, "MIR verifier found invalid filesystem read buffer", "filesystem read buffer");
    case IR_VALUE_FS_WRITE_PATH:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "filesystem write result")) return false;
      return mir_verify_byte_view_pair(ir, value, "MIR verifier found invalid filesystem write input", "filesystem write path", "filesystem write bytes");
    case IR_VALUE_FS_WRITE_BYTES_PATH:
      if (!mir_verify_maybe_scalar_result(ir, value, IR_TYPE_USIZE, "MIR verifier found filesystem write result type mismatch", "filesystem write bytes")) return false;
      return mir_verify_byte_view_pair(ir, value, "MIR verifier found invalid filesystem write input", "filesystem write path", "filesystem write bytes");
    case IR_VALUE_FS_READ_ALL:
      if (value->type == IR_TYPE_MAYBE_BYTE_VIEW) {
        if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_BYTE_VIEW, "filesystem readAll result")) return false;
      } else if (value->type != IR_TYPE_I64 || value->element_type != IR_TYPE_BYTE_VIEW) {
        char actual[160];
        snprintf(actual, sizeof(actual), "readAll result %s value %s", mir_type_kind_name(value->type), mir_type_kind_name(value->element_type));
        mir_verify_mark_unsupported(ir, "MIR verifier found filesystem readAll result type mismatch", value->line, value->column, actual);
        return false;
      }
      if (!mir_verify_mutable_local_value_kind(ir, fun, value->local_index, IR_TYPE_ALLOC, value->line, value->column, "MIR verifier found invalid filesystem readAll allocator", "allocator")) return false;
      if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid filesystem readAll path", "filesystem readAll path")) return false;
      return mir_verify_value_is_integer(ir, value->right, "MIR verifier found invalid filesystem readAll limit", "filesystem readAll limit");
    case IR_VALUE_FS_READ_FILE:
      if (!mir_verify_maybe_scalar_or_fallible_result(ir, value, IR_TYPE_USIZE, "MIR verifier found filesystem file read result type mismatch", "filesystem file read")) return false;
      if (!mir_verify_file_local(ir, fun, value, "MIR verifier found invalid filesystem file read target", "File")) return false;
      return mir_verify_mutable_byte_storage(ir, fun, state, value->left, "MIR verifier found invalid filesystem file read buffer", "filesystem file read buffer");
    case IR_VALUE_FS_WRITE_ALL_FILE:
      if (value->type == IR_TYPE_BOOL) {
        if (value->element_type != IR_TYPE_VOID) {
          mir_verify_mark_unsupported(ir, "MIR verifier found filesystem file write result type mismatch", value->line, value->column, "writeAll Bool result carries non-Void value");
          return false;
        }
      } else if (value->type != IR_TYPE_I64 || value->element_type != IR_TYPE_VOID) {
        char actual[160];
        snprintf(actual, sizeof(actual), "writeAll result %s value %s", mir_type_kind_name(value->type), mir_type_kind_name(value->element_type));
        mir_verify_mark_unsupported(ir, "MIR verifier found filesystem file write result type mismatch", value->line, value->column, actual);
        return false;
      }
      if (!mir_verify_file_local(ir, fun, value, "MIR verifier found invalid filesystem file write target", "File")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid filesystem file write bytes", "filesystem file write bytes");
    case IR_VALUE_FS_CLOSE_FILE:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_VOID, "filesystem close result")) return false;
      return mir_verify_file_local(ir, fun, value, "MIR verifier found invalid filesystem close target", "File");
    case IR_VALUE_FS_EXISTS:
    case IR_VALUE_FS_REMOVE:
    case IR_VALUE_FS_MAKE_DIR:
    case IR_VALUE_FS_REMOVE_DIR:
    case IR_VALUE_FS_IS_DIR:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "filesystem boolean result")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid filesystem path", "filesystem path");
    case IR_VALUE_FS_RENAME:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "filesystem rename result")) return false;
      return mir_verify_byte_view_pair(ir, value, "MIR verifier found invalid filesystem rename input", "filesystem rename source", "filesystem rename destination");
    case IR_VALUE_FS_FILE_LEN:
      if (!mir_verify_maybe_scalar_or_fallible_result(ir, value, IR_TYPE_USIZE, "MIR verifier found filesystem file length result type mismatch", "filesystem file length")) return false;
      return mir_verify_file_local(ir, fun, value, "MIR verifier found invalid filesystem file length target", "File");
    case IR_VALUE_FS_DIR_ENTRY_COUNT:
      if (!mir_verify_maybe_scalar_result(ir, value, IR_TYPE_USIZE, "MIR verifier found filesystem directory count result type mismatch", "filesystem directory count")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid filesystem directory path", "filesystem directory path");
    case IR_VALUE_FS_TEMP_NAME:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_BYTE_VIEW, "filesystem temp name result")) return false;
      if (!mir_verify_mutable_byte_storage(ir, fun, state, value->left, "MIR verifier found invalid filesystem temp buffer", "filesystem temp buffer")) return false;
      return mir_verify_value_type(ir, value->right, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid filesystem temp prefix", "filesystem temp prefix");
    case IR_VALUE_FS_ATOMIC_WRITE:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "filesystem atomic write result")) return false;
      if (!mir_verify_byte_view_pair(ir, value, "MIR verifier found invalid filesystem atomic write input", "filesystem atomic write path", "filesystem atomic write temp path")) return false;
      return mir_verify_value_type(ir, value->index, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid filesystem atomic write bytes", "filesystem atomic write bytes");
    default:
      return true;
  }
}

static bool mir_verify_platform_value_contract(IrProgram *ir, const IrFunction *fun, const IrValue *value) {
  if (!ir || !ir->mir_valid || !value) return false;
  switch (value->kind) {
    case IR_VALUE_ARGS_LEN:
      return mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "args length result");
    case IR_VALUE_ARGS_GET:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_BYTE_VIEW, "args get result")) return false;
      return mir_verify_value_is_integer(ir, value->left, "MIR verifier found invalid args index", "args index");
    case IR_VALUE_ARGS_EQ:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "args equality result")) return false;
      if (!mir_verify_value_is_integer(ir, value->left, "MIR verifier found invalid args equality index", "args equality index")) return false;
      return mir_verify_value_type(ir, value->right, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid args equality text", "args equality text");
    case IR_VALUE_ARGS_GET_OR:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BYTE_VIEW, "args getOr result")) return false;
      if (!mir_verify_value_is_integer(ir, value->left, "MIR verifier found invalid args getOr index", "args getOr index")) return false;
      return mir_verify_value_type(ir, value->right, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid args getOr fallback", "args getOr fallback");
    case IR_VALUE_ENV_GET:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_BYTE_VIEW, "environment get result")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid environment key", "environment key");
    case IR_VALUE_TIME_WALL_SECONDS:
    case IR_VALUE_TIME_MONOTONIC:
      return mir_verify_helper_result_type(ir, value, IR_TYPE_I64, value->kind == IR_VALUE_TIME_WALL_SECONDS ? "wall time result" : "monotonic time result");
    case IR_VALUE_TIME_AS_MS:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_I32, "duration milliseconds result")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_I64, "MIR verifier found invalid duration conversion input", "duration value");
    case IR_VALUE_RAND_NEXT_U32:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_U32, "random next result")) return false;
      if (!mir_verify_local_value_kind(ir, fun, value->local_index, IR_TYPE_U32, value->line, value->column, "MIR verifier found invalid random source", "random source")) return false;
      if (!fun->locals[value->local_index].is_mutable) {
        mir_verify_mark_unsupported(ir, "MIR verifier found immutable random source", value->line, value->column, fun->locals[value->local_index].name ? fun->locals[value->local_index].name : "<unnamed>");
        return false;
      }
      return true;
    case IR_VALUE_RAND_ENTROPY_U32:
      return mir_verify_helper_result_type(ir, value, IR_TYPE_U32, "entropy result");
    default:
      return true;
  }
}

static bool mir_verify_local_value_contract(IrProgram *ir, const IrFunction *fun, const IrValue *value) {
  if (!mir_verify_local_index(ir, fun, value->local_index, value->line, value->column, "MIR verifier found local value outside the local table")) return false;
  const IrLocal *local = &fun->locals[value->local_index];
  if (value->type == local->type) return true;
  char actual[192];
  snprintf(actual, sizeof(actual), "local value %u has %s but local %s has %s", value->local_index, mir_type_kind_name(value->type), local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type));
  mir_verify_mark_unsupported(ir, "MIR verifier found local value type mismatch", value->line, value->column, actual);
  return false;
}

static bool mir_verify_field_load_value_contract(IrProgram *ir, const IrFunction *fun, const IrValue *value) {
  if (!mir_verify_local_index(ir, fun, value->local_index, value->line, value->column, "MIR verifier found field load outside the local table")) return false;
  const IrLocal *local = &fun->locals[value->local_index];
  if (!local->is_record && !local->is_record_ref) {
    char actual[160];
    snprintf(actual, sizeof(actual), "local %s is %s", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type));
    mir_verify_mark_unsupported(ir, "MIR verifier found field load from a non-record local", value->line, value->column, actual);
    return false;
  }
  if (value->type == IR_TYPE_BYTE_VIEW && !(value->element_type == IR_TYPE_BOOL || mir_type_is_value(value->element_type))) {
    mir_verify_mark_unsupported(ir, "MIR verifier found byte-view field load without element type", value->line, value->column, "byte-view field load");
    return false;
  }
  return mir_verify_record_field_span(ir, local, value->field_offset, value->type, value->line, value->column, "MIR verifier found field load outside the local storage");
}

static bool mir_verify_record_addr_value_contract(IrProgram *ir, const IrFunction *fun, const IrValue *value) {
  if (!mir_verify_local_index(ir, fun, value->local_index, value->line, value->column, "MIR verifier found record address outside the local table")) return false;
  const IrLocal *local = &fun->locals[value->local_index];
  if (!local->is_record) {
    char actual[160];
    snprintf(actual, sizeof(actual), "local %s is %s", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type));
    mir_verify_mark_unsupported(ir, "MIR verifier found record address of a non-record local", value->line, value->column, actual);
    return false;
  }
  return mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "record address result");
}

static bool mir_verify_maybe_byte_view_literal_contract(IrProgram *ir, const IrValue *value) {
  if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_BYTE_VIEW, "Maybe byte-view literal")) return false;
  if (value->data_len > 1) {
    char actual[160];
    snprintf(actual, sizeof(actual), "Maybe byte-view literal has flag %u", value->data_len);
    mir_verify_mark_unsupported(ir, "MIR verifier found invalid Maybe byte-view literal", value->line, value->column, actual);
    return false;
  }
  if (!value->data_len) return true;
  return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid Maybe byte-view payload", "Maybe byte-view payload");
}

static bool mir_verify_byte_view_index_load_contract(IrProgram *ir, const IrValue *value) {
  if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid byte-view index load input", "byte-view index load input")) return false;
  IrTypeKind element_type = value->left->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : value->left->element_type;
  if (value->type != element_type) {
    char actual[160];
    snprintf(actual, sizeof(actual), "index load has %s but view element is %s", mir_type_kind_name(value->type), mir_type_kind_name(value->left->element_type));
    mir_verify_mark_unsupported(ir, "MIR verifier found byte-view index load result type mismatch", value->line, value->column, actual);
    return false;
  }
  return mir_verify_value_is_integer(ir, value->index, "MIR verifier found invalid byte-view index load index", "byte-view index");
}

static bool mir_verify_str_contains_contract(IrProgram *ir, const IrValue *value, MirHelperRequirements *requirements) {
  mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, "std.str.contains");
  mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, "std.str.contains");
  if (!mir_verify_value_type(ir, value, IR_TYPE_BOOL, "MIR verifier found string contains result type mismatch", "string contains result")) return false;
  if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid string contains text", "string contains text")) return false;
  return mir_verify_value_type(ir, value->right, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid string contains needle", "string contains needle");
}

static const char *mir_verify_str_op_name(IrStrOp op) {
  switch (op) {
    case IR_STR_OP_REVERSE: return "std.str.reverse";
    case IR_STR_OP_COPY: return "std.str.copy";
    case IR_STR_OP_CONCAT: return "std.str.concat";
    case IR_STR_OP_REPEAT: return "std.str.repeat";
    case IR_STR_OP_TO_LOWER_ASCII: return "std.str.toLowerAscii";
    case IR_STR_OP_TO_UPPER_ASCII: return "std.str.toUpperAscii";
    case IR_STR_OP_TRIM_ASCII: return "std.str.trimAscii";
    case IR_STR_OP_TRIM_START_ASCII: return "std.str.trimStartAscii";
    case IR_STR_OP_TRIM_END_ASCII: return "std.str.trimEndAscii";
    case IR_STR_OP_COUNT_BYTE: return "std.str.countByte";
    case IR_STR_OP_STARTS_WITH: return "std.str.startsWith";
    case IR_STR_OP_ENDS_WITH: return "std.str.endsWith";
    case IR_STR_OP_CONTAINS: return "std.str.contains";
    case IR_STR_OP_COUNT: return "std.str.count";
    case IR_STR_OP_INDEX_OF: return "std.str.indexOf";
    case IR_STR_OP_LAST_INDEX_OF: return "std.str.lastIndexOf";
    case IR_STR_OP_EQL_IGNORE_ASCII_CASE: return "std.str.eqlIgnoreAsciiCase";
    case IR_STR_OP_WORD_COUNT_ASCII: return "std.str.wordCountAscii";
    case IR_STR_OP_PATH_BASENAME: return "std.path.basename";
    case IR_STR_OP_PATH_DIRNAME: return "std.path.dirname";
    case IR_STR_OP_PATH_EXTENSION: return "std.path.extension";
    case IR_STR_OP_PARSE_TOKEN_ASCII: return "std.parse.tokenAscii";
  }
  return "std.str";
}

static bool mir_verify_str_runtime_arg(IrProgram *ir, const IrValue *value, size_t index, IrTypeKind expected, const char *role) {
  IrValue *arg = value && index < value->arg_len ? value->args[index] : NULL;
  if (expected == IR_TYPE_BYTE_VIEW) {
    return mir_verify_value_type(ir, arg, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid std.str runtime byte-view argument", role);
  }
  if (expected == IR_TYPE_U8) {
    return mir_verify_value_type(ir, arg, IR_TYPE_U8, "MIR verifier found invalid std.str runtime byte argument", role);
  }
  return mir_verify_value_is_integer(ir, arg, "MIR verifier found invalid std.str runtime integer argument", role);
}

static bool mir_verify_str_runtime_contract(IrProgram *ir, const IrFunction *fun, const MirVerifierState *state, const IrValue *value, MirHelperRequirements *requirements) {
  mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, mir_verify_str_op_name((IrStrOp)value->int_value));
  mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, mir_verify_str_op_name((IrStrOp)value->int_value));
  switch ((IrStrOp)value->int_value) {
    case IR_STR_OP_REVERSE:
    case IR_STR_OP_COPY:
    case IR_STR_OP_TO_LOWER_ASCII:
    case IR_STR_OP_TO_UPPER_ASCII:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_BYTE_VIEW, "std.str buffer result")) return false;
      if (!mir_verify_mutable_byte_storage(ir, fun, state, value->arg_len > 0 ? value->args[0] : NULL, "MIR verifier found invalid std.str output buffer", "std.str output buffer")) return false;
      return mir_verify_str_runtime_arg(ir, value, 1, IR_TYPE_BYTE_VIEW, "std.str text");
    case IR_STR_OP_CONCAT:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_BYTE_VIEW, "std.str concat result")) return false;
      if (!mir_verify_mutable_byte_storage(ir, fun, state, value->arg_len > 0 ? value->args[0] : NULL, "MIR verifier found invalid std.str concat buffer", "std.str concat buffer")) return false;
      if (!mir_verify_str_runtime_arg(ir, value, 1, IR_TYPE_BYTE_VIEW, "std.str concat left")) return false;
      return mir_verify_str_runtime_arg(ir, value, 2, IR_TYPE_BYTE_VIEW, "std.str concat right");
    case IR_STR_OP_REPEAT:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_BYTE_VIEW, "std.str repeat result")) return false;
      if (!mir_verify_mutable_byte_storage(ir, fun, state, value->arg_len > 0 ? value->args[0] : NULL, "MIR verifier found invalid std.str repeat buffer", "std.str repeat buffer")) return false;
      if (!mir_verify_str_runtime_arg(ir, value, 1, IR_TYPE_BYTE_VIEW, "std.str repeat text")) return false;
      return mir_verify_str_runtime_arg(ir, value, 2, IR_TYPE_USIZE, "std.str repeat count");
    case IR_STR_OP_TRIM_ASCII:
    case IR_STR_OP_TRIM_START_ASCII:
    case IR_STR_OP_TRIM_END_ASCII:
    case IR_STR_OP_PATH_BASENAME:
    case IR_STR_OP_PATH_DIRNAME:
    case IR_STR_OP_PATH_EXTENSION:
    case IR_STR_OP_PARSE_TOKEN_ASCII:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BYTE_VIEW, "std.str trim result")) return false;
      return mir_verify_str_runtime_arg(ir, value, 0, IR_TYPE_BYTE_VIEW, "std.str trim text");
    case IR_STR_OP_COUNT_BYTE:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "std.str countByte result")) return false;
      if (!mir_verify_str_runtime_arg(ir, value, 0, IR_TYPE_BYTE_VIEW, "std.str countByte text")) return false;
      return mir_verify_str_runtime_arg(ir, value, 1, IR_TYPE_U8, "std.str countByte byte");
    case IR_STR_OP_STARTS_WITH:
    case IR_STR_OP_ENDS_WITH:
    case IR_STR_OP_CONTAINS:
    case IR_STR_OP_EQL_IGNORE_ASCII_CASE:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "std.str boolean result")) return false;
      if (!mir_verify_str_runtime_arg(ir, value, 0, IR_TYPE_BYTE_VIEW, "std.str text")) return false;
      return mir_verify_str_runtime_arg(ir, value, 1, IR_TYPE_BYTE_VIEW, "std.str pattern");
    case IR_STR_OP_COUNT:
    case IR_STR_OP_INDEX_OF:
    case IR_STR_OP_LAST_INDEX_OF:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "std.str search result")) return false;
      if (!mir_verify_str_runtime_arg(ir, value, 0, IR_TYPE_BYTE_VIEW, "std.str text")) return false;
      return mir_verify_str_runtime_arg(ir, value, 1, IR_TYPE_BYTE_VIEW, "std.str needle");
    case IR_STR_OP_WORD_COUNT_ASCII:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "std.str word count result")) return false;
      return mir_verify_str_runtime_arg(ir, value, 0, IR_TYPE_BYTE_VIEW, "std.str word text");
  }
  mir_verify_mark_unsupported(ir, "MIR verifier found unknown std.str runtime operation", value->line, value->column, "unknown std.str operation");
  return false;
}

static const char *mir_verify_ascii_op_name(IrAsciiOp op) {
  switch (op) {
    case IR_ASCII_OP_IS_DIGIT: return "std.ascii.isDigit";
    case IR_ASCII_OP_IS_LOWER: return "std.ascii.isLower";
    case IR_ASCII_OP_IS_UPPER: return "std.ascii.isUpper";
    case IR_ASCII_OP_IS_ALPHA: return "std.ascii.isAlpha";
    case IR_ASCII_OP_IS_ALNUM: return "std.ascii.isAlnum";
    case IR_ASCII_OP_IS_WHITESPACE: return "std.ascii.isWhitespace";
    case IR_ASCII_OP_IS_HEX_DIGIT: return "std.ascii.isHexDigit";
    case IR_ASCII_OP_TO_LOWER: return "std.ascii.toLower";
    case IR_ASCII_OP_TO_UPPER: return "std.ascii.toUpper";
    case IR_ASCII_OP_DIGIT_VALUE: return "std.ascii.digitValue";
    case IR_ASCII_OP_HEX_VALUE: return "std.ascii.hexValue";
  }
  return "std.ascii";
}

static bool mir_verify_ascii_runtime_contract(IrProgram *ir, const IrValue *value, MirHelperRequirements *requirements) {
  mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, mir_verify_ascii_op_name((IrAsciiOp)value->int_value));
  mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, mir_verify_ascii_op_name((IrAsciiOp)value->int_value));
  if (value->arg_len != 1) {
    mir_verify_mark_unsupported(ir, "MIR verifier found invalid std.ascii runtime arity", value->line, value->column, "std.ascii helper must have one argument");
    return false;
  }
  if (!mir_verify_value_type(ir, value->args[0], IR_TYPE_U8, "MIR verifier found invalid std.ascii byte argument", "std.ascii byte")) return false;
  switch ((IrAsciiOp)value->int_value) {
    case IR_ASCII_OP_IS_DIGIT:
    case IR_ASCII_OP_IS_LOWER:
    case IR_ASCII_OP_IS_UPPER:
    case IR_ASCII_OP_IS_ALPHA:
    case IR_ASCII_OP_IS_ALNUM:
    case IR_ASCII_OP_IS_WHITESPACE:
    case IR_ASCII_OP_IS_HEX_DIGIT:
      return mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "std.ascii predicate result");
    case IR_ASCII_OP_TO_LOWER:
    case IR_ASCII_OP_TO_UPPER:
      return mir_verify_helper_result_type(ir, value, IR_TYPE_U8, "std.ascii conversion result");
    case IR_ASCII_OP_DIGIT_VALUE:
    case IR_ASCII_OP_HEX_VALUE:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_SCALAR, "std.ascii Maybe result")) return false;
      if (value->element_type != IR_TYPE_U8) {
        mir_verify_mark_unsupported(ir, "MIR verifier found std.ascii Maybe element type mismatch", value->line, value->column, mir_type_kind_name(value->element_type));
        return false;
      }
      return true;
  }
  mir_verify_mark_unsupported(ir, "MIR verifier found unknown std.ascii runtime operation", value->line, value->column, "unknown std.ascii operation");
  return false;
}

static const char *mir_verify_text_op_name(IrTextOp op) {
  switch (op) {
    case IR_TEXT_OP_IS_ASCII: return "std.text.isAscii";
    case IR_TEXT_OP_UTF8_VALID: return "std.text.utf8Valid";
    case IR_TEXT_OP_UTF8_LEN: return "std.text.utf8Len";
  }
  return "std.text";
}

static bool mir_verify_text_runtime_contract(IrProgram *ir, const IrValue *value, MirHelperRequirements *requirements) {
  mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, mir_verify_text_op_name((IrTextOp)value->int_value));
  mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, mir_verify_text_op_name((IrTextOp)value->int_value));
  if (value->arg_len != 1) {
    mir_verify_mark_unsupported(ir, "MIR verifier found invalid std.text runtime arity", value->line, value->column, "std.text helper must have one argument");
    return false;
  }
  if (!mir_verify_value_type(ir, value->args[0], IR_TYPE_BYTE_VIEW, "MIR verifier found invalid std.text byte-view argument", "std.text bytes")) return false;
  switch ((IrTextOp)value->int_value) {
    case IR_TEXT_OP_IS_ASCII:
    case IR_TEXT_OP_UTF8_VALID:
      return mir_verify_helper_result_type(ir, value, IR_TYPE_BOOL, "std.text predicate result");
    case IR_TEXT_OP_UTF8_LEN:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_SCALAR, "std.text Maybe length result")) return false;
      if (value->element_type != IR_TYPE_USIZE) {
        mir_verify_mark_unsupported(ir, "MIR verifier found std.text Maybe element type mismatch", value->line, value->column, mir_type_kind_name(value->element_type));
        return false;
      }
      return true;
  }
  mir_verify_mark_unsupported(ir, "MIR verifier found unknown std.text runtime operation", value->line, value->column, "unknown std.text operation");
  return false;
}

static const char *mir_verify_time_op_name(IrTimeOp op) {
  switch (op) {
    case IR_TIME_OP_AS_US_FLOOR: return "std.time.asUsFloor";
    case IR_TIME_OP_AS_MS_FLOOR: return "std.time.asMsFloor";
    case IR_TIME_OP_AS_SECONDS_FLOOR: return "std.time.asSecondsFloor";
    case IR_TIME_OP_MIN: return "std.time.min";
    case IR_TIME_OP_MAX: return "std.time.max";
    case IR_TIME_OP_CLAMP: return "std.time.clamp";
  }
  return "std.time";
}

static bool mir_verify_time_runtime_contract(IrProgram *ir, const IrValue *value, MirHelperRequirements *requirements) {
  mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, mir_verify_time_op_name((IrTimeOp)value->int_value));
  mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, mir_verify_time_op_name((IrTimeOp)value->int_value));
  size_t expected_args = 1;
  IrTypeKind expected_result = IR_TYPE_I64;
  switch ((IrTimeOp)value->int_value) {
    case IR_TIME_OP_AS_US_FLOOR:
    case IR_TIME_OP_AS_SECONDS_FLOOR:
      expected_args = 1;
      expected_result = IR_TYPE_I64;
      break;
    case IR_TIME_OP_AS_MS_FLOOR:
      expected_args = 1;
      expected_result = IR_TYPE_I32;
      break;
    case IR_TIME_OP_MIN:
    case IR_TIME_OP_MAX:
      expected_args = 2;
      expected_result = IR_TYPE_I64;
      break;
    case IR_TIME_OP_CLAMP:
      expected_args = 3;
      expected_result = IR_TYPE_I64;
      break;
    default:
      mir_verify_mark_unsupported(ir, "MIR verifier found unknown std.time runtime operation", value->line, value->column, "unknown std.time operation");
      return false;
  }
  if (value->arg_len != expected_args) {
    mir_verify_mark_unsupported(ir, "MIR verifier found invalid std.time runtime arity", value->line, value->column, "wrong std.time arity");
    return false;
  }
  if (!mir_verify_helper_result_type(ir, value, expected_result, "std.time runtime result")) return false;
  for (size_t i = 0; i < value->arg_len; i++) {
    if (!mir_verify_value_type(ir, value->args[i], IR_TYPE_I64, "MIR verifier found invalid std.time Duration argument", "std.time Duration")) return false;
  }
  return true;
}

static const char *mir_verify_math_op_name(IrMathOp op) {
  switch (op) {
    case IR_MATH_OP_MIN_I32: return "std.math.minI32";
    case IR_MATH_OP_MAX_I32: return "std.math.maxI32";
    case IR_MATH_OP_CLAMP_I32: return "std.math.clampI32";
    case IR_MATH_OP_MIN_I64: return "std.math.minI64";
    case IR_MATH_OP_MAX_I64: return "std.math.maxI64";
    case IR_MATH_OP_CLAMP_I64: return "std.math.clampI64";
    case IR_MATH_OP_MIN_U32: return "std.math.minU32";
    case IR_MATH_OP_MAX_U32: return "std.math.maxU32";
    case IR_MATH_OP_CLAMP_U32: return "std.math.clampU32";
    case IR_MATH_OP_MIN_U64: return "std.math.minU64";
    case IR_MATH_OP_MAX_U64: return "std.math.maxU64";
    case IR_MATH_OP_CLAMP_U64: return "std.math.clampU64";
    case IR_MATH_OP_MIN_USIZE: return "std.math.minUsize";
    case IR_MATH_OP_MAX_USIZE: return "std.math.maxUsize";
    case IR_MATH_OP_CLAMP_USIZE: return "std.math.clampUsize";
    case IR_MATH_OP_ABS_I32: return "std.math.absI32";
    case IR_MATH_OP_ABS_I64: return "std.math.absI64";
    case IR_MATH_OP_CHECKED_ADD_U32: return "std.math.checkedAddU32";
    case IR_MATH_OP_CHECKED_SUB_U32: return "std.math.checkedSubU32";
    case IR_MATH_OP_CHECKED_MUL_U32: return "std.math.checkedMulU32";
    case IR_MATH_OP_SATURATING_ADD_U32: return "std.math.saturatingAddU32";
    case IR_MATH_OP_SATURATING_SUB_U32: return "std.math.saturatingSubU32";
    case IR_MATH_OP_SATURATING_MUL_U32: return "std.math.saturatingMulU32";
    case IR_MATH_OP_CHECKED_ADD_I32: return "std.math.checkedAddI32";
    case IR_MATH_OP_CHECKED_SUB_I32: return "std.math.checkedSubI32";
    case IR_MATH_OP_CHECKED_MUL_I32: return "std.math.checkedMulI32";
    case IR_MATH_OP_SATURATING_ADD_I32: return "std.math.saturatingAddI32";
    case IR_MATH_OP_SATURATING_SUB_I32: return "std.math.saturatingSubI32";
    case IR_MATH_OP_SATURATING_MUL_I32: return "std.math.saturatingMulI32";
    case IR_MATH_OP_GCD_U32: return "std.math.gcdU32";
    case IR_MATH_OP_LCM_U32: return "std.math.lcmU32";
    case IR_MATH_OP_CHECKED_LCM_U32: return "std.math.checkedLcmU32";
    case IR_MATH_OP_POW_U32: return "std.math.powU32";
    case IR_MATH_OP_CHECKED_POW_U32: return "std.math.checkedPowU32";
    case IR_MATH_OP_MOD_POW_U32: return "std.math.modPowU32";
    case IR_MATH_OP_IS_PRIME_U32: return "std.math.isPrimeU32";
    case IR_MATH_OP_SQRT_FLOOR_U32: return "std.math.sqrtFloorU32";
    case IR_MATH_OP_FACTORIAL_U32: return "std.math.factorialU32";
    case IR_MATH_OP_BINOMIAL_U32: return "std.math.binomialU32";
    case IR_MATH_OP_DIVISOR_COUNT_U32: return "std.math.divisorCountU32";
    case IR_MATH_OP_PROPER_DIVISOR_SUM_U32: return "std.math.properDivisorSumU32";
    case IR_MATH_OP_CHECKED_ADD_USIZE: return "std.math.checkedAddUsize";
    case IR_MATH_OP_CHECKED_SUB_USIZE: return "std.math.checkedSubUsize";
    case IR_MATH_OP_CHECKED_MUL_USIZE: return "std.math.checkedMulUsize";
    case IR_MATH_OP_SATURATING_ADD_USIZE: return "std.math.saturatingAddUsize";
    case IR_MATH_OP_SATURATING_SUB_USIZE: return "std.math.saturatingSubUsize";
    case IR_MATH_OP_SATURATING_MUL_USIZE: return "std.math.saturatingMulUsize";
  }
  return "std.math";
}

static bool mir_verify_math_runtime_contract(IrProgram *ir, const IrValue *value, MirHelperRequirements *requirements) {
  mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, mir_verify_math_op_name((IrMathOp)value->int_value));
  mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, mir_verify_math_op_name((IrMathOp)value->int_value));
  size_t expected_args = 2;
  IrTypeKind expected_result = IR_TYPE_I32;
  IrTypeKind expected_element = IR_TYPE_UNSUPPORTED;
  switch ((IrMathOp)value->int_value) {
    case IR_MATH_OP_MIN_I32:
    case IR_MATH_OP_MAX_I32:
      expected_args = 2;
      expected_result = IR_TYPE_I32;
      break;
    case IR_MATH_OP_CLAMP_I32:
      expected_args = 3;
      expected_result = IR_TYPE_I32;
      break;
    case IR_MATH_OP_MIN_I64:
    case IR_MATH_OP_MAX_I64:
      expected_args = 2;
      expected_result = IR_TYPE_I64;
      break;
    case IR_MATH_OP_CLAMP_I64:
      expected_args = 3;
      expected_result = IR_TYPE_I64;
      break;
    case IR_MATH_OP_MIN_U32:
    case IR_MATH_OP_MAX_U32:
      expected_args = 2;
      expected_result = IR_TYPE_U32;
      break;
    case IR_MATH_OP_CLAMP_U32:
      expected_args = 3;
      expected_result = IR_TYPE_U32;
      break;
    case IR_MATH_OP_MIN_U64:
    case IR_MATH_OP_MAX_U64:
      expected_args = 2;
      expected_result = IR_TYPE_U64;
      break;
    case IR_MATH_OP_CLAMP_U64:
      expected_args = 3;
      expected_result = IR_TYPE_U64;
      break;
    case IR_MATH_OP_MIN_USIZE:
    case IR_MATH_OP_MAX_USIZE:
      expected_args = 2;
      expected_result = IR_TYPE_USIZE;
      break;
    case IR_MATH_OP_CLAMP_USIZE:
      expected_args = 3;
      expected_result = IR_TYPE_USIZE;
      break;
    case IR_MATH_OP_ABS_I32:
      expected_args = 1;
      expected_result = IR_TYPE_U32;
      break;
    case IR_MATH_OP_ABS_I64:
      expected_args = 1;
      expected_result = IR_TYPE_U64;
      break;
    case IR_MATH_OP_CHECKED_ADD_U32:
    case IR_MATH_OP_CHECKED_SUB_U32:
    case IR_MATH_OP_CHECKED_MUL_U32:
    case IR_MATH_OP_CHECKED_LCM_U32:
    case IR_MATH_OP_CHECKED_POW_U32:
    case IR_MATH_OP_BINOMIAL_U32:
      expected_args = 2;
      expected_result = IR_TYPE_MAYBE_SCALAR;
      expected_element = IR_TYPE_U32;
      break;
    case IR_MATH_OP_FACTORIAL_U32:
      expected_args = 1;
      expected_result = IR_TYPE_MAYBE_SCALAR;
      expected_element = IR_TYPE_U32;
      break;
    case IR_MATH_OP_SATURATING_ADD_U32:
    case IR_MATH_OP_SATURATING_SUB_U32:
    case IR_MATH_OP_SATURATING_MUL_U32:
    case IR_MATH_OP_GCD_U32:
    case IR_MATH_OP_LCM_U32:
    case IR_MATH_OP_POW_U32:
      expected_args = 2;
      expected_result = IR_TYPE_U32;
      break;
    case IR_MATH_OP_MOD_POW_U32:
      expected_args = 3;
      expected_result = IR_TYPE_U32;
      break;
    case IR_MATH_OP_IS_PRIME_U32:
      expected_args = 1;
      expected_result = IR_TYPE_BOOL;
      break;
    case IR_MATH_OP_SQRT_FLOOR_U32:
    case IR_MATH_OP_DIVISOR_COUNT_U32:
    case IR_MATH_OP_PROPER_DIVISOR_SUM_U32:
      expected_args = 1;
      expected_result = IR_TYPE_U32;
      break;
    case IR_MATH_OP_CHECKED_ADD_I32:
    case IR_MATH_OP_CHECKED_SUB_I32:
    case IR_MATH_OP_CHECKED_MUL_I32:
      expected_args = 2;
      expected_result = IR_TYPE_MAYBE_SCALAR;
      expected_element = IR_TYPE_I32;
      break;
    case IR_MATH_OP_SATURATING_ADD_I32:
    case IR_MATH_OP_SATURATING_SUB_I32:
    case IR_MATH_OP_SATURATING_MUL_I32:
      expected_args = 2;
      expected_result = IR_TYPE_I32;
      break;
    case IR_MATH_OP_CHECKED_ADD_USIZE:
    case IR_MATH_OP_CHECKED_SUB_USIZE:
    case IR_MATH_OP_CHECKED_MUL_USIZE:
      expected_args = 2;
      expected_result = IR_TYPE_MAYBE_SCALAR;
      expected_element = IR_TYPE_USIZE;
      break;
    case IR_MATH_OP_SATURATING_ADD_USIZE:
    case IR_MATH_OP_SATURATING_SUB_USIZE:
    case IR_MATH_OP_SATURATING_MUL_USIZE:
      expected_args = 2;
      expected_result = IR_TYPE_USIZE;
      break;
    default:
      mir_verify_mark_unsupported(ir, "MIR verifier found unknown std.math runtime operation", value->line, value->column, "unknown std.math operation");
      return false;
  }
  if (value->arg_len != expected_args) {
    mir_verify_mark_unsupported(ir, "MIR verifier found invalid std.math runtime arity", value->line, value->column, "wrong std.math arity");
    return false;
  }
  if (!mir_verify_helper_result_type(ir, value, expected_result, "std.math runtime result")) return false;
  if (expected_result == IR_TYPE_MAYBE_SCALAR && value->element_type != expected_element) {
    mir_verify_mark_unsupported(ir, "MIR verifier found std.math Maybe element type mismatch", value->line, value->column, mir_type_kind_name(value->element_type));
    return false;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (!mir_verify_value_type(ir, value->args[i], IR_TYPE_I64, "MIR verifier found invalid std.math runtime argument", "std.math argument")) return false;
  }
  return true;
}

static const char *mir_verify_search_op_name(IrSearchOp op) {
  switch (op) {
    case IR_SEARCH_OP_LOWER_BOUND_I32: return "std.search.lowerBoundI32";
    case IR_SEARCH_OP_BINARY_I32: return "std.search.binaryI32";
    case IR_SEARCH_OP_LOWER_BOUND_U32: return "std.search.lowerBoundU32";
    case IR_SEARCH_OP_BINARY_U32: return "std.search.binaryU32";
    case IR_SEARCH_OP_LOWER_BOUND_USIZE: return "std.search.lowerBoundUsize";
    case IR_SEARCH_OP_BINARY_USIZE: return "std.search.binaryUsize";
  }
  return "std.search";
}

static IrTypeKind mir_verify_search_op_element(IrSearchOp op) {
  switch (op) {
    case IR_SEARCH_OP_LOWER_BOUND_I32:
    case IR_SEARCH_OP_BINARY_I32:
      return IR_TYPE_I32;
    case IR_SEARCH_OP_LOWER_BOUND_U32:
    case IR_SEARCH_OP_BINARY_U32:
      return IR_TYPE_U32;
    case IR_SEARCH_OP_LOWER_BOUND_USIZE:
    case IR_SEARCH_OP_BINARY_USIZE:
      return IR_TYPE_USIZE;
  }
  return IR_TYPE_UNSUPPORTED;
}

static bool mir_verify_search_runtime_contract(IrProgram *ir, const IrValue *value, MirHelperRequirements *requirements) {
  IrSearchOp op = (IrSearchOp)value->int_value;
  IrTypeKind expected_element = mir_verify_search_op_element(op);
  if (expected_element == IR_TYPE_UNSUPPORTED) {
    mir_verify_mark_unsupported(ir, "MIR verifier found unknown std.search runtime operation", value->line, value->column, "unknown std.search operation");
    return false;
  }
  mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, mir_verify_search_op_name(op));
  mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, mir_verify_search_op_name(op));
  if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "std.search runtime result")) return false;
  if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid std.search span", "std.search span")) return false;
  if (value->left && value->left->element_type != expected_element) {
    mir_verify_mark_unsupported(ir, "MIR verifier found std.search span element mismatch", value->line, value->column, mir_type_kind_name(value->left->element_type));
    return false;
  }
  return mir_verify_value_type(ir, value->right, IR_TYPE_I64, "MIR verifier found invalid std.search needle", "std.search needle");
}

static const char *mir_verify_sort_op_name(IrSortOp op) {
  switch (op) {
    case IR_SORT_OP_INSERTION_I32: return "std.sort.insertionI32";
    case IR_SORT_OP_IS_SORTED_I32: return "std.sort.isSortedI32";
    case IR_SORT_OP_INSERTION_U32: return "std.sort.insertionU32";
    case IR_SORT_OP_IS_SORTED_U32: return "std.sort.isSortedU32";
    case IR_SORT_OP_INSERTION_USIZE: return "std.sort.insertionUsize";
    case IR_SORT_OP_IS_SORTED_USIZE: return "std.sort.isSortedUsize";
  }
  return "std.sort";
}

static IrTypeKind mir_verify_sort_op_element(IrSortOp op) {
  switch (op) {
    case IR_SORT_OP_INSERTION_I32:
    case IR_SORT_OP_IS_SORTED_I32:
      return IR_TYPE_I32;
    case IR_SORT_OP_INSERTION_U32:
    case IR_SORT_OP_IS_SORTED_U32:
      return IR_TYPE_U32;
    case IR_SORT_OP_INSERTION_USIZE:
    case IR_SORT_OP_IS_SORTED_USIZE:
      return IR_TYPE_USIZE;
  }
  return IR_TYPE_UNSUPPORTED;
}

static bool mir_verify_sort_runtime_contract(IrProgram *ir, const IrFunction *fun, const MirVerifierState *state, const IrValue *value, MirHelperRequirements *requirements) {
  IrSortOp op = (IrSortOp)value->int_value;
  IrTypeKind expected_element = mir_verify_sort_op_element(op);
  if (expected_element == IR_TYPE_UNSUPPORTED) {
    mir_verify_mark_unsupported(ir, "MIR verifier found unknown std.sort runtime operation", value->line, value->column, "unknown std.sort operation");
    return false;
  }
  bool sorted_check =
    op == IR_SORT_OP_IS_SORTED_I32 ||
    op == IR_SORT_OP_IS_SORTED_U32 ||
    op == IR_SORT_OP_IS_SORTED_USIZE;
  mir_require_count(&requirements->runtime_helpers, 1, value->line, value->column, mir_verify_sort_op_name(op));
  mir_require_count(&requirements->host_runtime_imports, 1, value->line, value->column, mir_verify_sort_op_name(op));
  if (!mir_verify_helper_result_type(ir, value, sorted_check ? IR_TYPE_BOOL : IR_TYPE_VOID, "std.sort runtime result")) return false;
  if (sorted_check) {
    if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid std.sort span", "std.sort span")) return false;
  } else {
    if (!mir_verify_mutable_typed_span_storage(ir, fun, state, value->left, expected_element, "MIR verifier found invalid std.sort mutable span", "std.sort mutable span")) return false;
  }
  if (value->left && value->left->element_type != expected_element) {
    mir_verify_mark_unsupported(ir, "MIR verifier found std.sort span element mismatch", value->line, value->column, mir_type_kind_name(value->left->element_type));
    return false;
  }
  return true;
}

static bool mir_verify_direct_value_kind_contract(IrProgram *ir, const IrFunction *fun, const MirVerifierState *state, const IrValue *value, MirHelperRequirements *requirements) {
  if (!ir || !ir->mir_valid) return false;
  if (!value) return true;
  switch (value->kind) {
    case IR_VALUE_INT:
      return mir_verify_value_is_integer(ir, value, "MIR verifier found integer literal type mismatch", "integer literal");
    case IR_VALUE_BOOL:
      return mir_verify_value_type(ir, value, IR_TYPE_BOOL, "MIR verifier found boolean literal type mismatch", "boolean literal");
    case IR_VALUE_LOCAL:
      return mir_verify_local_value_contract(ir, fun, value);
    case IR_VALUE_CAST: return mir_verify_cast_value_contract(ir, value);
    case IR_VALUE_BINARY: return mir_verify_binary_value_contract(ir, value);
    case IR_VALUE_COMPARE: return mir_verify_compare_value_contract(ir, value);
    case IR_VALUE_CALL:
      return mir_verify_direct_call_contract(ir, value);
    case IR_VALUE_INDEX_LOAD:
      return mir_verify_array_load_contract(ir, fun, value);
    case IR_VALUE_STRING_LITERAL:
      return mir_verify_value_type(ir, value, IR_TYPE_BYTE_VIEW, "MIR verifier found string literal type mismatch", "string literal");
    case IR_VALUE_ARRAY_BYTE_VIEW:
      return mir_verify_array_byte_view_contract(ir, fun, value);
    case IR_VALUE_BYTE_SLICE:
      return mir_verify_byte_view_value_contract(ir, value);
    case IR_VALUE_BYTE_VIEW_LEN:
      if (!mir_verify_byte_view_len_result(ir, value)) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid byte-view length input", "byte-view length input");
    case IR_VALUE_BYTE_VIEW_REMAINING:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_USIZE, "byte-view remaining result")) return false;
      if (!mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid byte-view remaining input", "byte-view remaining input")) return false;
      return mir_verify_value_is_integer(ir, value->index, "MIR verifier found invalid byte-view remaining offset", "byte-view remaining offset");
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD:
      return mir_verify_byte_view_index_load_contract(ir, value);
    case IR_VALUE_BYTE_VIEW_EQ:
      if (!mir_verify_value_type(ir, value, IR_TYPE_BOOL, "MIR verifier found byte-view equality result type mismatch", "byte-view equality result")) return false;
      return mir_verify_byte_view_pair_same_element(ir, value, "MIR verifier found invalid byte-view equality input");
    case IR_VALUE_STR_CONTAINS:
      return mir_verify_str_contains_contract(ir, value, requirements);
    case IR_VALUE_STR_RUNTIME:
      return mir_verify_str_runtime_contract(ir, fun, state, value, requirements);
    case IR_VALUE_ASCII_RUNTIME:
      return mir_verify_ascii_runtime_contract(ir, value, requirements);
    case IR_VALUE_TEXT_RUNTIME:
      return mir_verify_text_runtime_contract(ir, value, requirements);
    case IR_VALUE_TIME_RUNTIME:
      return mir_verify_time_runtime_contract(ir, value, requirements);
    case IR_VALUE_MATH_RUNTIME:
      return mir_verify_math_runtime_contract(ir, value, requirements);
    case IR_VALUE_SEARCH_RUNTIME:
      return mir_verify_search_runtime_contract(ir, value, requirements);
    case IR_VALUE_SORT_RUNTIME:
      return mir_verify_sort_runtime_contract(ir, fun, state, value, requirements);
    case IR_VALUE_BYTE_COPY:
    case IR_VALUE_BYTE_FILL:
      return mir_verify_byte_mutation_value_contract(ir, fun, state, value);
    case IR_VALUE_ITEM_COPY:
    case IR_VALUE_ITEM_FILL:
    case IR_VALUE_ITEM_CONTAINS:
      return mir_verify_item_helper_value_contract(ir, fun, state, value);
    case IR_VALUE_CRC32_BYTES:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_U32, "CRC32 result")) return false;
      return mir_verify_value_type(ir, value->left, IR_TYPE_BYTE_VIEW, "MIR verifier found invalid CRC32 input", "CRC32 bytes");
    case IR_VALUE_FIXED_BUF_ALLOC:
    case IR_VALUE_VEC_INIT:
    case IR_VALUE_VEC_PUSH:
    case IR_VALUE_VEC_LEN:
    case IR_VALUE_VEC_CAPACITY:
    case IR_VALUE_VEC_BYTES:
    case IR_VALUE_VEC_GET:
    case IR_VALUE_VEC_SET:
    case IR_VALUE_VEC_REMOVE_SWAP:
    case IR_VALUE_VEC_INDEX:
    case IR_VALUE_VEC_CONTAINS:
    case IR_VALUE_VEC_INSERT_UNIQUE:
    case IR_VALUE_VEC_REMOVE_VALUE:
    case IR_VALUE_VEC_CLEAR:
    case IR_VALUE_VEC_POP:
    case IR_VALUE_VEC_TRUNCATE:
    case IR_VALUE_ALLOC_BYTES:
    case IR_VALUE_JSON_PARSE_BYTES:
    case IR_VALUE_JSON_VALIDATE_BYTES:
    case IR_VALUE_JSON_STREAM_TOKENS_BYTES:
    case IR_VALUE_HTTP_FETCH:
    case IR_VALUE_HTTP_RESULT_OK:
    case IR_VALUE_HTTP_RESULT_STATUS:
    case IR_VALUE_HTTP_RESULT_BODY_LEN:
    case IR_VALUE_HTTP_RESULT_ERROR:
    case IR_VALUE_HTTP_RESPONSE_LEN:
    case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN:
    case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET:
    case IR_VALUE_HTTP_HEADER_VALUE:
    case IR_VALUE_HTTP_HEADER_FOUND:
    case IR_VALUE_HTTP_HEADER_OFFSET:
    case IR_VALUE_HTTP_HEADER_LEN:
    case IR_VALUE_HTTP_WRITE_JSON_RESPONSE: case IR_VALUE_HTTP_REQUEST_METHOD_NAME: case IR_VALUE_HTTP_REQUEST_PATH: case IR_VALUE_HTTP_REQUEST_MATCHES: case IR_VALUE_HTTP_REQUEST_BODY_WITHIN:
    case IR_VALUE_HTTP_STATUS_CLASS:
    case IR_VALUE_PARSE_RUNTIME: case IR_VALUE_PARSE_I32: case IR_VALUE_PARSE_U32: case IR_VALUE_ARGS_PARSE_U32: case IR_VALUE_ARGS_FIND: case IR_VALUE_ARGS_CONTAINS:
    case IR_VALUE_ARGS_VALUE_AFTER: case IR_VALUE_ARGS_VALUE_AFTER_OR: case IR_VALUE_ARGS_VALUE_AFTER_PARSE_U32:
    case IR_VALUE_FMT_BOOL: case IR_VALUE_FMT_HEX_U32: case IR_VALUE_FMT_I32: case IR_VALUE_FMT_U32: case IR_VALUE_FMT_USIZE:
      return mir_verify_direct_helper_value_contract(ir, fun, state, value, requirements);
    case IR_VALUE_MAYBE_HAS:
    case IR_VALUE_MAYBE_VALUE:
      return mir_verify_maybe_value_contract(ir, fun, value);
    case IR_VALUE_MAYBE_BYTE_VIEW_LITERAL:
      return mir_verify_maybe_byte_view_literal_contract(ir, value);
    case IR_VALUE_MAYBE_SCALAR_LITERAL:
      if (!mir_verify_helper_result_type(ir, value, IR_TYPE_MAYBE_SCALAR, "Maybe scalar literal")) return false;
      if (!(value->element_type == IR_TYPE_BOOL || mir_type_is_integer_value(value->element_type)) || value->data_len > 1) {
        char actual[160];
        snprintf(actual, sizeof(actual), "Maybe scalar literal value %s has flag %u", mir_type_kind_name(value->element_type), value->data_len);
        mir_verify_mark_unsupported(ir, "MIR verifier found invalid Maybe scalar literal", value->line, value->column, actual);
        return false;
      }
      return true;
    case IR_VALUE_ARGS_LEN:
    case IR_VALUE_ARGS_GET:
    case IR_VALUE_ARGS_EQ:
    case IR_VALUE_ARGS_GET_OR:
    case IR_VALUE_ENV_GET:
    case IR_VALUE_TIME_WALL_SECONDS:
    case IR_VALUE_TIME_MONOTONIC:
    case IR_VALUE_TIME_AS_MS:
    case IR_VALUE_RAND_NEXT_U32:
    case IR_VALUE_RAND_ENTROPY_U32:
      return mir_verify_platform_value_contract(ir, fun, value);
    case IR_VALUE_FS_HOST:
    case IR_VALUE_FS_OPEN:
    case IR_VALUE_FS_CREATE:
    case IR_VALUE_FS_READ_PATH:
    case IR_VALUE_FS_WRITE_PATH:
    case IR_VALUE_FS_READ_BYTES_PATH:
    case IR_VALUE_FS_READ_BYTES_AT_PATH:
    case IR_VALUE_FS_WRITE_BYTES_PATH:
    case IR_VALUE_FS_READ_ALL:
    case IR_VALUE_FS_READ_FILE:
    case IR_VALUE_FS_WRITE_ALL_FILE:
    case IR_VALUE_FS_CLOSE_FILE:
    case IR_VALUE_FS_EXISTS:
    case IR_VALUE_FS_REMOVE:
    case IR_VALUE_FS_RENAME:
    case IR_VALUE_FS_FILE_LEN:
    case IR_VALUE_FS_MAKE_DIR:
    case IR_VALUE_FS_REMOVE_DIR:
    case IR_VALUE_FS_IS_DIR:
    case IR_VALUE_FS_DIR_ENTRY_COUNT:
    case IR_VALUE_FS_TEMP_NAME:
    case IR_VALUE_FS_ATOMIC_WRITE:
      return mir_verify_fs_value_contract(ir, fun, state, value);
    case IR_VALUE_FIELD_LOAD:
      return mir_verify_field_load_value_contract(ir, fun, value);
    case IR_VALUE_CHECK:
    case IR_VALUE_RESCUE:
      return mir_verify_fallible_flow_value_contract(ir, fun, value);
    case IR_VALUE_RECORD_ADDR:
      return mir_verify_record_addr_value_contract(ir, fun, value);
  }
  char actual[128];
  snprintf(actual, sizeof(actual), "value kind %d", (int)value->kind);
  mir_verify_mark_unsupported(ir, "MIR verifier found unsupported value kind", value->line, value->column, actual);
  return false;
}

static bool mir_verify_direct_value(IrProgram *ir, const IrFunction *fun, const MirVerifierState *state, const IrValue *value, MirHelperRequirements *requirements) {
  if (!ir || !ir->mir_valid) return false;
  if (!value) return true;
  if (!mir_verify_direct_value_kind_contract(ir, fun, state, value, requirements)) return false;
  for (size_t i = 0; i < value->arg_len; i++) {
    if (!mir_verify_direct_value(ir, fun, state, value->args[i], requirements)) return false;
  }
  if (!mir_verify_direct_value(ir, fun, state, value->index, requirements)) return false;
  if (!mir_verify_direct_value(ir, fun, state, value->left, requirements)) return false;
  if (!mir_verify_direct_value(ir, fun, state, value->right, requirements)) return false;
  return true;
}

static bool mir_verify_direct_return_instr(IrProgram *ir, const IrFunction *fun, const IrInstr *instr) {
  if (!ir || !ir->mir_valid || !fun || !instr) return false;
  const IrValue *value = instr->value;
  if (fun->raises) {
    if (fun->value_return_type == IR_TYPE_VOID) {
      if (!value) return true;
      char actual[160];
      snprintf(actual, sizeof(actual), "fallible Void return carries %s", mir_type_kind_name(value->type));
      mir_verify_mark_unsupported(ir, "MIR verifier found return value mismatch", instr->line, instr->column, actual);
      return false;
    }
    if (value && value->type == fun->value_return_type) return true;
    char actual[160];
    snprintf(actual, sizeof(actual), "fallible return has %s but function value return is %s", value ? mir_type_kind_name(value->type) : "missing", mir_type_kind_name(fun->value_return_type));
    mir_verify_mark_unsupported(ir, "MIR verifier found return value mismatch", instr->line, instr->column, actual);
    return false;
  }
  if (fun->return_type == IR_TYPE_VOID) {
    if (!value) return true;
    char actual[160];
    snprintf(actual, sizeof(actual), "Void return carries %s", mir_type_kind_name(value->type));
    mir_verify_mark_unsupported(ir, "MIR verifier found return value mismatch", instr->line, instr->column, actual);
    return false;
  }
  if (value && value->type == fun->return_type) {
    if (fun->return_type == IR_TYPE_BYTE_VIEW) {
      IrTypeKind actual_element = mir_view_element_or_u8(value->element_type);
      IrTypeKind expected_element = mir_view_element_or_u8(fun->return_element_type);
      if (actual_element != expected_element) {
        char actual[160];
        snprintf(actual, sizeof(actual), "return element %s but function return element is %s", mir_type_kind_name(actual_element), mir_type_kind_name(expected_element));
        mir_verify_mark_unsupported(ir, "MIR verifier found return value mismatch", instr->line, instr->column, actual);
        return false;
      }
    } else if (fun->return_type == IR_TYPE_MAYBE_SCALAR && value->element_type != fun->return_element_type) {
      char actual[160];
      snprintf(actual, sizeof(actual), "return element %s but function return element is %s", mir_type_kind_name(value->element_type), mir_type_kind_name(fun->return_element_type));
      mir_verify_mark_unsupported(ir, "MIR verifier found return value mismatch", instr->line, instr->column, actual);
      return false;
    }
    return true;
  }
  if (fun->return_type == IR_TYPE_MAYBE_SCALAR && value && value->type == fun->return_element_type) return true;
  char actual[160];
  snprintf(actual, sizeof(actual), "return has %s but function returns %s", value ? mir_type_kind_name(value->type) : "missing", mir_type_kind_name(fun->return_type));
  mir_verify_mark_unsupported(ir, "MIR verifier found return value mismatch", instr->line, instr->column, actual);
  return false;
}

static bool mir_verify_direct_instr_contract(IrProgram *ir, const IrFunction *fun, const IrInstr *instr, MirHelperRequirements *requirements) {
  if (!ir || !ir->mir_valid || !fun || !instr) return false;
  switch (instr->kind) {
    case IR_INSTR_LOCAL_SET: {
      if (!mir_verify_local_index(ir, fun, instr->local_index, instr->line, instr->column, "MIR verifier found local write outside the local table")) return false;
      if (!instr->value) {
        mir_verify_mark_unsupported(ir, "MIR verifier found local write without a value", instr->line, instr->column, "missing local initializer");
        return false;
      }
      const IrLocal *local = &fun->locals[instr->local_index];
      if (!mir_value_can_initialize_local(local, instr->value)) {
        char actual[192];
        snprintf(actual, sizeof(actual), "local %s has %s but value has %s", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type), mir_type_kind_name(instr->value->type));
        mir_verify_mark_unsupported(ir, "MIR verifier found local write type mismatch", instr->line, instr->column, actual);
        return false;
      }
      if (!mir_verify_local_initializer_kind(ir, local, instr->value, instr->line, instr->column)) return false;
      break;
    }
    case IR_INSTR_INDEX_STORE: {
      if (!mir_verify_local_index(ir, fun, instr->array_index, instr->line, instr->column, "MIR verifier found indexed write outside the local table")) return false;
      const IrLocal *local = &fun->locals[instr->array_index];
      if (!local->is_array && local->type != IR_TYPE_BYTE_VIEW) {
        char actual[160];
        snprintf(actual, sizeof(actual), "local %s is %s", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type));
        mir_verify_mark_unsupported(ir, "MIR verifier found indexed write to an unsupported local", instr->line, instr->column, actual);
        return false;
      }
      if (!instr->index || !mir_type_is_integer_value(instr->index->type)) {
        char actual[128];
        snprintf(actual, sizeof(actual), "index is %s", instr->index ? mir_type_kind_name(instr->index->type) : "missing");
        mir_verify_mark_unsupported(ir, "MIR verifier found invalid indexed write index", instr->line, instr->column, actual);
        return false;
      }
      IrTypeKind element_type = local->type == IR_TYPE_BYTE_VIEW ? (local->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : local->element_type) : local->element_type;
      if (!instr->value || instr->value->type != element_type) {
        char actual[160];
        snprintf(actual, sizeof(actual), "indexed write has %s but element is %s", instr->value ? mir_type_kind_name(instr->value->type) : "missing", mir_type_kind_name(element_type));
        mir_verify_mark_unsupported(ir, "MIR verifier found indexed write type mismatch", instr->line, instr->column, actual);
        return false;
      }
      break;
    }
    case IR_INSTR_FIELD_STORE: {
      if (!mir_verify_local_index(ir, fun, instr->local_index, instr->line, instr->column, "MIR verifier found field write outside the local table")) return false;
      const IrLocal *local = &fun->locals[instr->local_index];
      if (!local->is_record && !local->is_record_ref) {
        char actual[160];
        snprintf(actual, sizeof(actual), "local %s is %s", local->name ? local->name : "<unnamed>", mir_type_kind_name(local->type));
        mir_verify_mark_unsupported(ir, "MIR verifier found field write to a non-record local", instr->line, instr->column, actual);
        return false;
      }
      if (!instr->value) {
        mir_verify_mark_unsupported(ir, "MIR verifier found field write without a value", instr->line, instr->column, "missing field value");
        return false;
      }
      if (!mir_verify_record_field_span(ir, local, instr->field_offset, instr->value->type, instr->line, instr->column, "MIR verifier found field write outside the local storage")) return false;
      break;
    }
    case IR_INSTR_WORLD_WRITE:
      mir_require_count(&requirements->runtime_helpers, 1, instr->line, instr->column, "world.write");
      if (!instr->value || instr->value->type != IR_TYPE_BYTE_VIEW) {
        char actual[128];
        snprintf(actual, sizeof(actual), "world write value is %s", instr->value ? mir_type_kind_name(instr->value->type) : "missing");
        mir_verify_mark_unsupported(ir, "MIR verifier found invalid world write value", instr->line, instr->column, actual);
        return false;
      }
      if (instr->field_offset != 1 && instr->field_offset != 2) {
        char actual[128];
        snprintf(actual, sizeof(actual), "world write stream id %u", instr->field_offset);
        mir_verify_mark_unsupported(ir, "MIR verifier found invalid world write stream", instr->line, instr->column, actual);
        return false;
      }
      break;
    case IR_INSTR_RETURN:
      if (!mir_verify_direct_return_instr(ir, fun, instr)) return false;
      break;
    case IR_INSTR_IF:
    case IR_INSTR_WHILE:
      if (!instr->value || instr->value->type != IR_TYPE_BOOL) {
        char actual[128];
        snprintf(actual, sizeof(actual), "branch condition is %s", instr->value ? mir_type_kind_name(instr->value->type) : "missing");
        mir_verify_mark_unsupported(ir, "MIR verifier found invalid branch condition", instr->line, instr->column, actual);
        return false;
      }
      break;
    case IR_INSTR_RAISE:
      if (!fun->raises && !fun->world_param_name) {
        mir_verify_mark_unsupported(ir, "MIR verifier found raise in a non-fallible function", instr->line, instr->column, fun->name ? fun->name : "<unnamed>");
        return false;
      }
      break;
    case IR_INSTR_BREAK:
    case IR_INSTR_CONTINUE:
    case IR_INSTR_EXPR:
      break;
    default: {
      char actual[128];
      snprintf(actual, sizeof(actual), "instruction kind %d", (int)instr->kind);
      mir_verify_mark_unsupported(ir, "MIR verifier found unsupported instruction kind", instr->line, instr->column, actual);
      return false;
    }
  }
  return true;
}

static bool mir_verify_direct_instrs(IrProgram *ir, const IrFunction *fun, const IrInstr *instrs, size_t len, MirVerifierState *state, MirHelperRequirements *requirements, size_t loop_depth);

static void mir_apply_instr_state_effect(const IrFunction *fun, const IrInstr *instr, MirVerifierState *state) {
  if (!fun || !instr || !state || instr->kind != IR_INSTR_LOCAL_SET) return;
  if (instr->local_index >= fun->local_len || instr->local_index >= state->len || !state->mutable_maybe_bytes) return;
  const IrLocal *local = &fun->locals[instr->local_index];
  if (local->type != IR_TYPE_MAYBE_BYTE_VIEW) return;
  state->mutable_maybe_bytes[instr->local_index] = mir_value_produces_mutable_byte_payload(instr->value);
}

static bool mir_verify_branch_instrs(IrProgram *ir, const IrFunction *fun, const IrInstr *instr, MirVerifierState *state, MirHelperRequirements *requirements, size_t loop_depth) {
  MirVerifierState then_state = {0};
  MirVerifierState else_state = {0};
  if (!mir_state_clone(ir, &then_state, state, instr->line, instr->column)) return false;
  if (!mir_state_clone(ir, &else_state, state, instr->line, instr->column)) {
    mir_state_free(&then_state);
    return false;
  }
  bool ok = mir_verify_direct_instrs(ir, fun, instr->then_instrs, instr->then_len, &then_state, requirements, loop_depth) &&
            mir_verify_direct_instrs(ir, fun, instr->else_instrs, instr->else_len, &else_state, requirements, loop_depth) &&
            mir_state_intersect_from(state, &then_state, &else_state);
  mir_state_free(&then_state);
  mir_state_free(&else_state);
  return ok;
}

static bool mir_verify_loop_instrs(IrProgram *ir, const IrFunction *fun, const IrInstr *instr, MirVerifierState *state, MirHelperRequirements *requirements, size_t loop_depth) {
  MirVerifierState entry_state = {0};
  if (!mir_state_clone(ir, &entry_state, state, instr->line, instr->column)) return false;
  bool changed = false;
  do {
    MirVerifierState body_state = {0};
    if (!mir_state_clone(ir, &body_state, &entry_state, instr->line, instr->column)) {
      mir_state_free(&entry_state);
      return false;
    }
    bool ok = mir_verify_direct_instrs(ir, fun, instr->then_instrs, instr->then_len, &body_state, requirements, loop_depth + 1);
    if (!ok) {
      mir_state_free(&body_state);
      mir_state_free(&entry_state);
      return false;
    }
    changed = false;
    for (size_t i = 0; i < entry_state.len; i++) {
      bool next = entry_state.mutable_maybe_bytes[i] && body_state.mutable_maybe_bytes[i];
      if (next != entry_state.mutable_maybe_bytes[i]) changed = true;
      entry_state.mutable_maybe_bytes[i] = next;
    }
    mir_state_free(&body_state);
  } while (changed);
  if (state->len == entry_state.len && state->len > 0) {
    memcpy(state->mutable_maybe_bytes, entry_state.mutable_maybe_bytes, state->len * sizeof(bool));
  }
  mir_state_free(&entry_state);
  return true;
}

static bool mir_verify_direct_instrs(IrProgram *ir, const IrFunction *fun, const IrInstr *instrs, size_t len, MirVerifierState *state, MirHelperRequirements *requirements, size_t loop_depth) {
  if (!ir || !ir->mir_valid) return false;
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (!mir_verify_direct_value(ir, fun, state, instr->value, requirements)) return false;
    if (!mir_verify_direct_value(ir, fun, state, instr->index, requirements)) return false;
    if (!mir_verify_direct_instr_contract(ir, fun, instr, requirements)) return false;
    if ((instr->kind == IR_INSTR_BREAK || instr->kind == IR_INSTR_CONTINUE) && loop_depth == 0) {
      mir_verify_mark_unsupported(ir, "MIR verifier found break or continue outside a loop", instr->line, instr->column, instr->kind == IR_INSTR_BREAK ? "break" : "continue");
      return false;
    }
    if (instr->kind == IR_INSTR_IF) {
      if (!mir_verify_branch_instrs(ir, fun, instr, state, requirements, loop_depth)) return false;
      continue;
    }
    if (instr->kind == IR_INSTR_WHILE) {
      if (!mir_verify_loop_instrs(ir, fun, instr, state, requirements, loop_depth)) return false;
      continue;
    }
    mir_apply_instr_state_effect(fun, instr, state);
  }
  return true;
}

static bool mir_verify_count_satisfies(IrProgram *ir, const MirCountRequirement *req, size_t actual, const char *name, const char *message) {
  if (!ir || !ir->mir_valid || !req || req->count <= actual) return ir && ir->mir_valid;
  char detail[192];
  snprintf(detail, sizeof(detail), "%s count %zu but MIR requires at least %zu for %s", name ? name : "helper", actual, req->count, req->reason ? req->reason : "helper use");
  mir_verify_mark_unsupported(ir, message, req->line, req->column, detail);
  return false;
}

static bool mir_verify_direct_helper_requirements(IrProgram *ir, const MirHelperRequirements *requirements) {
  if (!ir || !ir->mir_valid || !requirements) return false;
  if (!mir_verify_count_satisfies(ir, &requirements->allocator_helpers, ir->direct_allocator_helper_count, "allocator helper", "MIR verifier found missing allocator helper contract")) return false;
  if (!mir_verify_count_satisfies(ir, &requirements->buffer_helpers, ir->direct_buffer_helper_count, "buffer helper", "MIR verifier found missing buffer helper contract")) return false;
  if (!mir_verify_count_satisfies(ir, &requirements->runtime_helpers, ir->direct_runtime_helper_count, "runtime helper", "MIR verifier found missing runtime helper contract")) return false;
  if (!mir_verify_count_satisfies(ir, &requirements->host_runtime_imports, ir->direct_host_runtime_import_count, "host runtime import", "MIR verifier found missing host runtime import contract")) return false;
  if (!mir_verify_count_satisfies(ir, &requirements->http_runtime_imports, ir->direct_http_runtime_import_count, "HTTP runtime import", "MIR verifier found missing HTTP runtime import contract")) return false;
  return true;
}

bool z_mir_verify_direct_contracts(IrProgram *ir) {
  if (!ir || !ir->mir_valid) return false;
  for (size_t i = 0; i < ir->function_len; i++) {
    if (!mir_verify_direct_function_contract(ir, &ir->functions[i])) return false;
  }
  MirHelperRequirements requirements = {0};
  for (size_t i = 0; i < ir->function_len; i++) {
    IrFunction *fun = &ir->functions[i];
    MirVerifierState state = {0};
    if (!mir_state_init(ir, &state, fun->local_len, fun->line, fun->column)) return false;
    bool ok = mir_verify_direct_instrs(ir, fun, fun->instrs, fun->instr_len, &state, &requirements, 0);
    mir_state_free(&state);
    if (!ok) return false;
  }
  return mir_verify_direct_helper_requirements(ir, &requirements);
}
