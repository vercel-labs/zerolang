#include "zero.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void a64_append_u8(ZBuf *buf, unsigned value) {
  zbuf_append_char(buf, (char)(value & 0xff));
}

static void a64_append_u16(ZBuf *buf, uint16_t value) {
  a64_append_u8(buf, value);
  a64_append_u8(buf, value >> 8);
}

static void a64_append_u32(ZBuf *buf, uint32_t value) {
  a64_append_u8(buf, value);
  a64_append_u8(buf, value >> 8);
  a64_append_u8(buf, value >> 16);
  a64_append_u8(buf, value >> 24);
}

static void a64_append_u64(ZBuf *buf, uint64_t value) {
  a64_append_u32(buf, (uint32_t)value);
  a64_append_u32(buf, (uint32_t)(value >> 32));
}

static void a64_append_bytes(ZBuf *buf, const unsigned char *bytes, size_t len) {
  for (size_t i = 0; i < len; i++) a64_append_u8(buf, bytes[i]);
}

static void a64_append_zeros(ZBuf *buf, size_t len) {
  for (size_t i = 0; i < len; i++) a64_append_u8(buf, 0);
}

static size_t a64_align(size_t value, size_t alignment) {
  size_t remainder = alignment ? value % alignment : 0;
  return remainder == 0 ? value : value + (alignment - remainder);
}

static void a64_pad_to(ZBuf *buf, size_t offset) {
  while (buf->len < offset) a64_append_u8(buf, 0);
}

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

static bool a64_value_contains_new_opcode(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_UNARY) return true;
  if (value->kind == IR_VALUE_BINARY) {
    if (value->binary_op == IR_BIN_BITAND || value->binary_op == IR_BIN_BITOR ||
        value->binary_op == IR_BIN_BITXOR || value->binary_op == IR_BIN_SHL ||
        value->binary_op == IR_BIN_SHR) return true;
  }
  if (a64_value_contains_new_opcode(value->left)) return true;
  if (a64_value_contains_new_opcode(value->right)) return true;
  if (a64_value_contains_new_opcode(value->index)) return true;
  for (size_t i = 0; i < value->arg_len; i++) {
    if (a64_value_contains_new_opcode(value->args[i])) return true;
  }
  return false;
}

static bool a64_instr_contains_new_opcode(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (a64_value_contains_new_opcode(instr->value)) return true;
    if (a64_value_contains_new_opcode(instr->index)) return true;
    if (a64_instr_contains_new_opcode(instr->then_instrs, instr->then_len)) return true;
    if (a64_instr_contains_new_opcode(instr->else_instrs, instr->else_len)) return true;
  }
  return false;
}

static bool a64_return_literal(const IrFunction *fun, uint32_t *out, ZDiag *diag) {
  if (!fun) return a64_diag(diag, "direct AArch64 ELF object backend requires a function", 1, 1, "missing function");
  if (fun->return_type != IR_TYPE_VOID && fun->return_type != IR_TYPE_U8 && fun->return_type != IR_TYPE_I32 && fun->return_type != IR_TYPE_U32 && fun->return_type != IR_TYPE_USIZE) {
    return a64_diag(diag, "direct AArch64 ELF object backend currently supports primitive 32-bit-or-smaller integer returns", fun->line, fun->column, fun->name);
  }
  if (a64_instr_contains_new_opcode(fun->instrs, fun->instr_len)) {
    return a64_diag(diag, "direct AArch64 ELF object backend does not yet lower unary or bitwise/shift operators", fun->line, fun->column, fun->name);
  }
  *out = 0;
  for (size_t i = 0; i < fun->instr_len; i++) {
    const IrInstr *instr = &fun->instrs[i];
    if (instr->kind != IR_INSTR_RETURN || !instr->value || instr->value->kind != IR_VALUE_INT || instr->value->int_value > 65535) continue;
    *out = (uint32_t)instr->value->int_value;
    return true;
  }
  return true;
}

static void a64_emit_function_text(ZBuf *text, uint32_t literal) {
  a64_append_u32(text, 0x52800000u | ((literal & 0xffffu) << 5)); // movz w0, #literal
  if (literal > 0xffffu) {
    a64_append_u32(text, 0x72a00000u | (((literal >> 16) & 0xffffu) << 5)); // movk w0, #literal>>16, lsl #16
  }
  a64_append_u32(text, 0xd65f03c0u); // ret
}

