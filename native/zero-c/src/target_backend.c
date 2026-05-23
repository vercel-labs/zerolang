#include "zero.h"

#include <string.h>

typedef struct {
  const char *object_format;
  const char *os;
  const char *arch;
  const char *abi;
  ZDirectBackend backend;
  bool object_supported;
  bool exe_supported;
} ZDirectBackendRule;

static const ZDirectBackendRule direct_backend_rules[] = {
  {"elf", "linux", "x86_64", "gnu", Z_DIRECT_BACKEND_ELF64, true, true},
  {"elf", "linux", "x86_64", "musl", Z_DIRECT_BACKEND_ELF64, true, true},
  {"elf", "linux", "aarch64", "gnu", Z_DIRECT_BACKEND_ELF_AARCH64, true, true},
  {"elf", "linux", "aarch64", "musl", Z_DIRECT_BACKEND_ELF_AARCH64, true, true},
  {"macho", "macos", "aarch64", "darwin", Z_DIRECT_BACKEND_MACHO64, true, true},
  {"coff", "windows", "x86_64", "msvc", Z_DIRECT_BACKEND_COFF_X64, true, true},
};

static bool target_field_matches(const char *actual, const char *expected) {
  return !expected || (actual && strcmp(actual, expected) == 0);
}

static ZDirectBackend direct_backend_for_target(const ZTargetInfo *target, bool executable) {
  if (!target) return Z_DIRECT_BACKEND_NONE;
  for (size_t i = 0; i < sizeof(direct_backend_rules) / sizeof(direct_backend_rules[0]); i++) {
    const ZDirectBackendRule *rule = &direct_backend_rules[i];
    if (executable ? !rule->exe_supported : !rule->object_supported) continue;
    if (!target_field_matches(target->object_format, rule->object_format)) continue;
    if (!target_field_matches(target->os, rule->os)) continue;
    if (!target_field_matches(target->arch, rule->arch)) continue;
    if (!target_field_matches(target->abi, rule->abi)) continue;
    return rule->backend;
  }
  return Z_DIRECT_BACKEND_NONE;
}

ZDirectBackend z_direct_object_backend(const ZTargetInfo *target) {
  return direct_backend_for_target(target, false);
}

ZDirectBackend z_direct_exe_backend(const ZTargetInfo *target) {
  return direct_backend_for_target(target, true);
}

const char *z_direct_backend_object_emitter(ZDirectBackend backend) {
  switch (backend) {
    case Z_DIRECT_BACKEND_ELF64: return "zero-elf64";
    case Z_DIRECT_BACKEND_ELF_AARCH64: return "zero-elf-aarch64";
    case Z_DIRECT_BACKEND_MACHO64: return "zero-macho64";
    case Z_DIRECT_BACKEND_COFF_X64: return "zero-coff-x64";
    case Z_DIRECT_BACKEND_NONE: return "none";
  }
  return "none";
}

const char *z_direct_backend_exe_emitter(ZDirectBackend backend) {
  switch (backend) {
    case Z_DIRECT_BACKEND_ELF64: return "zero-elf64-exe";
    case Z_DIRECT_BACKEND_ELF_AARCH64: return "zero-elf-aarch64-exe";
    case Z_DIRECT_BACKEND_MACHO64: return "zero-macho64-exe";
    case Z_DIRECT_BACKEND_COFF_X64: return "zero-coff-x64-exe";
    case Z_DIRECT_BACKEND_NONE: return "none";
  }
  return "none";
}

ZDirectBackend z_direct_backend_from_emitter(const char *emitter) {
  if (!emitter) return Z_DIRECT_BACKEND_NONE;
  for (size_t i = 0; i < sizeof(direct_backend_rules) / sizeof(direct_backend_rules[0]); i++) {
    ZDirectBackend backend = direct_backend_rules[i].backend;
    if (strcmp(emitter, z_direct_backend_object_emitter(backend)) == 0) return backend;
    if (strcmp(emitter, z_direct_backend_exe_emitter(backend)) == 0) return backend;
  }
  return Z_DIRECT_BACKEND_NONE;
}

