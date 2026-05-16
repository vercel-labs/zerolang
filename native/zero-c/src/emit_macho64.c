#include "zero.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void append_u8(ZBuf *buf, unsigned value) {
  zbuf_append_char(buf, (char)(value & 0xffu));
}

static void append_u16le(ZBuf *buf, uint16_t value) {
  append_u8(buf, value);
  append_u8(buf, value >> 8);
}

static void append_u32le(ZBuf *buf, uint32_t value) {
  append_u8(buf, value);
  append_u8(buf, value >> 8);
  append_u8(buf, value >> 16);
  append_u8(buf, value >> 24);
}

static void append_u64le(ZBuf *buf, uint64_t value) {
  append_u32le(buf, (uint32_t)(value & 0xffffffffu));
  append_u32le(buf, (uint32_t)(value >> 32));
}

static void patch_u64le(ZBuf *buf, size_t offset, uint64_t value) {
  for (unsigned i = 0; i < 8; i++) buf->data[offset + i] = (char)((value >> (i * 8)) & 0xffu);
}

static void append_bytes(ZBuf *buf, const char *bytes, size_t len);
static size_t macho_align(size_t value, size_t alignment);

static void append_u8be(ZBuf *buf, unsigned value) {
  append_u8(buf, value);
}

static void append_u32be(ZBuf *buf, uint32_t value) {
  append_u8(buf, value >> 24);
  append_u8(buf, value >> 16);
  append_u8(buf, value >> 8);
  append_u8(buf, value);
}

static void append_u64be(ZBuf *buf, uint64_t value) {
  append_u32be(buf, (uint32_t)(value >> 32));
  append_u32be(buf, (uint32_t)(value & 0xffffffffu));
}

typedef struct {
  uint32_t state[8];
  uint64_t bitlen;
  unsigned char data[64];
  size_t datalen;
} MachOSha256;

static uint32_t macho_sha_rotr(uint32_t value, unsigned bits) {
  return (value >> bits) | (value << (32 - bits));
}

static void macho_sha256_transform(MachOSha256 *ctx, const unsigned char data[64]) {
  static const uint32_t k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
  };
  uint32_t m[64];
  for (unsigned i = 0; i < 16; i++) {
    m[i] = ((uint32_t)data[i * 4] << 24) |
           ((uint32_t)data[i * 4 + 1] << 16) |
           ((uint32_t)data[i * 4 + 2] << 8) |
           ((uint32_t)data[i * 4 + 3]);
  }
  for (unsigned i = 16; i < 64; i++) {
    uint32_t s0 = macho_sha_rotr(m[i - 15], 7) ^ macho_sha_rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
    uint32_t s1 = macho_sha_rotr(m[i - 2], 17) ^ macho_sha_rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
    m[i] = m[i - 16] + s0 + m[i - 7] + s1;
  }

  uint32_t a = ctx->state[0];
  uint32_t b = ctx->state[1];
  uint32_t c = ctx->state[2];
  uint32_t d = ctx->state[3];
  uint32_t e = ctx->state[4];
  uint32_t f = ctx->state[5];
  uint32_t g = ctx->state[6];
  uint32_t h = ctx->state[7];
  for (unsigned i = 0; i < 64; i++) {
    uint32_t s1 = macho_sha_rotr(e, 6) ^ macho_sha_rotr(e, 11) ^ macho_sha_rotr(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t temp1 = h + s1 + ch + k[i] + m[i];
    uint32_t s0 = macho_sha_rotr(a, 2) ^ macho_sha_rotr(a, 13) ^ macho_sha_rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

static void macho_sha256_init(MachOSha256 *ctx) {
  ctx->datalen = 0;
  ctx->bitlen = 0;
  ctx->state[0] = 0x6a09e667u;
  ctx->state[1] = 0xbb67ae85u;
  ctx->state[2] = 0x3c6ef372u;
  ctx->state[3] = 0xa54ff53au;
  ctx->state[4] = 0x510e527fu;
  ctx->state[5] = 0x9b05688cu;
  ctx->state[6] = 0x1f83d9abu;
  ctx->state[7] = 0x5be0cd19u;
}

static void macho_sha256_update(MachOSha256 *ctx, const unsigned char *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    ctx->data[ctx->datalen++] = data[i];
    if (ctx->datalen == 64) {
      macho_sha256_transform(ctx, ctx->data);
      ctx->bitlen += 512;
      ctx->datalen = 0;
    }
  }
}

static void macho_sha256_final(MachOSha256 *ctx, unsigned char hash[32]) {
  size_t i = ctx->datalen;
  ctx->data[i++] = 0x80;
  if (i > 56) {
    while (i < 64) ctx->data[i++] = 0;
    macho_sha256_transform(ctx, ctx->data);
    i = 0;
  }
  while (i < 56) ctx->data[i++] = 0;
  ctx->bitlen += ctx->datalen * 8;
  for (unsigned j = 0; j < 8; j++) ctx->data[63 - j] = (unsigned char)(ctx->bitlen >> (j * 8));
  macho_sha256_transform(ctx, ctx->data);
  for (unsigned j = 0; j < 8; j++) {
    hash[j * 4] = (unsigned char)(ctx->state[j] >> 24);
    hash[j * 4 + 1] = (unsigned char)(ctx->state[j] >> 16);
    hash[j * 4 + 2] = (unsigned char)(ctx->state[j] >> 8);
    hash[j * 4 + 3] = (unsigned char)ctx->state[j];
  }
}

static void macho_sha256_hash(const unsigned char *data, size_t len, unsigned char hash[32]) {
  MachOSha256 ctx;
  macho_sha256_init(&ctx);
  macho_sha256_update(&ctx, data, len);
  macho_sha256_final(&ctx, hash);
}

static void macho_make_uuid(const ZBuf *text, const ZBuf *rodata, unsigned char uuid[16]) {
  MachOSha256 ctx;
  unsigned char hash[32];
  const unsigned char tag[] = "zero-macho64-exe-v1";
  macho_sha256_init(&ctx);
  macho_sha256_update(&ctx, tag, sizeof(tag) - 1);
  if (text && text->data && text->len) macho_sha256_update(&ctx, (const unsigned char *)text->data, text->len);
  if (rodata && rodata->data && rodata->len) macho_sha256_update(&ctx, (const unsigned char *)rodata->data, rodata->len);
  macho_sha256_final(&ctx, hash);
  memcpy(uuid, hash, 16);
  uuid[6] = (unsigned char)((uuid[6] & 0x0fu) | 0x40u);
  uuid[8] = (unsigned char)((uuid[8] & 0x3fu) | 0x80u);
}

static void macho_append_code_signature(ZBuf *sig, const unsigned char *code, size_t code_len, const char *identifier) {
  const uint32_t page_log = 12;
  const size_t page_size = 1u << page_log;
  const uint32_t hash_size = 32;
  const uint32_t nslots = (uint32_t)((code_len + page_size - 1) / page_size);
  const uint32_t cd_header_size = 88;
  const uint32_t ident_offset = cd_header_size;
  const uint32_t ident_len = (uint32_t)strlen(identifier) + 1;
  const uint32_t hash_offset = (uint32_t)macho_align(ident_offset + ident_len, 4);
  const uint32_t cd_length = hash_offset + nslots * hash_size;
  const uint32_t cd_offset = 20;
  const uint32_t super_length = cd_offset + cd_length;

  zbuf_init(sig);
  append_u32be(sig, 0xfade0cc0u);      // CSMAGIC_EMBEDDED_SIGNATURE
  append_u32be(sig, super_length);
  append_u32be(sig, 1);
  append_u32be(sig, 0);                // CSSLOT_CODEDIRECTORY
  append_u32be(sig, cd_offset);

  append_u32be(sig, 0xfade0c02u);      // CSMAGIC_CODEDIRECTORY
  append_u32be(sig, cd_length);
  append_u32be(sig, 0x00020400u);
  append_u32be(sig, 0x00000002u);      // ad-hoc
  append_u32be(sig, hash_offset);
  append_u32be(sig, ident_offset);
  append_u32be(sig, 0);
  append_u32be(sig, nslots);
  append_u32be(sig, (uint32_t)code_len);
  append_u8be(sig, hash_size);
  append_u8be(sig, 2);                 // SHA-256
  append_u8be(sig, 0);
  append_u8be(sig, page_log);
  append_u32be(sig, 0);
  append_u32be(sig, 0);                // scatterOffset
  append_u32be(sig, 0);                // teamOffset
  append_u32be(sig, 0);                // spare3
  append_u64be(sig, code_len);
  append_u64be(sig, 0);                // execSegBase
  append_u64be(sig, code_len);         // execSegLimit
  append_u64be(sig, 0);                // execSegFlags
  append_bytes(sig, identifier, ident_len);
  while (sig->len < cd_offset + hash_offset) append_u8(sig, 0);
  for (uint32_t slot = 0; slot < nslots; slot++) {
    size_t offset = (size_t)slot * page_size;
    size_t len = code_len - offset;
    if (len > page_size) len = page_size;
    unsigned char hash[32];
    macho_sha256_hash(code + offset, len, hash);
    append_bytes(sig, (const char *)hash, sizeof(hash));
  }
}

static void append_bytes(ZBuf *buf, const char *bytes, size_t len) {
  for (size_t i = 0; i < len; i++) append_u8(buf, (unsigned char)bytes[i]);
}

static void append_fixed(ZBuf *buf, const char *text, size_t width) {
  size_t len = text ? strlen(text) : 0;
  if (len > width) len = width;
  for (size_t i = 0; i < len; i++) append_u8(buf, (unsigned char)text[i]);
  for (size_t i = len; i < width; i++) append_u8(buf, 0);
}

static bool macho_diag_at(ZDiag *diag, const char *message, int line, int column, const char *actual) {
  if (diag) {
    diag->code = 4004;
    diag->line = line > 0 ? line : 1;
    diag->column = column > 0 ? column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message);
    snprintf(diag->expected, sizeof(diag->expected), "direct AArch64 Mach-O object MVP subset");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "unsupported construct");
    snprintf(diag->help, sizeof(diag->help), "choose a supported direct target or restrict this program to exported no-parameter functions returning small integer literals");
  }
  return false;
}

static bool macho_diag(ZDiag *diag, const char *message) {
  return macho_diag_at(diag, message, 1, 1, "unsupported feature");
}

