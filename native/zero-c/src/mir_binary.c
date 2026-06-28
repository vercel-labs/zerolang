#include "mir_binary.h"
#include "std_source.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <sys/mman.h>
#include <unistd.h>
#endif

typedef struct {
  uint64_t offset;
  uint64_t len;
} MirStringRef;

typedef struct {
  const unsigned char *data;
  size_t len;
  size_t cursor;
} MirReader;

typedef struct {
  ZBuf bytes;
} MirStringTable;

typedef struct {
  const char *name;
  const char *stable_id;
  const char *world_param_name;
  IrTypeKind return_type;
  IrTypeKind value_return_type;
  IrTypeKind return_element_type;
  uint64_t local_start;
  uint64_t local_len;
  uint64_t param_count;
  uint64_t instr_ref_start;
  uint64_t instr_ref_len;
  uint64_t frame_bytes;
  bool is_exported;
  bool raises;
  int line;
  int column;
} MirFunctionFlat;

typedef struct {
  const char *name;
  const char *shape_name;
  IrTypeKind type;
  IrTypeKind element_type;
  unsigned index;
  unsigned frame_offset;
  unsigned array_len;
  unsigned field_offset;
  unsigned byte_size;
  unsigned alignment;
  bool is_param;
  bool is_array;
  bool is_record;
  bool is_record_ref;
  bool is_mutable;
  unsigned ref_byte_size;
  int line;
  int column;
} MirLocalFlat;

typedef struct {
  IrValueKind kind;
  IrTypeKind type;
  unsigned long long int_value;
  unsigned local_index;
  unsigned callee_index;
  unsigned array_index;
  unsigned field_offset;
  unsigned data_offset;
  unsigned data_len;
  IrTypeKind element_type;
  unsigned error_code;
  unsigned external_index;
  bool external_call;
  IrBinaryOp binary_op;
  IrCompareOp compare_op;
  uint64_t arg_ref_start;
  uint64_t arg_ref_len;
  uint64_t index_ref;
  uint64_t left_ref;
  uint64_t right_ref;
  int line;
  int column;
} MirValueFlat;

typedef struct {
  IrInstrKind kind;
  unsigned local_index;
  unsigned array_index;
  unsigned field_offset;
  unsigned error_code;
  uint64_t value_ref;
  uint64_t index_ref;
  uint64_t then_ref_start;
  uint64_t then_ref_len;
  uint64_t else_ref_start;
  uint64_t else_ref_len;
  int line;
  int column;
} MirInstrFlat;

typedef struct {
  const char *symbol;
  const char *import_header;
  const char *import_resolved_header;
  IrTypeKind return_type;
  uint64_t param_type_start;
  uint64_t param_type_len;
} MirExternalFlat;

typedef struct {
  unsigned offset;
  unsigned len;
  uint64_t data_start;
} MirDataFlat;

typedef struct {
  MirFunctionFlat *functions;
  size_t function_len;
  size_t function_cap;
  MirLocalFlat *locals;
  size_t local_len;
  size_t local_cap;
  MirInstrFlat *instrs;
  size_t instr_len;
  size_t instr_cap;
  MirValueFlat *values;
  size_t value_len;
  size_t value_cap;
  uint64_t *value_refs;
  size_t value_ref_len;
  size_t value_ref_cap;
  uint64_t *instr_refs;
  size_t instr_ref_len;
  size_t instr_ref_cap;
  MirExternalFlat *externals;
  size_t external_len;
  size_t external_cap;
  uint32_t *external_param_types;
  size_t external_param_type_len;
  size_t external_param_type_cap;
  MirDataFlat *data_segments;
  size_t data_segment_len;
  size_t data_segment_cap;
  ZBuf data_bytes;
} MirFlat;

typedef struct {
  uint32_t schema;
  uint32_t flags;
  uint64_t function_count;
  uint64_t local_count;
  uint64_t instr_count;
  uint64_t value_count;
  uint64_t value_ref_count;
  uint64_t instr_ref_count;
  uint64_t external_count;
  uint64_t external_param_type_count;
  uint64_t data_segment_count;
  uint64_t data_bytes_len;
  uint64_t strings_len;
  MirStringRef compiler_version_ref;
  MirStringRef graph_hash_ref;
  MirStringRef target_ref;
  MirStringRef emit_kind_ref;
  MirStringRef backend_ref;
  uint64_t readonly_data_bytes;
  uint64_t direct_function_count;
  uint64_t direct_export_count;
  uint64_t direct_stack_bytes;
  uint64_t direct_max_frame_bytes;
  uint64_t direct_readonly_data_bytes;
  uint64_t direct_allocator_helper_count;
  uint64_t direct_buffer_helper_count;
  uint64_t direct_runtime_helper_count;
  uint64_t direct_host_runtime_import_count;
  uint64_t direct_http_runtime_import_count;
  uint64_t direct_c_import_call_count;
  uint64_t direct_c_import_symbol_count;
  int32_t mir_line;
  int32_t mir_column;
  const unsigned char *strings;
  size_t strings_offset;
  size_t strings_len_size;
} MirHeader;

typedef struct {
  const unsigned char *data;
  size_t len;
  bool mapped;
#if !defined(_WIN32)
  int fd;
#endif
} MirMappedFile;

typedef struct {
  const MirHeader *header;
  MirFunctionFlat *functions;
  MirLocalFlat *locals;
  MirInstrFlat *instrs;
  MirValueFlat *values;
  uint64_t *value_refs;
  uint64_t *instr_refs;
  MirExternalFlat *externals;
  uint32_t *external_param_types;
  MirDataFlat *data_segments;
  const unsigned char *data_bytes;
} MirDecoded;

static const unsigned char MIR_BINARY_MAGIC[8] = {'Z', 'M', 'I', 'R', 'B', 'I', 'N', '1'};
enum { MIR_BINARY_SCHEMA_VERSION = 7 };
static const uint64_t MIR_NULL_LEN = UINT64_MAX;
static const uint64_t MIR_BINARY_MAX_FUNCTION_COUNT = 100000;
static const uint64_t MIR_BINARY_MAX_LOCAL_COUNT = 1000000;
static const uint64_t MIR_BINARY_MAX_INSTR_COUNT = 4000000;
static const uint64_t MIR_BINARY_MAX_VALUE_COUNT = 4000000;
static const uint64_t MIR_BINARY_MAX_REF_COUNT = 8000000;
static const uint64_t MIR_BINARY_MAX_EXTERNAL_COUNT = 100000;
static const uint64_t MIR_BINARY_MAX_EXTERNAL_PARAM_TYPE_COUNT = 1000000;
static const uint64_t MIR_BINARY_MAX_DATA_SEGMENT_COUNT = 1000000;
static const uint64_t MIR_BINARY_MAX_DATA_BYTES = 256u * 1024u * 1024u;
static const uint64_t MIR_BINARY_MAX_STRING_BYTES = 256u * 1024u * 1024u;
enum {
  MIR_FUNCTION_RECORD_BYTES = 120,
  MIR_LOCAL_RECORD_BYTES = 80,
  MIR_INSTR_RECORD_BYTES = 80,
  MIR_VALUE_RECORD_BYTES = 112,
  MIR_EXTERNAL_RECORD_BYTES = 72,
  MIR_DATA_SEGMENT_RECORD_BYTES = 16,
};

static void mir_diag(ZDiag *diag, const char *path, const char *message, const char *actual) {
  if (!diag) return;
  *diag = (ZDiag){0};
  diag->code = 2002;
  diag->path = path;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "invalid mapped MIR cache");
  snprintf(diag->expected, sizeof(diag->expected), "valid zero mapped MIR binary");
  snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "invalid MIR binary");
}

static bool mir_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static void mir_append_bytes(ZBuf *buf, const void *data, size_t len) {
  const unsigned char *bytes = (const unsigned char *)data;
  for (size_t i = 0; i < len; i++) zbuf_append_char(buf, (char)bytes[i]);
}

static void mir_put_u32(ZBuf *buf, uint32_t value) {
  for (unsigned i = 0; i < 4; i++) zbuf_append_char(buf, (char)((value >> (i * 8)) & 0xffu));
}

static void mir_put_i32(ZBuf *buf, int32_t value) {
  mir_put_u32(buf, (uint32_t)value);
}

static void mir_put_u64(ZBuf *buf, uint64_t value) {
  for (unsigned i = 0; i < 8; i++) zbuf_append_char(buf, (char)((value >> (i * 8)) & 0xffu));
}

