#include "manifest_toml.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *toml_skip_ws(const char *cursor) {
  while (*cursor && isspace((unsigned char)*cursor)) cursor++;
  return cursor;
}

static const char *toml_skip_string(const char *cursor) {
  if (*cursor != '"') return NULL;
  cursor++;
  while (*cursor) {
    if (*cursor == '\\' && cursor[1]) {
      cursor += 2;
      continue;
    }
    if (*cursor == '"') return cursor + 1;
    cursor++;
  }
  return NULL;
}

static void toml_manifest_push_c_lib(ZManifest *manifest, ZManifestCLib lib) {
  manifest->c_libs = z_checked_reallocarray(manifest->c_libs, manifest->c_lib_count + 1, sizeof(ZManifestCLib));
  manifest->c_libs[manifest->c_lib_count++] = lib;
}

static void toml_manifest_push_dependency(ZManifest *manifest, ZManifestDependency dep) {
  manifest->dependencies = z_checked_reallocarray(manifest->dependencies, manifest->dependency_count + 1, sizeof(ZManifestDependency));
  manifest->dependencies[manifest->dependency_count++] = dep;
}

static ZManifestDependency *toml_manifest_dependency_named(ZManifest *manifest, const char *name) {
  if (!manifest || !name || !name[0]) return NULL;
  for (size_t i = 0; i < manifest->dependency_count; i++) {
    if (manifest->dependencies[i].name && strcmp(manifest->dependencies[i].name, name) == 0) return &manifest->dependencies[i];
  }
  ZManifestDependency dep = {0};
  dep.name = z_strdup(name ? name : "");
  dep.version = z_strdup("");
  dep.path = z_strdup("");
  dep.targets_json = z_strdup("[]");
  toml_manifest_push_dependency(manifest, dep);
  return &manifest->dependencies[manifest->dependency_count - 1];
}

static ZManifestCLib *toml_manifest_c_lib_named(ZManifest *manifest, const char *name) {
  if (!manifest || !name || !name[0]) return NULL;
  for (size_t i = 0; i < manifest->c_lib_count; i++) {
    if (manifest->c_libs[i].name && strcmp(manifest->c_libs[i].name, name) == 0) return &manifest->c_libs[i];
  }
  ZManifestCLib lib = {0};
  lib.name = z_strdup(name ? name : "");
  lib.headers_json = z_strdup("[]");
  lib.include_json = z_strdup("[]");
  lib.lib_json = z_strdup("[]");
  lib.link_json = z_strdup("[]");
  lib.mode = z_strdup("static");
  lib.pkg_config = z_strdup("");
  toml_manifest_push_c_lib(manifest, lib);
  return &manifest->c_libs[manifest->c_lib_count - 1];
}

static void toml_manifest_replace_string(char **slot, char *value) {
  if (!slot) {
    free(value);
    return;
  }
  free(*slot);
  *slot = value ? value : z_strdup("");
}

static char *toml_trim_copy(const char *start, size_t len) {
  while (len > 0 && isspace((unsigned char)*start)) {
    start++;
    len--;
  }
  while (len > 0 && isspace((unsigned char)start[len - 1])) len--;
  return z_strndup(start, len);
}

static void toml_strip_comment(char *line) {
  bool in_string = false;
  bool escaped = false;
  for (char *cursor = line; cursor && *cursor; cursor++) {
    if (escaped) {
      escaped = false;
      continue;
    }
    if (in_string && *cursor == '\\') {
      escaped = true;
      continue;
    }
    if (*cursor == '"') {
      in_string = !in_string;
      continue;
    }
    if (!in_string && *cursor == '#') {
      *cursor = 0;
      return;
    }
  }
}

static char *toml_parse_string_copy(const char *value) {
  const char *cursor = toml_skip_ws(value ? value : "");
  if (*cursor != '"') return NULL;
  cursor++;
  ZBuf out;
  zbuf_init(&out);
  while (*cursor) {
    if (*cursor == '"') {
      if (!out.data) zbuf_append(&out, "");
      return out.data;
    }
    if (*cursor == '\\' && cursor[1]) {
      cursor++;
      switch (*cursor) {
        case '"': zbuf_append_char(&out, '"'); break;
        case '\\': zbuf_append_char(&out, '\\'); break;
        case 'n': zbuf_append_char(&out, '\n'); break;
        case 'r': zbuf_append_char(&out, '\r'); break;
        case 't': zbuf_append_char(&out, '\t'); break;
        default: zbuf_append_char(&out, *cursor); break;
      }
      cursor++;
      continue;
    }
    zbuf_append_char(&out, *cursor++);
  }
  zbuf_free(&out);
  return NULL;
}

