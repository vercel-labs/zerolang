#include "zero.h"

#include <string.h>

typedef struct {
  ZDirectBackend backend;
  const char *object_emitter;
  const char *exe_emitter;
  const char *linker_flavor;
  const char *object_artifact_path;
  const char *exe_artifact_path;
  const char *runtime_object_cache_key;
  bool runtime_object_supported;
} ZDirectBackendDescriptor;

typedef struct {
  const char *object_format;
  const char *os;
  const char *arch;
  const char *abi;
  ZDirectBackend backend;
  bool object_supported;
  bool exe_supported;
} ZDirectBackendRule;

static const ZDirectBackendDescriptor direct_backend_descriptors[] = {
  {Z_DIRECT_BACKEND_ELF64, "zero-elf64", "zero-elf64-exe", "elf64", "direct-elf64-object", "direct-elf64-exe", "direct-elf64-object-runtime-link", true},
  {Z_DIRECT_BACKEND_ELF_AARCH64, "zero-elf-aarch64", "zero-elf-aarch64-exe", "elf64", "direct-elf-aarch64-object", "direct-elf-aarch64-exe", "unsupported", false},
  {Z_DIRECT_BACKEND_MACHO64, "zero-macho64", "zero-macho64-exe", "macho64", "direct-macho64-object", "direct-macho64-exe", "direct-macho64-object-runtime-link", true},
  {Z_DIRECT_BACKEND_COFF_X64, "zero-coff-x64", "zero-coff-x64-exe", "coff", "direct-coff-x64-object", "direct-coff-x64-exe", "unsupported", false},
};

static const ZDirectBackendRule direct_backend_rules[] = {
  {"elf", "linux", "x86_64", "gnu", Z_DIRECT_BACKEND_ELF64, true, true},
  {"elf", "linux", "x86_64", "musl", Z_DIRECT_BACKEND_ELF64, true, true},
  {"elf", "linux", "aarch64", "gnu", Z_DIRECT_BACKEND_ELF_AARCH64, true, true},
  {"elf", "linux", "aarch64", "musl", Z_DIRECT_BACKEND_ELF_AARCH64, true, true},
  {"macho", "macos", "aarch64", "darwin", Z_DIRECT_BACKEND_MACHO64, true, true},
  {"coff", "windows", "x86_64", "msvc", Z_DIRECT_BACKEND_COFF_X64, true, true},
};

static const ZDirectBackendDescriptor *direct_backend_descriptor(ZDirectBackend backend) {
  for (size_t i = 0; i < sizeof(direct_backend_descriptors) / sizeof(direct_backend_descriptors[0]); i++) {
    if (direct_backend_descriptors[i].backend == backend) return &direct_backend_descriptors[i];
  }
  return NULL;
}

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
  const ZDirectBackendDescriptor *descriptor = direct_backend_descriptor(backend);
  return descriptor ? descriptor->object_emitter : "none";
}

const char *z_direct_backend_exe_emitter(ZDirectBackend backend) {
  const ZDirectBackendDescriptor *descriptor = direct_backend_descriptor(backend);
  return descriptor ? descriptor->exe_emitter : "none";
}

ZDirectBackend z_direct_backend_from_emitter(const char *emitter) {
  if (!emitter) return Z_DIRECT_BACKEND_NONE;
  for (size_t i = 0; i < sizeof(direct_backend_descriptors) / sizeof(direct_backend_descriptors[0]); i++) {
    const ZDirectBackendDescriptor *descriptor = &direct_backend_descriptors[i];
    if (strcmp(emitter, descriptor->object_emitter) == 0) return descriptor->backend;
    if (strcmp(emitter, descriptor->exe_emitter) == 0) return descriptor->backend;
  }
  return Z_DIRECT_BACKEND_NONE;
}

const char *z_direct_backend_linker_flavor(ZDirectBackend backend) {
  const ZDirectBackendDescriptor *descriptor = direct_backend_descriptor(backend);
  return descriptor ? descriptor->linker_flavor : "none";
}

const char *z_direct_backend_artifact_path(ZDirectBackend backend, bool executable) {
  const ZDirectBackendDescriptor *descriptor = direct_backend_descriptor(backend);
  if (!descriptor) return "unsupported";
  return executable ? descriptor->exe_artifact_path : descriptor->object_artifact_path;
}

