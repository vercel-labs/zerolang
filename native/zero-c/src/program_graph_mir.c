#include "zero.h"
#include "c_import.h"
#include "mir_verify.h"
#include "program_graph_build.h"
#include "program_graph_format.h"
#include "program_graph.h"
#include "program_graph_lower.h"
#include "program_graph_projection.h"
#include "program_graph_size.h"
#include "program_graph_store.h"
#include "program_graph_mir_std.h"
#include "std_source.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { IR_READONLY_DATA_BASE = 1024u, IR_READONLY_DATA_LIMIT = 65536u };

static void *ir_grow_tracked_items(IrProgram *ir, void *items, size_t len, size_t *cap, size_t initial, size_t item_size) {
  if (len + 1 > *cap) {
    size_t next_cap = z_grow_capacity(*cap, len + 1, initial);
    void *next_items = z_checked_reallocarray(items, next_cap, item_size);
    if (ir) ir->mir_bytes += next_cap * item_size;
    *cap = next_cap;
    return next_items;
  }
  return items;
}

static IrTypeKind ir_type_kind(const char *type);

static bool ir_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static IrTypeKind ir_span_element_kind(const char *type) {
  if (!type) return IR_TYPE_UNSUPPORTED;
  if (ir_text_eq(type, "Bool") || ir_text_eq(type, "bool")) return IR_TYPE_BOOL;
  if (ir_text_eq(type, "u8")) return IR_TYPE_U8;
  if (ir_text_eq(type, "u16")) return IR_TYPE_U16;
  if (ir_text_eq(type, "usize")) return IR_TYPE_USIZE;
  if (ir_text_eq(type, "i32")) return IR_TYPE_I32;
  if (ir_text_eq(type, "u32")) return IR_TYPE_U32;
  if (ir_text_eq(type, "i64")) return IR_TYPE_I64;
  if (ir_text_eq(type, "u64")) return IR_TYPE_U64;
  return IR_TYPE_UNSUPPORTED;
}

static bool ir_span_type_element(const char *type, bool *is_mutable, IrTypeKind *element_type) {
  if (!type) return false;
  const char *inner = NULL;
  size_t inner_len = 0;
  size_t type_len = strlen(type);
  bool mut = false;
  if (type_len > strlen("Span<>") && strncmp(type, "Span<", strlen("Span<")) == 0) {
    inner = type + strlen("Span<");
    inner_len = type_len - strlen("Span<") - 1;
  } else if (type_len > strlen("MutSpan<>") && strncmp(type, "MutSpan<", strlen("MutSpan<")) == 0) {
    inner = type + strlen("MutSpan<");
    inner_len = type_len - strlen("MutSpan<") - 1;
    mut = true;
  } else {
    return false;
  }
  if (inner_len == 0 || type[type_len - 1] != '>') return false;
  char *element_text = z_strndup(inner, inner_len);
  IrTypeKind element = ir_span_element_kind(element_text);
  free(element_text);
  if (element == IR_TYPE_UNSUPPORTED) return false;
  if (is_mutable) *is_mutable = mut;
  if (element_type) *element_type = element;
  return true;
}

static bool ir_type_name_is_mutable_byte_view(const char *type) { bool is_mutable = false; return ir_span_type_element(type, &is_mutable, NULL) && is_mutable; }

static IrTypeKind ir_view_element_type_for_type(const char *type) {
  IrTypeKind element = IR_TYPE_UNSUPPORTED;
  if (ir_span_type_element(type, NULL, &element)) return element;
  if (type && ir_type_kind(type) == IR_TYPE_BYTE_VIEW) return IR_TYPE_U8;
  return IR_TYPE_UNSUPPORTED;
}

static IrTypeKind ir_type_kind(const char *type) {
  if (!type) return IR_TYPE_UNSUPPORTED;
  if (ir_text_eq(type, "Void")) return IR_TYPE_VOID;
  IrTypeKind scalar_type = ir_span_element_kind(type);
  if (scalar_type != IR_TYPE_UNSUPPORTED) return scalar_type;
  if (ir_text_eq(type, "Duration")) return IR_TYPE_I64;
  if (ir_text_eq(type, "RandSource")) return IR_TYPE_U32;
  if (ir_text_eq(type, "ProcStatus")) return IR_TYPE_I32;
  if (ir_text_eq(type, "Net") || ir_text_eq(type, "HttpClient")) return IR_TYPE_I32;
  if (ir_text_eq(type, "HttpResult")) return IR_TYPE_U64;
  if (ir_text_eq(type, "HttpError")) return IR_TYPE_U32;
  if (ir_text_eq(type, "HttpHeaderValue")) return IR_TYPE_U64;
  if (ir_text_eq(type, "Fs") || ir_text_eq(type, "File") || ir_text_eq(type, "owned<File>")) return IR_TYPE_I32;
  if (ir_text_eq(type, "String") || ir_text_eq(type, "Span<const u8>") || ir_text_eq(type, "ByteBuf") || ir_text_eq(type, "owned<ByteBuf>") || ir_span_type_element(type, NULL, NULL)) return IR_TYPE_BYTE_VIEW;
  if (ir_text_eq(type, "FixedBufAlloc")) return IR_TYPE_ALLOC;
  if (ir_text_eq(type, "Vec")) return IR_TYPE_VEC;
  if (ir_text_eq(type, "BufferedReader") || ir_text_eq(type, "BufferedWriter")) return IR_TYPE_BYTE_VIEW;
  if (ir_text_eq(type, "Maybe<MutSpan<u8>>") || ir_text_eq(type, "Maybe<Span<u8>>") || ir_text_eq(type, "Maybe<String>") || ir_text_eq(type, "Maybe<owned<ByteBuf>>")) return IR_TYPE_MAYBE_BYTE_VIEW;
  if (ir_text_eq(type, "Maybe<JsonDoc>") || ir_text_eq(type, "Maybe<Bool>") || ir_text_eq(type, "Maybe<u8>") || ir_text_eq(type, "Maybe<u16>") || ir_text_eq(type, "Maybe<usize>") || ir_text_eq(type, "Maybe<i32>") || ir_text_eq(type, "Maybe<u32>") || ir_text_eq(type, "Maybe<owned<File>>")) return IR_TYPE_MAYBE_SCALAR;
  return IR_TYPE_UNSUPPORTED;
}

static bool ir_type_is_value(IrTypeKind type) {
  return type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_I32 || type == IR_TYPE_U32 || type == IR_TYPE_I64 || type == IR_TYPE_U64;
}

static bool ir_type_is_direct_local(IrTypeKind type) {
  return type == IR_TYPE_BOOL || ir_type_is_value(type) || type == IR_TYPE_BYTE_VIEW || type == IR_TYPE_ALLOC || type == IR_TYPE_VEC || type == IR_TYPE_MAYBE_BYTE_VIEW || type == IR_TYPE_MAYBE_SCALAR;
}

static bool ir_type_is_direct_abi(IrTypeKind type) {
  return type == IR_TYPE_BOOL || ir_type_is_value(type);
}

static bool ir_type_is_direct_param_abi(IrTypeKind type) {
  return ir_type_is_direct_abi(type) || type == IR_TYPE_BYTE_VIEW;
}

static bool ir_type_is_direct_return_abi(IrTypeKind type) {
  return ir_type_is_direct_abi(type) || type == IR_TYPE_BYTE_VIEW || type == IR_TYPE_MAYBE_BYTE_VIEW || type == IR_TYPE_MAYBE_SCALAR;
}

static bool ir_type_is_direct_fallible_value(IrTypeKind type) {
  return type == IR_TYPE_VOID || type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_I32 || type == IR_TYPE_U32;
}

static IrTypeKind ir_maybe_scalar_element_type(const char *type) {
  const char *prefix = "Maybe<";
  size_t prefix_len = strlen(prefix);
  size_t len = type ? strlen(type) : 0;
  if (len <= prefix_len + 1 || strncmp(type, prefix, prefix_len) != 0 || type[len - 1] != '>') return IR_TYPE_UNSUPPORTED;
  char *inner = z_strndup(type + prefix_len, len - prefix_len - 1);
  IrTypeKind element = ir_type_kind(inner);
  free(inner);
  return (ir_type_is_value(element) || element == IR_TYPE_BOOL) ? element : IR_TYPE_UNSUPPORTED;
}

static unsigned ir_type_byte_size(IrTypeKind type) {
  switch (type) {
    case IR_TYPE_BOOL:
    case IR_TYPE_U8: return 1;
    case IR_TYPE_U16: return 2;
    case IR_TYPE_I32:
    case IR_TYPE_U32: return 4;
    case IR_TYPE_I64:
    case IR_TYPE_USIZE:
    case IR_TYPE_U64: return 8;
    default: return 0;
  }
}

static unsigned ir_type_alignment(IrTypeKind type) {
  unsigned size = ir_type_byte_size(type);
  return size >= 8 ? 8 : (size >= 4 ? 4 : (size ? size : 1));
}

static bool ir_parse_fixed_array_type(const char *type, unsigned *out_len, IrTypeKind *out_element) {
  if (!type || type[0] != '[') return false;
  const char *close = strchr(type, ']');
  if (!close || close == type + 1 || !close[1]) return false;
  unsigned long long len = 0;
  for (const char *cursor = type + 1; cursor < close; cursor++) {
    if (*cursor < '0' || *cursor > '9') return false;
    len = len * 10ull + (unsigned long long)(*cursor - '0');
    if (len > UINT_MAX) return false;
  }
  IrTypeKind element = ir_type_kind(close + 1);
  if (element == IR_TYPE_UNSUPPORTED) return false;
  if (out_len) *out_len = (unsigned)len;
  if (out_element) *out_element = element;
  return true;
}

static size_t ir_align_to(size_t value, size_t alignment) {
  size_t remainder = alignment ? value % alignment : 0;
  return remainder == 0 ? value : value + (alignment - remainder);
}

static void ir_mark_unsupported_at(IrProgram *ir, const char *message, const char *path, int line, int column, const char *actual) {
  if (!ir || !ir->mir_valid) return;
  ir->mir_valid = false;
  ir->mir_path = path && path[0] ? z_strdup(path) : NULL;
  ir->mir_line = line > 0 ? line : 1;
  ir->mir_column = column > 0 ? column : 1;
  snprintf(ir->mir_message, sizeof(ir->mir_message), "%s", message ? message : "typed graph MIR lowering failed");
  snprintf(ir->mir_expected, sizeof(ir->mir_expected), "typed program graph MIR subset");
  snprintf(ir->mir_actual, sizeof(ir->mir_actual), "%s", actual ? actual : "unsupported graph construct");
  snprintf(ir->mir_help, sizeof(ir->mir_help), "use zero check to inspect unsupported graph constructs or choose another supported target");
  z_backend_blocker_set(&ir->backend_blocker, NULL, NULL, NULL, "lower", ir->mir_actual);
}

static void ir_mark_unsupported(IrProgram *ir, const char *message, int line, int column, const char *actual) {
  ir_mark_unsupported_at(ir, message, NULL, line, column, actual);
}

static const char *ir_graph_pin_diag_path(SourceInput *input, const IrProgram *ir, const char *fallback_path) {
  const char *path = ir && ir->mir_path && ir->mir_path[0] ? ir->mir_path : (input && input->source_file ? input->source_file : fallback_path);
  if (!input || !path || !path[0] || ir_text_eq(path, input->source_file)) return path;
  input->source_files = z_checked_reallocarray(input->source_files, input->source_file_count + 1, sizeof(char *));
  input->source_files[input->source_file_count] = z_strdup(path);
  return input->source_files[input->source_file_count++];
}

static void ir_graph_init_lowering_diag(ZDiag *diag, SourceInput *input, const ZTargetInfo *target, const char *emit_kind, const char *requested_backend, const IrProgram *ir, const char *fallback_path) {
  if (!diag) return;
  memset(diag, 0, sizeof(*diag));
  diag->code = 2004;
  diag->path = ir_graph_pin_diag_path(input, ir, fallback_path);
  diag->line = ir && ir->mir_line > 0 ? ir->mir_line : 1;
  diag->column = ir && ir->mir_column > 0 ? ir->mir_column : 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", ir && ir->mir_message[0] ? ir->mir_message : "typed graph MIR lowering failed");
  snprintf(diag->expected, sizeof(diag->expected), "%s", ir && ir->mir_expected[0] ? ir->mir_expected : "typed program graph MIR subset");
  snprintf(diag->actual, sizeof(diag->actual), "%s", ir && ir->mir_actual[0] ? ir->mir_actual : "unsupported graph construct");
  snprintf(diag->help, sizeof(diag->help), "%s", ir && ir->mir_help[0] ? ir->mir_help : "use zero check to inspect unsupported graph constructs or choose another supported target");
  const char *backend = z_backend_request_is_llvm(requested_backend, emit_kind) ? "llvm" : z_direct_backend_name_for_emit_kind(target, emit_kind ? emit_kind : "exe", z_backend_direct_request_name(requested_backend));
  z_backend_blocker_set(&diag->backend_blocker, target ? target->name : "", target ? target->object_format : "", backend, "lower", diag->actual);
}

static bool ir_parse_integer_literal(const char *text, unsigned long long *out) {
  if (!text || !text[0]) return false;
  size_t text_len = strlen(text);
  size_t body_len = text_len;
  const char *last_underscore = strrchr(text, '_');
  if (last_underscore && (last_underscore[1] == 'i' || last_underscore[1] == 'u')) body_len = (size_t)(last_underscore - text);
  if (body_len == 0) return false;
  unsigned radix = 10;
  size_t index = 0;
  if (body_len > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) { radix = 16; index = 2; }
  else if (body_len > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) { radix = 2; index = 2; }
  else if (body_len > 2 && text[0] == '0' && (text[1] == 'o' || text[1] == 'O')) { radix = 8; index = 2; }
  unsigned long long value = 0;
  bool saw_digit = false;
  for (; index < body_len; index++) {
    char ch = text[index];
    if (ch == '_') continue;
    int digit = -1;
    if (ch >= '0' && ch <= '9') digit = ch - '0';
    else if (ch >= 'a' && ch <= 'f') digit = 10 + (ch - 'a');
    else if (ch >= 'A' && ch <= 'F') digit = 10 + (ch - 'A');
    if (digit < 0 || (unsigned)digit >= radix) return false;
    if (value > (ULLONG_MAX - (unsigned)digit) / radix) return false;
    value = value * radix + (unsigned)digit;
    saw_digit = true;
  }
  if (!saw_digit) return false;
  if (out) *out = value;
  return true;
}

static IrTypeKind ir_integer_literal_suffix_type(const char *text) {
  const char *suffix = text ? strrchr(text, '_') : NULL;
  if (!suffix || suffix == text) return IR_TYPE_UNSUPPORTED;
  suffix++;
  if (ir_text_eq(suffix, "u8")) return IR_TYPE_U8;
  if (ir_text_eq(suffix, "u16")) return IR_TYPE_U16;
  if (ir_text_eq(suffix, "usize")) return IR_TYPE_USIZE;
  if (ir_text_eq(suffix, "i32")) return IR_TYPE_I32;
  if (ir_text_eq(suffix, "u32")) return IR_TYPE_U32;
  if (ir_text_eq(suffix, "i64")) return IR_TYPE_I64;
  if (ir_text_eq(suffix, "u64")) return IR_TYPE_U64;
  return IR_TYPE_UNSUPPORTED;
}

static IrValue *ir_new_value(IrProgram *ir, IrValueKind kind, IrTypeKind type, int line, int column) {
  IrValue *value = z_checked_calloc(1, sizeof(IrValue));
  value->kind = kind;
  value->type = type;
  value->line = line;
  value->column = column;
  if (ir) ir->mir_bytes += sizeof(IrValue);
  return value;
}

static IrValue *ir_new_bool_literal(IrProgram *ir, bool enabled, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_BOOL, IR_TYPE_BOOL, line, column);
  value->int_value = enabled ? 1 : 0;
  return value;
}

static IrValue *ir_new_integer_literal_value(IrProgram *ir, IrTypeKind type, unsigned long long number, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_INT, type, line, column);
  value->int_value = number;
  return value;
}

static IrValue *ir_new_index_literal(IrProgram *ir, unsigned index, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_INT, IR_TYPE_USIZE, line, column);
  value->int_value = index;
  return value;
}

static IrValue *ir_new_maybe_byte_view_literal(IrProgram *ir, bool has, IrValue *view, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_BYTE_VIEW_LITERAL, IR_TYPE_MAYBE_BYTE_VIEW, line, column);
  value->data_len = has ? 1u : 0u;
  value->element_type = view && view->element_type != IR_TYPE_UNSUPPORTED ? view->element_type : IR_TYPE_U8;
  value->left = view;
  return value;
}

static IrValue *ir_new_maybe_scalar_literal(IrProgram *ir, bool has, IrTypeKind element_type, unsigned long long number, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_SCALAR_LITERAL, IR_TYPE_MAYBE_SCALAR, line, column);
  value->data_len = has ? 1u : 0u;
  value->element_type = element_type;
  value->int_value = number;
  return value;
}

static IrValue *ir_new_cast_value(IrProgram *ir, IrValue *inner, IrTypeKind type, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_CAST, type, line, column);
  value->left = inner;
  return value;
}

static IrValue *ir_new_binary_value(IrProgram *ir, IrBinaryOp op, IrTypeKind type, IrValue *left, IrValue *right, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_BINARY, type, line, column);
  value->binary_op = op;
  value->left = left;
  value->right = right;
  return value;
}

static IrValue *ir_new_compare_value(IrProgram *ir, IrCompareOp op, IrValue *left, IrValue *right, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_COMPARE, IR_TYPE_BOOL, line, column);
  value->compare_op = op;
  value->left = left;
  value->right = right;
  return value;
}

static void ir_free_value(IrValue *value) {
  if (!value) return;
  ir_free_value(value->index);
  ir_free_value(value->left);
  ir_free_value(value->right);
  for (size_t i = 0; i < value->arg_len; i++) ir_free_value(value->args[i]);
  free(value->args);
  free(value);
}

static void ir_free_instrs(IrInstr *instrs, size_t len) {
  if (!instrs) return;
  for (size_t i = 0; i < len; i++) {
    ir_free_value(instrs[i].value);
    ir_free_value(instrs[i].index);
    ir_free_instrs(instrs[i].then_instrs, instrs[i].then_len);
    ir_free_instrs(instrs[i].else_instrs, instrs[i].else_len);
    free(instrs[i].then_instrs);
    free(instrs[i].else_instrs);
  }
}

