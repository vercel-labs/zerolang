#include "zero.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

static void llvm_append_json_string(ZBuf *buf, const char *value) {
  zbuf_append_char(buf, '"');
  for (const char *cursor = value ? value : ""; *cursor; cursor++) {
    unsigned char ch = (unsigned char)*cursor;
    if (ch == '"') zbuf_append(buf, "\\\"");
    else if (ch == '\\') zbuf_append(buf, "\\\\");
    else if (ch == '\n') zbuf_append(buf, "\\n");
    else if (ch == '\r') zbuf_append(buf, "\\r");
    else if (ch == '\t') zbuf_append(buf, "\\t");
    else if (ch < 0x20) zbuf_appendf(buf, "\\u%04x", (unsigned)ch);
    else zbuf_append_char(buf, (char)ch);
  }
  zbuf_append_char(buf, '"');
}

static void llvm_append_capabilities_json(ZBuf *buf, const ZTargetInfo *target) {
  zbuf_append_char(buf, '[');
  const char *caps[] = {"memory", "stdio", "args", "env", "fs", "net", "proc", "time", "rand", "web", NULL};
  bool first = true;
  for (int i = 0; caps[i]; i++) {
    if (!z_target_has_capability(target, caps[i])) continue;
    if (!first) zbuf_append_char(buf, ',');
    first = false;
    llvm_append_json_string(buf, caps[i]);
  }
  zbuf_append_char(buf, ']');
}