const char *z_direct_backend_linker_flavor(ZDirectBackend backend) {
  switch (backend) {
    case Z_DIRECT_BACKEND_ELF64: return "elf64";
    case Z_DIRECT_BACKEND_ELF_AARCH64: return "elf64";
    case Z_DIRECT_BACKEND_MACHO64: return "macho64";
    case Z_DIRECT_BACKEND_COFF_X64: return "coff";
    case Z_DIRECT_BACKEND_NONE: return "none";
  }
  return "none";
}

const char *z_direct_backend_artifact_path(ZDirectBackend backend, bool executable) {
  switch (backend) {
    case Z_DIRECT_BACKEND_ELF64: return executable ? "direct-elf64-exe" : "direct-elf64-object";
    case Z_DIRECT_BACKEND_ELF_AARCH64: return executable ? "direct-elf-aarch64-exe" : "direct-elf-aarch64-object";
    case Z_DIRECT_BACKEND_MACHO64: return executable ? "direct-macho64-exe" : "direct-macho64-object";
    case Z_DIRECT_BACKEND_COFF_X64: return executable ? "direct-coff-x64-exe" : "direct-coff-x64-object";
    case Z_DIRECT_BACKEND_NONE: return "unsupported";
  }
  return "direct-backend";
}

bool z_direct_backend_supports_runtime_object(ZDirectBackend backend) {
  return backend == Z_DIRECT_BACKEND_ELF64 || backend == Z_DIRECT_BACKEND_MACHO64;
}

bool z_direct_backend_emitter_is_executable(const char *emitter) {
  ZDirectBackend backend = z_direct_backend_from_emitter(emitter);
  return backend != Z_DIRECT_BACKEND_NONE && strcmp(emitter, z_direct_backend_exe_emitter(backend)) == 0;
}

bool z_direct_backend_is_request_name(const char *requested_backend) {
  if (!requested_backend || !requested_backend[0]) return false;
  ZDirectBackend backend = z_direct_backend_from_emitter(requested_backend);
  return backend != Z_DIRECT_BACKEND_NONE && strcmp(requested_backend, z_direct_backend_object_emitter(backend)) == 0;
}

bool z_direct_requested_backend_matches(const char *requested_backend, ZDirectBackend backend) {
  if (!requested_backend || !requested_backend[0]) return true;
  if (backend == Z_DIRECT_BACKEND_NONE) return false;
  return strcmp(requested_backend, z_direct_backend_object_emitter(backend)) == 0;
}

const char *z_direct_object_emitter(const ZTargetInfo *target) {
  return z_direct_backend_object_emitter(z_direct_object_backend(target));
}

const char *z_direct_exe_emitter(const ZTargetInfo *target) {
  return z_direct_backend_exe_emitter(z_direct_exe_backend(target));
}

const char *z_direct_backend_status(const ZTargetInfo *target) {
  if (!target) return "known-unimplemented";
  if (z_direct_exe_backend(target) != Z_DIRECT_BACKEND_NONE) return "native-exe";
  if (z_direct_object_backend(target) != Z_DIRECT_BACKEND_NONE) return "native-object";
  return "known-unimplemented";
}

const char *z_direct_backend_reason(const ZTargetInfo *target) {
  if (!target) return "unknown target";
  const char *format = target->object_format ? target->object_format : "unknown";
  const char *arch = target->arch ? target->arch : "unknown";
  if (z_direct_object_backend(target) != Z_DIRECT_BACKEND_NONE) {
    if (z_direct_exe_backend(target) != Z_DIRECT_BACKEND_NONE) return "direct object and executable backend available";
    return "direct object backend available; direct executable linker is not implemented for this target";
  }
  if (strcmp(format, "elf") == 0 && strcmp(arch, "aarch64") == 0) return "AArch64 ELF machine-code backend is not implemented yet";
  if (strcmp(format, "coff") == 0 && strcmp(arch, "aarch64") == 0) return "AArch64 COFF machine-code backend is not implemented yet";
  return "direct backend is not implemented for this target format/architecture pair";
}