static void ir_function_push_local(IrProgram *ir, IrFunction *fun, const char *name, IrTypeKind type, bool is_param, bool is_array, bool is_record, const char *shape_name, IrTypeKind element_type, unsigned array_len, unsigned byte_size_override, unsigned alignment_override, bool is_mutable, int line, int column) {
  fun->locals = ir_grow_tracked_items(ir, fun->locals, fun->local_len, &fun->local_cap, 4, sizeof(IrLocal));
  unsigned byte_size = byte_size_override ? byte_size_override : (is_array ? ir_type_byte_size(element_type) * array_len : (type == IR_TYPE_BYTE_VIEW || type == IR_TYPE_ALLOC || type == IR_TYPE_VEC ? 16 : (type == IR_TYPE_MAYBE_BYTE_VIEW ? 24 : (type == IR_TYPE_MAYBE_SCALAR ? 16 : 8))));
  unsigned alignment = alignment_override ? alignment_override : (is_array ? ir_type_alignment(element_type) : 8);
  fun->locals[fun->local_len] = (IrLocal){.name = z_strdup(name), .type = type, .element_type = element_type, .index = (unsigned)fun->local_len, .array_len = array_len, .byte_size = byte_size, .alignment = alignment, .is_param = is_param, .is_array = is_array, .is_record = is_record, .is_mutable = is_mutable, .shape_name = shape_name ? z_strdup(shape_name) : NULL, .line = line, .column = column};
  fun->local_len++;
  if (is_param) fun->param_count++;
}

static const IrLocal *ir_function_find_local(const IrFunction *fun, const char *name) {
  for (size_t i = fun ? fun->local_len : 0; i > 0; i--) {
    if (ir_text_eq(fun->locals[i - 1].name, name)) return &fun->locals[i - 1];
  }
  return NULL;
}

static size_t ir_active_local_mark(const IrProgram *ir) { return ir ? ir->active_local_len : 0; }

static void ir_active_local_restore(IrProgram *ir, size_t mark) {
  if (!ir || mark > ir->active_local_len) return;
  for (size_t i = mark; i < ir->active_local_len; i++) free(ir->active_local_names[i]);
  ir->active_local_len = mark;
}

static void ir_active_local_push(IrProgram *ir, const char *name) {
  if (!ir || !name) return;
  ir->active_local_names = ir_grow_tracked_items(ir, ir->active_local_names, ir->active_local_len, &ir->active_local_cap, 8, sizeof(char *));
  ir->active_local_names[ir->active_local_len++] = z_strdup(name);
}

static bool ir_active_local_has(const IrProgram *ir, const char *name) {
  for (size_t i = ir ? ir->active_local_len : 0; i > 0; i--) {
    if (ir_text_eq(ir->active_local_names[i - 1], name)) return true;
  }
  return false;
}

static bool ir_find_function_index(const IrProgram *ir, const char *name, unsigned *out_index) {
  for (size_t i = 0; ir && name && i < ir->function_len; i++) {
    if (ir_text_eq(ir->functions[i].name, name)) {
      if (out_index) *out_index = (unsigned)i;
      return true;
    }
  }
  return false;
}

static void ir_graph_external_function_push_param(IrProgram *ir, IrExternalFunction *function, IrTypeKind type) {
  function->param_types = ir_grow_tracked_items(ir, function->param_types, function->param_len, &function->param_cap, 4, sizeof(IrTypeKind));
  function->param_types[function->param_len++] = type;
}

static bool ir_graph_find_external_function_index(const IrProgram *ir, const ZCImportFunction *function, unsigned *out_index) {
  if (!ir || !function || !function->name) return false;
  for (size_t i = 0; i < ir->external_function_len; i++) {
    const IrExternalFunction *candidate = &ir->external_functions[i];
    const char *candidate_header = candidate->import_resolved_header && candidate->import_resolved_header[0] ? candidate->import_resolved_header : candidate->import_header;
    const char *function_header = function->import_resolved_header && function->import_resolved_header[0] ? function->import_resolved_header : function->import_header;
    if (!candidate->symbol || strcmp(candidate->symbol, function->name) != 0 || candidate->return_type != ir_type_kind(function->return_zero_type) || candidate->param_len != function->param_len) continue;
    if ((candidate_header && !function_header) || (!candidate_header && function_header) ||
        (candidate_header && function_header && strcmp(candidate_header, function_header) != 0)) continue;
    bool params_match = true;
    for (size_t p = 0; p < function->param_len; p++) {
      if (candidate->param_types[p] != ir_type_kind(function->params[p].zero_type)) {
        params_match = false;
        break;
      }
    }
    if (params_match) {
      if (out_index) *out_index = (unsigned)i;
      return true;
    }
  }
  return false;
}

static unsigned ir_graph_add_external_function(IrProgram *ir, const ZCImportFunction *function) {
  unsigned index = 0;
  if (ir_graph_find_external_function_index(ir, function, &index)) return index;
  ir->external_functions = ir_grow_tracked_items(ir, ir->external_functions, ir->external_function_len, &ir->external_function_cap, 4, sizeof(IrExternalFunction));
  IrExternalFunction *external = &ir->external_functions[ir->external_function_len];
  *external = (IrExternalFunction){
    .symbol = z_strdup(function->name),
    .import_header = function->import_header ? z_strdup(function->import_header) : NULL,
    .import_resolved_header = function->import_resolved_header ? z_strdup(function->import_resolved_header) : NULL,
    .return_type = ir_type_kind(function->return_zero_type)
  };
  for (size_t i = 0; i < function->param_len; i++) {
    ir_graph_external_function_push_param(ir, external, ir_type_kind(function->params[i].zero_type));
  }
  return (unsigned)ir->external_function_len++;
}

static void ir_value_push_arg(IrProgram *ir, IrValue *value, IrValue *arg) {
  value->args = ir_grow_tracked_items(ir, value->args, value->arg_len, &value->arg_cap, 4, sizeof(IrValue *));
  value->args[value->arg_len++] = arg;
}

bool z_program_graph_ir_add_readonly_data(IrProgram *ir, const unsigned char *bytes, unsigned len, int line, int column, unsigned *out_offset) {
  for (size_t i = 0; i < ir->data_segment_len; i++) {
    IrDataSegment *segment = &ir->data_segments[i];
    if (segment->len == len && (len == 0 || memcmp(segment->bytes, bytes, len) == 0)) {
      if (out_offset) *out_offset = segment->offset;
      return true;
    }
  }
  size_t offset = IR_READONLY_DATA_BASE + ir->readonly_data_bytes;
  if (offset + len >= IR_READONLY_DATA_LIMIT) {
    ir_mark_unsupported(ir, "typed graph MIR readonly data exceeds the bootstrap data limit", line, column, "string/data segment");
    return false;
  }
  ir->data_segments = ir_grow_tracked_items(ir, ir->data_segments, ir->data_segment_len, &ir->data_segment_cap, 4, sizeof(IrDataSegment));
  IrDataSegment *segment = &ir->data_segments[ir->data_segment_len++];
  segment->offset = (unsigned)offset;
  segment->len = len;
  segment->bytes = z_checked_malloc(len == 0 ? 1 : len);
  if (len > 0) memcpy(segment->bytes, bytes, len);
  ir->readonly_data_bytes += len;
  ir->direct_readonly_data_bytes = ir->readonly_data_bytes;
  if (out_offset) *out_offset = segment->offset;
  return true;
}

static bool ir_make_string_literal_value(IrProgram *ir, const char *text, int line, int column, IrValue **out) {
  unsigned offset = 0;
  text = text ? text : "";
  unsigned len = (unsigned)strlen(text);
  unsigned char *nul_terminated = z_checked_malloc((size_t)len + 1);
  memcpy(nul_terminated, text, len);
  nul_terminated[len] = 0;
  bool added = z_program_graph_ir_add_readonly_data(ir, nul_terminated, len + 1, line, column, &offset);
  free(nul_terminated);
  if (!added) return false;
  IrValue *value = ir_new_value(ir, IR_VALUE_STRING_LITERAL, IR_TYPE_BYTE_VIEW, line, column);
  value->data_offset = offset;
  value->data_len = len;
  value->element_type = IR_TYPE_U8;
  *out = value;
  return true;
}

static void ir_instr_vec_push(IrProgram *ir, IrInstr **items, size_t *len, size_t *cap, IrInstr instr) {
  *items = ir_grow_tracked_items(ir, *items, *len, cap, 4, sizeof(IrInstr));
  (*items)[(*len)++] = instr;
}

static bool ir_binary_op(const char *text, IrBinaryOp *out) {
  if (!text) return false;
  if (ir_text_eq(text, "+")) *out = IR_BIN_ADD;
  else if (ir_text_eq(text, "-")) *out = IR_BIN_SUB;
  else if (ir_text_eq(text, "*")) *out = IR_BIN_MUL;
  else if (ir_text_eq(text, "/")) *out = IR_BIN_DIV;
  else if (ir_text_eq(text, "%")) *out = IR_BIN_MOD;
  else if (ir_text_eq(text, "&&")) *out = IR_BIN_AND;
  else if (ir_text_eq(text, "||")) *out = IR_BIN_OR;
  else return false;
  return true;
}

static bool ir_compare_op(const char *text, IrCompareOp *out) {
  if (!text) return false;
  if (ir_text_eq(text, "==")) *out = IR_CMP_EQ;
  else if (ir_text_eq(text, "!=")) *out = IR_CMP_NE;
  else if (ir_text_eq(text, "<")) *out = IR_CMP_LT;
  else if (ir_text_eq(text, "<=")) *out = IR_CMP_LE;
  else if (ir_text_eq(text, ">")) *out = IR_CMP_GT;
  else if (ir_text_eq(text, ">=")) *out = IR_CMP_GE;
  else return false;
  return true;
}

typedef struct {
  const char *name;
  const char *stable_id;
  size_t source_index;
} IrFunctionOrder;

static int ir_function_order_compare(const void *left, const void *right) {
  const IrFunctionOrder *a = (const IrFunctionOrder *)left;
  const IrFunctionOrder *b = (const IrFunctionOrder *)right;
  int stable_cmp = strcmp(a->stable_id ? a->stable_id : "", b->stable_id ? b->stable_id : "");
  if (stable_cmp != 0) return stable_cmp;
  if (a->source_index < b->source_index) return -1;
  if (a->source_index > b->source_index) return 1;
  return 0;
}

static void ir_free_function_order(IrFunctionOrder *order, char **stable_ids, size_t len) {
  for (size_t i = 0; stable_ids && i < len; i++) free(stable_ids[i]);
  free(stable_ids);
  free(order);
}

static bool ir_graph_node_is_test_function(const ZProgramGraphNode *node) {
  return node && node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION &&
         node->name && strncmp(node->name, "__zero_test_", strlen("__zero_test_")) == 0;
}

static int ir_graph_line(const ZProgramGraphNode *node) { return node && node->line > 0 ? node->line : 1; }
static int ir_graph_column(const ZProgramGraphNode *node) { return node && node->column > 0 ? node->column : 1; }
static void ir_graph_mark_unsupported(IrProgram *ir, const ZProgramGraphNode *node, const char *message, const char *actual) {
  ir_mark_unsupported_at(ir, message, node && node->path && node->path[0] ? node->path : NULL, ir_graph_line(node), ir_graph_column(node), actual);
}

static const ZProgramGraphNode *ir_graph_find_node(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (ir_text_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  }
  return NULL;
}

static const ZProgramGraphEdge *ir_graph_ordered_edge(const ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        edge->order == order &&
        ir_text_eq(edge->from, from) &&
        ir_text_eq(edge->kind, kind)) {
      return edge;
    }
  }
  return NULL;
}

static const ZProgramGraphNode *ir_graph_ordered_node(const ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  const ZProgramGraphEdge *edge = ir_graph_ordered_edge(graph, from, kind, order);
  return edge ? ir_graph_find_node(graph, edge->to) : NULL;
}

static const ZProgramGraphEdge *ir_graph_next_edge_by_order(const ZProgramGraph *graph, const char *from, const char *kind, bool have_last, size_t last_order) {
  const ZProgramGraphEdge *best = NULL;
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE ||
        !ir_text_eq(edge->from, from) ||
        !ir_text_eq(edge->kind, kind) ||
        (have_last && edge->order <= last_order)) {
      continue;
    }
    if (!best || edge->order < best->order) best = edge;
  }
  return best;
}

static size_t ir_graph_edge_count(const ZProgramGraph *graph, const char *from, const char *kind) {
  size_t count = 0;
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        ir_text_eq(edge->from, from) &&
        ir_text_eq(edge->kind, kind)) {
      count++;
    }
  }
  return count;
}

static const char *ir_graph_node_type(const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  if (!node) return NULL;
  if (node->type && node->type[0]) return node->type;
  const ZProgramGraphNode *declared = ir_graph_ordered_node(graph, node->id, "declaredType", 0);
  if (declared && declared->type && declared->type[0]) return declared->type;
  const ZProgramGraphNode *ret = ir_graph_ordered_node(graph, node->id, "returnType", 0);
  if (ret && ret->type && ret->type[0]) return ret->type;
  return NULL;
}

static const ZProgramGraphNode *ir_graph_module_for_function(const ZProgramGraph *graph, const ZProgramGraphNode *function) {
  for (size_t i = 0; graph && function && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE ||
        !ir_text_eq(edge->to, function->id) ||
        !ir_text_eq(edge->kind, "function")) {
      continue;
    }
    const ZProgramGraphNode *module = ir_graph_find_node(graph, edge->from);
    if (module && module->kind == Z_PROGRAM_GRAPH_NODE_MODULE) return module;
  }
  return NULL;
}

static char *ir_graph_stable_id_for_function(const ZProgramGraph *graph, const ZProgramGraphNode *function) {
  const ZProgramGraphNode *module = ir_graph_module_for_function(graph, function);
  ZBuf stable_id;
  zbuf_init(&stable_id);
  zbuf_append(&stable_id, module && module->name && module->name[0] ? module->name : "main");
  zbuf_append_char(&stable_id, '.');
  zbuf_append(&stable_id, function && function->name ? function->name : "");
  return stable_id.data;
}

static bool ir_graph_function_is_hosted_world_main(const ZProgramGraph *graph, const ZProgramGraphNode *function) {
  const ZProgramGraphNode *param = ir_graph_ordered_node(graph, function ? function->id : NULL, "param", 0);
  const char *return_type = ir_graph_node_type(graph, function);
  return function && function->is_public &&
         ir_text_eq(function->name, "main") &&
         ir_graph_edge_count(graph, function->id, "param") == 1 &&
         param && ir_text_eq(param->type, "World") &&
         ir_text_eq(return_type, "Void");
}

static bool ir_graph_expr_qualified_name_into(const ZProgramGraph *graph, const ZProgramGraphNode *node, ZBuf *out) {
  if (!node || !out) return false;
  if (node->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER) {
    zbuf_append(out, node->name ? node->name : "");
    return true;
  }
  if (node->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS) {
    const ZProgramGraphNode *left = ir_graph_ordered_node(graph, node->id, "left", 0);
    if (!ir_graph_expr_qualified_name_into(graph, left, out)) return false;
    zbuf_append_char(out, '.');
    zbuf_append(out, node->name ? node->name : "");
    return true;
  }
  if (node->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL) {
    const ZProgramGraphNode *left = ir_graph_ordered_node(graph, node->id, "left", 0);
    if (!ir_graph_expr_qualified_name_into(graph, left, out)) return false;
    if (!left || left->kind != Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS || !ir_text_eq(left->name, node->name)) {
      zbuf_append_char(out, '.');
      zbuf_append(out, node->name ? node->name : "");
    }
    return true;
  }
  if (node->kind == Z_PROGRAM_GRAPH_NODE_CALL) {
    zbuf_append(out, node->name ? node->name : "");
    return true;
  }
  return false;
}

static char *ir_graph_expr_qualified_name(const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  ZBuf name;
  zbuf_init(&name);
  if (!ir_graph_expr_qualified_name_into(graph, node, &name)) {
    zbuf_free(&name);
    return NULL;
  }
  return name.data;
}

static bool ir_graph_requires_source_std_ast_mir(const ZProgramGraph *graph) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_CALL && node->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL) continue;
    char *qualified = ir_graph_expr_qualified_name(graph, node);
    bool needs_fallback = qualified &&
                          strncmp(qualified, "std.path.", strlen("std.path.")) == 0 &&
                          z_std_source_target_for_public_call(qualified) != NULL;
    free(qualified);
    if (needs_fallback) return true;
  }
  return false;
}

static char *ir_graph_dirname_copy(const char *path) {
  if (!path || !path[0]) return z_strdup(".");
  const char *slash = strrchr(path, '/');
  if (!slash) return z_strdup(".");
  if (slash == path) return z_strdup("/");
  return z_strndup(path, (size_t)(slash - path));
}

static char *ir_graph_join_path(const char *dir, const char *path) {
  if (!path || !path[0]) return z_strdup("");
  if (path[0] == '/') return z_strdup(path);
  if (!dir || !dir[0] || ir_text_eq(dir, ".")) return z_strdup(path);
  ZBuf joined;
  zbuf_init(&joined);
  zbuf_append(&joined, dir);
  if (joined.len > 0 && joined.data[joined.len - 1] != '/') zbuf_append_char(&joined, '/');
  zbuf_append(&joined, path);
  return joined.data ? joined.data : z_strdup(path);
}

static bool ir_graph_read_c_header_candidate(const char *path, char **out_header, char **out_resolved) {
  if (!path || !path[0] || !out_header || !out_resolved) return false;
  ZDiag diag = {0};
  char *header = z_read_file(path, &diag);
  if (!header) return false;
  *out_header = header;
  *out_resolved = z_strdup(path);
  return true;
}

static bool ir_graph_read_c_header(const ZProgramGraphNode *c_import, char **out_header, char **out_resolved) {
  if (!c_import || !c_import->value || !c_import->value[0] || !out_header || !out_resolved) return false;
  *out_header = NULL;
  *out_resolved = NULL;
  if (ir_graph_read_c_header_candidate(c_import->value, out_header, out_resolved)) return true;
  char *source_dir = ir_graph_dirname_copy(c_import->path);
  char *source_relative = ir_graph_join_path(source_dir, c_import->value);
  if (ir_graph_read_c_header_candidate(source_relative, out_header, out_resolved)) {
    free(source_relative);
    free(source_dir);
    return true;
  }
  free(source_relative);
  char *package_dir = ir_graph_dirname_copy(source_dir);
  char *package_relative = ir_graph_join_path(package_dir, c_import->value);
  bool ok = ir_graph_read_c_header_candidate(package_relative, out_header, out_resolved);
  free(package_relative);
  free(package_dir);
  free(source_dir);
  return ok;
}

static const ZProgramGraphNode *ir_graph_c_import_for_alias(const ZProgramGraph *graph, const char *alias) {
  for (size_t i = 0; graph && alias && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_C_IMPORT && ir_text_eq(node->name, alias)) return node;
  }
  return NULL;
}

static bool ir_graph_find_c_import_function(const ZProgramGraphNode *c_import, const char *symbol, ZCImportFunction *out) {
  if (!c_import || !symbol || !symbol[0] || !out) return false;
  char *header = NULL;
  char *resolved_header = NULL;
  if (!ir_graph_read_c_header(c_import, &header, &resolved_header)) return false;
  ZCImportFunctionVec functions = {0};
  z_c_header_parse_functions(header, &functions);
  free(header);
  for (size_t i = 0; i < functions.len; i++) {
    ZCImportFunction *function = &functions.items[i];
    if (!ir_text_eq(function->name, symbol)) continue;
    *out = *function;
    *function = (ZCImportFunction){0};
    out->import_header = z_strdup(c_import->value);
    out->import_resolved_header = z_strdup(resolved_header ? resolved_header : c_import->value);
    z_c_import_function_vec_free(&functions);
    free(resolved_header);
    return true;
  }
  z_c_import_function_vec_free(&functions);
  free(resolved_header);
  return false;
}

