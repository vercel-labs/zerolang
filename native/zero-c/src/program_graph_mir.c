#include "zero.h"
#include "mir_binary.h"
#include "mir_verify.h"
#include "program_graph_build.h"
#include "program_graph_c_import.h"
#include "program_graph_format.h"
#include "program_graph.h"
#include "program_graph_lower.h"
#include "program_graph_projection.h"
#include "program_graph_size.h"
#include "program_graph_store.h"
#include "std_source.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
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
static IrTypeKind ir_type_kind(const char *type), ir_graph_type_kind(const ZProgramGraph *graph, const char *type), ir_graph_view_element_type_for_type(const ZProgramGraph *graph, const char *type), ir_graph_maybe_scalar_element_type(const ZProgramGraph *graph, const char *type);
static bool ir_graph_parse_fixed_array_type(const ZProgramGraph *graph, const char *type, unsigned *out_len, IrTypeKind *out_element), ir_graph_type_name_is_mutable_byte_view(const ZProgramGraph *graph, const char *type);
static bool ir_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static long long ir_graph_now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

static void ir_graph_set_mapped_mir_cache_facts(SourceInput *input, const ZMirBinaryCacheFacts *facts, bool reused, bool written, bool codegen_immediate, bool program_reconstructed) {
  if (!input || !facts || !facts->hit) return;
  free(input->mapped_mir_cache_path);
  input->mapped_mir_cache_path = z_strdup(facts->path);
  input->mapped_mir_cache_bytes = facts->byte_len;
  input->mapped_mir_cache_hit = reused; input->mapped_mir_cache_written = written;
  input->mapped_mir_memory_mapped = facts->mapped; input->mapped_mir_borrowed_storage = facts->borrowed_storage;
  input->mapped_mir_codegen_immediate = codegen_immediate; input->mapped_mir_program_reconstructed = program_reconstructed;
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
  if (ir_text_eq(type, "ProcChild")) return IR_TYPE_I32;
  if (ir_text_eq(type, "Net") || ir_text_eq(type, "HttpClient") || ir_text_eq(type, "Conn") || ir_text_eq(type, "Listener") || ir_text_eq(type, "HttpServer")) return IR_TYPE_I32;
  if (ir_text_eq(type, "HttpMethod")) return IR_TYPE_U32;
  if (ir_text_eq(type, "HttpResult")) return IR_TYPE_U64;
  if (ir_text_eq(type, "HttpError")) return IR_TYPE_U32;
  if (ir_text_eq(type, "HttpHeaderValue")) return IR_TYPE_U64;
  if (ir_text_eq(type, "Fs") || ir_text_eq(type, "File") || ir_text_eq(type, "owned<File>")) return IR_TYPE_I32;
  if (ir_text_eq(type, "String") || ir_text_eq(type, "Span<const u8>") || ir_text_eq(type, "Address") || ir_text_eq(type, "ByteBuf") || ir_text_eq(type, "owned<ByteBuf>") || ir_span_type_element(type, NULL, NULL)) return IR_TYPE_BYTE_VIEW;
  if (ir_text_eq(type, "FixedBufAlloc")) return IR_TYPE_ALLOC;
  if (ir_text_eq(type, "Vec")) return IR_TYPE_VEC;
  if (ir_text_eq(type, "BufferedReader") || ir_text_eq(type, "BufferedWriter")) return IR_TYPE_BYTE_VIEW;
  if (ir_text_eq(type, "Maybe<MutSpan<u8>>") || ir_text_eq(type, "Maybe<Span<u8>>") || ir_text_eq(type, "Maybe<String>") || ir_text_eq(type, "Maybe<owned<ByteBuf>>")) return IR_TYPE_MAYBE_BYTE_VIEW;
  if (ir_text_eq(type, "Maybe<JsonDoc>") || ir_text_eq(type, "Maybe<Bool>") || ir_text_eq(type, "Maybe<u8>") || ir_text_eq(type, "Maybe<u16>") || ir_text_eq(type, "Maybe<usize>") || ir_text_eq(type, "Maybe<i32>") || ir_text_eq(type, "Maybe<u32>") || ir_text_eq(type, "Maybe<i64>") || ir_text_eq(type, "Maybe<u64>") || ir_text_eq(type, "Maybe<Duration>") || ir_text_eq(type, "Maybe<Conn>") || ir_text_eq(type, "Maybe<Listener>") || ir_text_eq(type, "Maybe<owned<File>>")) return IR_TYPE_MAYBE_SCALAR;
  return IR_TYPE_UNSUPPORTED;
}

static bool ir_type_is_value(IrTypeKind type) {
  return type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_I32 || type == IR_TYPE_U32 || type == IR_TYPE_I64 || type == IR_TYPE_U64;
}

static const char *ir_type_source_name(IrTypeKind type) {
  switch (type) {
    case IR_TYPE_BOOL: return "Bool";
    case IR_TYPE_U8: return "u8";
    case IR_TYPE_U16: return "u16";
    case IR_TYPE_USIZE: return "usize";
    case IR_TYPE_I32: return "i32";
    case IR_TYPE_U32: return "u32";
    case IR_TYPE_I64: return "i64";
    case IR_TYPE_U64: return "u64";
    default: return NULL;
  }
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

static unsigned ir_graph_error_code_for_name(const char *name) {
  if (!name) return IR_ERROR_UNKNOWN;
  if (ir_text_eq(name, "NotFound")) return IR_ERROR_NOT_FOUND;
  if (ir_text_eq(name, "TooLarge")) return IR_ERROR_TOO_LARGE;
  if (ir_text_eq(name, "Io")) return IR_ERROR_IO;
  return IR_ERROR_UNKNOWN;
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

typedef enum {
  IR_VEC_HELPER_NONE = 0,
  IR_VEC_HELPER_LEN,
  IR_VEC_HELPER_CAPACITY,
  IR_VEC_HELPER_REMAINING,
  IR_VEC_HELPER_IS_EMPTY,
  IR_VEC_HELPER_IS_FULL,
  IR_VEC_HELPER_BYTES
} IrVecHelper;

static IrVecHelper ir_std_mem_vec_helper(const char *callee_name) {
  if (ir_text_eq(callee_name, "std.mem.vecLen")) return IR_VEC_HELPER_LEN;
  if (ir_text_eq(callee_name, "std.mem.vecCapacity")) return IR_VEC_HELPER_CAPACITY;
  if (ir_text_eq(callee_name, "std.mem.vecRemaining")) return IR_VEC_HELPER_REMAINING;
  if (ir_text_eq(callee_name, "std.mem.vecIsEmpty")) return IR_VEC_HELPER_IS_EMPTY;
  if (ir_text_eq(callee_name, "std.mem.vecIsFull")) return IR_VEC_HELPER_IS_FULL;
  if (ir_text_eq(callee_name, "std.mem.vecBytes")) return IR_VEC_HELPER_BYTES;
  return IR_VEC_HELPER_NONE;
}

static IrValue *ir_new_vec_helper_value(IrProgram *ir, IrVecHelper helper, size_t local_index, int line, int column) {
  if (helper == IR_VEC_HELPER_BYTES) {
    IrValue *bytes = ir_new_value(ir, IR_VALUE_VEC_BYTES, IR_TYPE_BYTE_VIEW, line, column);
    bytes->local_index = local_index;
    bytes->element_type = IR_TYPE_U8;
    return bytes;
  }
  if (helper == IR_VEC_HELPER_CAPACITY) {
    IrValue *capacity = ir_new_value(ir, IR_VALUE_VEC_CAPACITY, IR_TYPE_USIZE, line, column);
    capacity->local_index = local_index;
    return capacity;
  }
  IrValue *len = ir_new_value(ir, IR_VALUE_VEC_LEN, IR_TYPE_USIZE, line, column);
  len->local_index = local_index;
  if (helper == IR_VEC_HELPER_LEN) return len;
  if (helper == IR_VEC_HELPER_IS_EMPTY) return ir_new_compare_value(ir, IR_CMP_EQ, len, ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 0, line, column), line, column);
  IrValue *capacity = ir_new_value(ir, IR_VALUE_VEC_CAPACITY, IR_TYPE_USIZE, line, column);
  capacity->local_index = local_index;
  if (helper == IR_VEC_HELPER_REMAINING) return ir_new_binary_value(ir, IR_BIN_SUB, IR_TYPE_USIZE, capacity, len, line, column);
  if (helper == IR_VEC_HELPER_IS_FULL) return ir_new_compare_value(ir, IR_CMP_EQ, len, capacity, line, column);
  return len;
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

static char *ir_graph_reference_inner_type_alloc(const char *type, bool *out_is_mutable) {
  size_t len = type ? strlen(type) : 0;
  const char *mut_prefix = "mutref<";
  size_t mut_prefix_len = strlen(mut_prefix);
  if (len > mut_prefix_len + 1 && strncmp(type, mut_prefix, mut_prefix_len) == 0 && type[len - 1] == '>') {
    if (out_is_mutable) *out_is_mutable = true;
    return z_strndup(type + mut_prefix_len, len - mut_prefix_len - 1);
  }
  const char *ref_prefix = "ref<";
  size_t ref_prefix_len = strlen(ref_prefix);
  if (len > ref_prefix_len + 1 && strncmp(type, ref_prefix, ref_prefix_len) == 0 && type[len - 1] == '>') {
    if (out_is_mutable) *out_is_mutable = false;
    return z_strndup(type + ref_prefix_len, len - ref_prefix_len - 1);
  }
  return NULL;
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

static bool ir_graph_make_json_error_label_value(IrProgram *ir, IrValue *code, bool expected, int line, int column, IrValue **out) {
  IrValue *value = ir_new_value(ir, IR_VALUE_JSON_ERROR_LABEL, IR_TYPE_BYTE_VIEW, line, column);
  value->left = code;
  value->int_value = expected ? 1u : 0u;
  value->element_type = IR_TYPE_U8;
  for (unsigned i = 0; i < 4; i++) {
    const char *label = "unknown";
    if (i == 0) label = expected ? "none" : "ok";
    else if (i == 1) label = expected ? "valid-json" : "invalid";
    else if (i == 2) label = expected ? "end-of-input" : "trailing";
    IrValue *literal = NULL;
    if (!ir_make_string_literal_value(ir, label, line, column, &literal)) {
      ir_free_value(value);
      return false;
    }
    ir_value_push_arg(ir, value, literal);
  }
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
  char **generic_param_names;
  char **generic_arg_types;
  size_t generic_binding_len;
  bool owns_name;
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
  for (size_t i = 0; order && i < len; i++) {
    if (order[i].owns_name) free((char *)order[i].name);
    for (size_t j = 0; j < order[i].generic_binding_len; j++) {
      free(order[i].generic_param_names[j]);
      free(order[i].generic_arg_types[j]);
    }
    free(order[i].generic_param_names);
    free(order[i].generic_arg_types);
  }
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

/*
 * Graph lowering walks node and edge relations constantly; linear scans over
 * the whole table per lookup made lowering quadratic in graph size. This
 * index sorts node, edge, and function-name handles once per graph so every
 * lookup touches only its own run. The cache stores table indexes rather
 * than captured pointers and revalidates against the graph identity, table
 * addresses, lengths, and graph hash, so a rebuilt graph that happens to
 * reuse an address can never serve stale entries. Runs preserve table order
 * through the index tiebreak, keeping lowering output byte-identical.
 */
typedef struct {
  const ZProgramGraph *graph;
  const ZProgramGraphNode *nodes;
  const ZProgramGraphEdge *edges;
  size_t node_len;
  size_t edge_len;
  char *graph_hash;
  size_t *nodes_by_id;
  size_t *parent_edges;
  size_t parent_len;
  size_t *child_edges;
  size_t child_len;
  size_t *functions_by_name;
  size_t function_len;
} IrGraphIndex;

static IrGraphIndex ir_graph_index_cache;
static const ZProgramGraph *ir_graph_index_sort_graph;

static int ir_text_cmp(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "");
}

static int ir_graph_index_tiebreak(const void *left, const void *right, int cmp) {
  if (cmp != 0) return cmp;
  return *(const size_t *)left < *(const size_t *)right ? -1 : 1;
}

static int ir_graph_index_node_id_cmp(const void *left, const void *right) {
  const ZProgramGraphNode *nodes = ir_graph_index_sort_graph->nodes;
  return ir_graph_index_tiebreak(left, right, ir_text_cmp(nodes[*(const size_t *)left].id, nodes[*(const size_t *)right].id));
}

static int ir_graph_index_parent_cmp(const void *left, const void *right) {
  const ZProgramGraphEdge *edges = ir_graph_index_sort_graph->edges;
  return ir_graph_index_tiebreak(left, right, ir_text_cmp(edges[*(const size_t *)left].to, edges[*(const size_t *)right].to));
}

static int ir_graph_index_child_cmp(const void *left, const void *right) {
  const ZProgramGraphEdge *edges = ir_graph_index_sort_graph->edges;
  return ir_graph_index_tiebreak(left, right, ir_text_cmp(edges[*(const size_t *)left].from, edges[*(const size_t *)right].from));
}

static int ir_graph_index_function_cmp(const void *left, const void *right) {
  const ZProgramGraphNode *nodes = ir_graph_index_sort_graph->nodes;
  return ir_graph_index_tiebreak(left, right, ir_text_cmp(nodes[*(const size_t *)left].name, nodes[*(const size_t *)right].name));
}

static const IrGraphIndex *ir_graph_index(const ZProgramGraph *graph) {
  IrGraphIndex *cache = &ir_graph_index_cache;
  if (cache->graph == graph &&
      cache->nodes == graph->nodes &&
      cache->edges == graph->edges &&
      cache->node_len == graph->node_len &&
      cache->edge_len == graph->edge_len &&
      ir_text_eq(cache->graph_hash, graph->graph_hash)) {
    return cache;
  }
  free(cache->graph_hash);
  free(cache->nodes_by_id);
  free(cache->parent_edges);
  free(cache->child_edges);
  free(cache->functions_by_name);
  *cache = (IrGraphIndex){
    .graph = graph,
    .nodes = graph->nodes,
    .edges = graph->edges,
    .node_len = graph->node_len,
    .edge_len = graph->edge_len,
    .graph_hash = z_strdup(graph->graph_hash ? graph->graph_hash : ""),
    .nodes_by_id = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(size_t)),
    .parent_edges = z_checked_calloc(graph->edge_len ? graph->edge_len : 1, sizeof(size_t)),
    .child_edges = z_checked_calloc(graph->edge_len ? graph->edge_len : 1, sizeof(size_t)),
    .functions_by_name = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(size_t)),
  };
  for (size_t i = 0; i < graph->node_len; i++) {
    cache->nodes_by_id[i] = i;
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_FUNCTION) cache->functions_by_name[cache->function_len++] = i;
  }
  for (size_t i = 0; i < graph->edge_len; i++) {
    if (graph->edges[i].target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) continue;
    cache->parent_edges[cache->parent_len] = i;
    cache->child_edges[cache->child_len] = i;
    cache->parent_len++;
    cache->child_len++;
  }
  ir_graph_index_sort_graph = graph;
  qsort(cache->nodes_by_id, graph->node_len, sizeof(size_t), ir_graph_index_node_id_cmp);
  qsort(cache->parent_edges, cache->parent_len, sizeof(size_t), ir_graph_index_parent_cmp);
  qsort(cache->child_edges, cache->child_len, sizeof(size_t), ir_graph_index_child_cmp);
  qsort(cache->functions_by_name, cache->function_len, sizeof(size_t), ir_graph_index_function_cmp);
  return cache;
}

typedef const char *(*IrGraphIndexKeyFn)(const ZProgramGraph *graph, size_t index);

static const char *ir_graph_index_node_id_key(const ZProgramGraph *graph, size_t index) { return graph->nodes[index].id; }
static const char *ir_graph_index_node_name_key(const ZProgramGraph *graph, size_t index) { return graph->nodes[index].name; }
static const char *ir_graph_index_edge_to_key(const ZProgramGraph *graph, size_t index) { return graph->edges[index].to; }
static const char *ir_graph_index_edge_from_key(const ZProgramGraph *graph, size_t index) { return graph->edges[index].from; }

/* Returns the [start, start+run_len) span of entries whose key equals text. */
static void ir_graph_index_run(const ZProgramGraph *graph, const size_t *items, size_t len, IrGraphIndexKeyFn key, const char *text, size_t *start, size_t *run_len) {
  size_t low = 0;
  size_t high = len;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (ir_text_cmp(key(graph, items[mid]), text) < 0) low = mid + 1;
    else high = mid;
  }
  size_t first = low;
  while (low < len && ir_text_eq(key(graph, items[low]), text ? text : "")) low++;
  *start = first;
  *run_len = low - first;
}

static void ir_graph_child_edge_run(const ZProgramGraph *graph, const IrGraphIndex **index, const char *from, size_t *start, size_t *run_len) {
  *start = 0;
  *run_len = 0;
  if (!graph || !from) return;
  *index = ir_graph_index(graph);
  ir_graph_index_run(graph, (*index)->child_edges, (*index)->child_len, ir_graph_index_edge_from_key, from, start, run_len);
}

static const ZProgramGraphNode *ir_graph_find_node(const ZProgramGraph *graph, const char *id) {
  if (!graph || !id) return NULL;
  const IrGraphIndex *index = ir_graph_index(graph);
  size_t start = 0;
  size_t run_len = 0;
  ir_graph_index_run(graph, index->nodes_by_id, index->node_len, ir_graph_index_node_id_key, id, &start, &run_len);
  return run_len ? &graph->nodes[index->nodes_by_id[start]] : NULL;
}

static const ZProgramGraphEdge *ir_graph_ordered_edge(const ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  if (!kind) return NULL;
  const IrGraphIndex *index = NULL;
  size_t start = 0;
  size_t run_len = 0;
  ir_graph_child_edge_run(graph, &index, from, &start, &run_len);
  for (size_t i = 0; i < run_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[index->child_edges[start + i]];
    if (edge->order == order && ir_text_eq(edge->kind, kind)) return edge;
  }
  return NULL;
}

static const ZProgramGraphNode *ir_graph_ordered_node(const ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  const ZProgramGraphEdge *edge = ir_graph_ordered_edge(graph, from, kind, order);
  return edge ? ir_graph_find_node(graph, edge->to) : NULL;
}

static const ZProgramGraphEdge *ir_graph_next_edge_by_order(const ZProgramGraph *graph, const char *from, const char *kind, bool have_last, size_t last_order) {
  if (!kind) return NULL;
  const ZProgramGraphEdge *best = NULL;
  const IrGraphIndex *index = NULL;
  size_t start = 0;
  size_t run_len = 0;
  ir_graph_child_edge_run(graph, &index, from, &start, &run_len);
  for (size_t i = 0; i < run_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[index->child_edges[start + i]];
    if (!ir_text_eq(edge->kind, kind) || (have_last && edge->order <= last_order)) continue;
    if (!best || edge->order < best->order) best = edge;
  }
  return best;
}

static size_t ir_graph_edge_count(const ZProgramGraph *graph, const char *from, const char *kind) {
  if (!kind) return 0;
  size_t count = 0;
  const IrGraphIndex *index = NULL;
  size_t start = 0;
  size_t run_len = 0;
  ir_graph_child_edge_run(graph, &index, from, &start, &run_len);
  for (size_t i = 0; i < run_len; i++) {
    if (ir_text_eq(graph->edges[index->child_edges[start + i]].kind, kind)) count++;
  }
  return count;
}

static const char *ir_graph_node_type(const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  if (!node) return NULL;
  if (node->type && node->type[0]) return node->type;
  if (node->kind == Z_PROGRAM_GRAPH_NODE_CAST && node->name && node->name[0]) return node->name;
  const ZProgramGraphNode *declared = ir_graph_ordered_node(graph, node->id, "declaredType", 0);
  if (declared && declared->type && declared->type[0]) return declared->type;
  const ZProgramGraphNode *ret = ir_graph_ordered_node(graph, node->id, "returnType", 0);
  if (ret && ret->type && ret->type[0]) return ret->type;
  return NULL;
}

static bool ir_graph_type_ident_char(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

static char *ir_graph_bound_type_text_alloc_from_arrays(const char *type, char **param_names, char **arg_types, size_t binding_len) {
  if (!type) return NULL;
  for (size_t i = 0; i < binding_len; i++) {
    if (ir_text_eq(type, param_names[i])) return z_strdup(arg_types[i]);
  }
  ZBuf buf;
  zbuf_init(&buf);
  bool changed = false;
  for (size_t offset = 0; type[offset];) {
    bool matched = false;
    for (size_t i = 0; i < binding_len; i++) {
      const char *param = param_names[i];
      const char *arg = arg_types[i];
      size_t param_len = param ? strlen(param) : 0;
      if (!param_len || !arg) continue;
      bool left_boundary = offset == 0 || !ir_graph_type_ident_char(type[offset - 1]);
      bool right_boundary = !ir_graph_type_ident_char(type[offset + param_len]);
      if (left_boundary && right_boundary && strncmp(type + offset, param, param_len) == 0) {
        zbuf_append(&buf, arg);
        offset += param_len;
        changed = true;
        matched = true;
        break;
      }
    }
    if (matched) continue;
    zbuf_append_char(&buf, type[offset]);
    offset++;
  }
  if (!changed) {
    free(buf.data);
    return z_strdup(type);
  }
  return buf.data;
}

static char *ir_graph_bound_type_text_alloc(const IrFunction *fun, const char *type) {
  return ir_graph_bound_type_text_alloc_from_arrays(type, fun ? fun->generic_param_names : NULL, fun ? fun->generic_arg_types : NULL, fun ? fun->generic_binding_len : 0);
}

static const char *ir_graph_bound_type_text_from_arrays(const char *type, char **param_names, char **arg_types, size_t binding_len) {
  for (size_t i = 0; type && i < binding_len; i++) {
    if (ir_text_eq(type, param_names[i])) return arg_types[i];
  }
  return type;
}

static char *ir_graph_node_type_for_function_alloc(const ZProgramGraph *graph, const IrFunction *fun, const ZProgramGraphNode *node) {
  return ir_graph_bound_type_text_alloc(fun, ir_graph_node_type(graph, node));
}

static char *ir_graph_stable_id_for_function_name(const ZProgramGraph *graph, const ZProgramGraphNode *function, const char *name);

static size_t ir_graph_function_type_param_count(const ZProgramGraph *graph, const ZProgramGraphNode *function) {
  return ir_graph_edge_count(graph, function ? function->id : NULL, "typeParam");
}

static const ZProgramGraphNode *ir_graph_call_type_arg_node(const ZProgramGraph *graph, const ZProgramGraphNode *call, size_t order) {
  const ZProgramGraphNode *left = ir_graph_ordered_node(graph, call ? call->id : NULL, "left", 0);
  const ZProgramGraphNode *arg = ir_graph_ordered_node(graph, left ? left->id : NULL, "typeArg", order);
  if (arg) return arg;
  return ir_graph_ordered_node(graph, call ? call->id : NULL, "typeArg", order);
}

static size_t ir_graph_call_type_arg_count(const ZProgramGraph *graph, const ZProgramGraphNode *call) {
  const ZProgramGraphNode *left = ir_graph_ordered_node(graph, call ? call->id : NULL, "left", 0);
  size_t count = ir_graph_edge_count(graph, left ? left->id : NULL, "typeArg");
  return count ? count : ir_graph_edge_count(graph, call ? call->id : NULL, "typeArg");
}

static const char *ir_graph_concrete_type_text_for_node(const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  const char *type = ir_graph_node_type(graph, node);
  if (type && type[0]) return type;
  if (node && node->kind == Z_PROGRAM_GRAPH_NODE_LITERAL) {
    IrTypeKind suffix_type = ir_integer_literal_suffix_type(node->value ? node->value : "");
    const char *suffix_name = ir_type_source_name(suffix_type);
    if (suffix_name) return suffix_name;
  }
  return NULL;
}

static const ZProgramGraphNode *ir_graph_find_function_by_name(const ZProgramGraph *graph, const char *name) {
  if (!graph || !name) return NULL;
  const IrGraphIndex *index = ir_graph_index(graph);
  size_t start = 0;
  size_t run_len = 0;
  ir_graph_index_run(graph, index->functions_by_name, index->function_len, ir_graph_index_node_name_key, name, &start, &run_len);
  return run_len ? &graph->nodes[index->functions_by_name[start]] : NULL;
}

static bool ir_graph_has_concrete_function_named(const ZProgramGraph *graph, const char *name) {
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION &&
        !ir_graph_node_is_test_function(node) &&
        ir_graph_function_type_param_count(graph, node) == 0 &&
        ir_text_eq(node->name, name)) {
      return true;
    }
  }
  return false;
}

static bool ir_graph_order_has_name(const IrFunctionOrder *order, size_t len, const char *name) {
  for (size_t i = 0; order && name && i < len; i++) {
    if (ir_text_eq(order[i].name, name)) return true;
  }
  return false;
}

static char *ir_graph_specialized_function_name_for_types(const ZProgramGraphNode *generic, char **arg_types, size_t arg_type_count) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, generic && generic->name ? generic->name : "");
  for (size_t i = 0; i < arg_type_count; i++) {
    const char *type = arg_types && arg_types[i] ? arg_types[i] : "Unknown";
    zbuf_append(&buf, "__");
    for (const char *p = type ? type : "Unknown"; *p; p++) {
      char c = *p;
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') zbuf_append_char(&buf, c);
      else zbuf_append_char(&buf, '_');
    }
  }
  return buf.data;
}

static const ZProgramGraphNode *ir_graph_find_shape(const ZProgramGraph *graph, const char *shape_name) {
  for (size_t i = 0; graph && shape_name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_SHAPE && ir_text_eq(node->name, shape_name)) return node;
  }
  return NULL;
}

static const ZProgramGraphNode *ir_graph_find_type_alias(const ZProgramGraph *graph, const char *name) {
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS && ir_text_eq(node->name, name)) return node;
  }
  return NULL;
}

static void ir_graph_type_arg_vec_free(TypeArgVec *args) {
  if (!args) return;
  for (size_t i = 0; i < args->len; i++) free(args->items[i].type);
  free(args->items);
  *args = (TypeArgVec){0};
}

static void ir_graph_type_arg_vec_push_owned(TypeArgVec *args, char *type) {
  args->items = z_checked_reallocarray(args->items, args->len + 1, sizeof(TypeArg));
  args->items[args->len++] = (TypeArg){.type = type};
  args->cap = args->len;
}

static bool ir_graph_split_generic_args(const char *start, const char *end, TypeArgVec *out_args) {
  const char *arg_start = start;
  int angle_depth = 0;
  int array_depth = 0;
  for (const char *cursor = start; cursor <= end; cursor++) {
    char ch = cursor < end ? *cursor : ',';
    if (ch == '<') angle_depth++;
    else if (ch == '>') angle_depth--;
    else if (ch == '[') array_depth++;
    else if (ch == ']') array_depth--;
    if (ch == ',' && angle_depth == 0 && array_depth == 0) {
      const char *arg_end = cursor;
      while (arg_start < arg_end && (*arg_start == ' ' || *arg_start == '\t')) arg_start++;
      while (arg_end > arg_start && (arg_end[-1] == ' ' || arg_end[-1] == '\t')) arg_end--;
      if (arg_end == arg_start) return false;
      ir_graph_type_arg_vec_push_owned(out_args, z_strndup(arg_start, (size_t)(arg_end - arg_start)));
      arg_start = cursor + 1;
    }
  }
  return angle_depth == 0 && array_depth == 0;
}

static char *ir_graph_resolve_alias_type_alloc_depth(const ZProgramGraph *graph, const char *type, unsigned depth) {
  if (!type) return NULL;
  if (depth > (unsigned)(graph ? graph->node_len : 0) + 8u) return z_strdup(type);
  const ZProgramGraphNode *alias = ir_graph_find_type_alias(graph, type);
  if (alias && alias->type && alias->type[0]) return ir_graph_resolve_alias_type_alloc_depth(graph, alias->type, depth + 1);

  if (type[0] == '[') {
    const char *close = strchr(type, ']');
    if (close && close[1]) {
      char *element = ir_graph_resolve_alias_type_alloc_depth(graph, close + 1, depth + 1);
      if (element) {
        ZBuf buf;
        zbuf_init(&buf);
        for (const char *cursor = type; cursor <= close; cursor++) zbuf_append_char(&buf, *cursor);
        zbuf_append(&buf, element);
        free(element);
        return buf.data;
      }
    }
  }

  const char *open = strchr(type, '<');
  const char *close = strrchr(type, '>');
  if (open && close && close[1] == '\0' && open < close) {
    TypeArgVec args = {0};
    if (ir_graph_split_generic_args(open + 1, close, &args)) {
      ZBuf buf;
      zbuf_init(&buf);
      for (const char *cursor = type; cursor < open; cursor++) zbuf_append_char(&buf, *cursor);
      zbuf_append_char(&buf, '<');
      for (size_t i = 0; i < args.len; i++) {
        if (i > 0) zbuf_append(&buf, ", ");
        char *resolved = ir_graph_resolve_alias_type_alloc_depth(graph, args.items[i].type, depth + 1);
        zbuf_append(&buf, resolved ? resolved : args.items[i].type);
        free(resolved);
      }
      zbuf_append_char(&buf, '>');
      ir_graph_type_arg_vec_free(&args);
      return buf.data;
    }
    ir_graph_type_arg_vec_free(&args);
  }

  return z_strdup(type);
}

static char *ir_graph_resolve_alias_type_alloc(const ZProgramGraph *graph, const char *type) { return ir_graph_resolve_alias_type_alloc_depth(graph, type, 0); }
static IrTypeKind ir_graph_type_kind(const ZProgramGraph *graph, const char *type) { char *resolved = ir_graph_resolve_alias_type_alloc(graph, type); IrTypeKind kind = ir_type_kind(resolved ? resolved : type); free(resolved); return kind; }
static bool ir_graph_parse_fixed_array_type(const ZProgramGraph *graph, const char *type, unsigned *out_len, IrTypeKind *out_element) { char *resolved = ir_graph_resolve_alias_type_alloc(graph, type); bool ok = ir_parse_fixed_array_type(resolved ? resolved : type, out_len, out_element); free(resolved); return ok; }
static bool ir_graph_type_name_is_mutable_byte_view(const ZProgramGraph *graph, const char *type) { char *resolved = ir_graph_resolve_alias_type_alloc(graph, type); bool ok = ir_type_name_is_mutable_byte_view(resolved ? resolved : type); free(resolved); return ok; }
static IrTypeKind ir_graph_view_element_type_for_type(const ZProgramGraph *graph, const char *type) { char *resolved = ir_graph_resolve_alias_type_alloc(graph, type); IrTypeKind element = ir_view_element_type_for_type(resolved ? resolved : type); free(resolved); return element; }
static IrTypeKind ir_graph_maybe_scalar_element_type(const ZProgramGraph *graph, const char *type) { char *resolved = ir_graph_resolve_alias_type_alloc(graph, type); IrTypeKind element = ir_maybe_scalar_element_type(resolved ? resolved : type); free(resolved); return element; }

static size_t ir_graph_shape_type_param_count(const ZProgramGraph *graph, const ZProgramGraphNode *shape) {
  return ir_graph_edge_count(graph, shape ? shape->id : NULL, "typeParam");
}

static bool ir_graph_shape_instance(const ZProgramGraph *graph, const char *type, const ZProgramGraphNode **out_shape, TypeArgVec *out_args) {
  char *resolved_type = ir_graph_resolve_alias_type_alloc(graph, type);
  const char *lookup_type = resolved_type ? resolved_type : type;
  const ZProgramGraphNode *exact = ir_graph_find_shape(graph, lookup_type);
  if (exact && ir_graph_shape_type_param_count(graph, exact) == 0) {
    if (out_shape) *out_shape = exact;
    free(resolved_type);
    return true;
  }
  const char *open = lookup_type ? strchr(lookup_type, '<') : NULL;
  const char *close = lookup_type ? strrchr(lookup_type, '>') : NULL;
  if (!open || !close || close[1] != '\0' || close < open) {
    free(resolved_type);
    return false;
  }
  char *base = z_strndup(lookup_type, (size_t)(open - lookup_type));
  const ZProgramGraphNode *shape = ir_graph_find_shape(graph, base);
  free(base);
  size_t param_count = ir_graph_shape_type_param_count(graph, shape);
  if (!shape || param_count == 0) {
    free(resolved_type);
    return false;
  }
  TypeArgVec args = {0};
  if (!ir_graph_split_generic_args(open + 1, close, &args) || args.len != param_count) {
    ir_graph_type_arg_vec_free(&args);
    free(resolved_type);
    return false;
  }
  if (out_shape) *out_shape = shape;
  if (out_args) *out_args = args;
  else ir_graph_type_arg_vec_free(&args);
  free(resolved_type);
  return true;
}

