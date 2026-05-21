#ifndef ZERO_C_COMMON_H
#define ZERO_C_COMMON_H

#include "zero.h"

static inline void elf_append_u8(ZBuf *buf, uint8_t value) {
  if (buf == NULL) return;

  zbuf_append_char(buf, (char)(value & 0xff));
}

static inline void elf_append_u16(ZBuf *buf, uint16_t value) {
  if (buf == NULL) return;

  elf_append_u8(buf, value);
  elf_append_u8(buf, value >> 8);
}

static inline void elf_append_u32(ZBuf *buf, uint32_t value) {
  if (buf == NULL) return;
  
  elf_append_u8(buf, value);
  elf_append_u8(buf, value >> 8);
  elf_append_u8(buf, value >> 16);
  elf_append_u8(buf, value >> 24);
}

static inline elf_append_u64(ZBuf *buf, uint64_t value) {
  if (buf == NULL) return;

  elf_append_u32(buf, (uint32_t)value);
  elf_append_u32(buf, (uint32_t)(value >> 32));
}

#define a64_append_u8 elf_append_u8
#define a64_append_u16 elf_append_u16
#define a64_append_u32 elf_append_u32
#define a64_append_u64 elf_append_u64

#endif /// !ifndef ZERO_C_COMMON_H
