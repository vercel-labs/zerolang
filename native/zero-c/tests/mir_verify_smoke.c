#include "mir_verify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void z_backend_blocker_set(ZBackendBlocker *blocker, const char *target, const char *object_format, const char *backend, const char *stage, const char *unsupported_feature) {
  if (!blocker) return;
  memset(blocker, 0, sizeof(*blocker));
  blocker->present = true;
  snprintf(blocker->target, sizeof(blocker->target), "%s", target ? target : "");
  snprintf(blocker->object_format, sizeof(blocker->object_format), "%s", object_format ? object_format : "");
  snprintf(blocker->backend, sizeof(blocker->backend), "%s", backend ? backend : "");
  snprintf(blocker->stage, sizeof(blocker->stage), "%s", stage ? stage : "");
  snprintf(blocker->unsupported_feature, sizeof(blocker->unsupported_feature), "%s", unsupported_feature ? unsupported_feature : "");
}

static unsigned smoke_type_byte_size(IrTypeKind type) {
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

static unsigned smoke_type_alignment(IrTypeKind type) {
  unsigned size = smoke_type_byte_size(type);
  return size >= 4 ? 4 : (size ? size : 1);
}

static IrLocal scalar_local(const char *name, IrTypeKind type, unsigned index, bool is_param) {
  unsigned byte_size = type == IR_TYPE_BYTE_VIEW || type == IR_TYPE_ALLOC || type == IR_TYPE_VEC || type == IR_TYPE_MAYBE_SCALAR ? 16 : (type == IR_TYPE_MAYBE_BYTE_VIEW ? 24 : 8);
  unsigned frame_offset = (index + 1) * 16;
  if (frame_offset < byte_size) frame_offset = byte_size;
  return (IrLocal){
    .name = (char *)name,
    .type = type,
    .index = index,
    .frame_offset = frame_offset,
    .byte_size = byte_size,
    .alignment = 8,
    .is_param = is_param,
    .line = 1,
    .column = 1
  };
}

static IrLocal array_local(const char *name, IrTypeKind element_type, unsigned index) {
  unsigned byte_size = smoke_type_byte_size(element_type) * 4;
  unsigned frame_offset = byte_size > 8 ? byte_size : (index + 1) * 8;
  return (IrLocal){
    .name = (char *)name,
    .type = IR_TYPE_UNSUPPORTED,
    .element_type = element_type,
    .index = index,
    .frame_offset = frame_offset,
    .array_len = 4,
    .byte_size = byte_size,
    .alignment = smoke_type_alignment(element_type),
    .is_array = true,
    .line = 1,
    .column = 1
  };
}

static IrLocal record_local(const char *name, unsigned index) {
  return (IrLocal){
    .name = (char *)name,
    .type = IR_TYPE_RECORD,
    .index = index,
    .frame_offset = (index + 1) * 16,
    .byte_size = 16,
    .alignment = 8,
    .is_record = true,
    .line = 1,
    .column = 1
  };
}

static IrValue value(IrValueKind kind, IrTypeKind type) {
  return (IrValue){
    .kind = kind,
    .type = type,
    .line = 1,
    .column = 1
  };
}

static IrValue byte_view_value(void) {
  return value(IR_VALUE_STRING_LITERAL, IR_TYPE_BYTE_VIEW);
}

static IrFunction function(const char *name, IrTypeKind return_type, IrTypeKind value_return_type, IrLocal *locals, size_t local_len, size_t param_count, IrInstr *instrs, size_t instr_len, size_t frame_bytes, bool raises) {
  return (IrFunction){
    .name = (char *)name,
    .return_type = return_type,
    .value_return_type = value_return_type,
    .locals = locals,
    .local_len = local_len,
    .param_count = param_count,
    .instrs = instrs,
    .instr_len = instr_len,
    .frame_bytes = frame_bytes,
    .raises = raises,
    .line = 1,
    .column = 1
  };
}

static IrProgram program(IrFunction *functions, size_t function_len) {
  return (IrProgram){
    .functions = functions,
    .function_len = function_len,
    .mir_valid = true
  };
}

static void expect_ok(const char *name, IrProgram *ir) {
  if (!z_mir_verify_direct_contracts(ir) || !ir->mir_valid) {
    fprintf(stderr, "%s: expected verifier success, got %s\n", name, ir->mir_message);
    exit(1);
  }
}

static void expect_fail(const char *name, IrProgram *ir, const char *message) {
  if (z_mir_verify_direct_contracts(ir) || ir->mir_valid) {
    fprintf(stderr, "%s: expected verifier failure\n", name);
    exit(1);
  }
  if (!strstr(ir->mir_message, message)) {
    fprintf(stderr, "%s: expected message containing '%s', got '%s'\n", name, message, ir->mir_message);
    exit(1);
  }
  if (!ir->backend_blocker.present || strcmp(ir->backend_blocker.stage, "lower") != 0) {
    fprintf(stderr, "%s: expected lower-stage backend blocker\n", name);
    exit(1);
  }
}

static void valid_direct_call_passes(void) {
  IrValue arg = value(IR_VALUE_INT, IR_TYPE_I32);
  IrValue *args[] = {&arg};
  IrValue call = value(IR_VALUE_CALL, IR_TYPE_I32);
  call.element_type = IR_TYPE_I32;
  call.callee_index = 1;
  call.args = args;
  call.arg_len = 1;
  IrInstr return_call = {.kind = IR_INSTR_RETURN, .value = &call, .line = 1, .column = 1};
  IrFunction caller = function("main", IR_TYPE_I32, IR_TYPE_I32, NULL, 0, 0, &return_call, 1, 0, false);
  IrLocal callee_locals[] = {scalar_local("x", IR_TYPE_I32, 0, true)};
  IrFunction callee = function("id", IR_TYPE_I32, IR_TYPE_I32, callee_locals, 1, 1, NULL, 0, 16, false);
  IrFunction functions[] = {caller, callee};
  IrProgram ir = program(functions, 2);
  expect_ok("valid direct call", &ir);
}

static void local_value_out_of_range_fails(void) {
  IrValue local = value(IR_VALUE_LOCAL, IR_TYPE_I32);
  local.local_index = 0;
  IrInstr return_local = {.kind = IR_INSTR_RETURN, .value = &local, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_I32, IR_TYPE_I32, NULL, 0, 0, &return_local, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("local value out of range", &ir, "local value outside the local table");
}

static void local_write_type_mismatch_fails(void) {
  IrLocal locals[] = {scalar_local("x", IR_TYPE_I32, 0, false)};
  IrValue boolean = value(IR_VALUE_BOOL, IR_TYPE_BOOL);
  IrInstr set = {.kind = IR_INSTR_LOCAL_SET, .local_index = 0, .value = &boolean, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 1, 0, &set, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("local write type mismatch", &ir, "local write type mismatch");
}

static void packed_maybe_scalar_write_passes(void) {
  IrLocal locals[] = {
    scalar_local("alloc", IR_TYPE_ALLOC, 0, false),
    scalar_local("parsed", IR_TYPE_MAYBE_SCALAR, 1, false)
  };
  locals[0].is_mutable = true;
  IrValue bytes = byte_view_value();
  IrValue packed = value(IR_VALUE_JSON_PARSE_BYTES, IR_TYPE_I64);
  packed.local_index = 0;
  packed.left = &bytes;
  IrInstr set = {.kind = IR_INSTR_LOCAL_SET, .local_index = 1, .value = &packed, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 2, 0, &set, 1, 48, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_allocator_helper_count = 2;
  ir.direct_runtime_helper_count = 1;
  ir.direct_host_runtime_import_count = 1;
  expect_ok("packed maybe scalar write", &ir);
}

static void return_type_mismatch_fails(void) {
  IrValue boolean = value(IR_VALUE_BOOL, IR_TYPE_BOOL);
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &boolean, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_I32, IR_TYPE_I32, NULL, 0, 0, &ret, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("return type mismatch", &ir, "return value mismatch");
}

static void branch_condition_mismatch_fails(void) {
  IrValue number = value(IR_VALUE_INT, IR_TYPE_I32);
  IrInstr branch = {.kind = IR_INSTR_IF, .value = &number, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, NULL, 0, 0, &branch, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("branch condition mismatch", &ir, "invalid branch condition");
}

static void unsupported_instruction_kind_fails(void) {
  IrInstr instr = {.kind = (IrInstrKind)999, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, NULL, 0, 0, &instr, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("unsupported instruction kind", &ir, "unsupported instruction kind");
}

static void array_write_contract_fails(void) {
  IrLocal locals[] = {scalar_local("x", IR_TYPE_I32, 0, false)};
  IrValue index = value(IR_VALUE_INT, IR_TYPE_USIZE);
  IrValue item = value(IR_VALUE_INT, IR_TYPE_U8);
  IrInstr store = {.kind = IR_INSTR_INDEX_STORE, .array_index = 0, .index = &index, .value = &item, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 1, 0, &store, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("array write to scalar", &ir, "indexed write to an unsupported local");
}

static void array_write_type_mismatch_fails(void) {
  IrLocal locals[] = {array_local("bytes", IR_TYPE_U8, 0)};
  IrValue index = value(IR_VALUE_INT, IR_TYPE_USIZE);
  IrValue item = value(IR_VALUE_INT, IR_TYPE_I32);
  IrInstr store = {.kind = IR_INSTR_INDEX_STORE, .array_index = 0, .index = &index, .value = &item, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 1, 0, &store, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("array write type mismatch", &ir, "indexed write type mismatch");
}

static void byte_view_write_contract_passes(void) {
  IrLocal locals[] = {scalar_local("bytes", IR_TYPE_BYTE_VIEW, 0, false)};
  IrValue index = value(IR_VALUE_INT, IR_TYPE_USIZE);
  IrValue item = value(IR_VALUE_INT, IR_TYPE_U8);
  IrInstr store = {.kind = IR_INSTR_INDEX_STORE, .array_index = 0, .index = &index, .value = &item, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 1, 0, &store, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  expect_ok("byte view write contract", &ir);
}

static void array_load_contract_fails(void) {
  IrLocal locals[] = {scalar_local("x", IR_TYPE_I32, 0, false)};
  IrValue index = value(IR_VALUE_INT, IR_TYPE_USIZE);
  IrValue load = value(IR_VALUE_INDEX_LOAD, IR_TYPE_I32);
  load.array_index = 0;
  load.index = &index;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &load, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_I32, IR_TYPE_I32, locals, 1, 0, &ret, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("array load from scalar", &ir, "array load from a non-array local");
}

static void array_load_type_mismatch_fails(void) {
  IrLocal locals[] = {array_local("bytes", IR_TYPE_U8, 0)};
  IrValue index = value(IR_VALUE_INT, IR_TYPE_USIZE);
  IrValue load = value(IR_VALUE_INDEX_LOAD, IR_TYPE_I32);
  load.array_index = 0;
  load.index = &index;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &load, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_I32, IR_TYPE_I32, locals, 1, 0, &ret, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("array load type mismatch", &ir, "array load type mismatch");
}

static void array_byte_view_contract_fails(void) {
  IrLocal locals[] = {array_local("numbers", IR_TYPE_I32, 0)};
  IrValue view = value(IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW);
  view.array_index = 0;
  view.data_len = 4;
  IrInstr write = {.kind = IR_INSTR_WORLD_WRITE, .value = &view, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 1, 0, &write, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_runtime_helper_count = 1;
  expect_fail("array byte view from non-byte array", &ir, "array byte view from a non-byte array local");
}

static void array_byte_view_length_mismatch_fails(void) {
  IrLocal locals[] = {array_local("bytes", IR_TYPE_U8, 0)};
  IrValue view = value(IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW);
  view.array_index = 0;
  view.data_len = 3;
  IrInstr write = {.kind = IR_INSTR_WORLD_WRITE, .value = &view, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 1, 0, &write, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_runtime_helper_count = 1;
  expect_fail("array byte view length mismatch", &ir, "array byte view length mismatch");
}

static void field_write_contract_fails(void) {
  IrLocal locals[] = {record_local("point", 0)};
  IrValue item = value(IR_VALUE_INT, IR_TYPE_I32);
  IrInstr store = {.kind = IR_INSTR_FIELD_STORE, .local_index = 0, .field_offset = 32, .value = &item, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 1, 0, &store, 1, 32, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("field write out of range", &ir, "field write outside the local storage");
}

static void field_write_partial_overrun_fails(void) {
  IrLocal locals[] = {record_local("point", 0)};
  IrValue item = value(IR_VALUE_INT, IR_TYPE_I64);
  IrInstr store = {.kind = IR_INSTR_FIELD_STORE, .local_index = 0, .field_offset = 12, .value = &item, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 1, 0, &store, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("field write partial overrun", &ir, "field write outside the local storage");
}

static void field_load_partial_overrun_fails(void) {
  IrLocal locals[] = {record_local("point", 0)};
  IrValue load = value(IR_VALUE_FIELD_LOAD, IR_TYPE_I64);
  load.local_index = 0;
  load.field_offset = 12;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &load, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_I64, IR_TYPE_I64, locals, 1, 0, &ret, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("field load partial overrun", &ir, "field load outside the local storage");
}

static void overlapping_frame_fails(void) {
  IrLocal locals[] = {
    scalar_local("a", IR_TYPE_I32, 0, false),
    scalar_local("b", IR_TYPE_I32, 1, false)
  };
  locals[1].frame_offset = 8;
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 2, 0, NULL, 0, 16, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("overlapping frame", &ir, "overlapping local storage");
}

static void small_scalar_slot_fails(void) {
  IrLocal locals[] = {scalar_local("flag", IR_TYPE_BOOL, 0, false)};
  locals[0].byte_size = 1;
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 1, 0, NULL, 0, 16, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("small scalar slot", &ir, "invalid local storage layout");
}

static void raise_in_non_fallible_function_fails(void) {
  IrInstr raise = {.kind = IR_INSTR_RAISE, .error_code = IR_ERROR_UNKNOWN, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, NULL, 0, 0, &raise, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("raise in non-fallible function", &ir, "raise in a non-fallible function");
}

static void raise_in_hosted_world_main_passes(void) {
  IrInstr raise = {.kind = IR_INSTR_RAISE, .error_code = IR_ERROR_UNKNOWN, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_I32, IR_TYPE_VOID, NULL, 0, 0, &raise, 1, 0, false);
  fun.world_param_name = "world";
  IrProgram ir = program(&fun, 1);
  expect_ok("raise in hosted world main", &ir);
}

static void allocator_helper_contract_fails(void) {
  IrLocal locals[] = {
    array_local("storage", IR_TYPE_U8, 0),
    scalar_local("alloc", IR_TYPE_ALLOC, 1, false)
  };
  locals[0].is_mutable = true;
  IrValue bytes = value(IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW);
  bytes.array_index = 0;
  bytes.data_len = 4;
  IrValue alloc = value(IR_VALUE_FIXED_BUF_ALLOC, IR_TYPE_ALLOC);
  alloc.left = &bytes;
  IrInstr set = {.kind = IR_INSTR_LOCAL_SET, .local_index = 1, .value = &alloc, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 2, 0, &set, 1, 48, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("allocator helper contract", &ir, "missing allocator helper contract");
}

static void buffer_helper_contract_fails(void) {
  IrLocal locals[] = {
    array_local("storage", IR_TYPE_U8, 0),
    scalar_local("vec", IR_TYPE_VEC, 1, false)
  };
  locals[0].is_mutable = true;
  IrValue bytes = value(IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW);
  bytes.array_index = 0;
  bytes.data_len = 4;
  IrValue vec = value(IR_VALUE_VEC_INIT, IR_TYPE_VEC);
  vec.left = &bytes;
  IrInstr set = {.kind = IR_INSTR_LOCAL_SET, .local_index = 1, .value = &vec, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 2, 0, &set, 1, 48, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("buffer helper contract", &ir, "missing buffer helper contract");
}

static void alloc_local_invalid_initializer_fails(void) {
  IrLocal locals[] = {
    scalar_local("dst", IR_TYPE_ALLOC, 0, false),
    scalar_local("src", IR_TYPE_ALLOC, 1, false)
  };
  IrValue src = value(IR_VALUE_LOCAL, IR_TYPE_ALLOC);
  src.local_index = 1;
  IrInstr set = {.kind = IR_INSTR_LOCAL_SET, .local_index = 0, .value = &src, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 2, 0, &set, 1, 48, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("Alloc local invalid initializer", &ir, "invalid local initializer");
}

static void vec_local_invalid_initializer_fails(void) {
  IrLocal locals[] = {
    scalar_local("dst", IR_TYPE_VEC, 0, false),
    scalar_local("src", IR_TYPE_VEC, 1, false)
  };
  IrValue src = value(IR_VALUE_LOCAL, IR_TYPE_VEC);
  src.local_index = 1;
  IrInstr set = {.kind = IR_INSTR_LOCAL_SET, .local_index = 0, .value = &src, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 2, 0, &set, 1, 48, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("Vec local invalid initializer", &ir, "invalid local initializer");
}

static void fixed_buf_alloc_readonly_storage_fails(void) {
  IrLocal locals[] = {scalar_local("alloc", IR_TYPE_ALLOC, 0, false)};
  IrValue bytes = byte_view_value();
  IrValue alloc = value(IR_VALUE_FIXED_BUF_ALLOC, IR_TYPE_ALLOC);
  alloc.left = &bytes;
  IrInstr set = {.kind = IR_INSTR_LOCAL_SET, .local_index = 0, .value = &alloc, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 1, 0, &set, 1, 32, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_allocator_helper_count = 1;
  expect_fail("FixedBufAlloc read-only storage", &ir, "invalid FixedBufAlloc helper value");
}

static void vec_init_immutable_storage_fails(void) {
  IrLocal locals[] = {
    array_local("storage", IR_TYPE_U8, 0),
    scalar_local("vec", IR_TYPE_VEC, 1, false)
  };
  IrValue bytes = value(IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW);
  bytes.array_index = 0;
  bytes.data_len = 4;
  IrValue vec = value(IR_VALUE_VEC_INIT, IR_TYPE_VEC);
  vec.left = &bytes;
  IrInstr set = {.kind = IR_INSTR_LOCAL_SET, .local_index = 1, .value = &vec, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 2, 0, &set, 1, 48, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_buffer_helper_count = 1;
  expect_fail("Vec init immutable storage", &ir, "invalid Vec helper value");
}

static void alloc_bytes_immutable_allocator_fails(void) {
  IrLocal locals[] = {
    scalar_local("alloc", IR_TYPE_ALLOC, 0, false),
    scalar_local("bytes", IR_TYPE_MAYBE_BYTE_VIEW, 1, false)
  };
  locals[1].frame_offset = 40;
  IrValue length = value(IR_VALUE_INT, IR_TYPE_USIZE);
  IrValue bytes = value(IR_VALUE_ALLOC_BYTES, IR_TYPE_MAYBE_BYTE_VIEW);
  bytes.local_index = 0;
  bytes.left = &length;
  IrInstr set = {.kind = IR_INSTR_LOCAL_SET, .local_index = 1, .value = &bytes, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 2, 0, &set, 1, 64, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_allocator_helper_count = 2;
  expect_fail("AllocBytes immutable allocator", &ir, "invalid allocation helper target");
}

static void vec_push_immutable_vec_fails(void) {
  IrLocal locals[] = {scalar_local("vec", IR_TYPE_VEC, 0, false)};
  IrValue item = value(IR_VALUE_INT, IR_TYPE_U8);
  IrValue push = value(IR_VALUE_VEC_PUSH, IR_TYPE_BOOL);
  push.local_index = 0;
  push.left = &item;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &push, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_BOOL, IR_TYPE_BOOL, locals, 1, 0, &ret, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_buffer_helper_count = 2;
  expect_fail("Vec push immutable Vec", &ir, "invalid Vec helper target");
}

static void json_parse_immutable_allocator_fails(void) {
  IrLocal locals[] = {scalar_local("alloc", IR_TYPE_ALLOC, 0, false)};
  IrValue bytes = byte_view_value();
  IrValue parsed = value(IR_VALUE_JSON_PARSE_BYTES, IR_TYPE_I64);
  parsed.local_index = 0;
  parsed.left = &bytes;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &parsed, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_I64, IR_TYPE_I64, locals, 1, 0, &ret, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_allocator_helper_count = 2;
  ir.direct_runtime_helper_count = 1;
  ir.direct_host_runtime_import_count = 1;
  expect_fail("JSON parse immutable allocator", &ir, "invalid JSON parse allocator");
}

static void fs_read_all_immutable_allocator_fails(void) {
  IrLocal locals[] = {
    scalar_local("alloc", IR_TYPE_ALLOC, 0, false),
    scalar_local("body", IR_TYPE_MAYBE_BYTE_VIEW, 1, false)
  };
  locals[1].frame_offset = 40;
  IrValue path = byte_view_value();
  IrValue limit = value(IR_VALUE_INT, IR_TYPE_USIZE);
  IrValue read = value(IR_VALUE_FS_READ_ALL, IR_TYPE_MAYBE_BYTE_VIEW);
  read.local_index = 0;
  read.left = &path;
  read.right = &limit;
  IrInstr set = {.kind = IR_INSTR_LOCAL_SET, .local_index = 1, .value = &read, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 2, 0, &set, 1, 64, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("filesystem readAll immutable allocator", &ir, "invalid filesystem readAll allocator");
}

static void fixed_buf_alloc_unknown_maybe_storage_fails(void) {
  IrLocal locals[] = {
    scalar_local("maybe", IR_TYPE_MAYBE_BYTE_VIEW, 0, false),
    scalar_local("alloc", IR_TYPE_ALLOC, 1, false)
  };
  locals[1].frame_offset = 48;
  IrValue bytes = value(IR_VALUE_MAYBE_VALUE, IR_TYPE_BYTE_VIEW);
  bytes.local_index = 0;
  IrValue alloc = value(IR_VALUE_FIXED_BUF_ALLOC, IR_TYPE_ALLOC);
  alloc.left = &bytes;
  IrInstr set = {.kind = IR_INSTR_LOCAL_SET, .local_index = 1, .value = &alloc, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 2, 0, &set, 1, 48, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_allocator_helper_count = 1;
  expect_fail("FixedBufAlloc unknown Maybe storage", &ir, "invalid FixedBufAlloc helper value");
}

static void vec_init_known_maybe_storage_passes(void) {
  IrLocal locals[] = {
    array_local("storage", IR_TYPE_U8, 0),
    scalar_local("alloc", IR_TYPE_ALLOC, 1, false),
    scalar_local("maybe", IR_TYPE_MAYBE_BYTE_VIEW, 2, false),
    scalar_local("vec", IR_TYPE_VEC, 3, false)
  };
  locals[0].is_mutable = true;
  locals[1].is_mutable = true;
  locals[2].byte_size = 24;
  locals[2].frame_offset = 56;
  locals[3].frame_offset = 72;
  IrValue storage = value(IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW);
  storage.array_index = 0;
  storage.data_len = 4;
  IrValue alloc = value(IR_VALUE_FIXED_BUF_ALLOC, IR_TYPE_ALLOC);
  alloc.left = &storage;
  IrValue length = value(IR_VALUE_INT, IR_TYPE_USIZE);
  length.int_value = 4;
  IrValue bytes = value(IR_VALUE_ALLOC_BYTES, IR_TYPE_MAYBE_BYTE_VIEW);
  bytes.local_index = 1;
  bytes.left = &length;
  IrValue maybe = value(IR_VALUE_MAYBE_VALUE, IR_TYPE_BYTE_VIEW);
  maybe.local_index = 2;
  IrValue vec = value(IR_VALUE_VEC_INIT, IR_TYPE_VEC);
  vec.left = &maybe;
  IrInstr instrs[] = {
    {.kind = IR_INSTR_LOCAL_SET, .local_index = 1, .value = &alloc, .line = 1, .column = 1},
    {.kind = IR_INSTR_LOCAL_SET, .local_index = 2, .value = &bytes, .line = 1, .column = 1},
    {.kind = IR_INSTR_LOCAL_SET, .local_index = 3, .value = &vec, .line = 1, .column = 1}
  };
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 4, 0, instrs, 3, 80, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_allocator_helper_count = 2;
  ir.direct_buffer_helper_count = 1;
  expect_ok("Vec init known Maybe storage", &ir);
}

static void vec_init_maybe_before_mutable_assignment_fails(void) {
  IrLocal locals[] = {
    array_local("storage", IR_TYPE_U8, 0),
    scalar_local("alloc", IR_TYPE_ALLOC, 1, false),
    scalar_local("maybe", IR_TYPE_MAYBE_BYTE_VIEW, 2, false),
    scalar_local("vec", IR_TYPE_VEC, 3, false)
  };
  locals[0].is_mutable = true;
  locals[1].is_mutable = true;
  locals[2].byte_size = 24;
  locals[2].frame_offset = 56;
  locals[3].frame_offset = 72;
  IrValue storage = value(IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW);
  storage.array_index = 0;
  storage.data_len = 4;
  IrValue alloc = value(IR_VALUE_FIXED_BUF_ALLOC, IR_TYPE_ALLOC);
  alloc.left = &storage;
  IrValue length = value(IR_VALUE_INT, IR_TYPE_USIZE);
  length.int_value = 4;
  IrValue bytes = value(IR_VALUE_ALLOC_BYTES, IR_TYPE_MAYBE_BYTE_VIEW);
  bytes.local_index = 1;
  bytes.left = &length;
  IrValue maybe = value(IR_VALUE_MAYBE_VALUE, IR_TYPE_BYTE_VIEW);
  maybe.local_index = 2;
  IrValue vec = value(IR_VALUE_VEC_INIT, IR_TYPE_VEC);
  vec.left = &maybe;
  IrInstr instrs[] = {
    {.kind = IR_INSTR_LOCAL_SET, .local_index = 1, .value = &alloc, .line = 1, .column = 1},
    {.kind = IR_INSTR_LOCAL_SET, .local_index = 3, .value = &vec, .line = 1, .column = 1},
    {.kind = IR_INSTR_LOCAL_SET, .local_index = 2, .value = &bytes, .line = 1, .column = 1}
  };
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 4, 0, instrs, 3, 80, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_allocator_helper_count = 2;
  ir.direct_buffer_helper_count = 1;
  expect_fail("Vec init Maybe before mutable assignment", &ir, "invalid Vec helper value");
}

static void vec_init_maybe_branch_only_assignment_fails(void) {
  IrLocal locals[] = {
    array_local("storage", IR_TYPE_U8, 0),
    scalar_local("alloc", IR_TYPE_ALLOC, 1, false),
    scalar_local("maybe", IR_TYPE_MAYBE_BYTE_VIEW, 2, false),
    scalar_local("vec", IR_TYPE_VEC, 3, false)
  };
  locals[0].is_mutable = true;
  locals[1].is_mutable = true;
  locals[2].byte_size = 24;
  locals[2].frame_offset = 56;
  locals[3].frame_offset = 72;
  IrValue storage = value(IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW);
  storage.array_index = 0;
  storage.data_len = 4;
  IrValue alloc = value(IR_VALUE_FIXED_BUF_ALLOC, IR_TYPE_ALLOC);
  alloc.left = &storage;
  IrValue length = value(IR_VALUE_INT, IR_TYPE_USIZE);
  length.int_value = 4;
  IrValue bytes = value(IR_VALUE_ALLOC_BYTES, IR_TYPE_MAYBE_BYTE_VIEW);
  bytes.local_index = 1;
  bytes.left = &length;
  IrInstr then_set = {.kind = IR_INSTR_LOCAL_SET, .local_index = 2, .value = &bytes, .line = 1, .column = 1};
  IrValue cond = value(IR_VALUE_BOOL, IR_TYPE_BOOL);
  cond.int_value = 1;
  IrInstr branch = {.kind = IR_INSTR_IF, .value = &cond, .then_instrs = &then_set, .then_len = 1, .line = 1, .column = 1};
  IrValue maybe = value(IR_VALUE_MAYBE_VALUE, IR_TYPE_BYTE_VIEW);
  maybe.local_index = 2;
  IrValue vec = value(IR_VALUE_VEC_INIT, IR_TYPE_VEC);
  vec.left = &maybe;
  IrInstr instrs[] = {
    {.kind = IR_INSTR_LOCAL_SET, .local_index = 1, .value = &alloc, .line = 1, .column = 1},
    branch,
    {.kind = IR_INSTR_LOCAL_SET, .local_index = 3, .value = &vec, .line = 1, .column = 1}
  };
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 4, 0, instrs, 3, 80, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_allocator_helper_count = 2;
  ir.direct_buffer_helper_count = 1;
  expect_fail("Vec init Maybe branch-only assignment", &ir, "invalid Vec helper value");
}

static void vec_init_maybe_immutable_overwrite_fails(void) {
  IrLocal locals[] = {
    array_local("storage", IR_TYPE_U8, 0),
    scalar_local("alloc", IR_TYPE_ALLOC, 1, false),
    scalar_local("maybe", IR_TYPE_MAYBE_BYTE_VIEW, 2, false),
    scalar_local("vec", IR_TYPE_VEC, 3, false)
  };
  locals[0].is_mutable = true;
  locals[1].is_mutable = true;
  locals[2].byte_size = 24;
  locals[2].frame_offset = 56;
  locals[3].frame_offset = 72;
  IrValue storage = value(IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW);
  storage.array_index = 0;
  storage.data_len = 4;
  IrValue alloc = value(IR_VALUE_FIXED_BUF_ALLOC, IR_TYPE_ALLOC);
  alloc.left = &storage;
  IrValue length = value(IR_VALUE_INT, IR_TYPE_USIZE);
  length.int_value = 4;
  IrValue bytes = value(IR_VALUE_ALLOC_BYTES, IR_TYPE_MAYBE_BYTE_VIEW);
  bytes.local_index = 1;
  bytes.left = &length;
  IrValue args = value(IR_VALUE_ARGS_GET, IR_TYPE_MAYBE_BYTE_VIEW);
  args.left = &length;
  IrValue maybe = value(IR_VALUE_MAYBE_VALUE, IR_TYPE_BYTE_VIEW);
  maybe.local_index = 2;
  IrValue vec = value(IR_VALUE_VEC_INIT, IR_TYPE_VEC);
  vec.left = &maybe;
  IrInstr instrs[] = {
    {.kind = IR_INSTR_LOCAL_SET, .local_index = 1, .value = &alloc, .line = 1, .column = 1},
    {.kind = IR_INSTR_LOCAL_SET, .local_index = 2, .value = &bytes, .line = 1, .column = 1},
    {.kind = IR_INSTR_LOCAL_SET, .local_index = 2, .value = &args, .line = 1, .column = 1},
    {.kind = IR_INSTR_LOCAL_SET, .local_index = 3, .value = &vec, .line = 1, .column = 1}
  };
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 4, 0, instrs, 4, 80, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_allocator_helper_count = 2;
  ir.direct_buffer_helper_count = 1;
  expect_fail("Vec init Maybe immutable overwrite", &ir, "invalid Vec helper value");
}

static void http_fetch_immutable_response_fails(void) {
  IrValue request = byte_view_value();
  IrValue response = byte_view_value();
  IrValue timeout = value(IR_VALUE_INT, IR_TYPE_I64);
  IrValue fetch = value(IR_VALUE_HTTP_FETCH, IR_TYPE_U64);
  fetch.left = &request;
  fetch.right = &response;
  fetch.index = &timeout;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &fetch, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_U64, IR_TYPE_U64, NULL, 0, 0, &ret, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_runtime_helper_count = 2;
  ir.direct_host_runtime_import_count = 2;
  ir.direct_http_runtime_import_count = 1;
  expect_fail("HTTP fetch immutable response", &ir, "invalid HTTP fetch response buffer");
}

static void helper_result_type_mismatch_fails(void) {
  IrValue request = byte_view_value();
  IrValue response = byte_view_value();
  IrValue timeout = value(IR_VALUE_INT, IR_TYPE_I64);
  IrValue fetch = value(IR_VALUE_HTTP_FETCH, IR_TYPE_BOOL);
  fetch.left = &request;
  fetch.right = &response;
  fetch.index = &timeout;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &fetch, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_BOOL, IR_TYPE_BOOL, NULL, 0, 0, &ret, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_runtime_helper_count = 2;
  ir.direct_host_runtime_import_count = 2;
  ir.direct_http_runtime_import_count = 1;
  expect_fail("helper result type mismatch", &ir, "helper result type mismatch");
}

static void byte_view_equality_result_type_mismatch_fails(void) {
  IrValue left = byte_view_value();
  IrValue right = byte_view_value();
  IrValue equal = value(IR_VALUE_BYTE_VIEW_EQ, IR_TYPE_I32);
  equal.left = &left;
  equal.right = &right;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &equal, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_I32, IR_TYPE_I32, NULL, 0, 0, &ret, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("byte-view equality result mismatch", &ir, "byte-view equality result type mismatch");
}

static void binary_operand_type_mismatch_fails(void) {
  IrValue left = value(IR_VALUE_INT, IR_TYPE_I32);
  IrValue right = value(IR_VALUE_BOOL, IR_TYPE_BOOL);
  IrValue binary = value(IR_VALUE_BINARY, IR_TYPE_I32);
  binary.binary_op = IR_BIN_ADD;
  binary.left = &left;
  binary.right = &right;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &binary, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_I32, IR_TYPE_I32, NULL, 0, 0, &ret, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("binary operand type mismatch", &ir, "binary operand type mismatch");
}

static void byte_copy_immutable_destination_fails(void) {
  IrValue src = byte_view_value();
  IrValue dst = byte_view_value();
  IrValue copy = value(IR_VALUE_BYTE_COPY, IR_TYPE_USIZE);
  copy.left = &src;
  copy.right = &dst;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &copy, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_USIZE, IR_TYPE_USIZE, NULL, 0, 0, &ret, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("byte copy immutable destination", &ir, "invalid byte copy destination");
}

static void byte_copy_result_type_mismatch_fails(void) {
  IrLocal locals[] = {array_local("dst", IR_TYPE_U8, 0)};
  locals[0].is_mutable = true;
  IrValue src = byte_view_value();
  IrValue dst = value(IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW);
  dst.array_index = 0;
  dst.data_len = 4;
  IrValue copy = value(IR_VALUE_BYTE_COPY, IR_TYPE_BOOL);
  copy.left = &src;
  copy.right = &dst;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &copy, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_BOOL, IR_TYPE_BOOL, locals, 1, 0, &ret, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("byte copy result type mismatch", &ir, "helper result type mismatch");
}

static void byte_fill_value_mismatch_fails(void) {
  IrLocal locals[] = {array_local("dst", IR_TYPE_U8, 0)};
  locals[0].is_mutable = true;
  IrValue fill = value(IR_VALUE_INT, IR_TYPE_I32);
  IrValue dst = value(IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW);
  dst.array_index = 0;
  dst.data_len = 4;
  IrValue copy = value(IR_VALUE_BYTE_FILL, IR_TYPE_USIZE);
  copy.left = &fill;
  copy.right = &dst;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &copy, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_USIZE, IR_TYPE_USIZE, locals, 1, 0, &ret, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("byte fill value mismatch", &ir, "invalid byte fill value");
}

static void check_input_type_mismatch_fails(void) {
  IrValue number = value(IR_VALUE_INT, IR_TYPE_I32);
  IrValue checked = value(IR_VALUE_CHECK, IR_TYPE_I32);
  checked.left = &number;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &checked, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_I32, IR_TYPE_I32, NULL, 0, 0, &ret, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("check input type mismatch", &ir, "invalid check input");
}

static void check_result_type_mismatch_fails(void) {
  IrValue packed = value(IR_VALUE_INT, IR_TYPE_I64);
  packed.element_type = IR_TYPE_U8;
  IrValue checked = value(IR_VALUE_CHECK, IR_TYPE_I32);
  checked.left = &packed;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &checked, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_I32, IR_TYPE_I32, NULL, 0, 0, &ret, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("check result type mismatch", &ir, "check result type mismatch");
}

static void check_non_fallible_function_fails(void) {
  IrValue packed = value(IR_VALUE_INT, IR_TYPE_I64);
  packed.element_type = IR_TYPE_I32;
  IrValue checked = value(IR_VALUE_CHECK, IR_TYPE_I32);
  checked.left = &packed;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &checked, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_I32, IR_TYPE_I32, NULL, 0, 0, &ret, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("check non-fallible function", &ir, "check in a non-fallible function");
}

static void check_hosted_world_main_passes(void) {
  IrValue packed = value(IR_VALUE_INT, IR_TYPE_I64);
  packed.element_type = IR_TYPE_I32;
  IrValue checked = value(IR_VALUE_CHECK, IR_TYPE_I32);
  checked.left = &packed;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &checked, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_I32, IR_TYPE_VOID, NULL, 0, 0, &ret, 1, 0, false);
  fun.world_param_name = "world";
  IrProgram ir = program(&fun, 1);
  expect_ok("check hosted world main", &ir);
}

static void rescue_fallback_type_mismatch_fails(void) {
  IrValue packed = value(IR_VALUE_INT, IR_TYPE_I64);
  packed.element_type = IR_TYPE_U8;
  IrValue fallback = value(IR_VALUE_INT, IR_TYPE_I32);
  IrValue rescued = value(IR_VALUE_RESCUE, IR_TYPE_U8);
  rescued.left = &packed;
  rescued.right = &fallback;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &rescued, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_U8, IR_TYPE_U8, NULL, 0, 0, &ret, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("rescue fallback type mismatch", &ir, "rescue type mismatch");
}

static void byte_view_len_u32_passes(void) {
  IrValue bytes = byte_view_value();
  IrValue len = value(IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_U32);
  len.left = &bytes;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &len, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_U32, IR_TYPE_U32, NULL, 0, 0, &ret, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_ok("byte-view length u32", &ir);
}

static void maybe_has_non_maybe_local_fails(void) {
  IrLocal locals[] = {scalar_local("x", IR_TYPE_I32, 0, false)};
  IrValue has = value(IR_VALUE_MAYBE_HAS, IR_TYPE_BOOL);
  has.local_index = 0;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &has, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_BOOL, IR_TYPE_BOOL, locals, 1, 0, &ret, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("Maybe.has non-Maybe local", &ir, "maybe helper for a non-Maybe local");
}

static void maybe_value_type_mismatch_fails(void) {
  IrLocal locals[] = {scalar_local("maybe", IR_TYPE_MAYBE_SCALAR, 0, false)};
  IrValue maybe = value(IR_VALUE_MAYBE_VALUE, IR_TYPE_BYTE_VIEW);
  maybe.local_index = 0;
  IrInstr write = {.kind = IR_INSTR_WORLD_WRITE, .value = &maybe, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, locals, 1, 0, &write, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_runtime_helper_count = 1;
  expect_fail("Maybe.value type mismatch", &ir, "maybe scalar value type mismatch");
}

static void runtime_helper_shape_fails(void) {
  IrValue number = value(IR_VALUE_INT, IR_TYPE_I32);
  IrValue json = value(IR_VALUE_JSON_VALIDATE_BYTES, IR_TYPE_BOOL);
  json.left = &number;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &json, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_BOOL, IR_TYPE_BOOL, NULL, 0, 0, &ret, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_runtime_helper_count = 1;
  ir.direct_host_runtime_import_count = 1;
  expect_fail("runtime helper shape", &ir, "invalid JSON runtime helper input");
}

static void runtime_helper_contract_fails(void) {
  IrValue bytes = byte_view_value();
  IrValue json = value(IR_VALUE_JSON_VALIDATE_BYTES, IR_TYPE_BOOL);
  json.left = &bytes;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &json, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_BOOL, IR_TYPE_BOOL, NULL, 0, 0, &ret, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("runtime helper contract", &ir, "missing runtime helper contract");
}

static void host_runtime_import_contract_fails(void) {
  IrValue bytes = byte_view_value();
  IrValue json = value(IR_VALUE_JSON_VALIDATE_BYTES, IR_TYPE_BOOL);
  json.left = &bytes;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &json, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_BOOL, IR_TYPE_BOOL, NULL, 0, 0, &ret, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_runtime_helper_count = 1;
  expect_fail("host runtime import contract", &ir, "missing host runtime import contract");
}

static void http_runtime_import_contract_fails(void) {
  IrLocal locals[] = {array_local("response", IR_TYPE_U8, 0)};
  locals[0].is_mutable = true;
  IrValue request = byte_view_value();
  IrValue response = value(IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW);
  response.array_index = 0;
  response.data_len = 4;
  IrValue timeout = value(IR_VALUE_INT, IR_TYPE_I64);
  IrValue fetch = value(IR_VALUE_HTTP_FETCH, IR_TYPE_U64);
  fetch.left = &request;
  fetch.right = &response;
  fetch.index = &timeout;
  IrInstr ret = {.kind = IR_INSTR_RETURN, .value = &fetch, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_U64, IR_TYPE_U64, locals, 1, 0, &ret, 1, 16, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_runtime_helper_count = 2;
  ir.direct_host_runtime_import_count = 2;
  expect_fail("HTTP runtime import contract", &ir, "missing HTTP runtime import contract");
}

static void world_write_helper_contract_fails(void) {
  IrValue bytes = byte_view_value();
  IrInstr write = {.kind = IR_INSTR_WORLD_WRITE, .field_offset = 1, .value = &bytes, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, NULL, 0, 0, &write, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  expect_fail("world write helper contract", &ir, "missing runtime helper contract");
}

static void world_write_stream_contract_fails(void) {
  IrValue bytes = byte_view_value();
  IrInstr write = {.kind = IR_INSTR_WORLD_WRITE, .field_offset = 3, .value = &bytes, .line = 1, .column = 1};
  IrFunction fun = function("main", IR_TYPE_VOID, IR_TYPE_VOID, NULL, 0, 0, &write, 1, 0, false);
  IrProgram ir = program(&fun, 1);
  ir.direct_runtime_helper_count = 1;
  expect_fail("world write stream contract", &ir, "invalid world write stream");
}

int main(void) {
  valid_direct_call_passes();
  local_value_out_of_range_fails();
  local_write_type_mismatch_fails();
  packed_maybe_scalar_write_passes();
  return_type_mismatch_fails();
  branch_condition_mismatch_fails();
  unsupported_instruction_kind_fails();
  array_write_contract_fails();
  array_write_type_mismatch_fails();
  byte_view_write_contract_passes();
  array_load_contract_fails();
  array_load_type_mismatch_fails();
  array_byte_view_contract_fails();
  array_byte_view_length_mismatch_fails();
  field_write_contract_fails();
  field_write_partial_overrun_fails();
  field_load_partial_overrun_fails();
  overlapping_frame_fails();
  small_scalar_slot_fails();
  raise_in_non_fallible_function_fails();
  raise_in_hosted_world_main_passes();
  allocator_helper_contract_fails();
  buffer_helper_contract_fails();
  alloc_local_invalid_initializer_fails();
  vec_local_invalid_initializer_fails();
  fixed_buf_alloc_readonly_storage_fails();
  vec_init_immutable_storage_fails();
  alloc_bytes_immutable_allocator_fails();
  vec_push_immutable_vec_fails();
  json_parse_immutable_allocator_fails();
  fs_read_all_immutable_allocator_fails();
  fixed_buf_alloc_unknown_maybe_storage_fails();
  vec_init_known_maybe_storage_passes();
  vec_init_maybe_before_mutable_assignment_fails();
  vec_init_maybe_branch_only_assignment_fails();
  vec_init_maybe_immutable_overwrite_fails();
  http_fetch_immutable_response_fails();
  helper_result_type_mismatch_fails();
  byte_view_equality_result_type_mismatch_fails();
  binary_operand_type_mismatch_fails();
  byte_copy_immutable_destination_fails();
  byte_copy_result_type_mismatch_fails();
  byte_fill_value_mismatch_fails();
  check_input_type_mismatch_fails();
  check_result_type_mismatch_fails();
  check_non_fallible_function_fails();
  check_hosted_world_main_passes();
  rescue_fallback_type_mismatch_fails();
  byte_view_len_u32_passes();
  maybe_has_non_maybe_local_fails();
  maybe_value_type_mismatch_fails();
  runtime_helper_shape_fails();
  runtime_helper_contract_fails();
  host_runtime_import_contract_fails();
  http_runtime_import_contract_fails();
  world_write_helper_contract_fails();
  world_write_stream_contract_fails();
  puts("mir verifier smoke ok");
  return 0;
}
