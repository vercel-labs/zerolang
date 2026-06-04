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
  {Z_DIRECT_BACKEND_MACHO_X64, "zero-macho-x64", "zero-macho-x64-exe", "macho64", "direct-macho-x64-object", "direct-macho-x64-exe", "direct-macho-x64-object-runtime-link", true},
  {Z_DIRECT_BACKEND_COFF_X64, "zero-coff-x64", "zero-coff-x64-exe", "coff", "direct-coff-x64-object", "direct-coff-x64-exe", "unsupported", false},
  {Z_DIRECT_BACKEND_COFF_AARCH64, "zero-coff-aarch64", "zero-coff-aarch64-exe", "coff", "direct-coff-aarch64-object", "direct-coff-aarch64-exe", "unsupported", false},
};

static const ZDirectBackendRule direct_backend_rules[] = {
  {"elf", "linux", "x86_64", "gnu", Z_DIRECT_BACKEND_ELF64, true, true},
  {"elf", "linux", "x86_64", "musl", Z_DIRECT_BACKEND_ELF64, true, true},
  {"elf", "linux", "aarch64", "gnu", Z_DIRECT_BACKEND_ELF_AARCH64, true, true},
  {"elf", "linux", "aarch64", "musl", Z_DIRECT_BACKEND_ELF_AARCH64, true, true},
  {"macho", "macos", "aarch64", "darwin", Z_DIRECT_BACKEND_MACHO64, true, true},
  {"macho", "macos", "x86_64", "darwin", Z_DIRECT_BACKEND_MACHO_X64, true, true},
  {"coff", "windows", "x86_64", "msvc", Z_DIRECT_BACKEND_COFF_X64, true, true},
  {"coff", "windows", "aarch64", "msvc", Z_DIRECT_BACKEND_COFF_AARCH64, true, true},
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

ZToolchainPlan z_direct_backend_toolchain_plan(ZDirectBackend backend, const ZTargetInfo *target) {
  const ZDirectBackendDescriptor *descriptor = direct_backend_descriptor(backend);
  const char *driver = descriptor ? descriptor->object_emitter : "none";
  ZToolchainPlan plan = {
    .driver_kind = driver,
    .selection_source = descriptor ? "direct-backend" : "unsupported",
    .compiler = driver,
    .target_triple = target && target->zig_target ? target->zig_target : "",
    .linker_flavor = descriptor ? descriptor->linker_flavor : "none",
    .libc_mode = "none",
    .sysroot_env = "",
    .sysroot_path = "",
    .sysroot_status = "not-required",
    .requires_sysroot = false,
    .uses_target_flag = false,
    .uses_zig_cache = false,
    .strip_artifact = false
  };
  return plan;
}

bool z_direct_backend_toolchain_plan_for_emit_kind(const ZTargetInfo *target, const char *emit_kind, const char *requested_backend, ZToolchainPlan *out) {
  ZDirectBackend backend = z_direct_backend_for_emit_kind(target, emit_kind, requested_backend);
  if (backend == Z_DIRECT_BACKEND_NONE) return false;
  if (out) *out = z_direct_backend_toolchain_plan(backend, target);
  return true;
}

const char *z_direct_backend_runtime_object_cache_key(ZDirectBackend backend) {
  const ZDirectBackendDescriptor *descriptor = direct_backend_descriptor(backend);
  return descriptor ? descriptor->runtime_object_cache_key : "unsupported";
}

size_t z_direct_backend_symbol_overhead(ZDirectBackend backend, bool has_readonly_data) {
  switch (backend) {
    case Z_DIRECT_BACKEND_COFF_X64:
    case Z_DIRECT_BACKEND_COFF_AARCH64: return has_readonly_data ? 2 : 1;
    case Z_DIRECT_BACKEND_ELF64:
    case Z_DIRECT_BACKEND_ELF_AARCH64:
    case Z_DIRECT_BACKEND_MACHO64:
    case Z_DIRECT_BACKEND_MACHO_X64: return has_readonly_data ? 1 : 0;
    case Z_DIRECT_BACKEND_NONE: return 0;
  }
  return 0;
}

bool z_direct_backend_supports_runtime_object(ZDirectBackend backend) {
  const ZDirectBackendDescriptor *descriptor = direct_backend_descriptor(backend);
  return descriptor ? descriptor->runtime_object_supported : false;
}

const char *z_direct_runtime_link_blocker(const ZTargetInfo *target, bool needs_http_runtime) {
  ZDirectBackend object_backend = z_direct_object_backend(target);
  if (!z_direct_backend_supports_runtime_object(object_backend)) return "runtime helpers currently require the Mach-O or ELF64 object link plan";
  if (needs_http_runtime && !z_target_is_host(target)) return "HTTP runtime provider is host-only for direct executable links";
  return NULL;
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
  return "direct backend is not implemented for this target format/architecture pair";
}

ZDirectBackend z_direct_backend_for_emit_kind(const ZTargetInfo *target, const char *emit_kind, const char *requested_backend) {
  if (emit_kind && strcmp(emit_kind, "obj") == 0) {
    ZDirectBackend backend = z_direct_object_backend(target);
    if (requested_backend && requested_backend[0] && !z_direct_requested_backend_matches(requested_backend, backend)) return Z_DIRECT_BACKEND_NONE;
    return backend;
  }
  if (emit_kind && strcmp(emit_kind, "exe") == 0 && requested_backend) {
    ZDirectBackend backend = z_direct_exe_backend(target);
    if (requested_backend[0] && !z_direct_requested_backend_matches(requested_backend, backend)) return Z_DIRECT_BACKEND_NONE;
    return backend;
  }
  return Z_DIRECT_BACKEND_NONE;
}

const char *z_direct_backend_emitter_for_emit_kind(const ZTargetInfo *target, const char *emit_kind, const char *requested_backend) {
  ZDirectBackend backend = z_direct_backend_for_emit_kind(target, emit_kind, requested_backend);
  if (backend == Z_DIRECT_BACKEND_NONE) return "none";
  if (emit_kind && strcmp(emit_kind, "exe") == 0) return z_direct_backend_exe_emitter(backend);
  return z_direct_backend_object_emitter(backend);
}

const char *z_direct_backend_name_for_emit_kind(const ZTargetInfo *target, const char *emit_kind, const char *requested_backend) {
  if (requested_backend && requested_backend[0]) return requested_backend;
  if (emit_kind && strcmp(emit_kind, "obj") == 0) return z_direct_object_emitter(target);
  if (emit_kind && strcmp(emit_kind, "exe") == 0) return z_direct_exe_emitter(target);
  return "none";
}

static const char *direct_artifact_kind_for_emit_kind(const char *emit_kind) {
  if (emit_kind && strcmp(emit_kind, "obj") == 0) return "native-object";
  if (emit_kind && strcmp(emit_kind, "exe") == 0) return "native-executable";
  return "artifact";
}

ZDirectReleaseTargetFacts z_direct_release_target_facts(const ZTargetInfo *target, const char *emit_kind, const char *requested_backend, const ZToolchainPlan *fallback_plan, bool linked_executable) {
  bool selected_executable = emit_kind && strcmp(emit_kind, "exe") == 0;
  ZDirectBackend selected_backend = linked_executable ? z_direct_object_backend(target) : z_direct_backend_for_emit_kind(target, emit_kind, requested_backend);
  if (selected_backend == Z_DIRECT_BACKEND_NONE && selected_executable && !linked_executable) selected_backend = z_direct_exe_backend(target);
  bool direct_selected = selected_backend != Z_DIRECT_BACKEND_NONE;
  bool target_requires_sysroot = z_target_requires_sysroot(target);
  bool artifact_requires_sysroot = linked_executable ? (fallback_plan && fallback_plan->requires_sysroot) : (!direct_selected && fallback_plan && fallback_plan->requires_sysroot);
  const char *fallback_linker = fallback_plan ? fallback_plan->linker_flavor : "none";
  const char *fallback_libc = fallback_plan ? fallback_plan->libc_mode : "";
  const char *fallback_sysroot_status = fallback_plan ? fallback_plan->sysroot_status : "not-required";
  ZDirectReleaseTargetFacts facts = {
    .selected_emitter = selected_backend == Z_DIRECT_BACKEND_NONE ? "none" : (selected_executable && !linked_executable ? z_direct_backend_exe_emitter(selected_backend) : z_direct_backend_object_emitter(selected_backend)),
    .artifact_kind = direct_artifact_kind_for_emit_kind(emit_kind),
    .linker_flavor = linked_executable || !direct_selected ? fallback_linker : z_direct_backend_linker_flavor(selected_backend),
    .artifact_libc_mode = linked_executable || !direct_selected ? fallback_libc : "none",
    .sysroot_status = artifact_requires_sysroot ? fallback_sysroot_status : (target_requires_sysroot ? "not-used-by-direct-artifact" : "not-required"),
    .direct_selected = direct_selected,
    .target_requires_sysroot = target_requires_sysroot,
    .artifact_requires_sysroot = artifact_requires_sysroot
  };
  return facts;
}

ZDirectObjectBackendFacts z_direct_object_backend_facts(const ZTargetInfo *target, const char *emit_kind, const char *requested_backend, bool has_runtime_imports) {
  bool emit_obj = emit_kind && strcmp(emit_kind, "obj") == 0;
  bool emit_exe = emit_kind && strcmp(emit_kind, "exe") == 0;
  bool metadata_only = emit_kind && (strcmp(emit_kind, "mem") == 0 || strcmp(emit_kind, "size") == 0);
  bool runtime_linked_exe = emit_exe && has_runtime_imports;
  bool requested_exe = emit_exe && requested_backend;
  ZDirectBackend backend = z_direct_backend_for_emit_kind(target, emit_kind, requested_backend);
  const char *emitter = z_direct_backend_emitter_for_emit_kind(target, emit_kind, requested_backend);
  if (metadata_only || runtime_linked_exe) {
    backend = z_direct_object_backend(target);
    if (metadata_only && requested_backend && requested_backend[0] && !z_direct_requested_backend_matches(requested_backend, backend)) {
      backend = Z_DIRECT_BACKEND_NONE;
      emitter = "none";
    } else {
      emitter = z_direct_backend_object_emitter(backend);
      if (metadata_only && (!emitter || strcmp(emitter, "none") == 0)) emitter = "metadata-only";
    }
  }
  bool active = (metadata_only && backend != Z_DIRECT_BACKEND_NONE) || runtime_linked_exe || (backend != Z_DIRECT_BACKEND_NONE && (emit_obj || requested_exe));
  bool executable_artifact = requested_exe && !runtime_linked_exe;
  ZDirectObjectBackendFacts facts = {
    .backend = backend,
    .selected_emitter = emitter,
    .artifact_path = backend != Z_DIRECT_BACKEND_NONE ? z_direct_backend_artifact_path(backend, executable_artifact) : "unsupported",
    .linker_flavor = backend != Z_DIRECT_BACKEND_NONE ? z_direct_backend_linker_flavor(backend) : "none",
    .active = active
  };
  return facts;
}

ZDirectObjectTargetFacts z_direct_object_target_facts(const ZTargetInfo *target) {
  ZDirectBackend backend = z_direct_object_backend(target);
  bool available = backend != Z_DIRECT_BACKEND_NONE;
  ZDirectObjectTargetFacts facts = {
    .backend = backend,
    .artifact_path = available ? z_direct_backend_artifact_path(backend, false) : "unsupported",
    .unsupported_reason = available ? "" : z_direct_backend_reason(target),
    .available = available
  };
  return facts;
}

ZDirectRuntimeObjectFacts z_direct_runtime_object_facts(const ZTargetInfo *target, bool needs_http_runtime) {
  ZDirectBackend backend = z_direct_object_backend(target);
  const char *blocker = z_direct_runtime_link_blocker(target, needs_http_runtime);
  ZDirectRuntimeObjectFacts facts = {
    .backend = backend,
    .cache_key = backend != Z_DIRECT_BACKEND_NONE ? z_direct_backend_runtime_object_cache_key(backend) : "unsupported",
    .blocker = blocker,
    .supported = blocker == NULL
  };
  return facts;
}

ZDirectExecutableTargetFacts z_direct_executable_target_facts(const ZTargetInfo *target, const char *requested_backend) {
  ZDirectBackend backend = z_direct_exe_backend(target);
  bool requested = requested_backend && requested_backend[0];
  bool available = backend != Z_DIRECT_BACKEND_NONE;
  ZDirectExecutableTargetFacts facts = {
    .backend = backend,
    .default_request_name = available ? z_direct_backend_object_emitter(backend) : "none",
    .artifact_path = available ? z_direct_backend_artifact_path(backend, true) : "unsupported",
    .requested = requested,
    .requested_name = z_direct_backend_is_request_name(requested_backend),
    .request_supported = available && z_direct_requested_backend_matches(requested_backend, backend)
  };
  return facts;
}

const char *z_direct_backend_expected(const ZTargetInfo *target) {
  ZDirectBackend backend = z_direct_object_backend(target);
  const char *format = target && target->object_format ? target->object_format : "";
  const char *arch = target && target->arch ? target->arch : "";
  if (backend == Z_DIRECT_BACKEND_MACHO64) return "direct AArch64 Mach-O object subset";
  if (backend == Z_DIRECT_BACKEND_MACHO_X64) return "direct x86_64 Mach-O object subset";
  if (strcmp(format, "macho") == 0) return "direct Mach-O object subset";
  if (backend == Z_DIRECT_BACKEND_COFF_AARCH64) return "direct COFF AArch64 object subset";
  if (backend == Z_DIRECT_BACKEND_COFF_X64 || strcmp(format, "coff") == 0) return "direct COFF x64 object subset";
  if (backend == Z_DIRECT_BACKEND_ELF_AARCH64 || (strcmp(format, "elf") == 0 && strcmp(arch, "aarch64") == 0)) return "direct AArch64 ELF object subset";
  if (backend == Z_DIRECT_BACKEND_ELF64 || strcmp(format, "elf") == 0) return "direct ELF64 object subset";
  return "direct target with matching object format and architecture";
}

const char *z_direct_backend_help(const ZTargetInfo *target) {
  ZDirectBackend backend = z_direct_object_backend(target);
  const char *format = target && target->object_format ? target->object_format : "";
  const char *arch = target && target->arch ? target->arch : "";
  if (backend == Z_DIRECT_BACKEND_MACHO_X64) return "reduce the program to primitive x86_64 Mach-O direct-backend constructs or choose a supported direct target";
  if (backend == Z_DIRECT_BACKEND_MACHO64 || backend == Z_DIRECT_BACKEND_ELF_AARCH64 ||
      strcmp(format, "macho") == 0 || (strcmp(format, "elf") == 0 && strcmp(arch, "aarch64") == 0)) {
    return "choose a supported direct target or reduce the program to supported AArch64 direct-backend constructs";
  }
  if (backend == Z_DIRECT_BACKEND_COFF_AARCH64) return "reduce the program to AArch64 COFF supported direct-backend constructs or choose a supported direct target";
  if (backend == Z_DIRECT_BACKEND_COFF_X64 || strcmp(format, "coff") == 0) return "reduce the program to primitive direct-backend constructs or choose a supported direct target";
  return "choose a supported direct target or restrict this program to exported primitive integer arithmetic functions";
}

const char *z_direct_backend_target_label(const ZTargetInfo *target) {
  ZDirectBackend backend = target ? z_direct_object_backend(target) : Z_DIRECT_BACKEND_NONE;
  switch (backend) {
    case Z_DIRECT_BACKEND_ELF64: return "ELF64";
    case Z_DIRECT_BACKEND_ELF_AARCH64: return "AArch64 ELF";
    case Z_DIRECT_BACKEND_MACHO64: return "AArch64 Mach-O";
    case Z_DIRECT_BACKEND_MACHO_X64: return "x86_64 Mach-O";
    case Z_DIRECT_BACKEND_COFF_X64: return "COFF x64";
    case Z_DIRECT_BACKEND_COFF_AARCH64: return "COFF AArch64";
    default: return NULL;
  }
}
