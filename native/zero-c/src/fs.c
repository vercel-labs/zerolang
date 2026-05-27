#include "zero.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void z_allocation_fatal(const char *operation) {
  fprintf(stderr, "zero: fatal: out of memory while %s\n", operation ? operation : "allocating memory");
  exit(70);
}

static size_t z_checked_allocation_size(size_t count, size_t item_size, const char *operation) {
  if (count == 0 || item_size == 0) return 1;
  if (count > SIZE_MAX / item_size) z_allocation_fatal(operation);
  return count * item_size;
}

void *z_checked_malloc(size_t size) {
  void *ptr = malloc(size == 0 ? 1 : size);
  if (!ptr) z_allocation_fatal("allocating memory");
  return ptr;
}

void *z_checked_calloc(size_t count, size_t item_size) {
  size_t size = z_checked_allocation_size(count, item_size, "allocating memory");
  void *ptr = calloc(1, size);
  if (!ptr) z_allocation_fatal("allocating memory");
  return ptr;
}

void *z_checked_reallocarray(void *ptr, size_t count, size_t item_size) {
  size_t size = z_checked_allocation_size(count, item_size, "growing memory");
  void *next = realloc(ptr, size);
  if (!next) z_allocation_fatal("growing memory");
  return next;
}

size_t z_grow_capacity(size_t current, size_t required, size_t initial) {
  size_t next = current == 0 ? (initial == 0 ? 1 : initial) : current;
  while (next < required) {
    if (next > SIZE_MAX / 2) {
      next = required;
      break;
    }
    next *= 2;
  }
  return next;
}

void zbuf_init(ZBuf *buf) {
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}

void zbuf_append_char(ZBuf *buf, char ch) {
  if (buf->len > SIZE_MAX - 2) z_allocation_fatal("growing buffer");
  size_t required = buf->len + 2;
  if (required > buf->cap) {
    size_t next = z_grow_capacity(buf->cap, required, 64);
    buf->data = z_checked_reallocarray(buf->data, next, sizeof(char));
    buf->cap = next;
  }
  buf->data[buf->len++] = ch;
  buf->data[buf->len] = 0;
}

void zbuf_append(ZBuf *buf, const char *text) {
  while (*text) zbuf_append_char(buf, *text++);
}

void zbuf_appendf(ZBuf *buf, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  va_list copy;
  va_copy(copy, args);
  int needed = vsnprintf(NULL, 0, fmt, copy);
  va_end(copy);
  if (needed < 0) {
    va_end(args);
    return;
  }
  char *tmp = z_checked_malloc((size_t)needed + 1);
  vsnprintf(tmp, (size_t)needed + 1, fmt, args);
  va_end(args);
  zbuf_append(buf, tmp);
  free(tmp);
}

void zbuf_free(ZBuf *buf) {
  free(buf->data);
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}

char *z_strdup(const char *text) {
  size_t len = strlen(text);
  return z_strndup(text, len);
}

char *z_strndup(const char *text, size_t len) {
  if (len == SIZE_MAX) z_allocation_fatal("copying string");
  char *copy = z_checked_malloc(len + 1);
  memcpy(copy, text, len);
  copy[len] = 0;
  return copy;
}

static void diag_io(ZDiag *diag, const char *path, const char *action) {
  diag->code = 1;
  diag->path = path;
  diag->line = 1;
  diag->column = 1;
  snprintf(diag->message, sizeof(diag->message), "failed to %s '%s': %s", action, path, strerror(errno));
}

static int zero_mkdir(const char *path) {
#if defined(_WIN32)
  return mkdir(path);
#else
  return mkdir(path, 0777);
#endif
}

char *z_read_file(const char *path, ZDiag *diag) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    diag_io(diag, path, "read");
    return NULL;
  }
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  if (size < 0) {
    diag_io(diag, path, "read");
    fclose(file);
    return NULL;
  }
  rewind(file);
  char *data = z_checked_malloc((size_t)size + 1);
  size_t read = fread(data, 1, (size_t)size, file);
  fclose(file);
  data[read] = 0;
  return data;
}

static bool mkdir_parents(const char *path) {
  char *copy = z_strdup(path);
  for (char *cursor = copy + 1; *cursor; cursor++) {
    if (*cursor == '/') {
      *cursor = 0;
      zero_mkdir(copy);
      *cursor = '/';
    }
  }
  free(copy);
  return true;
}

bool z_write_file(const char *path, const char *text, ZDiag *diag) {
  mkdir_parents(path);
  FILE *file = fopen(path, "wb");
  if (!file) {
    diag_io(diag, path, "write");
    return false;
  }
  fputs(text, file);
  fclose(file);
  return true;
}

bool z_write_binary_file(const char *path, const unsigned char *data, size_t len, ZDiag *diag) {
  mkdir_parents(path);
  FILE *file = fopen(path, "wb");
  if (!file) {
    diag_io(diag, path, "write");
    return false;
  }
  if (len > 0 && fwrite(data, 1, len, file) != len) {
    diag_io(diag, path, "write");
    fclose(file);
    return false;
  }
  fclose(file);
  return true;
}

static bool ends_with(const char *text, const char *suffix) {
  size_t text_len = strlen(text);
  size_t suffix_len = strlen(suffix);
  return text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0;
}

static char *dirname_of(const char *path) {
  const char *slash = strrchr(path, '/');
  if (!slash) return z_strdup(".");
  return z_strndup(path, (size_t)(slash - path));
}

static char *join_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left);
  if (left[strlen(left) - 1] != '/') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right);
  return buf.data;
}