static bool toml_parse_bool(const char *value, bool *out) {
  const char *start = toml_skip_ws(value ? value : "");
  const char *end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) end--;
  if ((size_t)(end - start) == 4 && strncmp(start, "true", 4) == 0) {
    if (out) *out = true;
    return true;
  }
  if ((size_t)(end - start) == 5 && strncmp(start, "false", 5) == 0) {
    if (out) *out = false;
    return true;
  }
  return false;
}

static void toml_append_json_string(ZBuf *buf, const char *text) {
  zbuf_append_char(buf, '"');
  for (const unsigned char *cursor = (const unsigned char *)(text ? text : ""); *cursor; cursor++) {
    switch (*cursor) {
      case '\\': zbuf_append(buf, "\\\\"); break;
      case '"': zbuf_append(buf, "\\\""); break;
      case '\n': zbuf_append(buf, "\\n"); break;
      case '\r': zbuf_append(buf, "\\r"); break;
      case '\t': zbuf_append(buf, "\\t"); break;
      default:
        if (*cursor < 0x20) zbuf_appendf(buf, "\\u%04x", *cursor);
        else zbuf_append_char(buf, (char)*cursor);
        break;
    }
  }
  zbuf_append_char(buf, '"');
}

static char *toml_array_to_json(const char *value) {
  const char *cursor = toml_skip_ws(value ? value : "");
  if (*cursor != '[') return z_strdup("[]");
  cursor++;
  ZBuf out;
  zbuf_init(&out);
  zbuf_append_char(&out, '[');
  bool first = true;
  while (*cursor) {
    cursor = toml_skip_ws(cursor);
    if (*cursor == ']') {
      zbuf_append_char(&out, ']');
      return out.data ? out.data : z_strdup("[]");
    }
    char *item = toml_parse_string_copy(cursor);
    if (!item) break;
    if (!first) zbuf_append_char(&out, ',');
    toml_append_json_string(&out, item);
    first = false;
    free(item);
    cursor = toml_skip_ws(toml_skip_string(cursor));
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == ']') {
      zbuf_append_char(&out, ']');
      return out.data ? out.data : z_strdup("[]");
    }
    break;
  }
  zbuf_free(&out);
  return z_strdup("[]");
}

static char *toml_string_to_json_array(const char *value) {
  char *item = toml_parse_string_copy(value);
  if (!item) return toml_array_to_json(value);
  ZBuf out;
  zbuf_init(&out);
  zbuf_append_char(&out, '[');
  toml_append_json_string(&out, item);
  zbuf_append_char(&out, ']');
  free(item);
  return out.data ? out.data : z_strdup("[]");
}

static char *toml_full_key(const char *table, const char *key) {
  if (!table || !table[0]) return z_strdup(key ? key : "");
  ZBuf out;
  zbuf_init(&out);
  zbuf_append(&out, table);
  zbuf_append_char(&out, '.');
  zbuf_append(&out, key ? key : "");
  return out.data ? out.data : z_strdup("");
}

static bool toml_split_name_field(const char *suffix, char *name, size_t name_len, const char **field) {
  const char *dot = suffix ? strchr(suffix, '.') : NULL;
  if (!dot || dot == suffix) return false;
  size_t len = (size_t)(dot - suffix);
  if (len >= name_len) len = name_len - 1;
  memcpy(name, suffix, len);
  name[len] = 0;
  if (field) *field = dot + 1;
  return true;
}

static bool toml_set_dependency_field(ZManifest *out, const char *suffix, const char *value) {
  char name[128];
  const char *field = NULL;
  if (!toml_split_name_field(suffix, name, sizeof(name), &field)) {
    ZManifestDependency *dep = toml_manifest_dependency_named(out, suffix);
    char *version = dep ? toml_parse_string_copy(value) : NULL;
    if (version) toml_manifest_replace_string(&dep->version, version);
    return dep != NULL;
  }
  ZManifestDependency *dep = toml_manifest_dependency_named(out, name);
  if (!dep) return false;
  if (strcmp(field, "path") == 0) toml_manifest_replace_string(&dep->path, toml_parse_string_copy(value));
  else if (strcmp(field, "version") == 0) toml_manifest_replace_string(&dep->version, toml_parse_string_copy(value));
  else if (strcmp(field, "targets") == 0) toml_manifest_replace_string(&dep->targets_json, toml_array_to_json(value));
  else if (strcmp(field, "target") == 0) toml_manifest_replace_string(&dep->targets_json, toml_string_to_json_array(value));
  return true;
}