static void mir_put_ref(ZBuf *buf, MirStringRef ref) {
  mir_put_u64(buf, ref.offset);
  mir_put_u64(buf, ref.len);
}

static MirStringRef mir_add_string(MirStringTable *table, const char *text) {
  if (!text) return (MirStringRef){.offset = 0, .len = MIR_NULL_LEN};
  size_t len = strlen(text);
  MirStringRef ref = {.offset = (uint64_t)table->bytes.len, .len = (uint64_t)len};
  mir_append_bytes(&table->bytes, text, len);
  zbuf_append_char(&table->bytes, '\0');
  return ref;
}

static void *mir_vec_push(void *items, size_t *len, size_t *cap, size_t item_size) {
  if (*len == *cap) {
    size_t next = z_grow_capacity(*cap, *len + 1, 16);
    items = z_checked_reallocarray(items, next, item_size);
    *cap = next;
  }
  *len += 1;
  return items;
}

#define MIR_PUSH(vec, field, value) do { \
  (vec)->field##s = mir_vec_push((vec)->field##s, &(vec)->field##_len, &(vec)->field##_cap, sizeof(*(vec)->field##s)); \
  (vec)->field##s[(vec)->field##_len - 1] = (value); \
} while (0)

static void mir_ref_vec_push(uint64_t **items, size_t *len, size_t *cap, uint64_t value) {
  *items = mir_vec_push(*items, len, cap, sizeof(uint64_t));
  (*items)[*len - 1] = value;
}

static uint64_t mir_collect_value(MirFlat *flat, const IrValue *value) {
  if (!value) return 0;
  uint64_t index_ref = mir_collect_value(flat, value->index);
  uint64_t left_ref = mir_collect_value(flat, value->left);
  uint64_t right_ref = mir_collect_value(flat, value->right);
  uint64_t *arg_refs = NULL;
  size_t arg_ref_len = 0;
  size_t arg_ref_cap = 0;
  for (size_t i = 0; i < value->arg_len; i++) {
    mir_ref_vec_push(&arg_refs, &arg_ref_len, &arg_ref_cap, mir_collect_value(flat, value->args[i]));
  }
  uint64_t arg_start = (uint64_t)flat->value_ref_len;
  for (size_t i = 0; i < arg_ref_len; i++) {
    mir_ref_vec_push(&flat->value_refs, &flat->value_ref_len, &flat->value_ref_cap, arg_refs[i]);
  }
  free(arg_refs);
  MirValueFlat record = {
    .kind = value->kind,
    .type = value->type,
    .int_value = value->int_value,
    .local_index = value->local_index,
    .callee_index = value->callee_index,
    .array_index = value->array_index,
    .field_offset = value->field_offset,
    .data_offset = value->data_offset,
    .data_len = value->data_len,
    .element_type = value->element_type,
    .error_code = value->error_code,
    .external_index = value->external_index,
    .external_call = value->external_call,
    .binary_op = value->binary_op,
    .compare_op = value->compare_op,
    .arg_ref_start = arg_start,
    .arg_ref_len = (uint64_t)value->arg_len,
    .index_ref = index_ref,
    .left_ref = left_ref,
    .right_ref = right_ref,
    .line = value->line,
    .column = value->column,
  };
  flat->values = mir_vec_push(flat->values, &flat->value_len, &flat->value_cap, sizeof(MirValueFlat));
  flat->values[flat->value_len - 1] = record;
  return (uint64_t)flat->value_len;
}

static uint64_t mir_collect_instr(MirFlat *flat, const IrInstr *instr);

static void mir_collect_instr_block(MirFlat *flat, const IrInstr *instrs, size_t len, uint64_t *out_start, uint64_t *out_len) {
  uint64_t *refs = NULL;
  size_t ref_len = 0;
  size_t ref_cap = 0;
  for (size_t i = 0; i < len; i++) {
    mir_ref_vec_push(&refs, &ref_len, &ref_cap, mir_collect_instr(flat, &instrs[i]));
  }
  *out_start = (uint64_t)flat->instr_ref_len;
  *out_len = (uint64_t)ref_len;
  for (size_t i = 0; i < ref_len; i++) {
    mir_ref_vec_push(&flat->instr_refs, &flat->instr_ref_len, &flat->instr_ref_cap, refs[i]);
  }
  free(refs);
}

static uint64_t mir_collect_instr(MirFlat *flat, const IrInstr *instr) {
  uint64_t then_start = 0, then_len = 0, else_start = 0, else_len = 0;
  mir_collect_instr_block(flat, instr->then_instrs, instr->then_len, &then_start, &then_len);
  mir_collect_instr_block(flat, instr->else_instrs, instr->else_len, &else_start, &else_len);
  MirInstrFlat record = {
    .kind = instr->kind,
    .local_index = instr->local_index,
    .array_index = instr->array_index,
    .field_offset = instr->field_offset,
    .error_code = instr->error_code,
    .value_ref = mir_collect_value(flat, instr->value),
    .index_ref = mir_collect_value(flat, instr->index),
    .then_ref_start = then_start,
    .then_ref_len = then_len,
    .else_ref_start = else_start,
    .else_ref_len = else_len,
    .line = instr->line,
    .column = instr->column,
  };
  flat->instrs = mir_vec_push(flat->instrs, &flat->instr_len, &flat->instr_cap, sizeof(MirInstrFlat));
  flat->instrs[flat->instr_len - 1] = record;
  return (uint64_t)flat->instr_len;
}

static bool mir_flatten_program(const IrProgram *program, MirFlat *flat) {
  if (!program || !program->mir_valid) return false;
  zbuf_init(&flat->data_bytes);
  for (size_t i = 0; i < program->function_len; i++) {
    const IrFunction *fun = &program->functions[i];
    uint64_t local_start = (uint64_t)flat->local_len;
    for (size_t local_index = 0; local_index < fun->local_len; local_index++) {
      const IrLocal *local = &fun->locals[local_index];
      MirLocalFlat record = {
        .name = local->name,
        .shape_name = local->shape_name,
        .type = local->type,
        .element_type = local->element_type,
        .index = local->index,
        .frame_offset = local->frame_offset,
        .array_len = local->array_len,
        .field_offset = local->field_offset,
        .byte_size = local->byte_size,
        .alignment = local->alignment,
        .is_param = local->is_param,
        .is_array = local->is_array,
        .is_record = local->is_record,
        .is_record_ref = local->is_record_ref,
        .is_mutable = local->is_mutable,
        .ref_byte_size = local->ref_byte_size,
        .line = local->line,
        .column = local->column,
      };
      flat->locals = mir_vec_push(flat->locals, &flat->local_len, &flat->local_cap, sizeof(MirLocalFlat));
      flat->locals[flat->local_len - 1] = record;
    }
    uint64_t instr_ref_start = 0, instr_ref_len = 0;
    mir_collect_instr_block(flat, fun->instrs, fun->instr_len, &instr_ref_start, &instr_ref_len);
    MirFunctionFlat record = {
      .name = fun->name,
      .stable_id = fun->stable_id,
      .world_param_name = fun->world_param_name,
      .return_type = fun->return_type,
      .value_return_type = fun->value_return_type,
      .return_element_type = fun->return_element_type,
      .local_start = local_start,
      .local_len = (uint64_t)fun->local_len,
      .param_count = (uint64_t)fun->param_count,
      .instr_ref_start = instr_ref_start,
      .instr_ref_len = instr_ref_len,
      .frame_bytes = (uint64_t)fun->frame_bytes,
      .is_exported = fun->is_exported,
      .raises = fun->raises,
      .line = fun->line,
      .column = fun->column,
    };
    flat->functions = mir_vec_push(flat->functions, &flat->function_len, &flat->function_cap, sizeof(MirFunctionFlat));
    flat->functions[flat->function_len - 1] = record;
  }
  for (size_t i = 0; i < program->external_function_len; i++) {
    const IrExternalFunction *external = &program->external_functions[i];
    uint64_t param_start = (uint64_t)flat->external_param_type_len;
    for (size_t param_index = 0; param_index < external->param_len; param_index++) {
      flat->external_param_types = mir_vec_push(flat->external_param_types, &flat->external_param_type_len, &flat->external_param_type_cap, sizeof(uint32_t));
      flat->external_param_types[flat->external_param_type_len - 1] = (uint32_t)external->param_types[param_index];
    }
    MirExternalFlat record = {
      .symbol = external->symbol,
      .import_header = external->import_header,
      .import_resolved_header = external->import_resolved_header,
      .return_type = external->return_type,
      .param_type_start = param_start,
      .param_type_len = (uint64_t)external->param_len,
    };
    flat->externals = mir_vec_push(flat->externals, &flat->external_len, &flat->external_cap, sizeof(MirExternalFlat));
    flat->externals[flat->external_len - 1] = record;
  }
  for (size_t i = 0; i < program->data_segment_len; i++) {
    const IrDataSegment *segment = &program->data_segments[i];
    MirDataFlat record = {.offset = segment->offset, .len = segment->len, .data_start = (uint64_t)flat->data_bytes.len};
    if (segment->len > 0 && segment->bytes) mir_append_bytes(&flat->data_bytes, segment->bytes, segment->len);
    flat->data_segments = mir_vec_push(flat->data_segments, &flat->data_segment_len, &flat->data_segment_cap, sizeof(MirDataFlat));
    flat->data_segments[flat->data_segment_len - 1] = record;
  }
  return true;
}