static char *normalize_path_text(const char *path) {
  bool absolute = path && path[0] == '/';
  char *copy = z_strdup(path ? path : "");
  char **segments = NULL;
  size_t segment_count = 0;
  char *cursor = copy;
  while (*cursor) {
    while (*cursor == '/') cursor++;
    if (!*cursor) break;
    char *start = cursor;
    while (*cursor && *cursor != '/') cursor++;
    char saved = *cursor;
    *cursor = 0;
    if (strcmp(start, ".") == 0) {
      // skip
    } else if (strcmp(start, "..") == 0) {
      if (segment_count > 0 && strcmp(segments[segment_count - 1], "..") != 0) {
        segment_count--;
      } else if (!absolute) {
        segments = z_checked_reallocarray(segments, segment_count + 1, sizeof(char *));
        segments[segment_count++] = start;
      }
    } else {
      segments = z_checked_reallocarray(segments, segment_count + 1, sizeof(char *));
      segments[segment_count++] = start;
    }
    if (!saved) break;
    cursor++;
  }
  ZBuf out;
  zbuf_init(&out);
  if (absolute) zbuf_append_char(&out, '/');
  for (size_t i = 0; i < segment_count; i++) {
    if ((absolute && out.len > 1) || (!absolute && out.len > 0)) zbuf_append_char(&out, '/');
    zbuf_append(&out, segments[i]);
  }
  if (out.len == 0) zbuf_append(&out, absolute ? "/" : ".");
  free(segments);
  free(copy);
  return out.data;
}

static bool file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static unsigned long long fs_fnv1a_text(const char *text) {
  unsigned long long hash = 1469598103934665603ull;
  for (const unsigned char *cursor = (const unsigned char *)(text ? text : ""); *cursor; cursor++) {
    hash ^= (unsigned long long)*cursor;
    hash *= 1099511628211ull;
  }
  return hash;
}

static unsigned long long fs_mix_hash_text(unsigned long long hash, const char *text) {
  hash ^= fs_fnv1a_text(text ? text : "");
  hash *= 1099511628211ull;
  return hash;
}

static void source_input_push_dependency(SourceInput *input, const ZManifestDependency *dep, const char *resolved_manifest, const char *resolved_name, const char *resolved_version, const char *status, bool direct) {
  input->dependencies = z_checked_reallocarray(input->dependencies, input->dependency_count + 1, sizeof(SourceDependency));
  SourceDependency *item = &input->dependencies[input->dependency_count++];
  memset(item, 0, sizeof(*item));
  item->name = z_strdup(dep && dep->name ? dep->name : "");
  item->version = z_strdup(dep && dep->version ? dep->version : "");
  item->path = z_strdup(dep && dep->path ? dep->path : "");
  item->resolved_manifest = z_strdup(resolved_manifest ? resolved_manifest : "");
  item->resolved_name = z_strdup(resolved_name ? resolved_name : (dep && dep->name ? dep->name : ""));
  item->resolved_version = z_strdup(resolved_version ? resolved_version : (dep && dep->version ? dep->version : ""));
  item->targets_json = z_strdup(dep && dep->targets_json ? dep->targets_json : "[]");
  item->status = z_strdup(status ? status : "ok");
  item->direct = direct;
  unsigned long long hash = fs_fnv1a_text("dependency");
  hash = fs_mix_hash_text(hash, item->name);
  hash = fs_mix_hash_text(hash, item->version);
  hash = fs_mix_hash_text(hash, item->path);
  hash = fs_mix_hash_text(hash, item->resolved_manifest);
  hash = fs_mix_hash_text(hash, item->resolved_name);
  hash = fs_mix_hash_text(hash, item->resolved_version);
  hash = fs_mix_hash_text(hash, item->targets_json);
  item->fingerprint = hash;
}

static const char *source_input_package_version(const SourceInput *input, const char *package_name) {
  if (!package_name || !package_name[0]) return NULL;
  if (input->package_name && strcmp(input->package_name, package_name) == 0) return input->package_version ? input->package_version : "";
  for (size_t i = 0; i < input->dependency_count; i++) {
    if (input->dependencies[i].resolved_name && strcmp(input->dependencies[i].resolved_name, package_name) == 0) {
      return input->dependencies[i].resolved_version ? input->dependencies[i].resolved_version : "";
    }
  }
  return NULL;
}

static const char *json_skip_ws(const char *cursor) {
  while (*cursor && isspace((unsigned char)*cursor)) cursor++;
  return cursor;
}

static const char *json_skip_string(const char *cursor) {
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

static char *json_parse_string_copy(const char *cursor, const char **end_out) {
  if (*cursor != '"') return NULL;
  cursor++;
  ZBuf out;
  zbuf_init(&out);
  while (*cursor) {
    if (*cursor == '"') {
      if (end_out) *end_out = cursor + 1;
      if (!out.data) zbuf_append(&out, "");
      return out.data;
    }
    if (*cursor == '\\' && cursor[1]) {
      cursor++;
      switch (*cursor) {
        case '"': zbuf_append_char(&out, '"'); break;
        case '\\': zbuf_append_char(&out, '\\'); break;
        case '/': zbuf_append_char(&out, '/'); break;
        case 'b': zbuf_append_char(&out, '\b'); break;
        case 'f': zbuf_append_char(&out, '\f'); break;
        case 'n': zbuf_append_char(&out, '\n'); break;
        case 'r': zbuf_append_char(&out, '\r'); break;
        case 't': zbuf_append_char(&out, '\t'); break;
        case 'u':
          for (int i = 0; i < 4 && cursor[1]; i++) cursor++;
          zbuf_append_char(&out, '?');
          break;
        default:
          zbuf_append_char(&out, *cursor);
          break;
      }
      cursor++;
      continue;
    }
    zbuf_append_char(&out, *cursor++);
  }
  zbuf_free(&out);
  return NULL;
}

static const char *json_skip_value(const char *cursor) {
  cursor = json_skip_ws(cursor);
  if (*cursor == '"') return json_skip_string(cursor);
  if (*cursor == '{') {
    cursor++;
    while (*cursor) {
      cursor = json_skip_ws(cursor);
      if (*cursor == '}') return cursor + 1;
      const char *key_end = json_skip_string(cursor);
      if (!key_end) return NULL;
      cursor = json_skip_ws(key_end);
      if (*cursor != ':') return NULL;
      cursor = json_skip_value(cursor + 1);
      if (!cursor) return NULL;
      cursor = json_skip_ws(cursor);
      if (*cursor == ',') {
        cursor++;
        continue;
      }
      if (*cursor == '}') return cursor + 1;
      return NULL;
    }
    return NULL;
  }
  if (*cursor == '[') {
    cursor++;
    while (*cursor) {
      cursor = json_skip_ws(cursor);
      if (*cursor == ']') return cursor + 1;
      cursor = json_skip_value(cursor);
      if (!cursor) return NULL;
      cursor = json_skip_ws(cursor);
      if (*cursor == ',') {
        cursor++;
        continue;
      }
      if (*cursor == ']') return cursor + 1;
      return NULL;
    }
    return NULL;
  }
  if (!*cursor) return NULL;
  while (*cursor && *cursor != ',' && *cursor != '}' && *cursor != ']') cursor++;
  return cursor;
}

static bool json_find_member_span(const char *object, const char *name, const char **value_start, const char **value_end) {
  const char *cursor = json_skip_ws(object);
  if (*cursor != '{') return false;
  cursor++;
  while (*cursor) {
    cursor = json_skip_ws(cursor);
    if (*cursor == '}') return false;
    const char *key_end = NULL;
    char *key = json_parse_string_copy(cursor, &key_end);
    if (!key) return false;
    cursor = json_skip_ws(key_end);
    if (*cursor != ':') {
      free(key);
      return false;
    }
    cursor = json_skip_ws(cursor + 1);
    const char *end = json_skip_value(cursor);
    if (!end) {
      free(key);
      return false;
    }
    bool matched = strcmp(key, name) == 0;
    free(key);
    if (matched) {
      *value_start = cursor;
      *value_end = end;
      return true;
    }
    cursor = json_skip_ws(end);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == '}') return false;
    return false;
  }
  return false;
}