static const char *ir_graph_shape_arg_for_param(const ZProgramGraph *graph, const ZProgramGraphNode *shape, const TypeArgVec *args, const char *name) {
  if (!graph || !shape || !args || !name) return NULL;
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = ir_graph_next_edge_by_order(graph, shape->id, "typeParam", have_last, last_order);
    if (!edge) break;
    have_last = true;
    last_order = edge->order;
    const ZProgramGraphNode *param = ir_graph_find_node(graph, edge->to);
    if (param && edge->order < args->len && ir_text_eq(param->name, name)) return args->items[edge->order].type;
  }
  return NULL;
}

static char *ir_graph_shape_substitute_type(const ZProgramGraph *graph, const ZProgramGraphNode *shape, const TypeArgVec *args, const char *type) {
  if (!type) return z_strdup("Unknown");
  const char *direct = ir_graph_shape_arg_for_param(graph, shape, args, type);
  if (direct) return z_strdup(direct);
  if (type[0] == '[') {
    const char *close = strchr(type, ']');
    if (close && close[1]) {
      char *len_text = z_strndup(type + 1, (size_t)(close - type - 1));
      const char *len_sub = ir_graph_shape_arg_for_param(graph, shape, args, len_text);
      char *elem = ir_graph_shape_substitute_type(graph, shape, args, close + 1);
      ZBuf buf;
      zbuf_init(&buf);
      zbuf_append_char(&buf, '[');
      zbuf_append(&buf, len_sub ? len_sub : len_text);
      zbuf_append_char(&buf, ']');
      zbuf_append(&buf, elem);
      free(len_text);
      free(elem);
      return buf.data;
    }
  }
  const char *open = strchr(type, '<');
  const char *close = strrchr(type, '>');
  if (open && close && close[1] == '\0' && open < close) {
    TypeArgVec inner_args = {0};
    if (ir_graph_split_generic_args(open + 1, close, &inner_args)) {
      ZBuf buf;
      zbuf_init(&buf);
      for (const char *cursor = type; cursor < open; cursor++) zbuf_append_char(&buf, *cursor);
      zbuf_append_char(&buf, '<');
      for (size_t i = 0; i < inner_args.len; i++) {
        if (i > 0) zbuf_append(&buf, ", ");
        char *inner = ir_graph_shape_substitute_type(graph, shape, args, inner_args.items[i].type);
        zbuf_append(&buf, inner);
        free(inner);
      }
      zbuf_append_char(&buf, '>');
      ir_graph_type_arg_vec_free(&inner_args);
      return buf.data;
    }
    ir_graph_type_arg_vec_free(&inner_args);
  }
  return ir_graph_bound_type_text_alloc_from_arrays(type, NULL, NULL, 0);
}

static bool ir_graph_field_storage_info_for_type(const ZProgramGraph *graph, const char *type_text, unsigned *out_byte_size, unsigned *out_align, IrTypeKind *out_type, bool *out_is_array, unsigned *out_array_len, IrTypeKind *out_element_type) {
  IrTypeKind type = ir_graph_type_kind(graph, type_text);
  unsigned array_len = 0;
  IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
  bool is_array = ir_graph_parse_fixed_array_type(graph, type_text, &array_len, &element_type);
  unsigned byte_size = 0;
  unsigned align = 0;
  if (is_array) {
    byte_size = ir_type_byte_size(element_type) * array_len;
    align = ir_type_alignment(element_type);
  } else if (type == IR_TYPE_BYTE_VIEW) {
    byte_size = 16;
    align = 8;
    element_type = ir_graph_view_element_type_for_type(graph, type_text);
  } else if (type == IR_TYPE_BOOL || ir_type_is_value(type)) {
    byte_size = ir_type_byte_size(type);
    align = ir_type_alignment(type);
  } else {
    return false;
  }
  if (!byte_size || !align) return false;
  if (out_byte_size) *out_byte_size = byte_size;
  if (out_align) *out_align = align;
  if (out_type) *out_type = type;
  if (out_is_array) *out_is_array = is_array;
  if (out_array_len) *out_array_len = array_len;
  if (out_element_type) *out_element_type = element_type;
  return true;
}

static bool ir_graph_shape_layout(const ZProgramGraph *graph, const char *shape_name, unsigned *out_size, unsigned *out_align) {
  const ZProgramGraphNode *shape = NULL;
  TypeArgVec args = {0};
  if (!ir_graph_shape_instance(graph, shape_name, &shape, &args)) return false;
  size_t offset = 0;
  unsigned max_align = 1;
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = ir_graph_next_edge_by_order(graph, shape->id, "field", have_last, last_order);
    if (!edge) break;
    have_last = true;
    last_order = edge->order;
    const ZProgramGraphNode *field = ir_graph_find_node(graph, edge->to);
    if (!field || field->kind != Z_PROGRAM_GRAPH_NODE_FIELD) {
      ir_graph_type_arg_vec_free(&args);
      return false;
    }
    unsigned byte_size = 0;
    unsigned align = 0;
    char *field_type = ir_graph_shape_substitute_type(graph, shape, &args, ir_graph_node_type(graph, field));
    bool ok = ir_graph_field_storage_info_for_type(graph, field_type, &byte_size, &align, NULL, NULL, NULL, NULL);
    free(field_type);
    if (!ok) {
      ir_graph_type_arg_vec_free(&args);
      return false;
    }
    offset = ir_align_to(offset, align);
    offset += byte_size;
    if (align > max_align) max_align = align;
  }
  offset = ir_align_to(offset, max_align);
  if (offset > UINT_MAX) {
    ir_graph_type_arg_vec_free(&args);
    return false;
  }
  if (out_size) *out_size = (unsigned)offset;
  if (out_align) *out_align = max_align;
  ir_graph_type_arg_vec_free(&args);
  return true;
}

static bool ir_graph_shape_field_info(const ZProgramGraph *graph, const char *shape_name, const char *field_name, unsigned *out_offset, IrTypeKind *out_type, bool *out_is_array, unsigned *out_array_len, IrTypeKind *out_element_type) {
  const ZProgramGraphNode *shape = NULL;
  TypeArgVec args = {0};
  if (!field_name || !ir_graph_shape_instance(graph, shape_name, &shape, &args)) return false;
  size_t offset = 0;
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = ir_graph_next_edge_by_order(graph, shape->id, "field", have_last, last_order);
    if (!edge) break;
    have_last = true;
    last_order = edge->order;
    const ZProgramGraphNode *field = ir_graph_find_node(graph, edge->to);
    if (!field || field->kind != Z_PROGRAM_GRAPH_NODE_FIELD) {
      ir_graph_type_arg_vec_free(&args);
      return false;
    }
    unsigned byte_size = 0;
    unsigned align = 0;
    IrTypeKind field_type = IR_TYPE_UNSUPPORTED;
    bool is_array = false;
    unsigned array_len = 0;
    IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
    char *field_type_text = ir_graph_shape_substitute_type(graph, shape, &args, ir_graph_node_type(graph, field));
    bool ok = ir_graph_field_storage_info_for_type(graph, field_type_text, &byte_size, &align, &field_type, &is_array, &array_len, &element_type);
    free(field_type_text);
    if (!ok) {
      ir_graph_type_arg_vec_free(&args);
      return false;
    }
    offset = ir_align_to(offset, align);
    if (ir_text_eq(field->name, field_name)) {
      if (offset > UINT_MAX) {
        ir_graph_type_arg_vec_free(&args);
        return false;
      }
      if (out_offset) *out_offset = (unsigned)offset;
      if (out_type) *out_type = field_type;
      if (out_is_array) *out_is_array = is_array;
      if (out_array_len) *out_array_len = array_len;
      if (out_element_type) *out_element_type = element_type;
      ir_graph_type_arg_vec_free(&args);
      return true;
    }
    offset += byte_size;
  }
  ir_graph_type_arg_vec_free(&args);
  return false;
}

static const ZProgramGraphNode *ir_graph_shape_literal_find_field(const ZProgramGraph *graph, const ZProgramGraphNode *literal, const char *name) {
  if (!literal || !name) return NULL;
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = ir_graph_next_edge_by_order(graph, literal->id, "field", have_last, last_order);
    if (!edge) break;
    have_last = true;
    last_order = edge->order;
    const ZProgramGraphNode *field = ir_graph_find_node(graph, edge->to);
    if (field && field->kind == Z_PROGRAM_GRAPH_NODE_FIELD_INIT && ir_text_eq(field->name, name)) return field;
  }
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

static char *ir_graph_stable_id_for_function_name(const ZProgramGraph *graph, const ZProgramGraphNode *function, const char *name) {
  const ZProgramGraphNode *module = ir_graph_module_for_function(graph, function);
  ZBuf stable_id;
  zbuf_init(&stable_id);
  zbuf_append(&stable_id, module && module->name && module->name[0] ? module->name : "main");
  zbuf_append_char(&stable_id, '.');
  zbuf_append(&stable_id, name ? name : (function && function->name ? function->name : ""));
  return stable_id.data;
}

static bool ir_graph_function_is_hosted_world_main(const ZProgramGraph *graph, const ZProgramGraphNode *function) {
  const ZProgramGraphNode *param = ir_graph_ordered_node(graph, function ? function->id : NULL, "param", 0);
  const char *return_type = ir_graph_node_type(graph, function);
  return function && function->is_public && ir_text_eq(function->name, "main") && ir_graph_edge_count(graph, function->id, "param") == 1 && param && ir_text_eq(param->type, "World") && ir_text_eq(return_type, "Void");
}

bool z_program_graph_has_direct_entry_function(const ZProgramGraph *graph) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION) continue;
    if (node->export_c || ir_graph_function_is_hosted_world_main(graph, node)) return true;
  }
  return false;
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

static const ZProgramGraphNode *ir_graph_c_import_for_alias(const ZProgramGraph *graph, const char *alias) {
  for (size_t i = 0; graph && alias && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_C_IMPORT && ir_text_eq(node->name, alias)) return node;
  }
  return NULL;
}

static IrFunction *ir_graph_push_function(IrProgram *ir, const ZProgramGraph *graph, const ZProgramGraphNode *node, const char *name_override, char **generic_param_names, char **generic_arg_types, size_t generic_binding_len) {
  ir->functions = ir_grow_tracked_items(ir, ir->functions, ir->function_len, &ir->function_cap, 4, sizeof(IrFunction));
  IrFunction *fun = &ir->functions[ir->function_len++];
  bool hosted_world_main = ir_graph_function_is_hosted_world_main(graph, node);
  const ZProgramGraphNode *world_param = ir_graph_ordered_node(graph, node ? node->id : NULL, "param", 0);
  char *source_return_type_owned = ir_graph_bound_type_text_alloc_from_arrays(ir_graph_node_type(graph, node), generic_param_names, generic_arg_types, generic_binding_len);
  const char *source_return_type = source_return_type_owned;
  IrTypeKind source_type = ir_graph_type_kind(graph, source_return_type);
  IrTypeKind return_type = hosted_world_main ? IR_TYPE_I32 : (node->fallible ? IR_TYPE_I64 : source_type);
  IrTypeKind return_element_type = source_type == IR_TYPE_BYTE_VIEW ? ir_graph_view_element_type_for_type(graph, source_return_type) : IR_TYPE_UNSUPPORTED;
  if (source_type == IR_TYPE_MAYBE_SCALAR) return_element_type = ir_graph_maybe_scalar_element_type(graph, source_return_type);

  *fun = (IrFunction){
    .name = z_strdup(name_override && name_override[0] ? name_override : node->name),
    .stable_id = ir_graph_stable_id_for_function_name(graph, node, name_override && name_override[0] ? name_override : node->name),
    .world_param_name = world_param && ir_text_eq(world_param->type, "World") && world_param->name ? z_strdup(world_param->name) : NULL,
    .return_type = return_type,
    .value_return_type = source_type,
    .return_element_type = return_element_type,
    .is_exported = node->export_c || hosted_world_main,
    .raises = hosted_world_main ? false : node->fallible,
    .line = ir_graph_line(node),
    .column = ir_graph_column(node)
  };
  if (generic_binding_len > 0) {
    fun->generic_param_names = z_checked_calloc(generic_binding_len, sizeof(char *));
    fun->generic_arg_types = z_checked_calloc(generic_binding_len, sizeof(char *));
    fun->generic_binding_len = generic_binding_len;
    for (size_t i = 0; i < generic_binding_len; i++) {
      fun->generic_param_names[i] = z_strdup(generic_param_names[i]);
      fun->generic_arg_types[i] = z_strdup(generic_arg_types[i]);
    }
  }
  free(source_return_type_owned);
  return fun;
}

static bool ir_graph_collect_block_locals(IrProgram *ir, IrFunction *fun, const ZProgramGraph *graph, const ZProgramGraphNode *block);

static bool ir_graph_collect_function_locals(IrProgram *ir, IrFunction *fun, const ZProgramGraph *graph, const ZProgramGraphNode *function) {
  bool have_last = false; size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = ir_graph_next_edge_by_order(graph, function->id, "param", have_last, last_order);
    if (!edge) break;
    have_last = true;
    last_order = edge->order;
    const ZProgramGraphNode *param = ir_graph_find_node(graph, edge->to);
    if (!param) continue;
    if (ir_text_eq(param->type, "World")) {
      if (edge->order == 0) continue;
      ir_graph_mark_unsupported(ir, param, "typed graph MIR World capability parameter must be first", param->name ? param->name : "World"); return false;
    }
    char *param_type_text_owned = ir_graph_bound_type_text_alloc(fun, param->type);
    const char *param_type_text = param_type_text_owned;
    IrTypeKind type = ir_graph_type_kind(graph, param_type_text);
    bool ref_is_mutable = false;
    char *ref_inner = type == IR_TYPE_UNSUPPORTED ? ir_graph_reference_inner_type_alloc(param_type_text, &ref_is_mutable) : NULL;
    if (ref_inner) {
      unsigned ref_size = 0;
      unsigned ref_align = 0;
      if (!ir_graph_shape_layout(graph, ref_inner, &ref_size, &ref_align)) {
        ir_graph_mark_unsupported(ir, param, "typed graph MIR reference parameter requires a shape whose fields are scalars or fixed scalar arrays", param_type_text);
        free(ref_inner);
        free(param_type_text_owned);
        return false;
      }
      ir_function_push_local(ir, fun, param->name, IR_TYPE_USIZE, true, false, false, ref_inner, IR_TYPE_UNSUPPORTED, 0, 0, 0, ref_is_mutable, ir_graph_line(param), ir_graph_column(param));
      fun->locals[fun->local_len - 1].is_record_ref = true;
      fun->locals[fun->local_len - 1].ref_byte_size = ref_size;
      free(ref_inner);
      free(param_type_text_owned);
      continue;
    }
    if (!ir_type_is_direct_param_abi(type)) {
      ir_graph_mark_unsupported(ir, param, "typed graph MIR parameter type is unsupported", param_type_text);
      free(param_type_text_owned);
      return false;
    }
    IrTypeKind element_type = type == IR_TYPE_BYTE_VIEW ? ir_graph_view_element_type_for_type(graph, param_type_text) : IR_TYPE_UNSUPPORTED;
    ir_function_push_local(ir, fun, param->name, type, true, false, false, NULL, element_type, 0, 0, 0, ir_graph_type_name_is_mutable_byte_view(graph, param_type_text), ir_graph_line(param), ir_graph_column(param));
    free(param_type_text_owned);
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
  char *type_text_owned = ir_graph_node_type_for_function_alloc(graph, fun, stmt);
  const char *type_text = type_text_owned;
  IrTypeKind type = ir_graph_type_kind(graph, type_text);
  unsigned array_len = 0;
  IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
  if (ir_graph_parse_fixed_array_type(graph, type_text, &array_len, &element_type)) {
    ir_function_push_local(ir, fun, stmt->name, IR_TYPE_UNSUPPORTED, false, true, false, NULL, element_type, array_len, 0, 0, stmt->is_mutable, ir_graph_line(stmt), ir_graph_column(stmt));
    free(type_text_owned);
    return true;
  }
  unsigned record_size = 0;
  unsigned record_align = 0;
  if (ir_graph_shape_layout(graph, type_text, &record_size, &record_align)) {
    ir_function_push_local(ir, fun, stmt->name, IR_TYPE_RECORD, false, false, true, type_text, IR_TYPE_UNSUPPORTED, 0, record_size, record_align, stmt->is_mutable, ir_graph_line(stmt), ir_graph_column(stmt));
    free(type_text_owned);
    return true;
  }
  if (!ir_type_is_direct_local(type)) {
    if (ir_text_eq(type_text, "PageAlloc") || ir_text_eq(type_text, "GeneralAlloc") || ir_text_eq(type_text, "NullAlloc")) {
      ir_graph_mark_unsupported(ir, stmt, "typed graph MIR allocator local requires FixedBufAlloc", type_text);
      snprintf(ir->mir_help, sizeof(ir->mir_help), "allocate from a fixed array with std.mem.fixedBufAlloc, or split large buffers across helper functions with smaller frames; PageAlloc, GeneralAlloc, and NullAlloc locals do not lower to direct backends yet");
    } else {
      ir_graph_mark_unsupported(ir, stmt, "typed graph MIR local type is unsupported", type_text ? type_text : "inferred unknown");
    }
    free(type_text_owned);
    return false;
  }
  bool mutable_byte_view = ir_graph_type_name_is_mutable_byte_view(graph, type_text);
  IrTypeKind view_element_type = type == IR_TYPE_BYTE_VIEW ? ir_graph_view_element_type_for_type(graph, type_text) : IR_TYPE_UNSUPPORTED;
  if (type == IR_TYPE_MAYBE_SCALAR) view_element_type = ir_graph_maybe_scalar_element_type(graph, type_text);
  ir_function_push_local(ir, fun, stmt->name, type, false, false, false, NULL, view_element_type, 0, 0, 0, stmt->is_mutable || mutable_byte_view, ir_graph_line(stmt), ir_graph_column(stmt));
  free(type_text_owned);
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
          !ir_graph_collect_block_locals(ir, fun, graph, ir_graph_ordered_node(graph, stmt->id, "else", 1))) {
        return false;
      }
    } else if (stmt->kind == Z_PROGRAM_GRAPH_NODE_WHILE) {
      if (!ir_graph_collect_block_locals(ir, fun, graph, ir_graph_ordered_node(graph, stmt->id, "then", 0))) return false;
    }
  }
  return true;
}

static bool ir_graph_lower_expr(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out);
static bool ir_graph_lower_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrTypeKind preferred_return_type, IrValue **out);
static bool ir_graph_function_has_type_params(const ZProgramGraph *graph, const ZProgramGraphNode *function);
static bool ir_graph_specialization_bindings_for_call(const ZProgramGraph *graph, const IrFunction *fun, const ZProgramGraphNode *generic, const ZProgramGraphNode *call, IrTypeKind preferred_return_type, char ***out_param_names, char ***out_arg_types, size_t *out_len);
static bool ir_graph_lower_expr_for_type(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrTypeKind target_type, IrTypeKind target_element_type, bool allow_i32_default, int line, int column, IrValue **out);

static bool ir_graph_lower_record_array_field_byte_view(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  if (!expr || expr->kind != Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS) return false;
  const ZProgramGraphNode *left = ir_graph_ordered_node(graph, expr->id, "left", 0);
  if (!left || left->kind != Z_PROGRAM_GRAPH_NODE_IDENTIFIER) return false;
  const IrLocal *record = ir_function_find_local(fun, left->name);
  unsigned field_offset = 0;
  bool field_is_array = false;
  unsigned field_array_len = 0;
  IrTypeKind field_element_type = IR_TYPE_UNSUPPORTED;
  if (!record || !(record->is_record || record->is_record_ref) ||
      !ir_graph_shape_field_info(graph, record->shape_name, expr->name, &field_offset, NULL, &field_is_array, &field_array_len, &field_element_type) ||
      !field_is_array) {
    return false;
  }
  unsigned record_byte_size = record->is_record_ref ? record->ref_byte_size : record->byte_size;
  unsigned element_size = ir_type_byte_size(field_element_type);
  if (!(ir_type_is_value(field_element_type) || field_element_type == IR_TYPE_BOOL) ||
      element_size == 0 ||
      field_array_len > UINT_MAX / element_size ||
      field_offset > record_byte_size ||
      field_array_len * element_size > record_byte_size - field_offset) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR record array field byte view has unsupported storage", expr && expr->name ? expr->name : "array field");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
  value->array_index = record->index;
  value->field_offset = field_offset;
  value->data_len = field_array_len;
  value->element_type = field_element_type;
  *out = value;
  return true;
}

static bool ir_graph_lower_byte_view(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  if (!expr) { ir_mark_unsupported(ir, "typed graph MIR byte view expression is missing", 1, 1, "missing expression"); return false; }
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_BORROW) {
    return ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "left", 0), out);
  }
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_LITERAL && ir_graph_type_kind(graph, expr->type) == IR_TYPE_BYTE_VIEW) {
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
  if (ir_graph_lower_record_array_field_byte_view(graph, ir, fun, expr, out)) return true;
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

static bool ir_graph_fixed_resource_types(const ZProgramGraph *graph, const char *type_text, const char *name, IrTypeKind *out_first, IrTypeKind *out_second) {
  char *resolved_type = ir_graph_resolve_alias_type_alloc(graph, type_text);
  type_text = resolved_type ? resolved_type : type_text;
  size_t name_len = strlen(name);
  size_t type_len = type_text ? strlen(type_text) : 0;
  if (type_len <= name_len + 2 || strncmp(type_text, name, name_len) != 0 || type_text[name_len] != '<' || type_text[type_len - 1] != '>') {
    free(resolved_type);
    return false;
  }
  TypeArgVec args = {0};
  bool ok = ir_graph_split_generic_args(type_text + name_len + 1, type_text + type_len - 1, &args) && args.len == (out_second ? 2u : 1u);
  IrTypeKind first = ok ? ir_graph_type_kind(graph, args.items[0].type) : IR_TYPE_UNSUPPORTED;
  IrTypeKind second = ok && out_second ? ir_graph_type_kind(graph, args.items[1].type) : IR_TYPE_UNSUPPORTED;
  ir_graph_type_arg_vec_free(&args);
  free(resolved_type);
  if (!(first == IR_TYPE_BOOL || ir_type_is_value(first)) || (out_second && !(second == IR_TYPE_BOOL || ir_type_is_value(second)))) return false;
  if (out_first) *out_first = first;
  if (out_second) *out_second = second;
  return true;
}
static bool ir_graph_fixed_set_element_type(const ZProgramGraph *graph, const char *type_text, IrTypeKind *out_element_type) { return ir_graph_fixed_resource_types(graph, type_text, "FixedSet", out_element_type, NULL); }
static bool ir_graph_fixed_deque_element_type(const ZProgramGraph *graph, const char *type_text, IrTypeKind *out_element_type) { return ir_graph_fixed_resource_types(graph, type_text, "FixedDeque", out_element_type, NULL); }
static bool ir_graph_fixed_ring_buffer_element_type(const ZProgramGraph *graph, const char *type_text, IrTypeKind *out_element_type) { return ir_graph_fixed_resource_types(graph, type_text, "FixedRingBuffer", out_element_type, NULL); }
static bool ir_graph_fixed_map_types(const ZProgramGraph *graph, const char *type_text, IrTypeKind *out_key_type, IrTypeKind *out_value_type) { return ir_graph_fixed_resource_types(graph, type_text, "FixedMap", out_key_type, out_value_type); }

static IrValue *ir_graph_record_field_load_value(IrProgram *ir, const IrLocal *local, unsigned field_offset, IrTypeKind field_type, IrTypeKind element_type, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_FIELD_LOAD, field_type, line, column);
  value->local_index = local ? local->index : 0;
  value->field_offset = field_offset;
  value->element_type = field_type == IR_TYPE_BYTE_VIEW ? element_type : field_type;
  return value;
}

static IrValue *ir_graph_record_view_len_value(IrProgram *ir, const IrLocal *local, unsigned view_offset, IrTypeKind view_element_type, int line, int column) {
  IrValue *view = ir_graph_record_field_load_value(ir, local, view_offset, IR_TYPE_BYTE_VIEW, view_element_type, line, column);
  IrValue *len = ir_new_value(ir, IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_USIZE, line, column);
  len->left = view;
  return len;
}

static IrValue *ir_graph_record_usize_field_value(IrProgram *ir, const IrLocal *local, unsigned field_offset, int line, int column) {
  return ir_graph_record_field_load_value(ir, local, field_offset, IR_TYPE_USIZE, IR_TYPE_USIZE, line, column);
}

static void ir_graph_push_record_field_store(IrProgram *ir, IrInstr **out_items, size_t *out_len, size_t *out_cap, const IrLocal *local, unsigned field_offset, IrValue *value, int line, int column) {
  ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_FIELD_STORE, .local_index = local ? local->index : 0, .field_offset = field_offset, .value = value, .line = line, .column = column});
}

static void ir_graph_push_conditional_field_store(IrProgram *ir, IrInstr **out_items, size_t *out_len, size_t *out_cap, const IrLocal *local, unsigned field_offset, IrValue *condition, IrValue *value, int line, int column) {
  IrInstr *then_instrs = NULL;
  size_t then_len = 0;
  size_t then_cap = 0;
  ir_graph_push_record_field_store(ir, &then_instrs, &then_len, &then_cap, local, field_offset, value, line, column);
  ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_IF, .value = condition, .then_instrs = then_instrs, .then_len = then_len, .then_cap = then_cap, .line = line, .column = column});
}

static void ir_graph_push_fixed_resource_len_clamp(IrProgram *ir, IrInstr **out_items, size_t *out_len, size_t *out_cap, const IrLocal *local, unsigned len_offset, unsigned view_offset, IrTypeKind view_element_type, int line, int column) {
  IrValue *condition = ir_new_compare_value(ir,
                                            IR_CMP_GT,
                                            ir_graph_record_usize_field_value(ir, local, len_offset, line, column),
                                            ir_graph_record_view_len_value(ir, local, view_offset, view_element_type, line, column),
                                            line,
                                            column);
  IrValue *clamped_len = ir_graph_record_view_len_value(ir, local, view_offset, view_element_type, line, column);
  ir_graph_push_conditional_field_store(ir, out_items, out_len, out_cap, local, len_offset, condition, clamped_len, line, column);
}

static IrValue *ir_graph_fixed_reader_slice_value(IrProgram *ir, const IrLocal *reader, unsigned bytes_offset, IrValue *start, IrValue *end, int line, int column) {
  IrValue *slice = ir_new_value(ir, IR_VALUE_BYTE_SLICE, IR_TYPE_BYTE_VIEW, line, column);
  slice->left = ir_graph_record_field_load_value(ir, reader, bytes_offset, IR_TYPE_BYTE_VIEW, IR_TYPE_U8, line, column);
  slice->index = start;
  slice->right = end;
  slice->element_type = IR_TYPE_U8;
  return slice;
}

static bool ir_graph_lower_fixed_reader_limit_initializer(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const IrLocal *local, const ZProgramGraphNode *expr, unsigned bytes_offset, unsigned cursor_offset, IrInstr **out_items, size_t *out_len, size_t *out_cap) {
  const ZProgramGraphNode *reader_arg = ir_graph_ordered_node(graph, expr->id, "arg", 0);
  const ZProgramGraphNode *reader_target = reader_arg && reader_arg->kind == Z_PROGRAM_GRAPH_NODE_BORROW ? ir_graph_ordered_node(graph, reader_arg->id, "left", 0) : reader_arg;
  const IrLocal *reader = reader_target && reader_target->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER ? ir_function_find_local(fun, reader_target->name) : NULL;
  if (!reader || !reader->is_record || !ir_text_eq(reader->shape_name, "FixedReader")) {
    ir_graph_mark_unsupported(ir, reader_arg, "typed graph MIR fixedReaderLimit expects a FixedReader reference", "non-FixedReader argument");
    return true;
  }
  unsigned source_bytes_offset = 0;
  unsigned source_cursor_offset = 0;
  IrTypeKind source_bytes_type = IR_TYPE_UNSUPPORTED;
  IrTypeKind source_cursor_type = IR_TYPE_UNSUPPORTED;
  bool source_bytes_is_array = false;
  bool source_cursor_is_array = false;
  if (!ir_graph_shape_field_info(graph, reader->shape_name, "bytes", &source_bytes_offset, &source_bytes_type, &source_bytes_is_array, NULL, NULL) ||
      !ir_graph_shape_field_info(graph, reader->shape_name, "cursor", &source_cursor_offset, &source_cursor_type, &source_cursor_is_array, NULL, NULL) ||
      source_bytes_is_array || source_cursor_is_array || source_bytes_type != IR_TYPE_BYTE_VIEW || source_cursor_type != IR_TYPE_USIZE) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR FixedReader source layout is unsupported", reader->shape_name ? reader->shape_name : "FixedReader");
    return true;
  }
  const ZProgramGraphNode *count_node = ir_graph_ordered_node(graph, expr->id, "arg", 1);
  IrValue *count = NULL;
  if (!ir_graph_lower_expr_for_type(graph, ir, fun, count_node, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, false, ir_graph_line(count_node), ir_graph_column(count_node), &count)) {
    return true;
  }
  if (!count || count->type != IR_TYPE_USIZE) {
    ir_free_value(count);
    ir_graph_mark_unsupported(ir, count_node, "typed graph MIR fixedReaderLimit count must be usize", "non-usize count");
    return true;
  }

  int line = ir_graph_line(expr);
  int column = ir_graph_column(expr);
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, cursor_offset, ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 0, line, column), line, column);
  IrValue *empty_slice = ir_graph_fixed_reader_slice_value(ir, reader, source_bytes_offset, ir_graph_record_view_len_value(ir, reader, source_bytes_offset, IR_TYPE_U8, line, column), NULL, line, column);
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, bytes_offset, empty_slice, line, column);

  IrValue *cursor_in_bounds = ir_new_compare_value(ir,
                                                   IR_CMP_LE,
                                                   ir_graph_record_usize_field_value(ir, reader, source_cursor_offset, line, column),
                                                   ir_graph_record_view_len_value(ir, reader, source_bytes_offset, IR_TYPE_U8, line, column),
                                                   line,
                                                   column);
  IrInstr *then_instrs = NULL;
  size_t then_len = 0;
  size_t then_cap = 0;
  ir_graph_push_record_field_store(ir, &then_instrs, &then_len, &then_cap, local, cursor_offset, count, line, column);
  IrValue *remaining_len = ir_new_binary_value(ir,
                                               IR_BIN_SUB,
                                               IR_TYPE_USIZE,
                                               ir_graph_record_view_len_value(ir, reader, source_bytes_offset, IR_TYPE_U8, line, column),
                                               ir_graph_record_usize_field_value(ir, reader, source_cursor_offset, line, column),
                                               line,
                                               column);
  IrValue *count_exceeds_remaining = ir_new_compare_value(ir,
                                                          IR_CMP_GT,
                                                          ir_graph_record_usize_field_value(ir, local, cursor_offset, line, column),
                                                          remaining_len,
                                                          line,
                                                          column);
  IrValue *clamped_len = ir_new_binary_value(ir,
                                             IR_BIN_SUB,
                                             IR_TYPE_USIZE,
                                             ir_graph_record_view_len_value(ir, reader, source_bytes_offset, IR_TYPE_U8, line, column),
                                             ir_graph_record_usize_field_value(ir, reader, source_cursor_offset, line, column),
                                             line,
                                             column);
  ir_graph_push_conditional_field_store(ir, &then_instrs, &then_len, &then_cap, local, cursor_offset, count_exceeds_remaining, clamped_len, line, column);
  IrValue *bounded_slice = ir_graph_fixed_reader_slice_value(ir,
                                                             reader,
                                                             source_bytes_offset,
                                                             ir_graph_record_usize_field_value(ir, reader, source_cursor_offset, line, column),
                                                             ir_new_binary_value(ir,
                                                                                 IR_BIN_ADD,
                                                                                 IR_TYPE_USIZE,
                                                                                 ir_graph_record_usize_field_value(ir, reader, source_cursor_offset, line, column),
                                                                                 ir_graph_record_usize_field_value(ir, local, cursor_offset, line, column),
                                                                                 line,
                                                                                 column),
                                                             line,
                                                             column);
  ir_graph_push_record_field_store(ir, &then_instrs, &then_len, &then_cap, local, bytes_offset, bounded_slice, line, column);
  ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_IF, .value = cursor_in_bounds, .then_instrs = then_instrs, .then_len = then_len, .then_cap = then_cap, .line = line, .column = column});
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, cursor_offset, ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 0, line, column), line, column);
  return true;
}