static void a64_patch_branch(ZBuf *text, size_t patch_offset, size_t target_offset) {
  int64_t delta = (int64_t)target_offset - (int64_t)patch_offset;
  int64_t words = delta / 4;
  uint32_t instr = 0x94000000u | ((uint32_t)words & 0x03ffffffu);
  text->data[patch_offset + 0] = (char)(instr & 0xff);
  text->data[patch_offset + 1] = (char)((instr >> 8) & 0xff);
  text->data[patch_offset + 2] = (char)((instr >> 16) & 0xff);
  text->data[patch_offset + 3] = (char)((instr >> 24) & 0xff);
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

static void a64_append_symbol(ZBuf *symtab, uint32_t name, unsigned char info, uint16_t shndx, uint64_t value, uint64_t size) {
  a64_append_u32(symtab, name);
  a64_append_u8(symtab, info);
  a64_append_u8(symtab, 0);
  a64_append_u16(symtab, shndx);
  a64_append_u64(symtab, value);
  a64_append_u64(symtab, size);
}

static void a64_append_section_header(ZBuf *out, uint32_t name, uint32_t type, uint64_t flags, uint64_t offset, uint64_t size, uint32_t link, uint32_t info, uint64_t align, uint64_t entsize) {
  a64_append_u32(out, name);
  a64_append_u32(out, type);
  a64_append_u64(out, flags);
  a64_append_u64(out, 0);
  a64_append_u64(out, offset);
  a64_append_u64(out, size);
  a64_append_u32(out, link);
  a64_append_u32(out, info);
  a64_append_u64(out, align);
  a64_append_u64(out, entsize);
}

bool z_emit_elf_aarch64_object_from_ir(const IrProgram *ir, ZBuf *out, ZDiag *diag) {
  if (!ir) return a64_diag(diag, "direct AArch64 ELF object backend requires MIR", 1, 1, "missing MIR");
  if (!ir->mir_valid) {
    bool ok = a64_diag(diag, ir->mir_message[0] ? ir->mir_message : "direct backend lowering failed", ir->mir_line, ir->mir_column, ir->mir_actual);
    z_diag_set_backend_blocker(diag, &ir->backend_blocker);
    return ok;
  }

  ZBuf text;
  ZBuf strtab;
  ZBuf symtab;
  ZBuf shstrtab;
  zbuf_init(&text);
  zbuf_init(&strtab);
  zbuf_init(&symtab);
  zbuf_init(&shstrtab);
  a64_append_u8(&strtab, 0);
  a64_append_zeros(&symtab, 24);

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
    zbuf_free(&shstrtab);
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
      zbuf_free(&shstrtab);
      return false;
    }
    a64_pad_to(&text, a64_align(text.len, 4));
    function_offsets[i] = text.len;
    a64_emit_function_text(&text, literal);
    function_sizes[i] = text.len - function_offsets[i];
    symbol_names[i] = (uint32_t)strtab.len;
    zbuf_append(&strtab, ir->functions[i].name ? ir->functions[i].name : "zero_fn");
    a64_append_u8(&strtab, 0);
  }
  if (!has_export) {
    free(function_offsets);
    free(function_sizes);
    free(symbol_names);
    zbuf_free(&text);
    zbuf_free(&strtab);
    zbuf_free(&symtab);
    zbuf_free(&shstrtab);
    return a64_diag(diag, "direct AArch64 ELF object backend requires at least one exported function", 1, 1, "no exported function");
  }

  for (size_t i = 0; i < ir->function_len; i++) {
    if (function_sizes[i] > 0) a64_append_symbol(&symtab, symbol_names[i], ir->functions[i].is_exported ? 0x12 : 0x02, 1, function_offsets[i], function_sizes[i]);
  }

  a64_append_u8(&shstrtab, 0);
  uint32_t sh_name_text = (uint32_t)shstrtab.len;
  zbuf_append(&shstrtab, ".text");
  a64_append_u8(&shstrtab, 0);
  uint32_t sh_name_symtab = (uint32_t)shstrtab.len;
  zbuf_append(&shstrtab, ".symtab");
  a64_append_u8(&shstrtab, 0);
  uint32_t sh_name_strtab = (uint32_t)shstrtab.len;
  zbuf_append(&shstrtab, ".strtab");
  a64_append_u8(&shstrtab, 0);
  uint32_t sh_name_shstrtab = (uint32_t)shstrtab.len;
  zbuf_append(&shstrtab, ".shstrtab");
  a64_append_u8(&shstrtab, 0);

  const size_t ehdr_size = 64;
  size_t text_offset = a64_align(ehdr_size, 4);
  size_t symtab_offset = a64_align(text_offset + text.len, 8);
  size_t strtab_offset = symtab_offset + symtab.len;
  size_t shstrtab_offset = strtab_offset + strtab.len;
  size_t shoff = a64_align(shstrtab_offset + shstrtab.len, 8);

  zbuf_init(out);
  const unsigned char ident[] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  a64_append_bytes(out, ident, sizeof(ident));
  a64_append_u16(out, 1);
  a64_append_u16(out, 183);
  a64_append_u32(out, 1);
  a64_append_u64(out, 0);
  a64_append_u64(out, 0);
  a64_append_u64(out, shoff);
  a64_append_u32(out, 0);
  a64_append_u16(out, 64);
  a64_append_u16(out, 0);
  a64_append_u16(out, 0);
  a64_append_u16(out, 64);
  a64_append_u16(out, 5);
  a64_append_u16(out, 4);

  a64_pad_to(out, text_offset);
  a64_append_bytes(out, (const unsigned char *)text.data, text.len);
  a64_pad_to(out, symtab_offset);
  a64_append_bytes(out, (const unsigned char *)symtab.data, symtab.len);
  a64_pad_to(out, strtab_offset);
  a64_append_bytes(out, (const unsigned char *)strtab.data, strtab.len);
  a64_pad_to(out, shstrtab_offset);
  a64_append_bytes(out, (const unsigned char *)shstrtab.data, shstrtab.len);
  a64_pad_to(out, shoff);

  a64_append_section_header(out, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  a64_append_section_header(out, sh_name_text, 1, 0x6, text_offset, text.len, 0, 0, 4, 0);
  a64_append_section_header(out, sh_name_symtab, 2, 0, symtab_offset, symtab.len, 3, 1, 8, 24);
  a64_append_section_header(out, sh_name_strtab, 3, 0, strtab_offset, strtab.len, 0, 0, 1, 0);
  a64_append_section_header(out, sh_name_shstrtab, 3, 0, shstrtab_offset, shstrtab.len, 0, 0, 1, 0);

  free(function_offsets);
  free(function_sizes);
  free(symbol_names);
  zbuf_free(&text);
  zbuf_free(&strtab);
  zbuf_free(&symtab);
  zbuf_free(&shstrtab);
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

  ZBuf text;
  zbuf_init(&text);
  size_t start_call_patch = text.len;
  a64_append_u32(&text, 0x94000000u); // bl main
  a64_append_u32(&text, 0xd2800ba8u); // movz x8, #93
  a64_append_u32(&text, 0xd4000001u); // svc #0
  a64_pad_to(&text, a64_align(text.len, 16));

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
    a64_pad_to(&text, a64_align(text.len, 4));
    function_offsets[i] = text.len;
    a64_emit_function_text(&text, literal);
  }
  a64_patch_branch(&text, start_call_patch, function_offsets[main_index]);

  const uint64_t base_addr = 0x400000;
  const size_t ehdr_size = 64;
  const size_t phdr_size = 56;
  const size_t text_offset = ehdr_size + phdr_size;
  const uint64_t entry_addr = base_addr + text_offset;
  uint64_t file_size = text_offset + text.len;

  zbuf_init(out);
  const unsigned char ident[] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  a64_append_bytes(out, ident, sizeof(ident));
  a64_append_u16(out, 2);
  a64_append_u16(out, 183);
  a64_append_u32(out, 1);
  a64_append_u64(out, entry_addr);
  a64_append_u64(out, ehdr_size);
  a64_append_u64(out, 0);
  a64_append_u32(out, 0);
  a64_append_u16(out, 64);
  a64_append_u16(out, 56);
  a64_append_u16(out, 1);
  a64_append_u16(out, 0);
  a64_append_u16(out, 0);
  a64_append_u16(out, 0);

  a64_append_u32(out, 1);
  a64_append_u32(out, 5);
  a64_append_u64(out, 0);
  a64_append_u64(out, base_addr);
  a64_append_u64(out, base_addr);
  a64_append_u64(out, file_size);
  a64_append_u64(out, file_size);
  a64_append_u64(out, 0x1000);

  a64_pad_to(out, text_offset);
  a64_append_bytes(out, (const unsigned char *)text.data, text.len);
  free(function_offsets);
  zbuf_free(&text);
  return true;
}
