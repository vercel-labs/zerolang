#include "zero.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool wasm_diag(ZDiag *diag, const char *message, int line, int column, const char *actual) {
  diag->code = 4004;
  diag->line = line > 0 ? line : 1;
  diag->column = column > 0 ? column : 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message);
  snprintf(diag->expected, sizeof(diag->expected), "direct wasm MVP subset");
  if (actual) snprintf(diag->actual, sizeof(diag->actual), "%s", actual);
  snprintf(diag->help, sizeof(diag->help), "restrict this program to direct-wasm-supported primitive arithmetic functions or choose a native direct target");
  return false;
}

static bool wasm_ir_diag(ZDiag *diag, const IrProgram *ir) {
  diag->code = 4004;
  diag->line = ir && ir->mir_line > 0 ? ir->mir_line : 1;
  diag->column = ir && ir->mir_column > 0 ? ir->mir_column : 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", ir && ir->mir_message[0] ? ir->mir_message : "direct backend lowering failed");
  snprintf(diag->expected, sizeof(diag->expected), "%s", ir && ir->mir_expected[0] ? ir->mir_expected : "direct wasm MVP subset");
  snprintf(diag->actual, sizeof(diag->actual), "%s", ir && ir->mir_actual[0] ? ir->mir_actual : "unsupported construct");
  snprintf(diag->help, sizeof(diag->help), "%s", ir && ir->mir_help[0] ? ir->mir_help : "restrict this program to direct-wasm-supported features");
  if (ir) z_diag_set_backend_blocker(diag, &ir->backend_blocker);
  return false;
}

static void wasm_append_byte(ZBuf *buf, unsigned char byte) {
  zbuf_append_char(buf, (char)byte);
}

static void wasm_append_data(ZBuf *buf, const unsigned char *data, size_t len) {
  for (size_t i = 0; i < len; i++) wasm_append_byte(buf, data[i]);
}

static void wasm_append_u32_leb(ZBuf *buf, uint32_t value) {
  do {
    unsigned char byte = (unsigned char)(value & 0x7f);
    value >>= 7;
    if (value != 0) byte |= 0x80;
    wasm_append_byte(buf, byte);
  } while (value != 0);
}

static void wasm_append_i32_leb(ZBuf *buf, int32_t value) {
  bool more = true;
  while (more) {
    unsigned char byte = (unsigned char)(value & 0x7f);
    value >>= 7;
    bool sign_bit_set = (byte & 0x40) != 0;
    more = !((value == 0 && !sign_bit_set) || (value == -1 && sign_bit_set));
    if (more) byte |= 0x80;
    wasm_append_byte(buf, byte);
  }
}

static void wasm_append_i64_leb(ZBuf *buf, int64_t value) {
  bool more = true;
  while (more) {
    unsigned char byte = (unsigned char)(value & 0x7f);
    value >>= 7;
    bool sign_bit_set = (byte & 0x40) != 0;
    more = !((value == 0 && !sign_bit_set) || (value == -1 && sign_bit_set));
    if (more) byte |= 0x80;
    wasm_append_byte(buf, byte);
  }
}

static void wasm_append_name(ZBuf *buf, const char *name) {
  size_t len = strlen(name ? name : "");
  wasm_append_u32_leb(buf, (uint32_t)len);
  for (size_t i = 0; i < len; i++) wasm_append_byte(buf, (unsigned char)name[i]);
}

static void wasm_append_section(ZBuf *module, unsigned char id, const ZBuf *payload) {
  wasm_append_byte(module, id);
  wasm_append_u32_leb(module, (uint32_t)payload->len);
  wasm_append_data(module, (const unsigned char *)payload->data, payload->len);
}

static void wasm_emit_i32_const(ZBuf *code, int32_t value) {
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, value);
}

static void wasm_emit_i64_const(ZBuf *code, int64_t value) {
  wasm_append_byte(code, 0x42);
  wasm_append_i64_leb(code, value);
}

static void wasm_emit_local_get(ZBuf *code, unsigned local_index) {
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, local_index);
}

static void wasm_emit_local_set(ZBuf *code, unsigned local_index) {
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, local_index);
}

static void wasm_emit_local_tee(ZBuf *code, unsigned local_index) {
  wasm_append_byte(code, 0x22);
  wasm_append_u32_leb(code, local_index);
}

static void wasm_emit_call_index(ZBuf *code, unsigned function_index) {
  wasm_append_byte(code, 0x10);
  wasm_append_u32_leb(code, function_index);
}

static void wasm_emit_drop(ZBuf *code) {
  wasm_append_byte(code, 0x1a);
}

static void wasm_emit_i32_load(ZBuf *code, unsigned align_log2, unsigned offset) {
  wasm_append_byte(code, 0x28);
  wasm_append_u32_leb(code, align_log2);
  wasm_append_u32_leb(code, offset);
}

static void wasm_emit_i64_load(ZBuf *code, unsigned align_log2, unsigned offset) {
  wasm_append_byte(code, 0x29);
  wasm_append_u32_leb(code, align_log2);
  wasm_append_u32_leb(code, offset);
}

static void wasm_emit_i32_store(ZBuf *code, unsigned align_log2, unsigned offset) {
  wasm_append_byte(code, 0x36);
  wasm_append_u32_leb(code, align_log2);
  wasm_append_u32_leb(code, offset);
}

static void wasm_emit_i32_store8(ZBuf *code, unsigned offset) {
  wasm_append_byte(code, 0x3a);
  wasm_append_u32_leb(code, 0);
  wasm_append_u32_leb(code, offset);
}

static void wasm_emit_maybe_byte_view_clear(ZBuf *code, unsigned maybe_base) {
  wasm_emit_i32_const(code, 0);
  wasm_emit_local_set(code, maybe_base);
  wasm_emit_i32_const(code, 0);
  wasm_emit_local_set(code, maybe_base + 1);
  wasm_emit_i32_const(code, 0);
  wasm_emit_local_set(code, maybe_base + 2);
}

static void wasm_emit_maybe_scalar_clear(ZBuf *code, unsigned maybe_base) {
  wasm_emit_i32_const(code, 0);
  wasm_emit_local_set(code, maybe_base);
  wasm_emit_i32_const(code, 0);
  wasm_emit_local_set(code, maybe_base + 1);
}

static bool wasm_value_type(IrTypeKind type, unsigned char *out) {
  if (type == IR_TYPE_BOOL) {
    *out = 0x7f;
    return true;
  }
  if (type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_I32 || type == IR_TYPE_U32) {
    *out = 0x7f;
    return true;
  }
  if (type == IR_TYPE_I64 || type == IR_TYPE_U64) {
    *out = 0x7e;
    return true;
  }
  return false;
}

static unsigned wasm_ir_local_width(const IrLocal *local) {
  if (!local || local->is_array || local->is_record) return 0;
  if (local->type == IR_TYPE_BYTE_VIEW) return 2;
  if (local->type == IR_TYPE_ALLOC || local->type == IR_TYPE_VEC || local->type == IR_TYPE_MAYBE_BYTE_VIEW) return 3;
  if (local->type == IR_TYPE_MAYBE_SCALAR) return 2;
  return 1;
}

static bool wasm_is_i64_type(IrTypeKind type) {
  return type == IR_TYPE_I64 || type == IR_TYPE_U64;
}

static bool wasm_is_unsigned_type(IrTypeKind type) {
  return type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_U32 || type == IR_TYPE_U64;
}

static bool wasm_function_propagates_to_process_exit(const IrFunction *fun) {
  return fun && (fun->raises ||
                 (fun->is_exported &&
                  fun->name && strcmp(fun->name, "main") == 0 &&
                  fun->return_type == IR_TYPE_I32 &&
                  fun->value_return_type == IR_TYPE_VOID));
}

typedef struct {
  const IrProgram *ir;
  const IrFunction *fun;
  bool has_arrays;
  bool has_byte_views;
  bool has_allocator;
  bool has_fallibility;
  unsigned imported_function_count;
  unsigned zero_json_parse_bytes_import_index;
  unsigned fd_write_import_index;
  unsigned fd_read_import_index;
  unsigned fd_close_import_index;
  unsigned path_open_import_index;
  unsigned args_sizes_get_import_index;
  unsigned args_get_import_index;
  unsigned environ_sizes_get_import_index;
  unsigned environ_get_import_index;
  unsigned clock_time_get_import_index;
  unsigned random_get_import_index;
  unsigned fd_filestat_get_import_index;
  unsigned fd_readdir_import_index;
  unsigned path_create_directory_import_index;
  unsigned path_remove_directory_import_index;
  unsigned path_unlink_file_import_index;
  unsigned path_rename_import_index;
  bool has_wasi_args;
  bool has_wasi_env;
  bool has_wasi_fs;
  unsigned frame_local_index;
  unsigned index_local_index;
  unsigned byte_index_local_index;
  unsigned byte_cmp_left_ptr_local_index;
  unsigned byte_cmp_right_ptr_local_index;
  unsigned byte_cmp_len_local_index;
  unsigned byte_cmp_i_local_index;
  unsigned byte_cmp_result_local_index;
  unsigned byte_mut_len_local_index;
  unsigned byte_mut_i_local_index;
  unsigned json_result_local_index;
  unsigned alloc_len_local_index;
  unsigned alloc_next_local_index;
  unsigned error_result_local_index;
  unsigned args_index_local_index;
  unsigned args_ptr_local_index;
  unsigned args_len_local_index;
  unsigned fs_fd_local_index;
  unsigned fs_result_local_index;
  unsigned fs_aux_local_index;
  unsigned fs_len_local_index;
  unsigned fs_i_local_index;
} WasmEmitContext;

typedef struct {
  bool zero_json_parse_bytes;
  bool fd_write;
  bool fd_read;
  bool fd_close;
  bool path_open;
  bool args_sizes_get;
  bool args_get;
  bool environ_sizes_get;
  bool environ_get;
  bool clock_time_get;
  bool random_get;
  bool fd_filestat_get;
  bool fd_readdir;
  bool path_create_directory;
  bool path_remove_directory;
  bool path_unlink_file;
  bool path_rename;
  unsigned function_count;
  unsigned type_count;
  unsigned zero_json_parse_bytes_function_index;
  unsigned fd_write_function_index;
  unsigned fd_read_function_index;
  unsigned fd_close_function_index;
  unsigned path_open_function_index;
  unsigned args_sizes_get_function_index;
  unsigned args_get_function_index;
  unsigned environ_sizes_get_function_index;
  unsigned environ_get_function_index;
  unsigned clock_time_get_function_index;
  unsigned random_get_function_index;
  unsigned fd_filestat_get_function_index;
  unsigned fd_readdir_function_index;
  unsigned path_create_directory_function_index;
  unsigned path_remove_directory_function_index;
  unsigned path_unlink_file_function_index;
  unsigned path_rename_function_index;
  unsigned zero_json_parse_bytes_type_index;
  unsigned fd_write_type_index;
  unsigned fd_read_type_index;
  unsigned fd_close_type_index;
  unsigned path_open_type_index;
  unsigned args_sizes_get_type_index;
  unsigned args_get_type_index;
  unsigned environ_sizes_get_type_index;
  unsigned environ_get_type_index;
  unsigned clock_time_get_type_index;
  unsigned random_get_type_index;
  unsigned fd_filestat_get_type_index;
  unsigned fd_readdir_type_index;
  unsigned path_create_directory_type_index;
  unsigned path_remove_directory_type_index;
  unsigned path_unlink_file_type_index;
  unsigned path_rename_type_index;
} WasmImportPlan;

static bool wasm_function_has_arrays(const IrFunction *fun) {
  for (size_t i = 0; i < fun->local_len; i++) {
    if (fun->locals[i].is_array || fun->locals[i].is_record) return true;
  }
  return false;
}

static unsigned wasm_local_index(const IrFunction *fun, unsigned ir_index) {
  unsigned wasm_index = 0;
  for (size_t i = 0; i < fun->local_len && i < ir_index; i++) {
    wasm_index += wasm_ir_local_width(&fun->locals[i]);
  }
  return wasm_index;
}

static unsigned wasm_scalar_local_count(const IrFunction *fun) {
  unsigned count = 0;
  for (size_t i = 0; i < fun->local_len; i++) {
    count += wasm_ir_local_width(&fun->locals[i]);
  }
  return count;
}

static unsigned wasm_array_base_offset(const IrLocal *local) {
  return local->frame_offset - local->byte_size;
}

static unsigned wasm_local_memory_offset(const IrLocal *local) {
  return local->frame_offset - local->byte_size;
}

static unsigned wasm_type_align_log2(IrTypeKind type) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) return 0;
  if (type == IR_TYPE_U16) return 1;
  return 2;
}

static unsigned wasm_type_byte_size(IrTypeKind type) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) return 1;
  if (type == IR_TYPE_U16) return 2;
  return 4;
}

static bool wasm_emit_value(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag);
static void wasm_emit_frame_restore(ZBuf *body, const WasmEmitContext *ctx);

