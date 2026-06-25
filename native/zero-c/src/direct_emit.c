#include "zero.h"

#include <stdlib.h>

bool z_direct_trap_branches_record(ZDirectTrapBranchList *list, size_t patch_offset) {
  if (!list) return false;
  if (list->len == list->cap) {
    size_t cap = list->cap ? list->cap * 2 : 8;
    size_t *items = realloc(list->items, cap * sizeof(size_t));
    if (!items) return false;
    list->items = items;
    list->cap = cap;
  }
  list->items[list->len++] = patch_offset;
  return true;
}

void z_direct_trap_branches_free(ZDirectTrapBranchList *lists, size_t count) {
  for (size_t i = 0; lists && i < count; i++) {
    free(lists[i].items);
    lists[i] = (ZDirectTrapBranchList){0};
  }
}

const char *z_direct_trap_message(ZDirectTrapKind kind) {
  switch (kind) {
    case Z_DIRECT_TRAP_INDEX_BOUNDS: return "trap: index out of bounds\n";
    case Z_DIRECT_TRAP_VALUE_BOUNDS: return "trap: value out of bounds\n";
    case Z_DIRECT_TRAP_WRITE_FAILED: return "trap: write failed\n";
    case Z_DIRECT_TRAP_KIND_COUNT: break;
  }
  return "trap\n";
}

bool z_direct_loop_frame_add_break(ZDirectLoopFrame *frame, size_t patch_offset) {
  if (!frame) return false;
  if (frame->break_len == frame->break_cap) {
    size_t cap = frame->break_cap ? frame->break_cap * 2 : 4;
    size_t *items = realloc(frame->break_patches, cap * sizeof(size_t));
    if (!items) return false;
    frame->break_patches = items;
    frame->break_cap = cap;
  }
  frame->break_patches[frame->break_len++] = patch_offset;
  return true;
}

bool z_direct_detect_fill_run(const IrFunction *fun, const IrInstr *instrs, size_t len, size_t start, size_t min_run, ZDirectFillRun *out) {
  if (!fun || !instrs || start >= len) return false;
  const IrInstr *first = &instrs[start];
  if (first->kind != IR_INSTR_INDEX_STORE) return false;
  if (first->array_index >= fun->local_len) return false;
  const IrLocal *local = &fun->locals[first->array_index];
  // Only plain fixed-array locals with a scalar element are foldable. Byte
  // views are pointer-indirected and excluded.
  if (!local->is_array || local->type == IR_TYPE_BYTE_VIEW) return false;
  if (!first->index || first->index->kind != IR_VALUE_INT || first->index->int_value != 0) return false;
  if (!first->value || first->value->kind != IR_VALUE_INT) return false;
  IrTypeKind element_type = local->element_type;
  if (element_type == IR_TYPE_UNSUPPORTED) return false;
  if (first->value->type != element_type) return false;
  unsigned long long fill_value = first->value->int_value;
  size_t run = 1;
  while (start + run < len) {
    const IrInstr *next = &instrs[start + run];
    if (next->kind != IR_INSTR_INDEX_STORE) break;
    if (next->array_index != first->array_index) break;
    if (!next->index || next->index->kind != IR_VALUE_INT || next->index->int_value != run) break;
    if (!next->value || next->value->kind != IR_VALUE_INT) break;
    if (next->value->type != element_type || next->value->int_value != fill_value) break;
    run++;
  }
  if (run < min_run) return false;
  if (out) {
    out->array_index = first->array_index;
    out->count = run;
    out->fill_value = fill_value;
    out->element_type = element_type;
  }
  return true;
}

bool z_direct_fill_run_from_instr(const IrFunction *fun, const IrInstr *instr, ZDirectFillRun *out) {
  if (!fun || !instr || instr->kind != IR_INSTR_ARRAY_FILL || instr->array_index >= fun->local_len) return false;
  const IrLocal *local = &fun->locals[instr->array_index];
  if (!local->is_array || local->array_len == 0 || local->type == IR_TYPE_BYTE_VIEW || local->element_type == IR_TYPE_UNSUPPORTED) return false;
  if (!instr->value || instr->value->type != local->element_type) return false;
  if (instr->value->kind != IR_VALUE_INT && instr->value->kind != IR_VALUE_BOOL) return false;
  if (out) {
    out->array_index = instr->array_index;
    out->count = local->array_len;
    out->fill_value = instr->value->int_value;
    out->element_type = local->element_type;
  }
  return true;
}

bool z_emit_direct_object_from_ir(ZDirectBackend backend, const IrProgram *program, ZBuf *out, ZDiag *diag) {
  switch (backend) {
    case Z_DIRECT_BACKEND_ELF64: return z_emit_elf64_object_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_ELF_AARCH64: return z_emit_elf_aarch64_object_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_MACHO64: return z_emit_macho64_object_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_MACHO_X64: return z_emit_macho_x64_object_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_COFF_X64: return z_emit_coff_x64_object_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_COFF_AARCH64: return z_emit_coff_aarch64_object_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_NONE: return false;
  }
  return false;
}

bool z_emit_direct_executable_from_ir(ZDirectBackend backend, const IrProgram *program, ZBuf *out, ZDiag *diag) {
  switch (backend) {
    case Z_DIRECT_BACKEND_ELF64: return z_emit_elf64_exe_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_ELF_AARCH64: return z_emit_elf_aarch64_exe_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_MACHO64: return z_emit_macho64_exe_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_MACHO_X64: return z_emit_macho_x64_exe_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_COFF_X64: return z_emit_coff_x64_exe_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_COFF_AARCH64: return z_emit_coff_aarch64_exe_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_NONE: return false;
  }
  return false;
}
