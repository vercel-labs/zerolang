#include "x64_emit.h"

void z_x64_append_u8(ZBuf *buf, unsigned value) {
  zbuf_append_char(buf, (char)(value & 0xffu));
}

void z_x64_append_u32(ZBuf *buf, uint32_t value) {
  z_x64_append_u8(buf, value);
  z_x64_append_u8(buf, value >> 8);
  z_x64_append_u8(buf, value >> 16);
  z_x64_append_u8(buf, value >> 24);
}

void z_x64_append_u64(ZBuf *buf, uint64_t value) {
  z_x64_append_u32(buf, (uint32_t)value);
  z_x64_append_u32(buf, (uint32_t)(value >> 32));
}

static void z_x64_emit_wide_prefix(ZBuf *buf, bool wide) {
  if (wide) z_x64_append_u8(buf, 0x48);
}

void z_x64_patch_u32(ZBuf *buf, size_t offset, uint32_t value) {
  buf->data[offset + 0] = (char)(value & 0xffu);
  buf->data[offset + 1] = (char)((value >> 8) & 0xffu);
  buf->data[offset + 2] = (char)((value >> 16) & 0xffu);
  buf->data[offset + 3] = (char)((value >> 24) & 0xffu);
}

void z_x64_patch_rel32(ZBuf *buf, size_t patch_offset, size_t target_offset) {
  int64_t rel = (int64_t)target_offset - (int64_t)(patch_offset + 4);
  z_x64_patch_u32(buf, patch_offset, (uint32_t)(int32_t)rel);
}

size_t z_x64_emit_jmp32_placeholder(ZBuf *buf, unsigned opcode) {
  z_x64_append_u8(buf, opcode);
  size_t patch = buf->len;
  z_x64_append_u32(buf, 0);
  return patch;
}

size_t z_x64_emit_call32_placeholder(ZBuf *buf) {
  return z_x64_emit_jmp32_placeholder(buf, 0xe8);
}

size_t z_x64_emit_jcc32_placeholder(ZBuf *buf, unsigned condition) {
  z_x64_append_u8(buf, 0x0f);
  z_x64_append_u8(buf, condition);
  size_t patch = buf->len;
  z_x64_append_u32(buf, 0);
  return patch;
}

void z_x64_emit_rbp_disp_reg(ZBuf *buf, unsigned opcode, unsigned reg, unsigned offset, bool wide) {
  if (wide || reg >= 8) {
    unsigned rex = wide ? 0x48 : 0x40;
    if (reg >= 8) rex |= 0x04;
    z_x64_append_u8(buf, rex);
  }
  z_x64_append_u8(buf, opcode);
  unsigned reg_low = reg & 7u;
  if (offset <= 127) {
    z_x64_append_u8(buf, 0x40 | (reg_low << 3) | 0x05);
    z_x64_append_u8(buf, (unsigned char)(-(int)offset));
  } else {
    z_x64_append_u8(buf, 0x80 | (reg_low << 3) | 0x05);
    z_x64_append_u32(buf, (uint32_t)(-(int32_t)offset));
  }
}

void z_x64_emit_load_rbp_positive_reg(ZBuf *buf, unsigned reg, unsigned offset, bool wide) {
  if (wide || reg >= 8) {
    unsigned rex = wide ? 0x48 : 0x40;
    if (reg >= 8) rex |= 0x04;
    z_x64_append_u8(buf, rex);
  }
  z_x64_append_u8(buf, 0x8b);
  unsigned reg_low = reg & 7u;
  if (offset <= 127) {
    z_x64_append_u8(buf, 0x40 | (reg_low << 3) | 0x05);
    z_x64_append_u8(buf, (unsigned char)offset);
  } else {
    z_x64_append_u8(buf, 0x80 | (reg_low << 3) | 0x05);
    z_x64_append_u32(buf, offset);
  }
}

void z_x64_emit_push_reg64(ZBuf *buf, unsigned reg) {
  if (reg >= 8) z_x64_append_u8(buf, 0x41);
  z_x64_append_u8(buf, 0x50 + (reg & 7u));
}

void z_x64_emit_pop_reg64(ZBuf *buf, unsigned reg) {
  if (reg >= 8) z_x64_append_u8(buf, 0x41);
  z_x64_append_u8(buf, 0x58 + (reg & 7u));
}

void z_x64_emit_push_rax(ZBuf *buf) {
  z_x64_emit_push_reg64(buf, 0);
}

void z_x64_emit_pop_rax(ZBuf *buf) {
  z_x64_emit_pop_reg64(buf, 0);
}

void z_x64_emit_mov_rcx_from_rax(ZBuf *buf, bool wide) {
  z_x64_emit_wide_prefix(buf, wide);
  z_x64_append_u8(buf, 0x89);
  z_x64_append_u8(buf, 0xc1);
}

void z_x64_emit_add_rax_rcx(ZBuf *buf, bool wide) {
  z_x64_emit_wide_prefix(buf, wide);
  z_x64_append_u8(buf, 0x01);
  z_x64_append_u8(buf, 0xc8);
}

void z_x64_emit_sub_rax_rcx(ZBuf *buf, bool wide) {
  z_x64_emit_wide_prefix(buf, wide);
  z_x64_append_u8(buf, 0x29);
  z_x64_append_u8(buf, 0xc8);
}

