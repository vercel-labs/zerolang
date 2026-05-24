#include "zero.h"
#include "aarch64_emit.h"
#include "elf_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool a64_diag(ZDiag *diag, const char *message, int line, int column, const char *actual) {
  if (diag) {
    diag->code = 4004;
    diag->line = line > 0 ? line : 1;
    diag->column = column > 0 ? column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message);
    snprintf(diag->expected, sizeof(diag->expected), "direct AArch64 ELF object MVP subset");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "unsupported construct");
    snprintf(diag->help, sizeof(diag->help), "choose a supported direct target or restrict this program to exported no-parameter functions returning small integer literals");
  }
  return false;
}

static const char *a64_instr_kind_name(IrInstrKind kind) {
  switch (kind) {
    case IR_INSTR_LOCAL_SET: return "IR_INSTR_LOCAL_SET";
    case IR_INSTR_INDEX_STORE: return "IR_INSTR_INDEX_STORE";
    case IR_INSTR_FIELD_STORE: return "IR_INSTR_FIELD_STORE";
    case IR_INSTR_WORLD_WRITE: return "IR_INSTR_WORLD_WRITE";
    case IR_INSTR_RAISE: return "IR_INSTR_RAISE";
    case IR_INSTR_EXPR: return "IR_INSTR_EXPR";
    case IR_INSTR_RETURN: return "IR_INSTR_RETURN";
    case IR_INSTR_IF: return "IR_INSTR_IF";
    case IR_INSTR_WHILE: return "IR_INSTR_WHILE";
  }
  return "unknown IR instruction";
}

static bool a64_return_literal(const IrFunction *fun, uint32_t *out, ZDiag *diag) {
  if (!fun) return a64_diag(diag, "direct AArch64 ELF object backend requires a function", 1, 1, "missing function");
  if (fun->return_type != IR_TYPE_VOID && fun->return_type != IR_TYPE_U8 && fun->return_type != IR_TYPE_I32 && fun->return_type != IR_TYPE_U32 && fun->return_type != IR_TYPE_USIZE) {
    return a64_diag(diag, "direct AArch64 ELF object backend currently supports primitive 32-bit-or-smaller integer returns", fun->line, fun->column, fun->name);
  }
  *out = 0;
  bool saw_return = false;
  for (size_t i = 0; i < fun->instr_len; i++) {
    const IrInstr *instr = &fun->instrs[i];
    if (instr->kind != IR_INSTR_RETURN) {
      return a64_diag(diag, "direct AArch64 ELF object backend does not yet support lowering this IR instruction", instr->line, instr->column, a64_instr_kind_name(instr->kind));
    }
    if (!instr->value) {
      if (fun->return_type == IR_TYPE_VOID) {
        saw_return = true;
        continue;
      }
      return a64_diag(diag, "direct AArch64 ELF object backend does not yet support lowering this IR instruction", instr->line, instr->column, a64_instr_kind_name(instr->kind));
    }
    if (instr->value->kind != IR_VALUE_INT || instr->value->int_value > 65535) {
      return a64_diag(diag, "direct AArch64 ELF object backend does not yet support lowering this IR instruction", instr->line, instr->column, a64_instr_kind_name(instr->kind));
    }
    *out = (uint32_t)instr->value->int_value;
    saw_return = true;
  }
  if (saw_return || fun->return_type == IR_TYPE_VOID) return true;
  return a64_diag(diag, "direct AArch64 ELF object backend currently requires a small integer literal return", fun->line, fun->column, fun->name);
}

static bool a64_validate_function(const IrFunction *fun, ZDiag *diag) {
  uint32_t scratch = 0;
  return a64_return_literal(fun, &scratch, diag);
}

static const IrFunction *a64_find_main(const IrProgram *ir, unsigned *out_index, ZDiag *diag) {
  const IrFunction *fun = NULL;
  unsigned index = 0;
  for (size_t i = 0; ir && i < ir->function_len; i++) {
    if (ir->functions[i].is_exported && strcmp(ir->functions[i].name, "main") == 0) {
      if (fun) {
        a64_diag(diag, "direct AArch64 ELF executable backend requires exactly one exported main function", ir->functions[i].line, ir->functions[i].column, ir->functions[i].name);
        return NULL;
      }
      fun = &ir->functions[i];
      index = (unsigned)i;
    }
  }
  if (!fun) {
    a64_diag(diag, "direct AArch64 ELF executable backend requires an exported main function", 1, 1, "missing main");
    return NULL;
  }
  if (out_index) *out_index = index;
  return fun;
}

