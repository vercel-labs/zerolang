#include "zero.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *fallback_manifest =
"[[target]]\n"
"name = \"darwin-arm64\"\n"
"aliases = [\"aarch64-macos\"]\n"
"os = \"macos\"\n"
"arch = \"aarch64\"\n"
"abi = \"darwin\"\n"
"objectFormat = \"macho\"\n"
"linker = \"cc\"\n"
"libc = \"default\"\n"
"libcMode = \"host-default\"\n"
"exeSuffix = \"\"\n"
"zigTarget = \"aarch64-macos\"\n"
"capabilities = [\"memory\", \"stdio\", \"args\", \"env\", \"fs\", \"time\", \"rand\", \"net\", \"proc\"]\n"
"[[target]]\n"
"name = \"darwin-x64\"\n"
"aliases = [\"x86_64-macos\"]\n"
"os = \"macos\"\n"
"arch = \"x86_64\"\n"
"abi = \"darwin\"\n"
"objectFormat = \"macho\"\n"
"linker = \"cc\"\n"
"libc = \"default\"\n"
"libcMode = \"sysroot\"\n"
"exeSuffix = \"\"\n"
"zigTarget = \"x86_64-macos\"\n"
"capabilities = [\"memory\", \"stdio\", \"fs\", \"time\", \"rand\"]\n"
"[[target]]\n"
"name = \"linux-musl-x64\"\n"
"aliases = [\"x86_64-linux-musl\"]\n"
"os = \"linux\"\n"
"arch = \"x86_64\"\n"
"abi = \"musl\"\n"
"objectFormat = \"elf\"\n"
"linker = \"zig cc\"\n"
"libc = \"musl\"\n"
"libcMode = \"bundled-libc\"\n"
"exeSuffix = \"\"\n"
"zigTarget = \"x86_64-linux-musl\"\n"
"capabilities = [\"memory\", \"stdio\", \"args\", \"env\", \"fs\", \"time\", \"rand\"]\n"
"[[target]]\n"
"name = \"linux-musl-arm64\"\n"
"aliases = [\"aarch64-linux-musl\"]\n"
"os = \"linux\"\n"
"arch = \"aarch64\"\n"
"abi = \"musl\"\n"
"objectFormat = \"elf\"\n"
"linker = \"zig cc\"\n"
"libc = \"musl\"\n"
"libcMode = \"bundled-libc\"\n"
"exeSuffix = \"\"\n"
"zigTarget = \"aarch64-linux-musl\"\n"
"capabilities = [\"memory\", \"stdio\", \"time\", \"rand\"]\n"
"[[target]]\n"
"name = \"linux-x64\"\n"
"aliases = [\"x86_64-linux-gnu\"]\n"
"os = \"linux\"\n"
"arch = \"x86_64\"\n"
"abi = \"gnu\"\n"
"objectFormat = \"elf\"\n"
"linker = \"zig cc\"\n"
"libc = \"gnu\"\n"
"libcMode = \"sysroot\"\n"
"exeSuffix = \"\"\n"
"zigTarget = \"x86_64-linux-gnu\"\n"
"capabilities = [\"memory\", \"stdio\", \"time\", \"rand\"]\n"
"[[target]]\n"
"name = \"linux-arm64\"\n"
"aliases = [\"aarch64-linux-gnu\"]\n"
"os = \"linux\"\n"
"arch = \"aarch64\"\n"
"abi = \"gnu\"\n"
"objectFormat = \"elf\"\n"
"linker = \"zig cc\"\n"
"libc = \"gnu\"\n"
"libcMode = \"sysroot\"\n"
"exeSuffix = \"\"\n"
"zigTarget = \"aarch64-linux-gnu\"\n"
"capabilities = [\"memory\", \"stdio\", \"time\", \"rand\"]\n"
"[[target]]\n"
"name = \"win32-x64.exe\"\n"
"aliases = [\"x86_64-windows-msvc\"]\n"
"os = \"windows\"\n"
"arch = \"x86_64\"\n"
"abi = \"msvc\"\n"
"objectFormat = \"coff\"\n"
"linker = \"lld-link\"\n"
"libc = \"msvc\"\n"
"libcMode = \"sysroot\"\n"
"exeSuffix = \".exe\"\n"
"zigTarget = \"x86_64-windows-msvc\"\n"
"capabilities = [\"memory\", \"stdio\", \"time\", \"rand\"]\n"
"[[target]]\n"
"name = \"win32-arm64.exe\"\n"
"aliases = [\"aarch64-windows-msvc\"]\n"
"os = \"windows\"\n"
"arch = \"aarch64\"\n"
"abi = \"msvc\"\n"
"objectFormat = \"coff\"\n"
"linker = \"lld-link\"\n"
"libc = \"msvc\"\n"
"libcMode = \"sysroot\"\n"
"exeSuffix = \".exe\"\n"
"zigTarget = \"aarch64-windows-msvc\"\n"
"capabilities = [\"memory\", \"stdio\", \"time\", \"rand\"]\n"
"[[target]]\n"
"name = \"wasm32-wasi\"\n"
"aliases = []\n"
"os = \"wasi\"\n"
"arch = \"wasm32\"\n"
"abi = \"wasi\"\n"
"objectFormat = \"wasm\"\n"
"linker = \"wasm-ld\"\n"
"libc = \"wasi\"\n"
"libcMode = \"bundled-libc\"\n"
"exeSuffix = \".wasm\"\n"
"zigTarget = \"wasm32-wasi\"\n"
"capabilities = [\"memory\", \"stdio\", \"args\", \"env\", \"fs\", \"time\", \"rand\"]\n"
"[[target]]\n"
"name = \"wasm32-web\"\n"
"aliases = []\n"
"os = \"web\"\n"
"arch = \"wasm32\"\n"
"abi = \"emscripten\"\n"
"objectFormat = \"wasm\"\n"
"linker = \"emcc\"\n"
"libc = \"emscripten\"\n"
"libcMode = \"emscripten\"\n"
"exeSuffix = \".js\"\n"
"zigTarget = \"wasm32-emscripten\"\n"
"capabilities = [\"memory\", \"stdio\", \"env\", \"time\", \"rand\", \"web\"]\n";

