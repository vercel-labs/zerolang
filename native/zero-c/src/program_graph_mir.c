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
  snprintf(ir->mir_help, sizeof(ir->mir_help), "use graph check to inspect unsupported graph constructs or choose another supported target");
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
  snprintf(diag->help, sizeof(diag->help), "%s", ir && ir->mir_help[0] ? ir->mir_help : "use graph check to inspect unsupported graph constructs or choose another supported target");
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

static IrValue *ir_new_maybe_byte_view_literal(IrProgram *ir, bool has, IrValue *view, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_BYTE_VIEW_LITERAL, IR_TYPE_MAYBE_BYTE_VIEW, line, column);
  value->data_len = has ? 1u : 0u;
  value->element_type = view && view->element_type != IR_TYPE_UNSUPPORTED ? view->element_type : IR_TYPE_U8;
  value->left = view;
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

static bool ir_add_readonly_data(IrProgram *ir, const unsigned char *bytes, unsigned len, int line, int column, unsigned *out_offset) {
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
  bool added = ir_add_readonly_data(ir, nul_terminated, len + 1, line, column, &offset);
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
      if (!ir_graph_collect_block_locals(ir, fun, graph, ir_graph_ordered_node(graph, stmt->id, "body", 0))) return false;
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

static bool ir_graph_lower_expr_for_type(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrTypeKind target_type, bool allow_i32_default, int line, int column, IrValue **out) {
  *out = NULL;
  if (!expr && allow_i32_default && target_type == IR_TYPE_I32) {
    *out = ir_new_integer_literal_value(ir, IR_TYPE_I32, 0, line, column);
    return true;
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
  if (target_type == IR_TYPE_MAYBE_BYTE_VIEW && expr && ir_type_kind(ir_graph_node_type(graph, expr)) == IR_TYPE_BYTE_VIEW) {
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
  IrTypeKind type = ir_type_kind(expr ? expr->type : NULL);
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

static bool ir_graph_lower_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_CALL && (ir_compare_op(expr->name, &(IrCompareOp){0}) || ir_binary_op(expr->name, &(IrBinaryOp){0}))) {
    return ir_graph_lower_binary_call(graph, ir, fun, expr, out);
  }

  char *qualified = ir_graph_expr_qualified_name(graph, expr);
  const char *callee_name = qualified && qualified[0] ? qualified : expr->name;
  size_t arg_count = ir_graph_edge_count(graph, expr->id, "arg");
  if (ir_text_eq(callee_name, "std.args.len") && arg_count == 0) {
    *out = ir_new_value(ir, IR_VALUE_ARGS_LEN, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    free(qualified);
    return true;
  }
  if (ir_text_eq(callee_name, "std.args.get") && arg_count == 1) {
    IrValue *index = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_USIZE, &index)) {
      free(qualified);
      return false;
    }
    if (!ir_type_is_value(index->type)) {
      ir_free_value(index);
      free(qualified);
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.args.get index must be an integer value", "non-integer index");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_GET, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = index;
    *out = value;
    free(qualified);
    return true;
  }
  if (ir_text_eq(callee_name, "std.mem.span") && arg_count == 1) {
    bool ok = ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), out);
    free(qualified);
    return ok;
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
    IrValue *value = NULL;
    const ZProgramGraphNode *expr = ir_graph_ordered_node(graph, stmt->id, "expr", 0);
    if (!ir_graph_lower_expr_for_type(graph, ir, fun, expr, local->type, false, ir_graph_line(stmt), ir_graph_column(stmt), &value)) return false;
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_LOCAL_SET, .local_index = local->index, .value = value, .line = ir_graph_line(stmt), .column = ir_graph_column(stmt)});
    ir_active_local_push(ir, stmt->name);
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