static void mir_flat_free(MirFlat *flat) {
  if (!flat) return;
  free(flat->functions);
  free(flat->locals);
  free(flat->instrs);
  free(flat->values);
  free(flat->value_refs);
  free(flat->instr_refs);
  free(flat->externals);
  free(flat->external_param_types);
  free(flat->data_segments);
  zbuf_free(&flat->data_bytes);
  *flat = (MirFlat){0};
}

static void mir_put_function_record(ZBuf *out, MirStringTable *strings, const MirFunctionFlat *record) {
  mir_put_ref(out, mir_add_string(strings, record->name));
  mir_put_ref(out, mir_add_string(strings, record->stable_id));
  mir_put_ref(out, mir_add_string(strings, record->world_param_name));
  mir_put_u32(out, (uint32_t)record->return_type);
  mir_put_u32(out, (uint32_t)record->value_return_type);
  mir_put_u32(out, (uint32_t)record->return_element_type);
  mir_put_u32(out, (record->is_exported ? 1u : 0u) | (record->raises ? 2u : 0u));
  mir_put_u64(out, record->local_start);
  mir_put_u64(out, record->local_len);
  mir_put_u64(out, record->param_count);
  mir_put_u64(out, record->instr_ref_start);
  mir_put_u64(out, record->instr_ref_len);
  mir_put_u64(out, record->frame_bytes);
  mir_put_i32(out, (int32_t)record->line);
  mir_put_i32(out, (int32_t)record->column);
}

static void mir_put_local_record(ZBuf *out, MirStringTable *strings, const MirLocalFlat *record) {
  mir_put_ref(out, mir_add_string(strings, record->name));
  mir_put_ref(out, mir_add_string(strings, record->shape_name));
  mir_put_u32(out, (uint32_t)record->type);
  mir_put_u32(out, (uint32_t)record->element_type);
  mir_put_u32(out, record->index);
  mir_put_u32(out, record->frame_offset);
  mir_put_u32(out, record->array_len);
  mir_put_u32(out, record->field_offset);
  mir_put_u32(out, record->byte_size);
  mir_put_u32(out, record->alignment);
  mir_put_u32(out, record->ref_byte_size);
  uint32_t flags = 0;
  if (record->is_param) flags |= 1u << 0;
  if (record->is_array) flags |= 1u << 1;
  if (record->is_record) flags |= 1u << 2;
  if (record->is_mutable) flags |= 1u << 3;
  if (record->is_record_ref) flags |= 1u << 4;
  mir_put_u32(out, flags);
  mir_put_i32(out, (int32_t)record->line);
  mir_put_i32(out, (int32_t)record->column);
}

static void mir_put_value_record(ZBuf *out, const MirValueFlat *record) {
  mir_put_u32(out, (uint32_t)record->kind);
  mir_put_u32(out, (uint32_t)record->type);
  mir_put_u64(out, (uint64_t)record->int_value);
  mir_put_u32(out, record->local_index);
  mir_put_u32(out, record->callee_index);
  mir_put_u32(out, record->array_index);
  mir_put_u32(out, record->field_offset);
  mir_put_u32(out, record->data_offset);
  mir_put_u32(out, record->data_len);
  mir_put_u32(out, (uint32_t)record->element_type);
  mir_put_u32(out, record->error_code);
  mir_put_u32(out, record->external_index);
  mir_put_u32(out, record->external_call ? 1u : 0u);
  mir_put_u32(out, (uint32_t)record->binary_op);
  mir_put_u32(out, (uint32_t)record->compare_op);
  mir_put_u64(out, record->arg_ref_start);
  mir_put_u64(out, record->arg_ref_len);
  mir_put_u64(out, record->index_ref);
  mir_put_u64(out, record->left_ref);
  mir_put_u64(out, record->right_ref);
  mir_put_i32(out, (int32_t)record->line);
  mir_put_i32(out, (int32_t)record->column);
}

static void mir_put_instr_record(ZBuf *out, const MirInstrFlat *record) {
  mir_put_u32(out, (uint32_t)record->kind);
  mir_put_u32(out, record->local_index);
  mir_put_u32(out, record->array_index);
  mir_put_u32(out, record->field_offset);
  mir_put_u32(out, record->error_code);
  mir_put_u32(out, 0);
  mir_put_u64(out, record->value_ref);
  mir_put_u64(out, record->index_ref);
  mir_put_u64(out, record->then_ref_start);
  mir_put_u64(out, record->then_ref_len);
  mir_put_u64(out, record->else_ref_start);
  mir_put_u64(out, record->else_ref_len);
  mir_put_i32(out, (int32_t)record->line);
  mir_put_i32(out, (int32_t)record->column);
}

static void mir_put_external_record(ZBuf *out, MirStringTable *strings, const MirExternalFlat *record) {
  mir_put_ref(out, mir_add_string(strings, record->symbol));
  mir_put_ref(out, mir_add_string(strings, record->import_header));
  mir_put_ref(out, mir_add_string(strings, record->import_resolved_header));
  mir_put_u32(out, (uint32_t)record->return_type);
  mir_put_u32(out, 0);
  mir_put_u64(out, record->param_type_start);
  mir_put_u64(out, record->param_type_len);
}

static void mir_put_data_record(ZBuf *out, const MirDataFlat *record) {
  mir_put_u32(out, record->offset);
  mir_put_u32(out, record->len);
  mir_put_u64(out, record->data_start);
}