static bool wasm_value_uses_byte_view(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_STRING_LITERAL ||
      value->kind == IR_VALUE_ARRAY_BYTE_VIEW ||
      value->kind == IR_VALUE_BYTE_SLICE ||
      value->kind == IR_VALUE_BYTE_VIEW_LEN ||
      value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD ||
      value->kind == IR_VALUE_CRC32_BYTES ||
      value->kind == IR_VALUE_JSON_PARSE_BYTES ||
      value->kind == IR_VALUE_JSON_VALIDATE_BYTES ||
      value->kind == IR_VALUE_JSON_STREAM_TOKENS_BYTES ||
      value->kind == IR_VALUE_FIXED_BUF_ALLOC ||
      value->kind == IR_VALUE_MAYBE_VALUE) {
    return true;
  }
  if (wasm_value_uses_byte_view(value->index) ||
      wasm_value_uses_byte_view(value->left) ||
      wasm_value_uses_byte_view(value->right)) {
    return true;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_byte_view(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_json_parse_bytes(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_JSON_PARSE_BYTES ||
      value->kind == IR_VALUE_JSON_VALIDATE_BYTES ||
      value->kind == IR_VALUE_JSON_STREAM_TOKENS_BYTES) {
    return true;
  }
  if (wasm_value_uses_json_parse_bytes(value->index) ||
      wasm_value_uses_json_parse_bytes(value->left) ||
      wasm_value_uses_json_parse_bytes(value->right)) {
    return true;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_json_parse_bytes(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_byte_compare(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_BYTE_VIEW_EQ || value->kind == IR_VALUE_CRC32_BYTES) return true;
  if (wasm_value_uses_byte_compare(value->index) ||
      wasm_value_uses_byte_compare(value->left) ||
      wasm_value_uses_byte_compare(value->right)) {
    return true;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_byte_compare(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_byte_mutation(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_BYTE_COPY || value->kind == IR_VALUE_BYTE_FILL) return true;
  if (wasm_value_uses_byte_mutation(value->index) ||
      wasm_value_uses_byte_mutation(value->left) ||
      wasm_value_uses_byte_mutation(value->right)) {
    return true;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_byte_mutation(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_memory_peek(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_MEMORY_PEEK_U8 || value->kind == IR_VALUE_MEMORY_POKE_U8) return true;
  if (wasm_value_uses_memory_peek(value->index) ||
      wasm_value_uses_memory_peek(value->left) ||
      wasm_value_uses_memory_peek(value->right)) {
    return true;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_memory_peek(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_allocator(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_FIXED_BUF_ALLOC || value->kind == IR_VALUE_ALLOC_BYTES ||
      value->kind == IR_VALUE_JSON_PARSE_BYTES ||
      value->kind == IR_VALUE_MAYBE_HAS || value->kind == IR_VALUE_MAYBE_VALUE ||
      value->kind == IR_VALUE_ARGS_GET || value->kind == IR_VALUE_ENV_GET ||
      value->kind == IR_VALUE_FS_READ_ALL) {
    return true;
  }
  if (wasm_value_uses_allocator(value->index) ||
      wasm_value_uses_allocator(value->left) ||
      wasm_value_uses_allocator(value->right)) {
    return true;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_allocator(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_fallibility(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_CHECK || value->kind == IR_VALUE_RESCUE) return true;
  if (wasm_value_uses_fallibility(value->index) ||
      wasm_value_uses_fallibility(value->left) ||
      wasm_value_uses_fallibility(value->right)) {
    return true;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_fallibility(value->args[i])) return true;
  }
  return false;
}

static bool wasm_instrs_use_fallibility(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (instr->kind == IR_INSTR_RAISE ||
        wasm_value_uses_fallibility(instr->value) ||
        wasm_value_uses_fallibility(instr->index) ||
        wasm_instrs_use_fallibility(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_fallibility(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

static bool wasm_instrs_use_allocator(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_allocator(instr->value) ||
        wasm_value_uses_allocator(instr->index) ||
        wasm_instrs_use_allocator(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_allocator(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

static bool wasm_instrs_use_byte_mutation(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_byte_mutation(instr->value) ||
        wasm_value_uses_byte_mutation(instr->index) ||
        wasm_instrs_use_byte_mutation(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_byte_mutation(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

static bool wasm_instrs_use_memory_peek(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_memory_peek(instr->value) ||
        wasm_value_uses_memory_peek(instr->index) ||
        wasm_instrs_use_memory_peek(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_memory_peek(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

static bool wasm_value_uses_wasi_clock(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_TIME_WALL_SECONDS || value->kind == IR_VALUE_TIME_MONOTONIC) return true;
  if (wasm_value_uses_wasi_clock(value->index) ||
      wasm_value_uses_wasi_clock(value->left) ||
      wasm_value_uses_wasi_clock(value->right)) {
    return true;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_wasi_clock(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_wasi_random(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_RAND_ENTROPY_U32) return true;
  if (wasm_value_uses_wasi_random(value->index) ||
      wasm_value_uses_wasi_random(value->left) ||
      wasm_value_uses_wasi_random(value->right)) {
    return true;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_wasi_random(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_wasi_args(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_ARGS_LEN || value->kind == IR_VALUE_ARGS_GET) return true;
  if (wasm_value_uses_wasi_args(value->index) ||
      wasm_value_uses_wasi_args(value->left) ||
      wasm_value_uses_wasi_args(value->right)) {
    return true;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_wasi_args(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_wasi_env(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_ENV_GET) return true;
  if (wasm_value_uses_wasi_env(value->index) ||
      wasm_value_uses_wasi_env(value->left) ||
      wasm_value_uses_wasi_env(value->right)) {
    return true;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_wasi_env(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_wasi_fs(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_FS_OPEN ||
      value->kind == IR_VALUE_FS_CREATE ||
      value->kind == IR_VALUE_FS_READ_PATH ||
      value->kind == IR_VALUE_FS_WRITE_PATH ||
      value->kind == IR_VALUE_FS_READ_BYTES_PATH ||
      value->kind == IR_VALUE_FS_WRITE_BYTES_PATH ||
      value->kind == IR_VALUE_FS_READ_ALL ||
      value->kind == IR_VALUE_FS_READ_FILE ||
      value->kind == IR_VALUE_FS_WRITE_ALL_FILE ||
      value->kind == IR_VALUE_FS_CLOSE_FILE ||
      value->kind == IR_VALUE_FS_EXISTS ||
      value->kind == IR_VALUE_FS_REMOVE ||
      value->kind == IR_VALUE_FS_RENAME ||
      value->kind == IR_VALUE_FS_FILE_LEN ||
      value->kind == IR_VALUE_FS_MAKE_DIR ||
      value->kind == IR_VALUE_FS_REMOVE_DIR ||
      value->kind == IR_VALUE_FS_IS_DIR ||
      value->kind == IR_VALUE_FS_DIR_ENTRY_COUNT ||
      value->kind == IR_VALUE_FS_TEMP_NAME ||
      value->kind == IR_VALUE_FS_ATOMIC_WRITE) {
    return true;
  }
  if (wasm_value_uses_wasi_fs(value->index) ||
      wasm_value_uses_wasi_fs(value->left) ||
      wasm_value_uses_wasi_fs(value->right)) {
    return true;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_wasi_fs(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_wasi_fd_filestat(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_FS_FILE_LEN) return true;
  if (value->kind == IR_VALUE_FS_READ_ALL && value->type == IR_TYPE_I64) return true;
  if (wasm_value_uses_wasi_fd_filestat(value->index) ||
      wasm_value_uses_wasi_fd_filestat(value->left) ||
      wasm_value_uses_wasi_fd_filestat(value->right)) return true;
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_wasi_fd_filestat(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_wasi_fd_readdir(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_FS_DIR_ENTRY_COUNT) return true;
  if (wasm_value_uses_wasi_fd_readdir(value->index) ||
      wasm_value_uses_wasi_fd_readdir(value->left) ||
      wasm_value_uses_wasi_fd_readdir(value->right)) return true;
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_wasi_fd_readdir(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_wasi_path_create_directory(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_FS_MAKE_DIR) return true;
  if (wasm_value_uses_wasi_path_create_directory(value->index) ||
      wasm_value_uses_wasi_path_create_directory(value->left) ||
      wasm_value_uses_wasi_path_create_directory(value->right)) return true;
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_wasi_path_create_directory(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_wasi_path_remove_directory(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_FS_REMOVE_DIR) return true;
  if (wasm_value_uses_wasi_path_remove_directory(value->index) ||
      wasm_value_uses_wasi_path_remove_directory(value->left) ||
      wasm_value_uses_wasi_path_remove_directory(value->right)) return true;
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_wasi_path_remove_directory(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_wasi_path_unlink_file(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_FS_REMOVE) return true;
  if (wasm_value_uses_wasi_path_unlink_file(value->index) ||
      wasm_value_uses_wasi_path_unlink_file(value->left) ||
      wasm_value_uses_wasi_path_unlink_file(value->right)) return true;
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_wasi_path_unlink_file(value->args[i])) return true;
  }
  return false;
}

static bool wasm_value_uses_wasi_path_rename(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_FS_RENAME || value->kind == IR_VALUE_FS_ATOMIC_WRITE) return true;
  if (wasm_value_uses_wasi_path_rename(value->index) ||
      wasm_value_uses_wasi_path_rename(value->left) ||
      wasm_value_uses_wasi_path_rename(value->right)) return true;
  for (size_t i = 0; i < value->arg_len; i++) {
    if (wasm_value_uses_wasi_path_rename(value->args[i])) return true;
  }
  return false;
}

static bool wasm_instrs_use_byte_compare(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_byte_compare(instr->value) ||
        wasm_value_uses_byte_compare(instr->index) ||
        wasm_instrs_use_byte_compare(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_byte_compare(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

static bool wasm_instrs_use_json_parse_bytes(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_json_parse_bytes(instr->value) ||
        wasm_value_uses_json_parse_bytes(instr->index) ||
        wasm_instrs_use_json_parse_bytes(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_json_parse_bytes(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

static bool wasm_instrs_use_wasi_clock(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_wasi_clock(instr->value) ||
        wasm_value_uses_wasi_clock(instr->index) ||
        wasm_instrs_use_wasi_clock(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_wasi_clock(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

static bool wasm_instrs_use_wasi_random(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_wasi_random(instr->value) ||
        wasm_value_uses_wasi_random(instr->index) ||
        wasm_instrs_use_wasi_random(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_wasi_random(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

static bool wasm_instrs_use_wasi_args(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_wasi_args(instr->value) ||
        wasm_value_uses_wasi_args(instr->index) ||
        wasm_instrs_use_wasi_args(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_wasi_args(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

static bool wasm_instrs_use_wasi_env(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_wasi_env(instr->value) ||
        wasm_value_uses_wasi_env(instr->index) ||
        wasm_instrs_use_wasi_env(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_wasi_env(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

static bool wasm_instrs_use_wasi_fs(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_wasi_fs(instr->value) ||
        wasm_value_uses_wasi_fs(instr->index) ||
        wasm_instrs_use_wasi_fs(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_wasi_fs(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

static bool wasm_instrs_use_wasi_fd_filestat(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_wasi_fd_filestat(instr->value) ||
        wasm_value_uses_wasi_fd_filestat(instr->index) ||
        wasm_instrs_use_wasi_fd_filestat(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_wasi_fd_filestat(instr->else_instrs, instr->else_len)) return true;
  }
  return false;
}

static bool wasm_instrs_use_wasi_fd_readdir(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_wasi_fd_readdir(instr->value) ||
        wasm_value_uses_wasi_fd_readdir(instr->index) ||
        wasm_instrs_use_wasi_fd_readdir(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_wasi_fd_readdir(instr->else_instrs, instr->else_len)) return true;
  }
  return false;
}

static bool wasm_instrs_use_wasi_path_create_directory(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_wasi_path_create_directory(instr->value) ||
        wasm_value_uses_wasi_path_create_directory(instr->index) ||
        wasm_instrs_use_wasi_path_create_directory(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_wasi_path_create_directory(instr->else_instrs, instr->else_len)) return true;
  }
  return false;
}

static bool wasm_instrs_use_wasi_path_remove_directory(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_wasi_path_remove_directory(instr->value) ||
        wasm_value_uses_wasi_path_remove_directory(instr->index) ||
        wasm_instrs_use_wasi_path_remove_directory(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_wasi_path_remove_directory(instr->else_instrs, instr->else_len)) return true;
  }
  return false;
}

static bool wasm_instrs_use_wasi_path_unlink_file(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_wasi_path_unlink_file(instr->value) ||
        wasm_value_uses_wasi_path_unlink_file(instr->index) ||
        wasm_instrs_use_wasi_path_unlink_file(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_wasi_path_unlink_file(instr->else_instrs, instr->else_len)) return true;
  }
  return false;
}

static bool wasm_instrs_use_wasi_path_rename(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_wasi_path_rename(instr->value) ||
        wasm_value_uses_wasi_path_rename(instr->index) ||
        wasm_instrs_use_wasi_path_rename(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_wasi_path_rename(instr->else_instrs, instr->else_len)) return true;
  }
  return false;
}

static bool wasm_instrs_use_byte_views(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (wasm_value_uses_byte_view(instr->value) ||
        wasm_value_uses_byte_view(instr->index) ||
        wasm_instrs_use_byte_views(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_byte_views(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

static bool wasm_instrs_use_world_write(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (instr->kind == IR_INSTR_WORLD_WRITE ||
        wasm_instrs_use_world_write(instr->then_instrs, instr->then_len) ||
        wasm_instrs_use_world_write(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

static bool wasm_function_uses_byte_views(const IrFunction *fun) {
  return fun && wasm_instrs_use_byte_views(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_byte_compare(const IrFunction *fun) {
  return fun && wasm_instrs_use_byte_compare(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_byte_mutation(const IrFunction *fun) {
  return fun && wasm_instrs_use_byte_mutation(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_json_parse_bytes(const IrFunction *fun) {
  return fun && wasm_instrs_use_json_parse_bytes(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_memory_peek(const IrFunction *fun) {
  return fun && wasm_instrs_use_memory_peek(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_wasi_clock(const IrFunction *fun) {
  return fun && wasm_instrs_use_wasi_clock(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_wasi_random(const IrFunction *fun) {
  return fun && wasm_instrs_use_wasi_random(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_wasi_args(const IrFunction *fun) {
  return fun && wasm_instrs_use_wasi_args(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_wasi_env(const IrFunction *fun) {
  return fun && wasm_instrs_use_wasi_env(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_wasi_fs(const IrFunction *fun) {
  return fun && wasm_instrs_use_wasi_fs(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_wasi_fd_filestat(const IrFunction *fun) {
  return fun && wasm_instrs_use_wasi_fd_filestat(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_wasi_fd_readdir(const IrFunction *fun) {
  return fun && wasm_instrs_use_wasi_fd_readdir(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_wasi_path_create_directory(const IrFunction *fun) {
  return fun && wasm_instrs_use_wasi_path_create_directory(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_wasi_path_remove_directory(const IrFunction *fun) {
  return fun && wasm_instrs_use_wasi_path_remove_directory(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_wasi_path_unlink_file(const IrFunction *fun) {
  return fun && wasm_instrs_use_wasi_path_unlink_file(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_wasi_path_rename(const IrFunction *fun) {
  return fun && wasm_instrs_use_wasi_path_rename(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_allocator(const IrFunction *fun) {
  return fun && wasm_instrs_use_allocator(fun->instrs, fun->instr_len);
}

static bool wasm_function_uses_fallibility(const IrFunction *fun) {
  return fun && (fun->raises || wasm_instrs_use_fallibility(fun->instrs, fun->instr_len));
}

static bool wasm_function_uses_world_write(const IrFunction *fun) {
  return fun && wasm_instrs_use_world_write(fun->instrs, fun->instr_len);
}

static void wasm_emit_trap_if_top_true(ZBuf *code) {
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x00);
  wasm_append_byte(code, 0x0b);
}

static bool wasm_emit_bounds_checked_address(ZBuf *code, const IrLocal *local, const IrValue *index, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || !ctx->has_arrays) return wasm_diag(diag, "direct wasm array access requires stack memory context", index ? index->line : 1, index ? index->column : 1, "missing stack context");
  if (!wasm_emit_value(code, index, ctx, diag)) return false;
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->index_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->index_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, (int32_t)local->array_len);
  wasm_append_byte(code, 0x4f);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x00);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->frame_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, (int32_t)wasm_array_base_offset(local));
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->index_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, (int32_t)wasm_type_byte_size(local->element_type));
  wasm_append_byte(code, 0x6c);
  wasm_append_byte(code, 0x6a);
  return true;
}

static bool wasm_emit_byte_view_len(ZBuf *code, const IrValue *view, const WasmEmitContext *ctx, ZDiag *diag);
static bool wasm_emit_byte_view_ptr(ZBuf *code, const IrValue *view, const WasmEmitContext *ctx, ZDiag *diag);

static bool wasm_emit_byte_view_start(ZBuf *code, const IrValue *view, const WasmEmitContext *ctx, ZDiag *diag) {
  if (view && view->index) return wasm_emit_value(code, view->index, ctx, diag);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  return true;
}

static bool wasm_emit_byte_view_end(ZBuf *code, const IrValue *view, const WasmEmitContext *ctx, ZDiag *diag) {
  if (view && view->right) return wasm_emit_value(code, view->right, ctx, diag);
  return wasm_emit_byte_view_len(code, view ? view->left : NULL, ctx, diag);
}

static bool wasm_emit_byte_slice_bounds(ZBuf *code, const IrValue *view, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!view || view->kind != IR_VALUE_BYTE_SLICE) return wasm_diag(diag, "direct wasm slice bounds require a byte slice view", view ? view->line : 1, view ? view->column : 1, "non-slice view");
  if (!wasm_emit_byte_view_end(code, view, ctx, diag) ||
      !wasm_emit_byte_view_len(code, view->left, ctx, diag)) {
    return false;
  }
  wasm_append_byte(code, 0x4b);
  wasm_emit_trap_if_top_true(code);

  if (!wasm_emit_byte_view_start(code, view, ctx, diag) ||
      !wasm_emit_byte_view_end(code, view, ctx, diag)) {
    return false;
  }
  wasm_append_byte(code, 0x4b);
  wasm_emit_trap_if_top_true(code);
  return true;
}

static bool wasm_emit_byte_view_len(ZBuf *code, const IrValue *view, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!view) return wasm_diag(diag, "direct wasm byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_LOCAL) {
    if (!ctx || view->local_index >= ctx->fun->local_len) {
      return wasm_diag(diag, "direct wasm byte-view local is invalid", view->line, view->column, "invalid byte-view local");
    }
    wasm_append_byte(code, 0x20);
    wasm_append_u32_leb(code, wasm_local_index(ctx->fun, view->local_index) + 1);
    return true;
  }
  if (view->kind == IR_VALUE_MAYBE_VALUE) {
    if (!ctx || view->local_index >= ctx->fun->local_len) {
      return wasm_diag(diag, "direct wasm maybe byte-view local is invalid", view->line, view->column, "invalid maybe byte-view local");
    }
    wasm_append_byte(code, 0x20);
    wasm_append_u32_leb(code, wasm_local_index(ctx->fun, view->local_index) + 2);
    return true;
  }
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    wasm_append_byte(code, 0x41);
    wasm_append_i32_leb(code, (int32_t)view->data_len);
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL) {
    wasm_append_byte(code, 0x41);
    wasm_append_i32_leb(code, (int32_t)view->data_len);
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    if (!wasm_emit_byte_slice_bounds(code, view, ctx, diag) ||
        !wasm_emit_byte_view_end(code, view, ctx, diag) ||
        !wasm_emit_byte_view_start(code, view, ctx, diag)) {
      return false;
    }
    wasm_append_byte(code, 0x6b);
    return true;
  }
  return wasm_diag(diag, "direct wasm value is not a byte view", view->line, view->column, "non-byte-view value");
}

static bool wasm_emit_byte_view_ptr(ZBuf *code, const IrValue *view, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!view) return wasm_diag(diag, "direct wasm byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_LOCAL) {
    if (!ctx || view->local_index >= ctx->fun->local_len) {
      return wasm_diag(diag, "direct wasm byte-view local is invalid", view->line, view->column, "invalid byte-view local");
    }
    wasm_append_byte(code, 0x20);
    wasm_append_u32_leb(code, wasm_local_index(ctx->fun, view->local_index));
    return true;
  }
  if (view->kind == IR_VALUE_MAYBE_VALUE) {
    if (!ctx || view->local_index >= ctx->fun->local_len) {
      return wasm_diag(diag, "direct wasm maybe byte-view local is invalid", view->line, view->column, "invalid maybe byte-view local");
    }
    wasm_append_byte(code, 0x20);
    wasm_append_u32_leb(code, wasm_local_index(ctx->fun, view->local_index) + 1);
    return true;
  }
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (!ctx || view->array_index >= ctx->fun->local_len) return wasm_diag(diag, "direct wasm byte-view array is invalid", view->line, view->column, "invalid array byte view");
    const IrLocal *local = &ctx->fun->locals[view->array_index];
    if (!local->is_array || local->element_type != IR_TYPE_U8) return wasm_diag(diag, "direct wasm byte-view array requires [N]u8", view->line, view->column, "non-u8 array view");
    wasm_append_byte(code, 0x20);
    wasm_append_u32_leb(code, ctx->frame_local_index);
    wasm_append_byte(code, 0x41);
    wasm_append_i32_leb(code, (int32_t)wasm_array_base_offset(local));
    wasm_append_byte(code, 0x6a);
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL) {
    wasm_append_byte(code, 0x41);
    wasm_append_i32_leb(code, (int32_t)view->data_offset);
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    if (!wasm_emit_byte_slice_bounds(code, view, ctx, diag) ||
        !wasm_emit_byte_view_ptr(code, view->left, ctx, diag) ||
        !wasm_emit_byte_view_start(code, view, ctx, diag)) {
      return false;
    }
    wasm_append_byte(code, 0x6a);
    return true;
  }
  return wasm_diag(diag, "direct wasm value is not a byte view", view->line, view->column, "non-byte-view value");
}

static bool wasm_emit_json_parse_bytes_call(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->zero_json_parse_bytes_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm JSON bytes helper requires a runtime import", value ? value->line : 1, value ? value->column : 1, "missing zero_json_parse_bytes import");
  }
  if (!value || !value->left) return wasm_diag(diag, "direct wasm JSON bytes helper requires a byte span", value ? value->line : 1, value ? value->column : 1, "missing byte span");
  if (!wasm_emit_byte_view_ptr(code, value->left, ctx, diag)) return false;
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_emit_call_index(code, ctx->zero_json_parse_bytes_import_index);
  return true;
}

static bool wasm_emit_byte_view_eq(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx) return wasm_diag(diag, "direct wasm byte comparison requires a memory context", value ? value->line : 1, value ? value->column : 1, "missing byte comparison context");
  if (!wasm_emit_byte_view_ptr(code, value->left, ctx, diag)) return false;
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_cmp_left_ptr_local_index);
  if (!wasm_emit_byte_view_ptr(code, value->right, ctx, diag)) return false;
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_cmp_right_ptr_local_index);
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_cmp_len_local_index);

  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_cmp_result_local_index);

  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_cmp_len_local_index);
  if (!wasm_emit_byte_view_len(code, value->right, ctx, diag)) return false;
  wasm_append_byte(code, 0x46);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x40);

  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_cmp_result_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_cmp_i_local_index);

  wasm_append_byte(code, 0x02);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x03);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_cmp_i_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_cmp_len_local_index);
  wasm_append_byte(code, 0x4f);
  wasm_append_byte(code, 0x0d);
  wasm_append_u32_leb(code, 1);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_cmp_result_local_index);
  wasm_append_byte(code, 0x45);
  wasm_append_byte(code, 0x0d);
  wasm_append_u32_leb(code, 1);

  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_cmp_left_ptr_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_cmp_i_local_index);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x2d);
  wasm_append_u32_leb(code, 0);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_cmp_right_ptr_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_cmp_i_local_index);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x2d);
  wasm_append_u32_leb(code, 0);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_cmp_result_local_index);
  wasm_append_byte(code, 0x0b);

  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_cmp_i_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_cmp_i_local_index);
  wasm_append_byte(code, 0x0c);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);

  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_cmp_result_local_index);
  return true;
}

static bool wasm_emit_byte_copy(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_mut_len_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_mut_len_local_index);
  if (!wasm_emit_byte_view_len(code, value->right, ctx, diag)) return false;
  wasm_append_byte(code, 0x4b);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x40);
  if (!wasm_emit_byte_view_len(code, value->right, ctx, diag)) return false;
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_mut_len_local_index);
  wasm_append_byte(code, 0x0b);

  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_mut_i_local_index);
  wasm_append_byte(code, 0x02);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x03);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_mut_i_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_mut_len_local_index);
  wasm_append_byte(code, 0x4f);
  wasm_append_byte(code, 0x0d);
  wasm_append_u32_leb(code, 1);

  if (!wasm_emit_byte_view_ptr(code, value->right, ctx, diag)) return false;
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_mut_i_local_index);
  wasm_append_byte(code, 0x6a);
  if (!wasm_emit_byte_view_ptr(code, value->left, ctx, diag)) return false;
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_mut_i_local_index);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x2d);
  wasm_append_u32_leb(code, 0);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x3a);
  wasm_append_u32_leb(code, 0);
  wasm_append_u32_leb(code, 0);

  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_mut_i_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_mut_i_local_index);
  wasm_append_byte(code, 0x0c);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);

  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_mut_len_local_index);
  return true;
}

static bool wasm_emit_byte_fill(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!wasm_emit_byte_view_len(code, value->right, ctx, diag)) return false;
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_mut_len_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_mut_i_local_index);
  wasm_append_byte(code, 0x02);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x03);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_mut_i_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_mut_len_local_index);
  wasm_append_byte(code, 0x4f);
  wasm_append_byte(code, 0x0d);
  wasm_append_u32_leb(code, 1);

  if (!wasm_emit_byte_view_ptr(code, value->right, ctx, diag)) return false;
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_mut_i_local_index);
  wasm_append_byte(code, 0x6a);
  if (!wasm_emit_value(code, value->left, ctx, diag)) return false;
  wasm_append_byte(code, 0x3a);
  wasm_append_u32_leb(code, 0);
  wasm_append_u32_leb(code, 0);

  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_mut_i_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->byte_mut_i_local_index);
  wasm_append_byte(code, 0x0c);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);

  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->byte_mut_len_local_index);
  return true;
}

static void wasm_emit_error_condition_from_result_local(ZBuf *code, unsigned result_local_index) {
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, result_local_index);
  wasm_append_byte(code, 0x42);
  wasm_append_i64_leb(code, 32);
  wasm_append_byte(code, 0x88);
  wasm_append_byte(code, 0xa7);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x47);
}

static bool wasm_emit_pack_success_value(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!value || value->type == IR_TYPE_VOID) {
    wasm_append_byte(code, 0x42);
    wasm_append_i64_leb(code, 0);
    return true;
  }
  if (wasm_is_i64_type(value->type)) return wasm_diag(diag, "direct wasm fallible returns currently support only 32-bit-or-smaller values", value->line, value->column, "i32-compatible value");
  if (!wasm_emit_value(code, value, ctx, diag)) return false;
  wasm_append_byte(code, 0xad);
  return true;
}

static void wasm_emit_unpacked_success_value(ZBuf *code, unsigned result_local_index) {
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, result_local_index);
  wasm_append_byte(code, 0xa7);
}

static void wasm_emit_packed_error(ZBuf *code, unsigned code_value) {
  wasm_append_byte(code, 0x42);
  wasm_append_i64_leb(code, ((int64_t)(code_value ? code_value : IR_ERROR_UNKNOWN)) << 32);
}

static void wasm_emit_i64_success_from_i32_stack(ZBuf *code) {
  wasm_append_byte(code, 0xad);
}

static bool wasm_emit_check_value(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || !wasm_function_propagates_to_process_exit(ctx->fun) || !ctx->has_fallibility) return wasm_diag(diag, "direct wasm check requires a fallible function context", value ? value->line : 1, value ? value->column : 1, "non-fallible context");
  if (!value || !value->left || value->left->type != IR_TYPE_I64) return wasm_diag(diag, "direct wasm check requires a packed fallible call result", value ? value->line : 1, value ? value->column : 1, "non-fallible value");
  if (!wasm_emit_value(code, value->left, ctx, diag)) return false;
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->error_result_local_index);
  wasm_emit_error_condition_from_result_local(code, ctx->error_result_local_index);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x40);
  if (ctx->fun->raises) {
    wasm_append_byte(code, 0x20);
    wasm_append_u32_leb(code, ctx->error_result_local_index);
  } else {
    wasm_append_byte(code, 0x41);
    wasm_append_i32_leb(code, 1);
  }
  wasm_emit_frame_restore(code, ctx);
  wasm_append_byte(code, 0x0f);
  wasm_append_byte(code, 0x0b);
  if (value->type == IR_TYPE_VOID) return true;
  wasm_emit_unpacked_success_value(code, ctx->error_result_local_index);
  return true;
}

static bool wasm_emit_rescue_value(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || !ctx->has_fallibility) return wasm_diag(diag, "direct wasm rescue requires fallibility scratch locals", value ? value->line : 1, value ? value->column : 1, "missing fallibility context");
  if (!value || !value->left || !value->right || value->left->type != IR_TYPE_I64) return wasm_diag(diag, "direct wasm rescue requires a packed fallible call and fallback", value ? value->line : 1, value ? value->column : 1, "unsupported rescue");
  unsigned char value_type = 0;
  if (!wasm_value_type(value->type, &value_type) || wasm_is_i64_type(value->type) || value->type == IR_TYPE_VOID) {
    return wasm_diag(diag, "direct wasm rescue currently supports only 32-bit-or-smaller primitive values", value->line, value->column, "i32-compatible value");
  }
  if (!wasm_emit_value(code, value->left, ctx, diag)) return false;
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->error_result_local_index);
  wasm_emit_error_condition_from_result_local(code, ctx->error_result_local_index);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, value_type);
  if (!wasm_emit_value(code, value->right, ctx, diag)) return false;
  wasm_append_byte(code, 0x05);
  wasm_emit_unpacked_success_value(code, ctx->error_result_local_index);
  wasm_append_byte(code, 0x0b);
  return true;
}

static bool wasm_emit_wasi_clock_time(ZBuf *code, unsigned clock_id, bool seconds, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->clock_time_get_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm time requires WASI clock_time_get import", value ? value->line : 1, value ? value->column : 1, "missing clock import");
  }
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, (int32_t)clock_id);
  wasm_append_byte(code, 0x42);
  wasm_append_i64_leb(code, 0);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 32);
  wasm_append_byte(code, 0x10);
  wasm_append_u32_leb(code, ctx->clock_time_get_import_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_emit_trap_if_top_true(code);

  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 32);
  wasm_append_byte(code, 0x29);
  wasm_append_u32_leb(code, 3);
  wasm_append_u32_leb(code, 0);
  if (seconds) {
    wasm_append_byte(code, 0x42);
    wasm_append_i64_leb(code, 1000000000ll);
    wasm_append_byte(code, 0x80);
  }
  return true;
}

static bool wasm_emit_wasi_random_u32(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->random_get_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm entropy requires WASI random_get import", value ? value->line : 1, value ? value->column : 1, "missing random import");
  }
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 40);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 4);
  wasm_append_byte(code, 0x10);
  wasm_append_u32_leb(code, ctx->random_get_import_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_emit_trap_if_top_true(code);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 40);
  wasm_append_byte(code, 0x28);
  wasm_append_u32_leb(code, 2);
  wasm_append_u32_leb(code, 0);
  return true;
}

static bool wasm_emit_wasi_args_len(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->args_sizes_get_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm args length requires WASI args_sizes_get import", value ? value->line : 1, value ? value->column : 1, "missing args_sizes_get import");
  }
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 80);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 84);
  wasm_append_byte(code, 0x10);
  wasm_append_u32_leb(code, ctx->args_sizes_get_import_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_emit_trap_if_top_true(code);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 80);
  wasm_append_byte(code, 0x28);
  wasm_append_u32_leb(code, 2);
  wasm_append_u32_leb(code, 0);
  return true;
}

static bool wasm_emit_wasi_args_get_to_local(ZBuf *code, const IrValue *value, unsigned maybe_base, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->args_sizes_get_import_index == (unsigned)-1 || ctx->args_get_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm std.args.get requires WASI args imports", value ? value->line : 1, value ? value->column : 1, "missing args imports");
  }
  if (!value || !value->left) return wasm_diag(diag, "direct wasm std.args.get requires an index", value ? value->line : 1, value ? value->column : 1, "missing index");
  if (!wasm_emit_value(code, value->left, ctx, diag)) return false;
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_index_local_index);

  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 80);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 84);
  wasm_append_byte(code, 0x10);
  wasm_append_u32_leb(code, ctx->args_sizes_get_import_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_emit_trap_if_top_true(code);

  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_index_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 80);
  wasm_append_byte(code, 0x28);
  wasm_append_u32_leb(code, 2);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x4f);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, maybe_base);
  wasm_append_byte(code, 0x05);

  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 88);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 512);
  wasm_append_byte(code, 0x10);
  wasm_append_u32_leb(code, ctx->args_get_import_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_emit_trap_if_top_true(code);

  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 88);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_index_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 4);
  wasm_append_byte(code, 0x6c);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x28);
  wasm_append_u32_leb(code, 2);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_len_local_index);

  wasm_append_byte(code, 0x02);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x03);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x2d);
  wasm_append_u32_leb(code, 0);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x45);
  wasm_append_byte(code, 0x0d);
  wasm_append_u32_leb(code, 1);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x0c);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);

  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, maybe_base);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, maybe_base + 1);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, maybe_base + 2);
  wasm_append_byte(code, 0x0b);
  return true;
}

