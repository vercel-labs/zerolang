#include "x64_emit.h"

#include <stdlib.h>

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

static void z_x64_require_reg(unsigned reg) {
  if (reg >= 16) abort();
}

static void z_x64_require_sib_index(unsigned reg) {
  z_x64_require_reg(reg);
  // SIB index field 4 without REX.X encodes no index, so rsp cannot be used here.
  if (reg == 4) abort();
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

size_t z_x64_emit_call_rip32_placeholder(ZBuf *buf) {
  z_x64_append_u8(buf, 0xff);
  z_x64_append_u8(buf, 0x15);
  size_t patch = buf->len;
  z_x64_append_u32(buf, 0);
  return patch;
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

static void z_x64_emit_rsp_offset_reg(ZBuf *buf, unsigned opcode, unsigned reg, unsigned offset, bool wide) {
  if (wide || reg >= 8) {
    unsigned rex = wide ? 0x48 : 0x40;
    if (reg >= 8) rex |= 0x04;
    z_x64_append_u8(buf, rex);
  }
  z_x64_append_u8(buf, opcode);
  unsigned reg_low = reg & 7u;
  if (offset == 0) {
    z_x64_append_u8(buf, (reg_low << 3) | 0x04);
    z_x64_append_u8(buf, 0x24);
  } else if (offset <= 127) {
    z_x64_append_u8(buf, 0x40 | (reg_low << 3) | 0x04);
    z_x64_append_u8(buf, 0x24);
    z_x64_append_u8(buf, offset);
  } else {
    z_x64_append_u8(buf, 0x80 | (reg_low << 3) | 0x04);
    z_x64_append_u8(buf, 0x24);
    z_x64_append_u32(buf, offset);
  }
}

void z_x64_emit_load_rsp_offset_reg(ZBuf *buf, unsigned reg, unsigned offset, bool wide) {
  z_x64_emit_rsp_offset_reg(buf, 0x8b, reg, offset, wide);
}

void z_x64_emit_store_rsp_offset_reg(ZBuf *buf, unsigned reg, unsigned offset, bool wide) {
  z_x64_emit_rsp_offset_reg(buf, 0x89, reg, offset, wide);
}

void z_x64_emit_lea_rsp_offset_reg(ZBuf *buf, unsigned reg, unsigned offset) {
  z_x64_emit_rsp_offset_reg(buf, 0x8d, reg, offset, true);
}

void z_x64_emit_mov_rsp_offset_u32(ZBuf *buf, unsigned offset, uint32_t value, bool wide) {
  if (wide) z_x64_append_u8(buf, 0x48);
  z_x64_append_u8(buf, 0xc7);
  if (offset == 0) {
    z_x64_append_u8(buf, 0x04);
    z_x64_append_u8(buf, 0x24);
  } else if (offset <= 127) {
    z_x64_append_u8(buf, 0x44);
    z_x64_append_u8(buf, 0x24);
    z_x64_append_u8(buf, offset);
  } else {
    z_x64_append_u8(buf, 0x84);
    z_x64_append_u8(buf, 0x24);
    z_x64_append_u32(buf, offset);
  }
  z_x64_append_u32(buf, value);
}

void z_x64_emit_inc_rsp_offset64(ZBuf *buf, unsigned offset) {
  z_x64_emit_rsp_offset_reg(buf, 0xff, 0, offset, true);
}

void z_x64_emit_add_rax_rsp_offset(ZBuf *buf, unsigned offset) {
  z_x64_emit_rsp_offset_reg(buf, 0x03, 0, offset, true);
}

void z_x64_emit_cmp_rax_rsp_offset(ZBuf *buf, unsigned offset) {
  z_x64_emit_rsp_offset_reg(buf, 0x3b, 0, offset, true);
}

void z_x64_emit_cmp_reg_reg(ZBuf *buf, unsigned lhs, unsigned rhs, bool wide) {
  z_x64_require_reg(lhs);
  z_x64_require_reg(rhs);
  unsigned rex = wide ? 0x48 : 0x40;
  if (rhs >= 8) rex |= 0x04;
  if (lhs >= 8) rex |= 0x01;
  if (rex != 0x40) z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, 0x39);
  z_x64_append_u8(buf, 0xc0 | ((rhs & 7u) << 3) | (lhs & 7u));
}

void z_x64_emit_mov_reg_from_rax(ZBuf *buf, unsigned reg, bool wide) {
  z_x64_require_reg(reg);
  unsigned rex = wide ? 0x48 : 0x40;
  if (reg >= 8) rex |= 0x01;
  if (rex != 0x40) z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, 0x89);
  z_x64_append_u8(buf, 0xc0 | (reg & 7u));
}