static bool json_get_span_path(const char *json, const char **path, size_t path_len, const char **value_start, const char **value_end) {
  const char *start = json;
  const char *end = NULL;
  for (size_t i = 0; i < path_len; i++) {
    if (!json_find_member_span(start, path[i], &start, &end)) return false;
  }
  *value_start = start;
  *value_end = end;
  return true;
}

static char *json_get_string_path(const char *json, const char **path, size_t path_len) {
  const char *start = NULL;
  const char *end = NULL;
  if (!json_get_span_path(json, path, path_len, &start, &end)) return NULL;
  (void)end;
  return json_parse_string_copy(start, NULL);
}

static char *json_span_copy(const char *start, const char *end) {
  while (end > start && isspace((unsigned char)end[-1])) end--;
  return z_strndup(start, (size_t)(end - start));
}

static char *json_array_from_value_span(const char *start, const char *end) {
  start = json_skip_ws(start);
  if (*start == '[') return json_span_copy(start, end);
  if (*start == '"') {
    char *value = json_span_copy(start, end);
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append_char(&buf, '[');
    zbuf_append(&buf, value);
    zbuf_append_char(&buf, ']');
    free(value);
    return buf.data;
  }
  return z_strdup("[]");
}

static char *json_get_array_path(const char *json, const char **path, size_t path_len) {
  const char *start = NULL;
  const char *end = NULL;
  if (!json_get_span_path(json, path, path_len, &start, &end)) return z_strdup("[]");
  return json_array_from_value_span(start, end);
}

static void manifest_push_c_lib(ZManifest *manifest, ZManifestCLib lib) {
  manifest->c_libs = z_checked_reallocarray(manifest->c_libs, manifest->c_lib_count + 1, sizeof(ZManifestCLib));
  manifest->c_libs[manifest->c_lib_count++] = lib;
}

static void manifest_push_dependency(ZManifest *manifest, ZManifestDependency dep) {
  manifest->dependencies = z_checked_reallocarray(manifest->dependencies, manifest->dependency_count + 1, sizeof(ZManifestDependency));
  manifest->dependencies[manifest->dependency_count++] = dep;
}

static void parse_manifest_dependencies_object(const char *deps, ZManifest *out) {
  const char *cursor = json_skip_ws(deps);
  if (*cursor != '{') return;
  cursor++;
  while (*cursor) {
    cursor = json_skip_ws(cursor);
    if (*cursor == '}') break;
    const char *key_end = NULL;
    char *name = json_parse_string_copy(cursor, &key_end);
    if (!name) break;
    cursor = json_skip_ws(key_end);
    if (*cursor != ':') {
      free(name);
      break;
    }
    cursor = json_skip_ws(cursor + 1);
    const char *value_end = json_skip_value(cursor);
    if (!value_end) {
      free(name);
      break;
    }
    ZManifestDependency dep = {0};
    dep.name = name;
    dep.targets_json = z_strdup("[]");
    if (*cursor == '"') {
      dep.version = json_parse_string_copy(cursor, NULL);
      dep.path = z_strdup("");
    } else if (*cursor == '{') {
      const char *path_path[] = {"path"};
      const char *version_path[] = {"version"};
      const char *targets_path[] = {"targets"};
      const char *target_path[] = {"target"};
      dep.path = json_get_string_path(cursor, path_path, 1);
      if (!dep.path) dep.path = z_strdup("");
      dep.version = json_get_string_path(cursor, version_path, 1);
      if (!dep.version) dep.version = z_strdup("");
      free(dep.targets_json);
      dep.targets_json = json_get_array_path(cursor, targets_path, 1);
      if (!dep.targets_json || strcmp(dep.targets_json, "[]") == 0) {
        free(dep.targets_json);
        dep.targets_json = json_get_array_path(cursor, target_path, 1);
      }
    } else {
      dep.version = z_strdup("");
      dep.path = z_strdup("");
    }
    manifest_push_dependency(out, dep);
    cursor = json_skip_ws(value_end);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == '}') break;
    break;
  }
}