static bool wasm_emit_wasi_env_get_to_local(ZBuf *code, const IrValue *value, unsigned maybe_base, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->environ_sizes_get_import_index == (unsigned)-1 || ctx->environ_get_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm std.env.get requires WASI environ imports", value ? value->line : 1, value ? value->column : 1, "missing environ imports");
  }
  if (!value || !value->left) return wasm_diag(diag, "direct wasm std.env.get requires a key", value ? value->line : 1, value ? value->column : 1, "missing key");

  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 80);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 84);
  wasm_append_byte(code, 0x10);
  wasm_append_u32_leb(code, ctx->environ_sizes_get_import_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_emit_trap_if_top_true(code);

  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 128);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 4096);
  wasm_append_byte(code, 0x10);
  wasm_append_u32_leb(code, ctx->environ_get_import_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_emit_trap_if_top_true(code);

  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, maybe_base);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_index_local_index);

  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 128);
  wasm_append_byte(code, 0x28);
  wasm_append_u32_leb(code, 2);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x02);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x03);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x2d);
  wasm_append_u32_leb(code, 0);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 61);
  wasm_append_byte(code, 0x46);
  wasm_append_byte(code, 0x0d);
  wasm_append_u32_leb(code, 1);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x0c);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_append_byte(code, 0x46);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x02);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x03);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x2d);
  wasm_append_u32_leb(code, 0);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x45);
  wasm_append_byte(code, 0x0d);
  wasm_append_u32_leb(code, 1);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x0c);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, maybe_base);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, maybe_base + 1);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, maybe_base + 2);
  wasm_append_byte(code, 0x0b);

  wasm_append_byte(code, 0x02);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x03);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_index_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 80);
  wasm_append_byte(code, 0x28);
  wasm_append_u32_leb(code, 2);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x4f);
  wasm_append_byte(code, 0x0d);
  wasm_append_u32_leb(code, 1);

  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 128);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_index_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 4);
  wasm_append_byte(code, 0x6c);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x28);
  wasm_append_u32_leb(code, 2);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_len_local_index);

  wasm_append_byte(code, 0x02);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x03);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_append_byte(code, 0x4f);
  wasm_append_byte(code, 0x0d);
  wasm_append_u32_leb(code, 1);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x2d);
  wasm_append_u32_leb(code, 0);
  wasm_append_u32_leb(code, 0);
  if (!wasm_emit_byte_view_ptr(code, value->left, ctx, diag)) return false;
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x2d);
  wasm_append_u32_leb(code, 0);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x0d);
  wasm_append_u32_leb(code, 1);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x0c);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);

  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_append_byte(code, 0x46);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x2d);
  wasm_append_u32_leb(code, 0);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 61);
  wasm_append_byte(code, 0x46);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x40);

  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_len_local_index);

  wasm_append_byte(code, 0x02);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x03);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x2d);
  wasm_append_u32_leb(code, 0);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x45);
  wasm_append_byte(code, 0x0d);
  wasm_append_u32_leb(code, 1);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x0c);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);

  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, maybe_base);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_ptr_local_index);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, maybe_base + 1);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_len_local_index);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, maybe_base + 2);
  wasm_append_byte(code, 0x0c);
  wasm_append_u32_leb(code, 3);

  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);

  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->args_index_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->args_index_local_index);
  wasm_append_byte(code, 0x0c);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);

  return true;
}

static bool wasm_emit_wasi_path_open(ZBuf *code, const IrValue *path, bool write, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->path_open_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm filesystem requires WASI path_open import", path ? path->line : 1, path ? path->column : 1, "missing path_open import");
  }
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 3);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  if (!wasm_emit_byte_view_ptr(code, path, ctx, diag)) return false;
  if (!wasm_emit_byte_view_len(code, path, ctx, diag)) return false;
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, write ? 9 : 0);
  wasm_append_byte(code, 0x42);
  wasm_append_i64_leb(code, write ? (64ll | 2097152ll) : (2ll | 2097152ll));
  wasm_append_byte(code, 0x42);
  wasm_append_i64_leb(code, 0);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 64);
  wasm_append_byte(code, 0x10);
  wasm_append_u32_leb(code, ctx->path_open_import_index);
  return true;
}

static bool wasm_emit_wasi_path_open_mode(ZBuf *code, const IrValue *path, unsigned oflags, int64_t rights, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->path_open_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm filesystem requires WASI path_open import", path ? path->line : 1, path ? path->column : 1, "missing path_open import");
  }
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 3);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  if (!wasm_emit_byte_view_ptr(code, path, ctx, diag)) return false;
  if (!wasm_emit_byte_view_len(code, path, ctx, diag)) return false;
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, (int32_t)oflags);
  wasm_append_byte(code, 0x42);
  wasm_append_i64_leb(code, rights);
  wasm_append_byte(code, 0x42);
  wasm_append_i64_leb(code, 0);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 64);
  wasm_append_byte(code, 0x10);
  wasm_append_u32_leb(code, ctx->path_open_import_index);
  return true;
}

static void wasm_emit_load_opened_fd(ZBuf *code) {
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 64);
  wasm_append_byte(code, 0x28);
  wasm_append_u32_leb(code, 2);
  wasm_append_u32_leb(code, 0);
}

static bool wasm_emit_wasi_fs_close_fd(ZBuf *code, const WasmEmitContext *ctx, const IrValue *value, ZDiag *diag) {
  if (!ctx || ctx->fd_close_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm filesystem requires WASI fd_close import", value ? value->line : 1, value ? value->column : 1, "missing fd_close import");
  }
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->fs_fd_local_index);
  wasm_append_byte(code, 0x10);
  wasm_append_u32_leb(code, ctx->fd_close_import_index);
  wasm_append_byte(code, 0x1a);
  return true;
}

static bool wasm_emit_wasi_fs_close_local_fd(ZBuf *code, unsigned fd_local_index, const WasmEmitContext *ctx, const IrValue *value, ZDiag *diag) {
  if (!ctx || ctx->fd_close_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm filesystem requires WASI fd_close import", value ? value->line : 1, value ? value->column : 1, "missing fd_close import");
  }
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, fd_local_index);
  wasm_append_byte(code, 0x10);
  wasm_append_u32_leb(code, ctx->fd_close_import_index);
  return true;
}

static bool wasm_emit_wasi_fs_read_path(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag, bool maybe_result) {
  if (!ctx || ctx->fd_read_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm filesystem requires WASI fd_read import", value ? value->line : 1, value ? value->column : 1, "missing fd_read import");
  }
  if (!wasm_emit_wasi_path_open(code, value->left, false, ctx, diag)) return false;
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x7f);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, maybe_result ? -1 : 0);
  wasm_append_byte(code, 0x05);

  wasm_emit_load_opened_fd(code);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->fs_fd_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 48);
  if (!wasm_emit_byte_view_ptr(code, value->right, ctx, diag)) return false;
  wasm_append_byte(code, 0x36);
  wasm_append_u32_leb(code, 2);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 52);
  if (!wasm_emit_byte_view_len(code, value->right, ctx, diag)) return false;
  wasm_append_byte(code, 0x36);
  wasm_append_u32_leb(code, 2);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->fs_fd_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 48);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 68);
  wasm_append_byte(code, 0x10);
  wasm_append_u32_leb(code, ctx->fd_read_import_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x7f);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, maybe_result ? -1 : 0);
  wasm_append_byte(code, 0x05);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 68);
  wasm_append_byte(code, 0x28);
  wasm_append_u32_leb(code, 2);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->fs_result_local_index);
  if (!wasm_emit_wasi_fs_close_fd(code, ctx, value, diag)) return false;
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->fs_result_local_index);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);
  return true;
}

static bool wasm_emit_wasi_fs_write_path(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag, bool maybe_result) {
  if (!ctx || ctx->fd_write_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm filesystem requires WASI fd_write import", value ? value->line : 1, value ? value->column : 1, "missing fd_write import");
  }
  if (!wasm_emit_wasi_path_open(code, value->left, true, ctx, diag)) return false;
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x7f);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, maybe_result ? -1 : 0);
  wasm_append_byte(code, 0x05);

  wasm_emit_load_opened_fd(code);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->fs_fd_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 48);
  if (!wasm_emit_byte_view_ptr(code, value->right, ctx, diag)) return false;
  wasm_append_byte(code, 0x36);
  wasm_append_u32_leb(code, 2);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 52);
  if (!wasm_emit_byte_view_len(code, value->right, ctx, diag)) return false;
  wasm_append_byte(code, 0x36);
  wasm_append_u32_leb(code, 2);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->fs_fd_local_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 48);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 68);
  wasm_append_byte(code, 0x10);
  wasm_append_u32_leb(code, ctx->fd_write_import_index);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x7f);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, maybe_result ? -1 : 0);
  wasm_append_byte(code, 0x05);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 68);
  wasm_append_byte(code, 0x28);
  wasm_append_u32_leb(code, 2);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->fs_result_local_index);
  if (!wasm_emit_wasi_fs_close_fd(code, ctx, value, diag)) return false;
  wasm_append_byte(code, 0x20);
  wasm_append_u32_leb(code, ctx->fs_result_local_index);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);
  return true;
}

static bool wasm_emit_wasi_fs_exists(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!wasm_emit_wasi_path_open(code, value->left, false, ctx, diag)) return false;
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x46);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x7f);
  wasm_emit_load_opened_fd(code);
  wasm_append_byte(code, 0x21);
  wasm_append_u32_leb(code, ctx->fs_fd_local_index);
  if (!wasm_emit_wasi_fs_close_fd(code, ctx, value, diag)) return false;
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 1);
  wasm_append_byte(code, 0x05);
  wasm_append_byte(code, 0x41);
  wasm_append_i32_leb(code, 0);
  wasm_append_byte(code, 0x0b);
  return true;
}

static bool wasm_emit_wasi_fs_open_create(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  bool create = value && value->kind == IR_VALUE_FS_CREATE;
  if (!wasm_emit_wasi_path_open(code, value ? value->left : NULL, create, ctx, diag)) return false;
  wasm_emit_i32_const(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, value && value->type == IR_TYPE_I64 ? 0x7e : 0x7f);
  if (value && value->type == IR_TYPE_I64) {
    wasm_emit_packed_error(code, value->error_code);
  } else {
    wasm_emit_i32_const(code, -1);
  }
  wasm_append_byte(code, 0x05);
  wasm_emit_load_opened_fd(code);
  if (value && value->type == IR_TYPE_I64) wasm_emit_i64_success_from_i32_stack(code);
  wasm_append_byte(code, 0x0b);
  return true;
}