void z_x64_emit_mov_reg_from_reg(ZBuf *buf, unsigned dst_reg, unsigned src_reg, bool wide) {
  z_x64_require_reg(dst_reg);
  z_x64_require_reg(src_reg);
  unsigned rex = wide ? 0x48 : 0x40;
  if (src_reg >= 8) rex |= 0x04;
  if (dst_reg >= 8) rex |= 0x01;
  if (rex != 0x40) z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, 0x89);
  z_x64_append_u8(buf, 0xc0 | ((src_reg & 7u) << 3) | (dst_reg & 7u));
}

void z_x64_emit_mov_reg_u32(ZBuf *buf, unsigned reg, uint32_t value) {
  z_x64_require_reg(reg);
  if (reg >= 8) z_x64_append_u8(buf, 0x41);
  z_x64_append_u8(buf, 0xb8 + (reg & 7u));
  z_x64_append_u32(buf, value);
}

void z_x64_emit_mov_reg_i32(ZBuf *buf, unsigned reg, int32_t value) {
  z_x64_require_reg(reg);
  unsigned rex = 0x48;
  if (reg >= 8) rex |= 0x01;
  z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, 0xc7);
  z_x64_append_u8(buf, 0xc0 | (reg & 7u));
  z_x64_append_u32(buf, (uint32_t)value);
}

void z_x64_emit_mov_reg_u64(ZBuf *buf, unsigned reg, uint64_t value) {
  z_x64_require_reg(reg);
  unsigned rex = 0x48;
  if (reg >= 8) rex |= 0x01;
  z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, 0xb8 + (reg & 7u));
  z_x64_append_u64(buf, value);
}

void z_x64_emit_xor_reg_reg(ZBuf *buf, unsigned reg, bool wide) {
  z_x64_require_reg(reg);
  unsigned rex = wide ? 0x48 : 0x40;
  if (reg >= 8) rex |= 0x05;
  if (rex != 0x40) z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, 0x31);
  z_x64_append_u8(buf, 0xc0 | ((reg & 7u) << 3) | (reg & 7u));
}

static void z_x64_emit_reg_reg_op(ZBuf *buf, unsigned opcode, unsigned dst_reg, unsigned src_reg, bool wide) {
  z_x64_require_reg(dst_reg);
  z_x64_require_reg(src_reg);
  unsigned rex = wide ? 0x48 : 0x40;
  if (src_reg >= 8) rex |= 0x04;
  if (dst_reg >= 8) rex |= 0x01;
  if (rex != 0x40) z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, opcode);
  z_x64_append_u8(buf, 0xc0 | ((src_reg & 7u) << 3) | (dst_reg & 7u));
}

void z_x64_emit_add_reg_reg(ZBuf *buf, unsigned dst_reg, unsigned src_reg, bool wide) {
  z_x64_emit_reg_reg_op(buf, 0x01, dst_reg, src_reg, wide);
}

void z_x64_emit_sub_reg_reg(ZBuf *buf, unsigned dst_reg, unsigned src_reg, bool wide) {
  z_x64_emit_reg_reg_op(buf, 0x29, dst_reg, src_reg, wide);
}

void z_x64_emit_xor_reg_from_reg(ZBuf *buf, unsigned dst_reg, unsigned src_reg, bool wide) {
  z_x64_emit_reg_reg_op(buf, 0x31, dst_reg, src_reg, wide);
}