static bool macho_return_literal(const IrFunction *fun, uint32_t *out, ZDiag *diag) {
  if (!fun || fun->param_count != 0) {
    return macho_diag_at(diag, "direct AArch64 Mach-O object backend currently supports exported functions without parameters", fun ? fun->line : 1, fun ? fun->column : 1, fun ? fun->name : "missing function");
  }
  if (fun->return_type != IR_TYPE_U8 && fun->return_type != IR_TYPE_I32 && fun->return_type != IR_TYPE_U32 && fun->return_type != IR_TYPE_USIZE) {
    return macho_diag_at(diag, "direct AArch64 Mach-O object backend currently supports primitive 32-bit-or-smaller integer returns", fun->line, fun->column, fun->name);
  }
  for (size_t i = 0; i < fun->instr_len; i++) {
    const IrInstr *instr = &fun->instrs[i];
    if (instr->kind != IR_INSTR_RETURN || !instr->value || instr->value->kind != IR_VALUE_INT || instr->value->int_value > 65535) continue;
    *out = (uint32_t)instr->value->int_value;
    return true;
  }
  return macho_diag_at(diag, "direct AArch64 Mach-O object backend currently requires a small integer literal return", fun->line, fun->column, fun->name);
}

static bool macho_is_literal_return_function(const IrFunction *fun, uint32_t *out, ZDiag *diag) {
  if (!fun || fun->local_len != 0 || fun->instr_len != 1) return false;
  return macho_return_literal(fun, out, diag);
}

static void macho_emit_aarch64_literal_return(ZBuf *text, uint32_t literal) {
  append_u32le(text, 0x52800000u | ((literal & 0xffffu) << 5)); // movz w0, #literal
  append_u32le(text, 0xd65f03c0u); // ret
}

typedef struct {
  size_t patch_offset;
  unsigned callee_index;
  int line;
  int column;
} MachOCallPatch;

typedef struct {
  size_t patch_offset;
  unsigned data_offset;
} MachODataPatch;

typedef struct {
  size_t patch_offset;
} MachOWorldWritePatch;

typedef struct {
  const IrProgram *program;
  size_t *function_offsets;
  size_t function_count;
  MachOCallPatch *call_patches;
  size_t call_patch_len;
  size_t call_patch_cap;
  MachODataPatch *data_patches;
  size_t data_patch_len;
  size_t data_patch_cap;
  MachOWorldWritePatch *world_write_patches;
  size_t world_write_patch_len;
  size_t world_write_patch_cap;
  unsigned rodata_base_offset;
  bool pie_relative_data;
} MachOEmitContext;

static size_t macho_align(size_t value, size_t alignment) {
  size_t remainder = alignment ? value % alignment : 0;
  return remainder == 0 ? value : value + (alignment - remainder);
}

static void macho_pad_to(ZBuf *buf, size_t offset) {
  while (buf->len < offset) append_u8(buf, 0);
}

static void macho_append_uleb128(ZBuf *buf, uint64_t value) {
  do {
    unsigned byte = (unsigned)(value & 0x7fu);
    value >>= 7;
    if (value) byte |= 0x80u;
    append_u8(buf, byte);
  } while (value);
}

static bool macho_type_is_scalar32(IrTypeKind type) {
  return type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_I32 || type == IR_TYPE_U32 || type == IR_TYPE_USIZE;
}

static unsigned macho_slot_offset(unsigned local_index) {
  return local_index * 8;
}

static void macho_emit_add_sp_imm(ZBuf *text, uint32_t base, unsigned imm) {
  append_u32le(text, base | ((imm & 0xfffu) << 10));
}

static void macho_emit_add_x_sp_imm(ZBuf *text, unsigned dst, unsigned imm) {
  append_u32le(text, 0x910003e0u | ((imm & 0xfffu) << 10) | (dst & 31u));
}

static void macho_emit_nop(ZBuf *text) {
  append_u32le(text, 0xd503201fu);
}

static void macho_emit_movz_w(ZBuf *text, unsigned reg, uint32_t literal) {
  append_u32le(text, 0x52800000u | ((literal & 0xffffu) << 5) | (reg & 31u));
  if (literal > 0xffffu) {
    append_u32le(text, 0x72a00000u | (((literal >> 16) & 0xffffu) << 5) | (reg & 31u));
  }
}

static void macho_emit_movz_x(ZBuf *text, unsigned reg, uint32_t literal) {
  append_u32le(text, 0xd2800000u | ((literal & 0xffffu) << 5) | (reg & 31u));
  if (literal > 0xffffu) {
    append_u32le(text, 0xf2a00000u | (((literal >> 16) & 0xffffu) << 5) | (reg & 31u));
  }
}

static void macho_emit_mov_w(ZBuf *text, unsigned dst, unsigned src) {
  append_u32le(text, 0x2a0003e0u | ((src & 31u) << 16) | (dst & 31u));
}

static void macho_emit_add_x_imm(ZBuf *text, unsigned dst, unsigned src, unsigned imm) {
  append_u32le(text, 0x91000000u | ((imm & 0xfffu) << 10) | ((src & 31u) << 5) | (dst & 31u));
}

static void macho_emit_add_w_imm(ZBuf *text, unsigned dst, unsigned src, unsigned imm) {
  append_u32le(text, 0x11000000u | ((imm & 0xfffu) << 10) | ((src & 31u) << 5) | (dst & 31u));
}

static unsigned macho_local_slot_offset(const IrFunction *fun, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  if (fun && local_index < fun->local_len && fun->locals[local_index].frame_offset > 0 && frame_size >= fun->locals[local_index].frame_offset) {
    return frame_size - fun->locals[local_index].frame_offset + slot_offset;
  }
  return macho_slot_offset(local_index) + slot_offset;
}

static void macho_emit_load_local_w(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  unsigned offset = macho_local_slot_offset(fun, local_index, slot_offset, frame_size);
  append_u32le(text, 0xb9400000u | ((offset / 4u) << 10) | (31u << 5) | (reg & 31u));
}

static void macho_emit_load_local_x(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  unsigned offset = macho_local_slot_offset(fun, local_index, slot_offset, frame_size);
  append_u32le(text, 0xf9400000u | ((offset / 8u) << 10) | (31u << 5) | (reg & 31u));
}

static void macho_emit_store_local_w(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  unsigned offset = macho_local_slot_offset(fun, local_index, slot_offset, frame_size);
  append_u32le(text, 0xb9000000u | ((offset / 4u) << 10) | (31u << 5) | (reg & 31u));
}

static void macho_emit_store_local_x(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  unsigned offset = macho_local_slot_offset(fun, local_index, slot_offset, frame_size);
  append_u32le(text, 0xf9000000u | ((offset / 8u) << 10) | (31u << 5) | (reg & 31u));
}

static void macho_emit_load_local_b(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  unsigned offset = macho_local_slot_offset(fun, local_index, slot_offset, frame_size);
  append_u32le(text, 0x39400000u | ((offset & 0xfffu) << 10) | (31u << 5) | (reg & 31u));
}

static void macho_emit_store_local_b(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  unsigned offset = macho_local_slot_offset(fun, local_index, slot_offset, frame_size);
  append_u32le(text, 0x39000000u | ((offset & 0xfffu) << 10) | (31u << 5) | (reg & 31u));
}

static void macho_emit_load_field(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned field_offset, IrTypeKind type, unsigned frame_size) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    macho_emit_load_local_b(text, fun, reg, local_index, field_offset, frame_size);
  } else {
    macho_emit_load_local_w(text, fun, reg, local_index, field_offset, frame_size);
  }
}

static void macho_emit_store_field(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned field_offset, IrTypeKind type, unsigned frame_size) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    macho_emit_store_local_b(text, fun, reg, local_index, field_offset, frame_size);
  } else {
    macho_emit_store_local_w(text, fun, reg, local_index, field_offset, frame_size);
  }
}

static void macho_emit_binary_w(ZBuf *text, IrBinaryOp op, unsigned dst, unsigned lhs, unsigned rhs) {
  if (op == IR_BIN_ADD) {
    append_u32le(text, 0x0b000000u | ((rhs & 31u) << 16) | ((lhs & 31u) << 5) | (dst & 31u));
  } else if (op == IR_BIN_SUB) {
    append_u32le(text, 0x4b000000u | ((rhs & 31u) << 16) | ((lhs & 31u) << 5) | (dst & 31u));
  } else if (op == IR_BIN_MUL) {
    append_u32le(text, 0x1b000000u | ((rhs & 31u) << 16) | (31u << 10) | ((lhs & 31u) << 5) | (dst & 31u));
  }
}

static void macho_emit_cmp_w(ZBuf *text, unsigned lhs, unsigned rhs) {
  append_u32le(text, 0x6b00001fu | ((rhs & 31u) << 16) | ((lhs & 31u) << 5));
}

static void macho_emit_ldrb_w(ZBuf *text, unsigned dst, unsigned base) {
  append_u32le(text, 0x39400000u | ((base & 31u) << 5) | (dst & 31u));
}

static void macho_emit_strb_w(ZBuf *text, unsigned src, unsigned base) {
  append_u32le(text, 0x39000000u | ((base & 31u) << 5) | (src & 31u));
}

static void macho_emit_add_x_reg(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs) {
  append_u32le(text, 0x8b000000u | ((rhs & 31u) << 16) | ((lhs & 31u) << 5) | (dst & 31u));
}

static void macho_emit_add_x_reg_lsl(ZBuf *text, unsigned dst, unsigned lhs, unsigned rhs, unsigned shift) {
  append_u32le(text, 0x8b000000u | ((rhs & 31u) << 16) | ((shift & 0x3fu) << 10) | ((lhs & 31u) << 5) | (dst & 31u));
}

static size_t macho_emit_bl_placeholder(ZBuf *text) {
  size_t patch = text->len;
  append_u32le(text, 0x94000000u);
  return patch;
}

static size_t macho_emit_b_placeholder(ZBuf *text) {
  size_t patch = text->len;
  append_u32le(text, 0x14000000u);
  return patch;
}

static size_t macho_emit_b_cond_placeholder(ZBuf *text, unsigned cond) {
  size_t patch = text->len;
  append_u32le(text, 0x54000000u | (cond & 15u));
  return patch;
}