bool z_mir_binary_write_path(const char *path, const IrProgram *program, const char *graph_hash, const ZTargetInfo *target, const char *emit_kind, const char *backend, ZDiag *diag) {
  if (!path || !program || !program->mir_valid || !graph_hash || !graph_hash[0]) return false;
  MirFlat flat = {0};
  if (!mir_flatten_program(program, &flat)) return false;
  MirStringTable strings;
  zbuf_init(&strings.bytes);
  ZBuf records;
  zbuf_init(&records);
  MirStringRef compiler_version_ref = mir_add_string(&strings, ZERO_VERSION);
  MirStringRef graph_hash_ref = mir_add_string(&strings, graph_hash);
  MirStringRef target_ref = mir_add_string(&strings, target && target->name ? target->name : "");
  MirStringRef emit_kind_ref = mir_add_string(&strings, emit_kind ? emit_kind : "");
  MirStringRef backend_ref = mir_add_string(&strings, backend ? backend : "");
  for (size_t i = 0; i < flat.function_len; i++) mir_put_function_record(&records, &strings, &flat.functions[i]);
  for (size_t i = 0; i < flat.local_len; i++) mir_put_local_record(&records, &strings, &flat.locals[i]);
  for (size_t i = 0; i < flat.instr_len; i++) mir_put_instr_record(&records, &flat.instrs[i]);
  for (size_t i = 0; i < flat.value_len; i++) mir_put_value_record(&records, &flat.values[i]);
  for (size_t i = 0; i < flat.value_ref_len; i++) mir_put_u64(&records, flat.value_refs[i]);
  for (size_t i = 0; i < flat.instr_ref_len; i++) mir_put_u64(&records, flat.instr_refs[i]);
  for (size_t i = 0; i < flat.external_len; i++) mir_put_external_record(&records, &strings, &flat.externals[i]);
  for (size_t i = 0; i < flat.external_param_type_len; i++) mir_put_u32(&records, flat.external_param_types[i]);
  for (size_t i = 0; i < flat.data_segment_len; i++) mir_put_data_record(&records, &flat.data_segments[i]);
  mir_append_bytes(&records, flat.data_bytes.data ? flat.data_bytes.data : "", flat.data_bytes.len);

  ZBuf out;
  zbuf_init(&out);
  mir_append_bytes(&out, MIR_BINARY_MAGIC, sizeof(MIR_BINARY_MAGIC));
  mir_put_u32(&out, MIR_BINARY_SCHEMA_VERSION);
  mir_put_u32(&out, 0);
  mir_put_u64(&out, (uint64_t)flat.function_len);
  mir_put_u64(&out, (uint64_t)flat.local_len);
  mir_put_u64(&out, (uint64_t)flat.instr_len);
  mir_put_u64(&out, (uint64_t)flat.value_len);
  mir_put_u64(&out, (uint64_t)flat.value_ref_len);
  mir_put_u64(&out, (uint64_t)flat.instr_ref_len);
  mir_put_u64(&out, (uint64_t)flat.external_len);
  mir_put_u64(&out, (uint64_t)flat.external_param_type_len);
  mir_put_u64(&out, (uint64_t)flat.data_segment_len);
  mir_put_u64(&out, (uint64_t)flat.data_bytes.len);
  mir_put_u64(&out, (uint64_t)strings.bytes.len);
  mir_put_ref(&out, compiler_version_ref);
  mir_put_ref(&out, graph_hash_ref);
  mir_put_ref(&out, target_ref);
  mir_put_ref(&out, emit_kind_ref);
  mir_put_ref(&out, backend_ref);
  mir_put_u64(&out, (uint64_t)program->readonly_data_bytes);
  mir_put_u64(&out, (uint64_t)program->direct_function_count);
  mir_put_u64(&out, (uint64_t)program->direct_export_count);
  mir_put_u64(&out, (uint64_t)program->direct_stack_bytes);
  mir_put_u64(&out, (uint64_t)program->direct_max_frame_bytes);
  mir_put_u64(&out, (uint64_t)program->direct_readonly_data_bytes);
  mir_put_u64(&out, (uint64_t)program->direct_allocator_helper_count);
  mir_put_u64(&out, (uint64_t)program->direct_buffer_helper_count);
  mir_put_u64(&out, (uint64_t)program->direct_runtime_helper_count);
  mir_put_u64(&out, (uint64_t)program->direct_host_runtime_import_count);
  mir_put_u64(&out, (uint64_t)program->direct_http_runtime_import_count);
  mir_put_u64(&out, (uint64_t)program->direct_c_import_call_count);
  mir_put_u64(&out, (uint64_t)program->direct_c_import_symbol_count);
  mir_put_i32(&out, (int32_t)program->mir_line);
  mir_put_i32(&out, (int32_t)program->mir_column);
  mir_append_bytes(&out, records.data ? records.data : "", records.len);
  mir_append_bytes(&out, strings.bytes.data ? strings.bytes.data : "", strings.bytes.len);
  bool ok = z_write_binary_file(path, (const unsigned char *)(out.data ? out.data : ""), out.len, diag);
  zbuf_free(&out);
  zbuf_free(&records);
  zbuf_free(&strings.bytes);
  mir_flat_free(&flat);
  return ok;
}

static bool mir_get_bytes(MirReader *reader, void *out, size_t len) {
  if (!reader || reader->cursor > reader->len || len > reader->len - reader->cursor) return false;
  memcpy(out, reader->data + reader->cursor, len);
  reader->cursor += len;
  return true;
}

static bool mir_get_u32(MirReader *reader, uint32_t *out) {
  unsigned char bytes[4];
  if (!mir_get_bytes(reader, bytes, sizeof(bytes))) return false;
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; i++) value |= (uint32_t)bytes[i] << (i * 8);
  *out = value;
  return true;
}

static bool mir_get_i32(MirReader *reader, int32_t *out) {
  uint32_t value = 0;
  if (!mir_get_u32(reader, &value)) return false;
  *out = (int32_t)value;
  return true;
}

static bool mir_get_u64(MirReader *reader, uint64_t *out) {
  unsigned char bytes[8];
  if (!mir_get_bytes(reader, bytes, sizeof(bytes))) return false;
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; i++) value |= (uint64_t)bytes[i] << (i * 8);
  *out = value;
  return true;
}

static bool mir_get_ref(MirReader *reader, MirStringRef *out) {
  return mir_get_u64(reader, &out->offset) && mir_get_u64(reader, &out->len);
}

static bool mir_get_count(MirReader *reader, uint64_t *out) {
  return mir_get_u64(reader, out) && *out <= (uint64_t)SIZE_MAX;
}

static bool mir_string_ref_bounds(const MirHeader *header, MirStringRef ref, const unsigned char **start_out, size_t *len_out) {
  if (!header || ref.len == MIR_NULL_LEN) return false;
  if (ref.offset > (uint64_t)header->strings_len_size ||
      ref.len > (uint64_t)header->strings_len_size - ref.offset ||
      ref.len >= (uint64_t)header->strings_len_size - ref.offset ||
      ref.len > (uint64_t)SIZE_MAX) return false;
  const unsigned char *start = header->strings + (size_t)ref.offset;
  size_t len = (size_t)ref.len;
  if (start[len] != '\0') return false;
  if (memchr(start, 0, len) != NULL) return false;
  if (start_out) *start_out = start;
  if (len_out) *len_out = len;
  return true;
}

static bool mir_ref_string(const MirHeader *header, MirStringRef ref, char **out) {
  if (ref.len == MIR_NULL_LEN) {
    *out = NULL;
    return true;
  }
  const unsigned char *start = NULL;
  size_t len = 0;
  if (!mir_string_ref_bounds(header, ref, &start, &len)) return false;
  *out = z_strndup((const char *)start, len);
  return true;
}

static bool mir_ref_borrow_string(const MirHeader *header, MirStringRef ref, const char **out) {
  if (ref.len == MIR_NULL_LEN) {
    *out = NULL;
    return true;
  }
  const unsigned char *start = NULL;
  if (!mir_string_ref_bounds(header, ref, &start, NULL)) return false;
  *out = (const char *)start;
  return true;
}

static bool mir_header_counts_are_reasonable(const MirHeader *header) {
  return header->function_count <= MIR_BINARY_MAX_FUNCTION_COUNT &&
         header->local_count <= MIR_BINARY_MAX_LOCAL_COUNT &&
         header->instr_count <= MIR_BINARY_MAX_INSTR_COUNT &&
         header->value_count <= MIR_BINARY_MAX_VALUE_COUNT &&
         header->value_ref_count <= MIR_BINARY_MAX_REF_COUNT &&
         header->instr_ref_count <= MIR_BINARY_MAX_REF_COUNT &&
         header->external_count <= MIR_BINARY_MAX_EXTERNAL_COUNT &&
         header->external_param_type_count <= MIR_BINARY_MAX_EXTERNAL_PARAM_TYPE_COUNT &&
         header->data_segment_count <= MIR_BINARY_MAX_DATA_SEGMENT_COUNT &&
         header->data_bytes_len <= MIR_BINARY_MAX_DATA_BYTES &&
         header->strings_len <= MIR_BINARY_MAX_STRING_BYTES;
}

static bool mir_add_record_bytes(uint64_t *total, uint64_t count, uint64_t record_size) {
  if (count > 0 && record_size > UINT64_MAX / count) return false;
  uint64_t bytes = count * record_size;
  if (*total > UINT64_MAX - bytes) return false;
  *total += bytes;
  return true;
}

static bool mir_header_records_fit(const MirHeader *header, const MirReader *reader) {
  uint64_t record_bytes = 0;
  return mir_add_record_bytes(&record_bytes, header->function_count, MIR_FUNCTION_RECORD_BYTES) &&
         mir_add_record_bytes(&record_bytes, header->local_count, MIR_LOCAL_RECORD_BYTES) &&
         mir_add_record_bytes(&record_bytes, header->instr_count, MIR_INSTR_RECORD_BYTES) &&
         mir_add_record_bytes(&record_bytes, header->value_count, MIR_VALUE_RECORD_BYTES) &&
         mir_add_record_bytes(&record_bytes, header->value_ref_count, 8) &&
         mir_add_record_bytes(&record_bytes, header->instr_ref_count, 8) &&
         mir_add_record_bytes(&record_bytes, header->external_count, MIR_EXTERNAL_RECORD_BYTES) &&
         mir_add_record_bytes(&record_bytes, header->external_param_type_count, 4) &&
         mir_add_record_bytes(&record_bytes, header->data_segment_count, MIR_DATA_SEGMENT_RECORD_BYTES) &&
         mir_add_record_bytes(&record_bytes, header->data_bytes_len, 1) &&
         reader->cursor <= header->strings_offset &&
         record_bytes == (uint64_t)(header->strings_offset - reader->cursor);
}