static IrFunction *ir_graph_push_function(IrProgram *ir, const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  ir->functions = ir_grow_tracked_items(ir, ir->functions, ir->function_len, &ir->function_cap, 4, sizeof(IrFunction));
  IrFunction *fun = &ir->functions[ir->function_len++];
  bool hosted_world_main = ir_graph_function_is_hosted_world_main(graph, node);
  const char *source_return_type = ir_graph_node_type(graph, node);
  IrTypeKind source_type = ir_type_kind(source_return_type);
  IrTypeKind return_type = hosted_world_main ? IR_TYPE_I32 : (node->fallible ? IR_TYPE_I64 : source_type);
  IrTypeKind return_element_type = source_type == IR_TYPE_BYTE_VIEW ? ir_view_element_type_for_type(source_return_type) : IR_TYPE_UNSUPPORTED;
  if (source_type == IR_TYPE_MAYBE_SCALAR) return_element_type = ir_maybe_scalar_element_type(source_return_type);

  *fun = (IrFunction){
    .name = z_strdup(node->name),
    .stable_id = ir_graph_stable_id_for_function(graph, node),
    .world_param_name = hosted_world_main ? z_strdup("world") : NULL,
    .return_type = return_type,
    .value_return_type = source_type,
    .return_element_type = return_element_type,
    .is_exported = node->export_c || hosted_world_main,
    .raises = hosted_world_main ? false : node->fallible,
    .line = ir_graph_line(node),
    .column = ir_graph_column(node)
  };
  return fun;
}

static bool ir_graph_collect_block_locals(IrProgram *ir, IrFunction *fun, const ZProgramGraph *graph, const ZProgramGraphNode *block);

static bool ir_graph_collect_function_locals(IrProgram *ir, IrFunction *fun, const ZProgramGraph *graph, const ZProgramGraphNode *function) {
  bool hosted_world_main = ir_graph_function_is_hosted_world_main(graph, function);
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = ir_graph_next_edge_by_order(graph, function->id, "param", have_last, last_order);
    if (!edge) break;
    have_last = true;
    last_order = edge->order;
    const ZProgramGraphNode *param = ir_graph_find_node(graph, edge->to);
    if (!param) continue;
    if (hosted_world_main && edge->order == 0 && ir_text_eq(param->type, "World")) continue;
    IrTypeKind type = ir_type_kind(param->type);
    if (!ir_type_is_direct_param_abi(type)) {
      ir_graph_mark_unsupported(ir, param, "typed graph MIR parameter type is unsupported", param->type);
      return false;
    }
    IrTypeKind element_type = type == IR_TYPE_BYTE_VIEW ? ir_view_element_type_for_type(param->type) : IR_TYPE_UNSUPPORTED;
    ir_function_push_local(ir, fun, param->name, type, true, false, false, NULL, element_type, 0, 0, 0, ir_type_name_is_mutable_byte_view(param->type), ir_graph_line(param), ir_graph_column(param));
  }
  const ZProgramGraphNode *body = ir_graph_ordered_node(graph, function->id, "body", 0);
  if (!ir_graph_collect_block_locals(ir, fun, graph, body)) return false;

  size_t offset = 0;
  for (size_t i = 0; i < fun->local_len; i++) {
    IrLocal *local = &fun->locals[i];
    offset = ir_align_to(offset, local->alignment);
    offset += local->byte_size;
    local->frame_offset = (unsigned)offset;
  }
  fun->frame_bytes = ir_align_to(offset, 16);
  ir->direct_stack_bytes += fun->frame_bytes;
  if (fun->frame_bytes > ir->direct_max_frame_bytes) ir->direct_max_frame_bytes = fun->frame_bytes;
  return true;
}

static bool ir_graph_collect_let_local(IrProgram *ir, IrFunction *fun, const ZProgramGraph *graph, const ZProgramGraphNode *stmt) {
  const char *type_text = ir_graph_node_type(graph, stmt);
  IrTypeKind type = ir_type_kind(type_text);
  unsigned array_len = 0;
  IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
  if (ir_parse_fixed_array_type(type_text, &array_len, &element_type)) {
    ir_function_push_local(ir, fun, stmt->name, IR_TYPE_UNSUPPORTED, false, true, false, NULL, element_type, array_len, 0, 0, stmt->is_mutable, ir_graph_line(stmt), ir_graph_column(stmt));
    return true;
  }
  if (!ir_type_is_direct_local(type)) {
    ir_graph_mark_unsupported(ir, stmt, "typed graph MIR local type is unsupported", type_text ? type_text : "inferred unknown");
    return false;
  }
  bool mutable_byte_view = ir_type_name_is_mutable_byte_view(type_text);
  IrTypeKind view_element_type = type == IR_TYPE_BYTE_VIEW ? ir_view_element_type_for_type(type_text) : IR_TYPE_UNSUPPORTED;
  if (type == IR_TYPE_MAYBE_SCALAR) view_element_type = ir_maybe_scalar_element_type(type_text);
  ir_function_push_local(ir, fun, stmt->name, type, false, false, false, NULL, view_element_type, 0, 0, 0, stmt->is_mutable || mutable_byte_view, ir_graph_line(stmt), ir_graph_column(stmt));
  return true;
}

static bool ir_graph_collect_block_locals(IrProgram *ir, IrFunction *fun, const ZProgramGraph *graph, const ZProgramGraphNode *block) {
  if (!block) return true;
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = ir_graph_next_edge_by_order(graph, block->id, "statement", have_last, last_order);
    if (!edge) break;
    have_last = true;
    last_order = edge->order;
    const ZProgramGraphNode *stmt = ir_graph_find_node(graph, edge->to);
    if (!stmt) continue;
    if (stmt->kind == Z_PROGRAM_GRAPH_NODE_LET) {
      if (!ir_graph_collect_let_local(ir, fun, graph, stmt)) return false;
    } else if (stmt->kind == Z_PROGRAM_GRAPH_NODE_IF) {
      if (!ir_graph_collect_block_locals(ir, fun, graph, ir_graph_ordered_node(graph, stmt->id, "then", 0)) ||
          !ir_graph_collect_block_locals(ir, fun, graph, ir_graph_ordered_node(graph, stmt->id, "else", 0))) {
        return false;
      }
    } else if (stmt->kind == Z_PROGRAM_GRAPH_NODE_WHILE) {
      if (!ir_graph_collect_block_locals(ir, fun, graph, ir_graph_ordered_node(graph, stmt->id, "then", 0))) return false;
    }
  }
  return true;
}

static bool ir_graph_lower_expr(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out);

static bool ir_graph_lower_byte_view(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  if (!expr) {
    ir_mark_unsupported(ir, "typed graph MIR byte view expression is missing", 1, 1, "missing expression");
    return false;
  }
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_LITERAL && ir_type_kind(expr->type) == IR_TYPE_BYTE_VIEW) {
    return ir_make_string_literal_value(ir, expr->value ? expr->value : "", ir_graph_line(expr), ir_graph_column(expr), out);
  }
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER) {
    const IrLocal *local = ir_function_find_local(fun, expr->name);
    if (local && local->type == IR_TYPE_BYTE_VIEW) {
      IrValue *value = ir_new_value(ir, IR_VALUE_LOCAL, IR_TYPE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
      value->local_index = local->index;
      value->element_type = local->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : local->element_type;
      *out = value;
      return true;
    }
    if (local && local->type == IR_TYPE_MAYBE_BYTE_VIEW) {
      IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_VALUE, IR_TYPE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
      value->local_index = local->index;
      value->element_type = IR_TYPE_U8;
      *out = value;
      return true;
    }
    if (local && local->is_array && (ir_type_is_value(local->element_type) || local->element_type == IR_TYPE_BOOL)) {
      IrValue *value = ir_new_value(ir, IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
      value->array_index = local->index;
      value->data_len = local->array_len;
      value->element_type = local->element_type;
      *out = value;
      return true;
    }
  }
  IrValue *value = NULL;
  if (!ir_graph_lower_expr(graph, ir, fun, expr, &value)) return false;
  if (value->type != IR_TYPE_BYTE_VIEW) {
    ir_free_value(value);
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR expression is not a byte view", expr->type ? expr->type : "unknown type");
    return false;
  }
  *out = value;
  return true;
}

static bool ir_graph_lower_byte_slice(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  const ZProgramGraphNode *base_node = ir_graph_ordered_node(graph, expr ? expr->id : NULL, "left", 0);
  IrValue *base = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, base_node, &base)) return false;
  IrValue *start = NULL;
  IrValue *end = NULL;
  const ZProgramGraphNode *start_node = ir_graph_ordered_node(graph, expr->id, "arg", 0);
  const ZProgramGraphNode *end_node = ir_graph_ordered_node(graph, expr->id, "arg", 1);
  if (start_node && !ir_graph_lower_expr(graph, ir, fun, start_node, &start)) {
    ir_free_value(base);
    return false;
  }
  if (end_node && !ir_graph_lower_expr(graph, ir, fun, end_node, &end)) {
    ir_free_value(base);
    ir_free_value(start);
    return false;
  }
  if ((start && !ir_type_is_value(start->type)) || (end && !ir_type_is_value(end->type))) {
    ir_free_value(base);
    ir_free_value(start);
    ir_free_value(end);
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR slice bounds must be integer values", "non-integer slice bound");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_SLICE, IR_TYPE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
  value->left = base;
  value->index = start;
  value->right = end;
  value->element_type = base->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : base->element_type;
  *out = value;
  return true;
}

static bool ir_graph_expr_is_byte_view_source(const ZProgramGraph *graph, const IrFunction *fun, const ZProgramGraphNode *expr) {
  if (!expr) return false;
  if (ir_type_kind(ir_graph_node_type(graph, expr)) == IR_TYPE_BYTE_VIEW) return true;
  switch (expr->kind) {
    case Z_PROGRAM_GRAPH_NODE_LITERAL:
      return ir_type_kind(expr->type) == IR_TYPE_BYTE_VIEW;
    case Z_PROGRAM_GRAPH_NODE_IDENTIFIER: {
      const IrLocal *local = ir_function_find_local(fun, expr->name);
      return local && (local->type == IR_TYPE_BYTE_VIEW || local->type == IR_TYPE_MAYBE_BYTE_VIEW ||
                       (local->is_array && (ir_type_is_value(local->element_type) || local->element_type == IR_TYPE_BOOL)));
    }
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS: {
      const ZProgramGraphNode *left = ir_graph_ordered_node(graph, expr->id, "left", 0);
      if (!left || left->kind != Z_PROGRAM_GRAPH_NODE_IDENTIFIER || !ir_text_eq(expr->name, "value")) return false;
      const IrLocal *local = ir_function_find_local(fun, left->name);
      return local && local->type == IR_TYPE_MAYBE_BYTE_VIEW;
    }
    case Z_PROGRAM_GRAPH_NODE_SLICE:
      return true;
    default:
      return false;
  }
}

static bool ir_graph_lower_expr_for_type(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrTypeKind target_type, bool allow_i32_default, int line, int column, IrValue **out) {
  *out = NULL;
  if (!expr && allow_i32_default && target_type == IR_TYPE_I32) {
    *out = ir_new_integer_literal_value(ir, IR_TYPE_I32, 0, line, column);
    return true;
  }
  if (expr && expr->kind == Z_PROGRAM_GRAPH_NODE_LITERAL && ir_text_eq(expr->value, "null")) {
    if (target_type == IR_TYPE_MAYBE_BYTE_VIEW) {
      *out = ir_new_maybe_byte_view_literal(ir, false, NULL, line, column);
      return true;
    }
    if (target_type == IR_TYPE_MAYBE_SCALAR) {
      *out = ir_new_maybe_scalar_literal(ir, false, IR_TYPE_UNSUPPORTED, 0, line, column);
      return true;
    }
  }
  if (expr && expr->kind == Z_PROGRAM_GRAPH_NODE_LITERAL && ir_type_is_value(target_type) && ir_type_kind(expr->type) == IR_TYPE_UNSUPPORTED) {
    unsigned long long parsed = 0;
    if (!ir_parse_integer_literal(expr->value ? expr->value : "", &parsed)) {
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR integer literal is malformed", expr->value ? expr->value : "");
      return false;
    }
    *out = ir_new_integer_literal_value(ir, target_type, parsed, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (target_type == IR_TYPE_BYTE_VIEW) return ir_graph_lower_byte_view(graph, ir, fun, expr, out);
  if (target_type == IR_TYPE_MAYBE_BYTE_VIEW && ir_graph_expr_is_byte_view_source(graph, fun, expr)) {
    IrValue *view = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, expr, &view)) return false;
    *out = ir_new_maybe_byte_view_literal(ir, true, view, line, column);
    return true;
  }
  return ir_graph_lower_expr(graph, ir, fun, expr, out);
}

static bool ir_graph_lower_literal(const ZProgramGraphNode *expr, IrProgram *ir, IrValue **out) {
  const char *value_text = expr && expr->value ? expr->value : "";
  if (ir_text_eq(value_text, "true") || ir_text_eq(value_text, "false")) {
    *out = ir_new_bool_literal(ir, ir_text_eq(value_text, "true"), ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (ir_text_eq(value_text, "null")) {
    IrTypeKind type = ir_type_kind(expr ? expr->type : NULL);
    if (type == IR_TYPE_MAYBE_BYTE_VIEW) {
      *out = ir_new_maybe_byte_view_literal(ir, false, NULL, ir_graph_line(expr), ir_graph_column(expr));
      return true;
    }
    if (type == IR_TYPE_MAYBE_SCALAR) {
      *out = ir_new_maybe_scalar_literal(ir, false, IR_TYPE_UNSUPPORTED, 0, ir_graph_line(expr), ir_graph_column(expr));
      return true;
    }
  }
  IrTypeKind type = ir_type_kind(expr ? expr->type : NULL);
  if (!ir_type_is_value(type)) type = ir_integer_literal_suffix_type(value_text);
  if (!ir_type_is_value(type) && ir_parse_integer_literal(value_text, NULL)) type = IR_TYPE_I32;
  if (type == IR_TYPE_BYTE_VIEW) return ir_make_string_literal_value(ir, value_text, ir_graph_line(expr), ir_graph_column(expr), out);
  if (!ir_type_is_value(type)) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR literal type is unsupported", expr && expr->type ? expr->type : "unknown literal type");
    return false;
  }
  unsigned long long parsed = 0;
  if (!ir_parse_integer_literal(value_text, &parsed)) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR integer literal is malformed", value_text);
    return false;
  }
  *out = ir_new_integer_literal_value(ir, type, parsed, ir_graph_line(expr), ir_graph_column(expr));
  return true;
}

static bool ir_graph_lower_array_initializer(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const IrLocal *local, const ZProgramGraphNode *expr, IrInstr **out_items, size_t *out_len, size_t *out_cap, int line, int column) {
  if (!local || !local->is_array || !expr || expr->kind != Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL) {
    ir_mark_unsupported(ir, "typed graph MIR fixed array locals require array literal initialization", line, column, local ? local->name : "array local");
    return false;
  }
  if (ir_text_eq(expr->value, "repeat")) {
    if (ir_graph_edge_count(graph, expr->id, "arg") != 2) {
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR fixed array repeat literal requires value and count", local->name);
      return false;
    }
    const ZProgramGraphNode *value_node = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    for (size_t i = 0; i < local->array_len; i++) {
      IrValue *index = ir_new_index_literal(ir, (unsigned)i, ir_graph_line(value_node), ir_graph_column(value_node));
      IrValue *value = NULL;
      if (!ir_graph_lower_expr_for_type(graph, ir, fun, value_node, local->element_type, false, ir_graph_line(value_node), ir_graph_column(value_node), &value)) {
        ir_free_value(index);
        return false;
      }
      if (value->type != local->element_type) {
        ir_free_value(index);
        ir_free_value(value);
        ir_graph_mark_unsupported(ir, value_node, "typed graph MIR array repeat element type does not match local type", local->name);
        return false;
      }
      ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_INDEX_STORE, .array_index = local->index, .index = index, .value = value, .line = ir_graph_line(value_node), .column = ir_graph_column(value_node)});
    }
    return true;
  }
  size_t arg_count = ir_graph_edge_count(graph, expr->id, "arg");
  if (arg_count != local->array_len) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR fixed array literal length must match local type", local->name);
    return false;
  }
  for (size_t i = 0; i < arg_count; i++) {
    const ZProgramGraphNode *value_node = ir_graph_ordered_node(graph, expr->id, "arg", i);
    IrValue *index = ir_new_index_literal(ir, (unsigned)i, ir_graph_line(value_node), ir_graph_column(value_node));
    IrValue *value = NULL;
    if (!ir_graph_lower_expr_for_type(graph, ir, fun, value_node, local->element_type, false, ir_graph_line(value_node), ir_graph_column(value_node), &value)) {
      ir_free_value(index);
      return false;
    }
    if (value->type != local->element_type) {
      ir_free_value(index);
      ir_free_value(value);
      ir_graph_mark_unsupported(ir, value_node, "typed graph MIR array literal element type does not match local type", local->name);
      return false;
    }
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_INDEX_STORE, .array_index = local->index, .index = index, .value = value, .line = ir_graph_line(value_node), .column = ir_graph_column(value_node)});
  }
  return true;
}

static bool ir_graph_lower_identifier(const ZProgramGraphNode *expr, IrProgram *ir, const IrFunction *fun, IrValue **out) {
  const IrLocal *local = ir_function_find_local(fun, expr ? expr->name : NULL);
  if (!local) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR identifier is not a local", expr && expr->name ? expr->name : "unknown identifier");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_LOCAL, local->type, ir_graph_line(expr), ir_graph_column(expr));
  value->local_index = local->index;
  if (local->type == IR_TYPE_BYTE_VIEW) value->element_type = local->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : local->element_type;
  *out = value;
  return true;
}

static bool ir_graph_lower_field_access(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  const ZProgramGraphNode *left = ir_graph_ordered_node(graph, expr ? expr->id : NULL, "left", 0);
  if (left && left->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER && ir_text_eq(expr->name, "has")) {
    const IrLocal *local = ir_function_find_local(fun, left->name);
    if (local && (local->type == IR_TYPE_MAYBE_BYTE_VIEW || local->type == IR_TYPE_MAYBE_SCALAR)) {
      IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_HAS, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
      value->local_index = local->index;
      *out = value;
      return true;
    }
  }
  if (left && left->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER && ir_text_eq(expr->name, "value")) {
    const IrLocal *local = ir_function_find_local(fun, left->name);
    if (local && local->type == IR_TYPE_MAYBE_BYTE_VIEW) {
      IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_VALUE, IR_TYPE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
      value->local_index = local->index;
      value->element_type = IR_TYPE_U8;
      *out = value;
      return true;
    }
    if (local && local->type == IR_TYPE_MAYBE_SCALAR) {
      IrTypeKind value_type = ir_type_kind(ir_graph_node_type(graph, expr));
      if (!(value_type == IR_TYPE_BOOL || ir_type_is_value(value_type))) value_type = local->element_type;
      if (!(value_type == IR_TYPE_BOOL || ir_type_is_value(value_type))) value_type = IR_TYPE_I32;
      IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_VALUE, value_type, ir_graph_line(expr), ir_graph_column(expr));
      value->local_index = local->index;
      *out = value;
      return true;
    }
  }
  ir_graph_mark_unsupported(ir, expr, "typed graph MIR field access is unsupported", expr && expr->name ? expr->name : "unknown field");
  return false;
}

