#include "aarch64_emit.h"

size_t z_aarch64_align(size_t value, size_t alignment) {
  size_t remainder = alignment ? value % alignment : 0;
  return remainder == 0 ? value : value + (alignment - remainder);
}

void z_aarch64_append_u8(ZBuf *buf, unsigned value) {
  zbuf_append_char(buf, (char)(value & 0xffu));
}

void z_aarch64_append_u32(ZBuf *buf, uint32_t value) {
  z_aarch64_append_u8(buf, value);
  z_aarch64_append_u8(buf, value >> 8);
  z_aarch64_append_u8(buf, value >> 16);
  z_aarch64_append_u8(buf, value >> 24);
}

void z_aarch64_append_u64(ZBuf *buf, uint64_t value) {
  z_aarch64_append_u32(buf, (uint32_t)value);
  z_aarch64_append_u32(buf, (uint32_t)(value >> 32));
}

void z_aarch64_append_zeros(ZBuf *buf, size_t len) {
  for (size_t i = 0; i < len; i++) z_aarch64_append_u8(buf, 0);
}

void z_aarch64_pad_to(ZBuf *buf, size_t offset) {
  while (buf->len < offset) z_aarch64_append_u8(buf, 0);
}

void z_aarch64_patch_u32(ZBuf *buf, size_t offset, uint32_t value) {
  buf->data[offset + 0] = (char)(value & 0xffu);
  buf->data[offset + 1] = (char)((value >> 8) & 0xffu);
  buf->data[offset + 2] = (char)((value >> 16) & 0xffu);
  buf->data[offset + 3] = (char)((value >> 24) & 0xffu);
}

void z_aarch64_patch_u64(ZBuf *buf, size_t offset, uint64_t value) {
  z_aarch64_patch_u32(buf, offset, (uint32_t)value);
  z_aarch64_patch_u32(buf, offset + 4, (uint32_t)(value >> 32));
}

void z_aarch64_emit_ret(ZBuf *text) {
  z_aarch64_append_u32(text, 0xd65f03c0u);
}

void z_aarch64_emit_blr_x(ZBuf *text, unsigned reg) {
  z_aarch64_append_u32(text, 0xd63f0000u | ((reg & 31u) << 5));
}

void z_aarch64_emit_nop(ZBuf *text) {
  z_aarch64_append_u32(text, 0xd503201fu);
}

void z_aarch64_emit_brk(ZBuf *text) {
  z_aarch64_append_u32(text, 0xd4200000u);
}

void z_aarch64_emit_svc(ZBuf *text, unsigned imm16) {
  z_aarch64_append_u32(text, 0xd4000001u | ((imm16 & 0xffffu) << 5));
}

void z_aarch64_emit_literal_return(ZBuf *text, uint32_t literal) {
  z_aarch64_emit_movz_w(text, 0, literal);
  z_aarch64_emit_ret(text);
}

void z_aarch64_emit_movz_w(ZBuf *text, unsigned reg, uint32_t literal) {
  z_aarch64_append_u32(text, 0x52800000u | ((literal & 0xffffu) << 5) | (reg & 31u));
  if (literal > 0xffffu) {
    z_aarch64_append_u32(text, 0x72a00000u | (((literal >> 16) & 0xffffu) << 5) | (reg & 31u));
  }
}

void z_aarch64_emit_movz_x(ZBuf *text, unsigned reg, uint64_t literal) {
  z_aarch64_append_u32(text, 0xd2800000u | ((uint32_t)(literal & 0xffffu) << 5) | (reg & 31u));
  if (literal > 0xffffu) {
    z_aarch64_append_u32(text, 0xf2a00000u | ((uint32_t)((literal >> 16) & 0xffffu) << 5) | (reg & 31u));
  }
  if (literal > 0xffffffffu) {
    z_aarch64_append_u32(text, 0xf2c00000u | ((uint32_t)((literal >> 32) & 0xffffu) << 5) | (reg & 31u));
  }
  if (literal > 0xffffffffffffu) {
    z_aarch64_append_u32(text, 0xf2e00000u | ((uint32_t)((literal >> 48) & 0xffffu) << 5) | (reg & 31u));
  }
}