bool z_parse_manifest_json(const char *manifest, ZManifest *out, ZDiag *diag) {
  memset(out, 0, sizeof(*out));
  if (*json_skip_ws(manifest) != '{') {
    if (diag) {
      diag->code = 2002;
      diag->line = 1;
      diag->column = 1;
      snprintf(diag->message, sizeof(diag->message), "zero.json must be a JSON object");
      snprintf(diag->expected, sizeof(diag->expected), "JSON object with targets.cli.main");
      snprintf(diag->help, sizeof(diag->help), "create zero.json with package and targets metadata");
    }
    return false;
  }

  const char *package_name_path[] = {"package", "name"};
  const char *package_version_path[] = {"package", "version"};
  const char *main_path[] = {"targets", "cli", "main"};
  const char *graph_path[] = {"targets", "cli", "graph"};
  const char *kind_path[] = {"targets", "cli", "kind"};
  out->package_name = json_get_string_path(manifest, package_name_path, 2);
  out->package_version = json_get_string_path(manifest, package_version_path, 2);
  out->main_path = json_get_string_path(manifest, main_path, 3);
  out->graph_path = json_get_string_path(manifest, graph_path, 3);
  out->kind = json_get_string_path(manifest, kind_path, 3);

  const char *dependencies_path[] = {"dependencies"};
  const char *deps_alias_path[] = {"deps"};
  const char *deps = NULL;
  const char *deps_end = NULL;
  if (json_get_span_path(manifest, dependencies_path, 1, &deps, &deps_end) ||
      json_get_span_path(manifest, deps_alias_path, 1, &deps, &deps_end)) {
    parse_manifest_dependencies_object(deps, out);
    (void)deps_end;
  }

  const char *libs_path[] = {"c", "libs"};
  const char *libs = NULL;
  const char *libs_end = NULL;
  if (json_get_span_path(manifest, libs_path, 2, &libs, &libs_end)) {
    const char *cursor = json_skip_ws(libs);
    if (*cursor == '{') {
      cursor++;
      while (*cursor) {
        cursor = json_skip_ws(cursor);
        if (*cursor == '}') break;
        const char *key_end = NULL;
        char *name = json_parse_string_copy(cursor, &key_end);
        if (!name) break;
        cursor = json_skip_ws(key_end);
        if (*cursor != ':') {
          free(name);
          break;
        }
        cursor = json_skip_ws(cursor + 1);
        const char *value_end = json_skip_value(cursor);
        if (!value_end) {
          free(name);
          break;
        }
        if (*cursor == '{') {
          const char *headers_path[] = {"headers"};
          const char *include_path[] = {"include"};
          const char *lib_path[] = {"lib"};
          const char *link_path[] = {"link"};
          const char *mode_path[] = {"mode"};
          const char *pkg_config_path[] = {"pkg_config"};
          const char *pkg_config_camel_path[] = {"pkgConfig"};
          ZManifestCLib lib = {0};
          lib.name = name;
          lib.headers_json = json_get_array_path(cursor, headers_path, 1);
          lib.include_json = json_get_array_path(cursor, include_path, 1);
          lib.lib_json = json_get_array_path(cursor, lib_path, 1);
          lib.link_json = json_get_array_path(cursor, link_path, 1);
          lib.mode = json_get_string_path(cursor, mode_path, 1);
          if (!lib.mode) lib.mode = z_strdup("static");
          lib.pkg_config = json_get_string_path(cursor, pkg_config_path, 1);
          if (!lib.pkg_config) lib.pkg_config = json_get_string_path(cursor, pkg_config_camel_path, 1);
          if (!lib.pkg_config) lib.pkg_config = z_strdup("");
          manifest_push_c_lib(out, lib);
        } else {
          free(name);
        }
        cursor = json_skip_ws(value_end);
        if (*cursor == ',') {
          cursor++;
          continue;
        }
        if (*cursor == '}') break;
        break;
      }
    }
    (void)libs_end;
  }

  return true;
}

void z_free_manifest(ZManifest *manifest) {
  free(manifest->package_name);
  free(manifest->package_version);
  free(manifest->main_path);
  free(manifest->graph_path);
  free(manifest->kind);
  for (size_t i = 0; i < manifest->dependency_count; i++) {
    free(manifest->dependencies[i].name);
    free(manifest->dependencies[i].version);
    free(manifest->dependencies[i].path);
    free(manifest->dependencies[i].targets_json);
  }
  for (size_t i = 0; i < manifest->c_lib_count; i++) {
    free(manifest->c_libs[i].name);
    free(manifest->c_libs[i].headers_json);
    free(manifest->c_libs[i].include_json);
    free(manifest->c_libs[i].lib_json);
    free(manifest->c_libs[i].link_json);
    free(manifest->c_libs[i].mode);
    free(manifest->c_libs[i].pkg_config);
  }
  free(manifest->dependencies);
  free(manifest->c_libs);
  memset(manifest, 0, sizeof(*manifest));
}

char *z_manifest_path_for_input(const char *input_path) {
  if (!input_path || !input_path[0]) return NULL;
  if (strcmp(input_path, "zero.json") == 0 || ends_with(input_path, "/zero.json")) return z_strdup(input_path);
  char *manifest_path = join_path(input_path, "zero.json");
  if (file_exists(manifest_path)) return manifest_path;
  free(manifest_path);
  return NULL;
}

static char *manifest_relative_path(const char *manifest_path, const char *relative_path) {
  if (!relative_path || !relative_path[0]) return NULL;
  if (relative_path[0] == '/') return normalize_path_text(relative_path);
  char *root = dirname_of(manifest_path);
  char *joined = join_path(root, relative_path);
  char *normalized = normalize_path_text(joined);
  free(joined);
  free(root);
  return normalized;
}

static void set_manifest_graph_diag(ZDiag *diag, const char *manifest_path, const char *message, const char *expected, const char *actual, const char *help) {
  if (!diag) return;
  diag->code = 2002;
  diag->path = z_strdup(manifest_path ? manifest_path : "");
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "invalid graph target");
  snprintf(diag->expected, sizeof(diag->expected), "%s", expected ? expected : "targets.cli.graph pointing at a derived ProgramGraph artifact");
  snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "invalid targets.cli.graph");
  snprintf(diag->help, sizeof(diag->help), "%s", help ? help : "set targets.cli.graph to a derived ProgramGraph artifact");
}

