#include "capability_summary.h"

void z_capability_summary_merge(CapabilitySummary *caps, const CapabilitySummary *other) {
  if (!caps || !other) return;
  caps->args = caps->args || other->args;
  caps->env = caps->env || other->env;
  caps->fs = caps->fs || other->fs;
  caps->memory = caps->memory || other->memory;
  caps->alloc = caps->alloc || other->alloc;
  caps->path = caps->path || other->path;
  caps->codec = caps->codec || other->codec;
  caps->parse = caps->parse || other->parse;
  caps->time = caps->time || other->time;
  caps->rand = caps->rand || other->rand;
  caps->net = caps->net || other->net;
  caps->proc = caps->proc || other->proc;
  caps->web = caps->web || other->web;
  caps->world = caps->world || other->world;
}

static void ir_value_kind_capabilities(IrValueKind kind, CapabilitySummary *caps) {
  switch (kind) {
    case IR_VALUE_STRING_LITERAL:
    case IR_VALUE_ARRAY_BYTE_VIEW:
    case IR_VALUE_BYTE_SLICE:
    case IR_VALUE_BYTE_VIEW_LEN:
    case IR_VALUE_BYTE_VIEW_REMAINING:
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD:
    case IR_VALUE_BYTE_VIEW_EQ:
    case IR_VALUE_STR_CONTAINS:
    case IR_VALUE_STR_RUNTIME:
    case IR_VALUE_FMT_BOOL:
    case IR_VALUE_FMT_HEX_U32:
    case IR_VALUE_FMT_I32:
    case IR_VALUE_FMT_U32:
    case IR_VALUE_FMT_USIZE:
    case IR_VALUE_BYTE_COPY:
    case IR_VALUE_BYTE_FILL:
    case IR_VALUE_ITEM_COPY:
    case IR_VALUE_ITEM_FILL:
    case IR_VALUE_ITEM_CONTAINS:
    case IR_VALUE_FIXED_BUF_ALLOC:
    case IR_VALUE_VEC_INIT:
    case IR_VALUE_VEC_PUSH:
    case IR_VALUE_VEC_LEN:
    case IR_VALUE_VEC_CAPACITY:
    case IR_VALUE_VEC_BYTES:
    case IR_VALUE_VEC_GET:
    case IR_VALUE_VEC_SET:
    case IR_VALUE_VEC_CLEAR:
    case IR_VALUE_VEC_POP:
    case IR_VALUE_VEC_TRUNCATE:
    case IR_VALUE_VEC_REMOVE_SWAP:
    case IR_VALUE_VEC_INDEX:
    case IR_VALUE_VEC_CONTAINS:
    case IR_VALUE_VEC_INSERT_UNIQUE:
    case IR_VALUE_VEC_REMOVE_VALUE:
      caps->memory = true;
      break;
    case IR_VALUE_ALLOC_BYTES:
      caps->memory = true;
      caps->alloc = true;
      break;
    case IR_VALUE_ASCII_RUNTIME:
    case IR_VALUE_TEXT_RUNTIME:
    case IR_VALUE_PARSE_RUNTIME:
    case IR_VALUE_PARSE_I32:
    case IR_VALUE_PARSE_U32:
      caps->parse = true;
      break;
    case IR_VALUE_ARGS_LEN:
    case IR_VALUE_ARGS_GET:
    case IR_VALUE_ARGS_EQ:
    case IR_VALUE_ARGS_GET_OR:
    case IR_VALUE_ARGS_FIND:
    case IR_VALUE_ARGS_CONTAINS:
    case IR_VALUE_ARGS_VALUE_AFTER:
    case IR_VALUE_ARGS_VALUE_AFTER_OR:
      caps->args = true;
      break;
    case IR_VALUE_ARGS_PARSE_U32:
    case IR_VALUE_ARGS_VALUE_AFTER_PARSE_U32:
      caps->args = true;
      caps->parse = true;
      break;
    case IR_VALUE_ENV_GET:
      caps->env = true;
      break;
    case IR_VALUE_TIME_RUNTIME:
    case IR_VALUE_TIME_WALL_SECONDS:
    case IR_VALUE_TIME_MONOTONIC:
    case IR_VALUE_TIME_AS_MS:
      caps->time = true;
      break;
    case IR_VALUE_RAND_NEXT_U32:
    case IR_VALUE_RAND_NEXT_BELOW:
    case IR_VALUE_RAND_RANGE_U32:
    case IR_VALUE_RAND_ENTROPY_U32:
      caps->rand = true;
      break;
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
      caps->fs = true;
      caps->path = true;
      break;
    case IR_VALUE_JSON_PARSE_BYTES:
    case IR_VALUE_JSON_VALIDATE_BYTES:
    case IR_VALUE_JSON_STREAM_TOKENS_BYTES:
    case IR_VALUE_JSON_DIAGNOSTIC_BYTES:
    case IR_VALUE_JSON_FIELD:
    case IR_VALUE_JSON_LOOKUP_SCALAR:
    case IR_VALUE_JSON_STRING_DECODE:
    case IR_VALUE_JSON_STRING_FIELD:
    case IR_VALUE_JSON_WRITE_STRING:
    case IR_VALUE_JSON_WRITE_RUNTIME:
    case IR_VALUE_JSON_ERROR_LABEL:
    case IR_VALUE_CRC32_BYTES:
      caps->codec = true;
      caps->memory = true;
      break;
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
    case IR_VALUE_HTTP_WRITE_JSON_RESPONSE:
    case IR_VALUE_HTTP_REQUEST_METHOD_NAME: case IR_VALUE_HTTP_REQUEST_PATH:
    case IR_VALUE_HTTP_REQUEST_MATCHES: case IR_VALUE_HTTP_REQUEST_BODY_WITHIN:
    case IR_VALUE_HTTP_STATUS_CLASS:
      caps->net = true;
      caps->web = true;
      break;
    default: break;
  }
}

static void ir_value_capabilities(const IrValue *value, CapabilitySummary *caps) {
  if (!value || !caps) return;
  ir_value_kind_capabilities(value->kind, caps);
  ir_value_capabilities(value->index, caps);
  ir_value_capabilities(value->left, caps);
  ir_value_capabilities(value->right, caps);
  for (size_t i = 0; i < value->arg_len; i++) ir_value_capabilities(value->args[i], caps);
}

static void ir_instr_capabilities(const IrInstr *instrs, size_t len, CapabilitySummary *caps) {
  if (!caps) return;
  for (size_t i = 0; instrs && i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (instr->kind == IR_INSTR_WORLD_WRITE) caps->world = true;
    ir_value_capabilities(instr->value, caps);
    ir_value_capabilities(instr->index, caps);
    ir_instr_capabilities(instr->then_instrs, instr->then_len, caps);
    ir_instr_capabilities(instr->else_instrs, instr->else_len, caps);
  }
}

CapabilitySummary z_ir_program_capabilities(const IrProgram *ir) {
  CapabilitySummary caps = {0};
  for (size_t i = 0; ir && i < ir->function_len; i++) {
    const IrFunction *fun = &ir->functions[i];
    if (fun->world_param_name) caps.world = true;
    for (size_t local_index = 0; local_index < fun->local_len; local_index++) {
      const IrLocal *local = &fun->locals[local_index];
      if (local->is_array || local->is_record) caps.memory = true;
    }
    ir_instr_capabilities(fun->instrs, fun->instr_len, &caps);
  }
  return caps;
}
