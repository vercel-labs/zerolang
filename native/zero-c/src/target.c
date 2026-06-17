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
"capabilities = [\"memory\", \"stdio\", \"args\", \"env\", \"fs\", \"time\", \"rand\", \"proc\"]\n"
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
"capabilities = [\"memory\", \"stdio\", \"time\", \"rand\"]\n";

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

static bool target_text_equals(const char *left, const char *right) {
  return left && right && strcmp(left, right) == 0;
}

static bool manifest_key_equals(const char *line, const char *key) {
  if (!line || !key) return false;
  while (*key && *line == *key) {
    line++;
    key++;
  }
  if (*key) return false;
  while (*line && isspace((unsigned char)*line)) line++;
  return *line == '=';
}

static bool manifest_list_token_equals(const char *start, const char *end, const char *token) {
  size_t token_len = token ? strlen(token) : 0;
  return token_len > 0 && start && end && start <= end && (size_t)(end - start) == token_len && memcmp(start, token, token_len) == 0;
}

static bool manifest_list_contains_token(const char *list, const char *token) {
  if (!list || !token || !token[0]) return false;
  const char *cursor = list;
  while (*cursor) {
    while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ',')) cursor++;
    const char *start = cursor;
    const char *end = cursor;
    if (*cursor == '"') {
      start = ++cursor;
      while (*cursor && *cursor != '"') cursor++;
      end = cursor;
      if (*cursor == '"') cursor++;
    } else {
      while (*cursor && *cursor != ',') cursor++;
      end = cursor;
      while (end > start && isspace((unsigned char)end[-1])) end--;
    }
    if (manifest_list_token_equals(start, end, token)) return true;
    while (*cursor && *cursor != ',') cursor++;
  }
  return false;
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
  struct {
    const char *key;
    const char **slot;
  } fields[] = {
    {"name", &target->name},
    {"aliases", &target->aliases},
    {"os", &target->os},
    {"arch", &target->arch},
    {"abi", &target->abi},
    {"libc", &target->libc},
    {"libcMode", &target->libc_mode},
    {"exeSuffix", &target->exe_suffix},
    {"zigTarget", &target->zig_target},
    {"objectFormat", &target->object_format},
    {"linker", &target->linker},
    {"capabilities", &target->capabilities},
  };
  for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
    if (!manifest_key_equals(line, fields[i].key)) continue;
    *fields[i].slot = value;
    return;
  }
  free(value);
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
  targets = z_checked_reallocarray(targets, target_count + 2, sizeof(ZTargetInfo));
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
    if (target_text_equals(line, "[[target]]")) {
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
  const char *name = target && !target_text_equals(target, "host") ? target : z_host_target();
  for (size_t i = 0; i < target_count; i++) {
    if (target_text_equals(targets[i].name, name)) return &targets[i];
    if (manifest_list_contains_token(targets[i].aliases, name)) return &targets[i];
  }
  return NULL;
}

bool z_is_known_target(const char *target) {
  if (!target || target_text_equals(target, "host")) return true;
  return z_find_target(target) != NULL;
}

bool z_target_is_host(const ZTargetInfo *target) {
  return target_text_equals(target ? target->name : NULL, z_host_target());
}

static bool host_capability_available(const char *capability) {
  const char *host_capabilities[] = {"args", "env", "fs", "net", "proc", NULL};
  for (int i = 0; host_capabilities[i]; i++) {
    if (target_text_equals(capability, host_capabilities[i])) return true;
  }
  return false;
}

bool z_target_has_capability(const ZTargetInfo *target, const char *capability) {
  ensure_targets_loaded();
  if (!target || !capability) return false;
  if (z_target_is_host(target) && host_capability_available(capability)) return true;
  return manifest_list_contains_token(target->capabilities, capability);
}

const char *z_target_libc_mode(const ZTargetInfo *target) {
  return target && target->libc_mode ? target->libc_mode : "host-default";
}