bool z_resolve_manifest_graph_artifact_path(const char *input_path, char **out_artifact_path, bool *handled, bool require_graph, ZDiag *diag) {
  if (out_artifact_path) *out_artifact_path = NULL;
  if (handled) *handled = false;
  char *manifest_path = z_manifest_path_for_input(input_path);
  if (!manifest_path) return true;

  char *manifest = z_read_file(manifest_path, diag);
  if (!manifest) {
    if (diag) diag->path = z_strdup(manifest_path);
    free(manifest_path);
    return false;
  }

  ZManifest parsed_manifest = {0};
  if (!z_parse_manifest_json(manifest, &parsed_manifest, diag)) {
    if (diag && !diag->path) diag->path = z_strdup(manifest_path);
    free(manifest);
    free(manifest_path);
    return false;
  }

  bool ok = true;
  bool resolved_graph = false;
  char *artifact_path = NULL;
  if (!parsed_manifest.graph_path || !parsed_manifest.graph_path[0]) {
    if (require_graph) {
      set_manifest_graph_diag(diag,
                              manifest_path,
                              "zero.json is missing targets.cli.graph",
                              "targets.cli.graph pointing at a derived ProgramGraph artifact",
                              "missing targets.cli.graph",
                              "run zero graph import --out <module.program-graph> <source>, then set targets.cli.graph");
      ok = false;
    }
  } else {
    artifact_path = manifest_relative_path(manifest_path, parsed_manifest.graph_path);
    if (!artifact_path || !file_exists(artifact_path)) {
      set_manifest_graph_diag(diag,
                              manifest_path,
                              "target ProgramGraph artifact does not exist",
                              artifact_path ? artifact_path : parsed_manifest.graph_path,
                              "missing ProgramGraph artifact",
                              "create the ProgramGraph artifact or update targets.cli.graph");
      ok = false;
    } else {
      resolved_graph = true;
    }
  }

  if (ok && resolved_graph && out_artifact_path) {
    *out_artifact_path = artifact_path;
    artifact_path = NULL;
  }
  if (ok && handled) *handled = resolved_graph;
  free(artifact_path);
  z_free_manifest(&parsed_manifest);
  free(manifest);
  free(manifest_path);
  return ok;
}

static char *dependency_manifest_path(const char *current_manifest_path, const char *dependency_path) {
  if (!dependency_path || !dependency_path[0]) return NULL;
  char *dep_root = NULL;
  if (dependency_path[0] == '/') dep_root = z_strdup(dependency_path);
  else {
    char *base = dirname_of(current_manifest_path);
    dep_root = join_path(base, dependency_path);
    free(base);
  }
  if (ends_with(dep_root, "zero.json")) {
    char *normalized = normalize_path_text(dep_root);
    free(dep_root);
    return normalized;
  }
  char *manifest = join_path(dep_root, "zero.json");
  free(dep_root);
  char *normalized = normalize_path_text(manifest);
  free(manifest);
  return normalized;
}

static bool dependency_stack_contains(char **stack, size_t stack_len, const char *manifest_path) {
  for (size_t i = 0; i < stack_len; i++) {
    if (strcmp(stack[i], manifest_path) == 0) return true;
  }
  return false;
}

static void set_package_diag(ZDiag *diag, int code, const char *path, const char *message, const char *expected, const char *actual, const char *help) {
  diag->code = code;
  diag->path = z_strdup(path ? path : "");
  diag->line = 1;
  diag->column = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "package dependency error");
  snprintf(diag->expected, sizeof(diag->expected), "%s", expected ? expected : "valid package dependency graph");
  snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "invalid package dependency graph");
  snprintf(diag->help, sizeof(diag->help), "%s", help ? help : "repair zero.json dependency metadata");
}

static unsigned long long source_dependency_graph_hash(const SourceInput *input) {
  unsigned long long hash = fs_fnv1a_text("zero-dependency-graph-v1");
  hash = fs_mix_hash_text(hash, input->package_name);
  hash = fs_mix_hash_text(hash, input->package_version);
  hash = fs_mix_hash_text(hash, input->manifest_path);
  for (size_t i = 0; i < input->dependency_count; i++) {
    const SourceDependency *dep = &input->dependencies[i];
    hash ^= dep->fingerprint;
    hash *= 1099511628211ull;
    hash = fs_mix_hash_text(hash, dep->status);
  }
  return hash;
}

static bool resolve_manifest_dependencies(const char *manifest_path, const ZManifest *manifest, SourceInput *out, ZDiag *diag, char ***stack, size_t *stack_len, bool direct);

static bool write_package_lockfile(SourceInput *input) {
  input->dependency_graph_hash = source_dependency_graph_hash(input);
  ZBuf lock;
  zbuf_init(&lock);
  zbuf_append(&lock, "{\n  \"schemaVersion\": 1,\n  \"format\": \"zero-lock-v1\",\n  \"package\": {\"name\": ");
  zbuf_append_char(&lock, '"');
  zbuf_append(&lock, input->package_name ? input->package_name : "");
  zbuf_append(&lock, "\", \"version\": \"");
  zbuf_append(&lock, input->package_version ? input->package_version : "");
  zbuf_append(&lock, "\"},\n  \"dependencyGraphHash\": \"");
  zbuf_appendf(&lock, "%016llx", input->dependency_graph_hash);
  zbuf_append(&lock, "\",\n  \"dependencies\": [");
  for (size_t i = 0; i < input->dependency_count; i++) {
    SourceDependency *dep = &input->dependencies[i];
    if (i > 0) zbuf_append(&lock, ", ");
    zbuf_append(&lock, "{\"name\":\"");
    zbuf_append(&lock, dep->name ? dep->name : "");
    zbuf_append(&lock, "\",\"version\":\"");
    zbuf_append(&lock, dep->version ? dep->version : "");
    zbuf_append(&lock, "\",\"resolvedName\":\"");
    zbuf_append(&lock, dep->resolved_name ? dep->resolved_name : "");
    zbuf_append(&lock, "\",\"resolvedVersion\":\"");
    zbuf_append(&lock, dep->resolved_version ? dep->resolved_version : "");
    zbuf_append(&lock, "\",\"status\":\"");
    zbuf_append(&lock, dep->status ? dep->status : "");
    zbuf_append(&lock, "\",\"fingerprint\":\"");
    zbuf_appendf(&lock, "%016llx", dep->fingerprint);
    zbuf_append(&lock, "\"}");
  }
  zbuf_append(&lock, "]\n}\n");
  input->lockfile_hash = fs_fnv1a_text(lock.data ? lock.data : "");
  char path[256];
  snprintf(path, sizeof(path), ".zero/package-locks/%016llx.lock.json", input->dependency_graph_hash);
  input->lockfile_path = z_strdup(path);
  ZDiag ignored = {0};
  bool ok = z_write_file(input->lockfile_path, lock.data ? lock.data : "", &ignored);
  zbuf_free(&lock);
  return ok;
}