static bool wasm_emit_wasi_fs_read_file(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->fd_read_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm filesystem requires WASI fd_read import", value ? value->line : 1, value ? value->column : 1, "missing fd_read import");
  }
  if (!ctx || !value || value->local_index >= ctx->fun->local_len) {
    return wasm_diag(diag, "direct wasm std.fs.read local is out of range", value ? value->line : 1, value ? value->column : 1, "invalid File");
  }
  wasm_emit_i32_const(code, 48);
  if (!wasm_emit_byte_view_ptr(code, value->left, ctx, diag)) return false;
  wasm_emit_i32_store(code, 2, 0);
  wasm_emit_i32_const(code, 52);
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_emit_i32_store(code, 2, 0);
  wasm_emit_local_get(code, wasm_local_index(ctx->fun, value->local_index));
  wasm_emit_i32_const(code, 48);
  wasm_emit_i32_const(code, 1);
  wasm_emit_i32_const(code, 68);
  wasm_emit_call_index(code, ctx->fd_read_import_index);
  wasm_emit_local_set(code, ctx->fs_result_local_index);
  wasm_emit_local_get(code, ctx->fs_result_local_index);
  wasm_emit_i32_const(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, value->type == IR_TYPE_I64 ? 0x7e : 0x7f);
  if (value->type == IR_TYPE_I64) {
    wasm_emit_packed_error(code, value->error_code);
  } else {
    wasm_emit_i32_const(code, -1);
  }
  wasm_append_byte(code, 0x05);
  wasm_emit_i32_const(code, 68);
  wasm_emit_i32_load(code, 2, 0);
  if (value->type == IR_TYPE_I64) wasm_emit_i64_success_from_i32_stack(code);
  wasm_append_byte(code, 0x0b);
  return true;
}

static bool wasm_emit_wasi_fs_write_all_file(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->fd_write_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm filesystem requires WASI fd_write import", value ? value->line : 1, value ? value->column : 1, "missing fd_write import");
  }
  if (!ctx || !value || value->local_index >= ctx->fun->local_len) {
    return wasm_diag(diag, "direct wasm std.fs.writeAll local is out of range", value ? value->line : 1, value ? value->column : 1, "invalid File");
  }
  wasm_emit_i32_const(code, 48);
  if (!wasm_emit_byte_view_ptr(code, value->left, ctx, diag)) return false;
  wasm_emit_i32_store(code, 2, 0);
  wasm_emit_i32_const(code, 52);
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_emit_i32_store(code, 2, 0);
  wasm_emit_local_get(code, wasm_local_index(ctx->fun, value->local_index));
  wasm_emit_i32_const(code, 48);
  wasm_emit_i32_const(code, 1);
  wasm_emit_i32_const(code, 68);
  wasm_emit_call_index(code, ctx->fd_write_import_index);
  wasm_emit_local_set(code, ctx->fs_result_local_index);
  wasm_emit_local_get(code, ctx->fs_result_local_index);
  wasm_emit_i32_const(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, value->type == IR_TYPE_I64 ? 0x7e : 0x7f);
  if (value->type == IR_TYPE_I64) {
    wasm_emit_packed_error(code, value->error_code);
  } else {
    wasm_emit_i32_const(code, 0);
  }
  wasm_append_byte(code, 0x05);
  wasm_emit_i32_const(code, 68);
  wasm_emit_i32_load(code, 2, 0);
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_append_byte(code, 0x46);
  if (value->type == IR_TYPE_I64) {
    wasm_append_byte(code, 0x04);
    wasm_append_byte(code, 0x7e);
    wasm_emit_i64_const(code, 0);
    wasm_append_byte(code, 0x05);
    wasm_emit_packed_error(code, value->error_code);
    wasm_append_byte(code, 0x0b);
  }
  wasm_append_byte(code, 0x0b);
  return true;
}

static bool wasm_emit_wasi_fs_close_file(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || !value || value->local_index >= ctx->fun->local_len) {
    return wasm_diag(diag, "direct wasm std.fs.close local is out of range", value ? value->line : 1, value ? value->column : 1, "invalid File");
  }
  if (!wasm_emit_wasi_fs_close_local_fd(code, wasm_local_index(ctx->fun, value->local_index), ctx, value, diag)) return false;
  wasm_emit_drop(code);
  return true;
}

static bool wasm_emit_wasi_fs_file_len(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->fd_filestat_get_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm std.fs.fileLen requires WASI fd_filestat_get import", value ? value->line : 1, value ? value->column : 1, "missing fd_filestat_get import");
  }
  if (!ctx || !value || value->local_index >= ctx->fun->local_len) {
    return wasm_diag(diag, "direct wasm std.fs.fileLen local is out of range", value ? value->line : 1, value ? value->column : 1, "invalid File");
  }
  wasm_emit_local_get(code, wasm_local_index(ctx->fun, value->local_index));
  wasm_emit_i32_const(code, 96);
  wasm_emit_call_index(code, ctx->fd_filestat_get_import_index);
  wasm_emit_local_set(code, ctx->fs_result_local_index);
  wasm_emit_local_get(code, ctx->fs_result_local_index);
  wasm_emit_i32_const(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, value->type == IR_TYPE_I64 ? 0x7e : 0x7f);
  if (value->type == IR_TYPE_I64) {
    wasm_emit_packed_error(code, value->error_code);
  } else {
    wasm_emit_i32_const(code, -1);
  }
  wasm_append_byte(code, 0x05);
  wasm_emit_i32_const(code, 128);
  wasm_emit_i64_load(code, 3, 0);
  if (value->type != IR_TYPE_I64) wasm_append_byte(code, 0xa7);
  wasm_append_byte(code, 0x0b);
  return true;
}

static bool wasm_emit_wasi_fs_remove(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->path_unlink_file_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm std.fs.remove requires WASI path_unlink_file import", value ? value->line : 1, value ? value->column : 1, "missing path_unlink_file import");
  }
  wasm_emit_i32_const(code, 3);
  if (!wasm_emit_byte_view_ptr(code, value->left, ctx, diag)) return false;
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_emit_call_index(code, ctx->path_unlink_file_import_index);
  wasm_append_byte(code, 0x45);
  return true;
}

static bool wasm_emit_wasi_fs_rename(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->path_rename_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm std.fs.rename requires WASI path_rename import", value ? value->line : 1, value ? value->column : 1, "missing path_rename import");
  }
  wasm_emit_i32_const(code, 3);
  if (!wasm_emit_byte_view_ptr(code, value->left, ctx, diag)) return false;
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_emit_i32_const(code, 3);
  if (!wasm_emit_byte_view_ptr(code, value->right, ctx, diag)) return false;
  if (!wasm_emit_byte_view_len(code, value->right, ctx, diag)) return false;
  wasm_emit_call_index(code, ctx->path_rename_import_index);
  wasm_append_byte(code, 0x45);
  return true;
}

static bool wasm_emit_wasi_fs_make_dir(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->path_create_directory_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm std.fs.makeDir requires WASI path_create_directory import", value ? value->line : 1, value ? value->column : 1, "missing path_create_directory import");
  }
  wasm_emit_i32_const(code, 3);
  if (!wasm_emit_byte_view_ptr(code, value->left, ctx, diag)) return false;
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_emit_call_index(code, ctx->path_create_directory_import_index);
  wasm_append_byte(code, 0x45);
  return true;
}

static bool wasm_emit_wasi_fs_remove_dir(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->path_remove_directory_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm std.fs.removeDir requires WASI path_remove_directory import", value ? value->line : 1, value ? value->column : 1, "missing path_remove_directory import");
  }
  wasm_emit_i32_const(code, 3);
  if (!wasm_emit_byte_view_ptr(code, value->left, ctx, diag)) return false;
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_emit_call_index(code, ctx->path_remove_directory_import_index);
  wasm_append_byte(code, 0x45);
  return true;
}

static bool wasm_emit_wasi_fs_is_dir(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!wasm_emit_wasi_path_open_mode(code, value->left, 2, 2ll | 16384ll, ctx, diag)) return false;
  wasm_emit_i32_const(code, 0);
  wasm_append_byte(code, 0x46);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x7f);
  wasm_emit_load_opened_fd(code);
  wasm_emit_local_set(code, ctx->fs_fd_local_index);
  if (!wasm_emit_wasi_fs_close_fd(code, ctx, value, diag)) return false;
  wasm_emit_i32_const(code, 1);
  wasm_append_byte(code, 0x05);
  wasm_emit_i32_const(code, 0);
  wasm_append_byte(code, 0x0b);
  return true;
}

static bool wasm_emit_wasi_fs_dir_entry_count(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || ctx->fd_readdir_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm std.fs.dirEntryCount requires WASI fd_readdir import", value ? value->line : 1, value ? value->column : 1, "missing fd_readdir import");
  }
  if (!wasm_emit_wasi_path_open_mode(code, value->left, 2, 16384ll, ctx, diag)) return false;
  wasm_emit_i32_const(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x7f);
  wasm_emit_i32_const(code, -1);
  wasm_append_byte(code, 0x05);
  wasm_emit_load_opened_fd(code);
  wasm_emit_local_set(code, ctx->fs_fd_local_index);
  wasm_emit_local_get(code, ctx->fs_fd_local_index);
  wasm_emit_i32_const(code, 256);
  wasm_emit_i32_const(code, 512);
  wasm_emit_i64_const(code, 0);
  wasm_emit_i32_const(code, 68);
  wasm_emit_call_index(code, ctx->fd_readdir_import_index);
  wasm_emit_local_set(code, ctx->fs_result_local_index);
  if (!wasm_emit_wasi_fs_close_fd(code, ctx, value, diag)) return false;
  wasm_emit_local_get(code, ctx->fs_result_local_index);
  wasm_emit_i32_const(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x7f);
  wasm_emit_i32_const(code, -1);
  wasm_append_byte(code, 0x05);
  wasm_emit_i32_const(code, 68);
  wasm_emit_i32_load(code, 2, 0);
  wasm_emit_i32_const(code, 0);
  wasm_append_byte(code, 0x4b);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);
  return true;
}

static bool wasm_emit_wasi_fs_atomic_write(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right || !value->index) {
    return wasm_diag(diag, "direct wasm std.fs.atomicWrite requires path, temp path, and bytes", value ? value->line : 1, value ? value->column : 1, "missing argument");
  }
  if (!ctx || ctx->fd_write_import_index == (unsigned)-1 || ctx->path_rename_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm std.fs.atomicWrite requires WASI write and rename imports", value->line, value->column, "missing WASI import");
  }
  if (!wasm_emit_wasi_path_open(code, value->right, true, ctx, diag)) return false;
  wasm_emit_i32_const(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x7f);
  wasm_emit_i32_const(code, 0);
  wasm_append_byte(code, 0x05);
  wasm_emit_load_opened_fd(code);
  wasm_emit_local_set(code, ctx->fs_fd_local_index);
  wasm_emit_i32_const(code, 48);
  if (!wasm_emit_byte_view_ptr(code, value->index, ctx, diag)) return false;
  wasm_emit_i32_store(code, 2, 0);
  wasm_emit_i32_const(code, 52);
  if (!wasm_emit_byte_view_len(code, value->index, ctx, diag)) return false;
  wasm_emit_i32_store(code, 2, 0);
  wasm_emit_local_get(code, ctx->fs_fd_local_index);
  wasm_emit_i32_const(code, 48);
  wasm_emit_i32_const(code, 1);
  wasm_emit_i32_const(code, 68);
  wasm_emit_call_index(code, ctx->fd_write_import_index);
  wasm_emit_local_set(code, ctx->fs_result_local_index);
  if (!wasm_emit_wasi_fs_close_fd(code, ctx, value, diag)) return false;
  wasm_emit_local_get(code, ctx->fs_result_local_index);
  wasm_emit_i32_const(code, 0);
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x7f);
  wasm_emit_i32_const(code, 0);
  wasm_append_byte(code, 0x05);
  wasm_emit_i32_const(code, 68);
  wasm_emit_i32_load(code, 2, 0);
  if (!wasm_emit_byte_view_len(code, value->index, ctx, diag)) return false;
  wasm_append_byte(code, 0x47);
  wasm_append_byte(code, 0x04);
  wasm_append_byte(code, 0x7f);
  wasm_emit_i32_const(code, 0);
  wasm_append_byte(code, 0x05);
  wasm_emit_i32_const(code, 3);
  if (!wasm_emit_byte_view_ptr(code, value->right, ctx, diag)) return false;
  if (!wasm_emit_byte_view_len(code, value->right, ctx, diag)) return false;
  wasm_emit_i32_const(code, 3);
  if (!wasm_emit_byte_view_ptr(code, value->left, ctx, diag)) return false;
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_emit_call_index(code, ctx->path_rename_import_index);
  wasm_append_byte(code, 0x45);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);
  return true;
}

static bool wasm_emit_crc32_bytes(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || !ctx->has_byte_views) {
    return wasm_diag(diag, "direct wasm CRC32 requires byte-view scratch locals", value ? value->line : 1, value ? value->column : 1, "missing byte view context");
  }
  if (!value || !value->left) {
    return wasm_diag(diag, "direct wasm CRC32 requires a byte view", value ? value->line : 1, value ? value->column : 1, "missing byte view");
  }
  if (!wasm_emit_byte_view_ptr(code, value->left, ctx, diag)) return false;
  wasm_emit_local_set(code, ctx->byte_cmp_left_ptr_local_index);
  if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
  wasm_emit_local_set(code, ctx->byte_cmp_len_local_index);
  wasm_emit_i32_const(code, -1);
  wasm_emit_local_set(code, ctx->byte_cmp_result_local_index);
  wasm_emit_i32_const(code, 0);
  wasm_emit_local_set(code, ctx->byte_cmp_i_local_index);

  wasm_append_byte(code, 0x02);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x03);
  wasm_append_byte(code, 0x40);
  wasm_emit_local_get(code, ctx->byte_cmp_i_local_index);
  wasm_emit_local_get(code, ctx->byte_cmp_len_local_index);
  wasm_append_byte(code, 0x4f);
  wasm_append_byte(code, 0x0d);
  wasm_append_u32_leb(code, 1);

  wasm_emit_local_get(code, ctx->byte_cmp_result_local_index);
  wasm_emit_local_get(code, ctx->byte_cmp_left_ptr_local_index);
  wasm_emit_local_get(code, ctx->byte_cmp_i_local_index);
  wasm_append_byte(code, 0x6a);
  wasm_append_byte(code, 0x2d);
  wasm_append_u32_leb(code, 0);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x73);
  wasm_emit_local_set(code, ctx->byte_cmp_result_local_index);
  wasm_emit_i32_const(code, 0);
  wasm_emit_local_set(code, ctx->byte_index_local_index);

  wasm_append_byte(code, 0x02);
  wasm_append_byte(code, 0x40);
  wasm_append_byte(code, 0x03);
  wasm_append_byte(code, 0x40);
  wasm_emit_local_get(code, ctx->byte_index_local_index);
  wasm_emit_i32_const(code, 8);
  wasm_append_byte(code, 0x4f);
  wasm_append_byte(code, 0x0d);
  wasm_append_u32_leb(code, 1);
  wasm_emit_i32_const(code, 0);
  wasm_emit_local_get(code, ctx->byte_cmp_result_local_index);
  wasm_emit_i32_const(code, 1);
  wasm_append_byte(code, 0x71);
  wasm_append_byte(code, 0x6b);
  wasm_emit_i32_const(code, (int32_t)0xedb88320u);
  wasm_append_byte(code, 0x71);
  wasm_emit_local_get(code, ctx->byte_cmp_result_local_index);
  wasm_emit_i32_const(code, 1);
  wasm_append_byte(code, 0x76);
  wasm_append_byte(code, 0x73);
  wasm_emit_local_set(code, ctx->byte_cmp_result_local_index);
  wasm_emit_local_get(code, ctx->byte_index_local_index);
  wasm_emit_i32_const(code, 1);
  wasm_append_byte(code, 0x6a);
  wasm_emit_local_set(code, ctx->byte_index_local_index);
  wasm_append_byte(code, 0x0c);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);

  wasm_emit_local_get(code, ctx->byte_cmp_i_local_index);
  wasm_emit_i32_const(code, 1);
  wasm_append_byte(code, 0x6a);
  wasm_emit_local_set(code, ctx->byte_cmp_i_local_index);
  wasm_append_byte(code, 0x0c);
  wasm_append_u32_leb(code, 0);
  wasm_append_byte(code, 0x0b);
  wasm_append_byte(code, 0x0b);

  wasm_emit_local_get(code, ctx->byte_cmp_result_local_index);
  wasm_emit_i32_const(code, -1);
  wasm_append_byte(code, 0x73);
  return true;
}