static bool mir_read_header(MirReader *reader, MirHeader *header, size_t file_len) {
  unsigned char magic[sizeof(MIR_BINARY_MAGIC)];
  bool ok = mir_get_bytes(reader, magic, sizeof(magic)) &&
            memcmp(magic, MIR_BINARY_MAGIC, sizeof(magic)) == 0 &&
            mir_get_u32(reader, &header->schema) &&
            mir_get_u32(reader, &header->flags) &&
            mir_get_count(reader, &header->function_count) &&
            mir_get_count(reader, &header->local_count) &&
            mir_get_count(reader, &header->instr_count) &&
            mir_get_count(reader, &header->value_count) &&
            mir_get_count(reader, &header->value_ref_count) &&
            mir_get_count(reader, &header->instr_ref_count) &&
            mir_get_count(reader, &header->external_count) &&
            mir_get_count(reader, &header->external_param_type_count) &&
            mir_get_count(reader, &header->data_segment_count) &&
            mir_get_count(reader, &header->data_bytes_len) &&
            mir_get_count(reader, &header->strings_len) &&
            mir_get_ref(reader, &header->compiler_version_ref) &&
            mir_get_ref(reader, &header->graph_hash_ref) &&
            mir_get_ref(reader, &header->target_ref) &&
            mir_get_ref(reader, &header->emit_kind_ref) &&
            mir_get_ref(reader, &header->backend_ref) &&
            mir_get_count(reader, &header->readonly_data_bytes) &&
            mir_get_count(reader, &header->direct_function_count) &&
            mir_get_count(reader, &header->direct_export_count) &&
            mir_get_count(reader, &header->direct_stack_bytes) &&
            mir_get_count(reader, &header->direct_max_frame_bytes) &&
            mir_get_count(reader, &header->direct_readonly_data_bytes) &&
            mir_get_count(reader, &header->direct_allocator_helper_count) &&
            mir_get_count(reader, &header->direct_buffer_helper_count) &&
            mir_get_count(reader, &header->direct_runtime_helper_count) &&
            mir_get_count(reader, &header->direct_host_runtime_import_count) &&
            mir_get_count(reader, &header->direct_http_runtime_import_count) &&
            mir_get_count(reader, &header->direct_c_import_call_count) &&
            mir_get_count(reader, &header->direct_c_import_symbol_count) &&
            mir_get_i32(reader, &header->mir_line) &&
            mir_get_i32(reader, &header->mir_column);
  if (!ok || header->schema != MIR_BINARY_SCHEMA_VERSION || header->flags != 0 || header->strings_len > (uint64_t)file_len) return false;
  header->strings_offset = file_len - (size_t)header->strings_len;
  header->strings = reader->data + header->strings_offset;
  header->strings_len_size = (size_t)header->strings_len;
  reader->len = header->strings_offset;
  return header->strings_offset >= reader->cursor &&
         mir_header_counts_are_reasonable(header) &&
         mir_header_records_fit(header, reader);
}

static bool mir_refs_fit(uint64_t start, uint64_t len, uint64_t count) {
  return start <= count && len <= count - start;
}

static bool mir_read_function(MirReader *reader, const MirHeader *header, MirFunctionFlat *record) {
  MirStringRef name_ref = {0}, stable_ref = {0}, world_ref = {0};
  uint32_t return_type = 0, value_return_type = 0, return_element_type = 0, flags = 0;
  int32_t line = 0, column = 0;
  if (!mir_get_ref(reader, &name_ref) || !mir_get_ref(reader, &stable_ref) || !mir_get_ref(reader, &world_ref) ||
      !mir_get_u32(reader, &return_type) || !mir_get_u32(reader, &value_return_type) || !mir_get_u32(reader, &return_element_type) ||
      !mir_get_u32(reader, &flags) || !mir_get_count(reader, &record->local_start) || !mir_get_count(reader, &record->local_len) ||
      !mir_get_count(reader, &record->param_count) || !mir_get_count(reader, &record->instr_ref_start) || !mir_get_count(reader, &record->instr_ref_len) ||
      !mir_get_count(reader, &record->frame_bytes) || !mir_get_i32(reader, &line) || !mir_get_i32(reader, &column) ||
      (flags & ~3u) != 0 || !mir_refs_fit(record->instr_ref_start, record->instr_ref_len, header->instr_ref_count) ||
      !mir_refs_fit(record->local_start, record->local_len, header->local_count)) return false;
  record->return_type = (IrTypeKind)return_type;
  record->value_return_type = (IrTypeKind)value_return_type;
  record->return_element_type = (IrTypeKind)return_element_type;
  record->is_exported = (flags & 1u) != 0;
  record->raises = (flags & 2u) != 0;
  record->line = (int)line;
  record->column = (int)column;
  return mir_ref_borrow_string(header, name_ref, &record->name) &&
         mir_ref_borrow_string(header, stable_ref, &record->stable_id) &&
         mir_ref_borrow_string(header, world_ref, &record->world_param_name);
}

static bool mir_read_local(MirReader *reader, const MirHeader *header, MirLocalFlat *record) {
  MirStringRef name_ref = {0}, shape_ref = {0};
  uint32_t type = 0, element_type = 0, flags = 0;
  int32_t line = 0, column = 0;
  if (!mir_get_ref(reader, &name_ref) || !mir_get_ref(reader, &shape_ref) ||
      !mir_get_u32(reader, &type) || !mir_get_u32(reader, &element_type) ||
      !mir_get_u32(reader, &record->index) || !mir_get_u32(reader, &record->frame_offset) ||
      !mir_get_u32(reader, &record->array_len) || !mir_get_u32(reader, &record->field_offset) ||
      !mir_get_u32(reader, &record->byte_size) || !mir_get_u32(reader, &record->alignment) ||
      !mir_get_u32(reader, &record->ref_byte_size) ||
      !mir_get_u32(reader, &flags) || !mir_get_i32(reader, &line) || !mir_get_i32(reader, &column) ||
      (flags & ~31u) != 0) return false;
  record->type = (IrTypeKind)type;
  record->element_type = (IrTypeKind)element_type;
  record->is_param = (flags & (1u << 0)) != 0;
  record->is_array = (flags & (1u << 1)) != 0;
  record->is_record = (flags & (1u << 2)) != 0;
  record->is_mutable = (flags & (1u << 3)) != 0;
  record->is_record_ref = (flags & (1u << 4)) != 0;
  record->line = (int)line;
  record->column = (int)column;
  return mir_ref_borrow_string(header, name_ref, &record->name) &&
         mir_ref_borrow_string(header, shape_ref, &record->shape_name);
}

static bool mir_read_instr(MirReader *reader, const MirHeader *header, MirInstrFlat *record) {
  uint32_t kind = 0, reserved = 0;
  int32_t line = 0, column = 0;
  if (!mir_get_u32(reader, &kind) || !mir_get_u32(reader, &record->local_index) ||
      !mir_get_u32(reader, &record->array_index) || !mir_get_u32(reader, &record->field_offset) ||
      !mir_get_u32(reader, &record->error_code) || !mir_get_u32(reader, &reserved) ||
      !mir_get_count(reader, &record->value_ref) || !mir_get_count(reader, &record->index_ref) ||
      !mir_get_count(reader, &record->then_ref_start) || !mir_get_count(reader, &record->then_ref_len) ||
      !mir_get_count(reader, &record->else_ref_start) || !mir_get_count(reader, &record->else_ref_len) ||
      !mir_get_i32(reader, &line) || !mir_get_i32(reader, &column) || reserved != 0 ||
      kind > (uint32_t)IR_INSTR_KIND_LAST || record->value_ref > header->value_count ||
      record->index_ref > header->value_count ||
      !mir_refs_fit(record->then_ref_start, record->then_ref_len, header->instr_ref_count) ||
      !mir_refs_fit(record->else_ref_start, record->else_ref_len, header->instr_ref_count)) return false;
  record->kind = (IrInstrKind)kind;
  record->line = (int)line;
  record->column = (int)column;
  return true;
}