static void ir_graph_push_fixed_ring_head_normalization(IrProgram *ir, IrInstr **out_items, size_t *out_len, size_t *out_cap, const IrLocal *local, unsigned items_offset, unsigned head_offset, unsigned len_offset, IrTypeKind element_type, int line, int column) {
  IrValue *capacity_is_zero = ir_new_compare_value(ir,
                                                   IR_CMP_EQ,
                                                   ir_graph_record_view_len_value(ir, local, items_offset, element_type, line, column),
                                                   ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 0, line, column),
                                                   line,
                                                   column);
  ir_graph_push_conditional_field_store(ir, out_items, out_len, out_cap, local, head_offset, capacity_is_zero, ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 0, line, column), line, column);

  IrValue *capacity_is_nonzero = ir_new_compare_value(ir,
                                                      IR_CMP_NE,
                                                      ir_graph_record_view_len_value(ir, local, items_offset, element_type, line, column),
                                                      ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 0, line, column),
                                                      line,
                                                      column);
  IrValue *normalized_head = ir_new_binary_value(ir,
                                                 IR_BIN_MOD,
                                                 IR_TYPE_USIZE,
                                                 ir_graph_record_usize_field_value(ir, local, head_offset, line, column),
                                                 ir_graph_record_view_len_value(ir, local, items_offset, element_type, line, column),
                                                 line,
                                                 column);
  ir_graph_push_conditional_field_store(ir, out_items, out_len, out_cap, local, head_offset, capacity_is_nonzero, normalized_head, line, column);

  ir_graph_push_fixed_resource_len_clamp(ir, out_items, out_len, out_cap, local, len_offset, items_offset, element_type, line, column);

  IrValue *len_is_zero = ir_new_compare_value(ir,
                                              IR_CMP_EQ,
                                              ir_graph_record_usize_field_value(ir, local, len_offset, line, column),
                                              ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 0, line, column),
                                              line,
                                              column);
  ir_graph_push_conditional_field_store(ir, out_items, out_len, out_cap, local, head_offset, len_is_zero, ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 0, line, column), line, column);
}

static bool ir_graph_lower_fixed_set_initializer(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const IrLocal *local, const ZProgramGraphNode *expr, IrInstr **out_items, size_t *out_len, size_t *out_cap) {
  IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
  if (!local || !local->is_record || !ir_graph_fixed_set_element_type(graph, local->shape_name, &element_type) ||
      !expr || (expr->kind != Z_PROGRAM_GRAPH_NODE_CALL && expr->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL)) {
    return false;
  }
  char *qualified = ir_graph_expr_qualified_name(graph, expr);
  bool is_constructor = ir_text_eq(qualified && qualified[0] ? qualified : expr->name, "std.collections.fixedSet") ||
                        ir_text_eq(qualified && qualified[0] ? qualified : expr->name, "__zero_std_collections_fixed_set");
  free(qualified);
  if (!is_constructor || ir_graph_edge_count(graph, expr->id, "arg") != 2) return false;

  unsigned items_offset = 0;
  unsigned len_offset = 0;
  IrTypeKind items_type = IR_TYPE_UNSUPPORTED;
  IrTypeKind len_type = IR_TYPE_UNSUPPORTED;
  bool items_is_array = false;
  bool len_is_array = false;
  if (!ir_graph_shape_field_info(graph, local->shape_name, "items", &items_offset, &items_type, &items_is_array, NULL, NULL) ||
      !ir_graph_shape_field_info(graph, local->shape_name, "len", &len_offset, &len_type, &len_is_array, NULL, NULL) ||
      items_is_array || len_is_array || items_type != IR_TYPE_BYTE_VIEW || len_type != IR_TYPE_USIZE) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR FixedSet layout is unsupported", local->shape_name ? local->shape_name : "FixedSet");
    return true;
  }

  const ZProgramGraphNode *items_node = ir_graph_ordered_node(graph, expr->id, "arg", 0);
  const ZProgramGraphNode *len_node = ir_graph_ordered_node(graph, expr->id, "arg", 1);
  IrValue *items = NULL;
  IrValue *len = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, items_node, &items) ||
      !ir_graph_lower_expr_for_type(graph, ir, fun, len_node, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, false, ir_graph_line(len_node), ir_graph_column(len_node), &len)) {
    ir_free_value(items);
    ir_free_value(len);
    return true;
  }
  if (items->element_type != element_type || len->type != IR_TYPE_USIZE) {
    ir_free_value(items);
    ir_free_value(len);
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR FixedSet constructor arguments do not match storage type", local->shape_name ? local->shape_name : "FixedSet");
    return true;
  }
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, items_offset, items, ir_graph_line(items_node), ir_graph_column(items_node));
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, len_offset, len, ir_graph_line(len_node), ir_graph_column(len_node));
  ir_graph_push_fixed_resource_len_clamp(ir, out_items, out_len, out_cap, local, len_offset, items_offset, element_type, ir_graph_line(expr), ir_graph_column(expr));
  return true;
}

static bool ir_graph_lower_fixed_deque_initializer(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const IrLocal *local, const ZProgramGraphNode *expr, IrInstr **out_items, size_t *out_len, size_t *out_cap) {
  IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
  if (!local || !local->is_record || !ir_graph_fixed_deque_element_type(graph, local->shape_name, &element_type) ||
      !expr || (expr->kind != Z_PROGRAM_GRAPH_NODE_CALL && expr->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL)) {
    return false;
  }
  char *qualified = ir_graph_expr_qualified_name(graph, expr);
  bool is_constructor = ir_text_eq(qualified && qualified[0] ? qualified : expr->name, "std.collections.fixedDeque") ||
                        ir_text_eq(qualified && qualified[0] ? qualified : expr->name, "__zero_std_collections_fixed_deque");
  free(qualified);
  if (!is_constructor || ir_graph_edge_count(graph, expr->id, "arg") != 2) return false;

  unsigned items_offset = 0;
  unsigned len_offset = 0;
  IrTypeKind items_type = IR_TYPE_UNSUPPORTED;
  IrTypeKind len_type = IR_TYPE_UNSUPPORTED;
  bool items_is_array = false;
  bool len_is_array = false;
  if (!ir_graph_shape_field_info(graph, local->shape_name, "items", &items_offset, &items_type, &items_is_array, NULL, NULL) ||
      !ir_graph_shape_field_info(graph, local->shape_name, "len", &len_offset, &len_type, &len_is_array, NULL, NULL) ||
      items_is_array || len_is_array || items_type != IR_TYPE_BYTE_VIEW || len_type != IR_TYPE_USIZE) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR FixedDeque layout is unsupported", local->shape_name ? local->shape_name : "FixedDeque");
    return true;
  }

  const ZProgramGraphNode *items_node = ir_graph_ordered_node(graph, expr->id, "arg", 0);
  const ZProgramGraphNode *len_node = ir_graph_ordered_node(graph, expr->id, "arg", 1);
  IrValue *items = NULL;
  IrValue *len = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, items_node, &items) ||
      !ir_graph_lower_expr_for_type(graph, ir, fun, len_node, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, false, ir_graph_line(len_node), ir_graph_column(len_node), &len)) {
    ir_free_value(items);
    ir_free_value(len);
    return true;
  }
  if (items->element_type != element_type || len->type != IR_TYPE_USIZE) {
    ir_free_value(items);
    ir_free_value(len);
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR FixedDeque constructor arguments do not match storage type", local->shape_name ? local->shape_name : "FixedDeque");
    return true;
  }
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, items_offset, items, ir_graph_line(items_node), ir_graph_column(items_node));
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, len_offset, len, ir_graph_line(len_node), ir_graph_column(len_node));
  ir_graph_push_fixed_resource_len_clamp(ir, out_items, out_len, out_cap, local, len_offset, items_offset, element_type, ir_graph_line(expr), ir_graph_column(expr));
  return true;
}

static bool ir_graph_lower_fixed_ring_buffer_initializer(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const IrLocal *local, const ZProgramGraphNode *expr, IrInstr **out_items, size_t *out_len, size_t *out_cap) {
  IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
  if (!local || !local->is_record || !ir_graph_fixed_ring_buffer_element_type(graph, local->shape_name, &element_type) ||
      !expr || (expr->kind != Z_PROGRAM_GRAPH_NODE_CALL && expr->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL)) {
    return false;
  }
  char *qualified = ir_graph_expr_qualified_name(graph, expr);
  bool is_constructor = ir_text_eq(qualified && qualified[0] ? qualified : expr->name, "std.collections.fixedRingBuffer") ||
                        ir_text_eq(qualified && qualified[0] ? qualified : expr->name, "__zero_std_collections_fixed_ring_buffer");
  free(qualified);
  if (!is_constructor || ir_graph_edge_count(graph, expr->id, "arg") != 3) return false;

  unsigned items_offset = 0;
  unsigned head_offset = 0;
  unsigned len_offset = 0;
  IrTypeKind items_type = IR_TYPE_UNSUPPORTED;
  IrTypeKind head_type = IR_TYPE_UNSUPPORTED;
  IrTypeKind len_type = IR_TYPE_UNSUPPORTED;
  bool items_is_array = false;
  bool head_is_array = false;
  bool len_is_array = false;
  if (!ir_graph_shape_field_info(graph, local->shape_name, "items", &items_offset, &items_type, &items_is_array, NULL, NULL) ||
      !ir_graph_shape_field_info(graph, local->shape_name, "head", &head_offset, &head_type, &head_is_array, NULL, NULL) ||
      !ir_graph_shape_field_info(graph, local->shape_name, "len", &len_offset, &len_type, &len_is_array, NULL, NULL) ||
      items_is_array || head_is_array || len_is_array || items_type != IR_TYPE_BYTE_VIEW || head_type != IR_TYPE_USIZE || len_type != IR_TYPE_USIZE) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR FixedRingBuffer layout is unsupported", local->shape_name ? local->shape_name : "FixedRingBuffer");
    return true;
  }

  const ZProgramGraphNode *items_node = ir_graph_ordered_node(graph, expr->id, "arg", 0);
  const ZProgramGraphNode *head_node = ir_graph_ordered_node(graph, expr->id, "arg", 1);
  const ZProgramGraphNode *len_node = ir_graph_ordered_node(graph, expr->id, "arg", 2);
  IrValue *items = NULL;
  IrValue *head = NULL;
  IrValue *len = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, items_node, &items) ||
      !ir_graph_lower_expr_for_type(graph, ir, fun, head_node, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, false, ir_graph_line(head_node), ir_graph_column(head_node), &head) ||
      !ir_graph_lower_expr_for_type(graph, ir, fun, len_node, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, false, ir_graph_line(len_node), ir_graph_column(len_node), &len)) {
    ir_free_value(items);
    ir_free_value(head);
    ir_free_value(len);
    return true;
  }
  if (items->element_type != element_type || head->type != IR_TYPE_USIZE || len->type != IR_TYPE_USIZE) {
    ir_free_value(items);
    ir_free_value(head);
    ir_free_value(len);
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR FixedRingBuffer constructor arguments do not match storage type", local->shape_name ? local->shape_name : "FixedRingBuffer");
    return true;
  }
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, items_offset, items, ir_graph_line(items_node), ir_graph_column(items_node));
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, head_offset, head, ir_graph_line(head_node), ir_graph_column(head_node));
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, len_offset, len, ir_graph_line(len_node), ir_graph_column(len_node));
  ir_graph_push_fixed_ring_head_normalization(ir, out_items, out_len, out_cap, local, items_offset, head_offset, len_offset, element_type, ir_graph_line(expr), ir_graph_column(expr));
  return true;
}

static bool ir_graph_lower_fixed_map_initializer(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const IrLocal *local, const ZProgramGraphNode *expr, IrInstr **out_items, size_t *out_len, size_t *out_cap) {
  IrTypeKind key_type = IR_TYPE_UNSUPPORTED;
  IrTypeKind value_type = IR_TYPE_UNSUPPORTED;
  if (!local || !local->is_record || !ir_graph_fixed_map_types(graph, local->shape_name, &key_type, &value_type) ||
      !expr || (expr->kind != Z_PROGRAM_GRAPH_NODE_CALL && expr->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL)) {
    return false;
  }
  char *qualified = ir_graph_expr_qualified_name(graph, expr);
  bool is_constructor = ir_text_eq(qualified && qualified[0] ? qualified : expr->name, "std.collections.fixedMap") ||
                        ir_text_eq(qualified && qualified[0] ? qualified : expr->name, "__zero_std_collections_fixed_map");
  free(qualified);
  if (!is_constructor || ir_graph_edge_count(graph, expr->id, "arg") != 3) return false;

  unsigned keys_offset = 0;
  unsigned values_offset = 0;
  unsigned len_offset = 0;
  IrTypeKind keys_type = IR_TYPE_UNSUPPORTED;
  IrTypeKind values_type = IR_TYPE_UNSUPPORTED;
  IrTypeKind len_type = IR_TYPE_UNSUPPORTED;
  IrTypeKind keys_element_type = IR_TYPE_UNSUPPORTED;
  IrTypeKind values_element_type = IR_TYPE_UNSUPPORTED;
  bool keys_is_array = false;
  bool values_is_array = false;
  bool len_is_array = false;
  if (!ir_graph_shape_field_info(graph, local->shape_name, "keys", &keys_offset, &keys_type, &keys_is_array, NULL, &keys_element_type) ||
      !ir_graph_shape_field_info(graph, local->shape_name, "values", &values_offset, &values_type, &values_is_array, NULL, &values_element_type) ||
      !ir_graph_shape_field_info(graph, local->shape_name, "len", &len_offset, &len_type, &len_is_array, NULL, NULL) ||
      keys_is_array || values_is_array || len_is_array ||
      keys_type != IR_TYPE_BYTE_VIEW || values_type != IR_TYPE_BYTE_VIEW || len_type != IR_TYPE_USIZE ||
      keys_element_type != key_type || values_element_type != value_type) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR FixedMap layout is unsupported", local->shape_name ? local->shape_name : "FixedMap");
    return true;
  }

  const ZProgramGraphNode *keys_node = ir_graph_ordered_node(graph, expr->id, "arg", 0);
  const ZProgramGraphNode *values_node = ir_graph_ordered_node(graph, expr->id, "arg", 1);
  const ZProgramGraphNode *len_node = ir_graph_ordered_node(graph, expr->id, "arg", 2);
  IrValue *keys = NULL;
  IrValue *values = NULL;
  IrValue *len = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, keys_node, &keys) ||
      !ir_graph_lower_byte_view(graph, ir, fun, values_node, &values) ||
      !ir_graph_lower_expr_for_type(graph, ir, fun, len_node, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, false, ir_graph_line(len_node), ir_graph_column(len_node), &len)) {
    ir_free_value(keys);
    ir_free_value(values);
    ir_free_value(len);
    return true;
  }
  if (keys->element_type != key_type || values->element_type != value_type || len->type != IR_TYPE_USIZE) {
    ir_free_value(keys);
    ir_free_value(values);
    ir_free_value(len);
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR FixedMap constructor arguments do not match storage type", local->shape_name ? local->shape_name : "FixedMap");
    return true;
  }
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, keys_offset, keys, ir_graph_line(keys_node), ir_graph_column(keys_node));
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, values_offset, values, ir_graph_line(values_node), ir_graph_column(values_node));
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, len_offset, len, ir_graph_line(len_node), ir_graph_column(len_node));
  ir_graph_push_fixed_resource_len_clamp(ir, out_items, out_len, out_cap, local, len_offset, keys_offset, key_type, ir_graph_line(expr), ir_graph_column(expr));
  ir_graph_push_fixed_resource_len_clamp(ir, out_items, out_len, out_cap, local, len_offset, values_offset, value_type, ir_graph_line(expr), ir_graph_column(expr));
  return true;
}

static bool ir_graph_lower_fixed_reader_initializer(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const IrLocal *local, const ZProgramGraphNode *expr, IrInstr **out_items, size_t *out_len, size_t *out_cap) {
  if (!local || !local->is_record || !ir_text_eq(local->shape_name, "FixedReader") ||
      !expr || (expr->kind != Z_PROGRAM_GRAPH_NODE_CALL && expr->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL)) {
    return false;
  }
  char *qualified = ir_graph_expr_qualified_name(graph, expr);
  const char *call_name = qualified && qualified[0] ? qualified : expr->name;
  bool is_constructor = ir_text_eq(call_name, "std.io.fixedReader") ||
                        ir_text_eq(call_name, "__zero_std_io_fixed_reader");
  bool is_limit = ir_text_eq(call_name, "std.io.fixedReaderLimit") ||
                  ir_text_eq(call_name, "__zero_std_io_fixed_reader_limit");
  free(qualified);
  if ((!is_constructor && !is_limit) || ir_graph_edge_count(graph, expr->id, "arg") != 2) return false;

  unsigned bytes_offset = 0;
  unsigned cursor_offset = 0;
  IrTypeKind bytes_type = IR_TYPE_UNSUPPORTED;
  IrTypeKind cursor_type = IR_TYPE_UNSUPPORTED;
  bool bytes_is_array = false;
  bool cursor_is_array = false;
  if (!ir_graph_shape_field_info(graph, local->shape_name, "bytes", &bytes_offset, &bytes_type, &bytes_is_array, NULL, NULL) ||
      !ir_graph_shape_field_info(graph, local->shape_name, "cursor", &cursor_offset, &cursor_type, &cursor_is_array, NULL, NULL) ||
      bytes_is_array || cursor_is_array || bytes_type != IR_TYPE_BYTE_VIEW || cursor_type != IR_TYPE_USIZE) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR FixedReader layout is unsupported", local->shape_name ? local->shape_name : "FixedReader");
    return true;
  }

  if (is_limit) {
    return ir_graph_lower_fixed_reader_limit_initializer(graph, ir, fun, local, expr, bytes_offset, cursor_offset, out_items, out_len, out_cap);
  }

  const ZProgramGraphNode *bytes_node = ir_graph_ordered_node(graph, expr->id, "arg", 0);
  const ZProgramGraphNode *cursor_node = ir_graph_ordered_node(graph, expr->id, "arg", 1);
  IrValue *bytes = NULL;
  IrValue *cursor = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, bytes_node, &bytes) ||
      !ir_graph_lower_expr_for_type(graph, ir, fun, cursor_node, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, false, ir_graph_line(cursor_node), ir_graph_column(cursor_node), &cursor)) {
    ir_free_value(bytes);
    ir_free_value(cursor);
    return true;
  }
  if (bytes->element_type != IR_TYPE_U8 || cursor->type != IR_TYPE_USIZE) {
    ir_free_value(bytes);
    ir_free_value(cursor);
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR FixedReader constructor arguments do not match storage type", local->shape_name ? local->shape_name : "FixedReader");
    return true;
  }
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, bytes_offset, bytes, ir_graph_line(bytes_node), ir_graph_column(bytes_node));
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, cursor_offset, cursor, ir_graph_line(cursor_node), ir_graph_column(cursor_node));
  ir_graph_push_fixed_resource_len_clamp(ir, out_items, out_len, out_cap, local, cursor_offset, bytes_offset, IR_TYPE_U8, ir_graph_line(expr), ir_graph_column(expr));
  return true;
}

static bool ir_graph_lower_fixed_writer_initializer(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const IrLocal *local, const ZProgramGraphNode *expr, IrInstr **out_items, size_t *out_len, size_t *out_cap) {
  if (!local || !local->is_record || !ir_text_eq(local->shape_name, "FixedWriter") ||
      !expr || (expr->kind != Z_PROGRAM_GRAPH_NODE_CALL && expr->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL)) {
    return false;
  }
  char *qualified = ir_graph_expr_qualified_name(graph, expr);
  bool is_constructor = ir_text_eq(qualified && qualified[0] ? qualified : expr->name, "std.io.fixedWriter") ||
                        ir_text_eq(qualified && qualified[0] ? qualified : expr->name, "__zero_std_io_fixed_writer");
  free(qualified);
  if (!is_constructor || ir_graph_edge_count(graph, expr->id, "arg") != 2) return false;

  unsigned buffer_offset = 0;
  unsigned cursor_offset = 0;
  IrTypeKind buffer_type = IR_TYPE_UNSUPPORTED;
  IrTypeKind cursor_type = IR_TYPE_UNSUPPORTED;
  bool buffer_is_array = false;
  bool cursor_is_array = false;
  if (!ir_graph_shape_field_info(graph, local->shape_name, "buffer", &buffer_offset, &buffer_type, &buffer_is_array, NULL, NULL) ||
      !ir_graph_shape_field_info(graph, local->shape_name, "cursor", &cursor_offset, &cursor_type, &cursor_is_array, NULL, NULL) ||
      buffer_is_array || cursor_is_array || buffer_type != IR_TYPE_BYTE_VIEW || cursor_type != IR_TYPE_USIZE) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR FixedWriter layout is unsupported", local->shape_name ? local->shape_name : "FixedWriter");
    return true;
  }

  const ZProgramGraphNode *buffer_node = ir_graph_ordered_node(graph, expr->id, "arg", 0);
  const ZProgramGraphNode *cursor_node = ir_graph_ordered_node(graph, expr->id, "arg", 1);
  IrValue *buffer = NULL;
  IrValue *cursor = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, buffer_node, &buffer) ||
      !ir_graph_lower_expr_for_type(graph, ir, fun, cursor_node, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, false, ir_graph_line(cursor_node), ir_graph_column(cursor_node), &cursor)) {
    ir_free_value(buffer);
    ir_free_value(cursor);
    return true;
  }
  if (buffer->element_type != IR_TYPE_U8 || cursor->type != IR_TYPE_USIZE) {
    ir_free_value(buffer);
    ir_free_value(cursor);
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR FixedWriter constructor arguments do not match storage type", local->shape_name ? local->shape_name : "FixedWriter");
    return true;
  }
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, buffer_offset, buffer, ir_graph_line(buffer_node), ir_graph_column(buffer_node));
  ir_graph_push_record_field_store(ir, out_items, out_len, out_cap, local, cursor_offset, cursor, ir_graph_line(cursor_node), ir_graph_column(cursor_node));
  ir_graph_push_fixed_resource_len_clamp(ir, out_items, out_len, out_cap, local, cursor_offset, buffer_offset, IR_TYPE_U8, ir_graph_line(expr), ir_graph_column(expr));
  return true;
}

static bool ir_graph_expr_is_byte_view_source(const ZProgramGraph *graph, const IrFunction *fun, const ZProgramGraphNode *expr) {
  if (!expr) return false;
  if (ir_graph_type_kind(graph, ir_graph_node_type(graph, expr)) == IR_TYPE_BYTE_VIEW) return true;
  switch (expr->kind) {
    case Z_PROGRAM_GRAPH_NODE_LITERAL:
      return ir_graph_type_kind(graph, expr->type) == IR_TYPE_BYTE_VIEW;
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

static IrTypeKind ir_graph_view_element_type(const IrValue *view) {
  return view && view->element_type != IR_TYPE_UNSUPPORTED ? view->element_type : IR_TYPE_U8;
}

static bool ir_graph_lower_binary_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrTypeKind preferred_type, IrValue **out);

static bool ir_graph_lower_expr_for_type(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrTypeKind target_type, IrTypeKind target_element_type, bool allow_i32_default, int line, int column, IrValue **out) {
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
      IrTypeKind element = target_element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_I32 : target_element_type;
      *out = ir_new_maybe_scalar_literal(ir, false, element, 0, line, column);
      return true;
    }
  }
  if (expr && expr->kind == Z_PROGRAM_GRAPH_NODE_LITERAL && ir_type_is_value(target_type) && ir_graph_type_kind(graph, expr->type) == IR_TYPE_UNSUPPORTED) {
    unsigned long long parsed = 0;
    if (!ir_parse_integer_literal(expr->value ? expr->value : "", &parsed)) {
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR integer literal is malformed", expr->value ? expr->value : "");
      return false;
    }
    *out = ir_new_integer_literal_value(ir, target_type, parsed, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (target_type == IR_TYPE_BYTE_VIEW && expr && expr->kind == Z_PROGRAM_GRAPH_NODE_CHECK) return ir_graph_lower_expr(graph, ir, fun, expr, out);
  if (target_type == IR_TYPE_BYTE_VIEW) return ir_graph_lower_byte_view(graph, ir, fun, expr, out);
  if (target_type == IR_TYPE_MAYBE_BYTE_VIEW) {
    IrValue *value = NULL;
    if (ir_graph_expr_is_byte_view_source(graph, fun, expr)) { if (!ir_graph_lower_byte_view(graph, ir, fun, expr, &value)) return false; *out = ir_new_maybe_byte_view_literal(ir, true, value, line, column); return true; }
    if (!ir_graph_lower_expr(graph, ir, fun, expr, &value)) return false;
    if (value->type == IR_TYPE_MAYBE_BYTE_VIEW) { *out = value; return true; }
    if (value->type == IR_TYPE_BYTE_VIEW) { *out = ir_new_maybe_byte_view_literal(ir, true, value, line, column); return true; }
    ir_free_value(value); ir_graph_mark_unsupported(ir, expr, "typed graph MIR expression cannot initialize Maybe<Span<u8>>", expr && expr->type ? expr->type : "unknown type"); return false;
  }
  if (expr && expr->kind == Z_PROGRAM_GRAPH_NODE_CALL && (target_type == IR_TYPE_BOOL || ir_type_is_value(target_type)) &&
      (ir_compare_op(expr->name, &(IrCompareOp){0}) || ir_binary_op(expr->name, &(IrBinaryOp){0}))) {
    return ir_graph_lower_binary_call(graph, ir, fun, expr, target_type, out);
  }
  if (expr && (expr->kind == Z_PROGRAM_GRAPH_NODE_CALL || expr->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL) &&
      (target_type == IR_TYPE_BOOL || ir_type_is_value(target_type))) {
    return ir_graph_lower_call(graph, ir, fun, expr, target_type, out);
  }
  return ir_graph_lower_expr(graph, ir, fun, expr, out);
}

static bool ir_graph_lower_literal(const ZProgramGraph *graph, const ZProgramGraphNode *expr, IrProgram *ir, IrValue **out) {
  const char *value_text = expr && expr->value ? expr->value : "";
  if (ir_text_eq(value_text, "true") || ir_text_eq(value_text, "false")) {
    *out = ir_new_bool_literal(ir, ir_text_eq(value_text, "true"), ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (ir_text_eq(value_text, "null")) {
    IrTypeKind type = ir_graph_type_kind(graph, expr ? expr->type : NULL);
    if (type == IR_TYPE_MAYBE_BYTE_VIEW) {
      *out = ir_new_maybe_byte_view_literal(ir, false, NULL, ir_graph_line(expr), ir_graph_column(expr));
      return true;
    }
    if (type == IR_TYPE_MAYBE_SCALAR) {
      *out = ir_new_maybe_scalar_literal(ir, false, IR_TYPE_UNSUPPORTED, 0, ir_graph_line(expr), ir_graph_column(expr));
      return true;
    }
  }
  IrTypeKind type = ir_graph_type_kind(graph, expr ? expr->type : NULL);
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
    const ZProgramGraphNode *count_node = ir_graph_ordered_node(graph, expr->id, "arg", 1);
    unsigned long long repeat_count = 0;
    if (count_node && count_node->kind == Z_PROGRAM_GRAPH_NODE_LITERAL && ir_parse_integer_literal(count_node->value, &repeat_count) && repeat_count != (unsigned long long)local->array_len) {
      ir_graph_mark_unsupported(ir, count_node, "typed graph MIR fixed array repeat count must match local type", local->name);
      return false;
    }
    const ZProgramGraphNode *value_node = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    for (size_t i = 0; i < local->array_len; i++) {
      IrValue *index = ir_new_index_literal(ir, (unsigned)i, ir_graph_line(value_node), ir_graph_column(value_node));
      IrValue *value = NULL;
      if (!ir_graph_lower_expr_for_type(graph, ir, fun, value_node, local->element_type, IR_TYPE_UNSUPPORTED, false, ir_graph_line(value_node), ir_graph_column(value_node), &value)) {
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
    if (!ir_graph_lower_expr_for_type(graph, ir, fun, value_node, local->element_type, IR_TYPE_UNSUPPORTED, false, ir_graph_line(value_node), ir_graph_column(value_node), &value)) {
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

static bool ir_graph_lower_record_initializer(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const IrLocal *local, const ZProgramGraphNode *expr, IrInstr **out_items, size_t *out_len, size_t *out_cap, int line, int column) {
  if (!local || !local->is_record || !expr || expr->kind != Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL) {
    ir_mark_unsupported(ir, "typed graph MIR record locals require shape literal initialization", line, column, local ? local->name : "record local");
    return false;
  }
  const ZProgramGraphNode *shape = NULL;
  TypeArgVec shape_args = {0};
  if (!ir_graph_shape_instance(graph, local->shape_name, &shape, &shape_args)) {
    ir_mark_unsupported(ir, "typed graph MIR record shape is unknown", line, column, local->shape_name ? local->shape_name : "record");
    return false;
  }
  if (expr->name && expr->name[0] && shape && shape->name && !ir_text_eq(expr->name, shape->name) && !ir_text_eq(expr->name, local->shape_name)) {
    ir_graph_type_arg_vec_free(&shape_args);
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR record literal type does not match local type", local->shape_name ? local->shape_name : "record local");
    return false;
  }
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = ir_graph_next_edge_by_order(graph, shape->id, "field", have_last, last_order);
    if (!edge) break;
    have_last = true;
    last_order = edge->order;
    const ZProgramGraphNode *field = ir_graph_find_node(graph, edge->to);
    if (!field || field->kind != Z_PROGRAM_GRAPH_NODE_FIELD) {
      ir_graph_type_arg_vec_free(&shape_args);
      ir_mark_unsupported(ir, "typed graph MIR record shape field is malformed", line, column, local->shape_name ? local->shape_name : "record");
      return false;
    }
    const ZProgramGraphNode *init = ir_graph_shape_literal_find_field(graph, expr, field->name);
    const ZProgramGraphNode *field_expr = init ? ir_graph_ordered_node(graph, init->id, "value", 0) : ir_graph_ordered_node(graph, field->id, "default", 0);
    if (!field_expr) {
      ir_graph_type_arg_vec_free(&shape_args);
      ir_graph_mark_unsupported(ir, init ? init : expr, "typed graph MIR record field requires an explicit initializer", field->name ? field->name : "field");
      return false;
    }
    unsigned field_offset = 0;
    IrTypeKind field_type = IR_TYPE_UNSUPPORTED;
    bool field_is_array = false;
    unsigned field_array_len = 0;
    IrTypeKind field_element_type = IR_TYPE_UNSUPPORTED;
    if (!ir_graph_shape_field_info(graph, local->shape_name, field->name, &field_offset, &field_type, &field_is_array, &field_array_len, &field_element_type)) {
      ir_graph_type_arg_vec_free(&shape_args);
      ir_graph_mark_unsupported(ir, field, "typed graph MIR record field type is unsupported", ir_graph_node_type(graph, field));
      return false;
    }
    if (field_is_array) {
      if (field_expr->kind != Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL) {
        ir_graph_type_arg_vec_free(&shape_args);
        ir_graph_mark_unsupported(ir, field_expr, "typed graph MIR record array field requires a matching array literal", field->name);
        return false;
      }
      size_t arg_count = ir_graph_edge_count(graph, field_expr->id, "arg");
      bool repeat = ir_text_eq(field_expr->value, "repeat");
      if ((!repeat && arg_count != field_array_len) || (repeat && arg_count != 2)) {
        ir_graph_type_arg_vec_free(&shape_args);
        ir_graph_mark_unsupported(ir, field_expr, "typed graph MIR record array field literal length must match field type", field->name);
        return false;
      }
      if (repeat) {
        const ZProgramGraphNode *count_node = ir_graph_ordered_node(graph, field_expr->id, "arg", 1);
        unsigned long long repeat_count = 0;
        if (count_node && count_node->kind == Z_PROGRAM_GRAPH_NODE_LITERAL && ir_parse_integer_literal(count_node->value, &repeat_count) && repeat_count != (unsigned long long)field_array_len) {
          ir_graph_type_arg_vec_free(&shape_args);
          ir_graph_mark_unsupported(ir, count_node, "typed graph MIR record array field repeat count must match field type", field->name);
          return false;
        }
      }
      const ZProgramGraphNode *repeat_value = repeat ? ir_graph_ordered_node(graph, field_expr->id, "arg", 0) : NULL;
      for (size_t element_index = 0; element_index < field_array_len; element_index++) {
        const ZProgramGraphNode *element_node = repeat ? repeat_value : ir_graph_ordered_node(graph, field_expr->id, "arg", element_index);
        IrValue *element = NULL;
        if (!ir_graph_lower_expr_for_type(graph, ir, fun, element_node, field_element_type, IR_TYPE_UNSUPPORTED, false, ir_graph_line(element_node), ir_graph_column(element_node), &element)) {
          ir_graph_type_arg_vec_free(&shape_args);
          return false;
        }
        if (element->type != field_element_type) {
          ir_free_value(element);
          ir_graph_type_arg_vec_free(&shape_args);
          ir_graph_mark_unsupported(ir, element_node, "typed graph MIR record array field initializer type does not match element", field->name);
          return false;
        }
        unsigned element_offset = field_offset + (unsigned)element_index * ir_type_byte_size(field_element_type);
        ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_FIELD_STORE, .local_index = local->index, .field_offset = element_offset, .value = element, .line = ir_graph_line(element_node), .column = ir_graph_column(element_node)});
      }
      continue;
    }
    IrValue *value = NULL;
    if (!ir_graph_lower_expr_for_type(graph, ir, fun, field_expr, field_type, IR_TYPE_UNSUPPORTED, false, ir_graph_line(field_expr), ir_graph_column(field_expr), &value)) {
      ir_graph_type_arg_vec_free(&shape_args);
      return false;
    }
    if (value->type != field_type) {
      ir_free_value(value);
      ir_graph_type_arg_vec_free(&shape_args);
      ir_graph_mark_unsupported(ir, field_expr, "typed graph MIR record field initializer type does not match field", field->name);
      return false;
    }
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_FIELD_STORE, .local_index = local->index, .field_offset = field_offset, .value = value, .line = ir_graph_line(field_expr), .column = ir_graph_column(field_expr)});
  }
  ir_graph_type_arg_vec_free(&shape_args);
  return true;
}

static bool ir_graph_lower_fixed_resource_initializer(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const IrLocal *local, const ZProgramGraphNode *expr, IrInstr **out_items, size_t *out_len, size_t *out_cap, bool *handled) {
  *handled = false;
  if (ir_graph_lower_fixed_writer_initializer(graph, ir, fun, local, expr, out_items, out_len, out_cap) ||
      ir_graph_lower_fixed_reader_initializer(graph, ir, fun, local, expr, out_items, out_len, out_cap) ||
      ir_graph_lower_fixed_map_initializer(graph, ir, fun, local, expr, out_items, out_len, out_cap) ||
      ir_graph_lower_fixed_ring_buffer_initializer(graph, ir, fun, local, expr, out_items, out_len, out_cap) ||
      ir_graph_lower_fixed_deque_initializer(graph, ir, fun, local, expr, out_items, out_len, out_cap) ||
      ir_graph_lower_fixed_set_initializer(graph, ir, fun, local, expr, out_items, out_len, out_cap)) {
    *handled = true;
    return ir->mir_valid;
  }
  return true;
}

static const ZProgramGraphNode *ir_graph_find_const(const ZProgramGraph *graph, const char *name) {
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_CONST && ir_text_eq(node->name, name)) return node;
  }
  return NULL;
}