void z_x64_emit_imul_rax_rcx(ZBuf *buf, bool wide) {
  z_x64_emit_wide_prefix(buf, wide);
  z_x64_append_u8(buf, 0x0f);
  z_x64_append_u8(buf, 0xaf);
  z_x64_append_u8(buf, 0xc1);
}

void z_x64_emit_and_rax_rcx(ZBuf *buf, bool wide) {
  z_x64_emit_wide_prefix(buf, wide);
  z_x64_append_u8(buf, 0x21);
  z_x64_append_u8(buf, 0xc8);
}

void z_x64_emit_or_rax_rcx(ZBuf *buf, bool wide) {
  z_x64_emit_wide_prefix(buf, wide);
  z_x64_append_u8(buf, 0x09);
  z_x64_append_u8(buf, 0xc8);
}

void z_x64_emit_div_rax_rcx(ZBuf *buf, bool wide, bool uns, bool keep_remainder) {
  if (uns) {
    z_x64_emit_wide_prefix(buf, wide);
    z_x64_append_u8(buf, 0x31);
    z_x64_append_u8(buf, 0xd2);
    z_x64_emit_wide_prefix(buf, wide);
    z_x64_append_u8(buf, 0xf7);
    z_x64_append_u8(buf, 0xf1);
  } else {
    z_x64_emit_wide_prefix(buf, wide);
    z_x64_append_u8(buf, 0x99);
    z_x64_emit_wide_prefix(buf, wide);
    z_x64_append_u8(buf, 0xf7);
    z_x64_append_u8(buf, 0xf9);
  }
  if (keep_remainder) {
    z_x64_emit_wide_prefix(buf, wide);
    z_x64_append_u8(buf, 0x89);
    z_x64_append_u8(buf, 0xd0);
  }
}

void z_x64_emit_test_rax_rax(ZBuf *buf, bool wide) {
  z_x64_emit_wide_prefix(buf, wide);
  z_x64_append_u8(buf, 0x85);
  z_x64_append_u8(buf, 0xc0);
}

void z_x64_emit_test_ecx_ecx(ZBuf *buf) {
  z_x64_append_u8(buf, 0x85);
  z_x64_append_u8(buf, 0xc9);
}

void z_x64_emit_cmp_rax_rcx(ZBuf *buf, bool wide) {
  z_x64_emit_wide_prefix(buf, wide);
  z_x64_append_u8(buf, 0x39);
  z_x64_append_u8(buf, 0xc8);
}

void z_x64_emit_cmp_rax_rcx_to_bool(ZBuf *buf, unsigned setcc_opcode, bool wide) {
  z_x64_emit_cmp_rax_rcx(buf, wide);
  z_x64_append_u8(buf, 0x0f);
  z_x64_append_u8(buf, setcc_opcode);
  z_x64_append_u8(buf, 0xc0);
  z_x64_append_u8(buf, 0x0f);
  z_x64_append_u8(buf, 0xb6);
  z_x64_append_u8(buf, 0xc0);
}

void z_x64_emit_bool_from_nonnegative_rax(ZBuf *buf) {
  z_x64_emit_test_rax_rax(buf, true);
  z_x64_append_u8(buf, 0x0f);
  z_x64_append_u8(buf, 0x99);
  z_x64_append_u8(buf, 0xc0);
  z_x64_append_u8(buf, 0x0f);
  z_x64_append_u8(buf, 0xb6);
  z_x64_append_u8(buf, 0xc0);
}

void z_x64_emit_prologue(ZBuf *buf, unsigned stack_size) {
  z_x64_append_u8(buf, 0x55);
  z_x64_append_u8(buf, 0x48);
  z_x64_append_u8(buf, 0x89);
  z_x64_append_u8(buf, 0xe5);
  z_x64_emit_sub_rsp(buf, stack_size);
}

void z_x64_emit_epilogue(ZBuf *buf) {
  z_x64_append_u8(buf, 0xc9);
  z_x64_append_u8(buf, 0xc3);
}

void z_x64_emit_mov_eax_u32(ZBuf *buf, uint32_t value) {
  z_x64_append_u8(buf, 0xb8);
  z_x64_append_u32(buf, value);
}

void z_x64_emit_ud2(ZBuf *buf) {
  z_x64_append_u8(buf, 0x0f);
  z_x64_append_u8(buf, 0x0b);
}

void z_x64_emit_sub_rsp(ZBuf *buf, unsigned amount) {
  if (amount == 0) return;
  z_x64_append_u8(buf, 0x48);
  if (amount <= 127) {
    z_x64_append_u8(buf, 0x83);
    z_x64_append_u8(buf, 0xec);
    z_x64_append_u8(buf, amount);
  } else {
    z_x64_append_u8(buf, 0x81);
    z_x64_append_u8(buf, 0xec);
    z_x64_append_u32(buf, amount);
  }
}

void z_x64_emit_add_rsp(ZBuf *buf, unsigned amount) {
  if (amount == 0) return;
  z_x64_append_u8(buf, 0x48);
  if (amount <= 127) {
    z_x64_append_u8(buf, 0x83);
    z_x64_append_u8(buf, 0xc4);
    z_x64_append_u8(buf, amount);
  } else {
    z_x64_append_u8(buf, 0x81);
    z_x64_append_u8(buf, 0xc4);
    z_x64_append_u32(buf, amount);
  }
}
