#include "crypto.h"

#include <stdint.h>
#include <stddef.h>

typedef struct {
  uint32_t state[8];
  uint64_t bitlen;
  unsigned char data[64];
  size_t data_len;
} Sha256Ctx;

static uint32_t sha256_rotr(uint32_t value, unsigned bits) {
  return (value >> bits) | (value << (32 - bits));
}

static void sha256_transform(Sha256Ctx* ctx, const unsigned char data[64]) {
  static const uint32_t k[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
      0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
      0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
      0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
      0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
  uint32_t m[64];
  for (unsigned i = 0; i < 16; i++) {
    m[i] = ((uint32_t)data[i * 4] << 24) | ((uint32_t)data[i * 4 + 1] << 16) | ((uint32_t)data[i * 4 + 2] << 8) |
           ((uint32_t)data[i * 4 + 3]);
  }
  for (unsigned i = 16; i < 64; i++) {
    uint32_t s0 = sha256_rotr(m[i - 15], 7) ^ sha256_rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
    uint32_t s1 = sha256_rotr(m[i - 2], 17) ^ sha256_rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
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
    uint32_t s1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t temp1 = h + s1 + ch + k[i] + m[i];
    uint32_t s0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
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

static void sha256_init(Sha256Ctx* ctx) {
  ctx->data_len = 0;
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

static void sha256_update(Sha256Ctx* ctx, const unsigned char* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    ctx->data[ctx->data_len++] = data[i];
    if (ctx->data_len == 64) {
      sha256_transform(ctx, ctx->data);
      ctx->bitlen += 512;
      ctx->data_len = 0;
    }
  }
}

static void sha256_final(Sha256Ctx* ctx, unsigned char hash[32]) {
  size_t i = ctx->data_len;
  ctx->data[i++] = 0x80;
  if (i > 56) {
    while (i < 64) ctx->data[i++] = 0;
    sha256_transform(ctx, ctx->data);
    i = 0;
  }
  while (i < 56) ctx->data[i++] = 0;
  ctx->bitlen += ctx->data_len * 8;
  for (unsigned j = 0; j < 8; j++) ctx->data[63 - j] = (unsigned char)(ctx->bitlen >> (j * 8));
  sha256_transform(ctx, ctx->data);
  for (unsigned j = 0; j < 8; j++) {
    hash[j * 4] = (unsigned char)(ctx->state[j] >> 24);
    hash[j * 4 + 1] = (unsigned char)(ctx->state[j] >> 16);
    hash[j * 4 + 2] = (unsigned char)(ctx->state[j] >> 8);
    hash[j * 4 + 3] = (unsigned char)ctx->state[j];
  }
}

void sha256_hash(const unsigned char* data, size_t len, unsigned char hash[32]) {
  Sha256Ctx ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, data, len);
  sha256_final(&ctx, hash);
}