bool z_resolve_package_metadata(const char *manifest_path, const char *manifest, const ZManifest *parsed_manifest, SourceInput *out, ZDiag *diag) {
  if (!manifest_path || !parsed_manifest || !out) return false;
  if (parsed_manifest->kind && strcmp(parsed_manifest->kind, "exe") != 0) {
    diag->code = 2002;
    diag->path = z_strdup(manifest_path);
    diag->line = 1;
    diag->column = 1;
    snprintf(diag->message, sizeof(diag->message), "unsupported target kind '%s'", parsed_manifest->kind);
    snprintf(diag->expected, sizeof(diag->expected), "targets.cli.kind = \"exe\"");
    snprintf(diag->actual, sizeof(diag->actual), "%s", parsed_manifest->kind);
    snprintf(diag->help, sizeof(diag->help), "use an exe target for the native bootstrap compiler");
    return false;
  }
  SourceInput metadata = {0};
  metadata.manifest_path = z_strdup(manifest_path);
  metadata.package_root = dirname_of(manifest_path);
  metadata.package_name = z_strdup(parsed_manifest->package_name ? parsed_manifest->package_name : "");
  metadata.package_version = z_strdup(parsed_manifest->package_version ? parsed_manifest->package_version : "");
  metadata.manifest_hash = fs_fnv1a_text(manifest);

  char **dependency_stack = NULL;
  size_t dependency_stack_len = 0;
  dependency_stack = z_checked_reallocarray(dependency_stack, 1, sizeof(char *));
  dependency_stack[dependency_stack_len++] = z_strdup(manifest_path);
  bool deps_ok = resolve_manifest_dependencies(manifest_path, parsed_manifest, &metadata, diag, &dependency_stack, &dependency_stack_len, true);
  while (dependency_stack_len > 0) free(dependency_stack[--dependency_stack_len]);
  free(dependency_stack);
  if (!deps_ok) {
    z_free_source(&metadata);
    return false;
  }
  write_package_lockfile(&metadata);
  *out = metadata;
  return true;
}

static bool resolve_manifest_dependencies(const char *manifest_path, const ZManifest *manifest, SourceInput *out, ZDiag *diag, char ***stack, size_t *stack_len, bool direct) {
  for (size_t i = 0; i < manifest->dependency_count; i++) {
    const ZManifestDependency *dep = &manifest->dependencies[i];
    if (!dep->path || !dep->path[0]) {
      source_input_push_dependency(out, dep, "", dep->name, dep->version, "registry-reference", direct);
      continue;
    }
    char *dep_manifest_path = dependency_manifest_path(manifest_path, dep->path);
    if (!dep_manifest_path || !file_exists(dep_manifest_path)) {
      set_package_diag(diag, 9001, manifest_path, "package dependency manifest not found", "dependency path containing zero.json", dep->path, "create the dependency package or update the dependency path");
      free(dep_manifest_path);
      return false;
    }
    if (dependency_stack_contains(*stack, *stack_len, dep_manifest_path)) {
      set_package_diag(diag, 9002, manifest_path, "package dependency cycle detected", "acyclic package dependency graph", dep_manifest_path, "move shared code into a third dependency or remove the cycle");
      free(dep_manifest_path);
      return false;
    }
    char *dep_manifest_text = z_read_file(dep_manifest_path, diag);
    if (!dep_manifest_text) {
      free(dep_manifest_path);
      return false;
    }
    ZManifest parsed_dep = {0};
    if (!z_parse_manifest_json(dep_manifest_text, &parsed_dep, diag)) {
      diag->path = dep_manifest_path;
      free(dep_manifest_text);
      return false;
    }
    const char *resolved_name = parsed_dep.package_name ? parsed_dep.package_name : dep->name;
    const char *resolved_version = parsed_dep.package_version ? parsed_dep.package_version : "";
    if (dep->version && dep->version[0] && resolved_version[0] && strcmp(dep->version, resolved_version) != 0) {
      set_package_diag(diag, 9003, manifest_path, "package dependency version mismatch", dep->version, resolved_version, "update the requested dependency version or the dependency package version");
      z_free_manifest(&parsed_dep);
      free(dep_manifest_text);
      free(dep_manifest_path);
      return false;
    }
    const char *seen_version = source_input_package_version(out, resolved_name);
    if (seen_version && resolved_version[0] && strcmp(seen_version, resolved_version) != 0) {
      set_package_diag(diag, 9003, manifest_path, "package dependency version conflict", seen_version, resolved_version, "choose one version of the dependency package for this graph");
      z_free_manifest(&parsed_dep);
      free(dep_manifest_text);
      free(dep_manifest_path);
      return false;
    }
    source_input_push_dependency(out, dep, dep_manifest_path, resolved_name, resolved_version, "path-resolved", direct);
    *stack = z_checked_reallocarray(*stack, *stack_len + 1, sizeof(char *));
    (*stack)[(*stack_len)++] = z_strdup(dep_manifest_path);
    bool ok = resolve_manifest_dependencies(dep_manifest_path, &parsed_dep, out, diag, stack, stack_len, false);
    free((*stack)[--(*stack_len)]);
    z_free_manifest(&parsed_dep);
    free(dep_manifest_text);
    free(dep_manifest_path);
    if (!ok) return false;
  }
  return true;
}