static void z_x64_emit_reg_i8_op(ZBuf *buf, unsigned modrm_op, unsigned reg, int8_t value, bool wide) {
  if (modrm_op > 7) abort();
  z_x64_require_reg(reg);
  unsigned rex = wide ? 0x48 : 0x40;
  if (reg >= 8) rex |= 0x01;
  if (rex != 0x40) z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, 0x83);
  z_x64_append_u8(buf, 0xc0 | ((modrm_op & 7u) << 3) | (reg & 7u));
  z_x64_append_u8(buf, (uint8_t)value);
}

void z_x64_emit_add_reg_i8(ZBuf *buf, unsigned reg, int8_t value, bool wide) {
  z_x64_emit_reg_i8_op(buf, 0, reg, value, wide);
}

void z_x64_emit_and_reg_i8(ZBuf *buf, unsigned reg, int8_t value, bool wide) {
  z_x64_emit_reg_i8_op(buf, 4, reg, value, wide);
}

void z_x64_emit_cmp_reg_i8(ZBuf *buf, unsigned reg, int8_t value, bool wide) {
  z_x64_emit_reg_i8_op(buf, 7, reg, value, wide);
}

void z_x64_emit_and_reg_u32(ZBuf *buf, unsigned reg, uint32_t value, bool wide) {
  z_x64_require_reg(reg);
  unsigned rex = wide ? 0x48 : 0x40;
  if (reg >= 8) rex |= 0x01;
  if (rex != 0x40) z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, 0x81);
  z_x64_append_u8(buf, 0xe0 | (reg & 7u));
  z_x64_append_u32(buf, value);
}

void z_x64_emit_neg_reg(ZBuf *buf, unsigned reg, bool wide) {
  z_x64_require_reg(reg);
  unsigned rex = wide ? 0x48 : 0x40;
  if (reg >= 8) rex |= 0x01;
  if (rex != 0x40) z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, 0xf7);
  z_x64_append_u8(buf, 0xd8 | (reg & 7u));
}

void z_x64_emit_shr_reg_one(ZBuf *buf, unsigned reg, bool wide) {
  z_x64_require_reg(reg);
  unsigned rex = wide ? 0x48 : 0x40;
  if (reg >= 8) rex |= 0x01;
  if (rex != 0x40) z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, 0xd1);
  z_x64_append_u8(buf, 0xe8 | (reg & 7u));
}

static void z_x64_emit_shift_reg_imm8(ZBuf *buf, unsigned modrm_op, unsigned reg, unsigned amount, bool wide) {
  if (modrm_op > 7 || amount > 0xff) abort();
  z_x64_require_reg(reg);
  unsigned rex = wide ? 0x48 : 0x40;
  if (reg >= 8) rex |= 0x01;
  if (rex != 0x40) z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, 0xc1);
  z_x64_append_u8(buf, 0xc0 | ((modrm_op & 7u) << 3) | (reg & 7u));
  z_x64_append_u8(buf, amount);
}

void z_x64_emit_shl_reg_imm8(ZBuf *buf, unsigned reg, unsigned amount, bool wide) {
  z_x64_emit_shift_reg_imm8(buf, 4, reg, amount, wide);
}

void z_x64_emit_shr_reg_imm8(ZBuf *buf, unsigned reg, unsigned amount, bool wide) {
  z_x64_emit_shift_reg_imm8(buf, 5, reg, amount, wide);
}

void z_x64_emit_imul_reg_i32(ZBuf *buf, unsigned reg, int32_t value, bool wide) {
  z_x64_require_reg(reg);
  unsigned rex = wide ? 0x48 : 0x40;
  if (reg >= 8) rex |= 0x05;
  if (rex != 0x40) z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, 0x69);
  z_x64_append_u8(buf, 0xc0 | ((reg & 7u) << 3) | (reg & 7u));
  z_x64_append_u32(buf, (uint32_t)value);
}