static bool mir_read_value(MirReader *reader, const MirHeader *header, MirValueFlat *record) {
  uint32_t kind = 0, type = 0, element_type = 0, external_call = 0, binary_op = 0, compare_op = 0;
  uint64_t int_value = 0;
  int32_t line = 0, column = 0;
  if (!mir_get_u32(reader, &kind) || !mir_get_u32(reader, &type) || !mir_get_u64(reader, &int_value) ||
      !mir_get_u32(reader, &record->local_index) || !mir_get_u32(reader, &record->callee_index) ||
      !mir_get_u32(reader, &record->array_index) || !mir_get_u32(reader, &record->field_offset) ||
      !mir_get_u32(reader, &record->data_offset) || !mir_get_u32(reader, &record->data_len) ||
      !mir_get_u32(reader, &element_type) || !mir_get_u32(reader, &record->error_code) ||
      !mir_get_u32(reader, &record->external_index) || !mir_get_u32(reader, &external_call) ||
      !mir_get_u32(reader, &binary_op) || !mir_get_u32(reader, &compare_op) ||
      !mir_get_count(reader, &record->arg_ref_start) || !mir_get_count(reader, &record->arg_ref_len) ||
      !mir_get_count(reader, &record->index_ref) || !mir_get_count(reader, &record->left_ref) ||
      !mir_get_count(reader, &record->right_ref) || !mir_get_i32(reader, &line) || !mir_get_i32(reader, &column) ||
      kind > (uint32_t)IR_VALUE_KIND_LAST || external_call > 1 || binary_op > (uint32_t)IR_BIN_OR ||
      compare_op > (uint32_t)IR_CMP_GE || record->index_ref > header->value_count ||
      record->left_ref > header->value_count || record->right_ref > header->value_count ||
      !mir_refs_fit(record->arg_ref_start, record->arg_ref_len, header->value_ref_count)) return false;
  record->kind = (IrValueKind)kind;
  record->type = (IrTypeKind)type;
  record->int_value = (unsigned long long)int_value;
  record->element_type = (IrTypeKind)element_type;
  record->external_call = external_call != 0;
  record->binary_op = (IrBinaryOp)binary_op;
  record->compare_op = (IrCompareOp)compare_op;
  record->line = (int)line;
  record->column = (int)column;
  return true;
}

static bool mir_read_external(MirReader *reader, const MirHeader *header, MirExternalFlat *record) {
  MirStringRef symbol_ref = {0}, header_ref = {0}, resolved_ref = {0};
  uint32_t return_type = 0, reserved = 0;
  if (!mir_get_ref(reader, &symbol_ref) || !mir_get_ref(reader, &header_ref) || !mir_get_ref(reader, &resolved_ref) ||
      !mir_get_u32(reader, &return_type) || !mir_get_u32(reader, &reserved) ||
      !mir_get_count(reader, &record->param_type_start) || !mir_get_count(reader, &record->param_type_len) ||
      reserved != 0 || !mir_refs_fit(record->param_type_start, record->param_type_len, header->external_param_type_count)) return false;
  record->return_type = (IrTypeKind)return_type;
  return mir_ref_borrow_string(header, symbol_ref, &record->symbol) &&
         mir_ref_borrow_string(header, header_ref, &record->import_header) &&
         mir_ref_borrow_string(header, resolved_ref, &record->import_resolved_header);
}

static bool mir_read_data_segment(MirReader *reader, const MirHeader *header, MirDataFlat *record) {
  return mir_get_u32(reader, &record->offset) &&
         mir_get_u32(reader, &record->len) &&
         mir_get_count(reader, &record->data_start) &&
         record->data_start <= header->data_bytes_len &&
         record->len <= header->data_bytes_len - record->data_start;
}

static void mir_decoded_free(MirDecoded *decoded) {
  if (!decoded) return;
  if (!decoded->header) {
    free(decoded->functions);
    free(decoded->locals);
    free(decoded->instrs);
    free(decoded->values);
    free(decoded->value_refs);
    free(decoded->instr_refs);
    free(decoded->externals);
    free(decoded->external_param_types);
    free(decoded->data_segments);
    free((void *)decoded->data_bytes);
    *decoded = (MirDecoded){0};
    return;
  }
  free(decoded->functions);
  free(decoded->locals);
  free(decoded->instrs);
  free(decoded->values);
  free(decoded->value_refs);
  free(decoded->instr_refs);
  free(decoded->externals);
  free(decoded->external_param_types);
  free(decoded->data_segments);
  *decoded = (MirDecoded){0};
}

static bool mir_decode(MirReader *reader, const MirHeader *header, MirDecoded *decoded) {
  decoded->header = header;
  if (header->function_count) decoded->functions = z_checked_calloc((size_t)header->function_count, sizeof(MirFunctionFlat));
  if (header->local_count) decoded->locals = z_checked_calloc((size_t)header->local_count, sizeof(MirLocalFlat));
  if (header->instr_count) decoded->instrs = z_checked_calloc((size_t)header->instr_count, sizeof(MirInstrFlat));
  if (header->value_count) decoded->values = z_checked_calloc((size_t)header->value_count, sizeof(MirValueFlat));
  if (header->value_ref_count) decoded->value_refs = z_checked_calloc((size_t)header->value_ref_count, sizeof(uint64_t));
  if (header->instr_ref_count) decoded->instr_refs = z_checked_calloc((size_t)header->instr_ref_count, sizeof(uint64_t));
  if (header->external_count) decoded->externals = z_checked_calloc((size_t)header->external_count, sizeof(MirExternalFlat));
  if (header->external_param_type_count) decoded->external_param_types = z_checked_calloc((size_t)header->external_param_type_count, sizeof(uint32_t));
  if (header->data_segment_count) decoded->data_segments = z_checked_calloc((size_t)header->data_segment_count, sizeof(MirDataFlat));
  for (size_t i = 0; i < (size_t)header->function_count; i++) if (!mir_read_function(reader, header, &decoded->functions[i])) return false;
  for (size_t i = 0; i < (size_t)header->local_count; i++) if (!mir_read_local(reader, header, &decoded->locals[i])) return false;
  for (size_t i = 0; i < (size_t)header->instr_count; i++) if (!mir_read_instr(reader, header, &decoded->instrs[i])) return false;
  for (size_t i = 0; i < (size_t)header->value_count; i++) if (!mir_read_value(reader, header, &decoded->values[i])) return false;
  for (size_t i = 0; i < (size_t)header->value_ref_count; i++) {
    if (!mir_get_count(reader, &decoded->value_refs[i]) || decoded->value_refs[i] > header->value_count) return false;
  }
  for (size_t i = 0; i < (size_t)header->instr_ref_count; i++) {
    if (!mir_get_count(reader, &decoded->instr_refs[i]) || decoded->instr_refs[i] == 0 || decoded->instr_refs[i] > header->instr_count) return false;
  }
  for (size_t i = 0; i < (size_t)header->external_count; i++) if (!mir_read_external(reader, header, &decoded->externals[i])) return false;
  for (size_t i = 0; i < (size_t)header->external_param_type_count; i++) {
    uint32_t type = 0;
    if (!mir_get_u32(reader, &type)) return false;
    decoded->external_param_types[i] = type;
  }
  for (size_t i = 0; i < (size_t)header->data_segment_count; i++) if (!mir_read_data_segment(reader, header, &decoded->data_segments[i])) return false;
  if (header->data_bytes_len) {
    if (reader->cursor > reader->len || header->data_bytes_len > (uint64_t)(reader->len - reader->cursor)) return false;
    decoded->data_bytes = reader->data + reader->cursor;
    reader->cursor += (size_t)header->data_bytes_len;
  }
  return reader->cursor == header->strings_offset;
}

static IrValue *mir_materialize_value(const MirDecoded *decoded, uint64_t index) {
  if (index == 0) return NULL;
  if (!decoded || index > decoded->header->value_count) return NULL;
  const MirValueFlat *record = &decoded->values[index - 1];
  IrValue *value = z_checked_calloc(1, sizeof(IrValue));
  value->kind = record->kind;
  value->type = record->type;
  value->int_value = record->int_value;
  value->local_index = record->local_index;
  value->callee_index = record->callee_index;
  value->array_index = record->array_index;
  value->field_offset = record->field_offset;
  value->data_offset = record->data_offset;
  value->data_len = record->data_len;
  value->element_type = record->element_type;
  value->error_code = record->error_code;
  value->external_index = record->external_index;
  value->external_call = record->external_call;
  value->binary_op = record->binary_op;
  value->compare_op = record->compare_op;
  value->line = record->line;
  value->column = record->column;
  value->arg_len = (size_t)record->arg_ref_len;
  value->arg_cap = value->arg_len;
  if (value->arg_len) value->args = z_checked_calloc(value->arg_len, sizeof(IrValue *));
  for (size_t i = 0; i < value->arg_len; i++) {
    value->args[i] = mir_materialize_value(decoded, decoded->value_refs[(size_t)record->arg_ref_start + i]);
  }
  value->index = mir_materialize_value(decoded, record->index_ref);
  value->left = mir_materialize_value(decoded, record->left_ref);
  value->right = mir_materialize_value(decoded, record->right_ref);
  return value;
}