bool z_map_source_diag(const SourceInput *input, ZDiag *diag) {
  if (!input || !diag || diag->line <= 0 || input->source_line_count == 0) return false;
  size_t index = (size_t)diag->line - 1;
  if (index >= input->source_line_count) return false;
  diag->path = input->source_line_paths[index];
  diag->line = input->source_line_numbers[index] > 0 ? input->source_line_numbers[index] : 1;
  for (size_t i = 0; i < diag->borrow_trace_count; i++) {
    ZBorrowTrace *trace = &diag->borrow_traces[i];
    if (trace->binding_line <= 0) continue;
    size_t binding_index = (size_t)trace->binding_line - 1;
    if (binding_index >= input->source_line_count) continue;
    trace->binding_decl_path = input->source_line_paths[binding_index];
    trace->binding_line = input->source_line_numbers[binding_index] > 0 ? input->source_line_numbers[binding_index] : 1;
  }
  return true;
}

void z_free_source(SourceInput *input) {
  free(input->source_file);
  free(input->source);
  free(input->package_root);
  free(input->manifest_path);
  free(input->package_name);
  free(input->package_version);
  free(input->lockfile_path);
  for (size_t i = 0; i < input->source_file_count; i++) free(input->source_files[i]);
  for (size_t i = 0; i < input->source_line_count; i++) free(input->source_line_paths[i]);
  for (size_t i = 0; i < input->import_count; i++) free(input->imports[i]);
  for (size_t i = 0; i < input->module_count; i++) {
    free(input->module_names[i]);
    free(input->module_paths[i]);
  }
  for (size_t i = 0; i < input->import_edge_count; i++) {
    free(input->import_from[i]);
    free(input->import_to[i]);
    free(input->import_paths[i]);
    free(input->import_source_paths[i]);
  }
  for (size_t i = 0; i < input->symbol_count; i++) {
    free(input->symbol_names[i]);
    free(input->symbol_modules[i]);
    free(input->symbol_kinds[i]);
  }
  for (size_t i = 0; i < input->dependency_count; i++) {
    free(input->dependencies[i].name);
    free(input->dependencies[i].version);
    free(input->dependencies[i].path);
    free(input->dependencies[i].resolved_manifest);
    free(input->dependencies[i].resolved_name);
    free(input->dependencies[i].resolved_version);
    free(input->dependencies[i].targets_json);
    free(input->dependencies[i].status);
  }
  free(input->source_files);
  free(input->source_line_paths);
  free(input->source_line_numbers);
  free(input->imports);
  free(input->module_names);
  free(input->module_paths);
  free(input->import_from);
  free(input->import_to);
  free(input->import_paths);
  free(input->import_source_paths);
  free(input->import_lines);
  free(input->import_columns);
  free(input->import_lengths);
  free(input->symbol_names);
  free(input->symbol_modules);
  free(input->symbol_kinds);
  free(input->dependencies);
  free(input->symbol_public);
}

char *z_default_out_path(const char *source_file) {
  const char *slash = strrchr(source_file, '/');
  const char *base = slash ? slash + 1 : source_file;
  const char *dot = strrchr(base, '.');
  size_t len = dot ? (size_t)(dot - base) : strlen(base);
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, ".zero/out/");
  for (size_t i = 0; i < len; i++) zbuf_append_char(&buf, base[i]);
  return buf.data;
}

static bool command_exists(const char *command) {
  ZBuf probe;
  zbuf_init(&probe);
  zbuf_appendf(&probe, "command -v '%s' >/dev/null 2>&1", command);
  bool ok = system(probe.data) == 0;
  zbuf_free(&probe);
  return ok;
}