static size_t macho_emit_cbz_w_placeholder(ZBuf *text, unsigned reg) {
  size_t patch = text->len;
  append_u32le(text, 0x34000000u | (reg & 31u));
  return patch;
}

static void macho_patch_branch26(ZBuf *text, size_t patch_offset, size_t target_offset) {
  uint32_t old_instr = ((unsigned char)text->data[patch_offset]) |
                       ((uint32_t)(unsigned char)text->data[patch_offset + 1] << 8) |
                       ((uint32_t)(unsigned char)text->data[patch_offset + 2] << 16) |
                       ((uint32_t)(unsigned char)text->data[patch_offset + 3] << 24);
  int64_t delta = (int64_t)target_offset - (int64_t)patch_offset;
  int64_t words = delta / 4;
  uint32_t instr = (old_instr & 0xfc000000u) | ((uint32_t)words & 0x03ffffffu);
  text->data[patch_offset + 0] = (char)(instr & 0xff);
  text->data[patch_offset + 1] = (char)((instr >> 8) & 0xff);
  text->data[patch_offset + 2] = (char)((instr >> 16) & 0xff);
  text->data[patch_offset + 3] = (char)((instr >> 24) & 0xff);
}

static void macho_patch_cond19(ZBuf *text, size_t patch_offset, size_t target_offset) {
  uint32_t instr = ((unsigned char)text->data[patch_offset]) |
                   ((uint32_t)(unsigned char)text->data[patch_offset + 1] << 8) |
                   ((uint32_t)(unsigned char)text->data[patch_offset + 2] << 16) |
                   ((uint32_t)(unsigned char)text->data[patch_offset + 3] << 24);
  int64_t delta = (int64_t)target_offset - (int64_t)patch_offset;
  int64_t words = delta / 4;
  instr = (instr & 0xff00001fu) | (((uint32_t)words & 0x7ffffu) << 5);
  text->data[patch_offset + 0] = (char)(instr & 0xff);
  text->data[patch_offset + 1] = (char)((instr >> 8) & 0xff);
  text->data[patch_offset + 2] = (char)((instr >> 16) & 0xff);
  text->data[patch_offset + 3] = (char)((instr >> 24) & 0xff);
}

static void macho_patch_adrp_add(ZBuf *text, size_t patch_offset, uint64_t instr_addr, uint64_t target_addr) {
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
  patch_u64le(text, patch_offset, ((uint64_t)patched_add << 32) | patched_adrp);
}

static bool macho_record_call_patch(MachOEmitContext *ctx, size_t patch_offset, unsigned callee_index, const IrValue *value, ZDiag *diag) {
  if (!ctx || callee_index >= ctx->function_count) {
    return macho_diag_at(diag, "direct AArch64 Mach-O call target is out of range", value ? value->line : 1, value ? value->column : 1, "invalid callee");
  }
  if (ctx->call_patch_len == ctx->call_patch_cap) {
    ctx->call_patch_cap = ctx->call_patch_cap ? ctx->call_patch_cap * 2 : 8;
    ctx->call_patches = realloc(ctx->call_patches, ctx->call_patch_cap * sizeof(MachOCallPatch));
    if (!ctx->call_patches) return macho_diag(diag, "out of memory while recording Mach-O call relocation");
  }
  ctx->call_patches[ctx->call_patch_len++] = (MachOCallPatch){.patch_offset = patch_offset, .callee_index = callee_index, .line = value ? value->line : 1, .column = value ? value->column : 1};
  return true;
}

static bool macho_record_data_patch(MachOEmitContext *ctx, size_t patch_offset, unsigned data_offset, const IrValue *value, ZDiag *diag) {
  if (!ctx) return macho_diag_at(diag, "direct AArch64 Mach-O data relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  if (ctx->data_patch_len == ctx->data_patch_cap) {
    ctx->data_patch_cap = ctx->data_patch_cap ? ctx->data_patch_cap * 2 : 8;
    MachODataPatch *items = realloc(ctx->data_patches, ctx->data_patch_cap * sizeof(MachODataPatch));
    if (!items) return macho_diag(diag, "out of memory while recording Mach-O data relocation");
    ctx->data_patches = items;
  }
  ctx->data_patches[ctx->data_patch_len++] = (MachODataPatch){.patch_offset = patch_offset, .data_offset = data_offset};
  return true;
}

static bool macho_record_world_write_patch(MachOEmitContext *ctx, size_t patch_offset, const IrInstr *instr, ZDiag *diag) {
  if (!ctx) return macho_diag_at(diag, "direct AArch64 Mach-O World write relocation requires an emit context", instr ? instr->line : 1, instr ? instr->column : 1, "missing context");
  if (ctx->world_write_patch_len == ctx->world_write_patch_cap) {
    ctx->world_write_patch_cap = ctx->world_write_patch_cap ? ctx->world_write_patch_cap * 2 : 4;
    MachOWorldWritePatch *items = realloc(ctx->world_write_patches, ctx->world_write_patch_cap * sizeof(MachOWorldWritePatch));
    if (!items) return macho_diag(diag, "out of memory while recording Mach-O World write relocation");
    ctx->world_write_patches = items;
  }
  ctx->world_write_patches[ctx->world_write_patch_len++] = (MachOWorldWritePatch){.patch_offset = patch_offset};
  return true;
}

static void macho_append_call_relocations(ZBuf *relocs, const MachOEmitContext *ctx) {
  for (size_t i = 0; ctx && i < ctx->call_patch_len; i++) {
    const MachOCallPatch *patch = &ctx->call_patches[i];
    uint32_t reloc_info = (patch->callee_index & 0x00ffffffu) |
                          (1u << 24) |  // r_pcrel
                          (2u << 25) |  // r_length: 4 bytes
                          (1u << 27) |  // r_extern: symbol table index
                          (2u << 28);   // ARM64_RELOC_BRANCH26
    append_u32le(relocs, (uint32_t)patch->patch_offset);
    append_u32le(relocs, reloc_info);
  }
}

static void macho_append_world_write_relocations(ZBuf *relocs, const MachOEmitContext *ctx, unsigned symbol_index) {
  for (size_t i = 0; ctx && i < ctx->world_write_patch_len; i++) {
    const MachOWorldWritePatch *patch = &ctx->world_write_patches[i];
    uint32_t reloc_info = (symbol_index & 0x00ffffffu) |
                          (1u << 24) |  // r_pcrel
                          (2u << 25) |  // r_length: 4 bytes
                          (1u << 27) |  // r_extern: symbol table index
                          (2u << 28);   // ARM64_RELOC_BRANCH26
    append_u32le(relocs, (uint32_t)patch->patch_offset);
    append_u32le(relocs, reloc_info);
  }
}

static void macho_append_data_relocations(ZBuf *relocs, const MachOEmitContext *ctx, unsigned data_symbol_index) {
  for (size_t i = 0; ctx && i < ctx->data_patch_len; i++) {
    const MachODataPatch *patch = &ctx->data_patches[i];
    (void)patch;
    uint32_t reloc_info = (data_symbol_index & 0x00ffffffu) |
                          (3u << 25) |  // r_length: 8 bytes
                          (1u << 27) |  // r_extern: symbol table index
                          (0u << 28);   // ARM64_RELOC_UNSIGNED
    append_u32le(relocs, (uint32_t)patch->patch_offset);
    append_u32le(relocs, reloc_info);
  }
}

static bool macho_const_u32_value(const IrValue *value, unsigned *out) {
  if (!value || value->kind != IR_VALUE_INT || value->int_value > UINT32_MAX) return false;
  if (out) *out = (unsigned)value->int_value;
  return true;
}

static unsigned macho_cond_for_compare(IrCompareOp op) {
  switch (op) {
    case IR_CMP_EQ: return 0;
    case IR_CMP_NE: return 1;
    case IR_CMP_LT: return 11;
    case IR_CMP_LE: return 13;
    case IR_CMP_GT: return 12;
    case IR_CMP_GE: return 10;
  }
  return 0;
}

static unsigned macho_invert_cond(unsigned cond) {
  return cond ^ 1u;
}

static bool macho_readonly_data_byte(const IrProgram *program, unsigned offset, unsigned char *out) {
  if (!program) return false;
  for (size_t i = 0; i < program->data_segment_len; i++) {
    const IrDataSegment *segment = &program->data_segments[i];
    if (offset >= segment->offset && offset < segment->offset + segment->len) {
      if (out) *out = segment->bytes[offset - segment->offset];
      return true;
    }
  }
  return false;
}

static bool macho_byte_view_const_len(const IrValue *view, unsigned *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (out) *out = view->data_len;
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned base_len = 0;
    if (!macho_byte_view_const_len(view->left, &base_len)) return false;
    unsigned start = 0;
    unsigned end = base_len;
    if (view->index && !macho_const_u32_value(view->index, &start)) return false;
    if (view->right && !macho_const_u32_value(view->right, &end)) return false;
    if (start > end || end > base_len) return false;
    if (out) *out = end - start;
    return true;
  }
  return false;
}

static bool macho_byte_view_const_byte(const IrProgram *program, const IrValue *view, unsigned index, unsigned char *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL) {
    if (index >= view->data_len) return false;
    return macho_readonly_data_byte(program, view->data_offset + index, out);
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned len = 0;
    unsigned start = 0;
    if (!macho_byte_view_const_len(view, &len) || index >= len) return false;
    if (view->index && !macho_const_u32_value(view->index, &start)) return false;
    return macho_byte_view_const_byte(program, view->left, start + index, out);
  }
  return false;
}

static bool macho_emit_rodata_ptr_literal(ZBuf *text, unsigned reg, unsigned data_offset, MachOEmitContext *ctx, const IrValue *value, ZDiag *diag) {
  if (ctx && ctx->pie_relative_data) {
    size_t patch_offset = text->len;
    append_u32le(text, 0x90000000u | (reg & 31u));                         // adrp xreg, target@page
    append_u32le(text, 0x91000000u | ((reg & 31u) << 5) | (reg & 31u));     // add xreg, xreg, target@pageoff
    return macho_record_data_patch(ctx, patch_offset, data_offset, value, diag);
  }
  while (((text->len + 8) % 8) != 0) macho_emit_nop(text);
  append_u32le(text, 0x58000000u | (2u << 5) | (reg & 31u)); // ldr xreg, .+8
  append_u32le(text, 0x14000003u); // b .+12, over the relocated literal
  size_t patch_offset = text->len;
  append_u64le(text, data_offset - (ctx ? ctx->rodata_base_offset : 0));
  return macho_record_data_patch(ctx, patch_offset, data_offset, value, diag);
}