bool z_emit_elf_aarch64_object_from_ir(const IrProgram *ir, ZBuf *out, ZDiag *diag) {
  if (!ir) return a64_diag(diag, "direct AArch64 ELF object backend requires MIR", 1, 1, "missing MIR");
  if (!ir->mir_valid) {
    bool ok = a64_diag(diag, ir->mir_message[0] ? ir->mir_message : "direct backend lowering failed", ir->mir_line, ir->mir_column, ir->mir_actual);
    z_diag_set_backend_blocker(diag, &ir->backend_blocker);
    return ok;
  }

  for (size_t i = 0; i < ir->function_len; i++) {
    if (!a64_validate_function(&ir->functions[i], diag)) return false;
  }

  ZBuf text;
  ZBuf strtab;
  ZBuf symtab;
  zbuf_init(&text);
  zbuf_init(&strtab);
  zbuf_init(&symtab);
  z_elf_append_u8(&strtab, 0);
  z_elf_append_zeros(&symtab, 24);

  size_t *function_offsets = z_checked_calloc(ir->function_len, sizeof(size_t));
  size_t *function_sizes = z_checked_calloc(ir->function_len, sizeof(size_t));
  uint32_t *symbol_names = z_checked_calloc(ir->function_len, sizeof(uint32_t));
  if (!function_offsets || !function_sizes || !symbol_names) {
    free(function_offsets);
    free(function_sizes);
    free(symbol_names);
    zbuf_free(&text);
    zbuf_free(&strtab);
    zbuf_free(&symtab);
    return a64_diag(diag, "direct AArch64 ELF object backend ran out of memory", 1, 1, "allocation failed");
  }

  bool has_export = false;
  for (size_t i = 0; i < ir->function_len; i++) {
    if (!ir->functions[i].is_exported) continue;
    has_export = true;
    uint32_t literal = 0;
    if (!a64_return_literal(&ir->functions[i], &literal, diag)) {
      free(function_offsets);
      free(function_sizes);
      free(symbol_names);
      zbuf_free(&text);
      zbuf_free(&strtab);
      zbuf_free(&symtab);
      return false;
    }
    z_aarch64_pad_to(&text, z_aarch64_align(text.len, 4));
    function_offsets[i] = text.len;
    z_aarch64_emit_literal_return(&text, literal);
    function_sizes[i] = text.len - function_offsets[i];
    symbol_names[i] = (uint32_t)strtab.len;
    zbuf_append(&strtab, ir->functions[i].name ? ir->functions[i].name : "zero_fn");
    z_elf_append_u8(&strtab, 0);
  }
  if (!has_export) {
    free(function_offsets);
    free(function_sizes);
    free(symbol_names);
    zbuf_free(&text);
    zbuf_free(&strtab);
    zbuf_free(&symtab);
    return a64_diag(diag, "direct AArch64 ELF object backend requires at least one exported function", 1, 1, "no exported function");
  }

  for (size_t i = 0; i < ir->function_len; i++) {
    if (function_sizes[i] > 0) z_elf_append_symbol(&symtab, symbol_names[i], ir->functions[i].is_exported ? 0x12 : 0x02, 1, function_offsets[i], function_sizes[i]);
  }

  ZElfObjectImage image = {
    .machine = Z_ELF_MACHINE_AARCH64,
    .text = &text,
    .text_align = 4,
    .symtab = &symtab,
    .strtab = &strtab,
    .local_symbol_count = 1
  };
  z_elf_write_object64(out, &image);

  free(function_offsets);
  free(function_sizes);
  free(symbol_names);
  zbuf_free(&text);
  zbuf_free(&strtab);
  zbuf_free(&symtab);
  return true;
}

bool z_emit_elf_aarch64_exe_from_ir(const IrProgram *ir, ZBuf *out, ZDiag *diag) {
  if (!ir) return a64_diag(diag, "direct AArch64 ELF executable backend requires MIR", 1, 1, "missing MIR");
  if (!ir->mir_valid) {
    bool ok = a64_diag(diag, ir->mir_message[0] ? ir->mir_message : "direct backend lowering failed", ir->mir_line, ir->mir_column, ir->mir_actual);
    z_diag_set_backend_blocker(diag, &ir->backend_blocker);
    return ok;
  }
  unsigned main_index = 0;
  if (!a64_find_main(ir, &main_index, diag)) return false;

  for (size_t i = 0; i < ir->function_len; i++) {
    if (!a64_validate_function(&ir->functions[i], diag)) return false;
  }

  ZBuf text;
  zbuf_init(&text);
  size_t start_call_patch = z_aarch64_emit_bl_placeholder(&text);
  z_aarch64_emit_movz_x(&text, 8, 93);
  z_aarch64_emit_svc(&text, 0);
  z_aarch64_pad_to(&text, z_aarch64_align(text.len, 16));

  size_t *function_offsets = z_checked_calloc(ir->function_len, sizeof(size_t));
  if (!function_offsets) {
    zbuf_free(&text);
    return a64_diag(diag, "direct AArch64 ELF executable backend ran out of memory", 1, 1, "allocation failed");
  }
  for (size_t i = 0; i < ir->function_len; i++) {
    if (!ir->functions[i].is_exported) continue;
    uint32_t literal = 0;
    if (!a64_return_literal(&ir->functions[i], &literal, diag)) {
      free(function_offsets);
      zbuf_free(&text);
      return false;
    }
    z_aarch64_pad_to(&text, z_aarch64_align(text.len, 4));
    function_offsets[i] = text.len;
    z_aarch64_emit_literal_return(&text, literal);
  }
  z_aarch64_patch_branch26(&text, start_call_patch, function_offsets[main_index]);

  const uint64_t base_addr = 0x400000;
  const size_t ehdr_size = 64;
  const size_t phdr_size = 56;
  const size_t text_offset = ehdr_size + phdr_size;
  const uint64_t entry_addr = base_addr + text_offset;
  ZElfExecutableImage image = {
    .machine = Z_ELF_MACHINE_AARCH64,
    .base_addr = base_addr,
    .entry_addr = entry_addr,
    .text_offset = text_offset,
    .text = &text,
    .segment_align = 0x1000
  };
  z_elf_write_executable64(out, &image);
  free(function_offsets);
  zbuf_free(&text);
  return true;
}
