#ifndef ZERO_C_AARCH64_EMIT_H
#define ZERO_C_AARCH64_EMIT_H

#include "zero.h"

#include <stdint.h>

size_t z_aarch64_align(size_t value, size_t alignment);
void z_aarch64_append_u8(ZBuf *buf, unsigned value);
void z_aarch64_append_u32(ZBuf *buf, uint32_t value);
void z_aarch64_append_u64(ZBuf *buf, uint64_t value);
void z_aarch64_append_zeros(ZBuf *buf, size_t len);
void z_aarch64_pad_to(ZBuf *buf, size_t offset);
void z_aarch64_patch_u32(ZBuf *buf, size_t offset, uint32_t value);
void z_aarch64_patch_u64(ZBuf *buf, size_t offset, uint64_t value);

void z_aarch64_emit_ret(ZBuf *text);
void z_aarch64_emit_blr_x(ZBuf *text, unsigned reg);
void z_aarch64_emit_nop(ZBuf *text);
void z_aarch64_emit_brk(ZBuf *text);
void z_aarch64_emit_svc(ZBuf *text, unsigned imm16);
void z_aarch64_emit_literal_return(ZBuf *text, uint32_t literal);
void z_aarch64_emit_movz_w(ZBuf *text, unsigned reg, uint32_t literal);
void z_aarch64_emit_movz_x(ZBuf *text, unsigned reg, uint64_t literal);
void z_aarch64_emit_mov_w(ZBuf *text, unsigned dst, unsigned src);
void z_aarch64_emit_mov_x(ZBuf *text, unsigned dst, unsigned src);
void z_aarch64_emit_uxtb_w(ZBuf *text, unsigned dst, unsigned src);
void z_aarch64_emit_uxth_w(ZBuf *text, unsigned dst, unsigned src);
void z_aarch64_emit_sxtw_x(ZBuf *text, unsigned dst, unsigned src);
void z_aarch64_emit_mov_x29_sp(ZBuf *text);
void z_aarch64_emit_stp_x20_x21_sp_pre16(ZBuf *text);
void z_aarch64_emit_stp_x29_x30_sp_pre16(ZBuf *text);
void z_aarch64_emit_ldp_x20_x21_sp_post16(ZBuf *text);
void z_aarch64_emit_ldp_x29_x30_sp_post16(ZBuf *text);
void z_aarch64_emit_add_sp_imm(ZBuf *text, unsigned imm);
void z_aarch64_emit_sub_sp_imm(ZBuf *text, unsigned imm);
void z_aarch64_emit_add_x_sp_imm(ZBuf *text, unsigned dst, unsigned imm);
void z_aarch64_emit_add_x_imm(ZBuf *text, unsigned dst, unsigned src, unsigned imm);
void z_aarch64_emit_add_w_imm(ZBuf *text, unsigned dst, unsigned src, unsigned imm);
void z_aarch64_emit_sub_w_imm(ZBuf *text, unsigned dst, unsigned src, unsigned imm);
void z_aarch64_emit_load_w_sp(ZBuf *text, unsigned reg, unsigned offset);
void z_aarch64_emit_load_x_sp(ZBuf *text, unsigned reg, unsigned offset);
void z_aarch64_emit_load_b_sp(ZBuf *text, unsigned reg, unsigned offset);
void z_aarch64_emit_store_w_sp(ZBuf *text, unsigned reg, unsigned offset);
void z_aarch64_emit_store_x_sp(ZBuf *text, unsigned reg, unsigned offset);
void z_aarch64_emit_store_b_sp(ZBuf *text, unsigned reg, unsigned offset);
void z_aarch64_emit_load_w_imm(ZBuf *text, unsigned dst, unsigned base, unsigned byte_offset);
void z_aarch64_emit_load_x_imm(ZBuf *text, unsigned dst, unsigned base, unsigned byte_offset);
void z_aarch64_emit_load_b_imm(ZBuf *text, unsigned dst, unsigned base, unsigned byte_offset);
void z_aarch64_emit_load_h_imm(ZBuf *text, unsigned dst, unsigned base, unsigned byte_offset);
void z_aarch64_emit_store_w_imm(ZBuf *text, unsigned src, unsigned base, unsigned byte_offset);
void z_aarch64_emit_store_x_imm(ZBuf *text, unsigned src, unsigned base, unsigned byte_offset);
void z_aarch64_emit_store_b_imm(ZBuf *text, unsigned src, unsigned base, unsigned byte_offset);
void z_aarch64_emit_store_h_imm(ZBuf *text, unsigned src, unsigned base, unsigned byte_offset);
void z_aarch64_emit_add_w_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs);
void z_aarch64_emit_add_x_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs);
void z_aarch64_emit_add_x_reg_lsl(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs, unsigned shift);
void z_aarch64_emit_lsr_x_imm(ZBuf *text, unsigned dst, unsigned src, unsigned shift);
void z_aarch64_emit_lsr_w_imm(ZBuf *text, unsigned dst, unsigned src, unsigned shift);
void z_aarch64_emit_eor_w_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs);
void z_aarch64_emit_and_w_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs);
void z_aarch64_emit_mvn_w(ZBuf *text, unsigned dst, unsigned src);
void z_aarch64_emit_movk_w(ZBuf *text, unsigned dst, uint32_t imm16, unsigned shift_halfwords);
void z_aarch64_emit_sub_w_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs);
void z_aarch64_emit_sub_x_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs);
void z_aarch64_emit_mul_w_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs);
void z_aarch64_emit_mul_x_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs);
void z_aarch64_emit_div_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs, bool is_unsigned, bool wide);
void z_aarch64_emit_msub_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs, unsigned acc, bool wide);
void z_aarch64_emit_cmp_w(ZBuf *text, unsigned lhs, unsigned rhs);
void z_aarch64_emit_cmp_x(ZBuf *text, unsigned lhs, unsigned rhs);
void z_aarch64_emit_adrp_add_placeholder(ZBuf *text, unsigned reg);
void z_aarch64_emit_ldr_x_literal8(ZBuf *text, unsigned reg);
void z_aarch64_emit_b_offset_words(ZBuf *text, int32_t words);
void z_aarch64_emit_byte_copy_min_loop(ZBuf *text, unsigned result_reg);
void z_aarch64_emit_byte_fill_loop(ZBuf *text, unsigned result_reg);
void z_aarch64_emit_byte_eq_loop(ZBuf *text, unsigned result_reg);
void z_aarch64_emit_crc32_bytes_loop(ZBuf *text, unsigned result_reg);

size_t z_aarch64_emit_bl_placeholder(ZBuf *text);
size_t z_aarch64_emit_b_placeholder(ZBuf *text);
size_t z_aarch64_emit_b_cond_placeholder(ZBuf *text, unsigned cond);
size_t z_aarch64_emit_cbz_w_placeholder(ZBuf *text, unsigned reg);
void z_aarch64_patch_branch26(ZBuf *text, size_t patch_offset, size_t target_offset);
void z_aarch64_patch_cond19(ZBuf *text, size_t patch_offset, size_t target_offset);
void z_aarch64_patch_adrp_add(ZBuf *text, size_t patch_offset, uint64_t instr_addr, uint64_t target_addr);

#endif