static bool macho_emit_byte_view_ptr(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag);
static bool macho_emit_byte_view_len(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag);
static bool macho_emit_value_to_reg(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag);

static bool macho_emit_byte_view_len(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (!view) return macho_diag_at(diag, "direct AArch64 Mach-O byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (view->data_len > 65535) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view length is too large for the current MVP", view->line, view->column, "large byte view");
    macho_emit_movz_w(text, reg, view->data_len);
    return true;
  }
  if (view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    macho_emit_load_local_w(text, fun, reg, view->local_index, 8, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    macho_emit_load_local_w(text, fun, reg, view->local_index, 16, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned start = 0;
    unsigned end = 0;
    if (macho_const_u32_value(view->index, &start) && macho_const_u32_value(view->right, &end) && end >= start && end - start <= 65535) {
      macho_emit_movz_w(text, reg, end - start);
      return true;
    }
  }
  (void)ctx;
  return macho_diag_at(diag, "direct AArch64 Mach-O byte-view length currently requires a literal, constant slice, or byte-view local", view->line, view->column, "unsupported byte view length");
}

static bool macho_emit_byte_view_ptr(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (!view) return macho_diag_at(diag, "direct AArch64 Mach-O byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    macho_emit_load_local_x(text, fun, reg, view->local_index, 0, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    macho_emit_load_local_x(text, fun, reg, view->local_index, 8, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW && view->array_index < fun->local_len) {
    const IrLocal *local = &fun->locals[view->array_index];
    if (!local->is_array || local->element_type != IR_TYPE_U8) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view array requires [N]u8", view->line, view->column, "unsupported array view");
    macho_emit_add_x_sp_imm(text, reg, macho_local_slot_offset(fun, view->array_index, 0, frame_size));
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL) {
    return macho_emit_rodata_ptr_literal(text, reg, view->data_offset, ctx, view, diag);
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned start = 0;
    if (!macho_const_u32_value(view->index, &start) || start > 4095) {
      return macho_diag_at(diag, "direct AArch64 Mach-O byte slice currently requires a small constant start", view->line, view->column, "unsupported byte slice");
    }
    if (!macho_emit_byte_view_ptr(text, fun, view->left, reg, frame_size, ctx, diag)) return false;
    if (start > 0) macho_emit_add_x_imm(text, reg, reg, start);
    return true;
  }
  return macho_diag_at(diag, "direct AArch64 Mach-O value is not a supported byte view", view->line, view->column, "unsupported byte view");
}

static bool macho_emit_call_to_reg(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (value->arg_len > 8) return macho_diag_at(diag, "direct AArch64 Mach-O call supports at most eight arguments", value->line, value->column, "too many arguments");
  for (size_t i = 0; i < value->arg_len; i++) {
    if (!macho_emit_value_to_reg(text, fun, value->args[i], (unsigned)i, frame_size, ctx, diag)) return false;
  }
  size_t patch = macho_emit_bl_placeholder(text);
  if (!macho_record_call_patch(ctx, patch, value->callee_index, value, diag)) return false;
  if (reg != 0) macho_emit_mov_w(text, reg, 0);
  return true;
}

static bool macho_emit_value_to_reg(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value) return macho_diag_at(diag, "direct AArch64 Mach-O expression is missing", 1, 1, "missing expression");
  switch (value->kind) {
    case IR_VALUE_BOOL:
    case IR_VALUE_INT:
      macho_emit_movz_w(text, reg, (uint32_t)value->int_value);
      return true;
    case IR_VALUE_LOCAL:
      if (value->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O local index is out of range", value->line, value->column, "invalid local");
      if (fun->locals[value->local_index].type == IR_TYPE_BYTE_VIEW) {
        return macho_diag_at(diag, "direct AArch64 Mach-O byte-view local cannot be used as a scalar", value->line, value->column, "byte-view local");
      }
      macho_emit_load_local_w(text, fun, reg, value->local_index, 0, frame_size);
      return true;
    case IR_VALUE_BINARY:
      if (value->binary_op != IR_BIN_ADD && value->binary_op != IR_BIN_SUB && value->binary_op != IR_BIN_MUL) return macho_diag_at(diag, "direct AArch64 Mach-O binary operator is unsupported", value->line, value->column, "unsupported operator");
      if (!macho_emit_value_to_reg(text, fun, value->left, 8, frame_size, ctx, diag)) return false;
      if (!macho_emit_value_to_reg(text, fun, value->right, 9, frame_size, ctx, diag)) return false;
      macho_emit_binary_w(text, value->binary_op, reg, 8, 9);
      return true;
    case IR_VALUE_COMPARE: {
      if (!value->left || !value->right) {
        return macho_diag_at(diag, "direct AArch64 Mach-O comparison requires two operands", value->line, value->column, "invalid comparison");
      }
      if (!macho_emit_value_to_reg(text, fun, value->left, 8, frame_size, ctx, diag)) return false;
      if (!macho_emit_value_to_reg(text, fun, value->right, 9, frame_size, ctx, diag)) return false;
      macho_emit_cmp_w(text, 8, 9);
      macho_emit_movz_w(text, reg, 0);
      size_t false_patch = macho_emit_b_cond_placeholder(text, macho_invert_cond(macho_cond_for_compare(value->compare_op)));
      macho_emit_movz_w(text, reg, 1);
      macho_patch_cond19(text, false_patch, text->len);
      return true;
    }
    case IR_VALUE_CALL:
      return macho_emit_call_to_reg(text, fun, value, reg, frame_size, ctx, diag);
    case IR_VALUE_VEC_LEN:
    case IR_VALUE_VEC_CAPACITY:
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec helper requires a Vec local", value->line, value->column, "invalid Vec local");
      macho_emit_load_local_w(text, fun, reg, value->local_index, value->kind == IR_VALUE_VEC_LEN ? 8 : 12, frame_size);
      return true;
    case IR_VALUE_VEC_PUSH: {
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec push requires a Vec local", value->line, value->column, "invalid Vec local");
      macho_emit_load_local_w(text, fun, 8, value->local_index, 8, frame_size);
      macho_emit_load_local_w(text, fun, 9, value->local_index, 12, frame_size);
      macho_emit_cmp_w(text, 8, 9);
      size_t ok_patch = macho_emit_b_cond_placeholder(text, 3); // unsigned lower
      macho_emit_movz_w(text, reg, 0);
      size_t end_patch = macho_emit_b_placeholder(text);
      macho_patch_cond19(text, ok_patch, text->len);
      macho_emit_store_local_w(text, fun, 8, value->local_index, 8, frame_size);
      macho_emit_load_local_x(text, fun, 9, value->local_index, 0, frame_size);
      macho_emit_add_x_reg(text, 9, 9, 8);
      if (!macho_emit_value_to_reg(text, fun, value->left, 10, frame_size, ctx, diag)) return false;
      macho_emit_strb_w(text, 10, 9);
      macho_emit_add_w_imm(text, 8, 8, 1);
      macho_emit_store_local_w(text, fun, 8, value->local_index, 8, frame_size);
      macho_emit_movz_w(text, reg, 1);
      macho_patch_branch26(text, end_patch, text->len);
      return true;
    }
    case IR_VALUE_MEMORY_PEEK_U8:
      if (!macho_emit_value_to_reg(text, fun, value->left, 8, frame_size, ctx, diag)) return false;
      macho_emit_ldrb_w(text, reg, 8);
      return true;
    case IR_VALUE_MEMORY_POKE_U8:
      if (!macho_emit_value_to_reg(text, fun, value->left, 8, frame_size, ctx, diag)) return false;
      if (!macho_emit_value_to_reg(text, fun, value->right, 9, frame_size, ctx, diag)) return false;
      macho_emit_strb_w(text, 9, 8);
      macho_emit_movz_w(text, reg, 1);
      return true;
    case IR_VALUE_MAYBE_HAS:
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_MAYBE_BYTE_VIEW) return macho_diag_at(diag, "direct AArch64 Mach-O maybe helper requires a Maybe<MutSpan<u8>> local", value->line, value->column, "invalid maybe local");
      macho_emit_load_local_w(text, fun, reg, value->local_index, 0, frame_size);
      return true;
    case IR_VALUE_BYTE_VIEW_LEN:
      return macho_emit_byte_view_len(text, fun, value->left, reg, frame_size, ctx, diag);
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: {
      unsigned const_index = 0;
      unsigned char byte = 0;
      if (macho_const_u32_value(value->index, &const_index) &&
          macho_byte_view_const_byte(ctx ? ctx->program : NULL, value->left, const_index, &byte)) {
        macho_emit_movz_w(text, reg, byte);
        return true;
      }
      if (!value->index || !macho_emit_value_to_reg(text, fun, value->index, 8, frame_size, ctx, diag)) return false;
      if (!macho_emit_byte_view_len(text, fun, value->left, 9, frame_size, ctx, diag)) return false;
      macho_emit_cmp_w(text, 8, 9);
      size_t ok_patch = macho_emit_b_cond_placeholder(text, 3); // unsigned lower
      append_u32le(text, 0xd4200000u); // brk #0
      macho_patch_cond19(text, ok_patch, text->len);
      if (!macho_emit_byte_view_ptr(text, fun, value->left, 9, frame_size, ctx, diag)) return false;
      macho_emit_add_x_reg(text, 9, 9, 8);
      macho_emit_ldrb_w(text, reg, 9);
      return true;
    }
    case IR_VALUE_INDEX_LOAD: {
      if (value->array_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O indexed load array is out of range", value->line, value->column, "invalid array local");
      const IrLocal *local = &fun->locals[value->array_index];
      unsigned const_index = 0;
      if (local->is_array && local->element_type != IR_TYPE_U8 && macho_const_u32_value(value->index, &const_index) && const_index < local->array_len) {
        macho_emit_load_local_w(text, fun, reg, value->array_index, const_index * 4u, frame_size);
        return true;
      }
      if (local->is_array && (local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 || local->element_type == IR_TYPE_USIZE)) {
        if (!value->index || !macho_emit_value_to_reg(text, fun, value->index, 8, frame_size, ctx, diag)) return false;
        macho_emit_movz_w(text, 9, local->array_len);
        macho_emit_cmp_w(text, 8, 9);
        size_t ok_patch = macho_emit_b_cond_placeholder(text, 3); // unsigned lower
        append_u32le(text, 0xd4200000u); // brk #0
        macho_patch_cond19(text, ok_patch, text->len);
        macho_emit_add_x_sp_imm(text, 9, macho_local_slot_offset(fun, value->array_index, 0, frame_size));
        macho_emit_add_x_reg_lsl(text, 9, 9, 8, 2);
        append_u32le(text, 0xb9400000u | (9u << 5) | (reg & 31u));
        return true;
      }
      if (!local->is_array || local->element_type != IR_TYPE_U8) return macho_diag_at(diag, "direct AArch64 Mach-O indexed load requires [N]u8 or integer arrays", value->line, value->column, "unsupported array local");
      if (!value->index || !macho_emit_value_to_reg(text, fun, value->index, 8, frame_size, ctx, diag)) return false;
      macho_emit_movz_w(text, 9, local->array_len);
      macho_emit_cmp_w(text, 8, 9);
      size_t ok_patch = macho_emit_b_cond_placeholder(text, 3); // unsigned lower
      append_u32le(text, 0xd4200000u); // brk #0
      macho_patch_cond19(text, ok_patch, text->len);
      macho_emit_add_x_sp_imm(text, 9, macho_local_slot_offset(fun, value->array_index, 0, frame_size));
      macho_emit_add_x_reg(text, 9, 9, 8);
      macho_emit_ldrb_w(text, reg, 9);
      return true;
    }
    case IR_VALUE_FIELD_LOAD:
      if (value->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O field load record is out of range", value->line, value->column, "invalid record local");
      if (!fun->locals[value->local_index].is_record) return macho_diag_at(diag, "direct AArch64 Mach-O field load requires record local", value->line, value->column, "non-record local");
      macho_emit_load_field(text, fun, reg, value->local_index, value->field_offset, value->type, frame_size);
      return true;
    default: {
      char actual[64];
      snprintf(actual, sizeof(actual), "unsupported value kind %d", value ? (int)value->kind : -1);
      return macho_diag_at(diag, "direct AArch64 Mach-O value kind is unsupported", value->line, value->column, actual);
    }
  }
}

static unsigned macho_frame_size(const IrFunction *fun) {
  return (unsigned)macho_align(fun ? (fun->frame_bytes ? fun->frame_bytes : fun->local_len * 8) : 0, 16);
}

static void macho_emit_epilogue(ZBuf *text, unsigned frame_size) {
  if (frame_size > 0) macho_emit_add_sp_imm(text, 0x910003ffu, frame_size); // add sp, sp, #frame_size
  append_u32le(text, 0xa8c17bfdu); // ldp x29, x30, [sp], #16
  append_u32le(text, 0xd65f03c0u); // ret
}

static bool macho_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag);

static bool macho_emit_world_write(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (!instr || !instr->value) return macho_diag_at(diag, "direct AArch64 Mach-O World write requires bytes", instr ? instr->line : 1, instr ? instr->column : 1, "missing byte view");
  if (!macho_emit_byte_view_ptr(text, fun, instr->value, 1, frame_size, ctx, diag)) return false;
  if (!macho_emit_byte_view_len(text, fun, instr->value, 2, frame_size, ctx, diag)) return false;
  macho_emit_movz_w(text, 0, instr->field_offset == 2 ? 2u : 1u);
  size_t patch = macho_emit_bl_placeholder(text);
  if (!macho_record_world_write_patch(ctx, patch, instr, diag)) return false;
  size_t ok_patch = macho_emit_cbz_w_placeholder(text, 0);
  append_u32le(text, 0xd4200000u); // brk #0 on runtime write failure
  macho_patch_cond19(text, ok_patch, text->len);
  return true;
}

static bool macho_emit_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (instr->kind == IR_INSTR_WORLD_WRITE) {
    return macho_emit_world_write(text, fun, instr, frame_size, ctx, diag);
  }
  if (instr->kind == IR_INSTR_LOCAL_SET) {
    if (instr->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O local store is out of range", instr->line, instr->column, "invalid local");
    if (fun->locals[instr->local_index].type == IR_TYPE_BYTE_VIEW) {
      if (!macho_emit_byte_view_ptr(text, fun, instr->value, 8, frame_size, ctx, diag)) return false;
      macho_emit_store_local_x(text, fun, 8, instr->local_index, 0, frame_size);
      if (!macho_emit_byte_view_len(text, fun, instr->value, 8, frame_size, ctx, diag)) return false;
      macho_emit_store_local_w(text, fun, 8, instr->local_index, 8, frame_size);
      return true;
    }
    if (fun->locals[instr->local_index].type == IR_TYPE_ALLOC) {
      if (!instr->value || instr->value->kind != IR_VALUE_FIXED_BUF_ALLOC) return macho_diag_at(diag, "direct AArch64 Mach-O FixedBufAlloc local requires std.mem.fixedBufAlloc", instr->line, instr->column, "unsupported allocator initializer");
      if (!macho_emit_byte_view_ptr(text, fun, instr->value->left, 8, frame_size, ctx, diag)) return false;
      macho_emit_store_local_x(text, fun, 8, instr->local_index, 0, frame_size);
      if (!macho_emit_byte_view_len(text, fun, instr->value->left, 8, frame_size, ctx, diag)) return false;
      macho_emit_store_local_w(text, fun, 8, instr->local_index, 8, frame_size);
      macho_emit_movz_w(text, 8, 0);
      macho_emit_store_local_w(text, fun, 8, instr->local_index, 12, frame_size);
      return true;
    }
    if (fun->locals[instr->local_index].type == IR_TYPE_VEC) {
      if (!instr->value || instr->value->kind != IR_VALUE_VEC_INIT) return macho_diag_at(diag, "direct AArch64 Mach-O Vec local requires std.mem.vec", instr->line, instr->column, "unsupported Vec initializer");
      if (!macho_emit_byte_view_ptr(text, fun, instr->value->left, 8, frame_size, ctx, diag)) return false;
      macho_emit_store_local_x(text, fun, 8, instr->local_index, 0, frame_size);
      macho_emit_movz_w(text, 8, 0);
      macho_emit_store_local_w(text, fun, 8, instr->local_index, 8, frame_size);
      if (!macho_emit_byte_view_len(text, fun, instr->value->left, 8, frame_size, ctx, diag)) return false;
      macho_emit_store_local_w(text, fun, 8, instr->local_index, 12, frame_size);
      return true;
    }
    if (fun->locals[instr->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
      if (!instr->value || instr->value->kind != IR_VALUE_ALLOC_BYTES || instr->value->local_index >= fun->local_len || fun->locals[instr->value->local_index].type != IR_TYPE_ALLOC) return macho_diag_at(diag, "direct AArch64 Mach-O allocation source is invalid", instr->line, instr->column, "invalid allocation");
      if (!macho_emit_value_to_reg(text, fun, instr->value->left, 10, frame_size, ctx, diag)) return false;
      macho_emit_load_local_w(text, fun, 8, instr->value->local_index, 12, frame_size);
      macho_emit_load_local_w(text, fun, 9, instr->value->local_index, 8, frame_size);
      macho_emit_add_w_imm(text, 11, 8, 0);
      macho_emit_binary_w(text, IR_BIN_ADD, 11, 11, 10);
      macho_emit_cmp_w(text, 11, 9);
      size_t ok_patch = macho_emit_b_cond_placeholder(text, 9); // unsigned lower or same
      macho_emit_movz_w(text, 8, 0);
      macho_emit_store_local_w(text, fun, 8, instr->local_index, 0, frame_size);
      macho_emit_store_local_x(text, fun, 8, instr->local_index, 8, frame_size);
      macho_emit_store_local_w(text, fun, 8, instr->local_index, 16, frame_size);
      size_t end_patch = macho_emit_b_placeholder(text);
      macho_patch_cond19(text, ok_patch, text->len);
      macho_emit_movz_w(text, 12, 1);
      macho_emit_store_local_w(text, fun, 12, instr->local_index, 0, frame_size);
      macho_emit_load_local_x(text, fun, 12, instr->value->local_index, 0, frame_size);
      macho_emit_add_x_reg(text, 12, 12, 8);
      macho_emit_store_local_x(text, fun, 12, instr->local_index, 8, frame_size);
      macho_emit_store_local_w(text, fun, 10, instr->local_index, 16, frame_size);
      macho_emit_store_local_w(text, fun, 11, instr->value->local_index, 12, frame_size);
      macho_patch_branch26(text, end_patch, text->len);
      return true;
    }
    if (!macho_emit_value_to_reg(text, fun, instr->value, 8, frame_size, ctx, diag)) return false;
    macho_emit_store_local_w(text, fun, 8, instr->local_index, 0, frame_size);
    return true;
  }
  if (instr->kind == IR_INSTR_FIELD_STORE) {
    if (instr->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O field store record is out of range", instr->line, instr->column, "invalid record local");
    if (!fun->locals[instr->local_index].is_record) return macho_diag_at(diag, "direct AArch64 Mach-O field store requires record local", instr->line, instr->column, "non-record local");
    if (!macho_emit_value_to_reg(text, fun, instr->value, 8, frame_size, ctx, diag)) return false;
    macho_emit_store_field(text, fun, 8, instr->local_index, instr->field_offset, instr->value ? instr->value->type : IR_TYPE_I32, frame_size);
    return true;
  }
  if (instr->kind == IR_INSTR_INDEX_STORE) {
    if (instr->array_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O indexed store array is out of range", instr->line, instr->column, "invalid array local");
    const IrLocal *local = &fun->locals[instr->array_index];
    unsigned const_index = 0;
    if (local->is_array && local->element_type != IR_TYPE_U8 && macho_const_u32_value(instr->index, &const_index) && const_index < local->array_len) {
      if (!macho_emit_value_to_reg(text, fun, instr->value, 10, frame_size, ctx, diag)) return false;
      macho_emit_store_local_w(text, fun, 10, instr->array_index, const_index * 4u, frame_size);
      return true;
    }
    if (local->is_array && (local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 || local->element_type == IR_TYPE_USIZE)) {
      if (!macho_emit_value_to_reg(text, fun, instr->value, 10, frame_size, ctx, diag)) return false;
      if (!instr->index || !macho_emit_value_to_reg(text, fun, instr->index, 8, frame_size, ctx, diag)) return false;
      macho_emit_movz_w(text, 9, local->array_len);
      macho_emit_cmp_w(text, 8, 9);
      size_t ok_patch = macho_emit_b_cond_placeholder(text, 3); // unsigned lower
      append_u32le(text, 0xd4200000u); // brk #0
      macho_patch_cond19(text, ok_patch, text->len);
      macho_emit_add_x_sp_imm(text, 9, macho_local_slot_offset(fun, instr->array_index, 0, frame_size));
      macho_emit_add_x_reg_lsl(text, 9, 9, 8, 2);
      append_u32le(text, 0xb9000000u | (9u << 5) | (10u & 31u));
      return true;
    }
    if (!local->is_array || local->element_type != IR_TYPE_U8) return macho_diag_at(diag, "direct AArch64 Mach-O indexed store requires [N]u8 or integer arrays", instr->line, instr->column, "unsupported array local");
    if (!macho_emit_value_to_reg(text, fun, instr->value, 10, frame_size, ctx, diag)) return false;
    if (!instr->index || !macho_emit_value_to_reg(text, fun, instr->index, 8, frame_size, ctx, diag)) return false;
    macho_emit_movz_w(text, 9, local->array_len);
    macho_emit_cmp_w(text, 8, 9);
    size_t ok_patch = macho_emit_b_cond_placeholder(text, 3); // unsigned lower
    append_u32le(text, 0xd4200000u); // brk #0
    macho_patch_cond19(text, ok_patch, text->len);
    macho_emit_add_x_sp_imm(text, 9, macho_local_slot_offset(fun, instr->array_index, 0, frame_size));
    macho_emit_add_x_reg(text, 9, 9, 8);
    macho_emit_strb_w(text, 10, 9);
    return true;
  }
  if (instr->kind == IR_INSTR_EXPR) {
    return !instr->value || macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag);
  }
  if (instr->kind == IR_INSTR_RETURN) {
    if (instr->value && !macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag)) return false;
    macho_emit_epilogue(text, frame_size);
    return true;
  }
  if (instr->kind == IR_INSTR_IF) {
    if (!macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag)) return false;
    size_t false_patch = macho_emit_cbz_w_placeholder(text, 0);
    if (!macho_emit_instrs(text, fun, instr->then_instrs, instr->then_len, frame_size, ctx, diag)) return false;
    if (instr->else_len > 0) {
      size_t end_patch = macho_emit_b_placeholder(text);
      macho_patch_cond19(text, false_patch, text->len);
      if (!macho_emit_instrs(text, fun, instr->else_instrs, instr->else_len, frame_size, ctx, diag)) return false;
      macho_patch_branch26(text, end_patch, text->len);
    } else {
      macho_patch_cond19(text, false_patch, text->len);
    }
    return true;
  }
  if (instr->kind == IR_INSTR_WHILE) {
    size_t loop_start = text->len;
    if (!macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag)) return false;
    size_t false_patch = macho_emit_cbz_w_placeholder(text, 0);
    if (!macho_emit_instrs(text, fun, instr->then_instrs, instr->then_len, frame_size, ctx, diag)) return false;
    size_t loop_patch = macho_emit_b_placeholder(text);
    macho_patch_branch26(text, loop_patch, loop_start);
    macho_patch_cond19(text, false_patch, text->len);
    return true;
  }
  char actual[64];
  snprintf(actual, sizeof(actual), "unsupported instruction kind %d", instr ? (int)instr->kind : -1);
  return macho_diag_at(diag, "direct AArch64 Mach-O instruction kind is unsupported", instr->line, instr->column, actual);
}