IrProgram z_lower_program_graph_with_source(const ZProgramGraph *graph, const SourceInput *input, const ZTargetInfo *target) {
  IrProgram ir = {0};
  ir.target = target;
  ir.mir_valid = true;
  ir.mir_line = 1;
  ir.mir_column = 1;
  snprintf(ir.mir_expected, sizeof(ir.mir_expected), "typed program graph MIR subset");
  snprintf(ir.mir_help, sizeof(ir.mir_help), "use graph check to inspect unsupported graph constructs or choose another supported target");
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

  bool has_export = false;
  for (size_t i = 0; i < ir.function_len; i++) {
    const ZProgramGraphNode *source = &graph->nodes[order[i].source_index];
    if (!ir_graph_collect_function_locals(&ir, &ir.functions[i], graph, source)) {
      ir_free_function_order(order, stable_ids, ordered_len);
      return ir;
    }
  }

  for (size_t i = 0; i < ir.function_len; i++) {
    const ZProgramGraphNode *source = &graph->nodes[order[i].source_index];
    if (ir.functions[i].is_exported) {
      has_export = true;
      ir.direct_export_count++;
    }
    if (!ir_graph_lower_function_body(&ir, &ir.functions[i], graph, source)) {
      ir_free_function_order(order, stable_ids, ordered_len);
      return ir;
    }
  }
  ir.direct_function_count = ir.function_len;
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

bool z_program_graph_prepare_artifact_mir_input(const char *artifact_path, const ZTargetInfo *target, Program *program, SourceInput *input, IrProgram *ir, ZProgramGraphArtifactSource *source, ZDiag *diag) {
  if (!ir) return false;
  ZProgramGraph graph = {0};
  if (!z_program_graph_load(artifact_path, &graph, diag)) return false;

  bool ok = z_program_graph_lower_to_program_with_source(&graph, artifact_path, program, input, diag);
  if (ok) {
    z_set_check_target(target);
    ok = z_check_program(program, diag);
  }
  if (!ok) {
    if (input && input->source_file) z_map_source_diag(input, diag);
    if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : artifact_path;
    z_program_graph_free(&graph);
    return false;
  }

  z_program_graph_seed_source_metadata(input, &graph);
  IrProgram graph_ir = z_lower_program_graph_with_source(&graph, input, target);
  bool graph_mir_valid = graph_ir.mir_valid;
  if (graph_mir_valid) {
    *ir = graph_ir;
  } else {
    size_t appended_std_functions = 0;
    if (!z_program_graph_append_source_std_functions(program, &appended_std_functions, diag) || appended_std_functions == 0) {
      if (diag && diag->code == 0) ir_graph_init_lowering_diag(diag, input, target, NULL, NULL, &graph_ir, artifact_path);
      z_free_ir_program(&graph_ir);
      if (input && input->source_file) z_map_source_diag(input, diag);
      if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : artifact_path;
      z_program_graph_free(&graph);
      return false;
    }
    z_free_ir_program(&graph_ir);
    z_set_check_target(target);
    if (!z_check_program(program, diag)) {
      if (input && input->source_file) z_map_source_diag(input, diag);
      if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : artifact_path;
      z_program_graph_free(&graph);
      return false;
    }
    *ir = z_lower_program_with_source(program, input, target);
    if (!ir->mir_valid) {
      z_free_ir_program(ir);
      z_program_graph_free(&graph);
      return false;
    }
  }
  if (input) {
    input->program_graph_hash = z_strdup(graph.graph_hash ? graph.graph_hash : "");
    input->program_graph_module_identity = z_strdup(graph.module_identity ? graph.module_identity : "");
  }
  if (source) {
    source->artifact = artifact_path;
    source->graph_hash = input ? input->program_graph_hash : "";
    source->module_identity = input ? input->program_graph_module_identity : "";
    source->lowering = graph_mir_valid ? "typed-program-graph-mir" : "program-graph-ast-mir";
    source->canonical_source = graph.canonical_source;
  }
  z_program_graph_free(&graph);
  return true;
}

bool z_program_graph_prepare_repository_store_mir_input(const char *store_path, const ZTargetInfo *target, const char *emit_kind, const char *requested_backend, Program *program, SourceInput *input, IrProgram *ir, ZProgramGraphArtifactSource *source, ZDiag *diag) {
  if (!ir) return false;
  ZProgramGraphStore store;
  if (!z_program_graph_store_load_path(store_path, &store, diag)) return false;

  bool ok = z_program_graph_lower_to_program_with_source(&store.graph, store_path, program, input, diag);
  if (ok) {
    z_set_check_target(target);
    ok = z_check_program(program, diag);
  }
  if (!ok) {
    if (input && input->source_file) z_map_source_diag(input, diag);
    if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : store_path;
    z_program_graph_store_free(&store);
    return false;
  }

  z_program_graph_seed_source_metadata(input, &store.graph);
  IrProgram graph_ir = z_lower_program_graph_with_source(&store.graph, input, target);
  bool graph_mir_valid = graph_ir.mir_valid;
  if (graph_mir_valid) {
    *ir = graph_ir;
  } else {
    size_t appended_std_functions = 0;
    if (!z_program_graph_append_source_std_functions(program, &appended_std_functions, diag) || appended_std_functions == 0) {
      if (diag && diag->code == 0) ir_graph_init_lowering_diag(diag, input, target, emit_kind, requested_backend, &graph_ir, store_path);
      z_free_ir_program(&graph_ir);
      if (input && input->source_file) z_map_source_diag(input, diag);
      if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : store_path;
      z_program_graph_store_free(&store);
      return false;
    }
    z_free_ir_program(&graph_ir);
    z_set_check_target(target);
    if (!z_check_program(program, diag)) {
      if (input && input->source_file) z_map_source_diag(input, diag);
      if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : store_path;
      z_program_graph_store_free(&store);
      return false;
    }
    *ir = z_lower_program_with_source(program, input, target);
    if (!ir->mir_valid) {
      ir_graph_init_lowering_diag(diag, input, target, emit_kind, requested_backend, ir, store_path);
      z_free_ir_program(ir);
      z_program_graph_store_free(&store);
      return false;
    }
  }
  if (input) {
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
  }
  z_program_graph_store_free(&store);
  return true;
}