static bool ir_graph_lower_const_reference(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const ZProgramGraphNode *const_decl, IrValue **out) {
  const ZProgramGraphNode *const_value = ir_graph_ordered_node(graph, const_decl->id, "value", 0);
  if (!const_value) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR const declaration has no value", expr->name);
    return false;
  }
  if (ir->const_lower_depth >= 16) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR const reference chain is too deep", expr->name);
    return false;
  }
  ir->const_lower_depth++;
  IrTypeKind const_type = ir_graph_type_kind(graph, const_decl->type);
  bool lowered = (ir_type_is_value(const_type) || const_type == IR_TYPE_BOOL)
    ? ir_graph_lower_expr_for_type(graph, ir, fun, const_value, const_type, IR_TYPE_UNSUPPORTED, false, ir_graph_line(expr), ir_graph_column(expr), out)
    : ir_graph_lower_expr(graph, ir, fun, const_value, out);
  ir->const_lower_depth--;
  return lowered;
}

static bool ir_graph_lower_identifier(const ZProgramGraph *graph, const ZProgramGraphNode *expr, IrProgram *ir, const IrFunction *fun, IrValue **out) {
  const IrLocal *local = ir_function_find_local(fun, expr ? expr->name : NULL);
  if (!local) {
    const ZProgramGraphNode *const_decl = expr ? ir_graph_find_const(graph, expr->name) : NULL;
    if (const_decl) return ir_graph_lower_const_reference(graph, ir, fun, expr, const_decl, out);
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR identifier is not a local", expr && expr->name ? expr->name : "unknown identifier");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_LOCAL, local->type, ir_graph_line(expr), ir_graph_column(expr));
  value->local_index = local->index;
  if (local->type == IR_TYPE_BYTE_VIEW) value->element_type = local->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : local->element_type;
  else value->element_type = local->element_type;
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
      IrTypeKind value_type = ir_graph_type_kind(graph, ir_graph_node_type(graph, expr));
      if (!(value_type == IR_TYPE_BOOL || ir_type_is_value(value_type))) value_type = local->element_type;
      if (!(value_type == IR_TYPE_BOOL || ir_type_is_value(value_type))) value_type = IR_TYPE_I32;
      IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_VALUE, value_type, ir_graph_line(expr), ir_graph_column(expr));
      value->local_index = local->index;
      *out = value;
      return true;
    }
  }
  if (left && left->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER) {
    const IrLocal *local = ir_function_find_local(fun, left->name);
    unsigned field_offset = 0;
    IrTypeKind field_type = IR_TYPE_UNSUPPORTED;
    bool field_is_array = false;
    IrTypeKind field_element_type = IR_TYPE_UNSUPPORTED;
    if (local && (local->is_record || local->is_record_ref) && ir_graph_shape_field_info(graph, local->shape_name, expr->name, &field_offset, &field_type, &field_is_array, NULL, &field_element_type)) {
      if (field_is_array) {
        ir_graph_mark_unsupported(ir, expr, "typed graph MIR record array field load requires indexing", expr && expr->name ? expr->name : "array field");
        return false;
      }
      IrValue *value = ir_new_value(ir, IR_VALUE_FIELD_LOAD, field_type, ir_graph_line(expr), ir_graph_column(expr));
      value->local_index = local->index;
      value->field_offset = field_offset;
      value->element_type = field_type == IR_TYPE_BYTE_VIEW ? field_element_type : field_type;
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
  return ir_graph_lower_expr_for_type(graph, ir, fun, arg, expected, IR_TYPE_UNSUPPORTED, false, ir_graph_line(arg), ir_graph_column(arg), out);
}

static bool ir_graph_node_is_untyped_literal(const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  return node && node->kind == Z_PROGRAM_GRAPH_NODE_LITERAL && ir_graph_type_kind(graph, node->type) == IR_TYPE_UNSUPPORTED;
}

static bool ir_graph_lower_binary_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrTypeKind preferred_type, IrValue **out) {
  const ZProgramGraphNode *left_node = ir_graph_ordered_node(graph, expr->id, "left", 0);
  const ZProgramGraphNode *right_node = ir_graph_ordered_node(graph, expr->id, "right", 1);
  IrCompareOp cmp = IR_CMP_EQ;
  if (ir_compare_op(expr->name, &cmp)) {
    IrValue *left = NULL;
    IrValue *right = NULL;
    bool lower_right_first = ir_graph_node_is_untyped_literal(graph, left_node) && !ir_graph_node_is_untyped_literal(graph, right_node);
    if (lower_right_first) {
      if (!ir_graph_lower_expr(graph, ir, fun, right_node, &right) ||
          !ir_graph_lower_expr_for_type(graph, ir, fun, left_node, right ? right->type : IR_TYPE_UNSUPPORTED, IR_TYPE_UNSUPPORTED, false, ir_graph_line(left_node), ir_graph_column(left_node), &left)) {
        ir_free_value(left);
        ir_free_value(right);
        return false;
      }
    } else if (!ir_graph_lower_expr(graph, ir, fun, left_node, &left) ||
               !ir_graph_lower_expr_for_type(graph, ir, fun, right_node, left ? left->type : IR_TYPE_UNSUPPORTED, IR_TYPE_UNSUPPORTED, false, ir_graph_line(right_node), ir_graph_column(right_node), &right)) {
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
  IrTypeKind type = ir_graph_type_kind(graph, ir_graph_node_type(graph, expr));
  if (!(type == IR_TYPE_BOOL || ir_type_is_value(type)) && (preferred_type == IR_TYPE_BOOL || ir_type_is_value(preferred_type))) type = preferred_type;
  if (op == IR_BIN_AND || op == IR_BIN_OR) type = IR_TYPE_BOOL;
  if (type == IR_TYPE_BOOL || ir_type_is_value(type)) {
    if (!ir_graph_lower_expr_for_type(graph, ir, fun, left_node, type, IR_TYPE_UNSUPPORTED, false, ir_graph_line(left_node), ir_graph_column(left_node), &left) ||
        !ir_graph_lower_expr_for_type(graph, ir, fun, right_node, type, IR_TYPE_UNSUPPORTED, false, ir_graph_line(right_node), ir_graph_column(right_node), &right)) {
      ir_free_value(left);
      ir_free_value(right);
      return false;
    }
  } else if (ir_graph_node_is_untyped_literal(graph, left_node) && !ir_graph_node_is_untyped_literal(graph, right_node)) {
    if (!ir_graph_lower_expr(graph, ir, fun, right_node, &right) ||
        !ir_graph_lower_expr_for_type(graph, ir, fun, left_node, right ? right->type : IR_TYPE_UNSUPPORTED, IR_TYPE_UNSUPPORTED, false, ir_graph_line(left_node), ir_graph_column(left_node), &left)) {
      ir_free_value(left);
      ir_free_value(right);
      return false;
    }
    type = right->type;
  } else {
    if (!ir_graph_lower_expr(graph, ir, fun, left_node, &left) ||
        !ir_graph_lower_expr_for_type(graph, ir, fun, right_node, left ? left->type : IR_TYPE_UNSUPPORTED, IR_TYPE_UNSUPPORTED, false, ir_graph_line(right_node), ir_graph_column(right_node), &right)) {
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

static bool ir_graph_lower_named_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, IrTypeKind preferred_return_type, IrValue **out) {
  unsigned callee_index = 0;
  char *specialized_name = NULL;
  if (!ir_find_function_index(ir, callee_name, &callee_index)) {
    const ZProgramGraphNode *generic = ir_graph_find_function_by_name(graph, callee_name);
    if (generic && ir_graph_function_has_type_params(graph, generic)) {
      char **param_names = NULL;
      char **arg_types = NULL;
      size_t binding_len = 0;
      if (!ir_graph_specialization_bindings_for_call(graph, fun, generic, expr, preferred_return_type, &param_names, &arg_types, &binding_len)) {
        ir_graph_mark_unsupported(ir, expr, "typed graph MIR generic specialization is missing from function table", callee_name ? callee_name : "unknown generic");
        return false;
      }
      specialized_name = ir_graph_specialized_function_name_for_types(generic, arg_types, binding_len);
      for (size_t i = 0; i < binding_len; i++) {
        free(param_names[i]);
        free(arg_types[i]);
      }
      free(param_names);
      free(arg_types);
      if (!ir_find_function_index(ir, specialized_name, &callee_index)) {
        ir_graph_mark_unsupported(ir, expr, "typed graph MIR generic specialization is missing from function table", specialized_name);
        free(specialized_name);
        return false;
      }
    } else {
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR call target is unsupported", callee_name ? callee_name : "unknown callee");
      return false;
    }
  }
  IrFunction *callee = &ir->functions[callee_index];
  IrValue *value = ir_new_value(ir, IR_VALUE_CALL, callee->raises ? IR_TYPE_I64 : callee->value_return_type, ir_graph_line(expr), ir_graph_column(expr));
  value->callee_index = callee_index;
  if (callee->raises) value->element_type = callee->value_return_type;
  else if (callee->value_return_type == IR_TYPE_BYTE_VIEW) value->element_type = callee->return_element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : callee->return_element_type;
  else if (callee->value_return_type == IR_TYPE_MAYBE_SCALAR) value->element_type = callee->return_element_type;
  else value->element_type = callee->value_return_type;
  size_t arg_index = callee->world_param_name ? 1 : 0;
  size_t lowered_arg_count = 0;
  for (size_t local_index = 0; local_index < callee->local_len && lowered_arg_count < callee->param_count; local_index++) {
    const IrLocal *param = &callee->locals[local_index];
    if (!param->is_param) continue;
    IrValue *arg = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, arg_index, param->type, &arg)) {
      ir_free_value(value);
      free(specialized_name);
      return false;
    }
    if (param->is_record_ref) {
      const IrLocal *source = NULL;
      if ((arg->kind == IR_VALUE_RECORD_ADDR || arg->kind == IR_VALUE_LOCAL) && arg->local_index < fun->local_len) {
        const IrLocal *candidate = &fun->locals[arg->local_index];
        if ((arg->kind == IR_VALUE_RECORD_ADDR && candidate->is_record) || (arg->kind == IR_VALUE_LOCAL && candidate->is_record_ref)) source = candidate;
      }
      char *source_shape = source ? ir_graph_resolve_alias_type_alloc(graph, source->shape_name) : NULL;
      char *param_shape = ir_graph_resolve_alias_type_alloc(graph, param->shape_name);
      bool shapes_match = source && ir_text_eq(source_shape ? source_shape : source->shape_name, param_shape ? param_shape : param->shape_name);
      free(source_shape);
      free(param_shape);
      if (!shapes_match) {
        ir_free_value(arg);
        ir_free_value(value);
        ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", arg_index), "typed graph MIR reference argument must borrow a matching local shape variable", param->shape_name ? param->shape_name : "shape");
        free(specialized_name);
        return false;
      }
    }
    ir_value_push_arg(ir, value, arg);
    arg_index++;
    lowered_arg_count++;
  }
  if (arg_index != ir_graph_edge_count(graph, expr->id, "arg")) {
    ir_free_value(value);
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR call arity does not match target", callee_name ? callee_name : "unknown callee");
    free(specialized_name);
    return false;
  }
  *out = value;
  free(specialized_name);
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
  if (!z_program_graph_find_c_import_function(c_import, ir, ir->target, symbol, &function)) {
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

static const char *ir_graph_json_error_label(unsigned long long code, bool expected) {
  if (expected) {
    if (code == 0) return "none";
    if (code == 1) return "valid-json";
    if (code == 2) return "end-of-input";
    return "unknown";
  }
  if (code == 0) return "ok";
  if (code == 1) return "invalid";
  if (code == 2) return "trailing";
  return "unknown";
}

static const char *ir_graph_term_sequence(const char *name) {
  static const struct { const char *name; const char *sequence; } sequences[] = {
    {"std.term.reset", "\x1b[0m"},
    {"std.term.bold", "\x1b[1m"},
    {"std.term.dim", "\x1b[2m"},
    {"std.term.underline", "\x1b[4m"},
    {"std.term.inverse", "\x1b[7m"},
    {"std.term.fgDefault", "\x1b[39m"},
    {"std.term.fgBlack", "\x1b[30m"},
    {"std.term.fgRed", "\x1b[31m"},
    {"std.term.fgGreen", "\x1b[32m"},
    {"std.term.fgYellow", "\x1b[33m"},
    {"std.term.fgBlue", "\x1b[34m"},
    {"std.term.fgMagenta", "\x1b[35m"},
    {"std.term.fgCyan", "\x1b[36m"},
    {"std.term.fgWhite", "\x1b[37m"},
    {"std.term.bgDefault", "\x1b[49m"},
    {"std.term.bgBlack", "\x1b[40m"},
    {"std.term.bgRed", "\x1b[41m"},
    {"std.term.bgGreen", "\x1b[42m"},
    {"std.term.bgYellow", "\x1b[43m"},
    {"std.term.bgBlue", "\x1b[44m"},
    {"std.term.bgMagenta", "\x1b[45m"},
    {"std.term.bgCyan", "\x1b[46m"},
    {"std.term.bgWhite", "\x1b[47m"},
    {"std.term.clearScreen", "\x1b[2J"},
    {"std.term.clearScreenDown", "\x1b[0J"},
    {"std.term.clearScreenUp", "\x1b[1J"},
    {"std.term.clearLine", "\x1b[2K"},
    {"std.term.clearLineRight", "\x1b[0K"},
    {"std.term.clearLineLeft", "\x1b[1K"},
    {"std.term.cursorHome", "\x1b[H"},
    {"std.term.saveCursor", "\x1b[s"},
    {"std.term.restoreCursor", "\x1b[u"},
    {"std.term.hideCursor", "\x1b[?25l"},
    {"std.term.showCursor", "\x1b[?25h"},
    {"std.term.enterAltScreen", "\x1b[?1049h"},
    {"std.term.leaveAltScreen", "\x1b[?1049l"},
    {"std.term.enterBracketedPaste", "\x1b[?2004h"},
    {"std.term.leaveBracketedPaste", "\x1b[?2004l"},
    {"std.term.enterMouseCapture", "\x1b[?1000h\x1b[?1002h\x1b[?1006h"},
    {"std.term.leaveMouseCapture", "\x1b[?1006l\x1b[?1002l\x1b[?1000l"},
  };
  if (!name) return NULL;
  for (size_t i = 0; i < sizeof(sequences) / sizeof(sequences[0]); i++) {
    if (ir_text_eq(name, sequences[i].name)) return sequences[i].sequence;
  }
  return NULL;
}

static bool ir_graph_term_key_constant(const char *name, unsigned long long *out) {
  static const struct { const char *name; unsigned long long code; } entries[] = {
    {"std.term.keyNone", 0ull},
    {"std.term.keyEscape", 27ull},
    {"std.term.keyEnter", 13ull},
    {"std.term.keyTab", 9ull},
    {"std.term.keyBackspace", 127ull},
    {"std.term.keyCtrlA", 1ull},
    {"std.term.keyCtrlC", 3ull},
    {"std.term.keyCtrlD", 4ull},
    {"std.term.keyCtrlE", 5ull},
    {"std.term.keyCtrlK", 11ull},
    {"std.term.keyCtrlL", 12ull},
    {"std.term.keyCtrlN", 14ull},
    {"std.term.keyCtrlP", 16ull},
    {"std.term.keyCtrlR", 18ull},
    {"std.term.keyCtrlU", 21ull},
    {"std.term.keyCtrlW", 23ull},
    {"std.term.keyArrowUp", 1114113ull},
    {"std.term.keyArrowDown", 1114114ull},
    {"std.term.keyArrowRight", 1114115ull},
    {"std.term.keyArrowLeft", 1114116ull},
    {"std.term.keyDelete", 1114117ull},
    {"std.term.keyHome", 1114118ull},
    {"std.term.keyEnd", 1114119ull},
    {"std.term.keyPageUp", 1114120ull},
    {"std.term.keyPageDown", 1114121ull},
    {"std.term.keyInsert", 1114122ull},
    {"std.term.keyShiftTab", 1114123ull},
    {"std.term.keyF1", 1114124ull},
    {"std.term.keyF2", 1114125ull},
    {"std.term.keyF3", 1114126ull},
    {"std.term.keyF4", 1114127ull},
    {"std.term.keyF5", 1114128ull},
    {"std.term.keyF6", 1114129ull},
    {"std.term.keyF7", 1114130ull},
    {"std.term.keyF8", 1114131ull},
    {"std.term.keyF9", 1114132ull},
    {"std.term.keyF10", 1114133ull},
    {"std.term.keyF11", 1114134ull},
    {"std.term.keyF12", 1114135ull},
    {"std.term.keyPasteStart", 1114136ull},
    {"std.term.keyPasteEnd", 1114137ull},
  };
  for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
    if (ir_text_eq(name, entries[i].name)) {
      *out = entries[i].code;
      return true;
    }
  }
  return false;
}

static int ir_graph_http_error_code(const char *name) {
  static const char *codes[] = {"std.http.errorNone", "std.http.errorInvalidUrl", "std.http.errorUnsupportedProtocol", "std.http.errorDns", "std.http.errorConnect", "std.http.errorTls", "std.http.errorTimeout", "std.http.errorTooLarge", "std.http.errorProviderUnavailable", "std.http.errorIo", "std.http.errorInvalidRequest"};
  for (int i = 0; name && i < (int)(sizeof(codes) / sizeof(codes[0])); i++) if (ir_text_eq(name, codes[i])) return i;
  return -1;
}

static bool ir_graph_http_request_matches_name(const char *name) {
  return ir_text_eq(name, "std.http.requestMatches") ||
         ir_text_eq(name, "__zero_std_http_request_matches");
}

static const char *ir_graph_http_route_method_name(const char *name) {
  if (ir_text_eq(name, "std.http.requestIsGet") || ir_text_eq(name, "__zero_std_http_request_is_get")) return "GET";
  if (ir_text_eq(name, "std.http.requestIsPost") || ir_text_eq(name, "__zero_std_http_request_is_post")) return "POST";
  if (ir_text_eq(name, "std.http.requestIsPut") || ir_text_eq(name, "__zero_std_http_request_is_put")) return "PUT";
  if (ir_text_eq(name, "std.http.requestIsPatch") || ir_text_eq(name, "__zero_std_http_request_is_patch")) return "PATCH";
  if (ir_text_eq(name, "std.http.requestIsDelete") || ir_text_eq(name, "__zero_std_http_request_is_delete")) return "DELETE";
  if (ir_text_eq(name, "std.http.requestIsHead") || ir_text_eq(name, "__zero_std_http_request_is_head")) return "HEAD";
  if (ir_text_eq(name, "std.http.requestIsOptions") || ir_text_eq(name, "__zero_std_http_request_is_options")) return "OPTIONS";
  return NULL;
}

static bool ir_graph_http_request_method_name_call(const char *name) {
  return ir_text_eq(name, "std.http.requestMethodName") ||
         ir_text_eq(name, "__zero_std_http_request_method_name");
}

static bool ir_graph_http_request_path_call(const char *name) {
  return ir_text_eq(name, "std.http.requestPath") ||
         ir_text_eq(name, "__zero_std_http_request_path");
}

static bool ir_graph_http_request_body_within_call(const char *name, bool *require_json) {
  if (!name || !require_json) return false;
  if (ir_text_eq(name, "std.http.requestBodyWithin") ||
      ir_text_eq(name, "__zero_std_http_request_body_within")) {
    *require_json = false;
    return true;
  }
  if (ir_text_eq(name, "std.http.requestJsonBodyWithin") ||
      ir_text_eq(name, "__zero_std_http_request_json_body_within")) {
    *require_json = true;
    return true;
  }
  return false;
}

static bool ir_graph_http_json_status_writer(const char *name, unsigned *status) {
  if (!name || !status) return false;
  if (ir_text_eq(name, "std.http.writeJsonOk") || ir_text_eq(name, "__zero_std_http_write_json_ok")) { *status = 200; return true; }
  if (ir_text_eq(name, "std.http.writeJsonCreated") || ir_text_eq(name, "__zero_std_http_write_json_created")) { *status = 201; return true; }
  if (ir_text_eq(name, "std.http.writeJsonBadRequest") || ir_text_eq(name, "__zero_std_http_write_json_bad_request")) { *status = 400; return true; }
  if (ir_text_eq(name, "std.http.writeJsonUnauthorized") || ir_text_eq(name, "__zero_std_http_write_json_unauthorized")) { *status = 401; return true; }
  if (ir_text_eq(name, "std.http.writeJsonForbidden") || ir_text_eq(name, "__zero_std_http_write_json_forbidden")) { *status = 403; return true; }
  if (ir_text_eq(name, "std.http.writeJsonNotFound") || ir_text_eq(name, "__zero_std_http_write_json_not_found")) { *status = 404; return true; }
  if (ir_text_eq(name, "std.http.writeJsonMethodNotAllowed") || ir_text_eq(name, "__zero_std_http_write_json_method_not_allowed")) { *status = 405; return true; }
  if (ir_text_eq(name, "std.http.writeJsonConflict") || ir_text_eq(name, "__zero_std_http_write_json_conflict")) { *status = 409; return true; }
  if (ir_text_eq(name, "std.http.writeJsonUnprocessable") || ir_text_eq(name, "__zero_std_http_write_json_unprocessable")) { *status = 422; return true; }
  if (ir_text_eq(name, "std.http.writeJsonTooManyRequests") || ir_text_eq(name, "__zero_std_http_write_json_too_many_requests")) { *status = 429; return true; }
  if (ir_text_eq(name, "std.http.writeJsonInternalServerError") || ir_text_eq(name, "__zero_std_http_write_json_internal_server_error")) { *status = 500; return true; }
  return false;
}

static unsigned ir_graph_http_method_code(const char *name) {
  static const char *methods[] = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"};
  for (unsigned i = 0; name && i < (unsigned)(sizeof(methods) / sizeof(methods[0])); i++) if (ir_text_eq(name, methods[i])) return i + 1;
  return 0;
}

static bool ir_graph_discard_typed_arg(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, size_t order, IrTypeKind type) {
  IrValue *value = NULL; bool ok = ir_graph_lower_ordered_arg(graph, ir, fun, expr, order, type, &value); ir_free_value(value); return ok;
}

static bool ir_graph_lower_byte_arg_with_discard(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, size_t byte_order, size_t discard_order, IrTypeKind discard_type, IrValue **out) {
  if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", byte_order), out)) return false;
  if (ir_graph_discard_typed_arg(graph, ir, fun, expr, discard_order, discard_type)) return true;
  ir_free_value(*out); *out = NULL; return false;
}

static IrValue *ir_graph_handle_literal(IrProgram *ir, IrTypeKind type, const ZProgramGraphNode *expr) {
  return ir_new_integer_literal_value(ir, type, 1, ir_graph_line(expr), ir_graph_column(expr));
}

static bool ir_graph_lower_net_std_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  if (ir_text_eq(callee_name, "std.net.host") && arg_count == 0) {
    *out = ir_graph_handle_literal(ir, IR_TYPE_I32, expr); return true;
  }
  if (ir_text_eq(callee_name, "std.net.localhost") && arg_count == 1) {
    if (!ir_graph_discard_typed_arg(graph, ir, fun, expr, 0, IR_TYPE_U16)) return false;
    return ir_make_string_literal_value(ir, "localhost", ir_graph_line(expr), ir_graph_column(expr), out);
  }
  if (ir_text_eq(callee_name, "std.net.loopback") && arg_count == 1) {
    if (!ir_graph_discard_typed_arg(graph, ir, fun, expr, 0, IR_TYPE_U16)) return false;
    return ir_make_string_literal_value(ir, "127.0.0.1", ir_graph_line(expr), ir_graph_column(expr), out);
  }
  if (ir_text_eq(callee_name, "std.net.address") && arg_count == 2) {
    return ir_graph_lower_byte_arg_with_discard(graph, ir, fun, expr, 0, 1, IR_TYPE_U16, out);
  }
  if (ir_text_eq(callee_name, "std.net.withTimeout") && arg_count == 2) {
    return ir_graph_lower_byte_arg_with_discard(graph, ir, fun, expr, 0, 1, IR_TYPE_I64, out);
  }
  if (ir_text_eq(callee_name, "std.net.dnsName") && arg_count == 1) {
    return ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), out);
  }
  if ((ir_text_eq(callee_name, "std.net.connect") || ir_text_eq(callee_name, "std.net.listen")) && arg_count == 2) {
    IrValue *address = NULL;
    bool ok = ir_graph_discard_typed_arg(graph, ir, fun, expr, 0, IR_TYPE_I32) &&
              ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &address);
    ir_free_value(address);
    if (!ok) return false;
    *out = ir_new_maybe_scalar_literal(ir, false, IR_TYPE_I32, 0, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  *handled = false;
  return true;
}

static bool ir_graph_lower_http_capability_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  if (ir_text_eq(callee_name, "std.http.client") && arg_count == 1) {
    if (!ir_graph_discard_typed_arg(graph, ir, fun, expr, 0, IR_TYPE_I32)) return false;
    *out = ir_graph_handle_literal(ir, IR_TYPE_I32, expr); return true;
  }
  if (ir_text_eq(callee_name, "std.http.server") && arg_count == 2) {
    IrValue *address = NULL;
    bool ok = ir_graph_discard_typed_arg(graph, ir, fun, expr, 0, IR_TYPE_I32) &&
              ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &address);
    ir_free_value(address);
    if (!ok) return false;
    *out = ir_graph_handle_literal(ir, IR_TYPE_I32, expr); return true;
  }
  if (ir_text_eq(callee_name, "std.http.parseMethod") && arg_count == 1) {
    const ZProgramGraphNode *method = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    IrValue *method_view = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, method, &method_view)) return false;
    ir_free_value(method_view);
    const char *text = method && method->kind == Z_PROGRAM_GRAPH_NODE_LITERAL ? method->value : NULL;
    *out = ir_new_integer_literal_value(ir, IR_TYPE_U32, ir_graph_http_method_code(text), ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (ir_text_eq(callee_name, "std.http.tlsBoundary") && arg_count == 0) {
    return ir_make_string_literal_value(ir, "platform-or-c-library", ir_graph_line(expr), ir_graph_column(expr), out);
  }
  *handled = false;
  return true;
}

static bool ir_graph_lower_http_fetch_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, size_t arg_count, bool *handled, IrValue **out) {
  *handled = false;
  if (arg_count != 4) return true;
  *handled = true;
  IrValue *client = NULL, *request = NULL, *response = NULL, *timeout = NULL;
  if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_I32, &client) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &request) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 2), &response) ||
      !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 3, IR_TYPE_I64, &timeout)) {
    ir_free_value(client); ir_free_value(request); ir_free_value(response); ir_free_value(timeout); return false;
  }
  ir_free_value(client);
  if (!timeout || timeout->type != IR_TYPE_I64) {
    ir_free_value(request); ir_free_value(response); ir_free_value(timeout);
    ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 3), "typed graph MIR std.http.fetch timeout must be a Duration", "non-Duration timeout");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_HTTP_FETCH, IR_TYPE_U64, ir_graph_line(expr), ir_graph_column(expr));
  value->left = request; value->right = response; value->index = timeout;
  if (ir->direct_runtime_helper_count < 2) ir->direct_runtime_helper_count = 2;
  if (ir->direct_host_runtime_import_count < 2) ir->direct_host_runtime_import_count = 2;
  if (ir->direct_http_runtime_import_count < 1) ir->direct_http_runtime_import_count = 1;
  *out = value;
  return true;
}

static bool ir_graph_lower_http_request_match_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = false;
  const char *route_method = ir_graph_http_route_method_name(callee_name);
  if (!ir_graph_http_request_matches_name(callee_name) && !route_method) return true;
  if ((!route_method && arg_count != 3) || (route_method && arg_count != 2)) return true;
  *handled = true;
  IrValue *request = NULL; IrValue *method = NULL; IrValue *path = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &request) ||
      !(route_method ? ir_make_string_literal_value(ir, route_method, ir_graph_line(expr), ir_graph_column(expr), &method) :
                       ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &method)) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", route_method ? 1 : 2), &path)) {
    ir_free_value(request); ir_free_value(method); ir_free_value(path);
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_HTTP_REQUEST_MATCHES, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
  value->left = request; value->index = method; value->right = path;
  ir_graph_require_runtime_helper(ir); *out = value; return true;
}

static bool ir_graph_lower_http_request_body_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = false;
  bool require_json = false;
  if (!ir_graph_http_request_body_within_call(callee_name, &require_json) || arg_count != 2) return true;
  *handled = true;
  IrValue *request = NULL;
  IrValue *max = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &request) ||
      !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_USIZE, &max)) {
    ir_free_value(request); ir_free_value(max);
    return false;
  }
  if (!max || max->type != IR_TYPE_USIZE) {
    ir_free_value(request); ir_free_value(max);
    ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), "typed graph MIR std.http request body limit must be usize", "non-usize body limit");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_HTTP_REQUEST_BODY_WITHIN, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
  value->left = request; value->index = max; value->int_value = require_json ? 1 : 0;
  ir_graph_require_runtime_helper(ir); *out = value; return true;
}