static bool ir_graph_lower_index_access(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  const ZProgramGraphNode *left = ir_graph_ordered_node(graph, expr ? expr->id : NULL, "left", 0);
  const ZProgramGraphNode *right = ir_graph_ordered_node(graph, expr ? expr->id : NULL, "right", 1);
  if (!left || !right) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR index access requires base and index", "missing index edge");
    return false;
  }
  if (left->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER) {
    const IrLocal *local = ir_function_find_local(fun, left->name);
    if (local && local->is_array) {
      IrValue *index = NULL;
      if (!ir_graph_lower_expr(graph, ir, fun, right, &index)) return false;
      if (!ir_type_is_value(index->type)) {
        ir_free_value(index);
        ir_graph_mark_unsupported(ir, right, "typed graph MIR fixed-array index must be an integer value", "non-integer index");
        return false;
      }
      IrValue *value = ir_new_value(ir, IR_VALUE_INDEX_LOAD, local->element_type, ir_graph_line(expr), ir_graph_column(expr));
      value->array_index = local->index;
      value->index = index;
      *out = value;
      return true;
    }
  }
  IrValue *view = NULL;
  IrValue *index = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, left, &view) ||
      !ir_graph_lower_expr(graph, ir, fun, right, &index)) {
    ir_free_value(view);
    ir_free_value(index);
    return false;
  }
  if (!ir_type_is_value(index->type)) {
    ir_free_value(view);
    ir_free_value(index);
    ir_graph_mark_unsupported(ir, right, "typed graph MIR byte-view index must be an integer value", "non-integer index");
    return false;
  }
  IrTypeKind element_type = view->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : view->element_type;
  IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_INDEX_LOAD, element_type, ir_graph_line(expr), ir_graph_column(expr));
  value->left = view;
  value->index = index;
  *out = value;
  return true;
}

static bool ir_graph_lower_ordered_arg(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *call, size_t order, IrTypeKind expected, IrValue **out) {
  const ZProgramGraphNode *arg = ir_graph_ordered_node(graph, call ? call->id : NULL, "arg", order);
  return ir_graph_lower_expr_for_type(graph, ir, fun, arg, expected, false, ir_graph_line(arg), ir_graph_column(arg), out);
}

static bool ir_graph_node_is_untyped_literal(const ZProgramGraphNode *node) {
  return node && node->kind == Z_PROGRAM_GRAPH_NODE_LITERAL && ir_type_kind(node->type) == IR_TYPE_UNSUPPORTED;
}

static bool ir_graph_lower_binary_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  const ZProgramGraphNode *left_node = ir_graph_ordered_node(graph, expr->id, "left", 0);
  const ZProgramGraphNode *right_node = ir_graph_ordered_node(graph, expr->id, "right", 1);
  IrCompareOp cmp = IR_CMP_EQ;
  if (ir_compare_op(expr->name, &cmp)) {
    IrValue *left = NULL;
    IrValue *right = NULL;
    bool lower_right_first = ir_graph_node_is_untyped_literal(left_node) && !ir_graph_node_is_untyped_literal(right_node);
    if (lower_right_first) {
      if (!ir_graph_lower_expr(graph, ir, fun, right_node, &right) ||
          !ir_graph_lower_expr_for_type(graph, ir, fun, left_node, right ? right->type : IR_TYPE_UNSUPPORTED, false, ir_graph_line(left_node), ir_graph_column(left_node), &left)) {
        ir_free_value(left);
        ir_free_value(right);
        return false;
      }
    } else if (!ir_graph_lower_expr(graph, ir, fun, left_node, &left) ||
               !ir_graph_lower_expr_for_type(graph, ir, fun, right_node, left ? left->type : IR_TYPE_UNSUPPORTED, false, ir_graph_line(right_node), ir_graph_column(right_node), &right)) {
      ir_free_value(left);
      ir_free_value(right);
      return false;
    }
    if (left->type != right->type && ir_type_is_value(left->type) && ir_type_is_value(right->type)) {
      if (right->kind == IR_VALUE_INT) right->type = left->type;
      else if (left->kind == IR_VALUE_INT) left->type = right->type;
    }
    if (!(left->type == IR_TYPE_BOOL || ir_type_is_value(left->type)) || left->type != right->type) {
      ir_free_value(left);
      ir_free_value(right);
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR comparison operands must have the same primitive type", expr->name);
      return false;
    }
    IrValue *value = ir_new_compare_value(ir, cmp, left, right, ir_graph_line(expr), ir_graph_column(expr));
    *out = value;
    return true;
  }
  IrBinaryOp op = IR_BIN_ADD;
  if (!ir_binary_op(expr->name, &op)) return false;
  IrValue *left = NULL;
  IrValue *right = NULL;
  IrTypeKind type = ir_type_kind(ir_graph_node_type(graph, expr));
  if (op == IR_BIN_AND || op == IR_BIN_OR) type = IR_TYPE_BOOL;
  if (type == IR_TYPE_BOOL || ir_type_is_value(type)) {
    if (!ir_graph_lower_expr_for_type(graph, ir, fun, left_node, type, false, ir_graph_line(left_node), ir_graph_column(left_node), &left) ||
        !ir_graph_lower_expr_for_type(graph, ir, fun, right_node, type, false, ir_graph_line(right_node), ir_graph_column(right_node), &right)) {
      ir_free_value(left);
      ir_free_value(right);
      return false;
    }
  } else {
    if (!ir_graph_lower_expr(graph, ir, fun, left_node, &left) ||
        !ir_graph_lower_expr_for_type(graph, ir, fun, right_node, left ? left->type : IR_TYPE_UNSUPPORTED, false, ir_graph_line(right_node), ir_graph_column(right_node), &right)) {
      ir_free_value(left);
      ir_free_value(right);
      return false;
    }
    type = left->type;
  }
  if (!(type == IR_TYPE_BOOL || ir_type_is_value(type))) {
    ir_free_value(left);
    ir_free_value(right);
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR binary expression type is unsupported", ir_graph_node_type(graph, expr));
    return false;
  }
  *out = ir_new_binary_value(ir, op, type, left, right, ir_graph_line(expr), ir_graph_column(expr));
  return true;
}

static bool ir_graph_lower_named_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, IrValue **out) {
  unsigned callee_index = 0;
  if (!ir_find_function_index(ir, callee_name, &callee_index)) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR call target is unsupported", callee_name ? callee_name : "unknown callee");
    return false;
  }
  IrFunction *callee = &ir->functions[callee_index];
  IrValue *value = ir_new_value(ir, IR_VALUE_CALL, callee->raises ? IR_TYPE_I64 : callee->value_return_type, ir_graph_line(expr), ir_graph_column(expr));
  value->callee_index = callee_index;
  if (callee->raises) value->element_type = callee->value_return_type;
  else if (callee->value_return_type == IR_TYPE_BYTE_VIEW) value->element_type = callee->return_element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : callee->return_element_type;
  else if (callee->value_return_type == IR_TYPE_MAYBE_SCALAR) value->element_type = callee->return_element_type;
  else value->element_type = callee->value_return_type;
  size_t arg_index = 0;
  for (size_t local_index = 0; local_index < callee->local_len && arg_index < callee->param_count; local_index++) {
    const IrLocal *param = &callee->locals[local_index];
    if (!param->is_param) continue;
    IrValue *arg = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, arg_index, param->type, &arg)) {
      ir_free_value(value);
      return false;
    }
    ir_value_push_arg(ir, value, arg);
    arg_index++;
  }
  if (arg_index != ir_graph_edge_count(graph, expr->id, "arg")) {
    ir_free_value(value);
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR call arity does not match target", callee_name ? callee_name : "unknown callee");
    return false;
  }
  *out = value;
  return true;
}

static bool ir_graph_lower_c_import_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *qualified_name, bool *handled, IrValue **out) {
  if (handled) *handled = false;
  if (!graph || !ir || !expr || !qualified_name || !out) return true;
  const char *dot = strchr(qualified_name, '.');
  if (!dot || dot == qualified_name || !dot[1]) return true;
  char *alias = z_strndup(qualified_name, (size_t)(dot - qualified_name));
  const char *symbol = dot + 1;
  if (ir_active_local_has(ir, alias)) {
    free(alias);
    return true;
  }
  const ZProgramGraphNode *c_import = ir_graph_c_import_for_alias(graph, alias);
  free(alias);
  if (!c_import) return true;
  if (handled) *handled = true;
  ZCImportFunction function = {0};
  if (!ir_graph_find_c_import_function(c_import, symbol, &function)) {
    ir_graph_mark_unsupported(ir, expr, "direct backend extern c symbol is missing from imported header", symbol);
    return false;
  }
  if (function.old_style_params) {
    ir_graph_mark_unsupported(ir, expr, "direct backend extern c function uses an old-style empty C parameter list", symbol);
    z_c_import_function_free(&function);
    return false;
  }
  IrTypeKind return_type = ir_type_kind(function.return_zero_type);
  if (return_type != IR_TYPE_VOID && !ir_type_is_direct_abi(return_type)) {
    ir_graph_mark_unsupported(ir, expr, "direct backend extern c return type is unsupported", function.return_c_type ? function.return_c_type : "unsupported C return type");
    z_c_import_function_free(&function);
    return false;
  }
  size_t arg_count = ir_graph_edge_count(graph, expr->id, "arg");
  if (arg_count != function.param_len) {
    ir_graph_mark_unsupported(ir, expr, "direct backend extern c call argument count does not match header", symbol);
    z_c_import_function_free(&function);
    return false;
  }
  for (size_t i = 0; i < function.param_len; i++) {
    IrTypeKind param_type = ir_type_kind(function.params[i].zero_type);
    if (!ir_type_is_direct_abi(param_type)) {
      ir_graph_mark_unsupported(ir, expr, "direct backend extern c parameter type is unsupported", function.params[i].c_type ? function.params[i].c_type : "unsupported C parameter type");
      z_c_import_function_free(&function);
      return false;
    }
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_CALL, return_type, ir_graph_line(expr), ir_graph_column(expr));
  value->external_call = true;
  value->external_index = ir_graph_add_external_function(ir, &function);
  value->element_type = return_type;
  for (size_t i = 0; i < function.param_len; i++) {
    IrTypeKind expected = ir_type_kind(function.params[i].zero_type);
    IrValue *arg = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, i, expected, &arg)) {
      ir_free_value(value);
      z_c_import_function_free(&function);
      return false;
    }
    if (arg->type != expected) {
      ir_free_value(arg);
      ir_free_value(value);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", i), "direct backend extern c argument type does not match parameter", function.params[i].zero_type ? function.params[i].zero_type : "Unknown");
      z_c_import_function_free(&function);
      return false;
    }
    ir_value_push_arg(ir, value, arg);
  }
  ir->direct_c_import_call_count++;
  *out = value;
  z_c_import_function_free(&function);
  return true;
}

static void ir_graph_require_runtime_helper(IrProgram *ir) {
  if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
  if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
}

static bool ir_graph_http_status_class_bounds(const char *name, unsigned *lower, unsigned *upper) {
  if (!name || !lower || !upper) return false;
  if (ir_text_eq(name, "std.http.statusIsInformational")) { *lower = 100; *upper = 200; return true; }
  if (ir_text_eq(name, "std.http.statusIsSuccess")) { *lower = 200; *upper = 300; return true; }
  if (ir_text_eq(name, "std.http.statusIsRedirect")) { *lower = 300; *upper = 400; return true; }
  if (ir_text_eq(name, "std.http.statusIsClientError")) { *lower = 400; *upper = 500; return true; }
  if (ir_text_eq(name, "std.http.statusIsServerError")) { *lower = 500; *upper = 600; return true; }
  return false;
}

static int ir_graph_json_error_code(const char *name) {
  if (!name) return -1;
  if (ir_text_eq(name, "std.json.errorNone")) return 0;
  if (ir_text_eq(name, "std.json.errorInvalid")) return 1;
  if (ir_text_eq(name, "std.json.errorTrailing")) return 2;
  return -1;
}

static bool ir_graph_lower_http_std_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  unsigned status_lower = 0;
  unsigned status_upper = 0;
  if (ir_graph_http_status_class_bounds(callee_name, &status_lower, &status_upper) && arg_count == 1) {
    IrValue *status = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_U16, &status)) return false;
    if (!status || status->type != IR_TYPE_U16) {
      ir_free_value(status);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 0), "typed graph MIR std.http status predicate expects a u16 status", "non-u16 status");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_HTTP_STATUS_CLASS, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->left = status;
    value->int_value = status_lower;
    value->data_len = status_upper;
    *out = value;
    return true;
  }
  if ((ir_text_eq(callee_name, "std.http.requestMethodName") || ir_text_eq(callee_name, "std.http.requestPath")) && arg_count == 1) {
    IrValue *request = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &request)) return false;
    IrValue *value = ir_new_value(ir, ir_text_eq(callee_name, "std.http.requestMethodName") ? IR_VALUE_HTTP_REQUEST_METHOD_NAME : IR_VALUE_HTTP_REQUEST_PATH, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = request;
    ir_graph_require_runtime_helper(ir);
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.http.writeJsonResponse") && arg_count == 3) {
    IrValue *buffer = NULL;
    IrValue *status = NULL;
    IrValue *body = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &buffer) ||
        !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_U16, &status) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 2), &body)) {
      ir_free_value(buffer);
      ir_free_value(status);
      ir_free_value(body);
      return false;
    }
    if (status->type != IR_TYPE_U16) {
      ir_free_value(buffer);
      ir_free_value(status);
      ir_free_value(body);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), "typed graph MIR std.http.writeJsonResponse status must be u16", "non-u16 status");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_HTTP_WRITE_JSON_RESPONSE, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = buffer;
    value->index = status;
    value->right = body;
    ir_graph_require_runtime_helper(ir);
    *out = value;
    return true;
  }
  *handled = false;
  return true;
}

static bool ir_graph_lower_std_str_arg(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, size_t order, IrTypeKind expected, IrValue **out) {
  const ZProgramGraphNode *arg = ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", order);
  if (expected == IR_TYPE_BYTE_VIEW) return ir_graph_lower_byte_view(graph, ir, fun, arg, out);
  return ir_graph_lower_expr_for_type(graph, ir, fun, arg, expected, false, ir_graph_line(arg), ir_graph_column(arg), out);
}

static bool ir_graph_make_std_str_runtime_value(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrStrOp op, IrTypeKind return_type, const IrTypeKind *arg_types, size_t arg_count, IrValue **out) {
  IrValue *value = ir_new_value(ir, IR_VALUE_STR_RUNTIME, return_type, ir_graph_line(expr), ir_graph_column(expr));
  value->int_value = (unsigned long long)op;
  if (return_type == IR_TYPE_BYTE_VIEW || return_type == IR_TYPE_MAYBE_BYTE_VIEW) value->element_type = IR_TYPE_U8;
  for (size_t i = 0; i < arg_count; i++) {
    IrValue *arg = NULL;
    if (!ir_graph_lower_std_str_arg(graph, ir, fun, expr, i, arg_types[i], &arg)) {
      ir_free_value(value);
      return false;
    }
    if (arg_types[i] == IR_TYPE_BYTE_VIEW) {
      if (!arg || arg->type != IR_TYPE_BYTE_VIEW) {
        ir_free_value(arg);
        ir_free_value(value);
        ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", i), "typed graph MIR std.str argument must be a byte view", "non-byte-view argument");
        return false;
      }
    } else if (arg && arg->type != arg_types[i]) {
      ir_free_value(arg);
      ir_free_value(value);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", i), "typed graph MIR std.str argument type does not match helper", ir_graph_node_type(graph, ir_graph_ordered_node(graph, expr->id, "arg", i)));
      return false;
    }
    ir_value_push_arg(ir, value, arg);
  }
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_str_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  const IrTypeKind one_view[] = {IR_TYPE_BYTE_VIEW};
  const IrTypeKind two_views[] = {IR_TYPE_BYTE_VIEW, IR_TYPE_BYTE_VIEW};
  const IrTypeKind view_byte[] = {IR_TYPE_BYTE_VIEW, IR_TYPE_U8};
  const IrTypeKind three_views[] = {IR_TYPE_BYTE_VIEW, IR_TYPE_BYTE_VIEW, IR_TYPE_BYTE_VIEW};
  const IrTypeKind view_view_count[] = {IR_TYPE_BYTE_VIEW, IR_TYPE_BYTE_VIEW, IR_TYPE_USIZE};
  if (arg_count == 2 && ir_text_eq(callee_name, "std.str.reverse")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_REVERSE, IR_TYPE_MAYBE_BYTE_VIEW, two_views, 2, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.str.copy")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_COPY, IR_TYPE_MAYBE_BYTE_VIEW, two_views, 2, out);
  if (arg_count == 3 && ir_text_eq(callee_name, "std.str.concat")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_CONCAT, IR_TYPE_MAYBE_BYTE_VIEW, three_views, 3, out);
  if (arg_count == 3 && ir_text_eq(callee_name, "std.str.repeat")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_REPEAT, IR_TYPE_MAYBE_BYTE_VIEW, view_view_count, 3, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.str.toLowerAscii")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_TO_LOWER_ASCII, IR_TYPE_MAYBE_BYTE_VIEW, two_views, 2, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.str.toUpperAscii")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_TO_UPPER_ASCII, IR_TYPE_MAYBE_BYTE_VIEW, two_views, 2, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.str.trimAscii")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_TRIM_ASCII, IR_TYPE_BYTE_VIEW, one_view, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.str.trimStartAscii")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_TRIM_START_ASCII, IR_TYPE_BYTE_VIEW, one_view, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.str.trimEndAscii")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_TRIM_END_ASCII, IR_TYPE_BYTE_VIEW, one_view, 1, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.str.countByte")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_COUNT_BYTE, IR_TYPE_USIZE, view_byte, 2, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.str.startsWith")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_STARTS_WITH, IR_TYPE_BOOL, two_views, 2, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.str.endsWith")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_ENDS_WITH, IR_TYPE_BOOL, two_views, 2, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.str.contains")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_CONTAINS, IR_TYPE_BOOL, two_views, 2, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.str.count")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_COUNT, IR_TYPE_USIZE, two_views, 2, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.str.indexOf")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_INDEX_OF, IR_TYPE_USIZE, two_views, 2, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.str.lastIndexOf")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_LAST_INDEX_OF, IR_TYPE_USIZE, two_views, 2, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.str.eqlIgnoreAsciiCase")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_EQL_IGNORE_ASCII_CASE, IR_TYPE_BOOL, two_views, 2, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.str.wordCountAscii")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_WORD_COUNT_ASCII, IR_TYPE_USIZE, one_view, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.path.basename")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_PATH_BASENAME, IR_TYPE_BYTE_VIEW, one_view, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.path.dirname")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_PATH_DIRNAME, IR_TYPE_BYTE_VIEW, one_view, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.path.extension")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_PATH_EXTENSION, IR_TYPE_BYTE_VIEW, one_view, 1, out);
  *handled = false;
  return true;
}