static ZTargetInfo *targets = NULL;
static size_t target_count = 0;

static char *trim_copy(const char *start, size_t len) {
  while (len > 0 && isspace((unsigned char)*start)) {
    start++;
    len--;
  }
  while (len > 0 && isspace((unsigned char)start[len - 1])) len--;
  return z_strndup(start, len);
}

static char *manifest_value(const char *line) {
  const char *eq = strchr(line, '=');
  if (!eq) return z_strdup("");
  const char *value = eq + 1;
  while (*value && isspace((unsigned char)*value)) value++;
  size_t len = strlen(value);
  while (len > 0 && (value[len - 1] == '\n' || value[len - 1] == '\r' || isspace((unsigned char)value[len - 1]))) len--;
  if (len >= 2 && value[0] == '"' && value[len - 1] == '"') return z_strndup(value + 1, len - 2);
  if (len >= 2 && value[0] == '[' && value[len - 1] == ']') return trim_copy(value + 1, len - 2);
  return z_strndup(value, len);
}

static void set_target_field(ZTargetInfo *target, const char *line) {
  char *value = manifest_value(line);
  if (strncmp(line, "name", 4) == 0) target->name = value;
  else if (strncmp(line, "aliases", 7) == 0) target->aliases = value;
  else if (strncmp(line, "os", 2) == 0) target->os = value;
  else if (strncmp(line, "arch", 4) == 0) target->arch = value;
  else if (strncmp(line, "abi", 3) == 0) target->abi = value;
  else if (strncmp(line, "libcMode", 8) == 0) target->libc_mode = value;
  else if (strncmp(line, "libc", 4) == 0) target->libc = value;
  else if (strncmp(line, "exeSuffix", 9) == 0) target->exe_suffix = value;
  else if (strncmp(line, "zigTarget", 9) == 0) target->zig_target = value;
  else if (strncmp(line, "objectFormat", 12) == 0) target->object_format = value;
  else if (strncmp(line, "linker", 6) == 0) target->linker = value;
  else if (strncmp(line, "capabilities", 12) == 0) target->capabilities = value;
  else free(value);
}