static IrInstr mir_materialize_instr(const MirDecoded *decoded, uint64_t index);

static IrInstr *mir_materialize_instr_block(const MirDecoded *decoded, uint64_t ref_start, uint64_t ref_len) {
  if (ref_len == 0) return NULL;
  IrInstr *items = z_checked_calloc((size_t)ref_len, sizeof(IrInstr));
  for (size_t i = 0; i < (size_t)ref_len; i++) {
    items[i] = mir_materialize_instr(decoded, decoded->instr_refs[(size_t)ref_start + i]);
  }
  return items;
}

static IrInstr mir_materialize_instr(const MirDecoded *decoded, uint64_t index) {
  IrInstr out = {0};
  if (!decoded || index == 0 || index > decoded->header->instr_count) return out;
  const MirInstrFlat *record = &decoded->instrs[index - 1];
  out.kind = record->kind;
  out.local_index = record->local_index;
  out.array_index = record->array_index;
  out.field_offset = record->field_offset;
  out.error_code = record->error_code;
  out.value = mir_materialize_value(decoded, record->value_ref);
  out.index = mir_materialize_value(decoded, record->index_ref);
  out.then_len = (size_t)record->then_ref_len;
  out.then_cap = out.then_len;
  out.then_instrs = mir_materialize_instr_block(decoded, record->then_ref_start, record->then_ref_len);
  out.else_len = (size_t)record->else_ref_len;
  out.else_cap = out.else_len;
  out.else_instrs = mir_materialize_instr_block(decoded, record->else_ref_start, record->else_ref_len);
  out.line = record->line;
  out.column = record->column;
  return out;
}

static char *mir_materialize_required_string(const char *text, bool borrow_storage) {
  return borrow_storage ? (char *)(text ? text : "") : z_strdup(text ? text : "");
}

static char *mir_materialize_optional_string(const char *text, bool borrow_storage) {
  if (!text) return NULL;
  return borrow_storage ? (char *)text : z_strdup(text);
}

static bool mir_materialize_program(const MirDecoded *decoded, const char *path, const ZTargetInfo *target, bool borrow_storage, IrProgram *out) {
  if (!decoded || !out) return false;
  *out = (IrProgram){0};
  const MirHeader *header = decoded->header;
  out->target = target;
  out->mir_binary_storage_borrowed = borrow_storage;
  out->function_len = (size_t)header->function_count;
  out->function_cap = out->function_len;
  if (out->function_len) out->functions = z_checked_calloc(out->function_len, sizeof(IrFunction));
  for (size_t i = 0; i < out->function_len; i++) {
    const MirFunctionFlat *record = &decoded->functions[i];
    IrFunction *fun = &out->functions[i];
    fun->name = mir_materialize_required_string(record->name, borrow_storage);
    fun->stable_id = mir_materialize_required_string(record->stable_id, borrow_storage);
    fun->world_param_name = mir_materialize_optional_string(record->world_param_name, borrow_storage);
    fun->return_type = record->return_type;
    fun->value_return_type = record->value_return_type;
    fun->return_element_type = record->return_element_type;
    fun->local_len = (size_t)record->local_len;
    fun->local_cap = fun->local_len;
    fun->param_count = (size_t)record->param_count;
    fun->instr_len = (size_t)record->instr_ref_len;
    fun->instr_cap = fun->instr_len;
    fun->frame_bytes = (size_t)record->frame_bytes;
    fun->is_exported = record->is_exported;
    fun->raises = record->raises;
    fun->line = record->line;
    fun->column = record->column;
    if (fun->local_len) fun->locals = z_checked_calloc(fun->local_len, sizeof(IrLocal));
    for (size_t local_index = 0; local_index < fun->local_len; local_index++) {
      const MirLocalFlat *local_record = &decoded->locals[(size_t)record->local_start + local_index];
      IrLocal *local = &fun->locals[local_index];
      local->name = mir_materialize_required_string(local_record->name, borrow_storage);
      local->shape_name = mir_materialize_optional_string(local_record->shape_name, borrow_storage);
      local->type = local_record->type;
      local->element_type = local_record->element_type;
      local->index = local_record->index;
      local->frame_offset = local_record->frame_offset;
      local->array_len = local_record->array_len;
      local->field_offset = local_record->field_offset;
      local->byte_size = local_record->byte_size;
      local->alignment = local_record->alignment;
      local->is_param = local_record->is_param;
      local->is_array = local_record->is_array;
      local->is_record = local_record->is_record;
      local->is_record_ref = local_record->is_record_ref;
      local->is_mutable = local_record->is_mutable;
      local->ref_byte_size = local_record->ref_byte_size;
      local->line = local_record->line;
      local->column = local_record->column;
    }
    fun->instrs = mir_materialize_instr_block(decoded, record->instr_ref_start, record->instr_ref_len);
  }
  out->external_function_len = (size_t)header->external_count;
  out->external_function_cap = out->external_function_len;
  if (out->external_function_len) out->external_functions = z_checked_calloc(out->external_function_len, sizeof(IrExternalFunction));
  for (size_t i = 0; i < out->external_function_len; i++) {
    const MirExternalFlat *record = &decoded->externals[i];
    IrExternalFunction *external = &out->external_functions[i];
    external->symbol = mir_materialize_required_string(record->symbol, borrow_storage);
    external->import_header = mir_materialize_optional_string(record->import_header, borrow_storage);
    external->import_resolved_header = mir_materialize_optional_string(record->import_resolved_header, borrow_storage);
    external->return_type = record->return_type;
    external->param_len = (size_t)record->param_type_len;
    external->param_cap = external->param_len;
    if (external->param_len) external->param_types = z_checked_calloc(external->param_len, sizeof(IrTypeKind));
    for (size_t param_index = 0; param_index < external->param_len; param_index++) {
      external->param_types[param_index] = (IrTypeKind)decoded->external_param_types[(size_t)record->param_type_start + param_index];
    }
  }
  out->data_segment_len = (size_t)header->data_segment_count;
  out->data_segment_cap = out->data_segment_len;
  if (out->data_segment_len) out->data_segments = z_checked_calloc(out->data_segment_len, sizeof(IrDataSegment));
  for (size_t i = 0; i < out->data_segment_len; i++) {
    const MirDataFlat *record = &decoded->data_segments[i];
    IrDataSegment *segment = &out->data_segments[i];
    segment->offset = record->offset;
    segment->len = record->len;
    if (segment->len) {
      if (borrow_storage) {
        segment->bytes = (unsigned char *)(decoded->data_bytes + record->data_start);
      } else {
        segment->bytes = z_checked_calloc(segment->len, sizeof(unsigned char));
        memcpy(segment->bytes, decoded->data_bytes + record->data_start, segment->len);
      }
    }
  }
  out->readonly_data_bytes = (size_t)header->readonly_data_bytes;
  out->mir_valid = true;
  out->mir_path = z_strdup(path ? path : "");
  out->mir_line = (int)header->mir_line;
  out->mir_column = (int)header->mir_column;
  out->mir_bytes = sizeof(IrProgram) + (size_t)header->function_count * sizeof(IrFunction) +
                  (size_t)header->local_count * sizeof(IrLocal) +
                  (size_t)header->instr_count * sizeof(IrInstr) +
                  (size_t)header->value_count * sizeof(IrValue) +
                  (size_t)header->data_bytes_len;
  out->direct_function_count = (size_t)header->direct_function_count;
  out->direct_export_count = (size_t)header->direct_export_count;
  out->direct_stack_bytes = (size_t)header->direct_stack_bytes;
  out->direct_max_frame_bytes = (size_t)header->direct_max_frame_bytes;
  out->direct_readonly_data_bytes = (size_t)header->direct_readonly_data_bytes;
  out->direct_allocator_helper_count = (size_t)header->direct_allocator_helper_count;
  out->direct_buffer_helper_count = (size_t)header->direct_buffer_helper_count;
  out->direct_runtime_helper_count = (size_t)header->direct_runtime_helper_count;
  out->direct_host_runtime_import_count = (size_t)header->direct_host_runtime_import_count;
  out->direct_http_runtime_import_count = (size_t)header->direct_http_runtime_import_count;
  out->direct_c_import_call_count = (size_t)header->direct_c_import_call_count;
  out->direct_c_import_symbol_count = (size_t)header->direct_c_import_symbol_count;
  return true;
}