static bool ir_graph_lower_http_json_status_writer_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = false;
  unsigned json_status = 0;
  if (!ir_graph_http_json_status_writer(callee_name, &json_status) || arg_count != 2) return true;
  *handled = true;
  IrValue *buffer = NULL;
  IrValue *body = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &buffer) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &body)) {
    ir_free_value(buffer); ir_free_value(body);
    return false;
  }
  IrValue *status = ir_new_integer_literal_value(ir, IR_TYPE_U16, json_status, ir_graph_line(expr), ir_graph_column(expr));
  IrValue *value = ir_new_value(ir, IR_VALUE_HTTP_WRITE_JSON_RESPONSE, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
  value->left = buffer; value->index = status; value->right = body; ir_graph_require_runtime_helper(ir); *out = value; return true;
}

static bool ir_graph_lower_http_std_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  bool local_handled = false;
  if (!ir_graph_lower_net_std_call(graph, ir, fun, expr, callee_name, arg_count, &local_handled, out)) return false;
  if (local_handled) return true;
  if (!ir_graph_lower_http_capability_call(graph, ir, fun, expr, callee_name, arg_count, &local_handled, out)) return false;
  if (local_handled) return true;
  if (ir_text_eq(callee_name, "std.http.listen") && (arg_count == 1 || arg_count == 2)) {
    IrValue *value = ir_new_integer_literal_value(ir, IR_TYPE_I64, 0, ir_graph_line(expr), ir_graph_column(expr));
    value->element_type = IR_TYPE_VOID;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.http.fetch")) {
    if (!ir_graph_lower_http_fetch_call(graph, ir, fun, expr, arg_count, &local_handled, out)) return false;
    if (local_handled) return true;
  }
  int http_error_code = ir_graph_http_error_code(callee_name);
  if (http_error_code >= 0 && arg_count == 0) { *out = ir_new_integer_literal_value(ir, IR_TYPE_U32, (unsigned long long)http_error_code, ir_graph_line(expr), ir_graph_column(expr)); return true; }
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
    value->left = status; value->int_value = status_lower; value->data_len = status_upper; *out = value; return true;
  }
  if ((ir_text_eq(callee_name, "std.http.resultOk") || ir_text_eq(callee_name, "std.http.resultStatus") || ir_text_eq(callee_name, "std.http.resultBodyLen") || ir_text_eq(callee_name, "std.http.resultError")) && arg_count == 1) {
    IrValue *result = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_U64, &result)) return false;
    if (!result || result->type != IR_TYPE_U64) { ir_free_value(result); ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 0), "typed graph MIR HTTP result helper expects HttpResult", "non-HttpResult argument"); return false; }
    IrValueKind kind = IR_VALUE_HTTP_RESULT_ERROR; IrTypeKind type = IR_TYPE_U32;
    if (ir_text_eq(callee_name, "std.http.resultOk")) { kind = IR_VALUE_HTTP_RESULT_OK; type = IR_TYPE_BOOL; }
    else if (ir_text_eq(callee_name, "std.http.resultStatus")) { kind = IR_VALUE_HTTP_RESULT_STATUS; type = IR_TYPE_U16; }
    else if (ir_text_eq(callee_name, "std.http.resultBodyLen")) { kind = IR_VALUE_HTTP_RESULT_BODY_LEN; type = IR_TYPE_USIZE; }
    IrValue *value = ir_new_value(ir, kind, type, ir_graph_line(expr), ir_graph_column(expr));
    value->left = result; ir_graph_require_runtime_helper(ir); *out = value; return true;
  }
  if ((ir_text_eq(callee_name, "std.http.responseLen") || ir_text_eq(callee_name, "std.http.responseHeadersLen") || ir_text_eq(callee_name, "std.http.responseBodyOffset")) && arg_count == 1) {
    IrValue *response = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &response)) return false;
    IrValueKind kind = IR_VALUE_HTTP_RESPONSE_LEN;
    if (ir_text_eq(callee_name, "std.http.responseHeadersLen")) kind = IR_VALUE_HTTP_RESPONSE_HEADERS_LEN;
    else if (ir_text_eq(callee_name, "std.http.responseBodyOffset")) kind = IR_VALUE_HTTP_RESPONSE_BODY_OFFSET;
    IrValue *value = ir_new_value(ir, kind, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    value->left = response; ir_graph_require_runtime_helper(ir); *out = value; return true;
  }
  if (ir_text_eq(callee_name, "std.http.headerValue") && arg_count == 2) {
    IrValue *headers = NULL; IrValue *name = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &headers) || !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &name)) { ir_free_value(headers); ir_free_value(name); return false; }
    IrValue *value = ir_new_value(ir, IR_VALUE_HTTP_HEADER_VALUE, IR_TYPE_U64, ir_graph_line(expr), ir_graph_column(expr));
    value->left = headers; value->right = name; ir_graph_require_runtime_helper(ir); *out = value; return true;
  }
  if (!ir_graph_lower_http_request_match_call(graph, ir, fun, expr, callee_name, arg_count, &local_handled, out)) return false;
  if (local_handled) return true;
  if ((ir_graph_http_request_method_name_call(callee_name) || ir_graph_http_request_path_call(callee_name)) && arg_count == 1) {
    IrValue *request = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &request)) return false;
    IrValue *value = ir_new_value(ir, ir_graph_http_request_method_name_call(callee_name) ? IR_VALUE_HTTP_REQUEST_METHOD_NAME : IR_VALUE_HTTP_REQUEST_PATH, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = request; ir_graph_require_runtime_helper(ir); *out = value; return true;
  }
  if (!ir_graph_lower_http_request_body_call(graph, ir, fun, expr, callee_name, arg_count, &local_handled, out)) return false;
  if (local_handled) return true;
  if ((ir_text_eq(callee_name, "std.http.headerFound") || ir_text_eq(callee_name, "std.http.headerOffset") || ir_text_eq(callee_name, "std.http.headerLen")) && arg_count == 1) {
    IrValue *header = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_U64, &header)) return false;
    if (!header || header->type != IR_TYPE_U64) {
      ir_free_value(header);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 0), "typed graph MIR HTTP header helper expects HttpHeaderValue", "non-HttpHeaderValue argument");
      return false;
    }
    IrValueKind kind = IR_VALUE_HTTP_HEADER_LEN;
    IrTypeKind type = IR_TYPE_USIZE;
    if (ir_text_eq(callee_name, "std.http.headerFound")) { kind = IR_VALUE_HTTP_HEADER_FOUND; type = IR_TYPE_BOOL; }
    else if (ir_text_eq(callee_name, "std.http.headerOffset")) kind = IR_VALUE_HTTP_HEADER_OFFSET;
    IrValue *value = ir_new_value(ir, kind, type, ir_graph_line(expr), ir_graph_column(expr));
    value->left = header; ir_graph_require_runtime_helper(ir); *out = value; return true;
  }
  if (!ir_graph_lower_http_json_status_writer_call(graph, ir, fun, expr, callee_name, arg_count, &local_handled, out)) return false;
  if (local_handled) return true;
  if (ir_text_eq(callee_name, "std.http.writeJsonResponse") && arg_count == 3) {
    IrValue *buffer = NULL;
    IrValue *status = NULL;
    IrValue *body = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &buffer) ||
        !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_U16, &status) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 2), &body)) {
      ir_free_value(buffer); ir_free_value(status); ir_free_value(body);
      return false;
    }
    if (status->type != IR_TYPE_U16) {
      ir_free_value(buffer); ir_free_value(status); ir_free_value(body);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), "typed graph MIR std.http.writeJsonResponse status must be u16", "non-u16 status");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_HTTP_WRITE_JSON_RESPONSE, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = buffer; value->index = status; value->right = body; ir_graph_require_runtime_helper(ir); *out = value; return true;
  }
  *handled = false;
  return true;
}

static bool ir_graph_lower_std_str_arg(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, size_t order, IrTypeKind expected, IrValue **out) {
  const ZProgramGraphNode *arg = ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", order);
  if (expected == IR_TYPE_BYTE_VIEW) return ir_graph_lower_byte_view(graph, ir, fun, arg, out);
  return ir_graph_lower_expr_for_type(graph, ir, fun, arg, expected, IR_TYPE_UNSUPPORTED, false, ir_graph_line(arg), ir_graph_column(arg), out);
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
  if (arg_count == 2 && ir_text_eq(callee_name, "std.crypto.sha256")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_CRYPTO_SHA256, IR_TYPE_MAYBE_BYTE_VIEW, two_views, 2, out);
  if (arg_count == 2 && ir_text_eq(callee_name, "std.crypto.sha256Hex")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_CRYPTO_SHA256_HEX, IR_TYPE_MAYBE_BYTE_VIEW, two_views, 2, out);
  if (arg_count == 3 && ir_text_eq(callee_name, "std.crypto.hmacSha256")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_CRYPTO_HMAC_SHA256, IR_TYPE_MAYBE_BYTE_VIEW, three_views, 3, out);
  if (arg_count == 3 && ir_text_eq(callee_name, "std.crypto.hmacSha256Hex")) return ir_graph_make_std_str_runtime_value(graph, ir, fun, expr, IR_STR_OP_CRYPTO_HMAC_SHA256_HEX, IR_TYPE_MAYBE_BYTE_VIEW, three_views, 3, out);
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
  if (arg_count == 1 && ir_text_eq(callee_name, "std.term.keyCode")) return ir_graph_make_std_parse_runtime_value(graph, ir, fun, expr, IR_PARSE_OP_TERM_KEY_CODE, IR_TYPE_U32, IR_TYPE_UNSUPPORTED, 1, out);
  if (arg_count == 1 && ir_text_eq(callee_name, "std.term.keyByteLen")) return ir_graph_make_std_parse_runtime_value(graph, ir, fun, expr, IR_PARSE_OP_TERM_KEY_BYTE_LEN, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, 1, out);
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

static bool ir_graph_make_std_term_runtime_value(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrTermOp op, IrTypeKind return_type, size_t expected_args, IrValue **out) {
  if (ir_graph_edge_count(graph, expr ? expr->id : NULL, "arg") != expected_args || expected_args > 1) {
    ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.term helper has unsupported arity", "wrong std.term arity");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_TERM_RUNTIME, return_type, ir_graph_line(expr), ir_graph_column(expr));
  value->int_value = (unsigned long long)op;
  if (op == IR_TERM_OP_READ_INPUT) {
    IrValue *buffer = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 0), &buffer)) {
      ir_free_value(value);
      return false;
    }
    value->left = buffer;
    value->element_type = IR_TYPE_USIZE;
  } else if (expected_args == 1) {
    IrValue *fallback = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_USIZE, &fallback)) {
      ir_free_value(value);
      return false;
    }
    if (!fallback || fallback->type != IR_TYPE_USIZE) {
      ir_free_value(fallback);
      ir_free_value(value);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 0), "typed graph MIR std.term fallback argument must be usize", "non-usize argument");
      return false;
    }
    ir_value_push_arg(ir, value, fallback);
  }
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_term_runtime_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  if (arg_count == 0 && ir_text_eq(callee_name, "std.term.stdinIsTty")) {
    return ir_graph_make_std_term_runtime_value(graph, ir, fun, expr, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_BOOL, 0, out);
  }
  if (arg_count == 0 && ir_text_eq(callee_name, "std.term.stdoutIsTty")) {
    return ir_graph_make_std_term_runtime_value(graph, ir, fun, expr, IR_TERM_OP_STDOUT_IS_TTY, IR_TYPE_BOOL, 0, out);
  }
  if (arg_count == 1 && ir_text_eq(callee_name, "std.term.widthOr")) {
    return ir_graph_make_std_term_runtime_value(graph, ir, fun, expr, IR_TERM_OP_WIDTH_OR, IR_TYPE_USIZE, 1, out);
  }
  if (arg_count == 1 && ir_text_eq(callee_name, "std.term.heightOr")) {
    return ir_graph_make_std_term_runtime_value(graph, ir, fun, expr, IR_TERM_OP_HEIGHT_OR, IR_TYPE_USIZE, 1, out);
  }
  if (arg_count == 0 && ir_text_eq(callee_name, "std.term.enterRawMode")) {
    return ir_graph_make_std_term_runtime_value(graph, ir, fun, expr, IR_TERM_OP_ENTER_RAW_MODE, IR_TYPE_BOOL, 0, out);
  }
  if (arg_count == 0 && ir_text_eq(callee_name, "std.term.leaveRawMode")) {
    return ir_graph_make_std_term_runtime_value(graph, ir, fun, expr, IR_TERM_OP_LEAVE_RAW_MODE, IR_TYPE_BOOL, 0, out);
  }
  if (arg_count == 1 && ir_text_eq(callee_name, "std.term.readInput")) {
    return ir_graph_make_std_term_runtime_value(graph, ir, fun, expr, IR_TERM_OP_READ_INPUT, IR_TYPE_MAYBE_SCALAR, 1, out);
  }
  *handled = false;
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
  if (arg_count == 1 && ir_text_eq(callee_name, "std.time.sleep")) return ir_graph_make_std_time_runtime_value(graph, ir, fun, expr, IR_TIME_OP_SLEEP, IR_TYPE_BOOL, 1, out);
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
    return ir_graph_make_std_time_runtime_value(graph, ir, fun, expr, IR_TIME_OP_WALL_SECONDS, IR_TYPE_I64, 0, out);
  }
  if (arg_count == 0 && ir_text_eq(callee_name, "std.time.monotonic")) {
    return ir_graph_make_std_time_runtime_value(graph, ir, fun, expr, IR_TIME_OP_MONOTONIC, IR_TYPE_I64, 0, out);
  }
  *handled = false;
  return true;
}

static bool ir_graph_is_std_rand_entropy_call(const char *callee_name) {
  return ir_text_eq(callee_name, "std.rand.entropyU32") ||
         ir_text_eq(callee_name, "std.rand.entropySeed") ||
         ir_text_eq(callee_name, "std.crypto.secureRandomU32");
}

static const IrLocal *ir_graph_find_mutable_rand_source_local(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *arg, const char *helper_name) {
  const ZProgramGraphNode *left = arg && arg->kind == Z_PROGRAM_GRAPH_NODE_BORROW ? ir_graph_ordered_node(graph, arg->id, "left", 0) : NULL;
  if (!arg || arg->kind != Z_PROGRAM_GRAPH_NODE_BORROW || !arg->is_mutable || !left || left->kind != Z_PROGRAM_GRAPH_NODE_IDENTIFIER) {
    ir_graph_mark_unsupported(ir, arg, "typed graph MIR std.rand helper expects a mutable RandSource local", helper_name ? helper_name : "std.rand");
    return NULL;
  }
  const IrLocal *rng = ir_function_find_local(fun, left->name);
  if (!rng || rng->type != IR_TYPE_U32 || !rng->is_mutable) {
    ir_graph_mark_unsupported(ir, left, "typed graph MIR std.rand helper expects a mutable RandSource local", left->name ? left->name : "non-mutable RandSource");
    return NULL;
  }
  return rng;
}

static bool ir_graph_lower_std_rand_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  if (arg_count == 1 && ir_text_eq(callee_name, "std.rand.seed")) {
    return ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_U32, out);
  }
  if (arg_count == 1 && ir_text_eq(callee_name, "std.rand.nextU32")) {
    const IrLocal *rng = ir_graph_find_mutable_rand_source_local(graph, ir, fun, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 0), callee_name);
    if (!rng) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_RAND_NEXT_U32, IR_TYPE_U32, ir_graph_line(expr), ir_graph_column(expr));
    value->local_index = rng->index;
    *out = value;
    return true;
  }
  if (arg_count == 1 && ir_text_eq(callee_name, "std.rand.nextBool")) {
    const IrLocal *rng = ir_graph_find_mutable_rand_source_local(graph, ir, fun, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 0), callee_name);
    if (!rng) return false;
    IrValue *next = ir_new_value(ir, IR_VALUE_RAND_NEXT_U32, IR_TYPE_U32, ir_graph_line(expr), ir_graph_column(expr));
    next->local_index = rng->index;
    *out = ir_new_compare_value(ir, IR_CMP_GE, next, ir_new_integer_literal_value(ir, IR_TYPE_U32, 2147483648ull, ir_graph_line(expr), ir_graph_column(expr)), ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (arg_count == 2 && ir_text_eq(callee_name, "std.rand.nextBelow")) {
    const IrLocal *rng = ir_graph_find_mutable_rand_source_local(graph, ir, fun, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 0), callee_name);
    if (!rng) return false;
    IrValue *bound = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_U32, &bound)) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_RAND_NEXT_BELOW, IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
    value->element_type = IR_TYPE_U32;
    value->local_index = rng->index;
    value->left = bound;
    *out = value;
    return true;
  }
  if (arg_count == 3 && ir_text_eq(callee_name, "std.rand.rangeU32")) {
    const IrLocal *rng = ir_graph_find_mutable_rand_source_local(graph, ir, fun, ir_graph_ordered_node(graph, expr ? expr->id : NULL, "arg", 0), callee_name);
    if (!rng) return false;
    IrValue *low = NULL;
    IrValue *high = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_U32, &low) ||
        !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 2, IR_TYPE_U32, &high)) {
      ir_free_value(low);
      ir_free_value(high);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_RAND_RANGE_U32, IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
    value->element_type = IR_TYPE_U32;
    value->local_index = rng->index;
    value->left = low;
    value->right = high;
    *out = value;
    return true;
  }
  if (arg_count == 0 && ir_graph_is_std_rand_entropy_call(callee_name)) {
    *out = ir_new_value(ir, IR_VALUE_RAND_ENTROPY_U32, IR_TYPE_U32, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  *handled = false;
  return true;
}

static const IrLocal *ir_graph_find_borrowed_local(const ZProgramGraph *graph, const IrFunction *fun, const ZProgramGraphNode *arg) {
  const ZProgramGraphNode *target = arg;
  if (arg && arg->kind == Z_PROGRAM_GRAPH_NODE_BORROW) target = ir_graph_ordered_node(graph, arg->id, "left", 0);
  return target && target->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER ? ir_function_find_local(fun, target->name) : NULL;
}

static const IrLocal *ir_graph_find_fs_file_local(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *arg, const ZProgramGraphNode *diagnostic, const char *message, const char *actual) {
  const IrLocal *file = ir_graph_find_borrowed_local(graph, fun, arg);
  if (!file || file->type != IR_TYPE_I32) {
    ir_graph_mark_unsupported(ir, diagnostic ? diagnostic : arg, message, actual);
    return NULL;
  }
  return file;
}

static bool ir_graph_lower_std_fs_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  if (arg_count == 0 && ir_text_eq(callee_name, "std.fs.host")) {
    *out = ir_new_value(ir, IR_VALUE_FS_HOST, IR_TYPE_I32, ir_graph_line(expr), ir_graph_column(expr));
    return true;
  }
  if (arg_count == 2 &&
      (ir_text_eq(callee_name, "std.fs.open") || ir_text_eq(callee_name, "std.fs.create") ||
       ir_text_eq(callee_name, "std.fs.openOrRaise") || ir_text_eq(callee_name, "std.fs.createOrRaise"))) {
    IrValue *path = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &path)) return false;
    bool raises = ir_text_eq(callee_name, "std.fs.openOrRaise") || ir_text_eq(callee_name, "std.fs.createOrRaise");
    IrValueKind kind = (ir_text_eq(callee_name, "std.fs.create") || ir_text_eq(callee_name, "std.fs.createOrRaise")) ? IR_VALUE_FS_CREATE : IR_VALUE_FS_OPEN;
    IrValue *value = ir_new_value(ir, kind, raises ? IR_TYPE_I64 : IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
    value->left = path;
    value->element_type = IR_TYPE_I32;
    if (raises) value->error_code = kind == IR_VALUE_FS_OPEN ? IR_ERROR_NOT_FOUND : IR_ERROR_IO;
    *out = value;
    return true;
  }
  if (arg_count == 2 && (ir_text_eq(callee_name, "std.fs.read") || ir_text_eq(callee_name, "std.fs.readOrRaise"))) {
    bool raises = ir_text_eq(callee_name, "std.fs.readOrRaise");
    const ZProgramGraphNode *first = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    IrValue *buf = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &buf)) return false;
    if (first && first->kind == Z_PROGRAM_GRAPH_NODE_BORROW) {
      const IrLocal *file = ir_graph_find_fs_file_local(graph, ir, fun, first, first, "typed graph MIR std.fs.read expects a File local", "non-File read");
      if (!file) {
        ir_free_value(buf);
        return false;
      }
      IrValue *value = ir_new_value(ir, IR_VALUE_FS_READ_FILE, raises ? IR_TYPE_I64 : IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
      value->local_index = file->index;
      value->left = buf;
      value->element_type = IR_TYPE_USIZE;
      if (raises) value->error_code = IR_ERROR_IO;
      *out = value;
      return true;
    }
    IrValue *path = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, first, &path)) {
      ir_free_value(buf);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_FS_READ_PATH, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    value->left = path;
    value->right = buf;
    *out = value;
    return true;
  }
  if (arg_count == 2 && (ir_text_eq(callee_name, "std.fs.write") || ir_text_eq(callee_name, "std.fs.writeBytes") || ir_text_eq(callee_name, "std.fs.appendBytes"))) {
    IrValue *path = NULL;
    IrValue *bytes = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &path) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &bytes)) {
      ir_free_value(path);
      ir_free_value(bytes);
      return false;
    }
    bool maybe = ir_text_eq(callee_name, "std.fs.writeBytes");
    bool append = ir_text_eq(callee_name, "std.fs.appendBytes");
    IrValueKind kind = append ? IR_VALUE_FS_APPEND_BYTES_PATH : (maybe ? IR_VALUE_FS_WRITE_BYTES_PATH : IR_VALUE_FS_WRITE_PATH);
    IrValue *value = ir_new_value(ir, kind, maybe || append ? IR_TYPE_MAYBE_SCALAR : IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    value->left = path;
    value->right = bytes;
    value->element_type = IR_TYPE_USIZE;
    *out = value;
    return true;
  }
  if (arg_count == 2 && ir_text_eq(callee_name, "std.fs.readBytes")) {
    IrValue *path = NULL;
    IrValue *buf = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &path) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &buf)) {
      ir_free_value(path);
      ir_free_value(buf);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_FS_READ_BYTES_PATH, IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
    value->left = path;
    value->right = buf;
    value->element_type = IR_TYPE_USIZE;
    *out = value;
    return true;
  }
  if (arg_count == 3 && ir_text_eq(callee_name, "std.fs.readBytesAt")) {
    IrValue *path = NULL;
    IrValue *offset = NULL;
    IrValue *buf = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &path) ||
        !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_USIZE, &offset) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 2), &buf)) {
      ir_free_value(path);
      ir_free_value(offset);
      ir_free_value(buf);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_FS_READ_BYTES_AT_PATH, IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
    value->left = path;
    value->index = offset;
    value->right = buf;
    value->element_type = IR_TYPE_USIZE;
    *out = value;
    return true;
  }
  if (arg_count == 4 && (ir_text_eq(callee_name, "std.fs.readAll") || ir_text_eq(callee_name, "std.fs.readAllOrRaise"))) {
    const ZProgramGraphNode *alloc_arg = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    const IrLocal *alloc = alloc_arg && alloc_arg->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER ? ir_function_find_local(fun, alloc_arg->name) : NULL;
    if (!alloc || alloc->type != IR_TYPE_ALLOC || !alloc->is_mutable) {
      ir_graph_mark_unsupported(ir, alloc_arg, "typed graph MIR std.fs.readAll expects a mutable FixedBufAlloc local", "non-mutable allocator");
      return false;
    }
    IrValue *path = NULL;
    IrValue *limit = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 2), &path) ||
        !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 3, IR_TYPE_USIZE, &limit)) {
      ir_free_value(path);
      ir_free_value(limit);
      return false;
    }
    bool raises = ir_text_eq(callee_name, "std.fs.readAllOrRaise");
    IrValue *value = ir_new_value(ir, IR_VALUE_FS_READ_ALL, raises ? IR_TYPE_I64 : IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->local_index = alloc->index;
    value->left = path;
    value->right = limit;
    value->element_type = IR_TYPE_BYTE_VIEW;
    if (raises) value->error_code = IR_ERROR_IO;
    *out = value;
    return true;
  }
  if (arg_count == 2 && (ir_text_eq(callee_name, "std.fs.writeAll") || ir_text_eq(callee_name, "std.fs.writeAllOrRaise"))) {
    const ZProgramGraphNode *file_arg = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    if (!file_arg || file_arg->kind != Z_PROGRAM_GRAPH_NODE_BORROW) {
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.fs.writeAll expects a File local", "non-File writeAll");
      return false;
    }
    const IrLocal *file = ir_graph_find_fs_file_local(graph, ir, fun, file_arg, file_arg, "typed graph MIR std.fs.writeAll expects a File local", "non-File writeAll");
    if (!file) return false;
    IrValue *bytes = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &bytes)) return false;
    bool raises = ir_text_eq(callee_name, "std.fs.writeAllOrRaise");
    IrValue *value = ir_new_value(ir, IR_VALUE_FS_WRITE_ALL_FILE, raises ? IR_TYPE_I64 : IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->local_index = file->index;
    value->left = bytes;
    value->element_type = IR_TYPE_VOID;
    if (raises) value->error_code = IR_ERROR_IO;
    *out = value;
    return true;
  }
  if (arg_count == 1 && ir_text_eq(callee_name, "std.fs.close")) {
    const ZProgramGraphNode *file_arg = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    const IrLocal *file = ir_graph_find_fs_file_local(graph, ir, fun, file_arg, expr, "typed graph MIR std.fs.close expects a File local", "non-File close");
    if (!file) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_FS_CLOSE_FILE, IR_TYPE_VOID, ir_graph_line(expr), ir_graph_column(expr));
    value->local_index = file->index;
    *out = value;
    return true;
  }
  if (arg_count == 1 &&
      (ir_text_eq(callee_name, "std.fs.exists") || ir_text_eq(callee_name, "std.fs.remove") ||
       ir_text_eq(callee_name, "std.fs.makeDir") || ir_text_eq(callee_name, "std.fs.removeDir") ||
       ir_text_eq(callee_name, "std.fs.isDir"))) {
    IrValue *path = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &path)) return false;
    IrValueKind kind = IR_VALUE_FS_EXISTS;
    if (ir_text_eq(callee_name, "std.fs.remove")) kind = IR_VALUE_FS_REMOVE;
    else if (ir_text_eq(callee_name, "std.fs.makeDir")) kind = IR_VALUE_FS_MAKE_DIR;
    else if (ir_text_eq(callee_name, "std.fs.removeDir")) kind = IR_VALUE_FS_REMOVE_DIR;
    else if (ir_text_eq(callee_name, "std.fs.isDir")) kind = IR_VALUE_FS_IS_DIR;
    IrValue *value = ir_new_value(ir, kind, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->left = path;
    *out = value;
    return true;
  }
  if (arg_count == 2 && ir_text_eq(callee_name, "std.fs.rename")) {
    IrValue *from = NULL;
    IrValue *to = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &from) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &to)) {
      ir_free_value(from);
      ir_free_value(to);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_FS_RENAME, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->left = from;
    value->right = to;
    *out = value;
    return true;
  }
  if (arg_count == 1 && ir_text_eq(callee_name, "std.fs.dirEntryCount")) {
    IrValue *path = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &path)) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_FS_DIR_ENTRY_COUNT, IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
    value->left = path;
    value->element_type = IR_TYPE_USIZE;
    *out = value;
    return true;
  }
  if (arg_count == 3 && ir_text_eq(callee_name, "std.fs.dirEntryName")) {
    IrValue *buf = NULL;
    IrValue *path = NULL;
    IrValue *index = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &buf) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &path) ||
        !ir_graph_lower_expr(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 2), &index)) {
      ir_free_value(buf);
      ir_free_value(path);
      ir_free_value(index);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_FS_DIR_ENTRY_NAME, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = buf;
    value->right = path;
    value->index = index;
    *out = value;
    return true;
  }
  if (arg_count == 2 && ir_text_eq(callee_name, "std.fs.tempName")) {
    IrValue *buf = NULL;
    IrValue *prefix = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &buf) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &prefix)) {
      ir_free_value(buf);
      ir_free_value(prefix);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_FS_TEMP_NAME, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = buf;
    value->right = prefix;
    *out = value;
    return true;
  }
  if (arg_count == 3 && ir_text_eq(callee_name, "std.fs.atomicWrite")) {
    IrValue *path = NULL;
    IrValue *tmp_path = NULL;
    IrValue *bytes = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &path) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &tmp_path) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 2), &bytes)) {
      ir_free_value(path);
      ir_free_value(tmp_path);
      ir_free_value(bytes);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_FS_ATOMIC_WRITE, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->left = path;
    value->right = tmp_path;
    value->index = bytes;
    *out = value;
    return true;
  }
  if (arg_count == 1 && (ir_text_eq(callee_name, "std.fs.fileLen") || ir_text_eq(callee_name, "std.fs.fileLenOrRaise"))) {
    const ZProgramGraphNode *file_arg = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    const IrLocal *file = ir_graph_find_fs_file_local(graph, ir, fun, file_arg, expr, "typed graph MIR std.fs.fileLen expects a File local", "non-File fileLen");
    if (!file) return false;
    bool raises = ir_text_eq(callee_name, "std.fs.fileLenOrRaise");
    IrValue *value = ir_new_value(ir, IR_VALUE_FS_FILE_LEN, raises ? IR_TYPE_I64 : IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
    value->local_index = file->index;
    value->element_type = IR_TYPE_USIZE;
    if (raises) value->error_code = IR_ERROR_IO;
    *out = value;
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
  if (ir_text_eq(callee_name, "std.search.upperBoundI32")) { *op = IR_SEARCH_OP_UPPER_BOUND_I32; *element_type = IR_TYPE_I32; return true; }
  if (ir_text_eq(callee_name, "std.search.upperBoundU32")) { *op = IR_SEARCH_OP_UPPER_BOUND_U32; *element_type = IR_TYPE_U32; return true; }
  if (ir_text_eq(callee_name, "std.search.upperBoundUsize")) { *op = IR_SEARCH_OP_UPPER_BOUND_USIZE; *element_type = IR_TYPE_USIZE; return true; }
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

static bool ir_graph_lower_std_env_get_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  IrValue *key = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &key)) return false;
  IrValue *value = ir_new_value(ir, IR_VALUE_ENV_GET, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
  value->left = key;
  *out = value;
  return true;
}

static bool ir_graph_lower_std_cli_arg_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  if (ir_text_eq(callee_name, "std.cli.command") && arg_count == 0) {
    IrValue *index = ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 1, ir_graph_line(expr), ir_graph_column(expr));
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_GET, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = index;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.cli.commandOr") && arg_count == 1) {
    IrValue *fallback = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &fallback)) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_GET_OR, IR_TYPE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 1, ir_graph_line(expr), ir_graph_column(expr));
    value->right = fallback;
    value->element_type = IR_TYPE_U8;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.cli.commandEquals") && arg_count == 1) {
    IrValue *expected = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &expected)) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_EQ, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->left = ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 1, ir_graph_line(expr), ir_graph_column(expr));
    value->right = expected;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.cli.argOr") && arg_count == 2) {
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
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.cli.argOr index must be an integer value", "non-integer index");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_GET_OR, IR_TYPE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = index;
    value->right = fallback;
    value->element_type = IR_TYPE_U8;
    *out = value;
    return true;
  }
  *handled = false;
  return true;
}

static bool ir_graph_lower_std_proc_capture_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  IrValue *command = NULL;
  IrValue *buffer = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &command) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &buffer)) {
    ir_free_value(command);
    ir_free_value(buffer);
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CAPTURE, IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
  value->left = command;
  value->right = buffer;
  value->element_type = IR_TYPE_USIZE;
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_proc_push_argv4(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue *value) {
  IrValue *items[4] = {0};
  for (size_t i = 0; i < 4; i++) {
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", i), &items[i])) {
      for (size_t j = 0; j < 4; j++) ir_free_value(items[j]);
      return false;
    }
  }
  for (size_t i = 0; i < 4; i++) ir_value_push_arg(ir, value, items[i]);
  return true;
}

static bool ir_graph_lower_std_proc_push_argv2(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue *value) {
  IrValue *items[2] = {0};
  for (size_t i = 0; i < 2; i++) {
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", i), &items[i])) {
      for (size_t j = 0; j < 2; j++) ir_free_value(items[j]);
      return false;
    }
  }
  for (size_t i = 0; i < 2; i++) ir_value_push_arg(ir, value, items[i]);
  return true;
}