static bool wasm_emit_value(ZBuf *code, const IrValue *value, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!value) return wasm_diag(diag, "direct wasm expression is missing", 1, 1, "missing expression");
  switch (value->kind) {
    case IR_VALUE_BOOL:
    case IR_VALUE_INT:
      if (wasm_is_i64_type(value->type)) {
        wasm_append_byte(code, 0x42);
        wasm_append_i64_leb(code, (int64_t)value->int_value);
      } else {
        wasm_append_byte(code, 0x41);
        wasm_append_i32_leb(code, (int32_t)value->int_value);
      }
      return true;
    case IR_VALUE_LOCAL:
      if (ctx && value->local_index < ctx->fun->local_len && (ctx->fun->locals[value->local_index].is_array || ctx->fun->locals[value->local_index].is_record)) {
        return wasm_diag(diag, "direct wasm cannot use memory-backed locals as scalar values", value->line, value->column, "memory-backed local");
      }
      wasm_append_byte(code, 0x20);
      wasm_append_u32_leb(code, ctx ? wasm_local_index(ctx->fun, value->local_index) : value->local_index);
      return true;
    case IR_VALUE_BINARY: {
      if (!wasm_emit_value(code, value->left, ctx, diag) || !wasm_emit_value(code, value->right, ctx, diag)) return false;
      bool i64 = wasm_is_i64_type(value->type);
      bool uns = wasm_is_unsigned_type(value->type);
      unsigned char op = 0;
      if (value->binary_op == IR_BIN_ADD) op = i64 ? 0x7c : 0x6a;
      else if (value->binary_op == IR_BIN_SUB) op = i64 ? 0x7d : 0x6b;
      else if (value->binary_op == IR_BIN_MUL) op = i64 ? 0x7e : 0x6c;
      else if (value->binary_op == IR_BIN_DIV) op = i64 ? (uns ? 0x80 : 0x7f) : (uns ? 0x6e : 0x6d);
      else if (value->binary_op == IR_BIN_MOD) op = i64 ? (uns ? 0x82 : 0x81) : (uns ? 0x70 : 0x6f);
      else if (value->binary_op == IR_BIN_AND) op = i64 ? 0x83 : 0x71;
      else if (value->binary_op == IR_BIN_OR) op = i64 ? 0x84 : 0x72;
      else return wasm_diag(diag, "direct wasm binary operator is unsupported", value->line, value->column, "unsupported operator");
      wasm_append_byte(code, op);
      return true;
    }
    case IR_VALUE_COMPARE: {
      if (!wasm_emit_value(code, value->left, ctx, diag) || !wasm_emit_value(code, value->right, ctx, diag)) return false;
      bool i64 = wasm_is_i64_type(value->left ? value->left->type : IR_TYPE_I32);
      bool uns = wasm_is_unsigned_type(value->left ? value->left->type : IR_TYPE_I32);
      unsigned char op = 0;
      if (value->compare_op == IR_CMP_EQ) op = i64 ? 0x51 : 0x46;
      else if (value->compare_op == IR_CMP_NE) op = i64 ? 0x52 : 0x47;
      else if (value->compare_op == IR_CMP_LT) op = i64 ? (uns ? 0x54 : 0x53) : (uns ? 0x49 : 0x48);
      else if (value->compare_op == IR_CMP_LE) op = i64 ? (uns ? 0x58 : 0x57) : (uns ? 0x4d : 0x4c);
      else if (value->compare_op == IR_CMP_GT) op = i64 ? (uns ? 0x56 : 0x55) : (uns ? 0x4b : 0x4a);
      else if (value->compare_op == IR_CMP_GE) op = i64 ? (uns ? 0x5a : 0x59) : (uns ? 0x4f : 0x4e);
      else return wasm_diag(diag, "direct wasm comparison operator is unsupported", value->line, value->column, "unsupported comparison");
      wasm_append_byte(code, op);
      return true;
    }
    case IR_VALUE_CALL:
      for (size_t i = 0; i < value->arg_len; i++) {
        if (!wasm_emit_value(code, value->args[i], ctx, diag)) return false;
      }
      wasm_append_byte(code, 0x10);
      wasm_append_u32_leb(code, value->callee_index + (ctx ? ctx->imported_function_count : 0));
      return true;
    case IR_VALUE_JSON_PARSE_BYTES:
      return wasm_emit_json_parse_bytes_call(code, value, ctx, diag);
    case IR_VALUE_JSON_VALIDATE_BYTES:
      if (!wasm_emit_json_parse_bytes_call(code, value, ctx, diag)) return false;
      wasm_emit_i64_const(code, 0);
      wasm_append_byte(code, 0x59);
      return true;
    case IR_VALUE_JSON_STREAM_TOKENS_BYTES:
      if (!ctx || ctx->json_result_local_index == (unsigned)-1) {
        return wasm_diag(diag, "direct wasm JSON token streaming requires a scratch local", value->line, value->column, "missing JSON scratch local");
      }
      if (!wasm_emit_json_parse_bytes_call(code, value, ctx, diag)) return false;
      wasm_emit_local_set(code, ctx->json_result_local_index);
      wasm_emit_local_get(code, ctx->json_result_local_index);
      wasm_emit_i64_const(code, 0);
      wasm_append_byte(code, 0x59);
      wasm_append_byte(code, 0x04);
      wasm_append_byte(code, 0x7f);
      wasm_emit_local_get(code, ctx->json_result_local_index);
      wasm_append_byte(code, 0xa7);
      wasm_append_byte(code, 0x05);
      wasm_emit_i32_const(code, 0);
      wasm_append_byte(code, 0x0b);
      return true;
    case IR_VALUE_CHECK:
      return wasm_emit_check_value(code, value, ctx, diag);
    case IR_VALUE_RESCUE:
      return wasm_emit_rescue_value(code, value, ctx, diag);
    case IR_VALUE_INDEX_LOAD: {
      if (!ctx || value->array_index >= ctx->fun->local_len) return wasm_diag(diag, "direct wasm array index is out of range", value->line, value->column, "invalid array local");
      const IrLocal *local = &ctx->fun->locals[value->array_index];
      if (!local->is_array) return wasm_diag(diag, "direct wasm indexed load requires fixed array local", value->line, value->column, "non-array local");
      if (!wasm_emit_bounds_checked_address(code, local, value->index, ctx, diag)) return false;
      if (local->element_type == IR_TYPE_U8) {
        wasm_append_byte(code, 0x2d);
        wasm_append_u32_leb(code, 0);
        wasm_append_u32_leb(code, 0);
      } else {
        wasm_append_byte(code, 0x28);
        wasm_append_u32_leb(code, wasm_type_align_log2(local->element_type));
        wasm_append_u32_leb(code, 0);
      }
      return true;
    }
    case IR_VALUE_FIELD_LOAD: {
      if (!ctx || value->local_index >= ctx->fun->local_len) return wasm_diag(diag, "direct wasm field load record is out of range", value->line, value->column, "invalid record local");
      const IrLocal *local = &ctx->fun->locals[value->local_index];
      if (!local->is_record) return wasm_diag(diag, "direct wasm field load requires record local", value->line, value->column, "non-record local");
      wasm_append_byte(code, 0x20);
      wasm_append_u32_leb(code, ctx->frame_local_index);
      wasm_append_byte(code, 0x41);
      wasm_append_i32_leb(code, (int32_t)(wasm_local_memory_offset(local) + value->field_offset));
      wasm_append_byte(code, 0x6a);
      if (value->type == IR_TYPE_U8 || value->type == IR_TYPE_BOOL) {
        wasm_append_byte(code, 0x2d);
        wasm_append_u32_leb(code, 0);
        wasm_append_u32_leb(code, 0);
      } else if (value->type == IR_TYPE_U16) {
        wasm_append_byte(code, 0x2f);
        wasm_append_u32_leb(code, 1);
        wasm_append_u32_leb(code, 0);
      } else {
        wasm_append_byte(code, 0x28);
        wasm_append_u32_leb(code, wasm_type_align_log2(value->type));
        wasm_append_u32_leb(code, 0);
      }
      return true;
    }
    case IR_VALUE_BYTE_VIEW_LEN:
      return wasm_emit_byte_view_len(code, value->left, ctx, diag);
    case IR_VALUE_MAYBE_HAS:
      if (!ctx || value->local_index >= ctx->fun->local_len) return wasm_diag(diag, "direct wasm maybe local is invalid", value->line, value->column, "invalid maybe local");
      wasm_append_byte(code, 0x20);
      wasm_append_u32_leb(code, wasm_local_index(ctx->fun, value->local_index));
      return true;
    case IR_VALUE_VEC_LEN:
    case IR_VALUE_VEC_CAPACITY:
      if (!ctx || value->local_index >= ctx->fun->local_len || ctx->fun->locals[value->local_index].type != IR_TYPE_VEC) {
        return wasm_diag(diag, "direct wasm Vec helper requires a Vec local", value->line, value->column, "invalid Vec local");
      }
      wasm_append_byte(code, 0x20);
      wasm_append_u32_leb(code, wasm_local_index(ctx->fun, value->local_index) + (value->kind == IR_VALUE_VEC_LEN ? 1 : 2));
      return true;
    case IR_VALUE_VEC_PUSH: {
      if (!ctx || value->local_index >= ctx->fun->local_len || ctx->fun->locals[value->local_index].type != IR_TYPE_VEC) {
        return wasm_diag(diag, "direct wasm Vec push requires a Vec local", value->line, value->column, "invalid Vec local");
      }
      unsigned vec_base = wasm_local_index(ctx->fun, value->local_index);
      wasm_append_byte(code, 0x20);
      wasm_append_u32_leb(code, vec_base + 1);
      wasm_append_byte(code, 0x20);
      wasm_append_u32_leb(code, vec_base + 2);
      wasm_append_byte(code, 0x4f);
      wasm_append_byte(code, 0x04);
      wasm_append_byte(code, 0x7f);
      wasm_append_byte(code, 0x41);
      wasm_append_i32_leb(code, 0);
      wasm_append_byte(code, 0x05);
      wasm_append_byte(code, 0x20);
      wasm_append_u32_leb(code, vec_base);
      wasm_append_byte(code, 0x20);
      wasm_append_u32_leb(code, vec_base + 1);
      wasm_append_byte(code, 0x6a);
      if (!wasm_emit_value(code, value->left, ctx, diag)) return false;
      wasm_append_byte(code, 0x3a);
      wasm_append_u32_leb(code, 0);
      wasm_append_u32_leb(code, 0);
      wasm_append_byte(code, 0x20);
      wasm_append_u32_leb(code, vec_base + 1);
      wasm_append_byte(code, 0x41);
      wasm_append_i32_leb(code, 1);
      wasm_append_byte(code, 0x6a);
      wasm_append_byte(code, 0x21);
      wasm_append_u32_leb(code, vec_base + 1);
      wasm_append_byte(code, 0x41);
      wasm_append_i32_leb(code, 1);
      wasm_append_byte(code, 0x0b);
      return true;
    }
    case IR_VALUE_BYTE_VIEW_EQ:
      return wasm_emit_byte_view_eq(code, value, ctx, diag);
    case IR_VALUE_BYTE_COPY:
      return wasm_emit_byte_copy(code, value, ctx, diag);
    case IR_VALUE_BYTE_FILL:
      return wasm_emit_byte_fill(code, value, ctx, diag);
    case IR_VALUE_CRC32_BYTES:
      return wasm_emit_crc32_bytes(code, value, ctx, diag);
    case IR_VALUE_ARGS_LEN:
      return wasm_emit_wasi_args_len(code, value, ctx, diag);
    case IR_VALUE_TIME_WALL_SECONDS:
      return wasm_emit_wasi_clock_time(code, 0, true, value, ctx, diag);
    case IR_VALUE_TIME_MONOTONIC:
      return wasm_emit_wasi_clock_time(code, 1, false, value, ctx, diag);
    case IR_VALUE_TIME_AS_MS:
      if (!wasm_emit_value(code, value->left, ctx, diag)) return false;
      wasm_append_byte(code, 0x42);
      wasm_append_i64_leb(code, 1000000ll);
      wasm_append_byte(code, 0x7f);
      wasm_append_byte(code, 0xa7);
      return true;
    case IR_VALUE_RAND_NEXT_U32: {
      if (!ctx || value->local_index >= ctx->fun->local_len) return wasm_diag(diag, "direct wasm std.rand.nextU32 local is out of range", value->line, value->column, "invalid RandSource");
      unsigned local_index = wasm_local_index(ctx->fun, value->local_index);
      wasm_append_byte(code, 0x20);
      wasm_append_u32_leb(code, local_index);
      wasm_append_byte(code, 0x41);
      wasm_append_i32_leb(code, 1664525);
      wasm_append_byte(code, 0x6c);
      wasm_append_byte(code, 0x41);
      wasm_append_i32_leb(code, 1013904223);
      wasm_append_byte(code, 0x6a);
      wasm_append_byte(code, 0x22);
      wasm_append_u32_leb(code, local_index);
      return true;
    }
    case IR_VALUE_RAND_ENTROPY_U32:
      return wasm_emit_wasi_random_u32(code, value, ctx, diag);
    case IR_VALUE_FS_OPEN:
    case IR_VALUE_FS_CREATE:
      return wasm_emit_wasi_fs_open_create(code, value, ctx, diag);
    case IR_VALUE_FS_READ_PATH:
      return wasm_emit_wasi_fs_read_path(code, value, ctx, diag, false);
    case IR_VALUE_FS_READ_BYTES_PATH:
      return wasm_emit_wasi_fs_read_path(code, value, ctx, diag, true);
    case IR_VALUE_FS_WRITE_PATH:
      return wasm_emit_wasi_fs_write_path(code, value, ctx, diag, false);
    case IR_VALUE_FS_WRITE_BYTES_PATH:
      return wasm_emit_wasi_fs_write_path(code, value, ctx, diag, true);
    case IR_VALUE_FS_READ_FILE:
      return wasm_emit_wasi_fs_read_file(code, value, ctx, diag);
    case IR_VALUE_FS_WRITE_ALL_FILE:
      return wasm_emit_wasi_fs_write_all_file(code, value, ctx, diag);
    case IR_VALUE_FS_CLOSE_FILE:
      return wasm_emit_wasi_fs_close_file(code, value, ctx, diag);
    case IR_VALUE_FS_EXISTS:
      return wasm_emit_wasi_fs_exists(code, value, ctx, diag);
    case IR_VALUE_FS_REMOVE:
      return wasm_emit_wasi_fs_remove(code, value, ctx, diag);
    case IR_VALUE_FS_RENAME:
      return wasm_emit_wasi_fs_rename(code, value, ctx, diag);
    case IR_VALUE_FS_FILE_LEN:
      return wasm_emit_wasi_fs_file_len(code, value, ctx, diag);
    case IR_VALUE_FS_MAKE_DIR:
      return wasm_emit_wasi_fs_make_dir(code, value, ctx, diag);
    case IR_VALUE_FS_REMOVE_DIR:
      return wasm_emit_wasi_fs_remove_dir(code, value, ctx, diag);
    case IR_VALUE_FS_IS_DIR:
      return wasm_emit_wasi_fs_is_dir(code, value, ctx, diag);
    case IR_VALUE_FS_DIR_ENTRY_COUNT:
      return wasm_emit_wasi_fs_dir_entry_count(code, value, ctx, diag);
    case IR_VALUE_FS_ATOMIC_WRITE:
      return wasm_emit_wasi_fs_atomic_write(code, value, ctx, diag);
    case IR_VALUE_MAYBE_VALUE:
      if (!ctx || value->local_index >= ctx->fun->local_len || ctx->fun->locals[value->local_index].type != IR_TYPE_MAYBE_SCALAR) {
        return wasm_diag(diag, "direct wasm maybe scalar value requires a Maybe scalar local", value->line, value->column, "invalid maybe value");
      }
      wasm_emit_local_get(code, wasm_local_index(ctx->fun, value->local_index) + 1);
      return true;
    case IR_VALUE_MEMORY_PEEK_U8:
      if (!wasm_emit_value(code, value->left, ctx, diag)) return false;
      wasm_append_byte(code, 0x2d);
      wasm_append_u32_leb(code, 0);
      wasm_append_u32_leb(code, 0);
      return true;
    case IR_VALUE_MEMORY_POKE_U8:
      if (!wasm_emit_value(code, value->left, ctx, diag)) return false;
      if (!wasm_emit_value(code, value->right, ctx, diag)) return false;
      wasm_append_byte(code, 0x3a);
      wasm_append_u32_leb(code, 0);
      wasm_append_u32_leb(code, 0);
      wasm_emit_i32_const(code, 1);
      return true;
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD:
      if (!ctx || !ctx->has_byte_views) return wasm_diag(diag, "direct wasm byte view access requires a memory context", value->line, value->column, "missing byte view context");
      if (!wasm_emit_value(code, value->index, ctx, diag)) return false;
      wasm_append_byte(code, 0x21);
      wasm_append_u32_leb(code, ctx->byte_index_local_index);
      wasm_append_byte(code, 0x20);
      wasm_append_u32_leb(code, ctx->byte_index_local_index);
      if (!wasm_emit_byte_view_len(code, value->left, ctx, diag)) return false;
      wasm_append_byte(code, 0x4f);
      wasm_emit_trap_if_top_true(code);
      if (!wasm_emit_byte_view_ptr(code, value->left, ctx, diag)) return false;
      wasm_append_byte(code, 0x20);
      wasm_append_u32_leb(code, ctx->byte_index_local_index);
      wasm_append_byte(code, 0x6a);
      wasm_append_byte(code, 0x2d);
      wasm_append_u32_leb(code, 0);
      wasm_append_u32_leb(code, 0);
      return true;
    case IR_VALUE_STRING_LITERAL:
    case IR_VALUE_BYTE_SLICE:
    case IR_VALUE_FIXED_BUF_ALLOC:
    case IR_VALUE_VEC_INIT:
    case IR_VALUE_ALLOC_BYTES:
    case IR_VALUE_FS_READ_ALL:
    case IR_VALUE_FS_TEMP_NAME:
      return wasm_diag(diag, "direct wasm byte views must be indexed, sliced, or passed to std.mem.len", value->line, value->column, "byte view used as scalar");
    default:
      return wasm_diag(diag, "direct wasm value kind is unsupported", value->line, value->column, "unsupported value");
  }
}

static void wasm_emit_frame_restore(ZBuf *body, const WasmEmitContext *ctx) {
  if (!ctx || !ctx->has_arrays) return;
  wasm_append_byte(body, 0x20);
  wasm_append_u32_leb(body, ctx->frame_local_index);
  wasm_append_byte(body, 0x41);
  wasm_append_i32_leb(body, (int32_t)ctx->fun->frame_bytes);
  wasm_append_byte(body, 0x6a);
  wasm_append_byte(body, 0x24);
  wasm_append_u32_leb(body, 0);
}

static bool wasm_emit_alloc_bytes_to_local(ZBuf *body, const IrValue *value, unsigned maybe_base, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || !ctx->has_allocator) return wasm_diag(diag, "direct wasm allocation requires allocator scratch locals", value ? value->line : 1, value ? value->column : 1, "missing allocator context");
  if (!value || value->kind != IR_VALUE_ALLOC_BYTES || value->local_index >= ctx->fun->local_len) {
    return wasm_diag(diag, "direct wasm allocation source is invalid", value ? value->line : 1, value ? value->column : 1, "invalid allocation");
  }
  unsigned alloc_base = wasm_local_index(ctx->fun, value->local_index);
  if (!wasm_emit_value(body, value->left, ctx, diag)) return false;
  wasm_append_byte(body, 0x21);
  wasm_append_u32_leb(body, ctx->alloc_len_local_index);

  wasm_append_byte(body, 0x20);
  wasm_append_u32_leb(body, alloc_base + 2);
  wasm_append_byte(body, 0x20);
  wasm_append_u32_leb(body, ctx->alloc_len_local_index);
  wasm_append_byte(body, 0x6a);
  wasm_append_byte(body, 0x21);
  wasm_append_u32_leb(body, ctx->alloc_next_local_index);

  wasm_append_byte(body, 0x20);
  wasm_append_u32_leb(body, ctx->alloc_next_local_index);
  wasm_append_byte(body, 0x20);
  wasm_append_u32_leb(body, alloc_base + 1);
  wasm_append_byte(body, 0x4b);
  wasm_append_byte(body, 0x04);
  wasm_append_byte(body, 0x40);

  wasm_append_byte(body, 0x41);
  wasm_append_i32_leb(body, 0);
  wasm_append_byte(body, 0x21);
  wasm_append_u32_leb(body, maybe_base);
  wasm_append_byte(body, 0x41);
  wasm_append_i32_leb(body, 0);
  wasm_append_byte(body, 0x21);
  wasm_append_u32_leb(body, maybe_base + 1);
  wasm_append_byte(body, 0x41);
  wasm_append_i32_leb(body, 0);
  wasm_append_byte(body, 0x21);
  wasm_append_u32_leb(body, maybe_base + 2);

  wasm_append_byte(body, 0x05);

  wasm_append_byte(body, 0x41);
  wasm_append_i32_leb(body, 1);
  wasm_append_byte(body, 0x21);
  wasm_append_u32_leb(body, maybe_base);
  wasm_append_byte(body, 0x20);
  wasm_append_u32_leb(body, alloc_base);
  wasm_append_byte(body, 0x20);
  wasm_append_u32_leb(body, alloc_base + 2);
  wasm_append_byte(body, 0x6a);
  wasm_append_byte(body, 0x21);
  wasm_append_u32_leb(body, maybe_base + 1);
  wasm_append_byte(body, 0x20);
  wasm_append_u32_leb(body, ctx->alloc_len_local_index);
  wasm_append_byte(body, 0x21);
  wasm_append_u32_leb(body, maybe_base + 2);
  wasm_append_byte(body, 0x20);
  wasm_append_u32_leb(body, ctx->alloc_next_local_index);
  wasm_append_byte(body, 0x21);
  wasm_append_u32_leb(body, alloc_base + 2);

  wasm_append_byte(body, 0x0b);
  return true;
}