static bool toml_set_c_lib_field(ZManifest *out, const char *suffix, const char *value) {
  char name[128];
  const char *field = NULL;
  if (!toml_split_name_field(suffix, name, sizeof(name), &field)) return true;
  ZManifestCLib *lib = toml_manifest_c_lib_named(out, name);
  if (!lib) return false;
  if (strcmp(field, "headers") == 0) toml_manifest_replace_string(&lib->headers_json, toml_array_to_json(value));
  else if (strcmp(field, "include") == 0) toml_manifest_replace_string(&lib->include_json, toml_array_to_json(value));
  else if (strcmp(field, "lib") == 0) toml_manifest_replace_string(&lib->lib_json, toml_array_to_json(value));
  else if (strcmp(field, "link") == 0) toml_manifest_replace_string(&lib->link_json, toml_array_to_json(value));
  else if (strcmp(field, "mode") == 0) toml_manifest_replace_string(&lib->mode, toml_parse_string_copy(value));
  else if (strcmp(field, "pkg_config") == 0 || strcmp(field, "pkgConfig") == 0) toml_manifest_replace_string(&lib->pkg_config, toml_parse_string_copy(value));
  return true;
}

bool z_parse_manifest_toml(const char *manifest, ZManifest *out, ZDiag *diag) {
  memset(out, 0, sizeof(*out));
  char *copy = z_strdup(manifest ? manifest : "");
  char table[256] = {0};
  int line_number = 0;
  char *line = copy;
  while (line) {
    line_number++;
    char *next = strchr(line, '\n');
    if (next) *next++ = 0;
    toml_strip_comment(line);
    char *trimmed = toml_trim_copy(line, strlen(line));
    if (!trimmed[0]) {
      free(trimmed);
      line = next;
      continue;
    }
    if (trimmed[0] == '[') {
      char *end = strchr(trimmed, ']');
      if (!end) {
        if (diag) {
          diag->code = 2002;
          diag->line = line_number;
          diag->column = 1;
          diag->length = 1;
          snprintf(diag->message, sizeof(diag->message), "zero.toml table header is not closed");
          snprintf(diag->expected, sizeof(diag->expected), "[table]");
          snprintf(diag->actual, sizeof(diag->actual), "%s", trimmed);
          snprintf(diag->help, sizeof(diag->help), "close the TOML table header before adding fields");
        }
        free(trimmed);
        free(copy);
        return false;
      }
      char *name = toml_trim_copy(trimmed + 1, (size_t)(end - trimmed - 1));
      snprintf(table, sizeof(table), "%s", name);
      free(name);
      free(trimmed);
      line = next;
      continue;
    }
    char *equals = strchr(trimmed, '=');
    if (!equals) {
      free(trimmed);
      line = next;
      continue;
    }
    char *key = toml_trim_copy(trimmed, (size_t)(equals - trimmed));
    char *value = toml_trim_copy(equals + 1, strlen(equals + 1));
    char *full = toml_full_key(table, key);

    if (strcmp(full, "package.name") == 0) toml_manifest_replace_string(&out->package_name, toml_parse_string_copy(value));
    else if (strcmp(full, "package.version") == 0) toml_manifest_replace_string(&out->package_version, toml_parse_string_copy(value));
    else if (strcmp(full, "targets.cli.main") == 0) toml_manifest_replace_string(&out->main_path, toml_parse_string_copy(value));
    else if (strcmp(full, "targets.cli.graph") == 0) toml_manifest_replace_string(&out->graph_path, toml_parse_string_copy(value));
    else if (strcmp(full, "targets.cli.kind") == 0) toml_manifest_replace_string(&out->kind, toml_parse_string_copy(value));
    else if (strcmp(full, "repositoryGraph.compilerInput") == 0) {
      bool bool_value = false;
      if (!toml_parse_bool(value, &bool_value)) {
        if (diag) {
          diag->code = 2002;
          diag->line = line_number;
          diag->column = 1;
          diag->length = 1;
          snprintf(diag->message, sizeof(diag->message), "repositoryGraph.compilerInput must be a boolean");
          snprintf(diag->expected, sizeof(diag->expected), "true or false");
          snprintf(diag->actual, sizeof(diag->actual), "%s", value);
          snprintf(diag->help, sizeof(diag->help), "set repositoryGraph.compilerInput = true only when zero.graph is checked in");
        }
        free(full);
        free(value);
        free(key);
        free(trimmed);
        free(copy);
        return false;
      }
      out->repository_graph_compiler_input_present = true;
      out->repository_graph_compiler_input = bool_value;
    } else if (strncmp(full, "deps.", 5) == 0) {
      toml_set_dependency_field(out, full + 5, value);
    } else if (strncmp(full, "dependencies.", 13) == 0) {
      toml_set_dependency_field(out, full + 13, value);
    } else if (strncmp(full, "c.libs.", 7) == 0) {
      toml_set_c_lib_field(out, full + 7, value);
    }

    free(full);
    free(value);
    free(key);
    free(trimmed);
    line = next;
  }
  free(copy);
  return true;
}