static bool ir_graph_make_std_ascii_runtime_value(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrAsciiOp op, IrTypeKind return_type, IrValue **out) {
  IrValue *arg = NULL;
  if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_U8, &arg)) return false;
  if (!arg || arg->type != IR_TYPE_U8) {
    ir_free_value(arg);
    ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 0), "typed graph MIR std.ascii helper argument must be u8", "non-u8 argument");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_ASCII_RUNTIME, return_type, ir_graph_line(expr), ir_graph_column(expr));
  value->int_value = (unsigned long long)op;
  if (return_type == IR_TYPE_MAYBE_SCALAR) value->element_type = IR_TYPE_U8;
  ir_value_push_arg(ir, value, arg);
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_ascii_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  if (arg_count == 1 && ir_text_eq(callee_name, "std.ascii.isDigit")) return ir_graph_make_std_ascii_runtime_value(graph, ir, fun, expr, IR_ASCII_OP_IS_DIGIT, IR_TYPE_BOOL, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.ascii.isLower")) return ir_graph_make_std_ascii_runtime_value(graph, ir, fun, expr, IR_ASCII_OP_IS_LOWER, IR_TYPE_BOOL, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.ascii.isUpper")) return ir_graph_make_std_ascii_runtime_value(graph, ir, fun, expr, IR_ASCII_OP_IS_UPPER, IR_TYPE_BOOL, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.ascii.isAlpha")) return ir_graph_make_std_ascii_runtime_value(graph, ir, fun, expr, IR_ASCII_OP_IS_ALPHA, IR_TYPE_BOOL, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.ascii.isAlnum")) return ir_graph_make_std_ascii_runtime_value(graph, ir, fun, expr, IR_ASCII_OP_IS_ALNUM, IR_TYPE_BOOL, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.ascii.isWhitespace")) return ir_graph_make_std_ascii_runtime_value(graph, ir, fun, expr, IR_ASCII_OP_IS_WHITESPACE, IR_TYPE_BOOL, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.ascii.isHexDigit")) return ir_graph_make_std_ascii_runtime_value(graph, ir, fun, expr, IR_ASCII_OP_IS_HEX_DIGIT, IR_TYPE_BOOL, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.ascii.toLower")) return ir_graph_make_std_ascii_runtime_value(graph, ir, fun, expr, IR_ASCII_OP_TO_LOWER, IR_TYPE_U8, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.ascii.toUpper")) return ir_graph_make_std_ascii_runtime_value(graph, ir, fun, expr, IR_ASCII_OP_TO_UPPER, IR_TYPE_U8, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.ascii.digitValue")) return ir_graph_make_std_ascii_runtime_value(graph, ir, fun, expr, IR_ASCII_OP_DIGIT_VALUE, IR_TYPE_MAYBE_SCALAR, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.ascii.hexValue")) return ir_graph_make_std_ascii_runtime_value(graph, ir, fun, expr, IR_ASCII_OP_HEX_VALUE, IR_TYPE_MAYBE_SCALAR, out);
  *handled = false;
  return true;
}

static bool ir_graph_make_std_text_runtime_value(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrTextOp op, IrTypeKind return_type, IrValue **out) {
  IrValue *arg = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 0), &arg)) return false;
  if (!arg || arg->type != IR_TYPE_BYTE_VIEW) {
    ir_free_value(arg);
    ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 0), "typed graph MIR std.text helper argument must be a byte view", "non-byte-view argument");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_TEXT_RUNTIME, return_type, ir_graph_line(expr), ir_graph_column(expr));
  value->int_value = (unsigned long long)op;
  if (return_type == IR_TYPE_MAYBE_SCALAR) value->element_type = IR_TYPE_USIZE;
  ir_value_push_arg(ir, value, arg);
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_text_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  if (arg_count == 1 && ir_text_eq(callee_name, "std.text.isAscii")) return ir_graph_make_std_text_runtime_value(graph, ir, fun, expr, IR_TEXT_OP_IS_ASCII, IR_TYPE_BOOL, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.text.utf8Valid")) return ir_graph_make_std_text_runtime_value(graph, ir, fun, expr, IR_TEXT_OP_UTF8_VALID, IR_TYPE_BOOL, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.codec.utf8Valid")) return ir_graph_make_std_text_runtime_value(graph, ir, fun, expr, IR_TEXT_OP_UTF8_VALID, IR_TYPE_BOOL, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.text.utf8Len")) return ir_graph_make_std_text_runtime_value(graph, ir, fun, expr, IR_TEXT_OP_UTF8_LEN, IR_TYPE_MAYBE_SCALAR, out);
  *handled = false;
  return true;
}

static bool ir_graph_make_std_parse_runtime_value(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrParseOp op, IrTypeKind return_type, IrTypeKind element_type, size_t expected_args, IrValue **out) {
  if (expected_args < 1 || expected_args > 2) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.parse helper has unsupported arity", "wrong std.parse arity");
    return false;
  }
  IrValue *input = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 0), &input)) return false;
  IrValue *value = ir_new_value(ir, IR_VALUE_PARSE_RUNTIME, return_type, ir_graph_line(expr), ir_graph_column(expr));
  value->int_value = (unsigned long long)op;
  if (return_type == IR_TYPE_MAYBE_SCALAR) value->element_type = element_type;
  ir_value_push_arg(ir, value, input);
  if (expected_args == 2) {
    IrValue *byte = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_U8, &byte)) {
      ir_free_value(value);
      return false;
    }
    if (!byte || byte->type != IR_TYPE_U8) {
      ir_free_value(byte);
      ir_free_value(value);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 1), "typed graph MIR std.parse byte argument must be u8", "non-u8 argument");
      return false;
    }
    ir_value_push_arg(ir, value, byte);
  }
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_parse_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  if (arg_count == 1 && ir_text_eq(callee_name, "std.parse.isAsciiDigit")) return ir_graph_make_std_parse_runtime_value(graph, ir, fun, expr, IR_PARSE_OP_IS_ASCII_DIGIT, IR_TYPE_BOOL, IR_TYPE_UNSUPPORTED, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.parse.isAsciiAlpha")) return ir_graph_make_std_parse_runtime_value(graph, ir, fun, expr, IR_PARSE_OP_IS_ASCII_ALPHA, IR_TYPE_BOOL, IR_TYPE_UNSUPPORTED, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.parse.isIdentifierStart")) return ir_graph_make_std_parse_runtime_value(graph, ir, fun, expr, IR_PARSE_OP_IS_IDENTIFIER_START, IR_TYPE_BOOL, IR_TYPE_UNSUPPORTED, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.parse.isWhitespace")) return ir_graph_make_std_parse_runtime_value(graph, ir, fun, expr, IR_PARSE_OP_IS_WHITESPACE, IR_TYPE_BOOL, IR_TYPE_UNSUPPORTED, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.parse.scanDigits")) return ir_graph_make_std_parse_runtime_value(graph, ir, fun, expr, IR_PARSE_OP_SCAN_DIGITS, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.parse.scanIdentifier")) return ir_graph_make_std_parse_runtime_value(graph, ir, fun, expr, IR_PARSE_OP_SCAN_IDENTIFIER, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, 1, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.parse.scanUntilByte")) return ir_graph_make_std_parse_runtime_value(graph, ir, fun, expr, IR_PARSE_OP_SCAN_UNTIL_BYTE, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, 2, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.parse.scanWhitespace")) return ir_graph_make_std_parse_runtime_value(graph, ir, fun, expr, IR_PARSE_OP_SCAN_WHITESPACE, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.parse.tokenAscii")) {
    const IrTypeKind one_view[] = {IR_TYPE_BYTE_VIEW};
    return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_PARSE_TOKEN_ASCII, IR_TYPE_BYTE_VIEW, one_view, 1, out);
  }
  if (arg_count == 1 && ir_text_eq(callee_name, "std.parse.parseBool")) return ir_graph_make_std_parse_runtime_value(graph, ir, fun, expr, IR_PARSE_OP_PARSE_BOOL, IR_TYPE_MAYBE_SCALAR, IR_TYPE_BOOL, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.parse.parseU8")) return ir_graph_make_std_parse_runtime_value(graph, ir, fun, expr, IR_PARSE_OP_PARSE_U8, IR_TYPE_MAYBE_SCALAR, IR_TYPE_U8, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.parse.parseU16")) return ir_graph_make_std_parse_runtime_value(graph, ir, fun, expr, IR_PARSE_OP_PARSE_U16, IR_TYPE_MAYBE_SCALAR, IR_TYPE_U16, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.parse.parseUsize")) return ir_graph_make_std_parse_runtime_value(graph, ir, fun, expr, IR_PARSE_OP_PARSE_USIZE, IR_TYPE_MAYBE_SCALAR, IR_TYPE_USIZE, 1, out);
  *handled = false;
  return true;
}

static bool ir_graph_std_time_duration_scale(const char *callee_name, unsigned long long *out_scale) {
  static const struct { const char *name; unsigned long long scale; } entries[] = {
    {"std.time.ns", 1ull},
    {"std.time.us", 1000ull},
    {"std.time.ms", 1000000ull},
    {"std.time.seconds", 1000000000ull},
    {"std.time.minutes", 60000000000ull},
    {"std.time.hours", 3600000000000ull},
  };
  for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
    if (ir_text_eq(callee_name, entries[i].name)) {
      *out_scale = entries[i].scale;
      return true;
    }
  }
  return false;
}

static bool ir_graph_make_std_time_constructor(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, unsigned long long scale, IrValue **out) {
  IrValue *arg = NULL;
  if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_I64, &arg)) return false;
  if (!arg || !ir_type_is_value(arg->type)) {
    ir_free_value(arg);
    ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 0), "typed graph MIR std.time duration argument must be integer", "non-integer duration");
    return false;
  }
  IrValue *value = ir_new_cast_value(ir, arg, IR_TYPE_I64, ir_graph_line(expr), ir_graph_column(expr));
  if (scale != 1) {
    value = ir_new_binary_value(ir, IR_BIN_MUL, IR_TYPE_I64, value, ir_new_integer_literal_value(ir, IR_TYPE_I64, scale, ir_graph_line(expr), ir_graph_column(expr)), ir_graph_line(expr), ir_graph_column(expr));
  }
  *out = value;
  return true;
}