void z_x64_emit_add_rax_u32(ZBuf *buf, uint32_t value, bool wide) {
  z_x64_emit_wide_prefix(buf, wide);
  z_x64_append_u8(buf, 0x05);
  z_x64_append_u32(buf, value);
}

void z_x64_emit_sub_rax_u32(ZBuf *buf, uint32_t value, bool wide) {
  z_x64_emit_wide_prefix(buf, wide);
  z_x64_append_u8(buf, 0x2d);
  z_x64_append_u32(buf, value);
}

static unsigned z_x64_scale_bits(unsigned scale) {
  switch (scale) {
    case 1: return 0;
    case 2: return 1;
    case 4: return 2;
    case 8: return 3;
  }
  abort();
}

static void z_x64_emit_base_index_scale_disp_op(ZBuf *buf, unsigned opcode, unsigned reg, unsigned base_reg, unsigned index_reg, unsigned scale, unsigned disp, bool wide, bool reg_is_byte) {
  z_x64_require_reg(reg);
  z_x64_require_reg(base_reg);
  z_x64_require_sib_index(index_reg);
  bool force_rex = !wide && reg_is_byte && reg >= 4 && reg < 8;
  unsigned rex = wide ? 0x48 : 0x40;
  if (reg >= 8) rex |= 0x04;
  if (index_reg >= 8) rex |= 0x02;
  if (base_reg >= 8) rex |= 0x01;
  if (rex != 0x40 || force_rex) z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, opcode);
  bool needs_base_disp = (base_reg & 7u) == 5;
  unsigned mod = 0x80;
  if (disp == 0 && !needs_base_disp) mod = 0x00;
  else if (disp <= 127) mod = 0x40;
  z_x64_append_u8(buf, mod | ((reg & 7u) << 3) | 0x04);
  z_x64_append_u8(buf, (z_x64_scale_bits(scale) << 6) | ((index_reg & 7u) << 3) | (base_reg & 7u));
  if (mod == 0x40) z_x64_append_u8(buf, disp);
  else if (mod == 0x80) z_x64_append_u32(buf, disp);
}

static void z_x64_emit_base_index_reg(ZBuf *buf, unsigned opcode, unsigned reg, unsigned base_reg, unsigned index_reg, bool wide, bool reg_is_byte) {
  z_x64_emit_base_index_scale_disp_op(buf, opcode, reg, base_reg, index_reg, 1, 0, wide, reg_is_byte);
}

void z_x64_emit_load_reg8_base_index(ZBuf *buf, unsigned dst_reg, unsigned base_reg, unsigned index_reg) {
  z_x64_emit_base_index_reg(buf, 0x8a, dst_reg, base_reg, index_reg, false, true);
}

void z_x64_emit_movzx_reg32_base_index_u8(ZBuf *buf, unsigned dst_reg, unsigned base_reg, unsigned index_reg) {
  z_x64_require_reg(dst_reg);
  z_x64_require_reg(base_reg);
  z_x64_require_sib_index(index_reg);
  unsigned rex = 0x40;
  if (dst_reg >= 8) rex |= 0x04;
  if (index_reg >= 8) rex |= 0x02;
  if (base_reg >= 8) rex |= 0x01;
  if (rex != 0x40) z_x64_append_u8(buf, rex);
  z_x64_append_u8(buf, 0x0f);
  z_x64_append_u8(buf, 0xb6);
  unsigned mod = (base_reg & 7u) == 5 ? 0x40 : 0x00;
  z_x64_append_u8(buf, mod | ((dst_reg & 7u) << 3) | 0x04);
  z_x64_append_u8(buf, ((index_reg & 7u) << 3) | (base_reg & 7u));
  if (mod == 0x40) z_x64_append_u8(buf, 0);
}

void z_x64_emit_store_base_index_reg8(ZBuf *buf, unsigned base_reg, unsigned index_reg, unsigned src_reg) {
  z_x64_emit_base_index_reg(buf, 0x88, src_reg, base_reg, index_reg, false, true);
}