static bool ir_graph_lower_std_proc_capture_args_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CAPTURE, IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
  value->element_type = IR_TYPE_USIZE;
  if (!ir_graph_lower_std_proc_push_argv2(graph, ir, fun, expr, value) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 2), &value->right)) {
    ir_free_value(value);
    return false;
  }
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_proc_spawn_inherit_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  IrValue *command = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &command)) return false;
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_SPAWN_INHERIT, IR_TYPE_I32, ir_graph_line(expr), ir_graph_column(expr));
  value->left = command;
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_proc_spawn_inherit_args_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_SPAWN_INHERIT, IR_TYPE_I32, ir_graph_line(expr), ir_graph_column(expr));
  if (!ir_graph_lower_std_proc_push_argv4(graph, ir, fun, expr, value)) {
    ir_free_value(value);
    return false;
  }
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_proc_capture_files_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  IrValue *command = NULL;
  IrValue *stdout_path = NULL;
  IrValue *stderr_path = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &command) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &stdout_path) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 2), &stderr_path)) {
    ir_free_value(command);
    ir_free_value(stdout_path);
    ir_free_value(stderr_path);
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CAPTURE_FILES, IR_TYPE_I32, ir_graph_line(expr), ir_graph_column(expr));
  value->left = command;
  value->right = stdout_path;
  value->index = stderr_path;
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_proc_capture_files_args_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CAPTURE_FILES, IR_TYPE_I32, ir_graph_line(expr), ir_graph_column(expr));
  if (!ir_graph_lower_std_proc_push_argv2(graph, ir, fun, expr, value) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 2), &value->right) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 3), &value->index)) {
    ir_free_value(value);
    return false;
  }
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_proc_child_spawn_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, bool pty, IrValue **out) {
  IrValue *command = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &command)) return false;
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CHILD_SPAWN, IR_TYPE_I32, ir_graph_line(expr), ir_graph_column(expr));
  value->left = command;
  value->int_value = pty ? 1 : 0;
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_proc_child_spawn_in_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, bool pty, IrValue **out) {
  IrValue *command = NULL;
  IrValue *cwd = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &command) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &cwd)) {
    ir_free_value(command);
    ir_free_value(cwd);
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CHILD_SPAWN, IR_TYPE_I32, ir_graph_line(expr), ir_graph_column(expr));
  value->left = command;
  value->right = cwd;
  value->int_value = pty ? 1 : 0;
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_proc_child_spawn_in_env_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, bool pty, IrValue **out) {
  IrValue *command = NULL;
  IrValue *cwd = NULL;
  IrValue *env = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &command) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &cwd) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 2), &env)) {
    ir_free_value(command);
    ir_free_value(cwd);
    ir_free_value(env);
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CHILD_SPAWN, IR_TYPE_I32, ir_graph_line(expr), ir_graph_column(expr));
  value->left = command;
  value->right = cwd;
  value->index = env;
  value->int_value = pty ? 1 : 0;
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_proc_child_spawn_args_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, bool pty, IrValue **out) {
  IrValue *program_arg = NULL;
  IrValue *args = NULL;
  IrValue *cwd = NULL;
  IrValue *env = NULL;
  if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &program_arg) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &args) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 2), &cwd) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 3), &env)) {
    ir_free_value(program_arg);
    ir_free_value(args);
    ir_free_value(cwd);
    ir_free_value(env);
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CHILD_SPAWN, IR_TYPE_I32, ir_graph_line(expr), ir_graph_column(expr));
  ir_value_push_arg(ir, value, program_arg);
  ir_value_push_arg(ir, value, args);
  ir_value_push_arg(ir, value, cwd);
  ir_value_push_arg(ir, value, env);
  value->int_value = pty ? 1 : 0;
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_proc_child_op_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrProcChildOp op, IrTypeKind result_type, IrValue **out) {
  IrValue *child = NULL;
  if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_I32, &child)) return false;
  if (!child || child->type != IR_TYPE_I32) {
    ir_free_value(child);
    ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 0), "typed graph MIR std.proc child helper expects ProcChild", "non-ProcChild argument");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CHILD_OP, result_type, ir_graph_line(expr), ir_graph_column(expr));
  value->left = child;
  value->int_value = (unsigned long long)op;
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_proc_child_io_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrProcChildIoOp op, IrValue **out) {
  IrValue *child = NULL;
  IrValue *bytes = NULL;
  if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_I32, &child) ||
      !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &bytes)) {
    ir_free_value(child);
    ir_free_value(bytes);
    return false;
  }
  if (!child || child->type != IR_TYPE_I32) {
    ir_free_value(child);
    ir_free_value(bytes);
    ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 0), "typed graph MIR std.proc stream helper expects ProcChild", "non-ProcChild argument");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CHILD_IO, IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
  value->left = child;
  value->right = bytes;
  value->int_value = (unsigned long long)op;
  value->element_type = IR_TYPE_USIZE;
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_pty_resize_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrValue **out) {
  IrValue *child = NULL;
  IrValue *columns = NULL;
  IrValue *rows = NULL;
  if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_I32, &child) ||
      !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_USIZE, &columns) ||
      !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 2, IR_TYPE_USIZE, &rows)) {
    ir_free_value(child);
    ir_free_value(columns);
    ir_free_value(rows);
    return false;
  }
  if (!child || child->type != IR_TYPE_I32) {
    ir_free_value(child);
    ir_free_value(columns);
    ir_free_value(rows);
    ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 0), "typed graph MIR std.pty.resize expects ProcChild", "non-ProcChild argument");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_PTY_RESIZE, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
  value->left = child;
  value->right = columns;
  value->index = rows;
  ir_graph_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_graph_lower_std_byte_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, const char *callee_name, size_t arg_count, bool *handled, IrValue **out) {
  *handled = true;
  if (ir_text_eq(callee_name, "std.args.len") && arg_count == 0) {
    *out = ir_new_value(ir, IR_VALUE_ARGS_LEN, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr)); return true;
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
  if (!ir_graph_lower_std_cli_arg_call(graph, ir, fun, expr, callee_name, arg_count, handled, out)) return false;
  if (*handled) return true;
  *handled = true;
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
  if (ir_text_eq(callee_name, "std.env.get") && arg_count == 1) return ir_graph_lower_std_env_get_call(graph, ir, fun, expr, out);
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
  if ((ir_text_eq(callee_name, "std.json.errorName") || ir_text_eq(callee_name, "std.json.errorExpected")) && arg_count == 1) {
    IrValue *code = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 0, IR_TYPE_U32, &code)) return false;
    if (code && code->kind == IR_VALUE_INT) {
      const char *label = ir_graph_json_error_label(code->int_value, ir_text_eq(callee_name, "std.json.errorExpected"));
      ir_free_value(code);
      return ir_make_string_literal_value(ir, label, ir_graph_line(expr), ir_graph_column(expr), out);
    }
    return ir_graph_make_json_error_label_value(ir, code, ir_text_eq(callee_name, "std.json.errorExpected"), ir_graph_line(expr), ir_graph_column(expr), out);
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
  if (ir_text_eq(callee_name, "std.proc.spawnInherit") && arg_count == 1) return ir_graph_lower_std_proc_spawn_inherit_call(graph, ir, fun, expr, out);
  if (ir_text_eq(callee_name, "std.proc.spawnInheritArgs") && arg_count == 4) return ir_graph_lower_std_proc_spawn_inherit_args_call(graph, ir, fun, expr, out);
  if (ir_text_eq(callee_name, "std.proc.capture") && arg_count == 2) return ir_graph_lower_std_proc_capture_call(graph, ir, fun, expr, out);
  if (ir_text_eq(callee_name, "std.proc.captureArgs") && arg_count == 3) return ir_graph_lower_std_proc_capture_args_call(graph, ir, fun, expr, out);
  if (ir_text_eq(callee_name, "std.proc.captureFiles") && arg_count == 3) return ir_graph_lower_std_proc_capture_files_call(graph, ir, fun, expr, out);
  if (ir_text_eq(callee_name, "std.proc.captureFilesArgs") && arg_count == 4) return ir_graph_lower_std_proc_capture_files_args_call(graph, ir, fun, expr, out);
  if (ir_text_eq(callee_name, "std.proc.spawnChild") && arg_count == 1) return ir_graph_lower_std_proc_child_spawn_call(graph, ir, fun, expr, false, out);
  if (ir_text_eq(callee_name, "std.proc.spawnChildIn") && arg_count == 2) return ir_graph_lower_std_proc_child_spawn_in_call(graph, ir, fun, expr, false, out);
  if (ir_text_eq(callee_name, "std.proc.spawnChildInEnv") && arg_count == 3) return ir_graph_lower_std_proc_child_spawn_in_env_call(graph, ir, fun, expr, false, out);
  if (ir_text_eq(callee_name, "std.proc.spawnChildArgs") && arg_count == 4) return ir_graph_lower_std_proc_child_spawn_args_call(graph, ir, fun, expr, false, out);
  if (ir_text_eq(callee_name, "std.pty.spawn") && arg_count == 1) return ir_graph_lower_std_proc_child_spawn_call(graph, ir, fun, expr, true, out);
  if (ir_text_eq(callee_name, "std.pty.spawnIn") && arg_count == 2) return ir_graph_lower_std_proc_child_spawn_in_call(graph, ir, fun, expr, true, out);
  if (ir_text_eq(callee_name, "std.pty.spawnInEnv") && arg_count == 3) return ir_graph_lower_std_proc_child_spawn_in_env_call(graph, ir, fun, expr, true, out);
  if (ir_text_eq(callee_name, "std.pty.spawnArgs") && arg_count == 4) return ir_graph_lower_std_proc_child_spawn_args_call(graph, ir, fun, expr, true, out);
  if (ir_text_eq(callee_name, "std.proc.childValid") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_VALID, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.proc.running") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_RUNNING, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.proc.wait") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_WAIT, IR_TYPE_I32, out);
  if (ir_text_eq(callee_name, "std.proc.kill") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_KILL, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.proc.interrupt") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_INTERRUPT, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.proc.close") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_CLOSE, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.proc.closeStdin") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_CLOSE_STDIN, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.proc.pid") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_PID, IR_TYPE_I32, out);
  if (ir_text_eq(callee_name, "std.proc.pidRunning") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_PID_RUNNING, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.proc.killPid") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_KILL_PID, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.proc.interruptPid") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_INTERRUPT_PID, IR_TYPE_BOOL, out); else if (ir_text_eq(callee_name, "std.proc.killGroupPid") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_KILL_GROUP_PID, IR_TYPE_BOOL, out); else if (ir_text_eq(callee_name, "std.proc.interruptGroupPid") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_INTERRUPT_GROUP_PID, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.proc.readStdout") && arg_count == 2) return ir_graph_lower_std_proc_child_io_call(graph, ir, fun, expr, IR_PROC_CHILD_IO_READ_STDOUT, out);
  if (ir_text_eq(callee_name, "std.proc.readStderr") && arg_count == 2) return ir_graph_lower_std_proc_child_io_call(graph, ir, fun, expr, IR_PROC_CHILD_IO_READ_STDERR, out);
  if (ir_text_eq(callee_name, "std.proc.writeStdin") && arg_count == 2) return ir_graph_lower_std_proc_child_io_call(graph, ir, fun, expr, IR_PROC_CHILD_IO_WRITE_STDIN, out);
  if ((ir_text_eq(callee_name, "std.pty.valid") || ir_text_eq(callee_name, "std.pty.childValid")) && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_VALID, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.pty.running") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_RUNNING, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.pty.wait") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_WAIT, IR_TYPE_I32, out);
  if (ir_text_eq(callee_name, "std.pty.kill") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_KILL, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.pty.interrupt") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_INTERRUPT, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.pty.close") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_CLOSE, IR_TYPE_BOOL, out);
  if (ir_text_eq(callee_name, "std.pty.pid") && arg_count == 1) return ir_graph_lower_std_proc_child_op_call(graph, ir, fun, expr, IR_PROC_CHILD_OP_PID, IR_TYPE_I32, out);
  if (ir_text_eq(callee_name, "std.pty.read") && arg_count == 2) return ir_graph_lower_std_proc_child_io_call(graph, ir, fun, expr, IR_PROC_CHILD_IO_READ_STDOUT, out);
  if (ir_text_eq(callee_name, "std.pty.write") && arg_count == 2) return ir_graph_lower_std_proc_child_io_call(graph, ir, fun, expr, IR_PROC_CHILD_IO_WRITE_STDIN, out);
  if (ir_text_eq(callee_name, "std.pty.resize") && arg_count == 3) return ir_graph_lower_std_pty_resize_call(graph, ir, fun, expr, out);
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
  if ((ir_text_eq(callee_name, "std.io.bufferedReader") || ir_text_eq(callee_name, "std.io.bufferedWriter")) && arg_count == 1) {
    return ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), out);
  }
  if ((ir_text_eq(callee_name, "std.io.readerCapacity") || ir_text_eq(callee_name, "std.io.writerCapacity")) && arg_count == 1) {
    IrValue *buf = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &buf)) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    value->left = buf;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.io.copy") && arg_count == 2) {
    IrValue *dst = NULL;
    IrValue *src = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &dst) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &src)) {
      ir_free_value(dst);
      ir_free_value(src);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_COPY, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    value->left = src;
    value->right = dst;
    value->element_type = IR_TYPE_U8;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.mem.fixedBufAlloc") && arg_count == 1) {
    IrValue *bytes = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &bytes)) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_FIXED_BUF_ALLOC, IR_TYPE_ALLOC, ir_graph_line(expr), ir_graph_column(expr));
    value->left = bytes;
    if (ir->direct_allocator_helper_count < 1) ir->direct_allocator_helper_count = 1;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.mem.allocBytes") && arg_count == 2) {
    const ZProgramGraphNode *alloc_arg = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    const IrLocal *alloc = alloc_arg && alloc_arg->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER ? ir_function_find_local(fun, alloc_arg->name) : NULL;
    if (!alloc || alloc->type != IR_TYPE_ALLOC || !alloc->is_mutable) {
      ir_graph_mark_unsupported(ir, alloc_arg, "typed graph MIR std.mem.allocBytes expects a mutable FixedBufAlloc local", "non-mutable allocator");
      return false;
    }
    IrValue *len = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_USIZE, &len)) return false;
    if (!len || !ir_type_is_value(len->type)) {
      ir_free_value(len);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), "typed graph MIR std.mem.allocBytes length must be an integer value", "non-integer length");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_ALLOC_BYTES, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->local_index = alloc->index;
    value->left = len;
    if (ir->direct_allocator_helper_count < 2) ir->direct_allocator_helper_count = 2;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.mem.vec") && arg_count == 1) {
    IrValue *bytes = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &bytes)) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_VEC_INIT, IR_TYPE_VEC, ir_graph_line(expr), ir_graph_column(expr));
    value->left = bytes;
    if (ir->direct_buffer_helper_count < 1) ir->direct_buffer_helper_count = 1;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.mem.bufBytes") && arg_count == 1) {
    const IrLocal *buf = ir_graph_find_borrowed_local(graph, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0));
    if (buf && buf->type == IR_TYPE_BYTE_VIEW) {
      IrValue *value = ir_new_value(ir, IR_VALUE_LOCAL, IR_TYPE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
      value->local_index = buf->index;
      value->element_type = buf->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : buf->element_type;
      *out = value;
      return true;
    }
  }
  if (ir_text_eq(callee_name, "std.mem.bufLen") && arg_count == 1) {
    const IrLocal *buf = ir_graph_find_borrowed_local(graph, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0));
    if (buf && buf->type == IR_TYPE_BYTE_VIEW) {
      IrValue *view = ir_new_value(ir, IR_VALUE_LOCAL, IR_TYPE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
      view->local_index = buf->index;
      view->element_type = buf->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : buf->element_type;
      IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
      value->left = view;
      *out = value;
      return true;
    }
  }
  if (ir_text_eq(callee_name, "std.mem.vecPush") && arg_count == 2) {
    const ZProgramGraphNode *vec_arg = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    const IrLocal *vec = vec_arg && vec_arg->kind == Z_PROGRAM_GRAPH_NODE_BORROW && vec_arg->is_mutable ? ir_graph_find_borrowed_local(graph, fun, vec_arg) : NULL;
    if (!vec || vec->type != IR_TYPE_VEC || !vec->is_mutable) {
      ir_graph_mark_unsupported(ir, vec_arg, "typed graph MIR std.mem.vecPush expects a mutable Vec local", "non-mutable Vec");
      return false;
    }
    IrValue *item = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_U8, &item)) return false;
    if (!item || item->type != IR_TYPE_U8) {
      ir_free_value(item);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), "typed graph MIR std.mem.vecPush currently supports only u8 values", "non-u8 value");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_VEC_PUSH, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->local_index = vec->index;
    value->left = item;
    if (ir->direct_buffer_helper_count < 2) ir->direct_buffer_helper_count = 2;
    *out = value;
    return true;
  }
  if ((ir_text_eq(callee_name, "std.mem.vecClear") || ir_text_eq(callee_name, "std.mem.vecPop")) && arg_count == 1) {
    const ZProgramGraphNode *vec_arg = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    const IrLocal *vec = vec_arg && vec_arg->kind == Z_PROGRAM_GRAPH_NODE_BORROW && vec_arg->is_mutable ? ir_graph_find_borrowed_local(graph, fun, vec_arg) : NULL;
    if (!vec || vec->type != IR_TYPE_VEC || !vec->is_mutable) {
      ir_graph_mark_unsupported(ir, vec_arg, "typed graph MIR std.mem Vec mutation expects a mutable Vec local", "non-mutable Vec");
      return false;
    }
    bool pop = ir_text_eq(callee_name, "std.mem.vecPop");
    IrValue *value = ir_new_value(ir, pop ? IR_VALUE_VEC_POP : IR_VALUE_VEC_CLEAR, pop ? IR_TYPE_BOOL : IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    value->local_index = vec->index;
    if (ir->direct_buffer_helper_count < 4) ir->direct_buffer_helper_count = 4;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.mem.vecTruncate") && arg_count == 2) {
    const ZProgramGraphNode *vec_arg = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    const IrLocal *vec = vec_arg && vec_arg->kind == Z_PROGRAM_GRAPH_NODE_BORROW && vec_arg->is_mutable ? ir_graph_find_borrowed_local(graph, fun, vec_arg) : NULL;
    if (!vec || vec->type != IR_TYPE_VEC || !vec->is_mutable) {
      ir_graph_mark_unsupported(ir, vec_arg, "typed graph MIR std.mem.vecTruncate expects a mutable Vec local", "non-mutable Vec");
      return false;
    }
    IrValue *len = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_USIZE, &len)) return false;
    if (!len || len->type != IR_TYPE_USIZE) {
      ir_free_value(len);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), "typed graph MIR std.mem.vecTruncate expects a usize length", "non-usize length");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_VEC_TRUNCATE, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    value->local_index = vec->index;
    value->left = len;
    if (ir->direct_buffer_helper_count < 4) ir->direct_buffer_helper_count = 4;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.mem.vecRemoveSwap") && arg_count == 2) {
    const ZProgramGraphNode *vec_arg = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    const IrLocal *vec = vec_arg && vec_arg->kind == Z_PROGRAM_GRAPH_NODE_BORROW && vec_arg->is_mutable ? ir_graph_find_borrowed_local(graph, fun, vec_arg) : NULL;
    if (!vec || vec->type != IR_TYPE_VEC || !vec->is_mutable) {
      ir_graph_mark_unsupported(ir, vec_arg, "typed graph MIR std.mem.vecRemoveSwap expects a mutable Vec local", "non-mutable Vec");
      return false;
    }
    IrValue *index = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_USIZE, &index)) return false;
    if (!index || index->type != IR_TYPE_USIZE) {
      ir_free_value(index);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), "typed graph MIR std.mem.vecRemoveSwap expects a usize index", "non-usize index");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_VEC_REMOVE_SWAP, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->element_type = IR_TYPE_U8;
    value->local_index = vec->index;
    value->left = index;
    if (ir->direct_buffer_helper_count < 5) ir->direct_buffer_helper_count = 5;
    *out = value;
    return true;
  }
  if ((ir_text_eq(callee_name, "std.mem.vecInsertUnique") || ir_text_eq(callee_name, "std.mem.vecRemoveValue")) && arg_count == 2) {
    const ZProgramGraphNode *vec_arg = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    const IrLocal *vec = vec_arg && vec_arg->kind == Z_PROGRAM_GRAPH_NODE_BORROW && vec_arg->is_mutable ? ir_graph_find_borrowed_local(graph, fun, vec_arg) : NULL;
    if (!vec || vec->type != IR_TYPE_VEC || !vec->is_mutable) {
      ir_graph_mark_unsupported(ir, vec_arg, "typed graph MIR std.mem Vec value mutation expects a mutable Vec local", "non-mutable Vec");
      return false;
    }
    IrValue *item = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_U8, &item)) return false;
    if (!item || item->type != IR_TYPE_U8) {
      ir_free_value(item);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), "typed graph MIR std.mem Vec value mutation currently supports only u8 values", "non-u8 value");
      return false;
    }
    bool insert_unique = ir_text_eq(callee_name, "std.mem.vecInsertUnique");
    IrValue *value = ir_new_value(ir, insert_unique ? IR_VALUE_VEC_INSERT_UNIQUE : IR_VALUE_VEC_REMOVE_VALUE, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->element_type = IR_TYPE_U8;
    value->local_index = vec->index;
    value->left = item;
    if (ir->direct_buffer_helper_count < 6) ir->direct_buffer_helper_count = 6;
    *out = value;
    return true;
  }
  if ((ir_text_eq(callee_name, "std.mem.vecIndex") || ir_text_eq(callee_name, "std.mem.vecContains")) && arg_count == 2) {
    const IrLocal *vec = ir_graph_find_borrowed_local(graph, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0));
    if (vec && vec->type == IR_TYPE_VEC) {
      IrValue *item = NULL;
      if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_U8, &item)) return false;
      if (!item || item->type != IR_TYPE_U8) {
        ir_free_value(item);
        ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), "typed graph MIR std.mem Vec lookup currently supports only u8 values", "non-u8 value");
        return false;
      }
      bool contains = ir_text_eq(callee_name, "std.mem.vecContains");
      IrValue *value = ir_new_value(ir, contains ? IR_VALUE_VEC_CONTAINS : IR_VALUE_VEC_INDEX, contains ? IR_TYPE_BOOL : IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
      value->element_type = IR_TYPE_U8;
      value->local_index = vec->index;
      value->left = item;
      if (ir->direct_buffer_helper_count < 6) ir->direct_buffer_helper_count = 6;
      *out = value;
      return true;
    }
  }
  if (ir_text_eq(callee_name, "std.mem.vecGet") && arg_count == 2) {
    const IrLocal *vec = ir_graph_find_borrowed_local(graph, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0));
    if (vec && vec->type == IR_TYPE_VEC) {
      IrValue *index = NULL;
      if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_USIZE, &index)) return false;
      if (!index || index->type != IR_TYPE_USIZE) {
        ir_free_value(index);
        ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), "typed graph MIR std.mem.vecGet expects a usize index", "non-usize index");
        return false;
      }
      IrValue *value = ir_new_value(ir, IR_VALUE_VEC_GET, IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
      value->element_type = IR_TYPE_U8;
      value->local_index = vec->index;
      value->left = index;
      if (ir->direct_buffer_helper_count < 5) ir->direct_buffer_helper_count = 5;
      *out = value;
      return true;
    }
  }
  if (ir_text_eq(callee_name, "std.mem.vecSet") && arg_count == 3) {
    const ZProgramGraphNode *vec_arg = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    const IrLocal *vec = vec_arg && vec_arg->kind == Z_PROGRAM_GRAPH_NODE_BORROW && vec_arg->is_mutable ? ir_graph_find_borrowed_local(graph, fun, vec_arg) : NULL;
    if (!vec || vec->type != IR_TYPE_VEC || !vec->is_mutable) {
      ir_graph_mark_unsupported(ir, vec_arg, "typed graph MIR std.mem.vecSet expects a mutable Vec local", "non-mutable Vec");
      return false;
    }
    IrValue *index = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_USIZE, &index)) return false;
    if (!index || index->type != IR_TYPE_USIZE) {
      ir_free_value(index);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), "typed graph MIR std.mem.vecSet expects a usize index", "non-usize index");
      return false;
    }
    IrValue *item = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 2, IR_TYPE_U8, &item)) {
      ir_free_value(index);
      return false;
    }
    if (!item || item->type != IR_TYPE_U8) {
      ir_free_value(index);
      ir_free_value(item);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 2), "typed graph MIR std.mem.vecSet currently supports only u8 values", "non-u8 value");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_VEC_SET, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->element_type = IR_TYPE_U8;
    value->local_index = vec->index;
    value->left = index;
    value->right = item;
    if (ir->direct_buffer_helper_count < 5) ir->direct_buffer_helper_count = 5;
    *out = value;
    return true;
  }
  IrVecHelper vec_helper = ir_std_mem_vec_helper(callee_name);
  if (vec_helper != IR_VEC_HELPER_NONE && arg_count == 1) {
    const IrLocal *vec = ir_graph_find_borrowed_local(graph, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0));
    if (vec && vec->type == IR_TYPE_VEC) {
      IrValue *value = ir_new_vec_helper_value(ir, vec_helper, vec->index, ir_graph_line(expr), ir_graph_column(expr));
      size_t required_helpers = vec_helper == IR_VEC_HELPER_BYTES ? 4 : 3;
      if (ir->direct_buffer_helper_count < required_helpers) ir->direct_buffer_helper_count = required_helpers;
      *out = value;
      return true;
    }
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
  if (ir_text_eq(callee_name, "std.mem.copy") && arg_count == 2) {
    IrValue *dst = NULL;
    IrValue *src = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &dst) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &src)) {
      ir_free_value(dst);
      ir_free_value(src);
      return false;
    }
    if (ir_graph_view_element_type(dst) != IR_TYPE_U8 || ir_graph_view_element_type(src) != IR_TYPE_U8) {
      ir_free_value(dst);
      ir_free_value(src);
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.mem.copy requires byte views", "non-byte copy view");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_COPY, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    value->left = src;
    value->right = dst;
    value->element_type = IR_TYPE_U8;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.mem.fill") && arg_count == 2) {
    IrValue *dst = NULL;
    IrValue *fill = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &dst) ||
        !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_U8, &fill)) {
      ir_free_value(dst);
      ir_free_value(fill);
      return false;
    }
    if (ir_graph_view_element_type(dst) != IR_TYPE_U8 || !fill || fill->type != IR_TYPE_U8) {
      ir_free_value(dst);
      ir_free_value(fill);
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.mem.fill requires a byte destination and u8 value", "non-byte fill");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_FILL, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    value->left = fill;
    value->right = dst;
    value->element_type = IR_TYPE_U8;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.mem.copyItems") && arg_count == 2) {
    IrValue *dst = NULL;
    IrValue *src = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &dst) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &src)) {
      ir_free_value(dst);
      ir_free_value(src);
      return false;
    }
    IrTypeKind dst_element = ir_graph_view_element_type(dst);
    IrTypeKind src_element = ir_graph_view_element_type(src);
    if (dst_element != src_element || !(dst_element == IR_TYPE_BOOL || ir_type_is_value(dst_element))) {
      ir_free_value(dst);
      ir_free_value(src);
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.mem.copyItems requires matching primitive span element types", "unsupported item copy");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_ITEM_COPY, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    value->left = src;
    value->right = dst;
    value->element_type = dst_element;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.mem.fillItems") && arg_count == 2) {
    IrValue *dst = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &dst)) return false;
    IrTypeKind element = ir_graph_view_element_type(dst);
    if (!(element == IR_TYPE_BOOL || ir_type_is_value(element))) {
      ir_free_value(dst);
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.mem.fillItems requires primitive span elements", "unsupported item fill");
      return false;
    }
    IrValue *fill = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, element, &fill)) {
      ir_free_value(dst);
      return false;
    }
    if (!fill || fill->type != element) {
      ir_free_value(dst);
      ir_free_value(fill);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), "typed graph MIR std.mem.fillItems value must match span element type", "mismatched fill item");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_ITEM_FILL, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
    value->left = fill;
    value->right = dst;
    value->element_type = element;
    *out = value;
    return true;
  }
  if (ir_text_eq(callee_name, "std.mem.contains") && arg_count == 2) {
    IrValue *items = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &items)) return false;
    IrTypeKind element = ir_graph_view_element_type(items);
    if (!(element == IR_TYPE_BOOL || ir_type_is_value(element))) {
      ir_free_value(items);
      ir_graph_mark_unsupported(ir, expr, "typed graph MIR std.mem.contains requires primitive span elements", "unsupported item contains");
      return false;
    }
    IrValue *needle = NULL;
    if (!ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, element, &needle)) {
      ir_free_value(items);
      return false;
    }
    if (!needle || needle->type != element) {
      ir_free_value(items);
      ir_free_value(needle);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), "typed graph MIR std.mem.contains value must match span element type", "mismatched needle");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_ITEM_CONTAINS, IR_TYPE_BOOL, ir_graph_line(expr), ir_graph_column(expr));
    value->left = items;
    value->right = needle;
    value->element_type = element;
    *out = value;
    return true;
  }
  if ((ir_text_eq(callee_name, "std.mem.prefix") || ir_text_eq(callee_name, "std.mem.dropPrefix")) && arg_count == 2) {
    IrValue *items = NULL;
    IrValue *count = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &items) ||
        !ir_graph_lower_ordered_arg(graph, ir, fun, expr, 1, IR_TYPE_USIZE, &count)) {
      ir_free_value(items);
      ir_free_value(count);
      return false;
    }
    if (!count || !ir_type_is_value(count->type)) {
      ir_free_value(items);
      ir_free_value(count);
      ir_graph_mark_unsupported(ir, ir_graph_ordered_node(graph, expr->id, "arg", 1), "typed graph MIR std.mem prefix count must be an integer value", "non-integer count");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_SLICE, IR_TYPE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = items;
    value->element_type = ir_graph_view_element_type(items);
    if (ir_text_eq(callee_name, "std.mem.prefix")) {
      value->right = count;
    } else {
      value->index = count;
    }
    *out = value;
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
  if ((ir_text_eq(callee_name, "std.mem.eql") || ir_text_eq(callee_name, "std.mem.eqlBytes") || ir_text_eq(callee_name, "std.crypto.constantTimeEql") || ir_text_eq(callee_name, "std.str.contains")) && arg_count == 2) {
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

static bool ir_graph_lower_call(const ZProgramGraph *graph, IrProgram *ir, const IrFunction *fun, const ZProgramGraphNode *expr, IrTypeKind preferred_return_type, IrValue **out) {
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_CALL && (ir_compare_op(expr->name, &(IrCompareOp){0}) || ir_binary_op(expr->name, &(IrBinaryOp){0}))) {
    return ir_graph_lower_binary_call(graph, ir, fun, expr, IR_TYPE_UNSUPPORTED, out);
  }

  char *qualified = ir_graph_expr_qualified_name(graph, expr);
  const char *callee_name = qualified && qualified[0] ? qualified : expr->name;
  size_t arg_count = ir_graph_edge_count(graph, expr->id, "arg");
  const char *term_sequence = ir_graph_term_sequence(callee_name);
  if (term_sequence && arg_count == 0) {
    bool ok = ir_make_string_literal_value(ir, term_sequence, ir_graph_line(expr), ir_graph_column(expr), out);
    free(qualified);
    return ok;
  }
  unsigned long long term_key_code = 0;
  if (arg_count == 0 && ir_graph_term_key_constant(callee_name, &term_key_code)) {
    *out = ir_new_integer_literal_value(ir, IR_TYPE_U32, term_key_code, ir_graph_line(expr), ir_graph_column(expr));
    free(qualified);
    return true;
  }
  bool handled = false;
  if (!ir_graph_lower_std_term_runtime_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) { free(qualified); return false; }
  if (handled) { free(qualified); return true; }
  if (!ir_graph_lower_http_std_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) { free(qualified); return false; }
  if (handled) { free(qualified); return true; }
  if (!ir_graph_lower_std_str_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) { free(qualified); return false; }
  if (handled) { free(qualified); return true; }
  if (!ir_graph_lower_std_ascii_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) { free(qualified); return false; }
  if (handled) { free(qualified); return true; }
  if (!ir_graph_lower_std_text_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) { free(qualified); return false; }
  if (handled) { free(qualified); return true; }
  if (!ir_graph_lower_std_parse_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) { free(qualified); return false; }
  if (handled) { free(qualified); return true; }
  if (!ir_graph_lower_std_time_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) { free(qualified); return false; }
  if (handled) { free(qualified); return true; }
  if (!ir_graph_lower_std_rand_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) { free(qualified); return false; }
  if (handled) { free(qualified); return true; }
  if (!ir_graph_lower_std_testing_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) { free(qualified); return false; }
  if (handled) { free(qualified); return true; }
  if (!ir_graph_lower_std_byte_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) { free(qualified); return false; }
  if (handled) { free(qualified); return true; }
  if (!ir_graph_lower_std_fs_call(graph, ir, fun, expr, callee_name, arg_count, &handled, out)) { free(qualified); return false; }
  if (handled) { free(qualified); return true; }
  if ((ir_text_eq(callee_name, "std.json.parse") || ir_text_eq(callee_name, "std.json.parseBytes")) && arg_count == 2) {
    const ZProgramGraphNode *alloc_arg = ir_graph_ordered_node(graph, expr->id, "arg", 0);
    const IrLocal *alloc = alloc_arg && alloc_arg->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER ? ir_function_find_local(fun, alloc_arg->name) : NULL;
    if (!alloc || alloc->type != IR_TYPE_ALLOC || !alloc->is_mutable) { ir_graph_mark_unsupported(ir, alloc_arg, "typed graph MIR std.json.parse expects a mutable FixedBufAlloc local", "non-mutable allocator"); free(qualified); return false; }
    IrValue *view = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &view)) { free(qualified); return false; }
    IrValue *value = ir_new_value(ir, IR_VALUE_JSON_PARSE_BYTES, IR_TYPE_I64, ir_graph_line(expr), ir_graph_column(expr));
    value->local_index = alloc->index; value->left = view;
    if (ir->direct_allocator_helper_count < 2) ir->direct_allocator_helper_count = 2;
    ir_graph_require_runtime_helper(ir); *out = value; free(qualified); return true;
  }
  if ((ir_text_eq(callee_name, "std.json.validate") || ir_text_eq(callee_name, "std.json.validateBytes") || ir_text_eq(callee_name, "std.json.streamTokens") || ir_text_eq(callee_name, "std.json.streamTokensBytes") || ir_text_eq(callee_name, "std.json.validateError") || ir_text_eq(callee_name, "std.json.errorOffset") || ir_text_eq(callee_name, "std.json.errorLine") || ir_text_eq(callee_name, "std.json.errorColumn")) && arg_count == 1) {
    IrValue *view = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &view)) { free(qualified); return false; }
    bool validate = ir_text_eq(callee_name, "std.json.validate") || ir_text_eq(callee_name, "std.json.validateBytes");
    bool stream = ir_text_eq(callee_name, "std.json.streamTokens") || ir_text_eq(callee_name, "std.json.streamTokensBytes");
    IrValueKind kind = validate ? IR_VALUE_JSON_VALIDATE_BYTES : (stream ? IR_VALUE_JSON_STREAM_TOKENS_BYTES : IR_VALUE_JSON_DIAGNOSTIC_BYTES);
    IrTypeKind type = validate ? IR_TYPE_BOOL : (stream ? IR_TYPE_USIZE : (ir_text_eq(callee_name, "std.json.validateError") ? IR_TYPE_U32 : IR_TYPE_USIZE));
    IrValue *value = ir_new_value(ir, kind, type, ir_graph_line(expr), ir_graph_column(expr));
    if (kind == IR_VALUE_JSON_DIAGNOSTIC_BYTES) {
      if (ir_text_eq(callee_name, "std.json.validateError")) value->int_value = 0;
      else if (ir_text_eq(callee_name, "std.json.errorOffset")) value->int_value = 1;
      else if (ir_text_eq(callee_name, "std.json.errorLine")) value->int_value = 2;
      else value->int_value = 3;
    }
    value->left = view; ir_graph_require_runtime_helper(ir); *out = value; free(qualified); return true;
  }
  if (ir_text_eq(callee_name, "std.json.field") && arg_count == 2) {
    IrValue *input = NULL;
    IrValue *key = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &input) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &key)) {
      ir_free_value(input);
      ir_free_value(key);
      free(qualified);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_JSON_FIELD, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = input;
    value->right = key;
    ir_graph_require_runtime_helper(ir);
    *out = value;
    free(qualified);
    return true;
  }
  if ((ir_text_eq(callee_name, "std.json.u32") || ir_text_eq(callee_name, "std.json.bool")) && arg_count == 2) {
    IrValue *input = NULL;
    IrValue *key = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &input) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &key)) {
      ir_free_value(input);
      ir_free_value(key);
      free(qualified);
      return false;
    }
    IrTypeKind element = ir_text_eq(callee_name, "std.json.bool") ? IR_TYPE_BOOL : IR_TYPE_U32;
    IrValue *value = ir_new_value(ir, IR_VALUE_JSON_LOOKUP_SCALAR, IR_TYPE_MAYBE_SCALAR, ir_graph_line(expr), ir_graph_column(expr));
    value->element_type = element;
    value->int_value = element == IR_TYPE_BOOL ? 1 : 0;
    value->left = input;
    value->right = key;
    ir_graph_require_runtime_helper(ir);
    *out = value;
    free(qualified);
    return true;
  }
  if (ir_text_eq(callee_name, "std.json.stringDecode") && arg_count == 2) {
    IrValue *buffer = NULL;
    IrValue *raw = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &buffer) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &raw)) {
      ir_free_value(buffer);
      ir_free_value(raw);
      free(qualified);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_JSON_STRING_DECODE, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = buffer;
    value->right = raw;
    ir_graph_require_runtime_helper(ir);
    *out = value;
    free(qualified);
    return true;
  }
  if ((ir_text_eq(callee_name, "std.json.writeString") || ir_text_eq(callee_name, "std.json.writeStringBytes") || ir_text_eq(callee_name, "__zero_std_json_write_string") || ir_text_eq(callee_name, "__zero_std_json_write_string_bytes")) && arg_count == 2) {
    IrValue *buffer = NULL;
    IrValue *text = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &buffer) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &text)) {
      ir_free_value(buffer);
      ir_free_value(text);
      free(qualified);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_JSON_WRITE_STRING, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = buffer;
    value->right = text;
    ir_graph_require_runtime_helper(ir);
    *out = value;
    free(qualified);
    return true;
  }
  IrJsonWriteOp json_write_op = IR_JSON_WRITE_FIELD_RAW;
  bool json_write = true;
  if (ir_text_eq(callee_name, "std.json.writeFieldRaw") && arg_count == 3) json_write_op = IR_JSON_WRITE_FIELD_RAW;
  else if (ir_text_eq(callee_name, "std.json.writeFieldString") && arg_count == 3) json_write_op = IR_JSON_WRITE_FIELD_STRING;
  else if (ir_text_eq(callee_name, "std.json.writeFieldU32") && arg_count == 3) json_write_op = IR_JSON_WRITE_FIELD_U32;
  else if (ir_text_eq(callee_name, "std.json.writeFieldBool") && arg_count == 3) json_write_op = IR_JSON_WRITE_FIELD_BOOL;
  else if (ir_text_eq(callee_name, "std.json.writeObject1String") && arg_count == 3) json_write_op = IR_JSON_WRITE_OBJECT1_STRING;
  else if (ir_text_eq(callee_name, "std.json.writeObject1U32") && arg_count == 3) json_write_op = IR_JSON_WRITE_OBJECT1_U32;
  else if (ir_text_eq(callee_name, "std.json.writeObject1Bool") && arg_count == 3) json_write_op = IR_JSON_WRITE_OBJECT1_BOOL;
  else if (ir_text_eq(callee_name, "std.json.writeObject2Fields") && arg_count == 3) json_write_op = IR_JSON_WRITE_OBJECT2_FIELDS;
  else if (ir_text_eq(callee_name, "std.json.writeObject2StringField") && arg_count == 4) json_write_op = IR_JSON_WRITE_OBJECT2_STRING_FIELD;
  else if (ir_text_eq(callee_name, "std.json.writeObject2U32Field") && arg_count == 4) json_write_op = IR_JSON_WRITE_OBJECT2_U32_FIELD;
  else if (ir_text_eq(callee_name, "std.json.writeObject2BoolField") && arg_count == 4) json_write_op = IR_JSON_WRITE_OBJECT2_BOOL_FIELD;
  else if (ir_text_eq(callee_name, "std.json.writeArray2Strings") && arg_count == 3) json_write_op = IR_JSON_WRITE_ARRAY2_STRINGS;
  else if (ir_text_eq(callee_name, "std.json.writeArray2U32") && arg_count == 3) json_write_op = IR_JSON_WRITE_ARRAY2_U32;
  else if (ir_text_eq(callee_name, "std.json.writeArray2Bools") && arg_count == 3) json_write_op = IR_JSON_WRITE_ARRAY2_BOOLS;
  else json_write = false;
  if (json_write) {
    IrValue *value = ir_new_value(ir, IR_VALUE_JSON_WRITE_RUNTIME, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->int_value = (unsigned long long)json_write_op;
    for (size_t i = 0; i < arg_count; i++) {
      IrTypeKind type = IR_TYPE_BYTE_VIEW;
      if ((json_write_op == IR_JSON_WRITE_FIELD_U32 || json_write_op == IR_JSON_WRITE_OBJECT1_U32) && i == 2) type = IR_TYPE_U32;
      if ((json_write_op == IR_JSON_WRITE_FIELD_BOOL || json_write_op == IR_JSON_WRITE_OBJECT1_BOOL) && i == 2) type = IR_TYPE_BOOL;
      if ((json_write_op == IR_JSON_WRITE_OBJECT2_U32_FIELD || json_write_op == IR_JSON_WRITE_OBJECT2_BOOL_FIELD) && i == 2) type = json_write_op == IR_JSON_WRITE_OBJECT2_U32_FIELD ? IR_TYPE_U32 : IR_TYPE_BOOL;
      if ((json_write_op == IR_JSON_WRITE_ARRAY2_U32 || json_write_op == IR_JSON_WRITE_ARRAY2_BOOLS) && i > 0) type = json_write_op == IR_JSON_WRITE_ARRAY2_U32 ? IR_TYPE_U32 : IR_TYPE_BOOL;
      IrValue *arg = NULL;
      bool ok = type == IR_TYPE_BYTE_VIEW ?
        ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", i), &arg) :
        ir_graph_lower_ordered_arg(graph, ir, fun, expr, i, type, &arg);
      if (!ok) {
        ir_free_value(value);
        free(qualified);
        return false;
      }
      ir_value_push_arg(ir, value, arg);
    }
    ir_graph_require_runtime_helper(ir);
    *out = value;
    free(qualified);
    return true;
  }
  if (ir_text_eq(callee_name, "std.json.string") && arg_count == 3) {
    IrValue *buffer = NULL;
    IrValue *input = NULL;
    IrValue *key = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &buffer) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &input) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 2), &key)) {
      ir_free_value(buffer);
      ir_free_value(input);
      ir_free_value(key);
      free(qualified);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_JSON_STRING_FIELD, IR_TYPE_MAYBE_BYTE_VIEW, ir_graph_line(expr), ir_graph_column(expr));
    value->left = buffer;
    value->right = input;
    value->index = key;
    ir_graph_require_runtime_helper(ir);
    *out = value;
    free(qualified);
    return true;
  }
  if ((ir_text_eq(callee_name, "std.codec.crc32") || ir_text_eq(callee_name, "std.codec.crc32Bytes") || ir_text_eq(callee_name, "std.crypto.hash32")) && arg_count == 1) {
    IrValue *view = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &view)) { free(qualified); return false; }
    IrValue *value = ir_new_value(ir, IR_VALUE_CRC32_BYTES, IR_TYPE_U32, ir_graph_line(expr), ir_graph_column(expr)); value->left = view; *out = value; free(qualified); return true;
  }
  if (ir_text_eq(callee_name, "std.crypto.hmac32") && arg_count == 2) {
    IrValue *left = NULL;
    IrValue *right = NULL;
    if (!ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 0), &left) ||
        !ir_graph_lower_byte_view(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "arg", 1), &right)) {
      ir_free_value(left);
      ir_free_value(right);
      free(qualified);
      return false;
    }
    IrValue *left_len = ir_new_value(ir, IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_U32, ir_graph_line(expr), ir_graph_column(expr));
    left_len->left = left;
    IrValue *right_len = ir_new_value(ir, IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_U32, ir_graph_line(expr), ir_graph_column(expr));
    right_len->left = right;
    IrValue *value = ir_new_value(ir, IR_VALUE_BINARY, IR_TYPE_U32, ir_graph_line(expr), ir_graph_column(expr));
    value->binary_op = IR_BIN_ADD;
    value->left = left_len;
    value->right = right_len;
    *out = value;
    free(qualified);
    return true;
  }
  const char *stdlib_graph_target = z_std_source_target_for_public_call(callee_name);
  if (stdlib_graph_target) {
    bool lowered = ir_graph_lower_named_call(graph, ir, fun, expr, stdlib_graph_target, preferred_return_type, out);
    free(qualified);
    return lowered;
  }
  bool c_import_handled = false;
  if (!ir_graph_lower_c_import_call(graph, ir, fun, expr, callee_name, &c_import_handled, out)) {
    free(qualified);
    return false;
  }
  if (c_import_handled) { free(qualified); return true; }
  bool lowered = ir_graph_lower_named_call(graph, ir, fun, expr, expr->name, preferred_return_type, out);
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
      return ir_graph_lower_literal(graph, expr, ir, out);
    case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
      return ir_graph_lower_identifier(graph, expr, ir, fun, out);
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
      return ir_graph_lower_field_access(graph, ir, fun, expr, out);
    case Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS:
      return ir_graph_lower_index_access(graph, ir, fun, expr, out);
    case Z_PROGRAM_GRAPH_NODE_SLICE:
      return ir_graph_lower_byte_slice(graph, ir, fun, expr, out);
    case Z_PROGRAM_GRAPH_NODE_CAST: {
      IrValue *inner = NULL;
      if (!ir_graph_lower_expr(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "left", 0), &inner)) return false;
      IrTypeKind cast_type = ir_graph_type_kind(graph, ir_graph_node_type(graph, expr));
      if (!ir_type_is_value(cast_type) && cast_type != IR_TYPE_BOOL) {
        ir_free_value(inner);
        ir_graph_mark_unsupported(ir, expr, "typed graph MIR cast target type is unsupported", ir_graph_node_type(graph, expr));
        return false;
      }
      *out = ir_new_cast_value(ir, inner, cast_type, ir_graph_line(expr), ir_graph_column(expr));
      return true;
    }
    case Z_PROGRAM_GRAPH_NODE_CHECK: {
      const ZProgramGraphNode *checked_node = ir_graph_ordered_node(graph, expr->id, "left", 0);
      if (!checked_node) checked_node = ir_graph_ordered_node(graph, expr->id, "expr", 0);
      IrValue *checked = NULL;
      if (!ir_graph_lower_expr(graph, ir, fun, checked_node, &checked)) return false;
      if (!checked || checked->type != IR_TYPE_I64) {
        ir_free_value(checked);
        ir_graph_mark_unsupported(ir, expr, "typed graph MIR check expression requires a fallible value", "non-fallible check");
        return false;
      }
      IrValue *value = ir_new_value(ir, IR_VALUE_CHECK, checked->element_type, ir_graph_line(expr), ir_graph_column(expr));
      value->left = checked;
      *out = value;
      return true;
    }
    case Z_PROGRAM_GRAPH_NODE_RESCUE: {
      IrValue *fallible = NULL;
      IrValue *fallback = NULL;
      if (!ir_graph_lower_expr(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "left", 0), &fallible) ||
          !ir_graph_lower_expr(graph, ir, fun, ir_graph_ordered_node(graph, expr->id, "right", 1), &fallback)) {
        ir_free_value(fallible);
        ir_free_value(fallback);
        return false;
      }
      if (!fallible || fallible->kind != IR_VALUE_CALL || fallible->type != IR_TYPE_I64 ||
          !fallback || fallback->type != fallible->element_type) {
        ir_free_value(fallible);
        ir_free_value(fallback);
        ir_graph_mark_unsupported(ir, expr, "typed graph MIR rescue supports fallible function calls with primitive fallbacks", "unsupported rescue");
        return false;
      }
      IrValue *value = ir_new_value(ir, IR_VALUE_RESCUE, fallible->element_type, ir_graph_line(expr), ir_graph_column(expr));
      value->left = fallible;
      value->right = fallback;
      *out = value;
      return true;
    }
    case Z_PROGRAM_GRAPH_NODE_CALL:
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL:
      return ir_graph_lower_call(graph, ir, fun, expr, IR_TYPE_UNSUPPORTED, out);
    case Z_PROGRAM_GRAPH_NODE_BORROW: {
      const ZProgramGraphNode *target = ir_graph_ordered_node(graph, expr->id, "left", 0);
      const IrLocal *local = target && target->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER ? ir_function_find_local(fun, target->name) : NULL;
      if (local && local->is_record_ref) {
        IrValue *value = ir_new_value(ir, IR_VALUE_LOCAL, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
        value->local_index = local->index;
        *out = value;
        return true;
      }
      if (!local || !local->is_record) {
        ir_graph_mark_unsupported(ir, expr, "typed graph MIR borrow expression supports only local shape variables", target && target->name ? target->name : "borrow target");
        return false;
      }
      if (expr->is_mutable && !local->is_mutable) {
        ir_graph_mark_unsupported(ir, expr, "typed graph MIR mutable shape borrow requires a mutable shape local", local->name ? local->name : "shape local");
        return false;
      }
      IrValue *value = ir_new_value(ir, IR_VALUE_RECORD_ADDR, IR_TYPE_USIZE, ir_graph_line(expr), ir_graph_column(expr));
      value->local_index = local->index;
      *out = value;
      return true;
    }
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
    if (local->is_record) {
      bool handled_fixed_resource = false;
      if (!ir_graph_lower_fixed_resource_initializer(graph, ir, fun, local, expr, out_items, out_len, out_cap, &handled_fixed_resource)) return false;
      if (handled_fixed_resource) {
        ir_active_local_push(ir, stmt->name);
        return ir->mir_valid;
      }
      if (!ir_graph_lower_record_initializer(graph, ir, fun, local, expr, out_items, out_len, out_cap, ir_graph_line(stmt), ir_graph_column(stmt))) return false;
      ir_active_local_push(ir, stmt->name);
      return true;
    }
    IrValue *value = NULL;
    if (!ir_graph_lower_expr_for_type(graph, ir, fun, expr, local->type, local->element_type, false, ir_graph_line(stmt), ir_graph_column(stmt), &value)) return false;
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
          !ir_graph_lower_expr_for_type(graph, ir, fun, expr, element_type, IR_TYPE_UNSUPPORTED, false, ir_graph_line(expr), ir_graph_column(expr), &value)) {
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
    if (target && target->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS) {
      const ZProgramGraphNode *base = ir_graph_ordered_node(graph, target->id, "left", 0);
      if (!base || base->kind != Z_PROGRAM_GRAPH_NODE_IDENTIFIER) {
        ir_graph_mark_unsupported(ir, target, "typed graph MIR record field assignment supports only local records", "non-local field assignment");
        return false;
      }
      const IrLocal *local = ir_function_find_local(fun, base->name);
      if (!local || !(local->is_record || local->is_record_ref)) {
        ir_graph_mark_unsupported(ir, target, "typed graph MIR field assignment target is not a record local", base->name);
        return false;
      }
      if (!local->is_mutable) {
        ir_graph_mark_unsupported(ir, target, "typed graph MIR record field assignment target must be mutable", base->name);
        return false;
      }
      unsigned field_offset = 0;
      IrTypeKind field_type = IR_TYPE_UNSUPPORTED;
      bool field_is_array = false;
      if (!ir_graph_shape_field_info(graph, local->shape_name, target->name, &field_offset, &field_type, &field_is_array, NULL, NULL) || field_is_array) {
        ir_graph_mark_unsupported(ir, target, "typed graph MIR record field assignment type is unsupported", target->name);
        return false;
      }
      IrValue *value = NULL;
      if (!ir_graph_lower_expr_for_type(graph, ir, fun, expr, field_type, IR_TYPE_UNSUPPORTED, false, ir_graph_line(expr), ir_graph_column(expr), &value)) return false;
      if (value->type != field_type) {
        ir_free_value(value);
        ir_graph_mark_unsupported(ir, target, "typed graph MIR record field assignment type does not match field", target->name);
        return false;
      }
      ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_FIELD_STORE, .local_index = local->index, .field_offset = field_offset, .value = value, .line = ir_graph_line(stmt), .column = ir_graph_column(stmt)});
      return true;
    }
    if (!target || target->kind != Z_PROGRAM_GRAPH_NODE_IDENTIFIER) {
      ir_graph_mark_unsupported(ir, target ? target : stmt, "typed graph MIR assignment currently supports only local variables", "non-local assignment");
      return false;
    }
    const IrLocal *local = ir_function_find_local(fun, target->name);
    if (!local) return false;
    IrValue *value = NULL;
    if (!ir_graph_lower_expr_for_type(graph, ir, fun, expr, local->type, local->element_type, fun->return_type == IR_TYPE_I32, ir_graph_line(stmt), ir_graph_column(stmt), &value)) return false;
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_LOCAL_SET, .local_index = local->index, .value = value, .line = ir_graph_line(stmt), .column = ir_graph_column(stmt)});
    return true;
  }
  if (stmt->kind == Z_PROGRAM_GRAPH_NODE_RETURN) {
    *saw_return = true;
    IrValue *value = NULL;
    const ZProgramGraphNode *expr = ir_graph_ordered_node(graph, stmt->id, "expr", 0);
    if (fun->raises && fun->value_return_type == IR_TYPE_VOID) {
      if (expr) {
        ir_graph_mark_unsupported(ir, stmt, "typed graph MIR fallible Void function cannot return a value", fun->name);
        return false;
      }
    } else if (fun->return_type == IR_TYPE_VOID) {
      if (expr) {
        ir_graph_mark_unsupported(ir, stmt, "typed graph MIR void function cannot return a value", fun->name);
        return false;
      }
    } else {
      IrTypeKind target_type = fun->raises ? fun->value_return_type : fun->return_type;
      IrTypeKind target_element_type = fun->raises ? IR_TYPE_UNSUPPORTED : fun->return_element_type;
      if (!ir_graph_lower_expr_for_type(graph, ir, fun, expr, target_type, target_element_type, true, ir_graph_line(stmt), ir_graph_column(stmt), &value)) {
        return false;
      }
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
    bool else_ok = then_ok && ir_graph_lower_block(graph, ir, fun, ir_graph_ordered_node(graph, stmt->id, "else", 1), &instr.else_instrs, &instr.else_len, &instr.else_cap, saw_return);
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
  if (stmt->kind == Z_PROGRAM_GRAPH_NODE_RAISE) {
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_RAISE, .field_offset = 1, .error_code = ir_graph_error_code_for_name(stmt->name), .line = ir_graph_line(stmt), .column = ir_graph_column(stmt)});
    return true;
  }
  if (stmt->kind == Z_PROGRAM_GRAPH_NODE_BREAK || stmt->kind == Z_PROGRAM_GRAPH_NODE_CONTINUE) {
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = stmt->kind == Z_PROGRAM_GRAPH_NODE_BREAK ? IR_INSTR_BREAK : IR_INSTR_CONTINUE, .line = ir_graph_line(stmt), .column = ir_graph_column(stmt)});
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
  char *return_type_text_owned = ir_graph_node_type_for_function_alloc(graph, fun, function);
  const char *return_type_text = return_type_text_owned;
  IrTypeKind return_type = hosted_world_main ? IR_TYPE_I32 : ir_graph_type_kind(graph, return_type_text);
  if (!hosted_world_main && function->fallible && !ir_type_is_direct_fallible_value(return_type)) {
    ir_graph_mark_unsupported(ir, function, "typed graph MIR fallible return type is unsupported", return_type_text);
    free(return_type_text_owned);
    return false;
  }
  if (!hosted_world_main && !function->fallible && return_type != IR_TYPE_VOID && !ir_type_is_direct_return_abi(return_type)) {
    ir_graph_mark_unsupported(ir, function, "typed graph MIR return type is unsupported", return_type_text);
    free(return_type_text_owned);
    return false;
  }
  free(return_type_text_owned);
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