static bool macho_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  for (size_t i = 0; i < len; i++) {
    if (!macho_emit_instr(text, fun, &instrs[i], frame_size, ctx, diag)) return false;
  }
  return true;
}

static bool macho_validate_function(const IrFunction *fun, ZDiag *diag) {
  uint32_t ignored = 0;
  if (macho_is_literal_return_function(fun, &ignored, NULL)) return true;
  if (fun->param_count > 8) return macho_diag_at(diag, "direct AArch64 Mach-O object backend supports at most eight parameters", fun->line, fun->column, fun->name);
  if (fun->return_type != IR_TYPE_VOID && !macho_type_is_scalar32(fun->return_type)) {
    return macho_diag_at(diag, "direct AArch64 Mach-O object backend currently supports only Void and 32-bit-or-smaller integer returns", fun->line, fun->column, fun->name);
  }
  for (size_t i = 0; i < fun->local_len; i++) {
    if (fun->locals[i].type == IR_TYPE_BYTE_VIEW) {
      if (fun->locals[i].is_param) {
        return macho_diag_at(diag, "direct AArch64 Mach-O object backend does not yet support byte-view parameters", fun->locals[i].line, fun->locals[i].column, fun->locals[i].name);
      }
      continue;
    }
    if (fun->locals[i].is_array && (fun->locals[i].element_type == IR_TYPE_U8 || fun->locals[i].element_type == IR_TYPE_U32 || fun->locals[i].element_type == IR_TYPE_I32 || fun->locals[i].element_type == IR_TYPE_USIZE)) continue;
    if (fun->locals[i].is_record) continue;
    if (fun->locals[i].type == IR_TYPE_ALLOC || fun->locals[i].type == IR_TYPE_MAYBE_BYTE_VIEW) continue;
    if (fun->locals[i].type == IR_TYPE_VEC) continue;
    if (fun->locals[i].is_array || !macho_type_is_scalar32(fun->locals[i].type)) {
      return macho_diag_at(diag, "direct AArch64 Mach-O object backend currently supports only primitive scalar locals", fun->locals[i].line, fun->locals[i].column, fun->locals[i].name);
    }
  }
  return true;
}