static bool dir_exists_for_cc(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool profile_should_strip_artifact(const char *profile);

static const char *sysroot_status_for(const ZTargetInfo *target, const char *env_name, const char *sysroot) {
  if (!z_target_requires_sysroot(target)) return "not-required";
  if (!env_name || !env_name[0] || !sysroot || !sysroot[0]) return "missing";
  if (strstr(sysroot, "/usr/include") || strstr(sysroot, "/usr/lib")) return "host-leakage";
  if (!dir_exists_for_cc(sysroot)) return "missing";
  return "present";
}

ZToolchainPlan z_plan_toolchain(const char *cc, const char *profile, const ZTargetInfo *target) {
  const char *cli_override = cc && cc[0] ? cc : NULL;
  const char *env_override = getenv("ZERO_CC");
  if (env_override && !env_override[0]) env_override = NULL;
  bool host_target = !target || z_target_is_host(target);
  const char *sysroot_env = target && z_target_requires_sysroot(target) ? z_target_sysroot_env_name(target) : "";
  const char *sysroot = sysroot_env && sysroot_env[0] ? getenv(sysroot_env) : NULL;
  const char *sysroot_status = sysroot_status_for(target, sysroot_env, sysroot);
  ZToolchainPlan plan = {
    .driver_kind = "host-cc",
    .selection_source = "host-default",
    .compiler = "cc",
    .target_triple = target && target->zig_target ? target->zig_target : z_host_target(),
    .linker_flavor = target && target->linker ? target->linker : "cc",
    .libc_mode = target ? z_target_libc_mode(target) : "host-default",
    .sysroot_env = sysroot_env ? sysroot_env : "",
    .sysroot_path = sysroot ? sysroot : "",
    .sysroot_status = sysroot_status,
    .requires_sysroot = z_target_requires_sysroot(target),
    .uses_target_flag = false,
    .uses_zig_cache = false,
    .strip_artifact = profile_should_strip_artifact(profile) && host_target
  };

  if (cli_override) {
    plan.driver_kind = "override-cc";
    plan.selection_source = "cli";
    plan.compiler = cli_override;
    return plan;
  }

  if (env_override) {
    plan.driver_kind = "override-cc";
    plan.selection_source = "env";
    plan.compiler = env_override;
    return plan;
  }

  if (!host_target) {
    plan.driver_kind = "zig-cc";
    plan.selection_source = "target-manifest";
    plan.compiler = "zig cc";
    plan.uses_target_flag = true;
    plan.uses_zig_cache = true;
    plan.strip_artifact = false;
  }

  return plan;
}

static bool validate_toolchain_plan(const ZToolchainPlan *plan, const ZTargetInfo *target) {
  if (strcmp(plan->driver_kind, "override-cc") == 0) return true;

  if (plan->requires_sysroot && strcmp(plan->sysroot_status, "present") != 0) {
    if (strcmp(plan->sysroot_status, "host-leakage") == 0) {
      fprintf(stderr, "target '%s' sysroot points at host headers/libs; refusing host header leakage from %s\n", target->name, plan->sysroot_path);
    } else if (plan->sysroot_path && plan->sysroot_path[0]) {
      fprintf(stderr, "target '%s' sysroot does not exist: %s\n", target->name, plan->sysroot_path);
    } else {
      fprintf(stderr, "target '%s' requires sysroot mode; set %s to the target SDK/sysroot\n", target->name, plan->sysroot_env);
    }
    return false;
  }

  if (strcmp(plan->driver_kind, "zig-cc") == 0 && !command_exists("zig")) {
    fprintf(stderr, "cross target '%s' requires a target-capable C compiler; pass --cc/ZERO_CC or install the bundled-toolchain default\n", target->name);
    return false;
  }

  if (strcmp(plan->driver_kind, "host-cc") == 0 && !command_exists("cc")) {
    fprintf(stderr, "host target requires cc; install a native C compiler or pass --cc/ZERO_CC\n");
    return false;
  }

  return true;
}

static const char *profile_c_flags(const char *profile) {
  if (!profile || strcmp(profile, "release") == 0 || strcmp(profile, "release-small") == 0 || strcmp(profile, "small") == 0) return "-Os -DNDEBUG";
  if (strcmp(profile, "tiny") == 0) return "-Os -DNDEBUG";
  if (strcmp(profile, "release-fast") == 0 || strcmp(profile, "fast") == 0) return "-O2 -DNDEBUG";
  if (strcmp(profile, "debug") == 0 || strcmp(profile, "dev") == 0) return "-O0 -g -DZERO_DEBUG";
  if (strcmp(profile, "audit") == 0) return "-O0 -g -DZERO_AUDIT";
  return "-Os -DNDEBUG";
}

static bool profile_should_strip_artifact(const char *profile) {
  return !profile || strcmp(profile, "release") == 0 || strcmp(profile, "release-small") == 0 || strcmp(profile, "small") == 0 || strcmp(profile, "tiny") == 0;
}

static void append_toolchain_driver_command(ZBuf *cmd, const ZToolchainPlan *plan) {
  if (strcmp(plan->driver_kind, "override-cc") == 0) {
    zbuf_appendf(cmd, "'%s'", plan->compiler);
  } else if (strcmp(plan->driver_kind, "host-cc") == 0) {
    zbuf_append(cmd, "cc");
  } else {
    zbuf_append(cmd, "mkdir -p .zero/zig-global-cache .zero/zig-local-cache && ZIG_GLOBAL_CACHE_DIR=.zero/zig-global-cache ZIG_LOCAL_CACHE_DIR=.zero/zig-local-cache zig cc");
    zbuf_appendf(cmd, " -target '%s'", plan->target_triple);
  }
}

bool z_toolchain_compile_c_object(const ZToolchainPlan *plan, const char *profile, const ZTargetInfo *target, const char *c_file, const char *object_file, const char *include_dir, const char *extra_c_flags) {
  if (!validate_toolchain_plan(plan, target)) return false;

  ZBuf cmd;
  zbuf_init(&cmd);
  append_toolchain_driver_command(&cmd, plan);
  zbuf_appendf(&cmd, " %s", profile_c_flags(profile));
  if (extra_c_flags && extra_c_flags[0]) zbuf_appendf(&cmd, " %s", extra_c_flags);
  if (include_dir && include_dir[0]) zbuf_appendf(&cmd, " -I '%s'", include_dir);
  zbuf_appendf(&cmd, " -c '%s' -o '%s'", c_file, object_file);
  bool ok = system(cmd.data) == 0;
  zbuf_free(&cmd);
  return ok;
}

bool z_toolchain_link_objects(const ZToolchainPlan *plan, const ZTargetInfo *target, const char *const *object_files, size_t object_count, const char *exe_file, const char *pre_link_flags, const char *post_object_flags) {
  if (!validate_toolchain_plan(plan, target)) return false;

  ZBuf cmd;
  zbuf_init(&cmd);
  append_toolchain_driver_command(&cmd, plan);
  if (pre_link_flags && pre_link_flags[0]) zbuf_appendf(&cmd, " %s", pre_link_flags);
  for (size_t i = 0; i < object_count; i++) {
    if (object_files[i] && object_files[i][0]) zbuf_appendf(&cmd, " '%s'", object_files[i]);
  }
  zbuf_appendf(&cmd, " -o '%s'", exe_file);
  if (post_object_flags && post_object_flags[0]) zbuf_appendf(&cmd, " %s", post_object_flags);
  bool ok = system(cmd.data) == 0;
  zbuf_free(&cmd);
  return ok;
}

bool z_run_cc(const char *c_file, const char *exe_file, const char *cc, const char *profile, const ZTargetInfo *target) {
  ZToolchainPlan plan = z_plan_toolchain(cc, profile, target);
  if (!validate_toolchain_plan(&plan, target)) return false;

  ZBuf cmd;
  zbuf_init(&cmd);
  append_toolchain_driver_command(&cmd, &plan);
  zbuf_appendf(&cmd, " %s '%s' -o '%s'", profile_c_flags(profile), c_file, exe_file);
  bool ok = system(cmd.data) == 0;
  zbuf_free(&cmd);
  if (!ok) {
    fprintf(
      stderr,
      "toolchain '%s' failed for target '%s' (%s selected by %s)\n",
      plan.compiler,
      target && target->name ? target->name : z_host_target(),
      plan.driver_kind,
      plan.selection_source
    );
  }
  if (ok && plan.strip_artifact && command_exists("strip")) {
    ZBuf strip_cmd;
    zbuf_init(&strip_cmd);
    zbuf_appendf(&strip_cmd, "strip '%s' >/dev/null 2>&1 || true", exe_file);
    system(strip_cmd.data);
    zbuf_free(&strip_cmd);
  }
  return ok;
}