static bool ir_graph_function_has_type_params(const ZProgramGraph *graph, const ZProgramGraphNode *function) {
  return function && ir_graph_edge_count(graph, function->id, "typeParam") > 0;
}

static void ir_graph_order_vec_push(IrProgram *ir, IrFunctionOrder **order, char ***stable_ids, size_t *len, size_t *cap, const ZProgramGraph *graph, const ZProgramGraphNode *source, const char *name, bool owns_name, char **generic_param_names, char **generic_arg_types, size_t generic_binding_len) {
  (void)ir;
  if (*len + 1 > *cap) {
    *cap = z_grow_capacity(*cap, *len + 1, 8);
    *order = z_checked_reallocarray(*order, *cap, sizeof(IrFunctionOrder));
    *stable_ids = z_checked_reallocarray(*stable_ids, *cap, sizeof(char *));
  }
  char *stable_id = ir_graph_stable_id_for_function_name(graph, source, name);
  (*stable_ids)[*len] = stable_id;
  (*order)[*len] = (IrFunctionOrder){
    .name = name,
    .stable_id = stable_id,
    .source_index = (size_t)(source - graph->nodes),
    .generic_param_names = generic_param_names,
    .generic_arg_types = generic_arg_types,
    .generic_binding_len = generic_binding_len,
    .owns_name = owns_name
  };
  (*len)++;
}

static bool ir_graph_type_is_function_type_param(const ZProgramGraph *graph, const ZProgramGraphNode *function, const char *type) {
  bool have_last = false; size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = ir_graph_next_edge_by_order(graph, function ? function->id : NULL, "typeParam", have_last, last_order);
    if (!edge) break;
    have_last = true; last_order = edge->order;
    const ZProgramGraphNode *param = ir_graph_find_node(graph, edge->to);
    if (param && ir_text_eq(param->name, type)) return true;
  }
  return false;
}

static bool ir_graph_bind_type_param(char **param_names, char **arg_types, size_t binding_len, const char *param_name, const char *arg_type) {
  if (!param_name || !arg_type || !arg_type[0]) return true;
  for (size_t i = 0; i < binding_len; i++) {
    if (!ir_text_eq(param_names[i], param_name)) continue;
    if (arg_types[i]) return ir_text_eq(arg_types[i], arg_type);
    arg_types[i] = z_strdup(arg_type); return true;
  }
  return true;
}

static bool ir_graph_pattern_matches_generic_wrapper(const char *pattern, const char *wrapper, const char *param) {
  size_t wrapper_len = wrapper ? strlen(wrapper) : 0, param_len = param ? strlen(param) : 0;
  return pattern && wrapper_len && param_len && strncmp(pattern, wrapper, wrapper_len) == 0 && pattern[wrapper_len] == '<' && strncmp(pattern + wrapper_len + 1, param, param_len) == 0 && pattern[wrapper_len + 1 + param_len] == '>' && pattern[wrapper_len + 2 + param_len] == '\0';
}

static bool ir_graph_generic_type_bounds(const char *type, const char **out_open, const char **out_close) {
  size_t len = type ? strlen(type) : 0;
  if (len < 3 || type[len - 1] != '>') return false;
  const char *open = strchr(type, '<');
  if (!open || open == type || open + 1 >= type + len - 1) return false;
  if (out_open) *out_open = open;
  if (out_close) *out_close = type + len - 1;
  return true;
}

static bool ir_graph_bind_type_param_from_pattern(const ZProgramGraph *graph, char **param_names, char **arg_types, size_t binding_len, const char *param_pattern, const char *arg_type) {
  if (!param_pattern || !param_pattern[0]) return true;
  bool pattern_ref_mutable = false;
  bool arg_ref_mutable = false;
  char *pattern_ref_inner = ir_graph_reference_inner_type_alloc(param_pattern, &pattern_ref_mutable);
  char *arg_ref_inner = ir_graph_reference_inner_type_alloc(arg_type, &arg_ref_mutable);
  if (pattern_ref_inner && arg_ref_inner) {
    bool ok = ir_graph_bind_type_param_from_pattern(graph, param_names, arg_types, binding_len, pattern_ref_inner, arg_ref_inner);
    free(pattern_ref_inner);
    free(arg_ref_inner);
    return ok;
  }
  free(pattern_ref_inner);
  free(arg_ref_inner);

  const char *pattern_open = NULL;
  const char *pattern_close = NULL;
  const char *arg_open = NULL;
  const char *arg_close = NULL;
  if (arg_type && ir_graph_generic_type_bounds(param_pattern, &pattern_open, &pattern_close) &&
      ir_graph_generic_type_bounds(arg_type, &arg_open, &arg_close) &&
      (size_t)(pattern_open - param_pattern) == (size_t)(arg_open - arg_type) &&
      strncmp(param_pattern, arg_type, (size_t)(pattern_open - param_pattern)) == 0) {
    TypeArgVec pattern_args = {0};
    TypeArgVec concrete_args = {0};
    bool ok = ir_graph_split_generic_args(pattern_open + 1, pattern_close, &pattern_args) &&
              ir_graph_split_generic_args(arg_open + 1, arg_close, &concrete_args) &&
              pattern_args.len == concrete_args.len;
    for (size_t i = 0; ok && i < pattern_args.len; i++) {
      ok = ir_graph_bind_type_param_from_pattern(graph, param_names, arg_types, binding_len, pattern_args.items[i].type, concrete_args.items[i].type);
    }
    size_t matched_arg_count = pattern_args.len;
    ir_graph_type_arg_vec_free(&pattern_args);
    ir_graph_type_arg_vec_free(&concrete_args);
    if (!ok) return false;
    if (matched_arg_count > 0) return true;
  }

  for (size_t i = 0; i < binding_len; i++) {
    const char *param = param_names[i];
    size_t param_len = param ? strlen(param) : 0;
    if (!param_len) continue;
    if (ir_text_eq(param_pattern, param)) return ir_graph_bind_type_param(param_names, arg_types, binding_len, param, arg_type);
    if (ir_graph_pattern_matches_generic_wrapper(param_pattern, "Span", param) || ir_graph_pattern_matches_generic_wrapper(param_pattern, "MutSpan", param)) {
      unsigned array_len = 0; IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
      if (ir_graph_parse_fixed_array_type(graph, arg_type, &array_len, &element_type)) {
        const char *element_name = ir_type_source_name(element_type);
        if (element_name) return ir_graph_bind_type_param(param_names, arg_types, binding_len, param, element_name);
      }
    }
    const char *match = param_pattern;
    while ((match = strstr(match, param)) != NULL) {
      bool left_boundary = match == param_pattern || !ir_graph_type_ident_char(match[-1]);
      bool right_boundary = !ir_graph_type_ident_char(match[param_len]);
      if (left_boundary && right_boundary) break;
      match += param_len;
    }
    if (!match) continue;
    if (!arg_type || !arg_type[0]) continue;
    size_t prefix_len = (size_t)(match - param_pattern);
    const char *suffix = match + param_len;
    size_t suffix_len = strlen(suffix);
    size_t arg_len = strlen(arg_type);
    if (arg_len < prefix_len + suffix_len || strncmp(arg_type, param_pattern, prefix_len) != 0 || strcmp(arg_type + arg_len - suffix_len, suffix) != 0) {
      continue;
    }
    char *extracted = z_strndup(arg_type + prefix_len, arg_len - prefix_len - suffix_len);
    bool ok = ir_graph_bind_type_param(param_names, arg_types, binding_len, param, extracted);
    free(extracted);
    if (!ok) return false;
  }
  return true;
}

static char *ir_graph_call_parent_expected_type_dup_depth(const ZProgramGraph *graph, const ZProgramGraphNode *call, IrTypeKind preferred_return_type, unsigned depth); static const char *ir_graph_enclosing_binding_type(const ZProgramGraph *graph, const ZProgramGraphNode *scope_node, const char *name);