static bool mir_map_file(const char *path, MirMappedFile *mapped, ZDiag *diag) {
  *mapped = (MirMappedFile){0};
#if !defined(_WIN32)
  mapped->fd = -1;
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    mir_diag(diag, path, "failed to open mapped MIR cache", strerror(errno));
    return false;
  }
  struct stat st;
  if (fstat(fd, &st) != 0) {
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    mir_diag(diag, path, "failed to stat mapped MIR cache", strerror(errno));
    return false;
  }
  if (!S_ISREG(st.st_mode)) {
    close(fd);
    mir_diag(diag, path, "failed to stat mapped MIR cache", "MIR cache is not a regular file");
    return false;
  }
  if (st.st_size <= 0 || (uintmax_t)st.st_size > (uintmax_t)SIZE_MAX) {
    close(fd);
    mir_diag(diag, path, "failed to stat mapped MIR cache", st.st_size <= 0 ? "empty MIR cache" : "MIR cache is too large");
    return false;
  }
  void *data = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (data == MAP_FAILED) {
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    mir_diag(diag, path, "failed to mmap MIR cache", strerror(errno));
    return false;
  }
  mapped->data = (const unsigned char *)data;
  mapped->len = (size_t)st.st_size;
  mapped->mapped = true;
  mapped->fd = fd;
  return true;
#else
  unsigned char *data = NULL;
  size_t len = 0;
  ZDiag read_diag = {0};
  if (!z_read_binary_file(path, &data, &len, &read_diag)) {
    mir_diag(diag, path, "failed to read mapped MIR cache", strerror(errno));
    return false;
  }
  if (len == 0) {
    free(data);
    mir_diag(diag, path, "failed to stat mapped MIR cache", "empty MIR cache");
    return false;
  }
  mapped->data = data;
  mapped->len = len;
  mapped->mapped = false;
  return true;
#endif
}

static void mir_unmap_file(MirMappedFile *mapped) {
  if (!mapped || !mapped->data) return;
#if !defined(_WIN32)
  if (mapped->mapped) munmap((void *)mapped->data, mapped->len);
  if (mapped->fd >= 0) close(mapped->fd);
#else
  free((void *)mapped->data);
#endif
  *mapped = (MirMappedFile){0};
}

static bool mir_validate_identity(const MirHeader *header, const char *expected_graph_hash, const ZTargetInfo *target, const char *emit_kind, const char *backend, ZDiag *diag, const char *path) {
  char *compiler_version = NULL;
  char *graph_hash = NULL;
  char *target_name = NULL;
  char *emit_name = NULL;
  char *backend_name = NULL;
  bool ok = mir_ref_string(header, header->compiler_version_ref, &compiler_version) &&
            mir_ref_string(header, header->graph_hash_ref, &graph_hash) &&
            mir_ref_string(header, header->target_ref, &target_name) &&
            mir_ref_string(header, header->emit_kind_ref, &emit_name) &&
            mir_ref_string(header, header->backend_ref, &backend_name) &&
            mir_text_eq(compiler_version, ZERO_VERSION) &&
            mir_text_eq(graph_hash, expected_graph_hash) &&
            mir_text_eq(target_name, target && target->name ? target->name : "") &&
            mir_text_eq(emit_name, emit_kind ? emit_kind : "") &&
            mir_text_eq(backend_name, backend ? backend : "");
  if (!ok) mir_diag(diag, path, "mapped MIR cache identity does not match compiler input", "stale MIR cache");
  free(compiler_version);
  free(graph_hash);
  free(target_name);
  free(emit_name);
  free(backend_name);
  return ok;
}

bool z_mir_binary_load_path(const char *path, const char *expected_graph_hash, const ZTargetInfo *target, const char *emit_kind, const char *backend, IrProgram *out, ZMirBinaryCacheFacts *facts, ZDiag *diag) {
  if (facts) {
    *facts = (ZMirBinaryCacheFacts){0};
    snprintf(facts->path, sizeof(facts->path), "%s", path ? path : "");
  }
  if (!path || !expected_graph_hash || !expected_graph_hash[0] || !out) return false;
  MirMappedFile mapped;
  if (!mir_map_file(path, &mapped, diag)) return false;
  MirReader reader = {.data = mapped.data, .len = mapped.len, .cursor = 0};
  MirHeader header = {0};
  MirDecoded decoded = {0};
  bool ok = mir_read_header(&reader, &header, mapped.len) &&
            mir_validate_identity(&header, expected_graph_hash, target, emit_kind, backend, diag, path) &&
            mir_decode(&reader, &header, &decoded) &&
            mir_materialize_program(&decoded, path, target, true, out);
  if (!ok && diag && diag->code == 0) mir_diag(diag, path, "invalid mapped MIR cache", "invalid MIR binary");
  if (ok) {
    out->mir_binary_storage = mapped.data;
    out->mir_binary_storage_len = mapped.len;
    out->mir_binary_storage_mapped = mapped.mapped;
    out->mir_binary_storage_borrowed = true;
#if !defined(_WIN32)
    out->mir_binary_storage_fd = mapped.fd;
    mapped.fd = -1;
#else
    out->mir_binary_storage_fd = -1;
#endif
    mapped.data = NULL;
    mapped.len = 0;
    mapped.mapped = false;
  }
  if (facts) {
    facts->hit = ok;
    facts->mapped = ok ? out->mir_binary_storage_mapped : mapped.mapped;
    facts->borrowed_storage = ok && out->mir_binary_storage_borrowed;
    facts->byte_len = ok ? out->mir_binary_storage_len : mapped.len;
  }
  mir_decoded_free(&decoded);
  mir_unmap_file(&mapped);
  return ok;
}

static char *mir_dirname(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  if (!slash) return z_strdup(".");
  if (slash == path) return z_strdup("/");
  return z_strndup(path, (size_t)(slash - path));
}

static char *mir_join_path(const char *left, const char *right) {
  ZBuf path;
  zbuf_init(&path);
  zbuf_append(&path, left && left[0] ? left : ".");
  if (path.len > 0 && path.data[path.len - 1] != '/' && path.data[path.len - 1] != '\\') zbuf_append_char(&path, '/');
  zbuf_append(&path, right ? right : "");
  return path.data;
}

static char *mir_graph_store_cache_root(const char *store_path) {
  const char *cache_override = getenv("ZERO_CACHE_DIR");
  if (cache_override && cache_override[0]) return z_strdup(cache_override);
  const char *home = getenv("HOME");
  if (!home || !home[0]) home = getenv("USERPROFILE");
  if (home && home[0]) return mir_join_path(home, ".zero/cache/native");
  char *root = mir_dirname(store_path ? store_path : "zero.graph");
  char *cache_root = mir_join_path(root, ".zero/cache/native");
  free(root);
  return cache_root;
}

static uint64_t mir_hash_text(uint64_t hash, const char *text) {
  const unsigned char *bytes = (const unsigned char *)(text ? text : "");
  while (*bytes) {
    hash ^= (uint64_t)*bytes++;
    hash *= 1099511628211ull;
  }
  return hash;
}

char *z_mir_binary_cache_path_for_graph_store(const char *store_path, const char *graph_hash, const ZTargetInfo *target, const char *emit_kind, const char *backend) {
  char *root = mir_graph_store_cache_root(store_path);
  uint64_t hash = 1469598103934665603ull;
  hash = mir_hash_text(hash, ZERO_VERSION);
  hash = mir_hash_text(hash, graph_hash ? graph_hash : "");
  char stdlib_fingerprint[32];
  snprintf(stdlib_fingerprint, sizeof(stdlib_fingerprint), "%016llx", (unsigned long long)z_std_source_graph_fingerprint());
  hash = mir_hash_text(hash, stdlib_fingerprint);
  hash = mir_hash_text(hash, target && target->name ? target->name : "");
  hash = mir_hash_text(hash, emit_kind ? emit_kind : "");
  hash = mir_hash_text(hash, backend ? backend : "");
  ZBuf path; zbuf_init(&path); zbuf_append(&path, root);
  if (path.len > 0 && path.data[path.len - 1] != '/') zbuf_append_char(&path, '/');
  zbuf_appendf(&path, "mir-%016llx.zmir", (unsigned long long)hash);
  free(root);
  return path.data;
}