bool z_target_requires_sysroot(const ZTargetInfo *target) {
  return target && !z_target_is_host(target) && target_text_equals(z_target_libc_mode(target), "sysroot");
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

static const char *target_linker_label(const ZTargetInfo *target) {
  const char *linker = target && target->linker ? target->linker : "cc";
  return target_text_equals(linker, "zig cc") ? "target-cc" : linker;
}

static const char *target_libc_mode(const ZTargetInfo *target) {
  return z_target_libc_mode(target);
}

static void append_target_toolchain_json(ZBuf *buf, const ZTargetInfo *target) {
  const char *driver = z_target_is_host(target) ? "cc" : "target-capable C compiler";
  zbuf_appendf(
    buf,
    "{\"cCompiler\":\"%s\",\"crossCompiler\":\"%s\",\"compilerTarget\":\"%s\",\"objectFormat\":\"%s\",\"linker\":\"%s\",\"requiresSysroot\":%s}",
    driver,
    driver,
    target->zig_target,
    target->object_format ? target->object_format : "unknown",
    target_linker_label(target),
    z_target_requires_sysroot(target) ? "true" : "false"
  );
}

static void append_target_direct_backend_json(ZBuf *buf, const ZTargetInfo *target) {
  ZDirectBackend object_backend = z_direct_object_backend(target);
  ZDirectBackend exe_backend = z_direct_exe_backend(target);
  const char *object_emitter = z_direct_backend_object_emitter(object_backend);
  const char *exe_emitter = z_direct_backend_exe_emitter(exe_backend);
  zbuf_appendf(
    buf,
    "{\"status\":\"%s\",\"objectSupported\":%s,\"exeSupported\":%s,\"objectEmitter\":\"%s\",\"exeEmitter\":\"%s\",\"objectFormat\":\"%s\",\"arch\":\"%s\",\"abi\":\"%s\",\"reason\":\"%s\",\"fallback\":\"removed\",\"explicitDirectFallback\":\"never-c-bridge\"}",
    z_direct_backend_status(target),
    object_backend != Z_DIRECT_BACKEND_NONE ? "true" : "false",
    exe_backend != Z_DIRECT_BACKEND_NONE ? "true" : "false",
    object_emitter,
    exe_emitter,
    target && target->object_format ? target->object_format : "unknown",
    target && target->arch ? target->arch : "unknown",
    target && target->abi ? target->abi : "",
    z_direct_backend_reason(target)
  );
}

static bool target_http_runtime_supported(const ZTargetInfo *target) {
  ZDirectBackend object_backend = z_direct_object_backend(target);
  return target &&
         z_target_is_host(target) &&
         z_target_has_capability(target, "net") &&
         z_direct_backend_supports_runtime_object(object_backend) &&
         z_direct_exe_backend(target) != Z_DIRECT_BACKEND_NONE &&
         (target_text_equals(target->os, "macos") ||
          target_text_equals(target->os, "linux"));
}

void z_append_http_runtime_json(ZBuf *buf, const ZTargetInfo *target) {
  if (target_http_runtime_supported(target)) {
    zbuf_append(buf, "{\"status\":\"supported\",\"provider\":\"curl\",\"providerLink\":\"system-library\",\"tlsBoundary\":\"platform-or-c-library\",\"caSource\":\"provider-default\",\"tlsVerification\":true,\"customCa\":{\"supported\":true,\"mode\":\"test-harness\",\"env\":\"ZERO_HTTP_TEST_CA_BUNDLE\"},\"insecureMode\":false,\"protocols\":[\"http\",\"https\"],\"staticLibraries\":[\"zero_runtime.o\",\"zero_http_curl.o\"],\"systemLibraries\":[\"curl\"],\"reason\":\"host direct runtime link plan uses libcurl with verification enabled\"}");
    return;
  }
  ZDirectBackend object_backend = z_direct_object_backend(target);
  const char *reason = "target has no audited HTTP runtime provider";
  if (target && !z_target_has_capability(target, "net")) reason = "target lacks net capability";
  else if (target && !z_target_is_host(target)) reason = "HTTP runtime provider is host-only today";
  else if (target && !z_direct_backend_supports_runtime_object(object_backend)) reason = "target lacks host runtime relocation support";
  zbuf_appendf(
    buf,
    "{\"status\":\"unsupported\",\"provider\":null,\"providerLink\":\"none\",\"tlsBoundary\":\"none\",\"caSource\":\"none\",\"tlsVerification\":false,\"customCa\":{\"supported\":false,\"mode\":\"none\",\"env\":\"\"},\"insecureMode\":false,\"protocols\":[],\"staticLibraries\":[],\"systemLibraries\":[],\"reason\":\"%s\"}",
    reason
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
      target_linker_label(&targets[i]),
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
    zbuf_append(buf, ", \"backendFamilies\": {\"default\": \"direct\", \"known\": [\"direct\", \"llvm\"], \"available\": [\"direct\", \"llvm\"], \"fallbackPolicy\": \"none\", \"llvm\": ");
    z_append_llvm_target_backend_json(buf, &targets[i]);
    zbuf_append(buf, "}");
    zbuf_append(buf, ", \"httpRuntime\": ");
    z_append_http_runtime_json(buf, &targets[i]);
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
