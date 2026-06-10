#include "zero.h"

#include <stdlib.h>

bool z_direct_loop_frame_add_break(ZDirectLoopFrame *frame, size_t patch_offset) {
  if (!frame) return false;
  if (frame->break_len == frame->break_cap) {
    size_t cap = frame->break_cap ? frame->break_cap * 2 : 4;
    size_t *items = realloc(frame->break_patches, cap * sizeof(size_t));
    if (!items) return false;
    frame->break_patches = items;
    frame->break_cap = cap;
  }
  frame->break_patches[frame->break_len++] = patch_offset;
  return true;
}

bool z_emit_direct_object_from_ir(ZDirectBackend backend, const IrProgram *program, ZBuf *out, ZDiag *diag) {
  switch (backend) {
    case Z_DIRECT_BACKEND_ELF64: return z_emit_elf64_object_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_ELF_AARCH64: return z_emit_elf_aarch64_object_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_MACHO64: return z_emit_macho64_object_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_MACHO_X64: return z_emit_macho_x64_object_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_COFF_X64: return z_emit_coff_x64_object_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_COFF_AARCH64: return z_emit_coff_aarch64_object_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_NONE: return false;
  }
  return false;
}

bool z_emit_direct_executable_from_ir(ZDirectBackend backend, const IrProgram *program, ZBuf *out, ZDiag *diag) {
  switch (backend) {
    case Z_DIRECT_BACKEND_ELF64: return z_emit_elf64_exe_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_ELF_AARCH64: return z_emit_elf_aarch64_exe_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_MACHO64: return z_emit_macho64_exe_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_MACHO_X64: return z_emit_macho_x64_exe_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_COFF_X64: return z_emit_coff_x64_exe_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_COFF_AARCH64: return z_emit_coff_aarch64_exe_from_ir(program, out, diag);
    case Z_DIRECT_BACKEND_NONE: return false;
  }
  return false;
}
