#ifndef ZERO_C_COFF_FORMAT_H
#define ZERO_C_COFF_FORMAT_H

#include "zero.h"

#include <stdint.h>

typedef enum {
  Z_COFF_MACHINE_ARM64 = 0xaa64,
  Z_COFF_MACHINE_AMD64 = 0x8664
} ZCoffMachine;

enum {
  Z_COFF_RELOC_AMD64_ADDR64 = 0x0001,
  Z_COFF_RELOC_AMD64_REL32 = 0x0004,
  Z_COFF_RELOC_ARM64_BRANCH26 = 0x0003,
  Z_COFF_RELOC_ARM64_ADDR64 = 0x000e
};

enum {
  Z_COFF_SYMBOL_EXTERNAL = 2,
  Z_COFF_SYMBOL_STATIC = 3
};

enum {
  Z_COFF_IMPORT_EXIT_PROCESS = 0,
  Z_COFF_IMPORT_GET_STD_HANDLE = 1,
  Z_COFF_IMPORT_WRITE_FILE = 2,
  Z_COFF_IMPORT_COUNT = 3
};

typedef struct {
  const char *name;
  uint32_t value;
  uint16_t section_number;
  uint16_t type;
  unsigned char storage_class;
} ZCoffSymbol;

typedef struct {
  ZCoffMachine machine;
  const ZBuf *text;
  const ZBuf *rodata;
  const ZBuf *text_relocs;
  uint16_t text_reloc_count;
  const ZCoffSymbol *symbols;
  size_t symbol_len;
} ZCoffObjectImage;

typedef struct {
  size_t patch_offset;
  unsigned data_offset;
} ZCoffImageDataPatch;

typedef struct {
  size_t patch_offset;
  unsigned import_index;
} ZCoffImportPatch;

typedef struct {
  ZCoffMachine machine;
  uint64_t image_base;
  uint32_t section_alignment;
  uint32_t file_alignment;
  ZBuf *text;
  ZBuf *rdata;
  unsigned rodata_base_offset;
  const ZCoffImageDataPatch *rodata_patches;
  size_t rodata_patch_len;
  const ZCoffImportPatch *import_patches;
  size_t import_patch_len;
} ZCoffExecutableImage;

size_t z_coff_align(size_t value, size_t alignment);
void z_coff_append_u8(ZBuf *buf, unsigned value);
void z_coff_append_u16(ZBuf *buf, uint16_t value);
void z_coff_append_u32(ZBuf *buf, uint32_t value);
void z_coff_append_u64(ZBuf *buf, uint64_t value);
void z_coff_append_bytes(ZBuf *buf, const char *bytes, size_t len);
void z_coff_append_zeros(ZBuf *buf, size_t len);
void z_coff_pad_to(ZBuf *buf, size_t offset);
void z_coff_patch_u32(ZBuf *buf, size_t offset, uint32_t value);
void z_coff_patch_u64(ZBuf *buf, size_t offset, uint64_t value);
void z_coff_append_reloc(ZBuf *buf, uint32_t offset, uint32_t symbol_index, uint16_t type);
void z_coff_append_reloc_amd64(ZBuf *buf, uint32_t offset, uint32_t symbol_index, uint16_t type);
void z_coff_write_object(ZBuf *out, const ZCoffObjectImage *image);
void z_coff_write_pe64_executable(ZBuf *out, const ZCoffExecutableImage *image);

#endif
