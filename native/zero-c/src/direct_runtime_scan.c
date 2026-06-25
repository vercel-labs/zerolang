#include "zero.h"

static bool ir_value_needs_zero_runtime_object(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_JSON_PARSE_BYTES ||
      value->kind == IR_VALUE_JSON_VALIDATE_BYTES ||
      value->kind == IR_VALUE_JSON_STREAM_TOKENS_BYTES ||
      value->kind == IR_VALUE_JSON_DIAGNOSTIC_BYTES ||
      value->kind == IR_VALUE_JSON_FIELD ||
      value->kind == IR_VALUE_JSON_LOOKUP_SCALAR ||
      value->kind == IR_VALUE_JSON_STRING_DECODE ||
      value->kind == IR_VALUE_JSON_STRING_FIELD ||
      value->kind == IR_VALUE_JSON_WRITE_STRING ||
      value->kind == IR_VALUE_JSON_WRITE_RUNTIME ||
      value->kind == IR_VALUE_ASCII_RUNTIME ||
      value->kind == IR_VALUE_TEXT_RUNTIME ||
      value->kind == IR_VALUE_TIME_RUNTIME ||
      value->kind == IR_VALUE_TERM_RUNTIME ||
      value->kind == IR_VALUE_TIME_WALL_SECONDS ||
      value->kind == IR_VALUE_TIME_MONOTONIC ||
      value->kind == IR_VALUE_RAND_ENTROPY_U32 ||
      value->kind == IR_VALUE_MATH_RUNTIME ||
      value->kind == IR_VALUE_SEARCH_RUNTIME ||
      value->kind == IR_VALUE_SORT_RUNTIME ||
      value->kind == IR_VALUE_STR_CONTAINS ||
      value->kind == IR_VALUE_STR_RUNTIME ||
      value->kind == IR_VALUE_ENV_GET ||
      value->kind == IR_VALUE_ARGS_LEN ||
      value->kind == IR_VALUE_ARGS_GET ||
      value->kind == IR_VALUE_ARGS_EQ ||
      value->kind == IR_VALUE_ARGS_GET_OR ||
      value->kind == IR_VALUE_ARGS_FIND ||
      value->kind == IR_VALUE_ARGS_CONTAINS ||
      value->kind == IR_VALUE_ARGS_VALUE_AFTER ||
      value->kind == IR_VALUE_ARGS_VALUE_AFTER_OR ||
      value->kind == IR_VALUE_ARGS_VALUE_AFTER_PARSE_U32 ||
      value->kind == IR_VALUE_PARSE_RUNTIME ||
      value->kind == IR_VALUE_PARSE_I32 ||
      value->kind == IR_VALUE_PARSE_U32 ||
      value->kind == IR_VALUE_ARGS_PARSE_U32 ||
      value->kind == IR_VALUE_FMT_BOOL ||
      value->kind == IR_VALUE_FMT_HEX_U32 ||
      value->kind == IR_VALUE_FMT_I32 ||
      value->kind == IR_VALUE_FMT_U32 ||
      value->kind == IR_VALUE_FMT_USIZE ||
      value->kind == IR_VALUE_FS_READ_BYTES_PATH ||
      value->kind == IR_VALUE_FS_READ_BYTES_AT_PATH ||
      value->kind == IR_VALUE_FS_WRITE_BYTES_PATH ||
      value->kind == IR_VALUE_FS_APPEND_BYTES_PATH ||
      value->kind == IR_VALUE_FS_EXISTS ||
      value->kind == IR_VALUE_FS_REMOVE ||
      value->kind == IR_VALUE_FS_RENAME ||
      value->kind == IR_VALUE_FS_MAKE_DIR ||
      value->kind == IR_VALUE_FS_REMOVE_DIR ||
      value->kind == IR_VALUE_FS_IS_DIR ||
      value->kind == IR_VALUE_FS_DIR_ENTRY_COUNT ||
      value->kind == IR_VALUE_FS_DIR_ENTRY_NAME ||
      value->kind == IR_VALUE_FS_ATOMIC_WRITE ||
      value->kind == IR_VALUE_PROC_SPAWN_INHERIT ||
      value->kind == IR_VALUE_PROC_CAPTURE ||
      value->kind == IR_VALUE_PROC_CAPTURE_FILES ||
      value->kind == IR_VALUE_PROC_CHILD_SPAWN ||
      value->kind == IR_VALUE_PROC_CHILD_OP ||
      value->kind == IR_VALUE_PROC_CHILD_IO ||
      value->kind == IR_VALUE_PROC_PTY_RESIZE ||
      value->kind == IR_VALUE_HTTP_FETCH ||
      value->kind == IR_VALUE_HTTP_RESULT_OK ||
      value->kind == IR_VALUE_HTTP_RESULT_STATUS ||
      value->kind == IR_VALUE_HTTP_RESULT_BODY_LEN ||
      value->kind == IR_VALUE_HTTP_RESULT_ERROR ||
      value->kind == IR_VALUE_HTTP_RESPONSE_LEN ||
      value->kind == IR_VALUE_HTTP_RESPONSE_HEADERS_LEN ||
      value->kind == IR_VALUE_HTTP_RESPONSE_BODY_OFFSET ||
      value->kind == IR_VALUE_HTTP_HEADER_VALUE ||
      value->kind == IR_VALUE_HTTP_HEADER_FOUND ||
      value->kind == IR_VALUE_HTTP_HEADER_OFFSET ||
      value->kind == IR_VALUE_HTTP_HEADER_LEN ||
      value->kind == IR_VALUE_HTTP_WRITE_JSON_RESPONSE ||
      value->kind == IR_VALUE_HTTP_REQUEST_METHOD_NAME ||
      value->kind == IR_VALUE_HTTP_REQUEST_MATCHES ||
      value->kind == IR_VALUE_HTTP_REQUEST_BODY_WITHIN ||
      value->kind == IR_VALUE_HTTP_REQUEST_PATH) return true;
  if (ir_value_needs_zero_runtime_object(value->index) ||
      ir_value_needs_zero_runtime_object(value->left) ||
      ir_value_needs_zero_runtime_object(value->right)) {
    return true;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (ir_value_needs_zero_runtime_object(value->args[i])) return true;
  }
  return false;
}

static bool ir_instrs_need_zero_runtime_object(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; instrs && i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (ir_value_needs_zero_runtime_object(instr->value) ||
        ir_value_needs_zero_runtime_object(instr->index) ||
        ir_instrs_need_zero_runtime_object(instr->then_instrs, instr->then_len) ||
        ir_instrs_need_zero_runtime_object(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

bool z_ir_needs_zero_runtime_object(const IrProgram *ir) {
  if (!ir) return false;
  for (size_t i = 0; i < ir->function_len; i++) {
    if (ir_instrs_need_zero_runtime_object(ir->functions[i].instrs, ir->functions[i].instr_len)) return true;
  }
  return false;
}