static bool ir_graph_make_std_time_runtime_value(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrTimeOp op, IrTypeKind return_type, size_t expected_args, IrValue **out) {
  if (ir_graph_edge_count(graph, expr ? expr->id : NULL, "arg") != expected_args) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.time helper has unsupported arity", "wrong std.time arity");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_TIME_RUNTIME, return_type, ir_graph_line(expr), ir_graph_column(expr));
  value->int_value = (unsigned long long)op;
  for (size_t i = 0; i < expected_args; i++) {
    IrValue *arg = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, i, IR_TYPE_I64, &arg)) {
      ir_free_value(value);
      return false;
    }
    if (!arg || !ir_type_is_value(arg->type)) {
      ir_free_value(arg);
      ir_free_value(value);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", i), "typed graph MIR std.time helper argument must be Duration", "non-Duration argument");
      return false;
    }
    ir_value_push_arg(ir, value, ir_new_cast_value(ir, arg, IR_TYPE_I64, ir_graph_line(expr), ir_graph_column(expr)));
  }
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_time_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  unsigned long long scale = 0;
  if (arg_count == 1 && ir_graph_std_time_duration_scale(callee_name, &scale)) {
    return ir_graph_make_std_time_constructor(graph, ir, fun, expr, scale, out);
  }
  if (arg_count == 0 && ir_text_eq(callee_name, "std.time.zero")) {
    *out = ir_new_integer_literal_value(ir, IR_TYPE_I64, 0, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (arg_count == 2 && (ir_text_eq(callee_name, "std.time.add") || ir_text_eq(callee_name, "std.time.sub"))) {
    IrValue *left = NULL;
    IrValue *right = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_I64, &left) ||
        !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_I64, &right)) {
      ir_free_value(left);
      ir_free_value(right);
      return false;
    }
    left = ir_new_cast_value(ir, left, IR_TYPE_I64, ir_graph_line(expr), ir_graph_column(expr));
    right = ir_new_cast_value(ir, right, IR_TYPE_I64, ir_graph_line(expr), ir_graph_column(expr));
    *out = ir_new_binary_value(ir, ir_text_eq(callee_name, "std.time.add") ? IR_BIN_ADD : IR_BIN_SUB, IR_TYPE_I64, left, right, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (arg_count == 1 && ir_text_eq(callee_name, "std.time.asNs")) {
    IrValue *arg = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_I64, &arg)) return false;
    *out = ir_new_cast_value(ir, arg, IR_TYPE_I64, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (arg_count == 1 && ir_text_eq(callee_name, "std.time.asUsFloor")) return ir_graph_make_std_time_runtime_value(graph, ir, fun, expr, IR_TIME_OP_AS_US_FLOOR, IR_TYPE_I64, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.time.asMsFloor")) return ir_graph_make_std_time_runtime_value(graph, ir, fun, expr, IR_TIME_OP_AS_MS_FLOOR, IR_TYPE_I32, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.time.asSecondsFloor")) return ir_graph_make_std_time_runtime_value(graph, ir, fun, expr, IR_TIME_OP_AS_SECONDS_FLOOR, IR_TYPE_I64, 1, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.time.min")) return ir_graph_make_std_time_runtime_value(graph, ir, fun, expr, IR_TIME_OP_MIN, IR_TYPE_I64, 2, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.time.max")) return ir_graph_make_std_time_runtime_value(graph, ir, fun, expr, IR_TIME_OP_MAX, IR_TYPE_I64, 2, out);
  if (arg_count == 3 && ir_text_eq(callee_name, "std.time.clamp")) return ir_graph_make_std_time_runtime_value(graph, ir, fun, expr, IR_TIME_OP_CLAMP, IR_TYPE_I64, 3, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.time.lessThan")) {
    IrValue *left = NULL;
    IrValue *right = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_I64, &left) ||
        !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_I64, &right)) {
      ir_free_value(left);
      ir_free_value(right);
      return false;
    }
    *out = ir_new_compare_value(ir, IR_CMP_LT, ir_new_cast_value(ir, left, IR_TYPE_I64, ir_graph_line(expr), ir_graph_column(expr)), ir_new_cast_value(ir, right, IR_TYPE_I64, ir_graph_line(expr), ir_graph_column(expr)), ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (arg_count == 1 && ir_text_eq(callee_name, "std.time.isZero")) {
    IrValue *arg = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_I64, &arg)) return false;
    *out = ir_new_compare_value(ir, IR_CMP_EQ, ir_new_cast_value(ir, arg, IR_TYPE_I64, ir_graph_line(expr), ir_graph_column(expr)), ir_new_integer_literal_value(ir, IR_TYPE_I64, 0, ir_graph_line(expr), ir_graph_column(expr)), ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (arg_count == 0 && ir_text_eq(callee_name, "std.time.wallSeconds")) {
    *out = ir_new_value(ir, IR_VALUE_TIME_WALL_SECONDS, IR_TYPE_I64, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (arg_count == 0 && ir_text_eq(callee_name, "std.time.monotonic")) {
    *out = ir_new_value(ir, IR_VALUE_TIME_MONOTONIC, IR_TYPE_I64, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  *handled = false;
  return true;
}

static bool ir_graph_std_math_runtime_spec(const char *callee_name, size_t arg_count, IrMathOp *op, IrTypeKind *arg_type, IrTypeKind *return_type, IrTypeKind *return_element_type, size_t *expected_args) {
  if (!callee_name || !op || !arg_type || !return_type || !return_element_type || !expected_args) return false;
  *expected_args = 2;
  *return_element_type = IR_TYPE_UNSUPPORTED;
  if (ir_text_eq(callee_name, "std.math.minI32")) { *op = IR_MATH_OP_MIN_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_I32; }
  else if (ir_text_eq(callee_name, "std.math.maxI32")) { *op = IR_MATH_OP_MAX_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_I32; }
  else if (ir_text_eq(callee_name, "std.math.clampI32")) { *op = IR_MATH_OP_CLAMP_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_I32; *expected_args = 3; }
  else if (ir_text_eq(callee_name, "std.math.minI64")) { *op = IR_MATH_OP_MIN_I64; *arg_type = IR_TYPE_I64; *return_type = IR_TYPE_I64; }
  else if (ir_text_eq(callee_name, "std.math.maxI64")) { *op = IR_MATH_OP_MAX_I64; *arg_type = IR_TYPE_I64; *return_type = IR_TYPE_I64; }
  else if (ir_text_eq(callee_name, "std.math.clampI64")) { *op = IR_MATH_OP_CLAMP_I64; *arg_type = IR_TYPE_I64; *return_type = IR_TYPE_I64; *expected_args = 3; }
  else if (ir_text_eq(callee_name, "std.math.minU32")) { *op = IR_MATH_OP_MIN_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (ir_text_eq(callee_name, "std.math.maxU32")) { *op = IR_MATH_OP_MAX_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (ir_text_eq(callee_name, "std.math.clampU32")) { *op = IR_MATH_OP_CLAMP_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; *expected_args = 3; }
  else if (ir_text_eq(callee_name, "std.math.minU64")) { *op = IR_MATH_OP_MIN_U64; *arg_type = IR_TYPE_U64; *return_type = IR_TYPE_U64; }
  else if (ir_text_eq(callee_name, "std.math.maxU64")) { *op = IR_MATH_OP_MAX_U64; *arg_type = IR_TYPE_U64; *return_type = IR_TYPE_U64; }
  else if (ir_text_eq(callee_name, "std.math.clampU64")) { *op = IR_MATH_OP_CLAMP_U64; *arg_type = IR_TYPE_U64; *return_type = IR_TYPE_U64; *expected_args = 3; }
  else if (ir_text_eq(callee_name, "std.math.minUsize")) { *op = IR_MATH_OP_MIN_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_USIZE; }
  else if (ir_text_eq(callee_name, "std.math.maxUsize")) { *op = IR_MATH_OP_MAX_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_USIZE; }
  else if (ir_text_eq(callee_name, "std.math.clampUsize")) { *op = IR_MATH_OP_CLAMP_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_USIZE; *expected_args = 3; }
  else if (ir_text_eq(callee_name, "std.math.absI32")) { *op = IR_MATH_OP_ABS_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_U32; *expected_args = 1; }
  else if (ir_text_eq(callee_name, "std.math.absI64")) { *op = IR_MATH_OP_ABS_I64; *arg_type = IR_TYPE_I64; *return_type = IR_TYPE_U64; *expected_args = 1; }
  else if (ir_text_eq(callee_name, "std.math.checkedAddU32")) { *op = IR_MATH_OP_CHECKED_ADD_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_U32; }
  else if (ir_text_eq(callee_name, "std.math.checkedSubU32")) { *op = IR_MATH_OP_CHECKED_SUB_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_U32; }
  else if (ir_text_eq(callee_name, "std.math.checkedMulU32")) { *op = IR_MATH_OP_CHECKED_MUL_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_U32; }
  else if (ir_text_eq(callee_name, "std.math.saturatingAddU32")) { *op = IR_MATH_OP_SATURATING_ADD_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (ir_text_eq(callee_name, "std.math.saturatingSubU32")) { *op = IR_MATH_OP_SATURATING_SUB_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (ir_text_eq(callee_name, "std.math.saturatingMulU32")) { *op = IR_MATH_OP_SATURATING_MUL_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (ir_text_eq(callee_name, "std.math.checkedAddI32")) { *op = IR_MATH_OP_CHECKED_ADD_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_I32; }
  else if (ir_text_eq(callee_name, "std.math.checkedSubI32")) { *op = IR_MATH_OP_CHECKED_SUB_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_I32; }
  else if (ir_text_eq(callee_name, "std.math.checkedMulI32")) { *op = IR_MATH_OP_CHECKED_MUL_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_I32; }
  else if (ir_text_eq(callee_name, "std.math.saturatingAddI32")) { *op = IR_MATH_OP_SATURATING_ADD_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_I32; }
  else if (ir_text_eq(callee_name, "std.math.saturatingSubI32")) { *op = IR_MATH_OP_SATURATING_SUB_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_I32; }
  else if (ir_text_eq(callee_name, "std.math.saturatingMulI32")) { *op = IR_MATH_OP_SATURATING_MUL_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_I32; }
  else if (ir_text_eq(callee_name, "std.math.gcdU32")) { *op = IR_MATH_OP_GCD_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (ir_text_eq(callee_name, "std.math.lcmU32")) { *op = IR_MATH_OP_LCM_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (ir_text_eq(callee_name, "std.math.checkedLcmU32")) { *op = IR_MATH_OP_CHECKED_LCM_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_U32; }
  else if (ir_text_eq(callee_name, "std.math.powU32")) { *op = IR_MATH_OP_POW_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (ir_text_eq(callee_name, "std.math.checkedPowU32")) { *op = IR_MATH_OP_CHECKED_POW_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_U32; }
  else if (ir_text_eq(callee_name, "std.math.modPowU32")) { *op = IR_MATH_OP_MOD_POW_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; *expected_args = 3; }
  else if (ir_text_eq(callee_name, "std.math.isPrimeU32")) { *op = IR_MATH_OP_IS_PRIME_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_BOOL; *expected_args = 1; }
  else if (ir_text_eq(callee_name, "std.math.sqrtFloorU32")) { *op = IR_MATH_OP_SQRT_FLOOR_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; *expected_args = 1; }
  else if (ir_text_eq(callee_name, "std.math.factorialU32")) { *op = IR_MATH_OP_FACTORIAL_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_U32; *expected_args = 1; }
  else if (ir_text_eq(callee_name, "std.math.binomialU32")) { *op = IR_MATH_OP_BINOMIAL_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_U32; }
  else if (ir_text_eq(callee_name, "std.math.divisorCountU32")) { *op = IR_MATH_OP_DIVISOR_COUNT_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; *expected_args = 1; }
  else if (ir_text_eq(callee_name, "std.math.properDivisorSumU32")) { *op = IR_MATH_OP_PROPER_DIVISOR_SUM_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; *expected_args = 1; }
  else if (ir_text_eq(callee_name, "std.math.checkedAddUsize")) { *op = IR_MATH_OP_CHECKED_ADD_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_USIZE; }
  else if (ir_text_eq(callee_name, "std.math.checkedSubUsize")) { *op = IR_MATH_OP_CHECKED_SUB_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_USIZE; }
  else if (ir_text_eq(callee_name, "std.math.checkedMulUsize")) { *op = IR_MATH_OP_CHECKED_MUL_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_USIZE; }
  else if (ir_text_eq(callee_name, "std.math.saturatingAddUsize")) { *op = IR_MATH_OP_SATURATING_ADD_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_USIZE; }
  else if (ir_text_eq(callee_name, "std.math.saturatingSubUsize")) { *op = IR_MATH_OP_SATURATING_SUB_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_USIZE; }
  else if (ir_text_eq(callee_name, "std.math.saturatingMulUsize")) { *op = IR_MATH_OP_SATURATING_MUL_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_USIZE; }
  else return false;
  return arg_count == *expected_args;
}

static bool ir_graph_make_std_math_runtime_value(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrMathOp op, IrTypeKind arg_type, IrTypeKind return_type, IrTypeKind return_element_type, size_t expected_args, IrValue **out) {
  if (ir_graph_edge_count(graph, expr ? expr->id : NULL, "arg") != expected_args) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.math helper has unsupported arity", "wrong std.math arity");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_MATH_RUNTIME, return_type, ir_graph_line(expr), ir_graph_column(expr));
  value->int_value = (unsigned long long)op;
  if (return_type == IR_TYPE_MAYBE_SCALAR) value->element_type = return_element_type;
  for (size_t i = 0; i < expected_args; i++) {
    IrValue *arg = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, i, arg_type, &arg)) {
      ir_free_value(value);
      return false;
    }
    if (!arg || arg->type != arg_type) {
      ir_free_value(arg);
      ir_free_value(value);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", i), "typed graph MIR std.math helper argument has wrong type", "wrong std.math argument type");
      return false;
    }
    ir_value_push_arg(ir, value, ir_new_cast_value(ir, arg, IR_TYPE_I64, ir_graph_line(expr), ir_graph_column(expr)));
  }
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_std_search_runtime_spec(const char *callee_name, IrSearchOp *op, IrTypeKind *element_type) {
  if (!callee_name || !op || !element_type) return false;
  if (ir_text_eq(callee_name, "std.search.lowerBoundI32")) { *op = IR_SEARCH_OP_LOWER_BOUND_I32; *element_type = IR_TYPE_I32; return true; }
  if (ir_text_eq(callee_name, "std.search.binaryI32")) { *op = IR_SEARCH_OP_BINARY_I32; *element_type = IR_TYPE_I32; return true; }
  if (ir_text_eq(callee_name, "std.search.lowerBoundU32")) { *op = IR_SEARCH_OP_LOWER_BOUND_U32; *element_type = IR_TYPE_U32; return true; }
  if (ir_text_eq(callee_name, "std.search.binaryU32")) { *op = IR_SEARCH_OP_BINARY_U32; *element_type = IR_TYPE_U32; return true; }
  if (ir_text_eq(callee_name, "std.search.lowerBoundUsize")) { *op = IR_SEARCH_OP_LOWER_BOUND_USIZE; *element_type = IR_TYPE_USIZE; return true; }
  if (ir_text_eq(callee_name, "std.search.binaryUsize")) { *op = IR_SEARCH_OP_BINARY_USIZE; *element_type = IR_TYPE_USIZE; return true; }
  return false;
}

static bool ir_graph_lower_std_search_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = false;
  IrSearchOp op = IR_SEARCH_OP_LOWER_BOUND_I32;
  IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
  if (!ir_graph_std_search_runtime_spec(callee_name, &op, &element_type)) return true;
  *handled = true;
  if (arg_count != 2) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.search helper has unsupported arity", "wrong std.search arity");
    return false;
  }
  const ZProgramGraphNode *items_node = ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 0);
  IrValue *items = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, items_node, &items)) return false;
  IrTypeKind actual_element = items && items->element_type != IR_TYPE_UNSUPPORTED ? items->element_type : IR_TYPE_U8;
  if (!items || items->type != IR_TYPE_BYTE_VIEW || actual_element != element_type) {
    ir_free_value(items);
    ir_graph_mark_unsupported(ir, items_node, "typed graph MIR std.search helper requires a typed span matching the helper width", "wrong std.search span type");
    return false;
  }
  IrValue *needle = NULL;
  if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, element_type, &needle)) {
    ir_free_value(items);
    return false;
  }
  if (!needle || needle->type != element_type) {
    ir_free_value(needle);
    ir_free_value(items);
    ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 1), "typed graph MIR std.search helper needle has wrong type", "wrong std.search needle type");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_SEARCH_RUNTIME, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
  value->int_value = (unsigned long long)op;
  value->left = items;
  value->right = ir_new_cast_value(ir, needle, IR_TYPE_I64, ir_graph_line(expr), ir_graph_column(expr));
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_std_sort_runtime_spec(const char *callee_name, IrSortOp *op, IrTypeKind *element_type, IrTypeKind *return_type) {
  if (!callee_name || !op || !element_type || !return_type) return false;
  if (ir_text_eq(callee_name, "std.sort.insertionI32")) { *op = IR_SORT_OP_INSERTION_I32; *element_type = IR_TYPE_I32; *return_type = IR_TYPE_VOID; return true; }
  if (ir_text_eq(callee_name, "std.sort.isSortedI32")) { *op = IR_SORT_OP_IS_SORTED_I32; *element_type = IR_TYPE_I32; *return_type = IR_TYPE_BOOL; return true; }
  if (ir_text_eq(callee_name, "std.sort.insertionU32")) { *op = IR_SORT_OP_INSERTION_U32; *element_type = IR_TYPE_U32; *return_type = IR_TYPE_VOID; return true; }
  if (ir_text_eq(callee_name, "std.sort.isSortedU32")) { *op = IR_SORT_OP_IS_SORTED_U32; *element_type = IR_TYPE_U32; *return_type = IR_TYPE_BOOL; return true; }
  if (ir_text_eq(callee_name, "std.sort.insertionUsize")) { *op = IR_SORT_OP_INSERTION_USIZE; *element_type = IR_TYPE_USIZE; *return_type = IR_TYPE_VOID; return true; }
  if (ir_text_eq(callee_name, "std.sort.isSortedUsize")) { *op = IR_SORT_OP_IS_SORTED_USIZE; *element_type = IR_TYPE_USIZE; *return_type = IR_TYPE_BOOL; return true; }
  return false;
}

static bool ir_graph_lower_std_sort_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = false;
  IrSortOp op = IR_SORT_OP_INSERTION_I32;
  IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
  IrTypeKind return_type = IR_TYPE_UNSUPPORTED;
  if (!ir_graph_std_sort_runtime_spec(callee_name, &op, &element_type, &return_type)) return true;
  *handled = true;
  if (arg_count != 1) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.sort helper has unsupported arity", "wrong std.sort arity");
    return false;
  }
  const ZProgramGraphNode *items_node = ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 0);
  IrValue *items = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, items_node, &items)) return false;
  IrTypeKind actual_element = items && items->element_type != IR_TYPE_UNSUPPORTED ? items->element_type : IR_TYPE_U8;
  if (!items || items->type != IR_TYPE_BYTE_VIEW || actual_element != element_type) {
    ir_free_value(items);
    ir_graph_mark_unsupported(ir, items_node, "typed graph MIR std.sort helper requires a typed span matching the helper width", "wrong std.sort span type");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_SORT_RUNTIME, return_type, ir_graph_line(expr), ir_graph_column(expr));
  value->int_value = (unsigned long long)op;
  value->left = items;
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_testing_arg(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, size_t order, IrTypeKind expected, IrValue **out) {
  const ZProgramGraphNode *arg = ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", order);
  if (!arg) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.testing helper argument is missing", "missing std.testing argument");
    return false;
  }
  if (expected == IR_TYPE_BYTE_VIEW) return ir_graph_lower_byte_view(graph, ir, fun, arg, out);
  if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, order, expected, out)) return false;
  if (*out && (*out)->type == expected) return true;
  ir_free_value(*out);
  *out = NULL;
  ir_graph_mark_unsupported(ir, arg, "typed graph MIR std.testing argument type does not match helper", ir_graph_node_type(graph, arg));
  return false;
}

static bool ir_graph_lower_std_testing_equal_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrTypeKind type, IrValue **out) {
  IrValue *left = NULL;
  IrValue *right = NULL;
  if (!ir_graph_lower_std_testing_arg(graph, ir, fun, expr, 0, type, &left) ||
      !ir_graph_lower_std_testing_arg(graph, ir, fun, expr, 1, type, &right)) {
    ir_free_value(left);
    ir_free_value(right);
    return false;
  }
  *out = ir_new_compare_value(ir, IR_CMP_EQ, left, right, ir_graph_line(expr), ir_graph_column(expr));
  return true;
}

static bool ir_graph_lower_std_testing_byte_pair_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrStrOp op, bool byte_equal, IrValue **out) {
  if (byte_equal) {
    IrValue *left = NULL;
    IrValue *right = NULL;
    if (!ir_graph_lower_std_testing_arg(graph, ir, fun, expr, 0, IR_TYPE_BYTE_VIEW, &left) ||
        !ir_graph_lower_std_testing_arg(graph, ir, fun, expr, 1, IR_TYPE_BYTE_VIEW, &right)) {
      ir_free_value(left);
      ir_free_value(right);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_EQ, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->left = left;
    value->right = right;
    *out = value;
    return true;
  }
  const IrTypeKind two_views[] = {IR_TYPE_BYTE_VIEW, IR_TYPE_BYTE_VIEW};
  return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, op, IR_TYPE_BOOL, two_views, 2, out);
}