const char *z_direct_backend_runtime_object_cache_key(ZDirectBackend backend) {
  const ZDirectBackendDescriptor *descriptor = direct_backend_descriptor(backend);
  return descriptor ? descriptor->runtime_object_cache_key : "unsupported";
}

size_t z_direct_backend_symbol_overhead(ZDirectBackend backend, bool has_readonly_data) {
  switch (backend) {
    case Z_DIRECT_BACKEND_COFF_X64: return has_readonly_data ? 2 : 1;
    case Z_DIRECT_BACKEND_ELF64:
    case Z_DIRECT_BACKEND_ELF_AARCH64:
    case Z_DIRECT_BACKEND_MACHO64: return has_readonly_data ? 1 : 0;
    case Z_DIRECT_BACKEND_NONE: return 0;
  }
  return 0;
}

bool z_direct_backend_supports_runtime_object(ZDirectBackend backend) {
  const ZDirectBackendDescriptor *descriptor = direct_backend_descriptor(backend);
  return descriptor ? descriptor->runtime_object_supported : false;
}

bool z_direct_backend_emitter_is_executable(const char *emitter) {
  if (!emitter) return false;
  for (size_t i = 0; i < sizeof(direct_backend_descriptors) / sizeof(direct_backend_descriptors[0]); i++) {
    if (strcmp(emitter, direct_backend_descriptors[i].exe_emitter) == 0) return true;
  }
  return false;
}

bool z_direct_backend_is_request_name(const char *requested_backend) {
  if (!requested_backend || !requested_backend[0]) return false;
  for (size_t i = 0; i < sizeof(direct_backend_descriptors) / sizeof(direct_backend_descriptors[0]); i++) {
    if (strcmp(requested_backend, direct_backend_descriptors[i].object_emitter) == 0) return true;
  }
  return false;
}

bool z_direct_requested_backend_matches(const char *requested_backend, ZDirectBackend backend) {
  if (!requested_backend || !requested_backend[0]) return true;
  const ZDirectBackendDescriptor *descriptor = direct_backend_descriptor(backend);
  return descriptor && strcmp(requested_backend, descriptor->object_emitter) == 0;
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

const char *z_direct_backend_expected(const ZTargetInfo *target) {
  ZDirectBackend backend = z_direct_object_backend(target);
  const char *format = target && target->object_format ? target->object_format : "";
  const char *arch = target && target->arch ? target->arch : "";
  if (backend == Z_DIRECT_BACKEND_MACHO64 || strcmp(format, "macho") == 0) return "direct AArch64 Mach-O object MVP subset";
  if (backend == Z_DIRECT_BACKEND_COFF_X64 || strcmp(format, "coff") == 0) return "direct COFF x64 object MVP subset";
  if (backend == Z_DIRECT_BACKEND_ELF_AARCH64 || (strcmp(format, "elf") == 0 && strcmp(arch, "aarch64") == 0)) return "direct AArch64 ELF object MVP subset";
  if (backend == Z_DIRECT_BACKEND_ELF64 || strcmp(format, "elf") == 0) return "direct ELF64 object MVP subset";
  return "direct target with matching object format and architecture";
}

const char *z_direct_backend_help(const ZTargetInfo *target) {
  ZDirectBackend backend = z_direct_object_backend(target);
  const char *format = target && target->object_format ? target->object_format : "";
  const char *arch = target && target->arch ? target->arch : "";
  if (backend == Z_DIRECT_BACKEND_MACHO64 || backend == Z_DIRECT_BACKEND_ELF_AARCH64 ||
      strcmp(format, "macho") == 0 || (strcmp(format, "elf") == 0 && strcmp(arch, "aarch64") == 0)) {
    return "choose a supported direct target or restrict this program to exported no-parameter functions returning small integer literals";
  }
  if (backend == Z_DIRECT_BACKEND_COFF_X64 || strcmp(format, "coff") == 0) return "reduce the program to primitive direct-backend constructs or choose a supported direct target";
  return "choose a supported direct target or restrict this program to exported primitive integer arithmetic functions";
}