static bool macho_emit_function_text(ZBuf *text, const IrFunction *fun, MachOEmitContext *ctx, ZDiag *diag) {
  uint32_t literal = 0;
  if (macho_is_literal_return_function(fun, &literal, NULL)) {
    macho_emit_aarch64_literal_return(text, literal);
    return true;
  }

  unsigned frame_size = macho_frame_size(fun);
  append_u32le(text, 0xa9bf7bfdu); // stp x29, x30, [sp, #-16]!
  append_u32le(text, 0x910003fdu); // mov x29, sp
  if (frame_size > 0) macho_emit_add_sp_imm(text, 0xd10003ffu, frame_size); // sub sp, sp, #frame_size
  for (size_t i = 0; i < fun->param_count; i++) {
    macho_emit_store_local_w(text, fun, (unsigned)i, (unsigned)i, 0, frame_size);
  }
  if (!macho_emit_instrs(text, fun, fun->instrs, fun->instr_len, frame_size, ctx, diag)) return false;
  if (fun->instr_len == 0 || fun->instrs[fun->instr_len - 1].kind != IR_INSTR_RETURN) macho_emit_epilogue(text, frame_size);
  return true;
}

static unsigned macho_rodata_base_offset(const IrProgram *program) {
  if (!program || program->data_segment_len == 0) return 0;
  unsigned base = program->data_segments[0].offset;
  for (size_t i = 1; i < program->data_segment_len; i++) {
    if (program->data_segments[i].offset < base) base = program->data_segments[i].offset;
  }
  return base;
}

static void macho_append_rodata(ZBuf *rodata, const IrProgram *program, unsigned base_offset) {
  for (size_t i = 0; program && i < program->data_segment_len; i++) {
    const IrDataSegment *segment = &program->data_segments[i];
    macho_pad_to(rodata, segment->offset - base_offset);
    append_bytes(rodata, (const char *)segment->bytes, segment->len);
  }
}