static bool llvm_path_is_executable_file(const char *path) {
  struct stat st;
  if (!path || !path[0] || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return false;
#if defined(_WIN32)
  return _access(path, 4) == 0;
#else
  return access(path, X_OK) == 0;
#endif
}

static bool llvm_command_available(const char *name) {
  if (!name || !name[0]) return false;
  if (strchr(name, '/') || strchr(name, '\\')) return llvm_path_is_executable_file(name);
  const char *path = getenv("PATH");
  if (!path || !path[0]) return false;
#if defined(_WIN32)
  const char separator = ';';
#else
  const char separator = ':';
#endif
  const char *cursor = path;
  while (*cursor) {
    const char *end = strchr(cursor, separator);
    size_t len = end ? (size_t)(end - cursor) : strlen(cursor);
    if (len > 0) {
      ZBuf candidate;
      zbuf_init(&candidate);
      for (size_t i = 0; i < len; i++) zbuf_append_char(&candidate, cursor[i]);
      zbuf_append_char(&candidate, '/');
      zbuf_append(&candidate, name);
      bool found = llvm_path_is_executable_file(candidate.data);
      zbuf_free(&candidate);
      if (found) return true;
    }
    if (!end) break;
    cursor = end + 1;
  }
  return false;
}

const char *z_llvm_target_triple(const ZTargetInfo *target) {
  if (!target || !target->name) return "unknown-unknown-unknown";
  if (strcmp(target->name, "darwin-arm64") == 0) return "arm64-apple-darwin";
  if (strcmp(target->name, "darwin-x64") == 0) return "x86_64-apple-darwin";
  if (strcmp(target->name, "linux-arm64") == 0) return "aarch64-unknown-linux-gnu";
  if (strcmp(target->name, "linux-x64") == 0) return "x86_64-unknown-linux-gnu";
  if (strcmp(target->name, "linux-musl-arm64") == 0) return "aarch64-unknown-linux-musl";
  if (strcmp(target->name, "linux-musl-x64") == 0) return "x86_64-unknown-linux-musl";
  if (strcmp(target->name, "win32-arm64.exe") == 0) return "aarch64-pc-windows-msvc";
  if (strcmp(target->name, "win32-x64.exe") == 0) return "x86_64-pc-windows-msvc";
  return "unknown-unknown-unknown";
}

static bool llvm_native_target_supported(const ZTargetInfo *target) {
  if (!target || !z_target_is_host(target)) return false;
  return target->os && (strcmp(target->os, "macos") == 0 || strcmp(target->os, "linux") == 0);
}

const char *z_llvm_optimization_level(const char *profile) {
  const char *value = profile && profile[0] ? profile : "release";
  if (strcmp(value, "debug") == 0 || strcmp(value, "dev") == 0) return "-O0";
  if (strcmp(value, "fast") == 0 || strcmp(value, "release-fast") == 0) return "-O2";
  if (strcmp(value, "audit") == 0) return "-O1";
  if (strcmp(value, "tiny") == 0 || strcmp(value, "small") == 0 || strcmp(value, "release-small") == 0 || strcmp(value, "release") == 0) return "-Oz";
  return "-Oz";
}

ZLlvmToolchainPlan z_llvm_toolchain_plan(const ZTargetInfo *target) {
  const char *env = getenv("ZERO_LLVM_CLANG");
  bool env_selected = env && env[0];
  const char *compiler = env_selected ? env : "clang";
  bool target_supported = llvm_native_target_supported(target);
  bool tool_available = llvm_command_available(compiler);
  const char *status = "ready";
  const char *reason = "clang can compile textual LLVM IR for this host target";
  if (!target_supported) {
    status = "unsupported-target";
    reason = "native LLVM executable builds are currently limited to macOS/Linux host targets";
  } else if (!tool_available) {
    status = "tool-missing";
    reason = "clang is required for native LLVM executable builds";
  }
  ZLlvmToolchainPlan plan = {
    .driver_kind = "clang",
    .selection_source = env_selected ? "env" : "path",
    .compiler = compiler,
    .target_triple = z_llvm_target_triple(target),
    .status = status,
    .reason = reason,
    .target_supported = target_supported,
    .tool_available = tool_available,
    .native_executable = target_supported && tool_available
  };
  return plan;
}

ZToolchainPlan z_llvm_c_toolchain_plan(const ZTargetInfo *target) {
  ZLlvmToolchainPlan llvm = z_llvm_toolchain_plan(target);
  ZToolchainPlan plan = {
    .driver_kind = "override-cc",
    .selection_source = llvm.selection_source,
    .compiler = llvm.compiler,
    .target_triple = llvm.target_triple,
    .linker_flavor = "clang",
    .libc_mode = target ? z_target_libc_mode(target) : "host-default",
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

bool z_llvm_native_executable_ready(const ZTargetInfo *target, const char *path, ZDiag *diag) {
  ZLlvmToolchainPlan plan = z_llvm_toolchain_plan(target);
  if (plan.native_executable) return true;
  if (diag) {
    memset(diag, 0, sizeof(*diag));
    diag->code = 2004;
    diag->path = path;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    if (!plan.target_supported) {
      snprintf(diag->message, sizeof(diag->message), "LLVM native executable backend does not support target '%s' yet", target && target->name ? target->name : "unknown");
      snprintf(diag->expected, sizeof(diag->expected), "host macOS/Linux target for native LLVM executable builds");
      snprintf(diag->actual, sizeof(diag->actual), "target=%s host=%s", target && target->name ? target->name : "unknown", z_host_target());
      snprintf(diag->help, sizeof(diag->help), "use --emit llvm-ir to inspect LLVM IR or choose the current host target for native LLVM execution");
      z_backend_blocker_set(&diag->backend_blocker,
                            target && target->name ? target->name : "unknown",
                            target && target->object_format ? target->object_format : "unknown",
                            "llvm",
                            "target-selection",
                            "llvm host executable target");
    } else {
      snprintf(diag->message, sizeof(diag->message), "LLVM native executable backend requires clang");
      snprintf(diag->expected, sizeof(diag->expected), "clang on PATH or ZERO_LLVM_CLANG");
      snprintf(diag->actual, sizeof(diag->actual), "%s not executable or not found", plan.compiler ? plan.compiler : "clang");
      snprintf(diag->help, sizeof(diag->help), "install clang or set ZERO_LLVM_CLANG to a clang-compatible driver");
      z_backend_blocker_set(&diag->backend_blocker,
                            target && target->name ? target->name : "unknown",
                            target && target->object_format ? target->object_format : "unknown",
                            "llvm",
                            "toolchain",
                            "clang");
    }
  }
  return false;
}

bool z_llvm_link_executable(const char *llvm_file, const char *runtime_object_file, const char *exe_file, const ZToolchainPlan *plan, const ZTargetInfo *target, const char *profile, bool links_zero_runtime, ZDiag *diag) {
  ZBuf pre_flags;
  zbuf_init(&pre_flags);
  zbuf_append(&pre_flags, "-Wno-override-module ");
  zbuf_append(&pre_flags, z_llvm_optimization_level(profile));
#if defined(__linux__)
  zbuf_append(&pre_flags, " -no-pie");
#endif
  const char *object_files[2] = {0};
  size_t object_count = 0;
  if (llvm_file && llvm_file[0]) object_files[object_count++] = llvm_file;
  if (runtime_object_file && runtime_object_file[0]) object_files[object_count++] = runtime_object_file;
  bool ok = z_toolchain_link_objects(plan, target, object_files, object_count, exe_file, pre_flags.data ? pre_flags.data : "-Wno-override-module", "2>/dev/null");
  zbuf_free(&pre_flags);
  if (!ok && diag) {
    diag->code = 2004;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "LLVM executable link failed");
    snprintf(diag->expected, sizeof(diag->expected), "%s", links_zero_runtime ? "LLVM IR plus zero runtime object link successfully with clang" : "LLVM IR links successfully with clang");
    snprintf(diag->actual, sizeof(diag->actual), "clang LLVM link command failed");
    snprintf(diag->help, sizeof(diag->help), "inspect the emitted LLVM IR, runtime object, and clang installation; use --emit llvm-ir to write the IR only");
    z_backend_blocker_set(&diag->backend_blocker, target && target->name ? target->name : "unknown", target && target->object_format ? target->object_format : "unknown", "llvm", "toolchain", "clang");
  }
  return ok;
}

void z_append_llvm_toolchain_plan_json(ZBuf *buf, const ZTargetInfo *target) {
  ZLlvmToolchainPlan plan = z_llvm_toolchain_plan(target);
  zbuf_append(buf, "{\"driverKind\":\"clang\",\"selectionSource\":");
  llvm_append_json_string(buf, plan.selection_source);
  zbuf_append(buf, ",\"compiler\":");
  llvm_append_json_string(buf, plan.compiler);
  zbuf_append(buf, ",\"targetTriple\":");
  llvm_append_json_string(buf, plan.target_triple);
  zbuf_append(buf, ",\"linkerFlavor\":\"clang\",\"libcMode\":");
  llvm_append_json_string(buf, target ? z_target_libc_mode(target) : "host-default");
  zbuf_append(buf, ",\"requiresSysroot\":false,\"sysrootEnv\":\"\",\"sysrootStatus\":\"not-required\",\"usesTargetFlag\":false,\"usesToolchainCache\":false,\"stripArtifact\":false,\"available\":");
  zbuf_append(buf, plan.tool_available ? "true" : "false");
  zbuf_append(buf, ",\"status\":");
  llvm_append_json_string(buf, plan.status);
  zbuf_append(buf, ",\"reason\":");
  llvm_append_json_string(buf, plan.reason);
  z_append_llvm_backend_lifecycle_field_json(buf);
  zbuf_append(buf, "}");
}

void z_append_llvm_target_backend_json(ZBuf *buf, const ZTargetInfo *target) {
  ZLlvmToolchainPlan llvm = z_llvm_toolchain_plan(target);
  zbuf_append(buf, "{\"status\":");
  llvm_append_json_string(buf, llvm.status);
  zbuf_append(buf, ",\"buildable\":");
  zbuf_append(buf, llvm.native_executable ? "true" : "false");
  zbuf_append(buf, ",\"emit\":[\"llvm-ir\"");
  if (llvm.native_executable) zbuf_append(buf, ",\"exe\"");
  zbuf_append(buf, "],\"targetTriple\":");
  llvm_append_json_string(buf, llvm.target_triple);
  zbuf_append(buf, ",\"toolchain\":{\"driverKind\":\"clang\",\"selectionSource\":");
  llvm_append_json_string(buf, llvm.selection_source);
  zbuf_append(buf, ",\"available\":");
  zbuf_append(buf, llvm.tool_available ? "true" : "false");
  zbuf_append(buf, "},\"reason\":");
  llvm_append_json_string(buf, llvm.reason);
  z_append_llvm_backend_lifecycle_field_json(buf);
  zbuf_append(buf, "}");
}

void z_append_llvm_ir_backend_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const char *emit_kind) {
  bool links_zero_runtime = input && input->direct_runtime_helper_count > 0;
  zbuf_append(buf, "{\"internalIr\":{\"typeRepresentation\":\"Zero MIR scalar values\",\"controlFlowRepresentation\":\"LLVM textual IR blocks\",\"callRepresentation\":\"direct MIR calls\",\"debugRepresentation\":\"source spans retained on diagnostics\"}");
  zbuf_append(buf, ",\"objectEmission\":{\"path\":\"llvm-ir\",\"functions\":true,\"dataSections\":");
  zbuf_append(buf, input && input->direct_readonly_data_bytes > 0 ? "true" : "false");
  zbuf_appendf(buf, ",\"symbols\":true,\"relocations\":\"none\",\"symbolCount\":%zu,\"internalHelperCount\":0}", input ? input->direct_function_count + input->direct_runtime_helper_count : 0);
  zbuf_append(buf, ",\"linking\":{\"linkerFlavor\":\"none\",\"objectFormat\":");
  llvm_append_json_string(buf, target && target->object_format ? target->object_format : "unknown");
  zbuf_appendf(buf, ",\"targetLibraries\":\"%s\",\"symbolMap\":\"llvm-module\",\"externalToolchain\":\"none\",\"toolchainSource\":\"%s\",\"stripArtifacts\":false}", links_zero_runtime ? "zero-runtime" : "none", links_zero_runtime ? "textual-llvm-ir-runtime-link-plan" : "textual-llvm-ir");
  zbuf_appendf(buf, ",\"linkerPlan\":{\"format\":\"llvm-ir\",\"flavor\":\"none\",\"archives\":[],\"staticLibraries\":%s,\"importLibraries\":[],\"systemLibraries\":[],\"rpaths\":[],\"loadPaths\":[],\"visibility\":\"exported-c-and-main-only\",\"crossLinking\":false,\"externalToolchain\":\"none\",\"reproducible\":true,\"libcMode\":\"none\",\"requiresSysroot\":false,\"sysrootStatus\":\"not-required\"}", links_zero_runtime ? "[\"zero_runtime.o\"]" : "[]");
  zbuf_append(buf, ",\"targetFacts\":{\"directAvailable\":false,\"llvmAvailable\":true,\"status\":\"ir-only\",\"selectedEmitter\":\"llvm-ir\",\"objectFormat\":");
  llvm_append_json_string(buf, target && target->object_format ? target->object_format : "unknown");
  zbuf_append(buf, ",\"arch\":");
  llvm_append_json_string(buf, target && target->arch ? target->arch : "unknown");
  zbuf_append(buf, ",\"abi\":");
  llvm_append_json_string(buf, target && target->abi ? target->abi : "");
  zbuf_append(buf, ",\"libcMode\":");
  llvm_append_json_string(buf, z_target_libc_mode(target));
  zbuf_append(buf, ",\"requiresSysroot\":false,\"capabilities\":");
  llvm_append_capabilities_json(buf, target);
  zbuf_append(buf, ",\"fallbackPolicy\":\"none\",\"reason\":\"LLVM textual IR emission is available; native host executable output requires --backend llvm --emit exe on a supported host with clang\"}");
  zbuf_appendf(buf, ",\"moduleCount\":%zu,\"emitKind\":", input ? input->module_count : 0);
  llvm_append_json_string(buf, emit_kind ? emit_kind : "llvm-ir");
  z_append_llvm_backend_lifecycle_field_json(buf);
  zbuf_append(buf, ",\"backendFamily\":\"llvm\"}");
}

void z_append_llvm_native_backend_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const char *emit_kind) {
  bool links_zero_runtime = input && input->direct_runtime_helper_count > 0;
  ZLlvmToolchainPlan plan = z_llvm_toolchain_plan(target);
  zbuf_append(buf, "{\"internalIr\":{\"typeRepresentation\":\"Zero MIR scalar values\",\"controlFlowRepresentation\":\"LLVM textual IR lowered by external clang\",\"callRepresentation\":\"LLVM direct calls\",\"functionIdentity\":\"module-qualified-stable-sorted\",\"debugRepresentation\":\"source spans retained on MIR nodes\"}");
  zbuf_append(buf, ",\"objectEmission\":{\"path\":\"llvm-clang-exe\",\"functions\":true,\"dataSections\":");
  zbuf_append(buf, input && input->direct_readonly_data_bytes > 0 ? "true" : "false");
  zbuf_appendf(buf, ",\"symbols\":true,\"relocations\":\"llvm-toolchain\",\"symbolCount\":%zu,\"internalHelperCount\":0}", input ? input->direct_function_count + input->direct_runtime_helper_count : 0);
  zbuf_append(buf, ",\"linking\":{\"linkerFlavor\":\"clang\",\"objectFormat\":");
  llvm_append_json_string(buf, target && target->object_format ? target->object_format : "unknown");
  zbuf_append(buf, ",\"targetLibraries\":");
  llvm_append_json_string(buf, links_zero_runtime ? "zero-runtime" : "none");
  zbuf_append(buf, ",\"symbolMap\":\"llvm-module\",\"externalToolchain\":");
  llvm_append_json_string(buf, plan.compiler);
  zbuf_append(buf, ",\"toolchainSource\":\"llvm-ir-clang-link-plan\",\"stripArtifacts\":false}");
  zbuf_append(buf, ",\"linkerPlan\":{\"format\":\"llvm-ir-to-native\",\"flavor\":\"clang\",\"archives\":[],\"staticLibraries\":");
  zbuf_append(buf, links_zero_runtime ? "[\"zero_runtime.o\"]" : "[]");
  zbuf_append(buf, ",\"importLibraries\":[],\"systemLibraries\":[],\"rpaths\":[],\"loadPaths\":[],\"visibility\":\"exported-c-and-main-only\",\"crossLinking\":false,\"externalToolchain\":");
  llvm_append_json_string(buf, plan.compiler);
  zbuf_append(buf, ",\"reproducible\":false,\"libcMode\":");
  llvm_append_json_string(buf, target ? z_target_libc_mode(target) : "host-default");
  zbuf_append(buf, ",\"requiresSysroot\":false,\"sysrootStatus\":\"not-required\",\"targetTriple\":");
  llvm_append_json_string(buf, plan.target_triple);
  zbuf_append(buf, "}");
  zbuf_append(buf, ",\"targetFacts\":{\"directAvailable\":false,\"llvmAvailable\":");
  zbuf_append(buf, plan.native_executable ? "true" : "false");
  zbuf_append(buf, ",\"status\":");
  llvm_append_json_string(buf, plan.status);
  zbuf_append(buf, ",\"selectedEmitter\":\"llvm-clang-exe\",\"objectFormat\":");
  llvm_append_json_string(buf, target && target->object_format ? target->object_format : "unknown");
  zbuf_append(buf, ",\"arch\":");
  llvm_append_json_string(buf, target && target->arch ? target->arch : "unknown");
  zbuf_append(buf, ",\"abi\":");
  llvm_append_json_string(buf, target && target->abi ? target->abi : "");
  zbuf_append(buf, ",\"libcMode\":");
  llvm_append_json_string(buf, z_target_libc_mode(target));
  zbuf_append(buf, ",\"requiresSysroot\":false,\"capabilities\":");
  llvm_append_capabilities_json(buf, target);
  zbuf_append(buf, ",\"fallbackPolicy\":\"none\",\"reason\":");
  llvm_append_json_string(buf, plan.reason);
  zbuf_append(buf, ",\"toolchain\":");
  z_append_llvm_toolchain_plan_json(buf, target);
  zbuf_append(buf, "}");
  zbuf_appendf(buf, ",\"moduleCount\":%zu,\"emitKind\":", input ? input->module_count : 0);
  llvm_append_json_string(buf, emit_kind ? emit_kind : "exe");
  z_append_llvm_backend_lifecycle_field_json(buf);
  zbuf_append(buf, ",\"backendFamily\":\"llvm\"}");
}