static void push_target(ZTargetInfo current) {
  if (!current.name || !current.os || !current.arch || !current.zig_target) return;
  if (!current.aliases) current.aliases = z_strdup("");
  if (!current.abi) current.abi = z_strdup("");
  if (!current.libc) current.libc = z_strdup("default");
  if (!current.libc_mode) current.libc_mode = z_strdup("host-default");
  if (!current.exe_suffix) current.exe_suffix = z_strdup("");
  if (!current.object_format) current.object_format = z_strdup("unknown");
  if (!current.linker) current.linker = z_strdup("cc");
  if (!current.capabilities) current.capabilities = z_strdup("\"memory\", \"stdio\"");
  targets = realloc(targets, (target_count + 2) * sizeof(ZTargetInfo));
  targets[target_count++] = current;
  targets[target_count] = (ZTargetInfo){0};
}

static void load_targets_from_text(const char *text) {
  ZTargetInfo current = {0};
  bool in_target = false;
  const char *cursor = text ? text : "";
  while (*cursor) {
    const char *line_end = strchr(cursor, '\n');
    size_t len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);
    char *line = trim_copy(cursor, len);
    if (strcmp(line, "[[target]]") == 0) {
      if (in_target) push_target(current);
      current = (ZTargetInfo){0};
      in_target = true;
    } else if (in_target && line[0] && line[0] != '#') {
      set_target_field(&current, line);
    }
    free(line);
    cursor = line_end ? line_end + 1 : cursor + len;
  }
  if (in_target) push_target(current);
}

static void ensure_targets_loaded(void) {
  if (targets) return;
  ZDiag diag = {0};
  char *text = z_read_file("native/zero-c/targets/targets.manifest", &diag);
  load_targets_from_text(text ? text : fallback_manifest);
  free(text);
}

const char *z_host_target(void) {
#if defined(__APPLE__) && defined(__aarch64__)
  return "darwin-arm64";
#elif defined(__APPLE__) && defined(__x86_64__)
  return "darwin-x64";
#elif defined(__linux__) && defined(__aarch64__)
  return "linux-arm64";
#elif defined(__linux__) && defined(__x86_64__)
  return "linux-x64";
#elif defined(_WIN32) && defined(__aarch64__)
  return "win32-arm64.exe";
#elif defined(_WIN32) && defined(__x86_64__)
  return "win32-x64.exe";
#else
  return "unknown";
#endif
}

size_t z_target_count(void) {
  ensure_targets_loaded();
  return target_count;
}

const ZTargetInfo *z_target_at(size_t index) {
  ensure_targets_loaded();
  return index < target_count ? &targets[index] : NULL;
}

const ZTargetInfo *z_find_target(const char *target) {
  ensure_targets_loaded();
  const char *name = target && strcmp(target, "host") != 0 ? target : z_host_target();
  for (size_t i = 0; i < target_count; i++) {
    if (strcmp(targets[i].name, name) == 0) return &targets[i];
    if (targets[i].aliases && strstr(targets[i].aliases, name)) return &targets[i];
  }
  return NULL;
}

bool z_is_known_target(const char *target) {
  if (!target || strcmp(target, "host") == 0) return true;
  return z_find_target(target) != NULL;
}

bool z_target_is_host(const ZTargetInfo *target) {
  return target && strcmp(target->name, z_host_target()) == 0;
}