bool z_emit_macho64_object_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {
  if (!program || !out) return macho_diag(diag, "direct Mach-O backend received no program");
  if (!program->mir_valid) return macho_diag_at(diag, program->mir_message[0] ? program->mir_message : "direct backend lowering failed", program->mir_line, program->mir_column, program->mir_actual);
  if (program->function_len == 0) return macho_diag_at(diag, "direct AArch64 Mach-O object backend requires at least one exported function", 1, 1, "empty program");
  bool has_export = false;
  for (size_t i = 0; i < program->function_len; i++) {
    if (program->functions[i].is_exported) has_export = true;
    if (!macho_validate_function(&program->functions[i], diag)) return false;
  }
  if (!has_export) return macho_diag_at(diag, "direct AArch64 Mach-O object backend requires at least one exported function", 1, 1, "no exported function");

  zbuf_init(out);

  ZBuf text;
  ZBuf rodata;
  ZBuf relocs;
  zbuf_init(&text);
  zbuf_init(&rodata);
  zbuf_init(&relocs);
  bool has_rodata = program->readonly_data_bytes > 0 || program->data_segment_len > 0;
  unsigned rodata_base_offset = macho_rodata_base_offset(program);
  if (has_rodata) macho_append_rodata(&rodata, program, rodata_base_offset);
  size_t *offsets = calloc(program->function_len, sizeof(size_t));
  uint32_t *string_offsets = calloc(program->function_len, sizeof(uint32_t));
  if (!offsets) {
    free(string_offsets);
    zbuf_free(&relocs);
    zbuf_free(&rodata);
    zbuf_free(&text);
    return macho_diag(diag, "out of memory while emitting Mach-O object");
  }
  if (!string_offsets) {
    free(offsets);
    zbuf_free(&relocs);
    zbuf_free(&rodata);
    zbuf_free(&text);
    return macho_diag(diag, "out of memory while emitting Mach-O symbols");
  }

  ZBuf strings;
  zbuf_init(&strings);
  append_u8(&strings, 0);
  MachOEmitContext ctx = {
    .program = program,
    .function_offsets = offsets,
    .function_count = program->function_len,
    .rodata_base_offset = rodata_base_offset
  };
  for (size_t i = 0; i < program->function_len; i++) {
    const IrFunction *fun = &program->functions[i];
    macho_pad_to(&text, macho_align(text.len, 4));
    offsets[i] = text.len;
    if (!macho_emit_function_text(&text, fun, &ctx, diag)) {
      zbuf_free(&strings);
      free(string_offsets);
      free(offsets);
      free(ctx.world_write_patches);
      free(ctx.data_patches);
      free(ctx.call_patches);
      zbuf_free(&relocs);
      zbuf_free(&rodata);
      zbuf_free(&text);
      return false;
    }
    string_offsets[i] = (uint32_t)strings.len;
    zbuf_append_char(&strings, '_');
    zbuf_append(&strings, fun->name ? fun->name : "zero_fn");
    append_u8(&strings, 0);
  }
  macho_append_call_relocations(&relocs, &ctx);
  if (has_rodata) {
    macho_append_data_relocations(&relocs, &ctx, (unsigned)program->function_len);
  }
  const bool has_world_write = ctx.world_write_patch_len > 0;
  const uint32_t world_write_symbol_index = (uint32_t)program->function_len + (has_rodata ? 1u : 0u);
  if (has_world_write) {
    macho_append_world_write_relocations(&relocs, &ctx, world_write_symbol_index);
  }

  const uint32_t header_size = 32;
  const uint32_t section_count = has_rodata ? 2u : 1u;
  const uint32_t segment_cmd_size = 72 + section_count * 80;
  const uint32_t symtab_cmd_size = 24;
  const uint32_t sizeofcmds = segment_cmd_size + symtab_cmd_size;
  const uint32_t text_offset = header_size + sizeofcmds;
  const uint32_t const_addr = has_rodata ? (uint32_t)macho_align(text.len, 8) : 0;
  const uint32_t segment_file_size = has_rodata ? const_addr + (uint32_t)rodata.len : (uint32_t)text.len;
  const uint32_t reloff = relocs.len > 0 ? text_offset + segment_file_size : 0;
  const uint32_t symoff = text_offset + segment_file_size + (uint32_t)relocs.len;
  const uint32_t nsyms = (uint32_t)program->function_len + (has_rodata ? 1u : 0u) + (has_world_write ? 1u : 0u);
  const uint32_t stroff = symoff + nsyms * 16;
  uint32_t rodata_string_offset = 0;
  if (has_rodata) {
    rodata_string_offset = (uint32_t)strings.len;
    zbuf_append(&strings, "l_.zero_rodata");
    append_u8(&strings, 0);
  }
  uint32_t world_write_string_offset = 0;
  if (has_world_write) {
    world_write_string_offset = (uint32_t)strings.len;
    zbuf_append(&strings, "_zero_world_write");
    append_u8(&strings, 0);
  }

  append_u32le(out, 0xfeedfacfu);      // MH_MAGIC_64
  append_u32le(out, 0x0100000cu);      // CPU_TYPE_ARM64
  append_u32le(out, 0);                // CPU_SUBTYPE_ARM64_ALL
  append_u32le(out, 1);                // MH_OBJECT
  append_u32le(out, 2);                // ncmds
  append_u32le(out, sizeofcmds);
  append_u32le(out, 0);                // flags
  append_u32le(out, 0);                // reserved

  append_u32le(out, 0x19);             // LC_SEGMENT_64
  append_u32le(out, segment_cmd_size);
  append_fixed(out, "", 16);
  append_u64le(out, 0);
  append_u64le(out, segment_file_size);
  append_u64le(out, text_offset);
  append_u64le(out, segment_file_size);
  append_u32le(out, 7);
  append_u32le(out, 5);
  append_u32le(out, section_count);
  append_u32le(out, 0);

  append_fixed(out, "__text", 16);
  append_fixed(out, "__TEXT", 16);
  append_u64le(out, 0);
  append_u64le(out, text.len);
  append_u32le(out, text_offset);
  append_u32le(out, 2);
  append_u32le(out, reloff);
  append_u32le(out, (uint32_t)(ctx.call_patch_len + ctx.data_patch_len + ctx.world_write_patch_len));
  append_u32le(out, 0x80000400u);
  append_u32le(out, 0);
  append_u32le(out, 0);
  append_u32le(out, 0);

  if (has_rodata) {
    append_fixed(out, "__const", 16);
    append_fixed(out, "__DATA", 16);
    append_u64le(out, const_addr);
    append_u64le(out, rodata.len);
    append_u32le(out, text_offset + const_addr);
    append_u32le(out, 3);
    append_u32le(out, 0);
    append_u32le(out, 0);
    append_u32le(out, 0);
    append_u32le(out, 0);
    append_u32le(out, 0);
    append_u32le(out, 0);
  }

  append_u32le(out, 0x2);              // LC_SYMTAB
  append_u32le(out, symtab_cmd_size);
  append_u32le(out, symoff);
  append_u32le(out, nsyms);
  append_u32le(out, stroff);
  append_u32le(out, (uint32_t)strings.len);

  if (text.data) append_bytes(out, text.data, text.len);
  if (has_rodata) {
    macho_pad_to(out, text_offset + const_addr);
    if (rodata.data) append_bytes(out, rodata.data, rodata.len);
  }
  if (relocs.data) append_bytes(out, relocs.data, relocs.len);
  for (size_t i = 0; i < program->function_len; i++) {
    append_u32le(out, string_offsets[i]);
    append_u8(out, program->functions[i].is_exported ? 0x0f : 0x0e); // N_EXT | N_SECT or local N_SECT
    append_u8(out, 1);
    append_u16le(out, 0);
    append_u64le(out, offsets[i]);
  }
  if (has_rodata) {
    append_u32le(out, rodata_string_offset);
    append_u8(out, 0x0e); // local N_SECT
    append_u8(out, 2);
    append_u16le(out, 0);
    append_u64le(out, const_addr);
  }
  if (has_world_write) {
    append_u32le(out, world_write_string_offset);
    append_u8(out, 0x01); // N_EXT undefined external
    append_u8(out, 0);
    append_u16le(out, 0);
    append_u64le(out, 0);
  }
  if (strings.data) append_bytes(out, strings.data, strings.len);

  free(ctx.world_write_patches);
  free(ctx.data_patches);
  free(ctx.call_patches);
  free(string_offsets);
  free(offsets);
  zbuf_free(&strings);
  zbuf_free(&relocs);
  zbuf_free(&rodata);
  zbuf_free(&text);
  return true;
}

static const IrFunction *macho_find_executable_main(const IrProgram *program, ZDiag *diag, unsigned *out_index) {
  const IrFunction *fun = NULL;
  unsigned index = 0;
  for (size_t i = 0; program && i < program->function_len; i++) {
    if (program->functions[i].is_exported && strcmp(program->functions[i].name, "main") == 0) {
      if (fun) {
        macho_diag_at(diag, "direct AArch64 Mach-O executable backend requires exactly one exported main function", program->functions[i].line, program->functions[i].column, program->functions[i].name);
        return NULL;
      }
      fun = &program->functions[i];
      index = (unsigned)i;
    }
  }
  if (!fun) {
    macho_diag_at(diag, "direct AArch64 Mach-O executable backend requires an exported main function", 1, 1, "missing main");
    return NULL;
  }
  if (fun->param_count != 0) {
    macho_diag_at(diag, "direct AArch64 Mach-O executable main must not take parameters", fun->line, fun->column, fun->name);
    return NULL;
  }
  if (fun->return_type != IR_TYPE_VOID && !macho_type_is_scalar32(fun->return_type)) {
    macho_diag_at(diag, "direct AArch64 Mach-O executable main must return Void, i32, or u32", fun->line, fun->column, fun->name);
    return NULL;
  }
  if (out_index) *out_index = index;
  return fun;
}

static size_t macho_emit_exe_start_stub(ZBuf *text) {
  size_t patch = macho_emit_b_placeholder(text); // tail-call main so it returns to dyld's LC_MAIN trampoline
  return patch;
}

static size_t macho_emit_exe_world_write(ZBuf *text) {
  size_t offset = text->len;
  macho_emit_movz_x(text, 16, 0x02000004u); // Darwin SYS_write(fd=x0, buf=x1, len=x2)
  append_u32le(text, 0xd4001001u); // svc #0x80
  macho_emit_movz_w(text, 0, 0);   // report success to the checked std.io shim
  append_u32le(text, 0xd65f03c0u); // ret
  return offset;
}