static bool ir_graph_lower_std_testing_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  if (ir_text_eq(callee_name, "std.testing.isTrue") && arg_count == 1) {
    return ir_graph_lower_std_testing_arg(graph, ir, fun, expr, 0, IR_TYPE_BOOL, out);
  }
  if (ir_text_eq(callee_name, "std.testing.isFalse") && arg_count == 1) {
    IrValue *arg = NULL;
    if (!ir_graph_lower_std_testing_arg(graph, ir, fun, expr, 0, IR_TYPE_BOOL, &arg)) return false;
    IrValue *false_value = ir_new_value(ir, IR_VALUE_BOOL, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    false_value->int_value = 0;
    *out = ir_new_compare_value(ir, IR_CMP_EQ, arg, false_value, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (ir_text_eq(callee_name, "std.testing.equalBool") && arg_count == 2) return ir_graph_lower_std_testing_equal_call(graph, ir, fun, expr, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.testing.equalUsize") && arg_count == 2) return ir_graph_lower_std_testing_equal_call(graph, ir, fun, expr, IR_TYPE_USIZE, out);
  if (ir_text_eq(callee_name, "std.testing.equalU32") && arg_count == 2) return ir_graph_lower_std_testing_equal_call(graph, ir, fun, expr, IR_TYPE_U32, out);
  if (ir_text_eq(callee_name, "std.testing.equalI32") && arg_count == 2) return ir_graph_lower_std_testing_equal_call(graph, ir, fun, expr, IR_TYPE_I32, out);
  if (ir_text_eq(callee_name, "std.testing.equalBytes") && arg_count == 2) return ir_graph_lower_std_testing_byte_pair_call(graph, ir, fun, expr, IR_STR_OP_CONTAINS, true, out);
  if (ir_text_eq(callee_name, "std.testing.containsBytes") && arg_count == 2) return ir_graph_lower_std_testing_byte_pair_call(graph, ir, fun, expr, IR_STR_OP_CONTAINS, false, out);
  if (ir_text_eq(callee_name, "std.testing.startsWith") && arg_count == 2) return ir_graph_lower_std_testing_byte_pair_call(graph, ir, fun, expr, IR_STR_OP_STARTS_WITH, false, out);
  if (ir_text_eq(callee_name, "std.testing.endsWith") && arg_count == 2) return ir_graph_lower_std_testing_byte_pair_call(graph, ir, fun, expr, IR_STR_OP_ENDS_WITH, false, out);
  *handled = false;
  return true;
}

static bool ir_graph_lower_std_byte_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  if (ir_text_eq(callee_name, "std.args.len") && arg_count == 0) {
    *out = ir_new_value(ir, IR_VALUE_ARGS_LEN, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (ir_text_eq(callee_name, "std.args.get") && arg_count == 1) {
    IrValue *index = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_USIZE, &index)) return false;
    if (!ir_type_is_value(index->type)) {
      ir_free_value(index);
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.args.get index must be an integer value", "non-integer index");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_GET, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = index;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.args.has") && arg_count == 1) {
    IrValue *index = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_USIZE, &index)) return false;
    if (!ir_type_is_value(index->type)) {
      ir_free_value(index);
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.args.has index must be an integer value", "non-integer index");
      return false;
    }
    IrValue *len = ir_new_value(ir, IR_VALUE_ARGS_LEN, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    *out = ir_new_compare_value(ir, IR_CMP_LT, index, len, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (ir_text_eq(callee_name, "std.args.getOr") && arg_count == 2) {
    IrValue *index = NULL;
    IrValue *fallback = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_USIZE, &index) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &fallback)) {
      ir_free_value(index);
      ir_free_value(fallback);
      return false;
    }
    if (!ir_type_is_value(index->type)) {
      ir_free_value(index);
      ir_free_value(fallback);
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.args.getOr index must be an integer value", "non-integer index");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_GET_OR, IR_TYPE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = index;
    value->right = fallback;
    value->element_type = IR_TYPE_U8;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.args.find") && arg_count == 1) {
    IrValue *name = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &name)) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_FIND, IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
    value->left = name;
    value->element_type = IR_TYPE_USIZE;
    ir_graph_require_runtime_helper(ir);
    *out = value;
    return true;
  }
  if ((ir_text_eq(callee_name, "std.args.valueAfter") || ir_text_eq(callee_name, "std.cli.optionValue")) && arg_count == 1) {
    IrValue *name = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &name)) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_VALUE_AFTER, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = name;
    value->element_type = IR_TYPE_U8;
    ir_graph_require_runtime_helper(ir);
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.cli.optionValueOr") && arg_count == 2) {
    IrValue *name = NULL;
    IrValue *fallback = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &name) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &fallback)) {
      ir_free_value(name);
      ir_free_value(fallback);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_VALUE_AFTER_OR, IR_TYPE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = name;
    value->right = fallback;
    value->element_type = IR_TYPE_U8;
    ir_graph_require_runtime_helper(ir);
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.cli.optionU32") && arg_count == 1) {
    IrValue *name = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &name)) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_VALUE_AFTER_PARSE_U32, IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
    value->left = name;
    value->element_type = IR_TYPE_U32;
    ir_graph_require_runtime_helper(ir);
    if (ir->direct_host_runtime_import_count < 2) ir->direct_host_runtime_import_count = 2;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.cli.successExitCode") && arg_count == 0) {
    *out = ir_new_integer_literal_value(ir, IR_TYPE_I32, 0, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (ir_text_eq(callee_name, "std.cli.usageExitCode") && arg_count == 0) {
    *out = ir_new_integer_literal_value(ir, IR_TYPE_I32, 2, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  int json_error_code = ir_graph_json_error_code(callee_name);
  if (json_error_code >= 0 && arg_count == 0) {
    *out = ir_new_integer_literal_value(ir, IR_TYPE_U32, (unsigned long long)json_error_code, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (ir_text_eq(callee_name, "std.cli.argEquals") && arg_count == 2) {
    IrValue *index = NULL;
    IrValue *expected = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_USIZE, &index) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &expected)) {
      ir_free_value(index);
      ir_free_value(expected);
      return false;
    }
    if (!ir_type_is_value(index->type)) {
      ir_free_value(index);
      ir_free_value(expected);
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.cli.argEquals index must be an integer value", "non-integer index");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_EQ, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->left = index;
    value->right = expected;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.cli.hasFlag") && arg_count == 1) {
    IrValue *name = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &name)) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_CONTAINS, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->left = name;
    ir_graph_require_runtime_helper(ir);
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.proc.spawn") && arg_count == 1) {
    IrValue *command = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &command)) return false;
    ir_free_value(command);
    *out = ir_new_integer_literal_value(ir, IR_TYPE_I32, 0, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (ir_text_eq(callee_name, "std.proc.exitCode") && arg_count == 1) {
    return ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_I32, out);
  }
  if ((ir_text_eq(callee_name, "std.proc.succeeded") || ir_text_eq(callee_name, "std.proc.failed")) && arg_count == 1) {
    IrValue *status = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_I32, &status)) return false;
    if (!status || status->type != IR_TYPE_I32) {
      ir_free_value(status);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 0), "typed graph MIR std.proc status helper expects ProcStatus", "non-ProcStatus argument");
      return false;
    }
    IrCompareOp op = ir_text_eq(callee_name, "std.proc.succeeded") ? IR_CMP_EQ : IR_CMP_NE;
    *out = ir_new_compare_value(ir, op, status, ir_new_integer_literal_value(ir, IR_TYPE_I32, 0, ir_graph_line(expr), ir_graph_column(expr)), ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  {
    IrMathOp math_op = IR_MATH_OP_MIN_I32;
    IrTypeKind math_arg_type = IR_TYPE_UNSUPPORTED;
    IrTypeKind math_return_type = IR_TYPE_UNSUPPORTED;
    IrTypeKind math_return_element_type = IR_TYPE_UNSUPPORTED;
    size_t math_expected_args = 0;
    if (ir_graph_std_math_runtime_spec(callee_name, arg_count, &math_op, &math_arg_type, &math_return_type, &math_return_element_type, &math_expected_args)) {
      return ir_graph_make_std_math_runtime_value(graph, ir, fun, expr, math_op, math_arg_type, math_return_type, math_return_element_type, math_expected_args, out);
    }
  }
  {
    bool handled = false;
    if (!ir_graph_lower_std_search_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) return false;
    if (handled) return true;
  }
  {
    bool handled = false;
    if (!ir_graph_lower_std_sort_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) return false;
    if (handled) return true;
  }
  if ((ir_text_eq(callee_name, "std.math.isEvenU32") || ir_text_eq(callee_name, "std.math.isOddU32")) && arg_count == 1) {
    IrValue *number = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_U32, &number)) return false;
    if (!number || number->type != IR_TYPE_U32) {
      ir_free_value(number);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 0), "typed graph MIR std.math parity helper expects u32", "non-u32 argument");
      return false;
    }
    IrValue *remainder = ir_new_binary_value(ir, IR_BIN_MOD, IR_TYPE_U32, number, ir_new_integer_literal_value(ir, IR_TYPE_U32, 2, ir_graph_line(expr), ir_graph_column(expr)), ir_graph_line(expr), ir_graph_column(expr));
    IrCompareOp op = ir_text_eq(callee_name, "std.math.isEvenU32") ? IR_CMP_EQ : IR_CMP_NE;
    *out = ir_new_compare_value(ir, op, remainder, ir_new_integer_literal_value(ir, IR_TYPE_U32, 0, ir_graph_line(expr), ir_graph_column(expr)), ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (ir_text_eq(callee_name, "std.args.parseU32") && arg_count == 1) {
    IrValue *index = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_USIZE, &index)) return false;
    if (!ir_type_is_value(index->type)) {
      ir_free_value(index);
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.args.parseU32 index must be an integer value", "non-integer index");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_PARSE_U32, IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
    value->left = index;
    value->element_type = IR_TYPE_U32;
    ir_graph_require_runtime_helper(ir);
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.mem.span") && arg_count == 1) {
    return ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), out);
  }
  if (ir_text_eq(callee_name, "std.mem.len") && arg_count == 1) {
    const ZProgramGraphNode *arg = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    if (arg && arg->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER) {
      const IrLocal *local = ir_function_find_local(fun, arg->name);
      if (local && local->is_array) {
        *out = ir_new_integer_literal_value(ir, IR_TYPE_USIZE, local->array_len, ir_graph_line(expr), ir_graph_column(expr));
        return true;
      }
    }
    IrValue *view = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, arg, &view)) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    value->left = view;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.mem.isEmpty") && arg_count == 1) {
    IrValue *view = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &view)) return false;
    IrValue *len = ir_new_value(ir, IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    len->left = view;
    *out = ir_new_compare_value(
        ir,
        IR_CMP_EQ,
        len,
        ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 0, ir_graph_line(expr), ir_graph_column(expr)),
        ir_graph_line(expr),
        ir_graph_column(expr));
    return true;
  }
  if (ir_text_eq(callee_name, "std.io.remaining") && arg_count == 2) {
    IrValue *view = NULL;
    IrValue *offset = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &view) ||
        !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_USIZE, &offset)) {
      ir_free_value(view);
      ir_free_value(offset);
      return false;
    }
    if (!ir_type_is_value(offset->type)) {
      ir_free_value(view);
      ir_free_value(offset);
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.io.remaining offset must be an integer value", "non-integer offset");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_REMAINING, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    value->left = view;
    value->index = offset;
    *out = value;
    return true;
  }
  if ((ir_text_eq(callee_name, "std.parse.parseI32") || ir_text_eq(callee_name, "std.parse.parseU32")) && arg_count == 1) {
    IrValue *text = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &text)) return false;
    bool signed_parse = ir_text_eq(callee_name, "std.parse.parseI32");
    IrValue *value = ir_new_value(ir, signed_parse ? IR_VALUE_PARSE_I32 : IR_VALUE_PARSE_U32, IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
    value->left = text;
    value->element_type = signed_parse ? IR_TYPE_I32 : IR_TYPE_U32;
    ir_graph_require_runtime_helper(ir);
    *out = value;
    return true;
  }
  if ((ir_text_eq(callee_name, "std.fmt.bool") ||
       ir_text_eq(callee_name, "std.fmt.hexLowerU32") ||
       ir_text_eq(callee_name, "std.fmt.i32") ||
       ir_text_eq(callee_name, "std.fmt.u32") ||
       ir_text_eq(callee_name, "std.fmt.usize")) &&
      arg_count == 2) {
    IrValue *buffer = NULL;
    IrValue *number = NULL;
    IrTypeKind number_type = IR_TYPE_U32;
    IrValueKind kind = IR_VALUE_FMT_U32;
    const char *type_error = "typed graph MIR std.fmt.u32 value must be u32";
    const char *actual = "non-u32 value";
    if (ir_text_eq(callee_name, "std.fmt.bool")) {
      number_type = IR_TYPE_BOOL;
      kind = IR_VALUE_FMT_BOOL;
      type_error = "typed graph MIR std.fmt.bool value must be Bool";
      actual = "non-Bool value";
    } else if (ir_text_eq(callee_name, "std.fmt.hexLowerU32")) {
      kind = IR_VALUE_FMT_HEX_U32;
      type_error = "typed graph MIR std.fmt.hexLowerU32 value must be u32";
    } else if (ir_text_eq(callee_name, "std.fmt.i32")) {
      number_type = IR_TYPE_I32;
      kind = IR_VALUE_FMT_I32;
      type_error = "typed graph MIR std.fmt.i32 value must be i32";
      actual = "non-i32 value";
    } else if (ir_text_eq(callee_name, "std.fmt.usize")) {
      number_type = IR_TYPE_USIZE;
      kind = IR_VALUE_FMT_USIZE;
      type_error = "typed graph MIR std.fmt.usize value must be usize";
      actual = "non-usize value";
    }
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &buffer) ||
        !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, number_type, &number)) {
      ir_free_value(buffer);
      ir_free_value(number);
      return false;
    }
    if (number->type != number_type) {
      ir_free_value(buffer);
      ir_free_value(number);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), type_error, actual);
      return false;
    }
    IrValue *value = ir_new_value(ir, kind, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = buffer;
    value->right = number;
    value->element_type = IR_TYPE_U8;
    ir_graph_require_runtime_helper(ir);
    *out = value;
    return true;
  }
  if ((ir_text_eq(callee_name, "std.mem.eql") || ir_text_eq(callee_name, "std.mem.eqlBytes") || ir_text_eq(callee_name, "std.str.contains")) && arg_count == 2) {
    IrValue *left = NULL;
    IrValue *right = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &left) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &right)) {
      ir_free_value(left);
      ir_free_value(right);
      return false;
    }
    IrValue *value = ir_new_value(ir, ir_text_eq(callee_name, "std.str.contains") ? IR_VALUE_STR_CONTAINS : IR_VALUE_BYTE_VIEW_EQ, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->left = left;
    value->right = right;
    if (value->kind == IR_VALUE_STR_CONTAINS) ir_graph_require_runtime_helper(ir);
    *out = value;
    return true;
  }
  *handled = false;
  return true;
}

static bool ir_graph_lower_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_CALL && (ir_compare_op(expr->name, &(IrCompareOp){0}) || ir_binary_op(expr->name, &(IrBinaryOp){0}))) {
    return ir_graph_lower_binary_call(graph, ir, fun, expr, out);
  }

  char *qualified = ir_graph_expr_qualified_name(graph, expr);
  const char *callee_name = qualified && qualified[0] ? qualified : expr->name;
  size_t arg_count = ir_graph_edge_count(graph, expr->id, "arg");
  bool handled = false;
  if (!ir_graph_lower_http_std_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) {
    free(qualified);
    return false;
  }
  if (handled) {
    free(qualified);
    return true;
  }
  if (!ir_graph_lower_std_str_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) {
    free(qualified);
    return false;
  }
  if (handled) {
    free(qualified);
    return true;
  }
  if (!ir_graph_lower_std_ascii_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) {
    free(qualified);
    return false;
  }
  if (handled) {
    free(qualified);
    return true;
  }
  if (!ir_graph_lower_std_text_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) {
    free(qualified);
    return false;
  }
  if (handled) {
    free(qualified);
    return true;
  }
  if (!ir_graph_lower_std_parse_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) {
    free(qualified);
    return false;
  }
  if (handled) {
    free(qualified);
    return true;
  }
  if (!ir_graph_lower_std_time_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) {
    free(qualified);
    return false;
  }
  if (handled) {
    free(qualified);
    return true;
  }
  if (!ir_graph_lower_std_testing_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) {
    free(qualified);
    return false;
  }
  if (handled) {
    free(qualified);
    return true;
  }
  if (!ir_graph_lower_std_byte_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) {
    free(qualified);
    return false;
  }
  if (handled) {
    free(qualified);
    return true;
  }
  const char *source_backed_std = z_std_source_target_for_public_call(callee_name);
  if (source_backed_std) {
    bool lowered = ir_graph_lower_named_call(graph, ir, fun, expr, source_backed_std, out);
    free(qualified);
    return lowered;
  }
  bool c_import_handled = false;
  if (!ir_graph_lower_c_import_call(graph, ir, fun, expr, callee_name, &c_import_handled, out)) {
    free(qualified);
    return false;
  }
  if (c_import_handled) {
    free(qualified);
    return true;
  }
  bool lowered = ir_graph_lower_named_call(graph, ir, fun, expr, expr->name, out);
  free(qualified);
  return lowered;
}

static bool ir_graph_lower_expr(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  if (!expr) {
    ir_mark_unsupported(ir, "typed graph MIR expression is missing", 1, 1, "missing expression");
    return false;
  }
  switch (expr->kind) {
    case Z_PROGRAM_GRAPH_NODE_LITERAL:
      return ir_graph_lower_literal(expr, ir, out);
    case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
      return ir_graph_lower_identifier(expr, ir, fun, out);
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
      return ir_graph_lower_field_access(graph, ir, fun, expr, out);
    case Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS:
      return ir_graph_lower_index_access(graph, ir, fun, expr, out);
    case Z_PROGRAM_GRAPH_NODE_SLICE:
      return ir_graph_lower_byte_slice(graph, ir, fun, expr, out);
    case Z_PROGRAM_GRAPH_NODE_CAST: {
      IrValue *inner = NULL;
      if (!ir_graph_lower_expr(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "left", 0), &inner)) return false;
      IrTypeKind cast_type = ir_type_kind(ir_graph_node_type(graph, expr));
      if (!ir_type_is_value(cast_type) && cast_type != IR_TYPE_BOOL) {
        ir_free_value(inner);
        ir_graph_mark_unsupported(ir, expr, "typed graph MIR cast target type is unsupported", ir_graph_node_type(graph, expr));
        return false;
      }
      *out = ir_new_cast_value(ir, inner, cast_type, ir_graph_line(expr), ir_graph_column(expr));
      return true;
    }
    case Z_PROGRAM_GRAPH_NODE_CALL:
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL:
      return ir_graph_lower_call(graph, ir, fun, expr, out);
    default:
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR expression kind is unsupported", z_program_graph_node_kind_name(expr->kind));
      return false;
  }
}

static bool ir_graph_is_world_stream_write(const ZProgramGraph *graph, const IrFunction *fun, const ZProgramGraphNode *expr, const char *stream) {
  if (!expr || expr->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL || ir_graph_edge_count(graph, expr->id, "arg") != 1) return false;
  char *qualified = ir_graph_expr_qualified_name(graph, expr);
  bool ok = false;
  if (qualified && fun && fun->world_param_name) {
    ZBuf expected;
    zbuf_init(&expected);
    zbuf_append(&expected, fun->world_param_name);
    zbuf_append_char(&expected, '.');
    zbuf_append(&expected, stream ? stream : "out");
    zbuf_append(&expected, ".write");
    ok = ir_text_eq(qualified, expected.data);
    zbuf_free(&expected);
  }
  free(qualified);
  return ok;
}

static bool ir_graph_lower_block(const ZProgramGraph *graph, IrProgram *ir, IrFunction *fun, const ZProgramGraphNode *block, IrInstr **out_items, size_t *out_len, size_t *out_cap, bool *saw_return);

static bool ir_graph_lower_stmt(const ZProgramGraph *graph, IrProgram *ir, IrFunction *fun, const ZProgramGraphNode *stmt, IrInstr **out_items, size_t *out_len, size_t *out_cap, bool *saw_return) {
  if (!stmt) return true;
  if (stmt->kind == Z_PROGRAM_GRAPH_NODE_LET) {
    const IrLocal *local = ir_function_find_local(fun, stmt->name);
    if (!local) return false;
    const ZProgramGraphNode *expr = ir_graph_ordered_node(graph, stmt->id, "expr", 0);
    if (local->is_array) {
      if (!ir_graph_lower_array_initializer(graph, ir, fun, local, expr, out_items, out_len, out_cap, ir_graph_line(stmt), ir_graph_column(stmt))) return false;
      ir_active_local_push(ir, stmt->name);
      return true;
    }
    IrValue *value = NULL;
    if (!ir_graph_lower_expr_for_type(graph, ir, fun, expr, local->type, false, ir_graph_line(stmt), ir_graph_column(stmt), &value)) return false;
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_LOCAL_SET, .local_index = local->index, .value = value, .line = ir_graph_line(stmt), .column = ir_graph_column(stmt)});
    ir_active_local_push(ir, stmt->name);
    return true;
  }
  if (stmt->kind == Z_PROGRAM_GRAPH_NODE_ASSIGNMENT) {
    const ZProgramGraphNode *target = ir_graph_ordered_node(graph, stmt->id, "target", 0);
    const ZProgramGraphNode *expr = ir_graph_ordered_node(graph, stmt->id, "expr", 0);
    if (target && target->kind == Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS) {
      const ZProgramGraphNode *base = ir_graph_ordered_node(graph, target->id, "left", 0);
      const ZProgramGraphNode *index_node = ir_graph_ordered_node(graph, target->id, "right", 1);
      if (!base || base->kind != Z_PROGRAM_GRAPH_NODE_IDENTIFIER) {
        ir_graph_mark_unsupported(ir, target, "typed graph MIR indexed assignment supports only local fixed arrays and mutable byte views", "non-local indexed assignment");
        return false;
      }
      const IrLocal *local = ir_function_find_local(fun, base->name);
      if (!local || (!local->is_array && local->type != IR_TYPE_BYTE_VIEW)) {
        ir_graph_mark_unsupported(ir, target, "typed graph MIR indexed assignment target is not a fixed array or byte view local", base->name);
        return false;
      }
      if (!local->is_mutable) {
        ir_graph_mark_unsupported(ir, target, "typed graph MIR indexed assignment target must be mutable", base->name);
        return false;
      }
      IrValue *index = NULL;
      IrValue *value = NULL;
      IrTypeKind element_type = local->type == IR_TYPE_BYTE_VIEW ? (local->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : local->element_type) : local->element_type;
      if (!ir_graph_lower_expr(graph, ir, fun, index_node, &index) ||
          !ir_graph_lower_expr_for_type(graph, ir, fun, expr, element_type, false, ir_graph_line(expr), ir_graph_column(expr), &value)) {
        ir_free_value(index);
        ir_free_value(value);
        return false;
      }
      if (!ir_type_is_value(index->type) || value->type != element_type) {
        ir_free_value(index);
        ir_free_value(value);
        ir_graph_mark_unsupported(ir, target, "typed graph MIR indexed assignment type does not match target element", local->name);
        return false;
      }
      ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_INDEX_STORE, .array_index = local->index, .index = index, .value = value, .line = ir_graph_line(stmt), .column = ir_graph_column(stmt)});
      return true;
    }
    if (!target || target->kind != Z_PROGRAM_GRAPH_NODE_IDENTIFIER) {
      ir_graph_mark_unsupported(ir, target ? target : stmt, "typed graph MIR assignment currently supports only local variables", "non-local assignment");
      return false;
    }
    const IrLocal *local = ir_function_find_local(fun, target->name);
    if (!local) return false;
    IrValue *value = NULL;
    if (!ir_graph_lower_expr_for_type(graph, ir, fun, expr, local->type, fun->return_type == IR_TYPE_I32, ir_graph_line(stmt), ir_graph_column(stmt), &value)) return false;
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_LOCAL_SET, .local_index = local->index, .value = value, .line = ir_graph_line(stmt), .column = ir_graph_column(stmt)});
    return true;
  }
  if (stmt->kind == Z_PROGRAM_GRAPH_NODE_RETURN) {
    *saw_return = true;
    IrValue *value = NULL;
    const ZProgramGraphNode *expr = ir_graph_ordered_node(graph, stmt->id, "expr", 0);
    if (fun->return_type == IR_TYPE_VOID) {
      if (expr) {
        ir_graph_mark_unsupported(ir, stmt, "typed graph MIR void function cannot return a value", fun->name);
        return false;
      }
    } else if (!ir_graph_lower_expr_for_type(graph, ir, fun, expr, fun->return_type, true, ir_graph_line(stmt), ir_graph_column(stmt), &value)) {
      return false;
    }
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_RETURN, .value = value, .line = ir_graph_line(stmt), .column = ir_graph_column(stmt)});
    return true;
  }
  if (stmt->kind == Z_PROGRAM_GRAPH_NODE_CHECK) {
    const ZProgramGraphNode *expr = ir_graph_ordered_node(graph, stmt->id, "expr", 0);
    if (ir_graph_is_world_stream_write(graph, fun, expr, "out") || ir_graph_is_world_stream_write(graph, fun, expr, "err")) {
      IrValue *bytes = NULL;
      if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &bytes)) return false;
      if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
      ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){
        .kind = IR_INSTR_WORLD_WRITE,
        .field_offset = ir_graph_is_world_stream_write(graph, fun, expr, "err") ? 2 : 1,
        .value = bytes,
        .line = ir_graph_line(stmt),
        .column = ir_graph_column(stmt)
      });
      return true;
    }
    IrValue *checked = NULL;
    if (!ir_graph_lower_expr(graph, ir, fun, expr, &checked)) return false;
    if (!checked || checked->type != IR_TYPE_I64) {
      ir_free_value(checked);
      ir_graph_mark_unsupported(ir, stmt, "typed graph MIR check statement requires a fallible value", "non-fallible check");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_CHECK, checked->element_type, ir_graph_line(stmt), ir_graph_column(stmt));
    value->left = checked;
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_EXPR, .value = value, .line = ir_graph_line(stmt), .column = ir_graph_column(stmt)});
    return true;
  }
  if (stmt->kind == Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT) {
    const ZProgramGraphNode *expr = ir_graph_ordered_node(graph, stmt->id, "expr", 0);
    IrValue *value = NULL;
    if (!ir_graph_lower_expr(graph, ir, fun, expr, &value)) return false;
    if (!value || value->type != IR_TYPE_VOID) {
      ir_free_value(value);
      ir_graph_mark_unsupported(ir, stmt, "typed graph MIR expression statement must return Void", "non-Void expression");
      return false;
    }
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_EXPR, .value = value, .line = ir_graph_line(stmt), .column = ir_graph_column(stmt)});
    return true;
  }
  if (stmt->kind == Z_PROGRAM_GRAPH_NODE_IF) {
    IrValue *cond = NULL;
    const ZProgramGraphNode *expr = ir_graph_ordered_node(graph, stmt->id, "expr", 0);
    if (!ir_graph_lower_expr(graph, ir, fun, expr, &cond)) return false;
    if (cond->type != IR_TYPE_BOOL) {
      ir_free_value(cond);
      ir_graph_mark_unsupported(ir, stmt, "typed graph MIR if condition must be Bool", "non-Bool condition");
      return false;
    }
    IrInstr instr = {.kind = IR_INSTR_IF, .value = cond, .line = ir_graph_line(stmt), .column = ir_graph_column(stmt)};
    size_t scope_mark = ir_active_local_mark(ir);
    bool then_ok = ir_graph_lower_block(graph, ir, fun, ir_graph_ordered_node(graph, stmt->id, "then", 0), &instr.then_instrs, &instr.then_len, &instr.then_cap, saw_return);
    ir_active_local_restore(ir, scope_mark);
    bool else_ok = then_ok && ir_graph_lower_block(graph, ir, fun, ir_graph_ordered_node(graph, stmt->id, "else", 0), &instr.else_instrs, &instr.else_len, &instr.else_cap, saw_return);
    ir_active_local_restore(ir, scope_mark);
    if (!then_ok || !else_ok) {
      ir_free_value(cond);
      ir_free_instrs(instr.then_instrs, instr.then_len);
      ir_free_instrs(instr.else_instrs, instr.else_len);
      free(instr.then_instrs);
      free(instr.else_instrs);
      return false;
    }
    ir_instr_vec_push(ir, out_items, out_len, out_cap, instr);
    return true;
  }
  if (stmt->kind == Z_PROGRAM_GRAPH_NODE_WHILE) {
    IrValue *cond = NULL;
    const ZProgramGraphNode *expr = ir_graph_ordered_node(graph, stmt->id, "expr", 0);
    if (!ir_graph_lower_expr(graph, ir, fun, expr, &cond)) return false;
    if (cond->type != IR_TYPE_BOOL) {
      ir_free_value(cond);
      ir_graph_mark_unsupported(ir, stmt, "typed graph MIR while condition must be Bool", "non-Bool condition");
      return false;
    }
    IrInstr instr = {.kind = IR_INSTR_WHILE, .value = cond, .line = ir_graph_line(stmt), .column = ir_graph_column(stmt)};
    size_t scope_mark = ir_active_local_mark(ir);
    bool body_ok = ir_graph_lower_block(graph, ir, fun, ir_graph_ordered_node(graph, stmt->id, "then", 0), &instr.then_instrs, &instr.then_len, &instr.then_cap, saw_return);
    ir_active_local_restore(ir, scope_mark);
    if (!body_ok) {
      ir_free_value(cond);
      ir_free_instrs(instr.then_instrs, instr.then_len);
      free(instr.then_instrs);
      return false;
    }
    ir_instr_vec_push(ir, out_items, out_len, out_cap, instr);
    return true;
  }
  ir_graph_mark_unsupported(ir, stmt, "typed graph MIR statement kind is unsupported", z_program_graph_node_kind_name(stmt->kind));
  return false;
}