static bool wasm_emit_wasi_fs_read_all_to_local(ZBuf *body, const IrValue *value, unsigned maybe_base, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || !value || value->local_index >= ctx->fun->local_len || ctx->fun->locals[value->local_index].type != IR_TYPE_ALLOC) {
    return wasm_diag(diag, "direct wasm std.fs.readAll allocator is invalid", value ? value->line : 1, value ? value->column : 1, "invalid allocator");
  }
  if (!ctx || ctx->fd_read_import_index == (unsigned)-1 || ctx->fd_close_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm std.fs.readAll requires WASI fd_read/fd_close imports", value->line, value->column, "missing WASI import");
  }
  unsigned alloc_base = wasm_local_index(ctx->fun, value->local_index);
  if (!wasm_emit_wasi_path_open(body, value->left, false, ctx, diag)) return false;
  wasm_emit_i32_const(body, 0);
  wasm_append_byte(body, 0x47);
  wasm_append_byte(body, 0x04);
  wasm_append_byte(body, 0x40);
  wasm_emit_maybe_byte_view_clear(body, maybe_base);
  wasm_append_byte(body, 0x05);

  wasm_emit_load_opened_fd(body);
  wasm_emit_local_set(body, ctx->fs_fd_local_index);
  wasm_emit_local_get(body, alloc_base);
  wasm_emit_local_get(body, alloc_base + 2);
  wasm_append_byte(body, 0x6a);
  wasm_emit_local_set(body, ctx->fs_aux_local_index);
  wasm_emit_local_get(body, alloc_base + 1);
  wasm_emit_local_get(body, alloc_base + 2);
  wasm_append_byte(body, 0x6b);
  wasm_emit_local_set(body, ctx->fs_len_local_index);
  if (value->right) {
    if (!wasm_emit_value(body, value->right, ctx, diag)) return false;
    wasm_emit_local_set(body, ctx->fs_i_local_index);
    wasm_emit_local_get(body, ctx->fs_len_local_index);
    wasm_emit_local_get(body, ctx->fs_i_local_index);
    wasm_append_byte(body, 0x4b);
    wasm_append_byte(body, 0x04);
    wasm_append_byte(body, 0x40);
    wasm_emit_local_get(body, ctx->fs_i_local_index);
    wasm_emit_local_set(body, ctx->fs_len_local_index);
    wasm_append_byte(body, 0x0b);
  }
  wasm_emit_i32_const(body, 48);
  wasm_emit_local_get(body, ctx->fs_aux_local_index);
  wasm_emit_i32_store(body, 2, 0);
  wasm_emit_i32_const(body, 52);
  wasm_emit_local_get(body, ctx->fs_len_local_index);
  wasm_emit_i32_store(body, 2, 0);
  wasm_emit_local_get(body, ctx->fs_fd_local_index);
  wasm_emit_i32_const(body, 48);
  wasm_emit_i32_const(body, 1);
  wasm_emit_i32_const(body, 68);
  wasm_emit_call_index(body, ctx->fd_read_import_index);
  wasm_emit_local_set(body, ctx->fs_result_local_index);
  if (!wasm_emit_wasi_fs_close_fd(body, ctx, value, diag)) return false;
  wasm_emit_local_get(body, ctx->fs_result_local_index);
  wasm_emit_i32_const(body, 0);
  wasm_append_byte(body, 0x47);
  wasm_append_byte(body, 0x04);
  wasm_append_byte(body, 0x40);
  wasm_emit_maybe_byte_view_clear(body, maybe_base);
  wasm_append_byte(body, 0x05);
  wasm_emit_i32_const(body, 1);
  wasm_emit_local_set(body, maybe_base);
  wasm_emit_local_get(body, ctx->fs_aux_local_index);
  wasm_emit_local_set(body, maybe_base + 1);
  wasm_emit_i32_const(body, 68);
  wasm_emit_i32_load(body, 2, 0);
  wasm_emit_local_tee(body, ctx->fs_len_local_index);
  wasm_emit_local_set(body, maybe_base + 2);
  wasm_emit_local_get(body, alloc_base + 2);
  wasm_emit_local_get(body, ctx->fs_len_local_index);
  wasm_append_byte(body, 0x6a);
  wasm_emit_local_set(body, alloc_base + 2);
  wasm_append_byte(body, 0x0b);
  wasm_append_byte(body, 0x0b);
  return true;
}

static bool wasm_emit_wasi_fs_read_all_checked_to_local(ZBuf *body, const IrValue *check, unsigned local_base, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || !check || check->kind != IR_VALUE_CHECK || !check->left || check->left->kind != IR_VALUE_FS_READ_ALL) {
    return wasm_diag(diag, "direct wasm checked std.fs.readAllOrRaise local requires a readAllOrRaise check", check ? check->line : 1, check ? check->column : 1, "unsupported checked readAll");
  }
  const IrValue *value = check->left;
  if (value->local_index >= ctx->fun->local_len || ctx->fun->locals[value->local_index].type != IR_TYPE_ALLOC) {
    return wasm_diag(diag, "direct wasm std.fs.readAllOrRaise allocator is invalid", value->line, value->column, "invalid allocator");
  }
  if (ctx->fd_read_import_index == (unsigned)-1 || ctx->fd_close_import_index == (unsigned)-1 ||
      ctx->fd_filestat_get_import_index == (unsigned)-1) {
    return wasm_diag(diag, "direct wasm std.fs.readAllOrRaise requires WASI fd_read/fd_close/fd_filestat_get imports", value->line, value->column, "missing WASI import");
  }
  if (!wasm_function_propagates_to_process_exit(ctx->fun) || !ctx->has_fallibility) {
    return wasm_diag(diag, "direct wasm std.fs.readAllOrRaise check requires a fallible function context", check->line, check->column, "non-fallible context");
  }
  unsigned alloc_base = wasm_local_index(ctx->fun, value->local_index);
  if (!wasm_emit_wasi_path_open(body, value->left, false, ctx, diag)) return false;
  wasm_emit_i32_const(body, 0);
  wasm_append_byte(body, 0x47);
  wasm_append_byte(body, 0x04);
  wasm_append_byte(body, 0x40);
  if (ctx->fun->raises) {
    wasm_emit_packed_error(body, IR_ERROR_NOT_FOUND);
  } else {
    wasm_emit_packed_error(body, IR_ERROR_NOT_FOUND);
    wasm_emit_drop(body);
    wasm_emit_i32_const(body, 1);
  }
  wasm_emit_frame_restore(body, ctx);
  wasm_append_byte(body, 0x0f);
  wasm_append_byte(body, 0x0b);

  wasm_emit_load_opened_fd(body);
  wasm_emit_local_set(body, ctx->fs_fd_local_index);
  wasm_emit_local_get(body, ctx->fs_fd_local_index);
  wasm_emit_i32_const(body, 96);
  wasm_emit_call_index(body, ctx->fd_filestat_get_import_index);
  wasm_emit_local_set(body, ctx->fs_result_local_index);
  wasm_emit_local_get(body, ctx->fs_result_local_index);
  wasm_emit_i32_const(body, 0);
  wasm_append_byte(body, 0x47);
  wasm_append_byte(body, 0x04);
  wasm_append_byte(body, 0x40);
  if (!wasm_emit_wasi_fs_close_fd(body, ctx, value, diag)) return false;
  if (ctx->fun->raises) {
    wasm_emit_packed_error(body, IR_ERROR_IO);
  } else {
    wasm_emit_packed_error(body, IR_ERROR_IO);
    wasm_emit_drop(body);
    wasm_emit_i32_const(body, 1);
  }
  wasm_emit_frame_restore(body, ctx);
  wasm_append_byte(body, 0x0f);
  wasm_append_byte(body, 0x0b);

  wasm_emit_local_get(body, alloc_base);
  wasm_emit_local_get(body, alloc_base + 2);
  wasm_append_byte(body, 0x6a);
  wasm_emit_local_set(body, ctx->fs_aux_local_index);
  wasm_emit_local_get(body, alloc_base + 1);
  wasm_emit_local_get(body, alloc_base + 2);
  wasm_append_byte(body, 0x6b);
  wasm_emit_local_set(body, ctx->fs_len_local_index);
  if (value->right) {
    if (!wasm_emit_value(body, value->right, ctx, diag)) return false;
    wasm_emit_local_set(body, ctx->fs_i_local_index);
    wasm_emit_i32_const(body, 128);
    wasm_emit_i64_load(body, 3, 0);
    wasm_append_byte(body, 0xa7);
    wasm_emit_local_get(body, ctx->fs_i_local_index);
    wasm_append_byte(body, 0x4b);
    wasm_append_byte(body, 0x04);
    wasm_append_byte(body, 0x40);
    if (!wasm_emit_wasi_fs_close_fd(body, ctx, value, diag)) return false;
    if (ctx->fun->raises) {
      wasm_emit_packed_error(body, IR_ERROR_TOO_LARGE);
    } else {
      wasm_emit_packed_error(body, IR_ERROR_TOO_LARGE);
      wasm_emit_drop(body);
      wasm_emit_i32_const(body, 1);
    }
    wasm_emit_frame_restore(body, ctx);
    wasm_append_byte(body, 0x0f);
    wasm_append_byte(body, 0x0b);
  }
  wasm_emit_i32_const(body, 48);
  wasm_emit_local_get(body, ctx->fs_aux_local_index);
  wasm_emit_i32_store(body, 2, 0);
  wasm_emit_i32_const(body, 52);
  wasm_emit_local_get(body, ctx->fs_len_local_index);
  wasm_emit_i32_store(body, 2, 0);
  wasm_emit_local_get(body, ctx->fs_fd_local_index);
  wasm_emit_i32_const(body, 48);
  wasm_emit_i32_const(body, 1);
  wasm_emit_i32_const(body, 68);
  wasm_emit_call_index(body, ctx->fd_read_import_index);
  wasm_emit_local_set(body, ctx->fs_result_local_index);
  if (!wasm_emit_wasi_fs_close_fd(body, ctx, value, diag)) return false;
  wasm_emit_local_get(body, ctx->fs_result_local_index);
  wasm_emit_i32_const(body, 0);
  wasm_append_byte(body, 0x47);
  wasm_append_byte(body, 0x04);
  wasm_append_byte(body, 0x40);
  if (ctx->fun->raises) {
    wasm_emit_packed_error(body, IR_ERROR_IO);
  } else {
    wasm_emit_packed_error(body, IR_ERROR_IO);
    wasm_emit_drop(body);
    wasm_emit_i32_const(body, 1);
  }
  wasm_emit_frame_restore(body, ctx);
  wasm_append_byte(body, 0x0f);
  wasm_append_byte(body, 0x0b);

  wasm_emit_local_get(body, ctx->fs_aux_local_index);
  wasm_emit_local_set(body, local_base);
  wasm_emit_i32_const(body, 68);
  wasm_emit_i32_load(body, 2, 0);
  wasm_emit_local_tee(body, ctx->fs_len_local_index);
  wasm_emit_local_set(body, local_base + 1);
  wasm_emit_local_get(body, alloc_base + 2);
  wasm_emit_local_get(body, ctx->fs_len_local_index);
  wasm_append_byte(body, 0x6a);
  wasm_emit_local_set(body, alloc_base + 2);
  return true;
}

static bool wasm_const_u32_value(const IrValue *value, unsigned *out) {
  if (!value) return false;
  if (value->kind == IR_VALUE_INT) {
    if (value->int_value > UINT32_MAX) return false;
    if (out) *out = (unsigned)value->int_value;
    return true;
  }
  return false;
}

static bool wasm_byte_view_const_len(const IrFunction *fun, const IrValue *view, unsigned *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (out) *out = view->data_len;
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned base_len = 0;
    unsigned start = 0;
    unsigned end = 0;
    if (!wasm_byte_view_const_len(fun, view->left, &base_len)) return false;
    if (view->index && !wasm_const_u32_value(view->index, &start)) return false;
    if (view->right) {
      if (!wasm_const_u32_value(view->right, &end)) return false;
    } else {
      end = base_len;
    }
    if (start > end || end > base_len) return false;
    if (out) *out = end - start;
    return true;
  }
  (void)fun;
  return false;
}

static bool wasm_byte_view_const_byte(const IrProgram *ir, const IrValue *view, unsigned index, unsigned char *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL) {
    if (index >= view->data_len) return false;
    for (size_t i = 0; ir && i < ir->data_segment_len; i++) {
      const IrDataSegment *segment = &ir->data_segments[i];
      if (view->data_offset >= segment->offset && view->data_offset + view->data_len <= segment->offset + segment->len) {
        if (out) *out = segment->bytes[view->data_offset - segment->offset + index];
        return true;
      }
    }
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned start = 0;
    if (view->index && !wasm_const_u32_value(view->index, &start)) return false;
    return wasm_byte_view_const_byte(ir, view->left, start + index, out);
  }
  return false;
}

static bool wasm_emit_wasi_fs_temp_name_to_local(ZBuf *body, const IrValue *value, unsigned maybe_base, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx || !value || !value->left || !value->right || value->left->kind != IR_VALUE_ARRAY_BYTE_VIEW || value->left->array_index >= ctx->fun->local_len) {
    return wasm_diag(diag, "direct wasm std.fs.tempName requires a caller-provided fixed byte buffer", value ? value->line : 1, value ? value->column : 1, "unsupported temp buffer");
  }
  const IrLocal *buf_local = &ctx->fun->locals[value->left->array_index];
  if (!buf_local->is_array || buf_local->element_type != IR_TYPE_U8) {
    return wasm_diag(diag, "direct wasm std.fs.tempName buffer must be [N]u8", value->line, value->column, "non-byte temp buffer");
  }
  unsigned prefix_len = 0;
  if (!wasm_byte_view_const_len(ctx->fun, value->right, &prefix_len)) {
    return wasm_diag(diag, "direct wasm std.fs.tempName currently requires a literal prefix", value->line, value->column, "dynamic prefix");
  }
  unsigned char last = 0;
  if (prefix_len > 0 && wasm_byte_view_const_byte(ctx->ir, value->right, prefix_len - 1, &last) && last == 0) prefix_len--;
  unsigned total_len = prefix_len + 4;
  if (buf_local->array_len <= total_len) {
    wasm_emit_maybe_byte_view_clear(body, maybe_base);
    return true;
  }
  if (!wasm_emit_byte_view_ptr(body, value->left, ctx, diag)) return false;
  wasm_emit_local_set(body, ctx->fs_aux_local_index);
  for (unsigned i = 0; i < prefix_len; i++) {
    unsigned char byte = 0;
    if (!wasm_byte_view_const_byte(ctx->ir, value->right, i, &byte)) {
      return wasm_diag(diag, "direct wasm std.fs.tempName prefix byte is unavailable", value->line, value->column, "unavailable prefix");
    }
    wasm_emit_local_get(body, ctx->fs_aux_local_index);
    wasm_emit_i32_const(body, (int32_t)i);
    wasm_append_byte(body, 0x6a);
    wasm_emit_i32_const(body, byte);
    wasm_emit_i32_store8(body, 0);
  }
  const unsigned char suffix[] = {'-', 't', 'm', 'p', 0};
  for (unsigned i = 0; i < sizeof(suffix); i++) {
    wasm_emit_local_get(body, ctx->fs_aux_local_index);
    wasm_emit_i32_const(body, (int32_t)(prefix_len + i));
    wasm_append_byte(body, 0x6a);
    wasm_emit_i32_const(body, suffix[i]);
    wasm_emit_i32_store8(body, 0);
  }
  wasm_emit_i32_const(body, 1);
  wasm_emit_local_set(body, maybe_base);
  wasm_emit_local_get(body, ctx->fs_aux_local_index);
  wasm_emit_local_set(body, maybe_base + 1);
  wasm_emit_i32_const(body, (int32_t)total_len);
  wasm_emit_local_set(body, maybe_base + 2);
  return true;
}

static bool wasm_emit_instrs(ZBuf *body, const IrInstr *instrs, size_t len, const WasmEmitContext *ctx, ZDiag *diag);

static bool wasm_emit_world_write(ZBuf *body, const IrInstr *instr, const WasmEmitContext *ctx, ZDiag *diag) {
  if (!ctx) return wasm_diag(diag, "direct wasm World write requires runtime context", instr ? instr->line : 1, instr ? instr->column : 1, "missing context");
  if (!instr || !instr->value) return wasm_diag(diag, "direct wasm World write requires bytes", instr ? instr->line : 1, instr ? instr->column : 1, "missing byte view");

  wasm_append_byte(body, 0x41);
  wasm_append_i32_leb(body, 16);
  if (!wasm_emit_byte_view_ptr(body, instr->value, ctx, diag)) return false;
  wasm_append_byte(body, 0x36);
  wasm_append_u32_leb(body, 2);
  wasm_append_u32_leb(body, 0);

  wasm_append_byte(body, 0x41);
  wasm_append_i32_leb(body, 20);
  if (!wasm_emit_byte_view_len(body, instr->value, ctx, diag)) return false;
  wasm_append_byte(body, 0x36);
  wasm_append_u32_leb(body, 2);
  wasm_append_u32_leb(body, 0);

  wasm_append_byte(body, 0x41);
  wasm_append_i32_leb(body, instr->field_offset == 2 ? 2 : 1);
  wasm_append_byte(body, 0x41);
  wasm_append_i32_leb(body, 16);
  wasm_append_byte(body, 0x41);
  wasm_append_i32_leb(body, 1);
  wasm_append_byte(body, 0x41);
  wasm_append_i32_leb(body, 24);
  wasm_append_byte(body, 0x10);
  wasm_append_u32_leb(body, ctx->fd_write_import_index);
  wasm_append_byte(body, 0x41);
  wasm_append_i32_leb(body, 0);
  wasm_append_byte(body, 0x47);
  wasm_emit_trap_if_top_true(body);
  return true;
}