static bool ir_graph_specialization_bindings_for_call_depth(const ZProgramGraph *graph, const IrFunction *fun, const ZProgramGraphNode *generic, const ZProgramGraphNode *call, IrTypeKind preferred_return_type, unsigned depth, char ***out_param_names, char ***out_arg_types, size_t *out_len) {
  if (depth > 12) return false;
  size_t type_arg_count = ir_graph_call_type_arg_count(graph, call);
  size_t type_param_count = ir_graph_function_type_param_count(graph, generic);
  if (type_param_count == 0 || type_arg_count > type_param_count) return false;
  char **param_names = z_checked_calloc(type_param_count, sizeof(char *)), **arg_types = z_checked_calloc(type_param_count, sizeof(char *));
  char *expected_type_owned = NULL, *bound_expected_type_owned = NULL;
  for (size_t i = 0; i < type_param_count; i++) {
    const ZProgramGraphNode *param = ir_graph_ordered_node(graph, generic->id, "typeParam", i);
    param_names[i] = z_strdup(param->name);
  }
  if (type_arg_count > 0) {
    if (type_arg_count != type_param_count) goto unresolved;
    for (size_t i = 0; i < type_param_count; i++) {
      const ZProgramGraphNode *arg = ir_graph_call_type_arg_node(graph, call, i);
      const char *arg_type = arg && arg->type ? arg->type : NULL;
      char *bound_arg_type = ir_graph_bound_type_text_alloc(fun, arg_type);
      if (!bound_arg_type || ir_graph_type_is_function_type_param(graph, generic, bound_arg_type)) {
        free(bound_arg_type);
        goto unresolved;
      }
      arg_types[i] = bound_arg_type;
    }
  } else {
    expected_type_owned = ir_graph_call_parent_expected_type_dup_depth(graph, call, preferred_return_type, depth + 1);
    bound_expected_type_owned = ir_graph_bound_type_text_alloc(fun, expected_type_owned);
    const char *expected_type = bound_expected_type_owned ? bound_expected_type_owned : expected_type_owned;
    const char *return_type = ir_graph_node_type(graph, generic);
    if (!ir_graph_bind_type_param_from_pattern(graph, param_names, arg_types, type_param_count, return_type, expected_type)) {
      goto unresolved;
    }
    size_t arg_count = ir_graph_edge_count(graph, call ? call->id : NULL, "arg");
    for (size_t i = 0; i < arg_count; i++) {
      const ZProgramGraphNode *param = ir_graph_ordered_node(graph, generic->id, "param", i);
      const char *param_type = ir_graph_node_type(graph, param);
      const ZProgramGraphNode *arg = ir_graph_ordered_node(graph, call->id, "arg", i);
      const char *arg_type = ir_graph_concrete_type_text_for_node(graph, arg);
      if (!arg_type && arg && arg->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER) arg_type = ir_graph_enclosing_binding_type(graph, call, arg->name);
      char *borrow_arg_type = NULL;
      if (!arg_type && arg && arg->kind == Z_PROGRAM_GRAPH_NODE_BORROW) {
        const ZProgramGraphNode *target = ir_graph_ordered_node(graph, arg->id, "left", 0);
        const char *target_type = target && target->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER ? ir_graph_enclosing_binding_type(graph, call, target->name) : NULL;
        if (target_type && target_type[0]) {
          ZBuf buf;
          zbuf_init(&buf);
          zbuf_append(&buf, arg->is_mutable ? "mutref<" : "ref<");
          zbuf_append(&buf, target_type);
          zbuf_append_char(&buf, '>');
          borrow_arg_type = buf.data;
          arg_type = borrow_arg_type;
        }
      }
      char *bound_arg_type = ir_graph_bound_type_text_alloc(fun, arg_type);
      const char *effective_arg_type = bound_arg_type;
      bool bound = ir_graph_bind_type_param_from_pattern(graph, param_names, arg_types, type_param_count, param_type, effective_arg_type);
      free(bound_arg_type);
      free(borrow_arg_type);
      if (!bound) goto unresolved;
    }
  }
  for (size_t i = 0; i < type_param_count; i++) if (!param_names[i] || !arg_types[i] || ir_graph_type_is_function_type_param(graph, generic, arg_types[i])) goto unresolved;
  free(bound_expected_type_owned);
  free(expected_type_owned);
  *out_param_names = param_names;
  *out_arg_types = arg_types;
  *out_len = type_param_count;
  return true;

unresolved:
  free(bound_expected_type_owned);
  free(expected_type_owned);
  for (size_t i = 0; i < type_param_count; i++) {
    free(param_names[i]);
    free(arg_types[i]);
  }
  free(param_names);
  free(arg_types);
  return false;
}

static bool ir_graph_specialization_bindings_for_call(const ZProgramGraph *graph, const IrFunction *fun, const ZProgramGraphNode *generic, const ZProgramGraphNode *call, IrTypeKind preferred_return_type, char ***out_param_names, char ***out_arg_types, size_t *out_len) {
  return ir_graph_specialization_bindings_for_call_depth(graph, fun, generic, call, preferred_return_type, 0, out_param_names, out_arg_types, out_len);
}

static char *ir_graph_call_parent_expected_type_dup_depth(const ZProgramGraph *graph, const ZProgramGraphNode *call, IrTypeKind preferred_return_type, unsigned depth) {
  if (depth > 12) return NULL;
  const char *preferred = ir_type_source_name(preferred_return_type);
  if (preferred) return z_strdup(preferred);
  for (size_t i = 0; graph && call && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !ir_text_eq(edge->to, call->id)) continue;
    const ZProgramGraphNode *parent = ir_graph_find_node(graph, edge->from);
    if (ir_text_eq(edge->kind, "expr") && parent && parent->kind == Z_PROGRAM_GRAPH_NODE_LET) {
      const char *type = ir_graph_node_type(graph, parent);
      if (type && type[0]) return z_strdup(type);
    }
    if (ir_text_eq(edge->kind, "arg") && parent && parent->kind == Z_PROGRAM_GRAPH_NODE_CALL) {
      const ZProgramGraphNode *callee = ir_graph_find_function_by_name(graph, parent->name);
      if (!callee) continue;
      const ZProgramGraphNode *param = ir_graph_ordered_node(graph, callee->id, "param", edge->order);
      const char *param_type = ir_graph_node_type(graph, param);
      if (!param_type || !param_type[0]) continue;
      if (!ir_graph_function_has_type_params(graph, callee)) return z_strdup(param_type);
      char **param_names = NULL, **arg_types = NULL; size_t binding_len = 0;
      if (!ir_graph_specialization_bindings_for_call_depth(graph, NULL, callee, parent, IR_TYPE_UNSUPPORTED, depth + 1, &param_names, &arg_types, &binding_len)) continue;
      const char *bound_type = ir_graph_bound_type_text_from_arrays(param_type, param_names, arg_types, binding_len);
      char *result = bound_type && bound_type[0] ? z_strdup(bound_type) : NULL;
      for (size_t j = 0; j < binding_len; j++) { free(param_names[j]); free(arg_types[j]); }
      free(param_names); free(arg_types);
      if (result) return result;
    }
  }
  return NULL;
}

static void ir_graph_parent_edge_run(const ZProgramGraph *graph, const IrGraphIndex **index, const char *to, size_t *start, size_t *run_len) {
  *start = 0;
  *run_len = 0;
  if (!graph || !to) return;
  *index = ir_graph_index(graph);
  ir_graph_index_run(graph, (*index)->parent_edges, (*index)->parent_len, ir_graph_index_edge_to_key, to, start, run_len);
}

static bool ir_graph_node_reaches_ancestor(const ZProgramGraph *graph, const char *node_id, const char *ancestor_id, unsigned depth) {
  if (!graph || !node_id || !ancestor_id || depth > 64) return false;
  if (ir_text_eq(node_id, ancestor_id)) return true;
  const IrGraphIndex *index = NULL;
  size_t start = 0;
  size_t run_len = 0;
  ir_graph_parent_edge_run(graph, &index, node_id, &start, &run_len);
  for (size_t i = 0; i < run_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[index->parent_edges[start + i]];
    if (ir_graph_node_reaches_ancestor(graph, edge->from, ancestor_id, depth + 1)) return true;
  }
  return false;
}

static const ZProgramGraphNode *ir_graph_enclosing_function_for_node(const ZProgramGraph *graph, const char *node_id, unsigned depth) {
  if (!graph || !node_id || depth > 64) return NULL;
  const ZProgramGraphNode *node = ir_graph_find_node(graph, node_id);
  if (node && node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION) return node;
  const IrGraphIndex *index = NULL;
  size_t start = 0;
  size_t run_len = 0;
  ir_graph_parent_edge_run(graph, &index, node_id, &start, &run_len);
  for (size_t i = 0; i < run_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[index->parent_edges[start + i]];
    const ZProgramGraphNode *parent = ir_graph_find_node(graph, edge->from);
    if (parent && parent->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION) return parent;
    const ZProgramGraphNode *enclosing = ir_graph_enclosing_function_for_node(graph, edge->from, depth + 1);
    if (enclosing) return enclosing;
  }
  return NULL;
}

static const char *ir_graph_enclosing_binding_type(const ZProgramGraph *graph, const ZProgramGraphNode *scope_node, const char *name) {
  const ZProgramGraphNode *function = ir_graph_enclosing_function_for_node(graph, scope_node ? scope_node->id : NULL, 0);
  bool have_last = false; size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = ir_graph_next_edge_by_order(graph, function ? function->id : NULL, "param", have_last, last_order);
    if (!edge) break;
    have_last = true; last_order = edge->order;
    const ZProgramGraphNode *param = ir_graph_find_node(graph, edge->to);
    if (param && ir_text_eq(param->name, name)) return ir_graph_node_type(graph, param);
  }
  const ZProgramGraphNode *body = ir_graph_ordered_node(graph, function ? function->id : NULL, "body", 0);
  for (size_t i = 0; graph && body && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_LET || !ir_text_eq(node->name, name) || !ir_graph_node_reaches_ancestor(graph, node->id, body->id, 0)) continue;
    const char *type = ir_graph_node_type(graph, node);
    if (type && type[0]) return type;
  }
  return NULL;
}

static bool ir_graph_add_nested_generic_specialization_orders(const ZProgramGraph *graph, IrProgram *ir, IrFunctionOrder **order, char ***stable_ids, size_t *len, size_t *cap, const ZProgramGraphNode *generic, char **param_names, char **arg_types, size_t binding_len);

static bool ir_graph_add_generic_specialization_order_context(const ZProgramGraph *graph, IrProgram *ir, IrFunctionOrder **order, char ***stable_ids, size_t *len, size_t *cap, const IrFunction *context_fun, const ZProgramGraphNode *call) {
  if (!call || (call->kind != Z_PROGRAM_GRAPH_NODE_CALL && call->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL)) return true;
  char *qualified = call->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL ? ir_graph_expr_qualified_name(graph, call) : NULL;
  const char *callee_name = call->name;
  if (qualified && qualified[0]) {
    if (ir_text_eq(qualified, "std.collections.fixedSet")) {
      free(qualified);
      return true;
    }
    if (ir_text_eq(qualified, "std.collections.fixedDeque")) {
      free(qualified);
      return true;
    }
    if (ir_text_eq(qualified, "std.collections.fixedRingBuffer")) {
      free(qualified);
      return true;
    }
    if (ir_text_eq(qualified, "std.collections.fixedMap")) {
      free(qualified);
      return true;
    }
    if (ir_text_eq(qualified, "std.io.fixedReader") || ir_text_eq(qualified, "std.io.fixedReaderLimit") || ir_text_eq(qualified, "std.io.fixedWriter")) {
      free(qualified);
      return true;
    }
    const char *target = z_std_source_target_for_public_call(qualified);
    if (target) callee_name = target;
  }
  if (ir_text_eq(callee_name, "__zero_std_collections_fixed_set")) {
    free(qualified);
    return true;
  }
  if (ir_text_eq(callee_name, "__zero_std_collections_fixed_deque")) {
    free(qualified);
    return true;
  }
  if (ir_text_eq(callee_name, "__zero_std_collections_fixed_ring_buffer")) {
    free(qualified);
    return true;
  }
  if (ir_text_eq(callee_name, "__zero_std_collections_fixed_map")) {
    free(qualified);
    return true;
  }
  if (ir_text_eq(callee_name, "__zero_std_io_fixed_reader") || ir_text_eq(callee_name, "__zero_std_io_fixed_reader_limit") || ir_text_eq(callee_name, "__zero_std_io_fixed_writer")) {
    free(qualified);
    return true;
  }
  const ZProgramGraphNode *generic = ir_graph_find_function_by_name(graph, callee_name);
  if (!generic || !ir_graph_function_has_type_params(graph, generic)) {
    free(qualified);
    return true;
  }
  char **param_names = NULL;
  char **arg_types = NULL;
  size_t binding_len = 0;
  if (!ir_graph_specialization_bindings_for_call(graph, context_fun, generic, call, IR_TYPE_UNSUPPORTED, &param_names, &arg_types, &binding_len)) {
    free(qualified);
    return true;
  }
  char *specialized_name = ir_graph_specialized_function_name_for_types(generic, arg_types, binding_len);
  if (ir_graph_has_concrete_function_named(graph, specialized_name)) {
    ir_graph_mark_unsupported(ir, call, "typed graph MIR generic specialization name collides with an existing function", specialized_name);
    free(specialized_name);
    free(qualified);
    for (size_t i = 0; i < binding_len; i++) { free(param_names[i]); free(arg_types[i]); }
    free(param_names);
    free(arg_types);
    return false;
  }
  if (ir_graph_order_has_name(*order, *len, specialized_name)) {
    free(specialized_name);
    free(qualified);
    for (size_t i = 0; i < binding_len; i++) { free(param_names[i]); free(arg_types[i]); }
    free(param_names);
    free(arg_types);
    return true;
  }
  ir_graph_order_vec_push(ir, order, stable_ids, len, cap, graph, generic, specialized_name, true, param_names, arg_types, binding_len);
  free(qualified);
  if (!ir_graph_add_nested_generic_specialization_orders(graph, ir, order, stable_ids, len, cap, generic, param_names, arg_types, binding_len)) return false;
  return true;
}

static bool ir_graph_add_nested_generic_specialization_orders(const ZProgramGraph *graph, IrProgram *ir, IrFunctionOrder **order, char ***stable_ids, size_t *len, size_t *cap, const ZProgramGraphNode *generic, char **param_names, char **arg_types, size_t binding_len) {
  const ZProgramGraphNode *body = ir_graph_ordered_node(graph, generic ? generic->id : NULL, "body", 0);
  if (!body) return true;
  IrFunction context_fun = {.generic_param_names = param_names, .generic_arg_types = arg_types, .generic_binding_len = binding_len};
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if ((node->kind != Z_PROGRAM_GRAPH_NODE_CALL && node->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL) || !ir_graph_node_reaches_ancestor(graph, node->id, body->id, 0)) continue;
    if (!ir_graph_add_generic_specialization_order_context(graph, ir, order, stable_ids, len, cap, &context_fun, node)) return false;
  }
  return true; }
static bool ir_graph_add_reachable_function_order(const ZProgramGraph *graph, IrProgram *ir, IrFunctionOrder **order, char ***stable_ids, size_t *len, size_t *cap, const ZProgramGraphNode *function);
static bool ir_graph_add_reachable_child_orders(const ZProgramGraph *graph, IrProgram *ir, IrFunctionOrder **order, char ***stable_ids, size_t *len, size_t *cap, const char *parent_id, unsigned depth) { if (!graph || !parent_id || depth > 128) return true;
  const IrGraphIndex *index = NULL; size_t run_start = 0; size_t run_len = 0;
  ir_graph_child_edge_run(graph, &index, parent_id, &run_start, &run_len);
  for (size_t i = 0; i < run_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[index->child_edges[run_start + i]];
    const ZProgramGraphNode *child = ir_graph_find_node(graph, edge->to); if (!child) continue;
    if (child->kind == Z_PROGRAM_GRAPH_NODE_CALL || child->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL) { if (!ir_graph_add_generic_specialization_order_context(graph, ir, order, stable_ids, len, cap, NULL, child)) return false;
      char *qualified = ir_graph_expr_qualified_name(graph, child); const char *callee = qualified && qualified[0] ? qualified : child->name;
      const char *target = z_std_source_target_for_public_call(callee);
      bool intrinsic_fixed_io = target &&
                                (ir_text_eq(target, "__zero_std_io_fixed_reader") ||
                                 ir_text_eq(target, "__zero_std_io_fixed_reader_limit") ||
                                 ir_text_eq(target, "__zero_std_io_fixed_writer"));
      const ZProgramGraphNode *callee_function = intrinsic_fixed_io ? NULL : ir_graph_find_function_by_name(graph, target ? target : child->name);
      free(qualified); if (callee_function && !ir_graph_add_reachable_function_order(graph, ir, order, stable_ids, len, cap, callee_function)) return false;
    }
    if (!ir_graph_add_reachable_child_orders(graph, ir, order, stable_ids, len, cap, child->id, depth + 1)) return false;
  }
  return true; }
static bool ir_graph_add_reachable_function_order(const ZProgramGraph *graph, IrProgram *ir, IrFunctionOrder **order, char ***stable_ids, size_t *len, size_t *cap, const ZProgramGraphNode *function) { if (!function || function->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION || ir_graph_node_is_test_function(function) || ir_graph_function_has_type_params(graph, function) || ir_graph_order_has_name(*order, *len, function->name)) return true;
  ir_graph_order_vec_push(ir, order, stable_ids, len, cap, graph, function, function->name, false, NULL, NULL, 0); const ZProgramGraphNode *body = ir_graph_ordered_node(graph, function->id, "body", 0);
  return !body || ir_graph_add_reachable_child_orders(graph, ir, order, stable_ids, len, cap, body->id, 0); }

static IrProgram ir_lower_program_graph(const ZProgramGraph *graph, const SourceInput *input, const ZTargetInfo *target) {
  IrProgram ir = {0};
  ir.target = target;
  ir.package_root = input && input->package_root ? z_strdup(input->package_root) : NULL;
  ir.mir_valid = true;
  ir.mir_line = 1;
  ir.mir_column = 1;
  snprintf(ir.mir_expected, sizeof(ir.mir_expected), "typed program graph MIR subset");
  snprintf(ir.mir_help, sizeof(ir.mir_help), "use zero check to inspect unsupported graph constructs or choose another supported target");
  ir.mir_bytes = sizeof(IrProgram);

  IrFunctionOrder *order = NULL;
  char **stable_ids = NULL;
  size_t ordered_len = 0;
  size_t ordered_cap = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && (node->export_c || ir_graph_function_is_hosted_world_main(graph, node))) {
      if (!ir_graph_add_reachable_function_order(graph, &ir, &order, &stable_ids, &ordered_len, &ordered_cap, node)) {
        ir_free_function_order(order, stable_ids, ordered_len);
        return ir;
      }
    }
  }
  qsort(order, ordered_len, sizeof(IrFunctionOrder), ir_function_order_compare);
  for (size_t i = 0; i < ordered_len; i++) {
    ir_graph_push_function(&ir, graph, &graph->nodes[order[i].source_index], order[i].name, order[i].generic_param_names, order[i].generic_arg_types, order[i].generic_binding_len);
  }

  for (size_t i = 0; i < ordered_len; i++) {
    const ZProgramGraphNode *source = &graph->nodes[order[i].source_index];
    if (!ir_graph_collect_function_locals(&ir, &ir.functions[i], graph, source)) {
      ir_free_function_order(order, stable_ids, ordered_len);
      return ir;
    }
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

IrProgram z_lower_program_graph_with_source(const ZProgramGraph *graph, const SourceInput *input, const ZTargetInfo *target) { return ir_lower_program_graph(graph, input, target); }

static bool ir_graph_lower_checked_program(const ZProgramGraph *graph, const char *path, const ZTargetInfo *target, Program *program, SourceInput *input, ZDiag *diag) {
  bool ok = z_program_graph_lower_to_program_with_source(graph, path, program, input, diag);
  if (ok) { z_set_check_target(target); ok = z_check_program(program, diag); }
  if (!ok) {
    if (input && input->source_file) z_map_source_diag(input, diag);
    if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : path;
  }
  return ok;
}

bool z_program_graph_prepare_artifact_mir_input(const char *artifact_path, const ZTargetInfo *target, const char *emit_kind, const char *requested_backend, Program *program, SourceInput *input, IrProgram *ir, ZProgramGraphArtifactSource *source, ZDiag *diag) {
  (void)program;
  if (!ir) return false;
  ZProgramGraph graph = {0};
  long long phase_started = ir_graph_now_ms();
  if (!z_program_graph_load(artifact_path, &graph, diag)) return false;
  if (input) input->graph_load_ms += ir_graph_now_ms() - phase_started;

  phase_started = ir_graph_now_ms();
  if (!z_std_source_path_is_module_artifact(artifact_path) && !z_program_graph_merge_embedded_std_graph_modules_timed(&graph, input, diag)) {
    if (input) input->graph_stdlib_merge_ms += ir_graph_now_ms() - phase_started;
    z_program_graph_free(&graph);
    return false;
  }
  if (input) input->graph_stdlib_merge_ms += ir_graph_now_ms() - phase_started;
  z_program_graph_seed_source_metadata(input, &graph);
  char *mir_cache_path = z_mir_binary_cache_path_for_graph_store(artifact_path, graph.graph_hash, target, emit_kind, requested_backend);
  ZMirBinaryCacheFacts mir_cache = {0};
  phase_started = ir_graph_now_ms();
  if (mir_cache_path && z_mir_binary_load_path(mir_cache_path, graph.graph_hash, target, emit_kind, requested_backend, ir, &mir_cache, NULL)) {
    if (input) input->graph_mir_cache_load_ms += ir_graph_now_ms() - phase_started;
    if (input) {
      z_program_graph_seed_artifact_source_paths(input, &graph, artifact_path);
      z_program_graph_seed_source_metadata_facts(input, &graph);
      input->program_graph_hash = z_strdup(graph.graph_hash ? graph.graph_hash : "");
      input->program_graph_module_identity = z_strdup(graph.module_identity ? graph.module_identity : "");
      ir_graph_set_mapped_mir_cache_facts(input, &mir_cache, true, false, true, false);
    }
    if (source) {
      source->artifact = artifact_path;
      source->graph_hash = input ? input->program_graph_hash : "";
      source->module_identity = input ? input->program_graph_module_identity : "";
      source->lowering = "mapped-final-mir";
      source->canonical_source = graph.canonical_source;
    }
    free(mir_cache_path);
    z_program_graph_free(&graph);
    return true;
  }
  if (input) input->graph_mir_cache_load_ms += ir_graph_now_ms() - phase_started;

  phase_started = ir_graph_now_ms();
  IrProgram graph_ir = z_lower_program_graph_with_source(&graph, input, target);
  if (input) input->graph_mir_lower_ms += ir_graph_now_ms() - phase_started;
  if (graph_ir.mir_valid) {
    if (input) z_program_graph_seed_artifact_source_paths(input, &graph, artifact_path);
    if (mir_cache_path) {
      ZDiag cache_diag = {0};
      phase_started = ir_graph_now_ms();
      if (z_mir_binary_write_path(mir_cache_path, &graph_ir, graph.graph_hash, target, emit_kind, requested_backend, &cache_diag)) {
        if (input) input->graph_mir_cache_write_ms += ir_graph_now_ms() - phase_started;
        IrProgram mapped_ir = {0};
        phase_started = ir_graph_now_ms();
        if (z_mir_binary_load_path(mir_cache_path, graph.graph_hash, target, emit_kind, requested_backend, &mapped_ir, &mir_cache, NULL)) {
          if (input) input->graph_mir_cache_reload_ms += ir_graph_now_ms() - phase_started;
          z_free_ir_program(&graph_ir);
          graph_ir = mapped_ir;
          ir_graph_set_mapped_mir_cache_facts(input, &mir_cache, false, true, false, false);
        } else if (input) {
          input->graph_mir_cache_reload_ms += ir_graph_now_ms() - phase_started;
        }
      } else if (input) {
        input->graph_mir_cache_write_ms += ir_graph_now_ms() - phase_started;
      }
    }
    *ir = graph_ir;
  } else {
    if (diag && diag->code == 0) ir_graph_init_lowering_diag(diag, input, target, NULL, NULL, &graph_ir, artifact_path);
    z_free_ir_program(&graph_ir);
    if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : artifact_path;
    free(mir_cache_path);
    z_program_graph_free(&graph);
    return false;
  }
  if (input) {
    z_program_graph_seed_source_metadata_facts(input, &graph);
    input->program_graph_hash = z_strdup(graph.graph_hash ? graph.graph_hash : "");
    input->program_graph_module_identity = z_strdup(graph.module_identity ? graph.module_identity : "");
    ir_graph_set_mapped_mir_cache_facts(input, &mir_cache, false, mir_cache.hit, false, false);
  }
  if (source) {
    source->artifact = artifact_path;
    source->graph_hash = input ? input->program_graph_hash : "";
    source->module_identity = input ? input->program_graph_module_identity : "";
    source->lowering = mir_cache.hit ? "mapped-final-mir" : "typed-program-graph-mir";
    source->canonical_source = graph.canonical_source;
  }
  free(mir_cache_path);
  z_program_graph_free(&graph);
  return true;
}

bool z_program_graph_prepare_repository_store_mir_input(const char *store_path, const ZTargetInfo *target, const char *emit_kind, const char *requested_backend, bool require_checked_program, Program *program, SourceInput *input, IrProgram *ir, ZProgramGraphArtifactSource *source, ZDiag *diag) {
  if (!ir) return false;
  ZProgramGraphStore store;
  long long phase_started = ir_graph_now_ms();
  if (!z_program_graph_store_load_path(store_path, &store, diag)) return false;
  if (input) input->graph_load_ms += ir_graph_now_ms() - phase_started;
  /* Classify the source projection before the stdlib merge mutates the stored
   * graph; classifying the merged graph would misreport the state as conflict. */
  const char *source_projection_state = source ? z_program_graph_projection_state_label(&store, target, NULL, NULL, NULL) : NULL;
  /*
   * The MIR cache is keyed on the pre-merge store graph hash. The merged
   * graph is a pure function of the stored graph and the embedded stdlib
   * graphs, and the cache path already folds in the stdlib fingerprint plus
   * compiler version, so a cache hit can skip the stdlib merge entirely.
   * Metadata seeding also reads the pre-merge graph on every path so cache
   * hits and misses report identical facts.
   */
  char *store_graph_hash = z_strdup(store.graph.graph_hash ? store.graph.graph_hash : "");
  z_program_graph_seed_source_metadata(input, &store.graph);
  if (input && !input->package_root && store.root) input->package_root = z_strdup(store.root);
  if (!require_checked_program) z_program_graph_seed_artifact_source_paths(input, &store.graph, store_path);
  char *mir_cache_path = z_mir_binary_cache_path_for_graph_store(store_path, store_graph_hash, target, emit_kind, requested_backend);
  ZMirBinaryCacheFacts mir_cache = {0};
  phase_started = ir_graph_now_ms();
  if (mir_cache_path && z_mir_binary_load_path(mir_cache_path, store_graph_hash, target, emit_kind, requested_backend, ir, &mir_cache, NULL)) {
    if (input) input->graph_mir_cache_load_ms += ir_graph_now_ms() - phase_started;
    if (require_checked_program) {
      /* The checked-program path verifies against stdlib bodies, so the
       * merge still runs before the checker on cache hits. */
      phase_started = ir_graph_now_ms();
      bool merged = z_program_graph_merge_embedded_std_graph_modules_timed(&store.graph, input, diag);
      if (input) input->graph_stdlib_merge_ms += ir_graph_now_ms() - phase_started;
      if (!merged) {
        z_free_ir_program(ir);
        free(mir_cache_path);
        free(store_graph_hash);
        z_program_graph_store_free(&store);
        return false;
      }
      phase_started = ir_graph_now_ms();
      bool checked = ir_graph_lower_checked_program(&store.graph, store_path, target, program, input, diag);
      if (input) input->graph_readiness_check_ms += ir_graph_now_ms() - phase_started;
      if (!checked) {
        z_free_ir_program(ir);
        free(mir_cache_path);
        free(store_graph_hash);
        z_program_graph_store_free(&store);
        return false;
      }
    }
    if (input) {
      input->program_graph_hash = z_strdup(store_graph_hash);
      input->program_graph_module_identity = z_strdup(store.graph.module_identity ? store.graph.module_identity : "");
      ir_graph_set_mapped_mir_cache_facts(input, &mir_cache, true, false, !require_checked_program, require_checked_program);
    }
    if (source) {
      source->artifact = store_path;
      source->graph_hash = input ? input->program_graph_hash : "";
      source->module_identity = input ? input->program_graph_module_identity : "";
      source->lowering = "mapped-final-mir";
      source->source_projection_state = source_projection_state;
      source->canonical_source = false;
    }
    free(mir_cache_path);
    free(store_graph_hash);
    z_program_graph_store_free(&store);
    return true;
  }
  if (input) input->graph_mir_cache_load_ms += ir_graph_now_ms() - phase_started;

  phase_started = ir_graph_now_ms();
  if (!z_program_graph_merge_embedded_std_graph_modules_timed(&store.graph, input, diag)) {
    if (input) input->graph_stdlib_merge_ms += ir_graph_now_ms() - phase_started;
    free(mir_cache_path);
    free(store_graph_hash);
    z_program_graph_store_free(&store);
    return false;
  }
  if (input) input->graph_stdlib_merge_ms += ir_graph_now_ms() - phase_started;

  phase_started = ir_graph_now_ms();
  IrProgram graph_ir = z_lower_program_graph_with_source(&store.graph, input, target);
  if (input) input->graph_mir_lower_ms += ir_graph_now_ms() - phase_started;
  bool graph_mir_valid = graph_ir.mir_valid;
  if (graph_mir_valid) {
    if (mir_cache_path) {
      ZDiag cache_diag = {0};
      phase_started = ir_graph_now_ms();
      if (z_mir_binary_write_path(mir_cache_path, &graph_ir, store_graph_hash, target, emit_kind, requested_backend, &cache_diag)) {
        if (input) input->graph_mir_cache_write_ms += ir_graph_now_ms() - phase_started;
        IrProgram mapped_ir = {0};
        phase_started = ir_graph_now_ms();
        if (z_mir_binary_load_path(mir_cache_path, store_graph_hash, target, emit_kind, requested_backend, &mapped_ir, &mir_cache, NULL)) {
          if (input) input->graph_mir_cache_reload_ms += ir_graph_now_ms() - phase_started;
          z_free_ir_program(&graph_ir);
          graph_ir = mapped_ir;
          ir_graph_set_mapped_mir_cache_facts(input, &mir_cache, false, true, false, require_checked_program);
        } else if (input) {
          input->graph_mir_cache_reload_ms += ir_graph_now_ms() - phase_started;
        }
      } else if (input) {
        input->graph_mir_cache_write_ms += ir_graph_now_ms() - phase_started;
      }
    }
    *ir = graph_ir;
    if (require_checked_program) {
      phase_started = ir_graph_now_ms();
      bool checked = ir_graph_lower_checked_program(&store.graph, store_path, target, program, input, diag);
      if (input) input->graph_readiness_check_ms += ir_graph_now_ms() - phase_started;
      if (!checked) {
        z_free_ir_program(ir);
        free(mir_cache_path);
        free(store_graph_hash);
        z_program_graph_store_free(&store);
        return false;
      }
    }
  } else {
    if (diag && diag->code == 0) ir_graph_init_lowering_diag(diag, input, target, emit_kind, requested_backend, &graph_ir, store_path);
    z_free_ir_program(&graph_ir);
    if (diag && !diag->path) diag->path = input && input->source_file ? input->source_file : store_path;
    free(mir_cache_path);
    free(store_graph_hash);
    z_program_graph_store_free(&store);
    return false;
  }
  if (input) {
    input->program_graph_hash = z_strdup(store_graph_hash);
    input->program_graph_module_identity = z_strdup(store.graph.module_identity ? store.graph.module_identity : "");
    ir_graph_set_mapped_mir_cache_facts(input, &mir_cache, false, mir_cache.hit, false, require_checked_program);
  }
  if (source) {
    source->artifact = store_path;
    source->graph_hash = input ? input->program_graph_hash : "";
    source->module_identity = input ? input->program_graph_module_identity : "";
    source->lowering = mir_cache.hit ? "mapped-final-mir" : "typed-program-graph-mir";
    source->source_projection_state = source_projection_state;
    source->canonical_source = false;
  }
  free(mir_cache_path);
  free(store_graph_hash);
  z_program_graph_store_free(&store);
  return true;
}