static bool host_capability_available(const char *capability) {
  return capability &&
         (strcmp(capability, "args") == 0 ||
          strcmp(capability, "env") == 0 ||
          strcmp(capability, "fs") == 0 ||
          strcmp(capability, "net") == 0 ||
          strcmp(capability, "proc") == 0);
}

bool z_target_has_capability(const ZTargetInfo *target, const char *capability) {
  ensure_targets_loaded();
  if (!target || !capability) return false;
  if (z_target_is_host(target) && host_capability_available(capability)) return true;
  return target->capabilities && strstr(target->capabilities, capability) != NULL;
}

const char *z_target_libc_mode(const ZTargetInfo *target) {
  return target && target->libc_mode ? target->libc_mode : "host-default";
}

bool z_target_requires_sysroot(const ZTargetInfo *target) {
  return target && !z_target_is_host(target) && strcmp(z_target_libc_mode(target), "sysroot") == 0;
}

static bool target_path_is_dir(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static const char *target_sysroot_status(const ZTargetInfo *target) {
  if (!z_target_requires_sysroot(target)) return "not-required";
  const char *path = getenv(z_target_sysroot_env_name(target));
  if (!path || !path[0]) return "missing";
  if (strstr(path, "/usr/include") || strstr(path, "/usr/lib")) return "host-leakage";
  if (!target_path_is_dir(path)) return "missing";
  return "present";
}

const char *z_target_sysroot_env_name(const ZTargetInfo *target) {
  static char name[128];
  const char *source = target && target->zig_target ? target->zig_target : "host";
  snprintf(name, sizeof(name), "ZERO_SYSROOT_");
  size_t offset = strlen(name);
  for (size_t i = 0; source[i] && offset + 1 < sizeof(name); i++) {
    char ch = source[i];
    name[offset++] = (char)(isalnum((unsigned char)ch) ? toupper((unsigned char)ch) : '_');
  }
  name[offset] = 0;
  return name;
}

static void append_target_capabilities_json(ZBuf *buf, const ZTargetInfo *target) {
  const char *capabilities[] = {"memory", "stdio", "args", "env", "fs", "net", "proc", "time", "rand", "web", NULL};
  zbuf_append(buf, "[");
  bool first = true;
  for (int i = 0; capabilities[i]; i++) {
    if (!z_target_has_capability(target, capabilities[i])) continue;
    if (!first) zbuf_append(buf, ", ");
    zbuf_append_char(buf, '"');
    zbuf_append(buf, capabilities[i]);
    zbuf_append_char(buf, '"');
    first = false;
  }
  zbuf_append(buf, "]");
}

static void append_target_capability_facts_json(ZBuf *buf, const ZTargetInfo *target) {
  const char *capabilities[] = {"memory", "stdio", "args", "env", "fs", "net", "proc", "time", "rand", "web", NULL};
  zbuf_append(buf, "[");
  for (int i = 0; capabilities[i]; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_appendf(
      buf,
      "{\"name\":\"%s\",\"available\":%s,\"source\":\"%s\"}",
      capabilities[i],
      z_target_has_capability(target, capabilities[i]) ? "true" : "false",
      z_target_has_capability(target, capabilities[i]) ? "manifest" : "unavailable"
    );
  }
  zbuf_append(buf, "]");
}

static const char *target_libc_mode(const ZTargetInfo *target) {
  return z_target_libc_mode(target);
}

static bool target_uses_emscripten(const ZTargetInfo *target) {
  return target &&
         ((target->linker && (strcmp(target->linker, "emcc") == 0 || strcmp(target->linker, "emscripten") == 0)) ||
          (target->libc_mode && strcmp(target->libc_mode, "emscripten") == 0));
}

const char *z_direct_object_emitter(const ZTargetInfo *target) {
  if (!target) return "none";
  const char *format = target->object_format ? target->object_format : "";
  const char *arch = target->arch ? target->arch : "";
  const char *os = target->os ? target->os : "";
  if (strcmp(format, "wasm") == 0 && strcmp(arch, "wasm32") == 0) return "zero-wasm";
  if (strcmp(format, "elf") == 0 && strcmp(arch, "x86_64") == 0 && strcmp(os, "linux") == 0) return "zero-elf64";
  if (strcmp(format, "elf") == 0 && strcmp(arch, "aarch64") == 0 && strcmp(os, "linux") == 0) return "zero-elf-aarch64";
  if (strcmp(format, "macho") == 0 && strcmp(arch, "aarch64") == 0 && strcmp(os, "macos") == 0) return "zero-macho64";
  if (strcmp(format, "coff") == 0 && strcmp(arch, "x86_64") == 0 && strcmp(os, "windows") == 0) return "zero-coff-x64";
  return "none";
}

const char *z_direct_exe_emitter(const ZTargetInfo *target) {
  if (!target) return "none";
  if (target->name && strcmp(target->name, "linux-x64") == 0) return "zero-elf64-exe";
  if (target->name && strcmp(target->name, "linux-musl-x64") == 0) return "zero-elf64-exe";
  if (target->name && strcmp(target->name, "linux-musl-arm64") == 0) return "zero-elf-aarch64-exe";
  if (target->name && strcmp(target->name, "linux-arm64") == 0) return "zero-elf-aarch64-exe";
  if (target->name && strcmp(target->name, "darwin-arm64") == 0) return "zero-macho64-exe";
  if (target->name && strcmp(target->name, "win32-x64.exe") == 0) return "zero-coff-x64-exe";
  return "none";
}

const char *z_direct_backend_status(const ZTargetInfo *target) {
  if (!target) return "known-unimplemented";
  if (strcmp(z_direct_exe_emitter(target), "none") != 0) return "native-exe";
  if (strcmp(z_direct_object_emitter(target), "zero-wasm") == 0) return "wasm-module";
  if (strcmp(z_direct_object_emitter(target), "none") != 0) return "native-object";
  return "known-unimplemented";
}

const char *z_direct_backend_reason(const ZTargetInfo *target) {
  if (!target) return "unknown target";
  const char *format = target->object_format ? target->object_format : "unknown";
  const char *arch = target->arch ? target->arch : "unknown";
  if (strcmp(z_direct_object_emitter(target), "none") != 0) {
    if (strcmp(z_direct_exe_emitter(target), "none") != 0) return "direct object and executable backend available";
    return "direct object backend available; direct executable linker is not implemented for this target";
  }
  if (strcmp(format, "elf") == 0 && strcmp(arch, "aarch64") == 0) return "AArch64 ELF machine-code backend is not implemented yet";
  if (strcmp(format, "coff") == 0 && strcmp(arch, "aarch64") == 0) return "AArch64 COFF machine-code backend is not implemented yet";
  return "direct backend is not implemented for this target format/architecture pair";
}

static void append_target_toolchain_json(ZBuf *buf, const ZTargetInfo *target) {
  const char *driver = z_target_is_host(target) ? "cc" : (target_uses_emscripten(target) ? "emcc" : "target-capable C compiler");
  const char *linker = target->linker && strcmp(target->linker, "zig cc") == 0 ? "target-cc" : (target->linker ? target->linker : "cc");
  zbuf_appendf(
    buf,
    "{\"cCompiler\":\"%s\",\"crossCompiler\":\"%s\",\"compilerTarget\":\"%s\",\"objectFormat\":\"%s\",\"linker\":\"%s\",\"requiresSysroot\":%s}",
    driver,
    driver,
    target->zig_target,
    target->object_format ? target->object_format : "unknown",
    linker,
    z_target_requires_sysroot(target) ? "true" : "false"
  );
}

static void append_target_direct_backend_json(ZBuf *buf, const ZTargetInfo *target) {
  const char *object_emitter = z_direct_object_emitter(target);
  const char *exe_emitter = z_direct_exe_emitter(target);
  zbuf_appendf(
    buf,
    "{\"status\":\"%s\",\"objectSupported\":%s,\"exeSupported\":%s,\"objectEmitter\":\"%s\",\"exeEmitter\":\"%s\",\"objectFormat\":\"%s\",\"arch\":\"%s\",\"abi\":\"%s\",\"reason\":\"%s\",\"fallback\":\"removed\",\"explicitDirectFallback\":\"never-c-bridge\"}",
    z_direct_backend_status(target),
    strcmp(object_emitter, "none") != 0 ? "true" : "false",
    strcmp(exe_emitter, "none") != 0 ? "true" : "false",
    object_emitter,
    exe_emitter,
    target && target->object_format ? target->object_format : "unknown",
    target && target->arch ? target->arch : "unknown",
    target && target->abi ? target->abi : "",
    z_direct_backend_reason(target)
  );
}

static void append_target_libc_json(ZBuf *buf, const ZTargetInfo *target) {
  zbuf_appendf(
    buf,
    "{\"name\":\"%s\",\"mode\":\"%s\",\"hostReusable\":%s,\"sysrootEnv\":\"%s\",\"sysrootStatus\":\"%s\"}",
    target->libc && target->libc[0] ? target->libc : "default",
    target_libc_mode(target),
    z_target_is_host(target) ? "true" : "false",
    z_target_requires_sysroot(target) ? z_target_sysroot_env_name(target) : "",
    target_sysroot_status(target)
  );
}

void z_append_targets_json(ZBuf *buf) {
  ensure_targets_loaded();
  zbuf_appendf(buf, "{\n  \"schemaVersion\": 1,\n  \"host\": \"%s\",\n  \"targets\": [\n", z_host_target());
  for (size_t i = 0; i < target_count; i++) {
    zbuf_appendf(
      buf,
      "    {\"name\": \"%s\", \"aliases\": [",
      targets[i].name
    );
    if (targets[i].aliases && targets[i].aliases[0]) zbuf_append(buf, targets[i].aliases);
    if (z_target_is_host(&targets[i])) {
      if (targets[i].aliases && targets[i].aliases[0]) zbuf_append(buf, ", ");
      zbuf_append(buf, "\"host\"");
    }
    zbuf_appendf(
      buf,
      "], \"os\": \"%s\", \"arch\": \"%s\", \"abi\": \"%s\", \"objectFormat\": \"%s\", \"linker\": \"%s\", \"libc\": \"%s\", \"exeSuffix\": \"%s\", \"compilerTarget\": \"%s\", \"targetCc\": true, \"hosted\": %s, \"capabilities\": ",
      targets[i].os,
      targets[i].arch,
      targets[i].abi,
      targets[i].object_format,
      targets[i].linker && strcmp(targets[i].linker, "zig cc") == 0 ? "target-cc" : targets[i].linker,
      targets[i].libc,
      targets[i].exe_suffix,
      targets[i].zig_target,
      z_target_is_host(&targets[i]) ? "true" : "false"
    );
    append_target_capabilities_json(buf, &targets[i]);
    zbuf_append(buf, ", \"capabilityFacts\": ");
    append_target_capability_facts_json(buf, &targets[i]);
    zbuf_append(buf, ", \"toolchain\": ");
    append_target_toolchain_json(buf, &targets[i]);
    zbuf_append(buf, ", \"libcFacts\": ");
    append_target_libc_json(buf, &targets[i]);
    zbuf_append(buf, ", \"directBackend\": ");
    append_target_direct_backend_json(buf, &targets[i]);
    zbuf_appendf(buf, "}%s\n", i + 1 < target_count ? "," : "");
  }
  zbuf_append(buf, "  ]\n}\n");
}

void z_append_target_names_json(ZBuf *buf) {
  ensure_targets_loaded();
  zbuf_append(buf, "[");
  for (size_t i = 0; i < target_count; i++) {
    zbuf_appendf(buf, "%s\"%s\"", i == 0 ? "" : ", ", targets[i].name);
  }
  zbuf_append(buf, "]");
}