void z_x64_emit_cmp_base_index_reg8(ZBuf *buf, unsigned base_reg, unsigned index_reg, unsigned reg) {
  z_x64_emit_base_index_reg(buf, 0x38, reg, base_reg, index_reg, false, true);
}

void z_x64_emit_cmp_base_index_u8(ZBuf *buf, unsigned base_reg, unsigned index_reg, unsigned value) {
  if (value > 0xff) abort();
  z_x64_emit_base_index_reg(buf, 0x80, 7, base_reg, index_reg, false, false);
  z_x64_append_u8(buf, value);
}

void z_x64_emit_load_base_index_scale_disp_reg(ZBuf *buf, unsigned dst_reg, unsigned base_reg, unsigned index_reg, unsigned scale, unsigned disp, bool wide) {
  z_x64_emit_base_index_scale_disp_op(buf, 0x8b, dst_reg, base_reg, index_reg, scale, disp, wide, false);
}

void z_x64_emit_lea_base_index_scale_disp_reg(ZBuf *buf, unsigned dst_reg, unsigned base_reg, unsigned index_reg, unsigned scale, unsigned disp) {
  z_x64_emit_base_index_scale_disp_op(buf, 0x8d, dst_reg, base_reg, index_reg, scale, disp, true, false);
}

void z_x64_emit_xor_r8d_r8d(ZBuf *buf) {
  z_x64_append_u8(buf, 0x45);
  z_x64_append_u8(buf, 0x31);
  z_x64_append_u8(buf, 0xc0);
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

void z_x64_emit_mov_r9_from_rax(ZBuf *buf) {
  z_x64_append_u8(buf, 0x49);
  z_x64_append_u8(buf, 0x89);
  z_x64_append_u8(buf, 0xc1);
}

void z_x64_emit_mov_rax_from_rcx(ZBuf *buf) {
  z_x64_append_u8(buf, 0x48);
  z_x64_append_u8(buf, 0x89);
  z_x64_append_u8(buf, 0xc8);
}

void z_x64_emit_mov_rdx_from_rax(ZBuf *buf) {
  z_x64_append_u8(buf, 0x48);
  z_x64_append_u8(buf, 0x89);
  z_x64_append_u8(buf, 0xc2);
}

void z_x64_emit_mov_rdi_from_rax(ZBuf *buf) {
  z_x64_append_u8(buf, 0x48);
  z_x64_append_u8(buf, 0x89);
  z_x64_append_u8(buf, 0xc7);
}

void z_x64_emit_mov_rsi_from_rax(ZBuf *buf) {
  z_x64_append_u8(buf, 0x48);
  z_x64_append_u8(buf, 0x89);
  z_x64_append_u8(buf, 0xc6);
}

void z_x64_emit_mov_rsi_from_rsp(ZBuf *buf) {
  z_x64_append_u8(buf, 0x48);
  z_x64_append_u8(buf, 0x89);
  z_x64_append_u8(buf, 0xe6);
}

void z_x64_emit_mov_rax_from_rdx(ZBuf *buf) {
  z_x64_append_u8(buf, 0x48);
  z_x64_append_u8(buf, 0x89);
  z_x64_append_u8(buf, 0xd0);
}

void z_x64_emit_mov_rax_from_rdi(ZBuf *buf) {
  z_x64_append_u8(buf, 0x48);
  z_x64_append_u8(buf, 0x89);
  z_x64_append_u8(buf, 0xf8);
}

void z_x64_emit_mov_eax_from_ecx(ZBuf *buf) {
  z_x64_append_u8(buf, 0x89);
  z_x64_append_u8(buf, 0xc8);
}

size_t z_x64_emit_mov_rax_u64_patchable(ZBuf *buf, uint64_t value) {
  z_x64_append_u8(buf, 0x48);
  z_x64_append_u8(buf, 0xb8);
  size_t patch = buf->len;
  z_x64_append_u64(buf, value);
  return patch;
}

void z_x64_emit_mov_rax_u64(ZBuf *buf, uint64_t value) {
  (void)z_x64_emit_mov_rax_u64_patchable(buf, value);
}

void z_x64_emit_xor_eax_eax(ZBuf *buf) {
  z_x64_append_u8(buf, 0x31);
  z_x64_append_u8(buf, 0xc0);
}

void z_x64_emit_xor_ecx_ecx(ZBuf *buf) {
  z_x64_append_u8(buf, 0x31);
  z_x64_append_u8(buf, 0xc9);
}

void z_x64_emit_xor_rdi_rdi(ZBuf *buf) {
  z_x64_append_u8(buf, 0x48);
  z_x64_append_u8(buf, 0x31);
  z_x64_append_u8(buf, 0xff);
}

void z_x64_emit_xor_rax_rax(ZBuf *buf) {
  z_x64_append_u8(buf, 0x48);
  z_x64_emit_xor_eax_eax(buf);
}

void z_x64_emit_inc_ecx(ZBuf *buf) {
  z_x64_append_u8(buf, 0xff);
  z_x64_append_u8(buf, 0xc1);
}

void z_x64_emit_inc_rcx(ZBuf *buf) {
  z_x64_append_u8(buf, 0x48);
  z_x64_emit_inc_ecx(buf);
}

void z_x64_emit_inc_r8(ZBuf *buf) {
  z_x64_append_u8(buf, 0x49);
  z_x64_append_u8(buf, 0xff);
  z_x64_append_u8(buf, 0xc0);
}

void z_x64_emit_dec_r8d(ZBuf *buf) {
  z_x64_append_u8(buf, 0x41);
  z_x64_append_u8(buf, 0xff);
  z_x64_append_u8(buf, 0xc8);
}

void z_x64_emit_add_rax_rcx(ZBuf *buf, bool wide) {
  z_x64_emit_add_reg_reg(buf, 0, 1, wide);
}

void z_x64_emit_sub_rax_rcx(ZBuf *buf, bool wide) {
  z_x64_emit_sub_reg_reg(buf, 0, 1, wide);
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

void z_x64_emit_add_rdx_rcx(ZBuf *buf, bool wide) {
  z_x64_emit_add_reg_reg(buf, 2, 1, wide);
}

void z_x64_emit_shl_rcx_imm8(ZBuf *buf, unsigned amount) {
  z_x64_emit_shl_reg_imm8(buf, 1, amount, true);
}

void z_x64_emit_shr_rcx_imm8(ZBuf *buf, unsigned amount) {
  z_x64_emit_shr_reg_imm8(buf, 1, amount, true);
}

static void z_x64_emit_ptr_reg_disp_op(ZBuf *buf, unsigned escape, unsigned opcode, unsigned reg, unsigned base_reg, unsigned disp, bool wide, bool reg_is_byte) {
  z_x64_require_reg(reg);
  z_x64_require_reg(base_reg);
  bool force_rex = !wide && reg_is_byte && reg >= 4 && reg < 8;
  unsigned rex = wide ? 0x48 : 0x40;
  if (reg >= 8) rex |= 0x04;
  if (base_reg >= 8) rex |= 0x01;
  if (rex != 0x40 || force_rex) z_x64_append_u8(buf, rex);
  if (escape) z_x64_append_u8(buf, escape);
  z_x64_append_u8(buf, opcode);
  bool sib = (base_reg & 7u) == 4;
  bool needs_base_disp = (base_reg & 7u) == 5;
  unsigned mod = 0x80;
  if (disp == 0 && !needs_base_disp) mod = 0x00;
  else if (disp <= 127) mod = 0x40;
  z_x64_append_u8(buf, mod | ((reg & 7u) << 3) | (sib ? 0x04 : (base_reg & 7u)));
  if (sib) z_x64_append_u8(buf, 0x20 | (base_reg & 7u));
  if (mod == 0x40) z_x64_append_u8(buf, disp);
  else if (mod == 0x80) z_x64_append_u32(buf, disp);
}

void z_x64_emit_movzx_reg32_ptr_reg_u8(ZBuf *buf, unsigned dst_reg, unsigned base_reg) { z_x64_emit_ptr_reg_disp_op(buf, 0x0f, 0xb6, dst_reg, base_reg, 0, false, false); }

void z_x64_emit_movzx_reg32_ptr_reg_disp_u16(ZBuf *buf, unsigned dst_reg, unsigned base_reg, unsigned disp) { z_x64_emit_ptr_reg_disp_op(buf, 0x0f, 0xb7, dst_reg, base_reg, disp, false, false); }

void z_x64_emit_load_reg_ptr_reg(ZBuf *buf, unsigned dst_reg, unsigned base_reg, bool wide) { z_x64_emit_ptr_reg_disp_op(buf, 0, 0x8b, dst_reg, base_reg, 0, wide, false); }

void z_x64_emit_mov_ptr_reg_disp_u8(ZBuf *buf, unsigned base_reg, unsigned disp, unsigned value) {
  if (value > 0xff) abort();
  z_x64_emit_ptr_reg_disp_op(buf, 0, 0xc6, 0, base_reg, disp, false, false);
  z_x64_append_u8(buf, value);
}

void z_x64_emit_store_ptr_reg8_from_reg(ZBuf *buf, unsigned base_reg, unsigned src_reg) { z_x64_emit_ptr_reg_disp_op(buf, 0, 0x88, src_reg, base_reg, 0, false, true); }

void z_x64_emit_store_ptr_reg_from_reg(ZBuf *buf, unsigned base_reg, unsigned src_reg, bool wide) { z_x64_emit_ptr_reg_disp_op(buf, 0, 0x89, src_reg, base_reg, 0, wide, false); }

void z_x64_emit_cmp_reg_ptr_reg(ZBuf *buf, unsigned lhs_reg, unsigned base_reg, bool wide) { z_x64_emit_ptr_reg_disp_op(buf, 0, 0x3b, lhs_reg, base_reg, 0, wide, false); }

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

void z_x64_emit_test_reg_reg(ZBuf *buf, unsigned reg, bool wide) {
  z_x64_emit_reg_reg_op(buf, 0x85, reg, reg, wide);
}

void z_x64_emit_test_rax_rax(ZBuf *buf, bool wide) {
  z_x64_emit_test_reg_reg(buf, 0, wide);
}

void z_x64_emit_test_ecx_ecx(ZBuf *buf) {
  z_x64_emit_test_reg_reg(buf, 1, false);
}

void z_x64_emit_cmp_rax_rcx(ZBuf *buf, bool wide) {
  z_x64_emit_cmp_reg_reg(buf, 0, 1, wide);
}

void z_x64_emit_setcc_al_to_bool(ZBuf *buf, unsigned setcc_opcode) {
  if (setcc_opcode < 0x90 || setcc_opcode > 0x9f) abort();
  z_x64_append_u8(buf, 0x0f);
  z_x64_append_u8(buf, setcc_opcode);
  z_x64_append_u8(buf, 0xc0);
  z_x64_append_u8(buf, 0x0f);
  z_x64_append_u8(buf, 0xb6);
  z_x64_append_u8(buf, 0xc0);
}

void z_x64_emit_cmp_rax_rcx_to_bool(ZBuf *buf, unsigned setcc_opcode, bool wide) {
  z_x64_emit_cmp_rax_rcx(buf, wide);
  z_x64_emit_setcc_al_to_bool(buf, setcc_opcode);
}

void z_x64_emit_bool_from_nonnegative_rax(ZBuf *buf) {
  z_x64_emit_test_rax_rax(buf, true);
  z_x64_emit_setcc_al_to_bool(buf, 0x99);
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

void z_x64_emit_syscall(ZBuf *buf) {
  z_x64_append_u8(buf, 0x0f);
  z_x64_append_u8(buf, 0x05);
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