bool z_emit_macho64_exe_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {
  if (!program || !out) return macho_diag(diag, "direct Mach-O executable backend received no program");
  if (!program->mir_valid) return macho_diag_at(diag, program->mir_message[0] ? program->mir_message : "direct backend lowering failed", program->mir_line, program->mir_column, program->mir_actual);
  unsigned main_index = 0;
  if (!macho_find_executable_main(program, diag, &main_index)) return false;
  for (size_t i = 0; i < program->function_len; i++) {
    if (!macho_validate_function(&program->functions[i], diag)) return false;
  }

  ZBuf text;
  ZBuf rodata;
  zbuf_init(&text);
  zbuf_init(&rodata);
  bool has_rodata = program->readonly_data_bytes > 0 || program->data_segment_len > 0;
  unsigned rodata_base_offset = macho_rodata_base_offset(program);
  if (has_rodata) macho_append_rodata(&rodata, program, rodata_base_offset);

  size_t *offsets = calloc(program->function_len, sizeof(size_t));
  if (!offsets) {
    zbuf_free(&rodata);
    zbuf_free(&text);
    return macho_diag(diag, "out of memory while emitting Mach-O executable");
  }

  MachOEmitContext ctx = {
    .program = program,
    .function_offsets = offsets,
    .function_count = program->function_len,
    .rodata_base_offset = rodata_base_offset,
    .pie_relative_data = true
  };
  size_t start_call_patch = macho_emit_exe_start_stub(&text);
  macho_pad_to(&text, macho_align(text.len, 16));
  for (size_t i = 0; i < program->function_len; i++) {
    macho_pad_to(&text, macho_align(text.len, 4));
    offsets[i] = text.len;
    if (!macho_emit_function_text(&text, &program->functions[i], &ctx, diag)) {
      free(ctx.world_write_patches);
      free(ctx.data_patches);
      free(ctx.call_patches);
      free(offsets);
      zbuf_free(&rodata);
      zbuf_free(&text);
      return false;
    }
  }

  size_t world_write_offset = 0;
  if (ctx.world_write_patch_len > 0) {
    macho_pad_to(&text, macho_align(text.len, 4));
    world_write_offset = macho_emit_exe_world_write(&text);
  }
  macho_patch_branch26(&text, start_call_patch, offsets[main_index]);
  for (size_t i = 0; i < ctx.call_patch_len; i++) {
    const MachOCallPatch *patch = &ctx.call_patches[i];
    macho_patch_branch26(&text, patch->patch_offset, offsets[patch->callee_index]);
  }
  for (size_t i = 0; i < ctx.world_write_patch_len; i++) {
    macho_patch_branch26(&text, ctx.world_write_patches[i].patch_offset, world_write_offset);
  }

  const uint64_t base_addr = 0x100000000ull;
  const uint32_t page_size = 0x4000;
  const uint32_t header_size = 32;
  const uint32_t pagezero_cmd_size = 72;
  const uint32_t section_count = has_rodata ? 2u : 1u;
  const uint32_t text_segment_cmd_size = 72 + section_count * 80;
  const uint32_t linkedit_cmd_size = 72;
  const uint32_t dyld_info_cmd_size = 48;
  const uint32_t dylinker_cmd_size = 32;
  const uint32_t libsystem_cmd_size = 56;
  const uint32_t main_cmd_size = 24;
  const uint32_t build_version_cmd_size = 24;
  const uint32_t uuid_cmd_size = 24;
  const uint32_t code_signature_cmd_size = 16;
  const uint32_t sizeofcmds = pagezero_cmd_size + text_segment_cmd_size + linkedit_cmd_size + dyld_info_cmd_size + dylinker_cmd_size + libsystem_cmd_size + main_cmd_size + build_version_cmd_size + uuid_cmd_size + code_signature_cmd_size;
  const uint32_t text_offset = (uint32_t)macho_align(header_size + sizeofcmds, 16);
  const uint32_t rodata_offset = has_rodata ? (uint32_t)macho_align(text_offset + text.len, 8) : 0;
  for (size_t i = 0; i < ctx.data_patch_len; i++) {
    const MachODataPatch *patch = &ctx.data_patches[i];
    uint64_t addr = base_addr + rodata_offset + (patch->data_offset - rodata_base_offset);
    if (ctx.pie_relative_data) macho_patch_adrp_add(&text, patch->patch_offset, base_addr + text_offset + patch->patch_offset, addr);
    else patch_u64le(&text, patch->patch_offset, addr);
  }
  const uint64_t segment_content_size = has_rodata ? rodata_offset + rodata.len : text_offset + text.len;
  const uint64_t segment_file_size = macho_align((size_t)segment_content_size, page_size);
  const uint64_t segment_vm_size = segment_file_size;
  ZBuf rebase;
  zbuf_init(&rebase);
  if (ctx.data_patch_len > 0 && !ctx.pie_relative_data) {
    append_u8(&rebase, 0x11); // REBASE_OPCODE_SET_TYPE_IMM | REBASE_TYPE_POINTER
    for (size_t i = 0; i < ctx.data_patch_len; i++) {
      append_u8(&rebase, 0x21); // REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB, __TEXT segment
      macho_append_uleb128(&rebase, text_offset + ctx.data_patches[i].patch_offset);
      append_u8(&rebase, 0x51); // REBASE_OPCODE_DO_REBASE_IMM_TIMES, once
    }
    append_u8(&rebase, 0x00);
  }
  const uint32_t rebase_offset = rebase.len > 0 ? (uint32_t)segment_file_size : 0;
  const uint32_t rebase_size = (uint32_t)rebase.len;
  const uint32_t code_signature_offset = (uint32_t)macho_align((size_t)segment_file_size + rebase.len, 16);
  const char *code_signature_id = "zero-direct";
  const uint32_t code_signature_hash_offset = (uint32_t)macho_align(88 + strlen(code_signature_id) + 1, 4);
  const uint32_t code_signature_slots = (code_signature_offset + 4095u) / 4096u;
  const uint32_t code_signature_size = 20 + code_signature_hash_offset + code_signature_slots * 32;
  const uint64_t linkedit_vmaddr = base_addr + segment_file_size;
  const uint64_t linkedit_vmsize = macho_align(code_signature_size, page_size);
  unsigned char uuid[16];
  macho_make_uuid(&text, has_rodata ? &rodata : NULL, uuid);

  zbuf_init(out);
  append_u32le(out, 0xfeedfacfu);      // MH_MAGIC_64
  append_u32le(out, 0x0100000cu);      // CPU_TYPE_ARM64
  append_u32le(out, 0);                // CPU_SUBTYPE_ARM64_ALL
  append_u32le(out, 2);                // MH_EXECUTE
  append_u32le(out, 10);               // ncmds
  append_u32le(out, sizeofcmds);
  append_u32le(out, 0x200085);         // MH_NOUNDEFS | MH_DYLDLINK | MH_TWOLEVEL | MH_PIE
  append_u32le(out, 0);

  append_u32le(out, 0x19);             // LC_SEGMENT_64
  append_u32le(out, pagezero_cmd_size);
  append_fixed(out, "__PAGEZERO", 16);
  append_u64le(out, 0);
  append_u64le(out, base_addr);
  append_u64le(out, 0);
  append_u64le(out, 0);
  append_u32le(out, 0);
  append_u32le(out, 0);
  append_u32le(out, 0);
  append_u32le(out, 0);

  append_u32le(out, 0x19);             // LC_SEGMENT_64
  append_u32le(out, text_segment_cmd_size);
  append_fixed(out, "__TEXT", 16);
  append_u64le(out, base_addr);
  append_u64le(out, segment_vm_size);
  append_u64le(out, 0);
  append_u64le(out, segment_file_size);
  append_u32le(out, 5);                // r-x
  append_u32le(out, 5);
  append_u32le(out, section_count);
  append_u32le(out, 0);

  append_fixed(out, "__text", 16);
  append_fixed(out, "__TEXT", 16);
  append_u64le(out, base_addr + text_offset);
  append_u64le(out, text.len);
  append_u32le(out, text_offset);
  append_u32le(out, 2);
  append_u32le(out, 0);
  append_u32le(out, 0);
  append_u32le(out, 0x80000400u);
  append_u32le(out, 0);
  append_u32le(out, 0);
  append_u32le(out, 0);

  if (has_rodata) {
    append_fixed(out, "__const", 16);
    append_fixed(out, "__TEXT", 16);
    append_u64le(out, base_addr + rodata_offset);
    append_u64le(out, rodata.len);
    append_u32le(out, rodata_offset);
    append_u32le(out, 3);
    append_u32le(out, 0);
    append_u32le(out, 0);
    append_u32le(out, 0);
    append_u32le(out, 0);
    append_u32le(out, 0);
    append_u32le(out, 0);
  }

  append_u32le(out, 0x19);             // LC_SEGMENT_64
  append_u32le(out, linkedit_cmd_size);
  append_fixed(out, "__LINKEDIT", 16);
  append_u64le(out, linkedit_vmaddr);
  append_u64le(out, linkedit_vmsize);
  append_u64le(out, code_signature_offset);
  append_u64le(out, code_signature_size);
  append_u32le(out, 1);                // r--
  append_u32le(out, 1);
  append_u32le(out, 0);
  append_u32le(out, 0);

  append_u32le(out, 0x80000022u);      // LC_DYLD_INFO_ONLY
  append_u32le(out, dyld_info_cmd_size);
  append_u32le(out, rebase_offset);
  append_u32le(out, rebase_size);
  for (unsigned i = 0; i < 8; i++) append_u32le(out, 0);

  append_u32le(out, 0xe);              // LC_LOAD_DYLINKER
  append_u32le(out, dylinker_cmd_size);
  append_u32le(out, 12);
  append_bytes(out, "/usr/lib/dyld", strlen("/usr/lib/dyld") + 1);
  macho_pad_to(out, header_size + pagezero_cmd_size + text_segment_cmd_size + linkedit_cmd_size + dyld_info_cmd_size + dylinker_cmd_size);

  append_u32le(out, 0xc);              // LC_LOAD_DYLIB
  append_u32le(out, libsystem_cmd_size);
  append_u32le(out, 24);
  append_u32le(out, 2);
  append_u32le(out, 0x054c0000);
  append_u32le(out, 0x00010000);
  append_bytes(out, "/usr/lib/libSystem.B.dylib", strlen("/usr/lib/libSystem.B.dylib") + 1);
  macho_pad_to(out, header_size + pagezero_cmd_size + text_segment_cmd_size + linkedit_cmd_size + dyld_info_cmd_size + dylinker_cmd_size + libsystem_cmd_size);

  append_u32le(out, 0x80000028u);      // LC_MAIN
  append_u32le(out, main_cmd_size);
  append_u64le(out, text_offset);
  append_u64le(out, 0);

  append_u32le(out, 0x32);             // LC_BUILD_VERSION
  append_u32le(out, build_version_cmd_size);
  append_u32le(out, 1);                // PLATFORM_MACOS
  append_u32le(out, 0x000b0000);       // macOS 11.0.0
  append_u32le(out, 0);
  append_u32le(out, 0);

  append_u32le(out, 0x1b);             // LC_UUID
  append_u32le(out, uuid_cmd_size);
  append_bytes(out, (const char *)uuid, sizeof(uuid));

  append_u32le(out, 0x1d);             // LC_CODE_SIGNATURE
  append_u32le(out, code_signature_cmd_size);
  append_u32le(out, code_signature_offset);
  append_u32le(out, code_signature_size);

  macho_pad_to(out, text_offset);
  if (text.data) append_bytes(out, text.data, text.len);
  if (has_rodata) {
    macho_pad_to(out, rodata_offset);
    if (rodata.data) append_bytes(out, rodata.data, rodata.len);
  }
  macho_pad_to(out, (size_t)segment_file_size);
  if (rebase.data) append_bytes(out, rebase.data, rebase.len);
  macho_pad_to(out, code_signature_offset);
  ZBuf signature;
  macho_append_code_signature(&signature, (const unsigned char *)out->data, out->len, code_signature_id);
  if (signature.data) append_bytes(out, signature.data, signature.len);
  zbuf_free(&signature);

  free(ctx.world_write_patches);
  free(ctx.data_patches);
  free(ctx.call_patches);
  free(offsets);
  zbuf_free(&rebase);
  zbuf_free(&rodata);
  zbuf_free(&text);
  return true;
}