static bool ir_graph_lower_block(const ZProgramGraph *graph, IrProgram *ir, IrFunction *fun, const ZProgramGraphNode *block, IrInstr **out_items, size_t *out_len, size_t *out_cap, bool *saw_return) {
  if (!block) return true;
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = ir_graph_next_edge_by_order(graph, block->id, "statement", have_last, last_order);
    if (!edge) break;
    have_last = true;
    last_order = edge->order;
    const ZProgramGraphNode *stmt = ir_graph_find_node(graph, edge->to);
    if (!ir_graph_lower_stmt(graph, ir, fun, stmt, out_items, out_len, out_cap, saw_return)) return false;
  }
  return true;
}

static bool ir_graph_lower_function_body(IrProgram *ir, IrFunction *fun, const ZProgramGraph *graph, const ZProgramGraphNode *function) {
  bool hosted_world_main = ir_graph_function_is_hosted_world_main(graph, function);
  IrTypeKind return_type = hosted_world_main ? IR_TYPE_I32 : ir_type_kind(ir_graph_node_type(graph, function));
  if (!hosted_world_main && function->fallible && !ir_type_is_direct_fallible_value(return_type)) {
    ir_graph_mark_unsupported(ir, function, "typed graph MIR fallible return type is unsupported", ir_graph_node_type(graph, function));
    return false;
  }
  if (!hosted_world_main && !function->fallible && return_type != IR_TYPE_VOID && !ir_type_is_direct_return_abi(return_type)) {
    ir_graph_mark_unsupported(ir, function, "typed graph MIR return type is unsupported", ir_graph_node_type(graph, function));
    return false;
  }
  size_t scope_mark = ir_active_local_mark(ir);
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = ir_graph_next_edge_by_order(graph, function->id, "param", have_last, last_order);
    if (!edge) break;
    have_last = true;
    last_order = edge->order;
    const ZProgramGraphNode *param = ir_graph_find_node(graph, edge->to);
    if (param) ir_active_local_push(ir, param->name);
  }
  bool saw_return = false;
  bool lowered = ir_graph_lower_block(graph, ir, fun, ir_graph_ordered_node(graph, function->id, "body", 0), &fun->instrs, &fun->instr_len, &fun->instr_cap, &saw_return);
  ir_active_local_restore(ir, scope_mark);
  if (!lowered) return false;
  if (hosted_world_main && !saw_return) {
    IrValue *exit_code = ir_new_integer_literal_value(ir, IR_TYPE_I32, 0, ir_graph_line(function), ir_graph_column(function));
    ir_instr_vec_push(ir, &fun->instrs, &fun->instr_len, &fun->instr_cap, (IrInstr){
      .kind = IR_INSTR_RETURN,
      .value = exit_code,
      .line = ir_graph_line(function),
      .column = ir_graph_column(function)
    });
    saw_return = true;
  }
  if (return_type != IR_TYPE_VOID && !saw_return) {
    ir_graph_mark_unsupported(ir, function, "typed graph MIR function must return a value", function->name);
    return false;
  }
  return true;
}

static IrProgram ir_lower_program_graph_with_source_helpers(const ZProgramGraph *graph, const SourceInput *input, const ZTargetInfo *target, IrProgram *source_helper_ir) {
  IrProgram ir = {0};
  ir.target = target;
  ir.mir_valid = true;
  ir.mir_line = 1;
  ir.mir_column = 1;
  snprintf(ir.mir_expected, sizeof(ir.mir_expected), "typed program graph MIR subset");
  snprintf(ir.mir_help, sizeof(ir.mir_help), "use zero check to inspect unsupported graph constructs or choose another supported target");
  ir.mir_bytes = sizeof(IrProgram);

  size_t function_count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && !ir_graph_node_is_test_function(&graph->nodes[i])) function_count++;
  }
  IrFunctionOrder *order = z_checked_calloc(function_count ? function_count : 1, sizeof(IrFunctionOrder));
  char **stable_ids = z_checked_calloc(function_count ? function_count : 1, sizeof(char *));
  size_t ordered_len = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION || ir_graph_node_is_test_function(node)) continue;
    stable_ids[ordered_len] = ir_graph_stable_id_for_function(graph, node);
    order[ordered_len] = (IrFunctionOrder){.name = node->name, .stable_id = stable_ids[ordered_len], .source_index = i};
    ordered_len++;
  }
  qsort(order, ordered_len, sizeof(IrFunctionOrder), ir_function_order_compare);
  for (size_t i = 0; i < ordered_len; i++) ir_graph_push_function(&ir, graph, &graph->nodes[order[i].source_index]);

  for (size_t i = 0; i < ordered_len; i++) {
    const ZProgramGraphNode *source = &graph->nodes[order[i].source_index];
    if (!ir_graph_collect_function_locals(&ir, &ir.functions[i], graph, source)) {
      ir_free_function_order(order, stable_ids, ordered_len);
      return ir;
    }
  }

  if (source_helper_ir && !z_program_graph_steal_source_std_helpers(&ir, source_helper_ir)) {
    ir_mark_unsupported(&ir, "typed graph MIR std helper import failed", 1, 1, "unmappable std helper MIR");
    ir_free_function_order(order, stable_ids, ordered_len);
    return ir;
  }

  for (size_t i = 0; i < ordered_len; i++) {
    const ZProgramGraphNode *source = &graph->nodes[order[i].source_index];
    if (!ir_graph_lower_function_body(&ir, &ir.functions[i], graph, source)) {
      ir_free_function_order(order, stable_ids, ordered_len);
      return ir;
    }
  }
  ir.direct_function_count = ir.function_len;
  bool has_export = false;
  for (size_t i = 0; i < ir.function_len; i++) {
    if (ir.functions[i].is_exported) {
      has_export = true;
      ir.direct_export_count++;
    }
  }
  if (!has_export) {
    ir_mark_unsupported(&ir, "typed graph MIR requires at least one exported C ABI entry function", 1, 1, "no exported function");
  } else if (!z_mir_verify_direct_contracts(&ir)) {
    ir_free_function_order(order, stable_ids, ordered_len);
    return ir;
  }
  ir_free_function_order(order, stable_ids, ordered_len);
  (void)input;
  return ir;
}

IrProgram z_lower_program_graph_with_source(const ZProgramGraph *graph, const SourceInput *input, const ZTargetInfo *target) {
  return ir_lower_program_graph_with_source_helpers(graph, input, target, NULL);
}

static bool ir_graph_lower_checked_program(const ZProgramGraph *graph, const char *path, const ZTargetInfo *target, Program *program, SourceInput *input, ZDiag *diag) {
  bool ok = z_program_graph_lower_to_program_with_source(graph, path, program, input, diag);
  if (ok) {
    z_set_check_target(target);
    ok = z_check_program(program, diag);
  }
  if (!ok) {
    if (input && input->source_file) z_map_source_diag(input, diag);
    if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : path;
  }
  return ok;
}

bool z_program_graph_prepare_artifact_mir_input(const char *artifact_path, const ZTargetInfo *target, Program *program, SourceInput *input, IrProgram *ir, ZProgramGraphArtifactSource *source, ZDiag *diag) {
  if (!ir) return false;
  ZProgramGraph graph = {0};
  if (!z_program_graph_load(artifact_path, &graph, diag)) return false;

  z_program_graph_seed_source_metadata(input, &graph);
  IrProgram graph_ir = z_lower_program_graph_with_source(&graph, input, target);
  bool graph_mir_valid = graph_ir.mir_valid;
  if (graph_mir_valid && ir_graph_requires_source_std_ast_mir(&graph)) graph_mir_valid = false;
  bool source_std_helpers_used = false;
  if (graph_mir_valid) {
    *ir = graph_ir;
    if (!ir_graph_lower_checked_program(&graph, artifact_path, target, program, input, diag)) {
      z_free_ir_program(ir);
      z_program_graph_free(&graph);
      return false;
    }
  } else {
    if (!ir_graph_lower_checked_program(&graph, artifact_path, target, program, input, diag)) {
      z_free_ir_program(&graph_ir);
      z_program_graph_free(&graph);
      return false;
    }
    size_t appended_std_functions = 0;
    if (!z_program_graph_append_source_std_functions(program, &appended_std_functions, diag) || appended_std_functions == 0) {
      if (diag && diag->code == 0) ir_graph_init_lowering_diag(diag, input, target, NULL, NULL, &graph_ir, artifact_path);
      z_free_ir_program(&graph_ir);
      if (input && input->source_file) z_map_source_diag(input, diag);
      if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : artifact_path;
      z_program_graph_free(&graph);
      return false;
    }
    source_std_helpers_used = true;
    z_free_ir_program(&graph_ir);
    if (!z_check_program(program, diag)) {
      if (input && input->source_file) z_map_source_diag(input, diag);
      if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : artifact_path;
      z_program_graph_free(&graph);
      return false;
    }
    IrProgram source_ir = z_lower_program_with_source(program, input, target);
    if (!source_ir.mir_valid) {
      z_free_ir_program(&source_ir);
      z_program_graph_free(&graph);
      return false;
    }
    IrProgram helper_graph_ir = ir_lower_program_graph_with_source_helpers(&graph, input, target, &source_ir);
    if (helper_graph_ir.mir_valid) {
      *ir = helper_graph_ir;
      graph_mir_valid = true;
      z_free_ir_program(&source_ir);
    } else {
      z_free_ir_program(&helper_graph_ir);
      z_free_ir_program(&source_ir);
      *ir = z_lower_program_with_source(program, input, target);
      if (!ir->mir_valid) {
        z_free_ir_program(ir);
        z_program_graph_free(&graph);
        return false;
      }
    }
  }
  if (input) {
    z_program_graph_seed_source_metadata_facts(input, &graph);
    input->program_graph_hash = z_strdup(graph.graph_hash ? graph.graph_hash : "");
    input->program_graph_module_identity = z_strdup(graph.module_identity ? graph.module_identity : "");
  }
  if (source) {
    source->artifact = artifact_path;
    source->graph_hash = input ? input->program_graph_hash : "";
    source->module_identity = input ? input->program_graph_module_identity : "";
    source->lowering = graph_mir_valid ? "typed-program-graph-mir" : "program-graph-ast-mir";
    source->canonical_source = graph.canonical_source;
    source->source_std_helpers_used = source_std_helpers_used;
  }
  z_program_graph_free(&graph);
  return true;
}

bool z_program_graph_prepare_repository_store_mir_input(const char *store_path, const ZTargetInfo *target, const char *emit_kind, const char *requested_backend, Program *program, SourceInput *input, IrProgram *ir, ZProgramGraphArtifactSource *source, ZDiag *diag) {
  if (!ir) return false;
  ZProgramGraphStore store;
  if (!z_program_graph_store_load_path(store_path, &store, diag)) return false;

  z_program_graph_seed_source_metadata(input, &store.graph);
  IrProgram graph_ir = z_lower_program_graph_with_source(&store.graph, input, target);
  bool graph_mir_valid = graph_ir.mir_valid;
  if (graph_mir_valid && ir_graph_requires_source_std_ast_mir(&store.graph)) graph_mir_valid = false;
  bool source_std_helpers_used = false;
  if (graph_mir_valid) {
    *ir = graph_ir;
    if (!ir_graph_lower_checked_program(&store.graph, store_path, target, program, input, diag)) {
      z_free_ir_program(ir);
      z_program_graph_store_free(&store);
      return false;
    }
  } else {
    if (!ir_graph_lower_checked_program(&store.graph, store_path, target, program, input, diag)) {
      z_free_ir_program(&graph_ir);
      z_program_graph_store_free(&store);
      return false;
    }
    size_t appended_std_functions = 0;
    if (!z_program_graph_append_source_std_functions(program, &appended_std_functions, diag) || appended_std_functions == 0) {
      if (diag && diag->code == 0) ir_graph_init_lowering_diag(diag, input, target, emit_kind, requested_backend, &graph_ir, store_path);
      z_free_ir_program(&graph_ir);
      if (input && input->source_file) z_map_source_diag(input, diag);
      if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : store_path;
      z_program_graph_store_free(&store);
      return false;
    }
    source_std_helpers_used = true;
    z_free_ir_program(&graph_ir);
    if (!z_check_program(program, diag)) {
      if (input && input->source_file) z_map_source_diag(input, diag);
      if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : store_path;
      z_program_graph_store_free(&store);
      return false;
    }
    IrProgram source_ir = z_lower_program_with_source(program, input, target);
    if (!source_ir.mir_valid) {
      ir_graph_init_lowering_diag(diag, input, target, emit_kind, requested_backend, &source_ir, store_path);
      z_free_ir_program(&source_ir);
      z_program_graph_store_free(&store);
      return false;
    }
    IrProgram helper_graph_ir = ir_lower_program_graph_with_source_helpers(&store.graph, input, target, &source_ir);
    if (helper_graph_ir.mir_valid) {
      *ir = helper_graph_ir;
      graph_mir_valid = true;
      z_free_ir_program(&source_ir);
    } else {
      z_free_ir_program(&helper_graph_ir);
      z_free_ir_program(&source_ir);
      *ir = z_lower_program_with_source(program, input, target);
      if (!ir->mir_valid) {
        ir_graph_init_lowering_diag(diag, input, target, emit_kind, requested_backend, ir, store_path);
        z_free_ir_program(ir);
        z_program_graph_store_free(&store);
        return false;
      }
    }
  }
  if (input) {
    z_program_graph_seed_source_metadata_facts(input, &store.graph);
    input->program_graph_hash = z_strdup(store.graph.graph_hash ? store.graph.graph_hash : "");
    input->program_graph_module_identity = z_strdup(store.graph.module_identity ? store.graph.module_identity : "");
  }
  if (source) {
    source->artifact = store_path;
    source->graph_hash = input ? input->program_graph_hash : "";
    source->module_identity = input ? input->program_graph_module_identity : "";
    source->lowering = graph_mir_valid ? "typed-program-graph-mir" : "program-graph-ast-mir";
    source->source_projection_state = z_program_graph_projection_state_label(&store, target, NULL, NULL, NULL);
    source->canonical_source = store.graph.canonical_source;
    source->source_std_helpers_used = source_std_helpers_used;
  }
  z_program_graph_store_free(&store);
  return true;
}