static bool wasm_emit_instr(ZBuf *body, const IrInstr *instr, const WasmEmitContext *ctx, ZDiag *diag) {
  if (instr->kind == IR_INSTR_WORLD_WRITE) {
    return wasm_emit_world_write(body, instr, ctx, diag);
  }
  if (instr->kind == IR_INSTR_LOCAL_SET) {
    if (ctx && instr->local_index < ctx->fun->local_len && ctx->fun->locals[instr->local_index].type == IR_TYPE_BYTE_VIEW) {
      unsigned local_base = wasm_local_index(ctx->fun, instr->local_index);
      if (instr->value && instr->value->kind == IR_VALUE_CHECK && instr->value->left && instr->value->left->kind == IR_VALUE_FS_READ_ALL) {
        return wasm_emit_wasi_fs_read_all_checked_to_local(body, instr->value, local_base, ctx, diag);
      }
      if (!wasm_emit_byte_view_ptr(body, instr->value, ctx, diag)) return false;
      wasm_append_byte(body, 0x21);
      wasm_append_u32_leb(body, local_base);
      if (!wasm_emit_byte_view_len(body, instr->value, ctx, diag)) return false;
      wasm_append_byte(body, 0x21);
      wasm_append_u32_leb(body, local_base + 1);
      return true;
    }
    if (ctx && instr->local_index < ctx->fun->local_len && ctx->fun->locals[instr->local_index].type == IR_TYPE_ALLOC) {
      if (!instr->value || instr->value->kind != IR_VALUE_FIXED_BUF_ALLOC) {
        return wasm_diag(diag, "direct wasm FixedBufAlloc local requires std.mem.fixedBufAlloc", instr->line, instr->column, "unsupported allocator initializer");
      }
      unsigned local_base = wasm_local_index(ctx->fun, instr->local_index);
      if (!wasm_emit_byte_view_ptr(body, instr->value->left, ctx, diag)) return false;
      wasm_append_byte(body, 0x21);
      wasm_append_u32_leb(body, local_base);
      if (!wasm_emit_byte_view_len(body, instr->value->left, ctx, diag)) return false;
      wasm_append_byte(body, 0x21);
      wasm_append_u32_leb(body, local_base + 1);
      wasm_append_byte(body, 0x41);
      wasm_append_i32_leb(body, 0);
      wasm_append_byte(body, 0x21);
      wasm_append_u32_leb(body, local_base + 2);
      return true;
    }
    if (ctx && instr->local_index < ctx->fun->local_len && ctx->fun->locals[instr->local_index].type == IR_TYPE_VEC) {
      if (!instr->value || instr->value->kind != IR_VALUE_VEC_INIT) {
        return wasm_diag(diag, "direct wasm Vec local requires std.mem.vec", instr->line, instr->column, "unsupported Vec initializer");
      }
      unsigned local_base = wasm_local_index(ctx->fun, instr->local_index);
      if (!wasm_emit_byte_view_ptr(body, instr->value->left, ctx, diag)) return false;
      wasm_append_byte(body, 0x21);
      wasm_append_u32_leb(body, local_base);
      wasm_append_byte(body, 0x41);
      wasm_append_i32_leb(body, 0);
      wasm_append_byte(body, 0x21);
      wasm_append_u32_leb(body, local_base + 1);
      if (!wasm_emit_byte_view_len(body, instr->value->left, ctx, diag)) return false;
      wasm_append_byte(body, 0x21);
      wasm_append_u32_leb(body, local_base + 2);
      return true;
    }
    if (ctx && instr->local_index < ctx->fun->local_len && ctx->fun->locals[instr->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
      unsigned local_base = wasm_local_index(ctx->fun, instr->local_index);
      if (instr->value && instr->value->kind == IR_VALUE_ARGS_GET) {
        return wasm_emit_wasi_args_get_to_local(body, instr->value, local_base, ctx, diag);
      }
      if (instr->value && instr->value->kind == IR_VALUE_ENV_GET) {
        return wasm_emit_wasi_env_get_to_local(body, instr->value, local_base, ctx, diag);
      }
      if (instr->value && instr->value->kind == IR_VALUE_FS_READ_ALL) {
        return wasm_emit_wasi_fs_read_all_to_local(body, instr->value, local_base, ctx, diag);
      }
      if (instr->value && instr->value->kind == IR_VALUE_FS_TEMP_NAME) {
        return wasm_emit_wasi_fs_temp_name_to_local(body, instr->value, local_base, ctx, diag);
      }
      return wasm_emit_alloc_bytes_to_local(body, instr->value, local_base, ctx, diag);
    }
    if (ctx && instr->local_index < ctx->fun->local_len && ctx->fun->locals[instr->local_index].type == IR_TYPE_MAYBE_SCALAR) {
      unsigned local_base = wasm_local_index(ctx->fun, instr->local_index);
      if (!instr->value) return wasm_diag(diag, "direct wasm Maybe scalar initializer is missing", instr->line, instr->column, "missing maybe value");
      if (instr->value->kind == IR_VALUE_MAYBE_SCALAR_LITERAL) {
        wasm_emit_i32_const(body, instr->value->data_len ? 1 : 0);
        wasm_emit_local_set(body, local_base);
        wasm_emit_i32_const(body, (int32_t)instr->value->int_value);
        wasm_emit_local_set(body, local_base + 1);
        return true;
      }
      if (instr->value->kind == IR_VALUE_JSON_PARSE_BYTES) {
        if (!ctx || ctx->json_result_local_index == (unsigned)-1) {
          return wasm_diag(diag, "direct wasm JSON parse result requires a scratch local", instr->line, instr->column, "missing JSON scratch local");
        }
        if (!ctx->has_allocator || instr->value->local_index >= ctx->fun->local_len || ctx->fun->locals[instr->value->local_index].type != IR_TYPE_ALLOC) {
          return wasm_diag(diag, "direct wasm JSON parse allocator is invalid", instr->line, instr->column, "invalid allocator");
        }
        unsigned alloc_base = wasm_local_index(ctx->fun, instr->value->local_index);
        if (!wasm_emit_value(body, instr->value, ctx, diag)) return false;
        wasm_emit_local_set(body, ctx->json_result_local_index);
        wasm_emit_local_get(body, ctx->json_result_local_index);
        wasm_emit_i64_const(body, 0);
        wasm_append_byte(body, 0x53);
        wasm_append_byte(body, 0x04);
        wasm_append_byte(body, 0x40);
        wasm_emit_maybe_scalar_clear(body, local_base);
        wasm_append_byte(body, 0x05);

        wasm_emit_local_get(body, alloc_base + 2);
        wasm_emit_local_get(body, ctx->json_result_local_index);
        wasm_append_byte(body, 0xa7);
        wasm_append_byte(body, 0x6a);
        wasm_emit_local_set(body, ctx->alloc_next_local_index);

        wasm_emit_local_get(body, ctx->alloc_next_local_index);
        wasm_emit_local_get(body, alloc_base + 1);
        wasm_append_byte(body, 0x4b);
        wasm_append_byte(body, 0x04);
        wasm_append_byte(body, 0x40);
        wasm_emit_maybe_scalar_clear(body, local_base);
        wasm_append_byte(body, 0x05);

        wasm_emit_i32_const(body, 1);
        wasm_emit_local_set(body, local_base);
        wasm_emit_local_get(body, ctx->json_result_local_index);
        wasm_append_byte(body, 0xa7);
        wasm_emit_local_set(body, local_base + 1);
        wasm_emit_local_get(body, ctx->alloc_next_local_index);
        wasm_emit_local_set(body, alloc_base + 2);
        wasm_append_byte(body, 0x0b);
        wasm_append_byte(body, 0x0b);
        return true;
      }
      if (!wasm_emit_value(body, instr->value, ctx, diag)) return false;
      wasm_emit_local_set(body, ctx->fs_result_local_index);
      wasm_emit_local_get(body, ctx->fs_result_local_index);
      wasm_emit_i32_const(body, 0);
      wasm_append_byte(body, 0x48);
      wasm_append_byte(body, 0x04);
      wasm_append_byte(body, 0x40);
      wasm_emit_maybe_scalar_clear(body, local_base);
      wasm_append_byte(body, 0x05);
      wasm_emit_i32_const(body, 1);
      wasm_emit_local_set(body, local_base);
      wasm_emit_local_get(body, ctx->fs_result_local_index);
      wasm_emit_local_set(body, local_base + 1);
      wasm_append_byte(body, 0x0b);
      return true;
    }
    if (!wasm_emit_value(body, instr->value, ctx, diag)) return false;
    wasm_append_byte(body, 0x21);
    wasm_append_u32_leb(body, wasm_local_index(ctx->fun, instr->local_index));
    return true;
  }
  if (instr->kind == IR_INSTR_INDEX_STORE) {
    if (!ctx || instr->array_index >= ctx->fun->local_len) return wasm_diag(diag, "direct wasm indexed store array is out of range", instr->line, instr->column, "invalid array local");
    const IrLocal *local = &ctx->fun->locals[instr->array_index];
    if (!wasm_emit_bounds_checked_address(body, local, instr->index, ctx, diag)) return false;
    if (!wasm_emit_value(body, instr->value, ctx, diag)) return false;
    if (local->element_type == IR_TYPE_U8) {
      wasm_append_byte(body, 0x3a);
      wasm_append_u32_leb(body, 0);
      wasm_append_u32_leb(body, 0);
    } else {
      wasm_append_byte(body, 0x36);
      wasm_append_u32_leb(body, wasm_type_align_log2(local->element_type));
      wasm_append_u32_leb(body, 0);
    }
    return true;
  }
  if (instr->kind == IR_INSTR_FIELD_STORE) {
    if (!ctx || instr->local_index >= ctx->fun->local_len) return wasm_diag(diag, "direct wasm field store record is out of range", instr->line, instr->column, "invalid record local");
    const IrLocal *local = &ctx->fun->locals[instr->local_index];
    if (!local->is_record) return wasm_diag(diag, "direct wasm field store requires record local", instr->line, instr->column, "non-record local");
    wasm_append_byte(body, 0x20);
    wasm_append_u32_leb(body, ctx->frame_local_index);
    wasm_append_byte(body, 0x41);
    wasm_append_i32_leb(body, (int32_t)(wasm_local_memory_offset(local) + instr->field_offset));
    wasm_append_byte(body, 0x6a);
    if (!wasm_emit_value(body, instr->value, ctx, diag)) return false;
    if (instr->value && (instr->value->type == IR_TYPE_U8 || instr->value->type == IR_TYPE_BOOL)) {
      wasm_append_byte(body, 0x3a);
      wasm_append_u32_leb(body, 0);
      wasm_append_u32_leb(body, 0);
    } else if (instr->value && instr->value->type == IR_TYPE_U16) {
      wasm_append_byte(body, 0x3b);
      wasm_append_u32_leb(body, 1);
      wasm_append_u32_leb(body, 0);
    } else {
      wasm_append_byte(body, 0x36);
      wasm_append_u32_leb(body, wasm_type_align_log2(instr->value ? instr->value->type : IR_TYPE_I32));
      wasm_append_u32_leb(body, 0);
    }
    return true;
  }
  if (instr->kind == IR_INSTR_EXPR) {
    if (instr->value && !wasm_emit_value(body, instr->value, ctx, diag)) return false;
    return true;
  }
  if (instr->kind == IR_INSTR_RETURN) {
    if (ctx && ctx->fun->raises) {
      if (!wasm_emit_pack_success_value(body, instr->value, ctx, diag)) return false;
    } else if (instr->value && !wasm_emit_value(body, instr->value, ctx, diag)) {
      return false;
    }
    wasm_emit_frame_restore(body, ctx);
    wasm_append_byte(body, 0x0f);
    return true;
  }
  if (instr->kind == IR_INSTR_RAISE) {
    if (!ctx || !wasm_function_propagates_to_process_exit(ctx->fun)) return wasm_diag(diag, "direct wasm raise requires a fallible function or hosted main exit context", instr->line, instr->column, "non-fallible function");
    if (ctx->fun->raises) {
      wasm_append_byte(body, 0x42);
      wasm_append_i64_leb(body, ((int64_t)(instr->error_code ? instr->error_code : IR_ERROR_UNKNOWN)) << 32);
    } else {
      wasm_append_byte(body, 0x41);
      wasm_append_i32_leb(body, 1);
    }
    wasm_emit_frame_restore(body, ctx);
    wasm_append_byte(body, 0x0f);
    return true;
  }
  if (instr->kind == IR_INSTR_IF) {
    if (!wasm_emit_value(body, instr->value, ctx, diag)) return false;
    wasm_append_byte(body, 0x04);
    wasm_append_byte(body, 0x40);
    if (!wasm_emit_instrs(body, instr->then_instrs, instr->then_len, ctx, diag)) return false;
    if (instr->else_len > 0) {
      wasm_append_byte(body, 0x05);
      if (!wasm_emit_instrs(body, instr->else_instrs, instr->else_len, ctx, diag)) return false;
    }
    wasm_append_byte(body, 0x0b);
    return true;
  }
  if (instr->kind == IR_INSTR_WHILE) {
    wasm_append_byte(body, 0x02);
    wasm_append_byte(body, 0x40);
    wasm_append_byte(body, 0x03);
    wasm_append_byte(body, 0x40);
    if (!wasm_emit_value(body, instr->value, ctx, diag)) return false;
    wasm_append_byte(body, 0x45);
    wasm_append_byte(body, 0x0d);
    wasm_append_u32_leb(body, 1);
    if (!wasm_emit_instrs(body, instr->then_instrs, instr->then_len, ctx, diag)) return false;
    wasm_append_byte(body, 0x0c);
    wasm_append_u32_leb(body, 0);
    wasm_append_byte(body, 0x0b);
    wasm_append_byte(body, 0x0b);
    return true;
  }
  return wasm_diag(diag, "direct wasm instruction kind is unsupported", instr->line, instr->column, "unsupported instruction");
}

static bool wasm_emit_instrs(ZBuf *body, const IrInstr *instrs, size_t len, const WasmEmitContext *ctx, ZDiag *diag) {
  for (size_t i = 0; i < len; i++) {
    if (!wasm_emit_instr(body, &instrs[i], ctx, diag)) return false;
  }
  return true;
}

static bool wasm_emit_function_body(ZBuf *body, const IrProgram *ir, const IrFunction *fun, const WasmImportPlan *imports, ZDiag *diag) {
  bool has_arrays = wasm_function_has_arrays(fun);
  bool has_byte_views = wasm_function_uses_byte_views(fun);
  bool has_byte_compare = wasm_function_uses_byte_compare(fun);
  bool has_byte_mutation = wasm_function_uses_byte_mutation(fun);
  bool has_json_parse_bytes = wasm_function_uses_json_parse_bytes(fun);
  bool has_allocator = wasm_function_uses_allocator(fun);
  bool has_fallibility = wasm_function_uses_fallibility(fun);
  bool has_wasi_args = wasm_function_uses_wasi_args(fun);
  bool has_wasi_env = wasm_function_uses_wasi_env(fun);
  bool has_wasi_fs = wasm_function_uses_wasi_fs(fun);
  bool has_wasi_args_env = has_wasi_args || has_wasi_env;
  unsigned scalar_count = wasm_scalar_local_count(fun);
  unsigned byte_compare_base = scalar_count + (has_arrays ? 2 : 0) + (has_byte_views ? 1 : 0);
  unsigned byte_mutation_base = byte_compare_base + (has_byte_compare ? 5 : 0);
  unsigned json_base = byte_mutation_base + (has_byte_mutation ? 2 : 0);
  unsigned allocator_base = json_base + (has_json_parse_bytes ? 1 : 0);
  unsigned fallibility_base = allocator_base + (has_allocator ? 2 : 0);
  unsigned args_base = fallibility_base + (has_fallibility ? 1 : 0);
  unsigned fs_base = args_base + (has_wasi_args_env ? 3 : 0);
  WasmEmitContext ctx = {
    .ir = ir,
    .fun = fun,
    .has_arrays = has_arrays,
    .has_byte_views = has_byte_views,
    .has_allocator = has_allocator,
    .has_fallibility = has_fallibility,
    .has_wasi_args = has_wasi_args,
    .has_wasi_env = has_wasi_env,
    .has_wasi_fs = has_wasi_fs,
    .imported_function_count = imports ? imports->function_count : 0,
    .zero_json_parse_bytes_import_index = imports ? imports->zero_json_parse_bytes_function_index : (unsigned)-1,
    .fd_write_import_index = imports ? imports->fd_write_function_index : (unsigned)-1,
    .fd_read_import_index = imports ? imports->fd_read_function_index : (unsigned)-1,
    .fd_close_import_index = imports ? imports->fd_close_function_index : (unsigned)-1,
    .path_open_import_index = imports ? imports->path_open_function_index : (unsigned)-1,
    .args_sizes_get_import_index = imports ? imports->args_sizes_get_function_index : (unsigned)-1,
    .args_get_import_index = imports ? imports->args_get_function_index : (unsigned)-1,
    .environ_sizes_get_import_index = imports ? imports->environ_sizes_get_function_index : (unsigned)-1,
    .environ_get_import_index = imports ? imports->environ_get_function_index : (unsigned)-1,
    .clock_time_get_import_index = imports ? imports->clock_time_get_function_index : (unsigned)-1,
    .random_get_import_index = imports ? imports->random_get_function_index : (unsigned)-1,
    .fd_filestat_get_import_index = imports ? imports->fd_filestat_get_function_index : (unsigned)-1,
    .fd_readdir_import_index = imports ? imports->fd_readdir_function_index : (unsigned)-1,
    .path_create_directory_import_index = imports ? imports->path_create_directory_function_index : (unsigned)-1,
    .path_remove_directory_import_index = imports ? imports->path_remove_directory_function_index : (unsigned)-1,
    .path_unlink_file_import_index = imports ? imports->path_unlink_file_function_index : (unsigned)-1,
    .path_rename_import_index = imports ? imports->path_rename_function_index : (unsigned)-1,
    .frame_local_index = scalar_count,
    .index_local_index = scalar_count + 1,
    .byte_index_local_index = scalar_count + (has_arrays ? 2 : 0),
    .byte_cmp_left_ptr_local_index = byte_compare_base,
    .byte_cmp_right_ptr_local_index = byte_compare_base + 1,
    .byte_cmp_len_local_index = byte_compare_base + 2,
    .byte_cmp_i_local_index = byte_compare_base + 3,
    .byte_cmp_result_local_index = byte_compare_base + 4,
    .byte_mut_len_local_index = byte_mutation_base,
    .byte_mut_i_local_index = byte_mutation_base + 1,
    .json_result_local_index = has_json_parse_bytes ? json_base : (unsigned)-1,
    .alloc_len_local_index = allocator_base,
    .alloc_next_local_index = allocator_base + 1,
    .error_result_local_index = fallibility_base,
    .args_index_local_index = args_base,
    .args_ptr_local_index = args_base + 1,
    .args_len_local_index = args_base + 2,
    .fs_fd_local_index = fs_base,
    .fs_result_local_index = fs_base + 1,
    .fs_aux_local_index = fs_base + 2,
    .fs_len_local_index = fs_base + 3,
    .fs_i_local_index = fs_base + 4
  };
  size_t local_decl_count = 0;
  ZBuf local_decls;
  zbuf_init(&local_decls);
  for (size_t i = fun->param_count; i < fun->local_len; i++) {
    if (fun->locals[i].is_array || fun->locals[i].is_record) continue;
    if (fun->locals[i].type == IR_TYPE_BYTE_VIEW) {
      wasm_append_u32_leb(&local_decls, 2);
      wasm_append_byte(&local_decls, 0x7f);
      local_decl_count++;
      continue;
    }
    if (fun->locals[i].type == IR_TYPE_ALLOC || fun->locals[i].type == IR_TYPE_VEC || fun->locals[i].type == IR_TYPE_MAYBE_BYTE_VIEW) {
      wasm_append_u32_leb(&local_decls, 3);
      wasm_append_byte(&local_decls, 0x7f);
      local_decl_count++;
      continue;
    }
    if (fun->locals[i].type == IR_TYPE_MAYBE_SCALAR) {
      wasm_append_u32_leb(&local_decls, 2);
      wasm_append_byte(&local_decls, 0x7f);
      local_decl_count++;
      continue;
    }
    unsigned char value_type = 0;
    wasm_value_type(fun->locals[i].type, &value_type);
    wasm_append_u32_leb(&local_decls, 1);
    wasm_append_byte(&local_decls, value_type);
    local_decl_count++;
  }
  if (has_arrays) {
    wasm_append_u32_leb(&local_decls, 1);
    wasm_append_byte(&local_decls, 0x7f);
    wasm_append_u32_leb(&local_decls, 1);
    wasm_append_byte(&local_decls, 0x7f);
    local_decl_count += 2;
  }
  if (has_byte_views) {
    wasm_append_u32_leb(&local_decls, 1);
    wasm_append_byte(&local_decls, 0x7f);
    local_decl_count++;
  }
  if (has_byte_compare) {
    wasm_append_u32_leb(&local_decls, 5);
    wasm_append_byte(&local_decls, 0x7f);
    local_decl_count++;
  }
  if (has_byte_mutation) {
    wasm_append_u32_leb(&local_decls, 2);
    wasm_append_byte(&local_decls, 0x7f);
    local_decl_count++;
  }
  if (has_json_parse_bytes) {
    wasm_append_u32_leb(&local_decls, 1);
    wasm_append_byte(&local_decls, 0x7e);
    local_decl_count++;
  }
  if (has_allocator) {
    wasm_append_u32_leb(&local_decls, 2);
    wasm_append_byte(&local_decls, 0x7f);
    local_decl_count++;
  }
  if (has_fallibility) {
    wasm_append_u32_leb(&local_decls, 1);
    wasm_append_byte(&local_decls, 0x7e);
    local_decl_count++;
  }
  if (has_wasi_args_env) {
    wasm_append_u32_leb(&local_decls, 3);
    wasm_append_byte(&local_decls, 0x7f);
    local_decl_count++;
  }
  if (has_wasi_fs) {
    wasm_append_u32_leb(&local_decls, 5);
    wasm_append_byte(&local_decls, 0x7f);
    local_decl_count++;
  }
  wasm_append_u32_leb(body, (uint32_t)local_decl_count);
  wasm_append_data(body, (const unsigned char *)local_decls.data, local_decls.len);
  zbuf_free(&local_decls);

  if (has_arrays) {
    wasm_append_byte(body, 0x23);
    wasm_append_u32_leb(body, 0);
    wasm_append_byte(body, 0x41);
    wasm_append_i32_leb(body, (int32_t)fun->frame_bytes);
    wasm_append_byte(body, 0x6b);
    wasm_append_byte(body, 0x22);
    wasm_append_u32_leb(body, ctx.frame_local_index);
    wasm_append_byte(body, 0x24);
    wasm_append_u32_leb(body, 0);
  }

  if (!wasm_emit_instrs(body, fun->instrs, fun->instr_len, &ctx, diag)) return false;
  if (fun->value_return_type == IR_TYPE_VOID) wasm_emit_frame_restore(body, &ctx);
  if (fun->return_type != IR_TYPE_VOID &&
      (fun->instr_len == 0 || fun->instrs[fun->instr_len - 1].kind != IR_INSTR_RETURN)) {
    if (fun->raises) {
      wasm_append_byte(body, 0x42);
      wasm_append_i64_leb(body, 0);
    } else if (fun->value_return_type == IR_TYPE_VOID) {
      wasm_append_byte(body, 0x41);
      wasm_append_i32_leb(body, 0);
    } else {
      wasm_append_byte(body, 0x00);
    }
  }
  wasm_append_byte(body, 0x0b);
  return true;
}

bool z_emit_wasm_from_ir(const IrProgram *ir, ZBuf *out, ZDiag *diag) {
  if (!ir) {
    return wasm_diag(diag, "direct wasm MVP requires at least one exported function", 1, 1, "empty program");
  }
  if (!ir->mir_valid) {
    return wasm_ir_diag(diag, ir);
  }
  if (ir->function_len == 0) {
    return wasm_diag(diag, "direct wasm MVP requires at least one exported function", 1, 1, "empty program");
  }
  bool module_has_arrays = false;
  bool module_uses_zero_json_parse_bytes = false;
  bool module_uses_memory_peek = false;
  bool module_uses_world_write = false;
  bool module_uses_wasi_clock = false;
  bool module_uses_wasi_random = false;
  bool module_uses_wasi_args = false;
  bool module_uses_wasi_env = false;
  bool module_uses_wasi_fs = false;
  bool module_uses_wasi_fd_filestat = false;
  bool module_uses_wasi_fd_readdir = false;
  bool module_uses_wasi_path_create_directory = false;
  bool module_uses_wasi_path_remove_directory = false;
  bool module_uses_wasi_path_unlink_file = false;
  bool module_uses_wasi_path_rename = false;
  for (size_t i = 0; i < ir->function_len; i++) {
    if (wasm_function_has_arrays(&ir->functions[i])) module_has_arrays = true;
    if (wasm_function_uses_json_parse_bytes(&ir->functions[i])) module_uses_zero_json_parse_bytes = true;
    if (wasm_function_uses_memory_peek(&ir->functions[i])) module_uses_memory_peek = true;
    if (wasm_function_uses_world_write(&ir->functions[i])) module_uses_world_write = true;
    if (wasm_function_uses_wasi_clock(&ir->functions[i])) module_uses_wasi_clock = true;
    if (wasm_function_uses_wasi_random(&ir->functions[i])) module_uses_wasi_random = true;
    if (wasm_function_uses_wasi_args(&ir->functions[i])) module_uses_wasi_args = true;
    if (wasm_function_uses_wasi_env(&ir->functions[i])) module_uses_wasi_env = true;
    if (wasm_function_uses_wasi_fs(&ir->functions[i])) module_uses_wasi_fs = true;
    if (wasm_function_uses_wasi_fd_filestat(&ir->functions[i])) module_uses_wasi_fd_filestat = true;
    if (wasm_function_uses_wasi_fd_readdir(&ir->functions[i])) module_uses_wasi_fd_readdir = true;
    if (wasm_function_uses_wasi_path_create_directory(&ir->functions[i])) module_uses_wasi_path_create_directory = true;
    if (wasm_function_uses_wasi_path_remove_directory(&ir->functions[i])) module_uses_wasi_path_remove_directory = true;
    if (wasm_function_uses_wasi_path_unlink_file(&ir->functions[i])) module_uses_wasi_path_unlink_file = true;
    if (wasm_function_uses_wasi_path_rename(&ir->functions[i])) module_uses_wasi_path_rename = true;
  }
  bool module_has_memory = module_has_arrays || module_uses_zero_json_parse_bytes || module_uses_memory_peek || module_uses_world_write || module_uses_wasi_clock || module_uses_wasi_random || module_uses_wasi_args || module_uses_wasi_env || module_uses_wasi_fs || ir->data_segment_len > 0;
  WasmImportPlan imports = {
    .zero_json_parse_bytes = module_uses_zero_json_parse_bytes,
    .fd_write = module_uses_world_write || module_uses_wasi_fs,
    .fd_read = module_uses_wasi_fs,
    .fd_close = module_uses_wasi_fs,
    .path_open = module_uses_wasi_fs,
    .args_sizes_get = module_uses_wasi_args,
    .args_get = module_uses_wasi_args,
    .environ_sizes_get = module_uses_wasi_env,
    .environ_get = module_uses_wasi_env,
    .clock_time_get = module_uses_wasi_clock,
    .random_get = module_uses_wasi_random,
    .fd_filestat_get = module_uses_wasi_fd_filestat,
    .fd_readdir = module_uses_wasi_fd_readdir,
    .path_create_directory = module_uses_wasi_path_create_directory,
    .path_remove_directory = module_uses_wasi_path_remove_directory,
    .path_unlink_file = module_uses_wasi_path_unlink_file,
    .path_rename = module_uses_wasi_path_rename,
    .zero_json_parse_bytes_function_index = (unsigned)-1,
    .fd_write_function_index = (unsigned)-1,
    .fd_read_function_index = (unsigned)-1,
    .fd_close_function_index = (unsigned)-1,
    .path_open_function_index = (unsigned)-1,
    .args_sizes_get_function_index = (unsigned)-1,
    .args_get_function_index = (unsigned)-1,
    .environ_sizes_get_function_index = (unsigned)-1,
    .environ_get_function_index = (unsigned)-1,
    .clock_time_get_function_index = (unsigned)-1,
    .random_get_function_index = (unsigned)-1,
    .fd_filestat_get_function_index = (unsigned)-1,
    .fd_readdir_function_index = (unsigned)-1,
    .path_create_directory_function_index = (unsigned)-1,
    .path_remove_directory_function_index = (unsigned)-1,
    .path_unlink_file_function_index = (unsigned)-1,
    .path_rename_function_index = (unsigned)-1,
    .zero_json_parse_bytes_type_index = (unsigned)-1,
    .fd_write_type_index = (unsigned)-1,
    .fd_read_type_index = (unsigned)-1,
    .fd_close_type_index = (unsigned)-1,
    .path_open_type_index = (unsigned)-1,
    .args_sizes_get_type_index = (unsigned)-1,
    .args_get_type_index = (unsigned)-1,
    .environ_sizes_get_type_index = (unsigned)-1,
    .environ_get_type_index = (unsigned)-1,
    .clock_time_get_type_index = (unsigned)-1,
    .random_get_type_index = (unsigned)-1,
    .fd_filestat_get_type_index = (unsigned)-1,
    .fd_readdir_type_index = (unsigned)-1,
    .path_create_directory_type_index = (unsigned)-1,
    .path_remove_directory_type_index = (unsigned)-1,
    .path_unlink_file_type_index = (unsigned)-1,
    .path_rename_type_index = (unsigned)-1
  };
  if (imports.zero_json_parse_bytes) {
    imports.zero_json_parse_bytes_function_index = imports.function_count++;
    imports.zero_json_parse_bytes_type_index = imports.type_count++;
  }
  if (imports.fd_write) {
    imports.fd_write_function_index = imports.function_count++;
    imports.fd_write_type_index = imports.type_count++;
  }
  if (imports.fd_read) {
    imports.fd_read_function_index = imports.function_count++;
    imports.fd_read_type_index = imports.type_count++;
  }
  if (imports.fd_close) {
    imports.fd_close_function_index = imports.function_count++;
    imports.fd_close_type_index = imports.type_count++;
  }
  if (imports.path_open) {
    imports.path_open_function_index = imports.function_count++;
    imports.path_open_type_index = imports.type_count++;
  }
  if (imports.args_sizes_get) {
    imports.args_sizes_get_function_index = imports.function_count++;
    imports.args_sizes_get_type_index = imports.type_count++;
  }
  if (imports.args_get) {
    imports.args_get_function_index = imports.function_count++;
    imports.args_get_type_index = imports.type_count++;
  }
  if (imports.environ_sizes_get) {
    imports.environ_sizes_get_function_index = imports.function_count++;
    imports.environ_sizes_get_type_index = imports.type_count++;
  }
  if (imports.environ_get) {
    imports.environ_get_function_index = imports.function_count++;
    imports.environ_get_type_index = imports.type_count++;
  }
  if (imports.clock_time_get) {
    imports.clock_time_get_function_index = imports.function_count++;
    imports.clock_time_get_type_index = imports.type_count++;
  }
  if (imports.random_get) {
    imports.random_get_function_index = imports.function_count++;
    imports.random_get_type_index = imports.type_count++;
  }
  if (imports.fd_filestat_get) {
    imports.fd_filestat_get_function_index = imports.function_count++;
    imports.fd_filestat_get_type_index = imports.type_count++;
  }
  if (imports.fd_readdir) {
    imports.fd_readdir_function_index = imports.function_count++;
    imports.fd_readdir_type_index = imports.type_count++;
  }
  if (imports.path_create_directory) {
    imports.path_create_directory_function_index = imports.function_count++;
    imports.path_create_directory_type_index = imports.type_count++;
  }
  if (imports.path_remove_directory) {
    imports.path_remove_directory_function_index = imports.function_count++;
    imports.path_remove_directory_type_index = imports.type_count++;
  }
  if (imports.path_unlink_file) {
    imports.path_unlink_file_function_index = imports.function_count++;
    imports.path_unlink_file_type_index = imports.type_count++;
  }
  if (imports.path_rename) {
    imports.path_rename_function_index = imports.function_count++;
    imports.path_rename_type_index = imports.type_count++;
  }

  zbuf_init(out);
  const unsigned char magic[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
  wasm_append_data(out, magic, sizeof(magic));

  ZBuf types;
  zbuf_init(&types);
  wasm_append_u32_leb(&types, (uint32_t)(ir->function_len + imports.type_count));
  if (imports.zero_json_parse_bytes) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 2);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7e);
  }
  if (imports.fd_write) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 4);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.fd_read) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 4);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.fd_close) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.path_open) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 9);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7e);
    wasm_append_byte(&types, 0x7e);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.args_sizes_get) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 2);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.args_get) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 2);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.environ_sizes_get) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 2);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.environ_get) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 2);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.clock_time_get) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 3);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7e);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.random_get) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 2);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.fd_filestat_get) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 2);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.fd_readdir) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 5);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7e);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.path_create_directory) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 3);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.path_remove_directory) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 3);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.path_unlink_file) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 3);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  if (imports.path_rename) {
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, 6);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_byte(&types, 0x7f);
    wasm_append_u32_leb(&types, 1);
    wasm_append_byte(&types, 0x7f);
  }
  for (size_t i = 0; i < ir->function_len; i++) {
    const IrFunction *fun = &ir->functions[i];
    wasm_append_byte(&types, 0x60);
    wasm_append_u32_leb(&types, (uint32_t)fun->param_count);
    for (size_t p = 0; p < fun->param_count; p++) {
      unsigned char value_type = 0;
      wasm_value_type(fun->locals[p].type, &value_type);
      wasm_append_byte(&types, value_type);
    }
    if (fun->return_type == IR_TYPE_VOID) {
      wasm_append_u32_leb(&types, 0);
    } else {
      unsigned char value_type = 0;
      wasm_value_type(fun->return_type, &value_type);
      wasm_append_u32_leb(&types, 1);
      wasm_append_byte(&types, value_type);
    }
  }
  wasm_append_section(out, 1, &types);
  zbuf_free(&types);

  if (imports.function_count > 0) {
    ZBuf import_section;
    zbuf_init(&import_section);
    wasm_append_u32_leb(&import_section, imports.function_count);
    if (imports.zero_json_parse_bytes) {
      wasm_append_name(&import_section, "zero_runtime");
      wasm_append_name(&import_section, "zero_json_parse_bytes");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.zero_json_parse_bytes_type_index);
    }
    if (imports.fd_write) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "fd_write");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.fd_write_type_index);
    }
    if (imports.fd_read) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "fd_read");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.fd_read_type_index);
    }
    if (imports.fd_close) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "fd_close");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.fd_close_type_index);
    }
    if (imports.path_open) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "path_open");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.path_open_type_index);
    }
    if (imports.args_sizes_get) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "args_sizes_get");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.args_sizes_get_type_index);
    }
    if (imports.args_get) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "args_get");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.args_get_type_index);
    }
    if (imports.environ_sizes_get) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "environ_sizes_get");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.environ_sizes_get_type_index);
    }
    if (imports.environ_get) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "environ_get");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.environ_get_type_index);
    }
    if (imports.clock_time_get) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "clock_time_get");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.clock_time_get_type_index);
    }
    if (imports.random_get) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "random_get");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.random_get_type_index);
    }
    if (imports.fd_filestat_get) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "fd_filestat_get");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.fd_filestat_get_type_index);
    }
    if (imports.fd_readdir) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "fd_readdir");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.fd_readdir_type_index);
    }
    if (imports.path_create_directory) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "path_create_directory");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.path_create_directory_type_index);
    }
    if (imports.path_remove_directory) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "path_remove_directory");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.path_remove_directory_type_index);
    }
    if (imports.path_unlink_file) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "path_unlink_file");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.path_unlink_file_type_index);
    }
    if (imports.path_rename) {
      wasm_append_name(&import_section, "wasi_snapshot_preview1");
      wasm_append_name(&import_section, "path_rename");
      wasm_append_byte(&import_section, 0x00);
      wasm_append_u32_leb(&import_section, imports.path_rename_type_index);
    }
    wasm_append_section(out, 2, &import_section);
    zbuf_free(&import_section);
  }

  ZBuf funcs;
  zbuf_init(&funcs);
  wasm_append_u32_leb(&funcs, (uint32_t)ir->function_len);
  for (size_t i = 0; i < ir->function_len; i++) wasm_append_u32_leb(&funcs, (uint32_t)(i + imports.type_count));
  wasm_append_section(out, 3, &funcs);
  zbuf_free(&funcs);

  if (module_has_memory) {
    ZBuf memory;
    zbuf_init(&memory);
    wasm_append_u32_leb(&memory, 1);
    wasm_append_byte(&memory, 0x00);
    wasm_append_u32_leb(&memory, 1);
    wasm_append_section(out, 5, &memory);
    zbuf_free(&memory);
  }

  if (module_has_arrays) {
    ZBuf globals;
    zbuf_init(&globals);
    wasm_append_u32_leb(&globals, 1);
    wasm_append_byte(&globals, 0x7f);
    wasm_append_byte(&globals, 0x01);
    wasm_append_byte(&globals, 0x41);
    wasm_append_i32_leb(&globals, 65536);
    wasm_append_byte(&globals, 0x0b);
    wasm_append_section(out, 6, &globals);
    zbuf_free(&globals);
  }

  ZBuf exports;
  zbuf_init(&exports);
  size_t export_count = 0;
  if (module_has_memory) export_count++;
  for (size_t i = 0; i < ir->function_len; i++) {
    if (ir->functions[i].is_exported) export_count++;
  }
  wasm_append_u32_leb(&exports, (uint32_t)export_count);
  if (module_has_memory) {
    wasm_append_name(&exports, "memory");
    wasm_append_byte(&exports, 0x02);
    wasm_append_u32_leb(&exports, 0);
  }
  for (size_t i = 0; i < ir->function_len; i++) {
    if (!ir->functions[i].is_exported) continue;
    wasm_append_name(&exports, ir->functions[i].name);
    wasm_append_byte(&exports, 0x00);
    wasm_append_u32_leb(&exports, (uint32_t)(i + imports.function_count));
  }
  wasm_append_section(out, 7, &exports);
  zbuf_free(&exports);

  ZBuf code_section;
  zbuf_init(&code_section);
  wasm_append_u32_leb(&code_section, (uint32_t)ir->function_len);
  for (size_t i = 0; i < ir->function_len; i++) {
    ZBuf body;
    zbuf_init(&body);
    if (!wasm_emit_function_body(&body, ir, &ir->functions[i], &imports, diag)) {
      zbuf_free(&body);
      zbuf_free(&code_section);
      zbuf_free(out);
      return false;
    }
    wasm_append_u32_leb(&code_section, (uint32_t)body.len);
    wasm_append_data(&code_section, (const unsigned char *)body.data, body.len);
    zbuf_free(&body);
  }
  wasm_append_section(out, 10, &code_section);
  zbuf_free(&code_section);

  if (ir->data_segment_len > 0) {
    ZBuf data_section;
    zbuf_init(&data_section);
    wasm_append_u32_leb(&data_section, (uint32_t)ir->data_segment_len);
    for (size_t i = 0; i < ir->data_segment_len; i++) {
      const IrDataSegment *segment = &ir->data_segments[i];
      wasm_append_byte(&data_section, 0x00);
      wasm_append_byte(&data_section, 0x41);
      wasm_append_i32_leb(&data_section, (int32_t)segment->offset);
      wasm_append_byte(&data_section, 0x0b);
      wasm_append_u32_leb(&data_section, segment->len);
      wasm_append_data(&data_section, segment->bytes, segment->len);
    }
    wasm_append_section(out, 11, &data_section);
    zbuf_free(&data_section);
  }
  return true;
}