void z_aarch64_emit_mov_w(ZBuf *text, unsigned dst, unsigned src) {
  z_aarch64_append_u32(text, 0x2a0003e0u | ((src & 31u) << 16) | (dst & 31u));
}

void z_aarch64_emit_mov_x(ZBuf *text, unsigned dst, unsigned src) {
  z_aarch64_append_u32(text, 0xaa0003e0u | ((src & 31u) << 16) | (dst & 31u));
}

void z_aarch64_emit_uxtb_w(ZBuf *text, unsigned dst, unsigned src) {
  z_aarch64_append_u32(text, 0x53000000u | (7u << 10) | ((src & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_uxth_w(ZBuf *text, unsigned dst, unsigned src) {
  z_aarch64_append_u32(text, 0x53000000u | (15u << 10) | ((src & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_sxtw_x(ZBuf *text, unsigned dst, unsigned src) {
  z_aarch64_append_u32(text, 0x93400000u | (31u << 10) | ((src & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_mov_x29_sp(ZBuf *text) {
  z_aarch64_append_u32(text, 0x910003fdu);
}

void z_aarch64_emit_stp_x20_x21_sp_pre16(ZBuf *text) {
  z_aarch64_append_u32(text, 0xa9bf57f4u);
}

void z_aarch64_emit_stp_x29_x30_sp_pre16(ZBuf *text) {
  z_aarch64_append_u32(text, 0xa9bf7bfdu);
}

void z_aarch64_emit_ldp_x20_x21_sp_post16(ZBuf *text) {
  z_aarch64_append_u32(text, 0xa8c157f4u);
}

void z_aarch64_emit_ldp_x29_x30_sp_post16(ZBuf *text) {
  z_aarch64_append_u32(text, 0xa8c17bfdu);
}

void z_aarch64_emit_add_sp_imm(ZBuf *text, unsigned imm) {
  z_aarch64_append_u32(text, 0x910003ffu | ((imm & 0xfffu) << 10));
}

void z_aarch64_emit_sub_sp_imm(ZBuf *text, unsigned imm) {
  z_aarch64_append_u32(text, 0xd10003ffu | ((imm & 0xfffu) << 10));
}

void z_aarch64_emit_add_x_sp_imm(ZBuf *text, unsigned dst, unsigned imm) {
  z_aarch64_append_u32(text, 0x910003e0u | ((imm & 0xfffu) << 10) | (dst & 31u));
}

void z_aarch64_emit_add_x_imm(ZBuf *text, unsigned dst, unsigned src, unsigned imm) {
  z_aarch64_append_u32(text, 0x91000000u | ((imm & 0xfffu) << 10) | ((src & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_add_w_imm(ZBuf *text, unsigned dst, unsigned src, unsigned imm) {
  z_aarch64_append_u32(text, 0x11000000u | ((imm & 0xfffu) << 10) | ((src & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_sub_w_imm(ZBuf *text, unsigned dst, unsigned src, unsigned imm) {
  z_aarch64_append_u32(text, 0x51000000u | ((imm & 0xfffu) << 10) | ((src & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_load_w_sp(ZBuf *text, unsigned reg, unsigned offset) {
  z_aarch64_append_u32(text, 0xb9400000u | ((offset / 4u) << 10) | (31u << 5) | (reg & 31u));
}

void z_aarch64_emit_load_x_sp(ZBuf *text, unsigned reg, unsigned offset) {
  z_aarch64_append_u32(text, 0xf9400000u | ((offset / 8u) << 10) | (31u << 5) | (reg & 31u));
}

void z_aarch64_emit_load_b_sp(ZBuf *text, unsigned reg, unsigned offset) {
  z_aarch64_append_u32(text, 0x39400000u | ((offset & 0xfffu) << 10) | (31u << 5) | (reg & 31u));
}

void z_aarch64_emit_store_w_sp(ZBuf *text, unsigned reg, unsigned offset) {
  z_aarch64_append_u32(text, 0xb9000000u | ((offset / 4u) << 10) | (31u << 5) | (reg & 31u));
}

void z_aarch64_emit_store_x_sp(ZBuf *text, unsigned reg, unsigned offset) {
  z_aarch64_append_u32(text, 0xf9000000u | ((offset / 8u) << 10) | (31u << 5) | (reg & 31u));
}

void z_aarch64_emit_store_b_sp(ZBuf *text, unsigned reg, unsigned offset) {
  z_aarch64_append_u32(text, 0x39000000u | ((offset & 0xfffu) << 10) | (31u << 5) | (reg & 31u));
}

void z_aarch64_emit_load_w_imm(ZBuf *text, unsigned dst, unsigned base, unsigned byte_offset) {
  z_aarch64_append_u32(text, 0xb9400000u | (((byte_offset / 4u) & 0xfffu) << 10) | ((base & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_load_x_imm(ZBuf *text, unsigned dst, unsigned base, unsigned byte_offset) {
  z_aarch64_append_u32(text, 0xf9400000u | (((byte_offset / 8u) & 0xfffu) << 10) | ((base & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_load_b_imm(ZBuf *text, unsigned dst, unsigned base, unsigned byte_offset) {
  z_aarch64_append_u32(text, 0x39400000u | ((byte_offset & 0xfffu) << 10) | ((base & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_load_h_imm(ZBuf *text, unsigned dst, unsigned base, unsigned byte_offset) {
  z_aarch64_append_u32(text, 0x79400000u | (((byte_offset / 2u) & 0xfffu) << 10) | ((base & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_store_w_imm(ZBuf *text, unsigned src, unsigned base, unsigned byte_offset) {
  z_aarch64_append_u32(text, 0xb9000000u | (((byte_offset / 4u) & 0xfffu) << 10) | ((base & 31u) << 5) | (src & 31u));
}

void z_aarch64_emit_store_x_imm(ZBuf *text, unsigned src, unsigned base, unsigned byte_offset) {
  z_aarch64_append_u32(text, 0xf9000000u | (((byte_offset / 8u) & 0xfffu) << 10) | ((base & 31u) << 5) | (src & 31u));
}

void z_aarch64_emit_store_b_imm(ZBuf *text, unsigned src, unsigned base, unsigned byte_offset) {
  z_aarch64_append_u32(text, 0x39000000u | ((byte_offset & 0xfffu) << 10) | ((base & 31u) << 5) | (src & 31u));
}

void z_aarch64_emit_store_h_imm(ZBuf *text, unsigned src, unsigned base, unsigned byte_offset) {
  z_aarch64_append_u32(text, 0x79000000u | (((byte_offset / 2u) & 0xfffu) << 10) | ((base & 31u) << 5) | (src & 31u));
}

void z_aarch64_emit_add_w_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs) {
  z_aarch64_append_u32(text, 0x0b000000u | ((rhs & 31u) << 16) | ((lhs & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_add_x_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs) {
  z_aarch64_append_u32(text, 0x8b000000u | ((rhs & 31u) << 16) | ((lhs & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_add_x_reg_lsl(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs, unsigned shift) {
  z_aarch64_append_u32(text, 0x8b000000u | ((rhs & 31u) << 16) | ((shift & 0x3fu) << 10) | ((lhs & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_lsr_x_imm(ZBuf *text, unsigned dst, unsigned src, unsigned shift) {
  z_aarch64_append_u32(text, 0xd340fc00u | ((shift & 0x3fu) << 16) | ((src & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_sub_w_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs) {
  z_aarch64_append_u32(text, 0x4b000000u | ((rhs & 31u) << 16) | ((lhs & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_sub_x_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs) {
  z_aarch64_append_u32(text, 0xcb000000u | ((rhs & 31u) << 16) | ((lhs & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_mul_w_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs) {
  z_aarch64_append_u32(text, 0x1b000000u | ((rhs & 31u) << 16) | (31u << 10) | ((lhs & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_mul_x_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs) {
  z_aarch64_append_u32(text, 0x9b000000u | ((rhs & 31u) << 16) | (31u << 10) | ((lhs & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_div_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs, bool is_unsigned, bool wide) {
  uint32_t sf = wide ? 0x80000000u : 0;
  z_aarch64_append_u32(text, sf | (is_unsigned ? 0x1ac00800u : 0x1ac00c00u) | ((rhs & 31u) << 16) | ((lhs & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_msub_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs, unsigned acc, bool wide) {
  uint32_t sf = wide ? 0x80000000u : 0;
  z_aarch64_append_u32(text, sf | 0x1b008000u | ((rhs & 31u) << 16) | ((acc & 31u) << 10) | ((lhs & 31u) << 5) | (dst & 31u));
}

void z_aarch64_emit_cmp_w(ZBuf *text, unsigned lhs, unsigned rhs) {
  z_aarch64_append_u32(text, 0x6b00001fu | ((rhs & 31u) << 16) | ((lhs & 31u) << 5));
}

void z_aarch64_emit_cmp_x(ZBuf *text, unsigned lhs, unsigned rhs) {
  z_aarch64_append_u32(text, 0xeb00001fu | ((rhs & 31u) << 16) | ((lhs & 31u) << 5));
}

void z_aarch64_emit_adrp_add_placeholder(ZBuf *text, unsigned reg) {
  z_aarch64_append_u32(text, 0x90000000u | (reg & 31u));
  z_aarch64_append_u32(text, 0x91000000u | ((reg & 31u) << 5) | (reg & 31u));
}

void z_aarch64_emit_ldr_x_literal8(ZBuf *text, unsigned reg) {
  z_aarch64_append_u32(text, 0x58000000u | (2u << 5) | (reg & 31u));
}

void z_aarch64_emit_b_offset_words(ZBuf *text, int32_t words) {
  z_aarch64_append_u32(text, 0x14000000u | ((uint32_t)words & 0x03ffffffu));
}

void z_aarch64_emit_byte_copy_min_loop(ZBuf *text, unsigned result_reg) {
  z_aarch64_emit_mov_w(text, 14, 13);
  z_aarch64_emit_cmp_w(text, 13, 10);
  size_t keep_dst_len = z_aarch64_emit_b_cond_placeholder(text, 9);
  z_aarch64_emit_mov_w(text, 14, 10);
  z_aarch64_patch_cond19(text, keep_dst_len, text->len);
  z_aarch64_emit_movz_w(text, 9, 0);
  size_t loop = text->len;
  z_aarch64_emit_cmp_w(text, 9, 14);
  size_t done = z_aarch64_emit_b_cond_placeholder(text, 2);
  z_aarch64_emit_add_x_reg(text, 15, 11, 9);
  z_aarch64_emit_load_b_imm(text, 15, 15, 0);
  z_aarch64_emit_add_x_reg(text, 13, 12, 9);
  z_aarch64_emit_store_b_imm(text, 15, 13, 0);
  z_aarch64_emit_add_w_imm(text, 9, 9, 1);
  size_t back = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, back, loop);
  z_aarch64_patch_cond19(text, done, text->len);
  z_aarch64_emit_mov_w(text, result_reg, 14);
}

void z_aarch64_emit_byte_fill_loop(ZBuf *text, unsigned result_reg) {
  z_aarch64_emit_movz_w(text, 9, 0);
  size_t loop = text->len;
  z_aarch64_emit_cmp_w(text, 9, 10);
  size_t done = z_aarch64_emit_b_cond_placeholder(text, 2);
  z_aarch64_emit_add_x_reg(text, 12, 11, 9);
  z_aarch64_emit_store_b_imm(text, 8, 12, 0);
  z_aarch64_emit_add_w_imm(text, 9, 9, 1);
  size_t back = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, back, loop);
  z_aarch64_patch_cond19(text, done, text->len);
  z_aarch64_emit_mov_w(text, result_reg, 10);
}

void z_aarch64_emit_byte_eq_loop(ZBuf *text, unsigned result_reg) {
  z_aarch64_emit_movz_w(text, 9, 0);
  size_t loop = text->len;
  z_aarch64_emit_cmp_w(text, 9, 10);
  size_t equal = z_aarch64_emit_b_cond_placeholder(text, 2);
  z_aarch64_emit_add_x_reg(text, 13, 11, 9);
  z_aarch64_emit_load_b_imm(text, 13, 13, 0);
  z_aarch64_emit_add_x_reg(text, 14, 12, 9);
  z_aarch64_emit_load_b_imm(text, 14, 14, 0);
  z_aarch64_emit_cmp_w(text, 13, 14);
  size_t mismatch = z_aarch64_emit_b_cond_placeholder(text, 1);
  z_aarch64_emit_add_w_imm(text, 9, 9, 1);
  size_t back = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, back, loop);
  z_aarch64_patch_cond19(text, mismatch, text->len);
  z_aarch64_emit_movz_w(text, result_reg, 0);
  size_t after_false = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, equal, text->len);
  z_aarch64_emit_movz_w(text, result_reg, 1);
  z_aarch64_patch_branch26(text, after_false, text->len);
}

size_t z_aarch64_emit_bl_placeholder(ZBuf *text) {
  size_t patch = text->len;
  z_aarch64_append_u32(text, 0x94000000u);
  return patch;
}

size_t z_aarch64_emit_b_placeholder(ZBuf *text) {
  size_t patch = text->len;
  z_aarch64_append_u32(text, 0x14000000u);
  return patch;
}

size_t z_aarch64_emit_b_cond_placeholder(ZBuf *text, unsigned cond) {
  size_t patch = text->len;
  z_aarch64_append_u32(text, 0x54000000u | (cond & 15u));
  return patch;
}

size_t z_aarch64_emit_cbz_w_placeholder(ZBuf *text, unsigned reg) {
  size_t patch = text->len;
  z_aarch64_append_u32(text, 0x34000000u | (reg & 31u));
  return patch;
}

void z_aarch64_patch_branch26(ZBuf *text, size_t patch_offset, size_t target_offset) {
  uint32_t old_instr = ((unsigned char)text->data[patch_offset]) |
                       ((uint32_t)(unsigned char)text->data[patch_offset + 1] << 8) |
                       ((uint32_t)(unsigned char)text->data[patch_offset + 2] << 16) |
                       ((uint32_t)(unsigned char)text->data[patch_offset + 3] << 24);
  int64_t delta = (int64_t)target_offset - (int64_t)patch_offset;
  int64_t words = delta / 4;
  z_aarch64_patch_u32(text, patch_offset, (old_instr & 0xfc000000u) | ((uint32_t)words & 0x03ffffffu));
}

void z_aarch64_patch_cond19(ZBuf *text, size_t patch_offset, size_t target_offset) {
  uint32_t instr = ((unsigned char)text->data[patch_offset]) |
                   ((uint32_t)(unsigned char)text->data[patch_offset + 1] << 8) |
                   ((uint32_t)(unsigned char)text->data[patch_offset + 2] << 16) |
                   ((uint32_t)(unsigned char)text->data[patch_offset + 3] << 24);
  int64_t delta = (int64_t)target_offset - (int64_t)patch_offset;
  int64_t words = delta / 4;
  z_aarch64_patch_u32(text, patch_offset, (instr & 0xff00001fu) | (((uint32_t)words & 0x7ffffu) << 5));
}

void z_aarch64_patch_adrp_add(ZBuf *text, size_t patch_offset, uint64_t instr_addr, uint64_t target_addr) {
  uint32_t adrp = ((unsigned char)text->data[patch_offset]) |
                  ((uint32_t)(unsigned char)text->data[patch_offset + 1] << 8) |
                  ((uint32_t)(unsigned char)text->data[patch_offset + 2] << 16) |
                  ((uint32_t)(unsigned char)text->data[patch_offset + 3] << 24);
  unsigned reg = adrp & 31u;
  int64_t instr_page = (int64_t)(instr_addr & ~0xfffull);
  int64_t target_page = (int64_t)(target_addr & ~0xfffull);
  int64_t pages = (target_page - instr_page) / 4096;
  uint32_t immlo = (uint32_t)pages & 0x3u;
  uint32_t immhi = ((uint32_t)pages >> 2) & 0x7ffffu;
  uint32_t patched_adrp = 0x90000000u | (immlo << 29) | (immhi << 5) | reg;
  uint32_t pageoff = (uint32_t)(target_addr & 0xfffu);
  uint32_t patched_add = 0x91000000u | ((pageoff & 0xfffu) << 10) | (reg << 5) | reg;
  z_aarch64_patch_u64(text, patch_offset, ((uint64_t)patched_add << 32) | patched_adrp);
}
