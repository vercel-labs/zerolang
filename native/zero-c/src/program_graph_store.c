#include "program_graph_store.h"

#include "program_graph_compare.h"
#include "program_graph_format.h"
#include "program_graph_reconcile_apply.h"
#include "program_graph_store_binary.h"
#include "program_graph_store_prune.h"
#include "program_graph_store_tables.h"
#include "program_graph_view.h"
#include "std_source.h"
#include "zero.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

typedef struct {
  char *id;
  char *hash;
} StoreNodeHash;

typedef struct {
  StoreNodeHash *items;
  size_t len;
  size_t cap;
} StoreNodeHashVec;

static bool store_path_is_dir(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool z_program_graph_store_file_exists(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

const char *z_program_graph_store_format_name(ZProgramGraphStoreFormat format) {
  switch (format) {
    case Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY: return "binary";
    case Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT:
    default: return "text";
  }
}

bool z_program_graph_store_format_from_name(const char *name, ZProgramGraphStoreFormat *out) {
  if (strcmp(name ? name : "", "text") == 0) {
    if (out) *out = Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT;
    return true;
  }
  if (strcmp(name ? name : "", "binary") == 0) {
    if (out) *out = Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY;
    return true;
  }
  return false;
}

static bool store_ends_with(const char *text, const char *suffix) {
  size_t text_len = text ? strlen(text) : 0;
  size_t suffix_len = suffix ? strlen(suffix) : 0;
  return text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0;
}

static char *store_dirname(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  if (!slash) return z_strdup(".");
  if (slash == path) return z_strdup("/");
  return z_strndup(path, (size_t)(slash - path));
}

static char *store_join_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left && left[0] ? left : ".");
  if (buf.len > 0 && buf.data[buf.len - 1] != '/') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right ? right : "");
  return buf.data;
}

static int store_text_cmp(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "");
}

static bool store_text_eq(const char *left, const char *right) { return store_text_cmp(left, right) == 0; }

static const char *store_basename(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  return slash ? slash + 1 : (path ? path : "");
}

static char *store_normalize_path_text(const char *path) {
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
    if (store_text_eq(start, ".")) {
    } else if (store_text_eq(start, "..")) {
      if (segment_count > 0 && !store_text_eq(segments[segment_count - 1], "..")) {
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

bool z_program_graph_store_source_path_is_local(const char *path) {
  if (!path || !path[0] || path[0] == '/' || strchr(path, '\\')) return false;
  if (path[1] == ':' &&
      ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))) {
    return false;
  }
  char *normalized = store_normalize_path_text(path);
  bool ok = normalized && normalized[0] &&
            !store_text_eq(normalized, ".") &&
            !store_text_eq(normalized, "..") &&
            strncmp(normalized, "../", 3) != 0 &&
            store_text_eq(normalized, path);
  free(normalized);
  return ok;
}

static bool store_path_relative_to_root(const char *root, const char *path, const char **relative) {
  if (!root || !path || !relative) return false;
  size_t root_len = strlen(root);
  if (root_len == 0) return false;
  if (store_text_eq(root, path)) {
    *relative = ".";
    return true;
  }
  if (store_text_eq(root, ".")) {
    if (path[0] == '/') return false;
    if (path[0] == '.' && path[1] == '/') *relative = path + 2;
    else *relative = path;
    return true;
  }
  if (strncmp(path, root, root_len) == 0 && path[root_len] == '/') {
    *relative = path + root_len + 1;
    return true;
  }
  return false;
}

static char *store_absolute_path_text(const char *path) {
  if (!path || !path[0]) return NULL;
  char *normalized = store_normalize_path_text(path);
  if (normalized[0] == '/') return normalized;
#if defined(_WIN32)
  return normalized;
#else
  char cwd[4096];
  if (!getcwd(cwd, sizeof(cwd))) return normalized;
  char *joined = store_join_path(cwd, normalized);
  free(normalized);
  char *absolute = store_normalize_path_text(joined);
  free(joined);
  return absolute;
#endif
}

static char *store_source_path_for_root(const char *root, const char *path) {
  if (!path || !path[0]) return z_strdup("");
  char *real_root = store_absolute_path_text(root && root[0] ? root : ".");
  char *real_path = store_absolute_path_text(path);
  const char *relative = NULL;
  if (real_root && real_path && store_path_relative_to_root(real_root, real_path, &relative)) {
    char *normalized = store_normalize_path_text(relative);
    free(real_root);
    free(real_path);
    return normalized;
  }
  free(real_root);
  free(real_path);

  char *normalized_root = store_normalize_path_text(root && root[0] ? root : ".");
  char *normalized_path = store_normalize_path_text(path);
  if (store_path_relative_to_root(normalized_root, normalized_path, &relative)) {
    char *result = store_normalize_path_text(relative);
    free(normalized_root);
    free(normalized_path);
    return result;
  }
  free(normalized_root);
  return normalized_path;
}

static bool store_same_existing_dir(const char *left, const char *right) {
  struct stat left_stat;
  struct stat right_stat;
  return left && right &&
         stat(left, &left_stat) == 0 && stat(right, &right_stat) == 0 &&
         S_ISDIR(left_stat.st_mode) && S_ISDIR(right_stat.st_mode) &&
         left_stat.st_dev == right_stat.st_dev && left_stat.st_ino == right_stat.st_ino;
}

static bool store_relative_dotdot_chain(const char *path) {
  if (!path || !path[0] || path[0] == '/') return false;
  const char *cursor = path;
  while (*cursor) {
    if (cursor[0] != '.' || cursor[1] != '.') return false;
    cursor += 2;
    if (!*cursor) return true;
    if (*cursor != '/') return false;
    cursor++;
  }
  return false;
}

static char *store_parent_dir(const char *path) {
  if (!path || !path[0] || (path[0] == '/' && path[1] == 0)) return NULL;
  if (path[0] == '.' && path[1] == 0) {
    if (store_same_existing_dir(".", "..")) return NULL;
    return z_strdup("..");
  }
  if (store_relative_dotdot_chain(path)) {
    char *parent = store_join_path(path, "..");
    if (store_same_existing_dir(path, parent)) {
      free(parent);
      return NULL;
    }
    return parent;
  }
  char *parent = store_dirname(path);
  if (!parent || strcmp(parent, path) == 0) {
    free(parent);
    return NULL;
  }
  return parent;
}

char *z_program_graph_store_root_for_input(const char *input) {
  if (!input || !input[0]) return z_strdup(".");
  char *cursor = NULL;
  if (store_ends_with(input, "zero.toml") || store_ends_with(input, "zero.json")) cursor = store_dirname(input);
  else if (store_path_is_dir(input)) cursor = z_strdup(input);
  else cursor = store_dirname(input);
  char *fallback = cursor && cursor[0] ? z_strdup(cursor) : z_strdup(".");

  while (cursor && cursor[0]) {
    char *manifest = store_join_path(cursor, "zero.toml");
    bool found = z_program_graph_store_file_exists(manifest);
    free(manifest);
    if (!found) {
      manifest = store_join_path(cursor, "zero.json");
      found = z_program_graph_store_file_exists(manifest);
      free(manifest);
    }
    if (found) {
      free(fallback);
      return cursor;
    }
    char *parent = store_parent_dir(cursor);
    if (!parent) break;
    free(cursor);
    cursor = parent;
  }
  free(cursor);
  return fallback;
}

char *z_program_graph_store_path_for_root(const char *root) {
  return store_join_path(root && root[0] ? root : ".", "zero.graph");
}

void z_program_graph_store_init(ZProgramGraphStore *store) {
  if (store) {
    *store = (ZProgramGraphStore){
      .format = Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT,
      .schema_version = 1,
    };
  }
}

void z_program_graph_store_free(ZProgramGraphStore *store) {
  if (!store) return;
  free(store->root);
  free(store->path);
  for (size_t i = 0; i < store->source_path_len; i++) free(store->source_paths[i]);
  free(store->source_paths);
  for (size_t i = 0; i < store->projection_len; i++) {
    free(store->projection_paths[i]);
    free(store->projection_texts[i]);
  }
  free(store->projection_paths);
  free(store->projection_texts);
  z_program_graph_free(&store->graph);
  *store = (ZProgramGraphStore){0};
}

static bool store_diag(ZDiag *diag, const char *path, size_t line, const char *message, const char *actual) {
  if (diag) {
    *diag = (ZDiag){0};
    diag->code = 1002;
    diag->path = path;
    diag->line = (int)(line ? line : 1);
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "invalid repository graph store");
    snprintf(diag->expected, sizeof(diag->expected), "valid zero.graph repository graph store");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "invalid store");
  }
  return false;
}

static bool store_read_file_bytes(const char *path, unsigned char **out, size_t *out_len, ZDiag *diag) {
  *out = NULL;
  *out_len = 0;
  FILE *file = fopen(path, "rb");
  if (!file) {
    if (diag) {
      *diag = (ZDiag){0};
      diag->code = 1;
      diag->path = path;
      diag->line = 1;
      diag->column = 1;
      snprintf(diag->message, sizeof(diag->message), "failed to read '%s'", path ? path : "zero.graph");
    }
    return false;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return store_diag(diag, path, 1, "failed to read repository graph store", "seek failed");
  }
  long size = ftell(file);
  if (size < 0) {
    fclose(file);
    return store_diag(diag, path, 1, "failed to read repository graph store", "size unavailable");
  }
  rewind(file);
  unsigned char *data = z_checked_malloc((size_t)size + 1);
  size_t read = fread(data, 1, (size_t)size, file);
  fclose(file);
  if (read != (size_t)size) {
    free(data);
    return store_diag(diag, path, 1, "failed to read repository graph store", "short read");
  }
  data[read] = 0;
  *out = data;
  *out_len = read;
  return true;
}

bool z_program_graph_store_path_is_binary(const char *path) {
  unsigned char *data = NULL;
  size_t len = 0;
  ZDiag diag = {0};
  bool ok = store_read_file_bytes(path, &data, &len, &diag);
  bool binary = ok && z_program_graph_store_bytes_are_binary(data, len);
  free(data);
  return binary;
}

static void store_append_quoted(ZBuf *buf, const char *text) {
  zbuf_append_char(buf, '"');
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
    switch (*p) {
      case '\\': zbuf_append(buf, "\\\\"); break;
      case '"': zbuf_append(buf, "\\\""); break;
      case '\n': zbuf_append(buf, "\\n"); break;
      case '\r': zbuf_append(buf, "\\r"); break;
      case '\t': zbuf_append(buf, "\\t"); break;
      default:
        if (*p < 0x20) zbuf_appendf(buf, "\\u%04x", *p);
        else zbuf_append_char(buf, (char)*p);
        break;
    }
  }
  zbuf_append_char(buf, '"');
}

static bool store_read_projection_text(const char *root, const char *source_path, char **out) {
  *out = NULL;
  if (!z_program_graph_store_source_path_is_local(source_path)) return false;
  char *joined = store_join_path(root && root[0] ? root : ".", source_path);
  ZDiag diag = {0};
  *out = z_read_file(joined, &diag);
  free(joined);
  return *out != NULL;
}

static bool store_next_line(const char **cursor, char **out) {
  const char *start = cursor ? *cursor : NULL;
  if (!start || !*start) return false;
  const char *end = start;
  while (*end && *end != '\n') end++;
  size_t len = (size_t)(end - start);
  if (len > 0 && start[len - 1] == '\r') len--;
  *out = z_strndup(start, len);
  *cursor = *end == '\n' ? end + 1 : end;
  return true;
}

static bool store_parse_literal(const char **cursor, const char *literal) {
  size_t len = strlen(literal);
  if (strncmp(*cursor, literal, len) != 0) return false;
  *cursor += len;
  return true;
}

static int store_hex_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

static bool store_parse_quoted(const char **cursor, char **out) {
  if (**cursor != '"') return false;
  (*cursor)++;
  ZBuf buf;
  zbuf_init(&buf);
  while (**cursor && **cursor != '"') {
    unsigned char ch = (unsigned char)**cursor;
    if (ch != '\\') {
      zbuf_append_char(&buf, (char)ch);
      (*cursor)++;
      continue;
    }
    (*cursor)++;
    switch (**cursor) {
      case '\\': zbuf_append_char(&buf, '\\'); (*cursor)++; break;
      case '"': zbuf_append_char(&buf, '"'); (*cursor)++; break;
      case 'n': zbuf_append_char(&buf, '\n'); (*cursor)++; break;
      case 'r': zbuf_append_char(&buf, '\r'); (*cursor)++; break;
      case 't': zbuf_append_char(&buf, '\t'); (*cursor)++; break;
      case 'u': {
        if ((*cursor)[1] != '0' || (*cursor)[2] != '0' || !(*cursor)[3] || !(*cursor)[4]) {
          zbuf_free(&buf);
          return false;
        }
        int high = store_hex_value((*cursor)[3]);
        int low = store_hex_value((*cursor)[4]);
        if (high < 0 || low < 0) {
          zbuf_free(&buf);
          return false;
        }
        zbuf_append_char(&buf, (char)((high << 4) | low));
        *cursor += 5;
        break;
      }
      default:
        zbuf_free(&buf);
        return false;
    }
  }
  if (**cursor != '"') {
    zbuf_free(&buf);
    return false;
  }
  (*cursor)++;
  *out = buf.data ? buf.data : z_strdup("");
  return true;
}

static bool store_parse_quoted_field(const char *line, const char *prefix, char **out) {
  const char *cursor = line;
  return store_parse_literal(&cursor, prefix) && store_parse_quoted(&cursor, out) && *cursor == 0;
}

static bool store_add_source_path(ZProgramGraphStore *store, const char *path) {
  if (!store || !path || !path[0]) return true;
  if (!z_program_graph_store_source_path_is_local(path)) return false;
  for (size_t i = 0; i < store->source_path_len; i++) {
    if (store_text_eq(store->source_paths[i], path)) return true;
  }
  if (store->source_path_len == store->source_path_cap) {
    size_t next = z_grow_capacity(store->source_path_cap, store->source_path_len + 1, 8);
    store->source_paths = z_checked_reallocarray(store->source_paths, next, sizeof(char *));
    store->source_path_cap = next;
  }
  store->source_paths[store->source_path_len++] = z_strdup(path);
  return true;
}

static bool store_add_projection(ZProgramGraphStore *store, const char *path, const char *text) {
  if (!store || !path || !path[0] || !text) return true;
  if (!z_program_graph_store_source_path_is_local(path)) return false;
  for (size_t i = 0; i < store->projection_len; i++) {
    if (store_text_eq(store->projection_paths[i], path)) return false;
  }
  if (store->projection_len == store->projection_cap) {
    size_t next = z_grow_capacity(store->projection_cap, store->projection_len + 1, 8);
    store->projection_paths = z_checked_reallocarray(store->projection_paths, next, sizeof(char *));
    store->projection_texts = z_checked_reallocarray(store->projection_texts, next, sizeof(char *));
    store->projection_cap = next;
  }
  store->projection_paths[store->projection_len] = z_strdup(path);
  store->projection_texts[store->projection_len] = z_strdup(text);
  store->projection_len++;
  return true;
}

static bool store_has_projection_path(const ZProgramGraphStore *store, const char *path) {
  for (size_t i = 0; store && path && i < store->projection_len; i++) {
    if (store_text_eq(store->projection_paths[i], path)) return true;
  }
  return false;
}

static void store_sort_source_paths(ZProgramGraphStore *store) {
  for (size_t i = 1; store && i < store->source_path_len; i++) {
    char *item = store->source_paths[i];
    size_t cursor = i;
    while (cursor > 0 && store_text_cmp(item, store->source_paths[cursor - 1]) < 0) {
      store->source_paths[cursor] = store->source_paths[cursor - 1];
      cursor--;
    }
    store->source_paths[cursor] = item;
  }
}

static void store_sort_projections(ZProgramGraphStore *store) {
  for (size_t i = 1; store && i < store->projection_len; i++) {
    char *path = store->projection_paths[i];
    char *text = store->projection_texts[i];
    size_t cursor = i;
    while (cursor > 0 && store_text_cmp(path, store->projection_paths[cursor - 1]) < 0) {
      store->projection_paths[cursor] = store->projection_paths[cursor - 1];
      store->projection_texts[cursor] = store->projection_texts[cursor - 1];
      cursor--;
    }
    store->projection_paths[cursor] = path;
    store->projection_texts[cursor] = text;
  }
}

static const ZStdSourceModule *store_std_source_module_for_path(const char *path) {
  for (size_t i = 0; path && i < z_std_source_module_count(); i++) {
    const ZStdSourceModule *module = z_std_source_module_at(i);
    if (module &&
        (store_text_eq(module->path, path) ||
         store_text_eq(store_basename(module->path), store_basename(path)))) return module;
  }
  return NULL;
}

static bool store_graph_source_path_is_embedded_std(const ZProgramGraph *graph, const char *path) {
  const ZStdSourceModule *module = store_std_source_module_for_path(path);
  if (!graph || !module) return false;
  const char *short_name = strrchr(module->module ? module->module : "", '.');
  short_name = short_name ? short_name + 1 : module->module;
  bool has_module_node = false;
  for (size_t i = 0; i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!store_text_eq(node->path, path)) continue;
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE &&
        (store_text_eq(node->name, module->module) || store_text_eq(node->name, short_name))) {
      has_module_node = true;
    }
  }
  return has_module_node;
}

static void store_collect_source_paths(ZProgramGraphStore *store, const ZProgramGraph *graph) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    store_add_source_path(store, graph->nodes[i].path);
  }
  store_sort_source_paths(store);
}

static void store_collect_owned_source_paths(ZProgramGraphStore *store, const ZProgramGraph *graph, const ZProgramGraphStore *projections) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const char *path = graph->nodes[i].path;
    if (store_graph_source_path_is_embedded_std(graph, path) && !store_has_projection_path(projections, path)) continue;
    store_add_source_path(store, path);
  }
  store_sort_source_paths(store);
}

static void store_collect_source_projections(ZProgramGraphStore *store, const char *root) {
  for (size_t i = 0; store && i < store->source_path_len; i++) {
    char *text = NULL;
    if (store_read_projection_text(root, store->source_paths[i], &text)) store_add_projection(store, store->source_paths[i], text);
    free(text);
  }
  store_sort_projections(store);
}

static bool store_collect_generated_source_projections(ZProgramGraphStore *store, const ZProgramGraph *graph, ZDiag *diag) {
  for (size_t i = 0; store && i < store->source_path_len; i++) {
    const char *path = store->source_paths[i];
    if (store_graph_source_path_is_embedded_std(graph, path)) continue;
    ZBuf source;
    zbuf_init(&source);
    bool ok = z_program_graph_append_source_view(&source, graph, path, diag);
    if (ok) ok = store_add_projection(store, path, source.data ? source.data : "");
    zbuf_free(&source);
    if (!ok) return false;
  }
  store_sort_projections(store);
  return true;
}

static bool store_source_path_present(const ZProgramGraphStore *store, const char *path) {
  if (!path || !path[0]) return true;
  for (size_t i = 0; store && i < store->source_path_len; i++) {
    if (store_text_eq(store->source_paths[i], path)) return true;
  }
  return false;
}

const char *z_program_graph_store_projection_text(const ZProgramGraphStore *store, const char *path) {
  for (size_t i = 0; store && path && i < store->projection_len; i++) {
    if (store_text_eq(store->projection_paths[i], path)) return store->projection_texts[i];
  }
  return NULL;
}

static bool store_source_paths_match_graph(const ZProgramGraphStore *store, const ZProgramGraph *source_graph) {
  if (!store || !source_graph) return false;
  ZProgramGraphStore current;
  z_program_graph_store_init(&current);
  store_collect_owned_source_paths(&current, source_graph, store);
  bool ok = store->source_path_len == current.source_path_len;
  for (size_t i = 0; ok && i < current.source_path_len; i++) {
    ok = store_source_path_present(store, current.source_paths[i]);
  }
  for (size_t i = 0; ok && i < store->source_path_len; i++) {
    ok = store_source_path_present(&current, store->source_paths[i]);
  }
  z_program_graph_store_free(&current);
  return ok;
}

static char *store_nullable_strdup(const char *text) {
  return text ? z_strdup(text) : NULL;
}

static void store_copy_graph(const ZProgramGraph *src, ZProgramGraph *dst) {
  z_program_graph_init(dst);
  if (!src) return;
  dst->schema_version = src->schema_version;
  dst->validation_state = src->validation_state;
  dst->canonical_source = src->canonical_source;
  dst->next_id = src->next_id;
  free(dst->module_identity);
  dst->module_identity = z_strdup(src->module_identity ? src->module_identity : "");
  free(dst->graph_hash);
  dst->graph_hash = z_strdup(src->graph_hash ? src->graph_hash : "");
  if (src->node_len > 0) {
    dst->nodes = z_checked_calloc(src->node_len, sizeof(ZProgramGraphNode));
    dst->node_len = src->node_len;
    dst->node_cap = src->node_len;
    for (size_t i = 0; i < src->node_len; i++) {
      const ZProgramGraphNode *from = &src->nodes[i];
      ZProgramGraphNode *to = &dst->nodes[i];
      to->id = store_nullable_strdup(from->id);
      to->kind = from->kind;
      to->name = store_nullable_strdup(from->name);
      to->type = store_nullable_strdup(from->type);
      to->value = store_nullable_strdup(from->value);
      to->path = store_nullable_strdup(from->path);
      to->symbol_id = store_nullable_strdup(from->symbol_id);
      to->type_id = store_nullable_strdup(from->type_id);
      to->effect_id = store_nullable_strdup(from->effect_id);
      to->node_hash = store_nullable_strdup(from->node_hash);
      to->line = from->line;
      to->column = from->column;
      to->is_public = from->is_public;
      to->is_mutable = from->is_mutable;
      to->is_static = from->is_static;
      to->fallible = from->fallible;
      to->export_c = from->export_c;
    }
  }
  if (src->edge_len > 0) {
    dst->edges = z_checked_calloc(src->edge_len, sizeof(ZProgramGraphEdge));
    dst->edge_len = src->edge_len;
    dst->edge_cap = src->edge_len;
    for (size_t i = 0; i < src->edge_len; i++) {
      const ZProgramGraphEdge *from = &src->edges[i];
      ZProgramGraphEdge *to = &dst->edges[i];
      to->from = store_nullable_strdup(from->from);
      to->to = store_nullable_strdup(from->to);
      to->kind = store_nullable_strdup(from->kind);
      to->target = from->target;
      to->order = from->order;
    }
  }
}

static void store_normalize_graph_paths_for_root(ZProgramGraph *graph, const char *root) {
  if (!graph) return;
  for (size_t i = 0; i < graph->node_len; i++) {
    char *normalized = store_source_path_for_root(root, graph->nodes[i].path);
    free(graph->nodes[i].path);
    graph->nodes[i].path = normalized;
  }
  z_program_graph_prune_embedded_std_source_nodes(graph);
  z_program_graph_assign_source_node_ids(graph);
  z_program_graph_finalize_identities(graph);
}

static void store_normalize_graph_paths_preserving_ids_for_root(ZProgramGraph *graph, const char *root) {
  if (!graph) return;
  for (size_t i = 0; i < graph->node_len; i++) {
    char *normalized = store_source_path_for_root(root, graph->nodes[i].path);
    free(graph->nodes[i].path);
    graph->nodes[i].path = normalized;
  }
  z_program_graph_finalize_identities(graph);
}

static bool store_normalized_source_graph(const char *root, const ZProgramGraph *source_graph, ZProgramGraph *out) {
  if (!source_graph || !out) return false;
  store_copy_graph(source_graph, out); store_normalize_graph_paths_for_root(out, root && root[0] ? root : ".");
  return true;
}

static bool store_identity_reconcile_diag(const char *path, const ZProgramGraphIdentityReconcile *identity, ZDiag *diag) {
  bool module_identity_changed = identity && identity->module_identity_changed;
  const char *message = module_identity_changed ? "repository graph source identity has a different module identity" : "repository graph source identity is ambiguous";
  const char *expected = module_identity_changed && identity && identity->node_id[0] ? identity->node_id : "unambiguous ProgramGraph source identity preservation";
  const char *actual = module_identity_changed && identity && identity->candidate_id[0] ? identity->candidate_id : (identity && identity->node_id[0] ? identity->node_id : (identity && identity->candidate_id[0] ? identity->candidate_id : "ambiguous source identity"));
  if (diag) {
    *diag = (ZDiag){0};
    diag->code = 1002;
    diag->path = path ? path : "zero.graph";
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message);
    snprintf(diag->expected, sizeof(diag->expected), "%s", expected);
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual);
  }
  return false;
}

static bool store_preserve_existing_source_identities(const char *path, ZProgramGraph *normalized, ZDiag *diag) {
  if (!path || !z_program_graph_store_file_exists(path)) return true;
  ZProgramGraphStore existing;
  ZDiag load_diag = {0};
  if (!z_program_graph_store_load_path(path, &existing, &load_diag)) return true;
  ZProgramGraphIdentityReconcile identity = {0};
  bool ok = z_program_graph_preserve_source_node_ids(&existing.graph, normalized, &identity);
  z_program_graph_store_free(&existing);
  if (ok) return true;
  return store_identity_reconcile_diag(path, &identity, diag);
}

static void store_node_hash_vec_free(StoreNodeHashVec *hashes) {
  if (!hashes) return;
  for (size_t i = 0; i < hashes->len; i++) {
    free(hashes->items[i].id);
    free(hashes->items[i].hash);
  }
  free(hashes->items);
  *hashes = (StoreNodeHashVec){0};
}

static bool store_node_hash_vec_add(StoreNodeHashVec *hashes, char *id, char *hash) {
  for (size_t i = 0; hashes && i < hashes->len; i++) {
    if (store_text_eq(hashes->items[i].id, id)) return false;
  }
  if (hashes->len == hashes->cap) {
    size_t next = z_grow_capacity(hashes->cap, hashes->len + 1, 32);
    hashes->items = z_checked_reallocarray(hashes->items, next, sizeof(StoreNodeHash));
    hashes->cap = next;
  }
  hashes->items[hashes->len++] = (StoreNodeHash){.id = id, .hash = hash};
  return true;
}

static const char *store_node_hash_find(const StoreNodeHashVec *hashes, const char *id) {
  for (size_t i = 0; hashes && i < hashes->len; i++) {
    if (store_text_eq(hashes->items[i].id, id)) return hashes->items[i].hash;
  }
  return NULL;
}

static bool store_parse_node_hash_line(const char *line, StoreNodeHashVec *hashes) {
  const char *cursor = line;
  char *id = NULL;
  char *hash = NULL;
  bool ok = store_parse_literal(&cursor, "nodeHash node:") &&
            store_parse_quoted(&cursor, &id) &&
            store_parse_literal(&cursor, " hash:") &&
            store_parse_quoted(&cursor, &hash) &&
            *cursor == 0 &&
            z_program_graph_node_id_valid(id) &&
            hash[0] != 0 &&
            store_node_hash_vec_add(hashes, id, hash);
  if (!ok) {
    free(id);
    free(hash);
  }
  return ok;
}

static bool store_parse_source_line(const char *line, ZProgramGraphStore *store) {
  const char *cursor = line;
  char *path = NULL;
  bool ok = store_parse_literal(&cursor, "source path:") &&
            store_parse_quoted(&cursor, &path) &&
            *cursor == 0 &&
            path[0] != 0 &&
            store_add_source_path(store, path);
  free(path);
  return ok;
}

static bool store_parse_projection_line(const char *line, ZProgramGraphStore *store) {
  const char *cursor = line;
  char *path = NULL;
  char *text = NULL;
  bool ok = store_parse_literal(&cursor, "projection path:") &&
            store_parse_quoted(&cursor, &path) &&
            store_parse_literal(&cursor, " text:") &&
            store_parse_quoted(&cursor, &text) &&
            *cursor == 0 &&
            path[0] != 0 &&
            store_add_projection(store, path, text ? text : "");
  free(path);
  free(text);
  return ok;
}

static bool store_verify_graph_metadata_common(const char *path, ZProgramGraphStore *store, const char *module_identity, const char *graph_hash, const char *module_hash, const StoreNodeHashVec *node_hashes, ZDiag *diag) {
  if (!store_text_eq(module_identity, store->graph.module_identity)) return store_diag(diag, path, 1, "repository graph module identity does not match stored graph", store->graph.module_identity);
  if (!store_text_eq(graph_hash, store->graph.graph_hash)) return store_diag(diag, path, 1, "repository graph hash does not match stored graph", store->graph.graph_hash);
  if (!store_text_eq(module_hash, store->graph.graph_hash)) return store_diag(diag, path, 1, "repository graph module hash does not match stored graph", store->graph.graph_hash);
  if (node_hashes->len != store->graph.node_len) return store_diag(diag, path, 1, "repository graph node hash table has the wrong size", "node hash count mismatch");
  for (size_t i = 0; i < store->graph.node_len; i++) {
    const ZProgramGraphNode *node = &store->graph.nodes[i];
    const char *expected_hash = store_node_hash_find(node_hashes, node->id);
    if (!expected_hash) return store_diag(diag, path, 1, "repository graph store is missing a node hash", node->id);
    if (!store_text_eq(expected_hash, node->node_hash)) return store_diag(diag, path, 1, "repository graph node hash does not match graph content", node->id);
    if (!store_source_path_present(store, node->path) && !store_graph_source_path_is_embedded_std(&store->graph, node->path)) {
      return store_diag(diag, path, 1, "repository graph source table is missing a node source path", node->path);
    }
  }
  bool requires_projection = false;
  for (size_t i = 0; i < store->source_path_len; i++) {
    if (store_graph_source_path_is_embedded_std(&store->graph, store->source_paths[i])) continue;
    requires_projection = true;
    if (!z_program_graph_store_projection_text(store, store->source_paths[i])) return store_diag(diag, path, 1, "repository graph projection table is missing a source path", store->source_paths[i]);
  }
  if (requires_projection && store->projection_len == 0) return store_diag(diag, path, 1, "repository graph store has no source projections", "empty projection table");
  for (size_t i = 0; i < store->projection_len; i++) {
    if (!store_source_path_present(store, store->projection_paths[i])) return store_diag(diag, path, 1, "repository graph projection table references an unknown source path", store->projection_paths[i]);
  }
  return true;
}

static bool store_verify_metadata(const char *path, ZProgramGraphStore *store, const char *module_identity, const char *graph_hash, const char *module_hash, const char *compiler_store, const char *compiler_tables, const char *compiler_hash_inputs, const StoreNodeHashVec *node_hashes, ZDiag *diag) {
  const char *actual = NULL;
  if (!z_program_graph_store_compiler_metadata_matches(&store->graph, store->source_path_len, store->projection_len, compiler_store, compiler_tables, compiler_hash_inputs, &actual)) {
    return store_diag(diag, path, 1, "repository graph compiler store metadata does not match stored graph", actual);
  }
  return store_verify_graph_metadata_common(path, store, module_identity, graph_hash, module_hash, node_hashes, diag);
}

static bool store_parse_text(const char *path, const char *text, ZProgramGraphStore *out, ZDiag *diag) {
  z_program_graph_store_init(out);
  out->path = z_strdup(path ? path : "zero.graph");
  out->root = store_dirname(out->path);
  out->present = true;

  const char *cursor = text;
  char *line = NULL;
  size_t line_no = 0;
  char *module_identity = NULL;
  char *graph_hash = NULL;
  char *module_hash = NULL;
  char *source_projection = NULL;
  char *compiler_store = NULL;
  char *compiler_tables = NULL;
  char *compiler_hash_inputs = NULL;
  StoreNodeHashVec node_hashes = {0};

  if (!store_next_line(&cursor, &line)) goto invalid;
  line_no++;
  if (!store_text_eq(line, "zero-repository-graph v1")) goto invalid;
  free(line);
  line = NULL;

  while (store_next_line(&cursor, &line)) {
    line_no++;
    if (line[0] == 0) {
      free(line);
      line = NULL;
      continue;
    }
    if (store_text_eq(line, "graph")) {
      free(line);
      line = NULL;
      break;
    }
    char *value = NULL;
    if (store_parse_quoted_field(line, "sourceProjection ", &value)) {
      if (source_projection || !store_text_eq(value, ".0")) {
        free(value);
        goto invalid;
      }
      source_projection = value;
    } else if (store_parse_quoted_field(line, "moduleIdentity ", &value)) {
      if (module_identity || !value[0]) {
        free(value);
        goto invalid;
      }
      module_identity = value;
    } else if (store_parse_quoted_field(line, "graphHash ", &value)) {
      if (graph_hash || !value[0]) {
        free(value);
        goto invalid;
      }
      graph_hash = value;
    } else if (store_parse_quoted_field(line, "moduleHash ", &value)) {
      if (module_hash || !value[0]) {
        free(value);
        goto invalid;
      }
      module_hash = value;
    } else if (strncmp(line, "compilerStore ", strlen("compilerStore ")) == 0) {
      if (compiler_store) goto invalid;
      compiler_store = z_strdup(line);
    } else if (strncmp(line, "compilerTables ", strlen("compilerTables ")) == 0) {
      if (compiler_tables) goto invalid;
      compiler_tables = z_strdup(line);
    } else if (strncmp(line, "compilerHashInputs ", strlen("compilerHashInputs ")) == 0) {
      if (compiler_hash_inputs) goto invalid;
      compiler_hash_inputs = z_strdup(line);
    } else if (strncmp(line, "source ", strlen("source ")) == 0) {
      if (!store_parse_source_line(line, out)) goto invalid;
    } else if (strncmp(line, "projection ", strlen("projection ")) == 0) {
      if (!store_parse_projection_line(line, out)) goto invalid;
    } else if (strncmp(line, "nodeHash ", strlen("nodeHash ")) == 0) {
      if (!store_parse_node_hash_line(line, &node_hashes)) goto invalid;
    } else {
      goto invalid;
    }
    free(line);
    line = NULL;
  }

  if (!source_projection || !module_identity || !graph_hash || !module_hash || !compiler_store || !compiler_tables || !compiler_hash_inputs || !*cursor) goto invalid;
  if (!z_program_graph_parse_dump(cursor, &out->graph, diag)) goto fail;
  store_sort_projections(out);
  ZProgramGraphValidation validation = {0};
  if (!z_program_graph_validate(&out->graph, &validation)) {
    store_diag(diag, path, 1, "repository graph store failed graph validation", validation.code);
    goto fail;
  }
  if (!store_verify_metadata(path, out, module_identity, graph_hash, module_hash, compiler_store, compiler_tables, compiler_hash_inputs, &node_hashes, diag)) goto fail;

  free(source_projection);
  free(module_identity);
  free(graph_hash);
  free(module_hash);
  free(compiler_store);
  free(compiler_tables);
  free(compiler_hash_inputs);
  store_node_hash_vec_free(&node_hashes);
  return true;

invalid:
  store_diag(diag, path, line_no, "invalid repository graph store", line);
fail:
  free(line);
  free(source_projection);
  free(module_identity);
  free(graph_hash);
  free(module_hash);
  free(compiler_store);
  free(compiler_tables);
  free(compiler_hash_inputs);
  store_node_hash_vec_free(&node_hashes);
  z_program_graph_store_free(out);
  return false;
}

static void store_append_text(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphStore *projections) {
  ZProgramGraphStore metadata;
  z_program_graph_store_init(&metadata);
  store_collect_owned_source_paths(&metadata, graph, projections);
  zbuf_append(buf, "zero-repository-graph v1\n");
  zbuf_append(buf, "sourceProjection ");
  store_append_quoted(buf, ".0");
  zbuf_append_char(buf, '\n');
  zbuf_append(buf, "moduleIdentity ");
  store_append_quoted(buf, graph ? graph->module_identity : "module:main");
  zbuf_append_char(buf, '\n');
  zbuf_append(buf, "graphHash ");
  store_append_quoted(buf, graph ? graph->graph_hash : "");
  zbuf_append_char(buf, '\n');
  zbuf_append(buf, "moduleHash ");
  store_append_quoted(buf, graph ? graph->graph_hash : "");
  zbuf_append_char(buf, '\n');
  z_program_graph_store_append_compiler_metadata_for_graph(buf, graph, metadata.source_path_len, projections ? projections->projection_len : 0);
  for (size_t i = 0; i < metadata.source_path_len; i++) {
    zbuf_append(buf, "source path:");
    store_append_quoted(buf, metadata.source_paths[i]);
    zbuf_append_char(buf, '\n');
  }
  for (size_t i = 0; projections && i < projections->projection_len; i++) {
    zbuf_append(buf, "projection path:");
    store_append_quoted(buf, projections->projection_paths[i]);
    zbuf_append(buf, " text:");
    store_append_quoted(buf, projections->projection_texts[i]);
    zbuf_append_char(buf, '\n');
  }
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    zbuf_append(buf, "nodeHash node:");
    store_append_quoted(buf, node->id);
    zbuf_append(buf, " hash:");
    store_append_quoted(buf, node->node_hash);
    zbuf_append_char(buf, '\n');
  }
  zbuf_append(buf, "\ngraph\n");
  ZProgramGraphValidation validation = {0};
  z_program_graph_validate(graph, &validation);
  z_program_graph_append_dump(buf, graph, &validation);
  z_program_graph_store_free(&metadata);
}

static bool store_byte_stability_fail(const char *path, ZDiag *diag) {
  return store_diag(diag, path, 1, "repository graph store is not byte-stable after reload", "different canonical bytes");
}

static void store_append_format(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphStore *store, ZProgramGraphStoreFormat format) {
  if (format == Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY) z_program_graph_store_append_binary(buf, graph, store);
  else store_append_text(buf, graph, store);
}

static bool store_parse_format(const char *path, const unsigned char *data, size_t len, ZProgramGraphStoreFormat format, ZProgramGraphStore *out, ZDiag *diag) {
  if (format == Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY) return z_program_graph_store_parse_binary(path, data, len, out, diag);
  char *text = z_strndup((const char *)data, len);
  bool ok = store_parse_text(path, text, out, diag);
  free(text);
  if (ok) out->format = Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT;
  return ok;
}

static bool store_bytes_equal(const unsigned char *left, size_t left_len, const ZBuf *right) {
  size_t right_len = right && right->data ? right->len : 0;
  const unsigned char *right_data = right && right->data ? (const unsigned char *)right->data : (const unsigned char *)"";
  return left_len == right_len && (left_len == 0 || memcmp(left, right_data, left_len) == 0);
}

bool z_program_graph_store_load_path(const char *path, ZProgramGraphStore *out, ZDiag *diag) {
  unsigned char *data = NULL;
  size_t len = 0;
  if (!store_read_file_bytes(path, &data, &len, diag)) return false;
  ZProgramGraphStoreFormat format = z_program_graph_store_bytes_are_binary(data, len)
    ? Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY
    : Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT;
  bool ok = store_parse_format(path, data, len, format, out, diag);
  if (ok) {
    ZBuf canonical;
    zbuf_init(&canonical);
    store_append_format(&canonical, &out->graph, out, out->format);
    ok = store_bytes_equal(data, len, &canonical);
    zbuf_free(&canonical);
    if (!ok) {
      z_program_graph_store_free(out);
      store_byte_stability_fail(path, diag);
    }
  }
  free(data);
  return ok;
}

bool z_program_graph_store_load_for_input(const char *input, ZProgramGraphStore *out, ZDiag *diag) {
  char *root = z_program_graph_store_root_for_input(input);
  char *path = z_program_graph_store_path_for_root(root);
  bool ok = z_program_graph_store_load_path(path, out, diag);
  if (!ok && diag && diag->path == path) diag->path = z_strdup(path);
  free(root);
  free(path);
  return ok;
}

bool z_program_graph_store_write_path_format(const char *path, const ZProgramGraphStore *store, ZProgramGraphStoreFormat format, ZDiag *diag) {
  if (!store) return store_diag(diag, path, 1, "repository graph store write requires a store", "missing store");
  ZProgramGraphValidation validation = {0};
  if (!z_program_graph_validate(&store->graph, &validation)) {
    return store_diag(diag, path, 1, "repository graph store failed graph validation", validation.code);
  }
  ZBuf first;
  zbuf_init(&first);
  store_append_format(&first, &store->graph, store, format);
  ZProgramGraphStore parsed;
  if (!store_parse_format(path, (const unsigned char *)(first.data ? first.data : ""), first.len, format, &parsed, diag)) {
    zbuf_free(&first);
    return false;
  }
  ZBuf second;
  zbuf_init(&second);
  store_append_format(&second, &parsed.graph, &parsed, format);
  bool stable = first.len == second.len && (first.len == 0 || memcmp(first.data, second.data, first.len) == 0);
  z_program_graph_store_free(&parsed);
  if (!stable) {
    zbuf_free(&first);
    zbuf_free(&second);
    return store_byte_stability_fail(path, diag);
  }
  zbuf_free(&second);
  bool wrote = z_write_binary_file(path, (const unsigned char *)(first.data ? first.data : ""), first.len, diag);
  zbuf_free(&first);
  return wrote;
}

bool z_program_graph_store_write_path(const char *path, const ZProgramGraphStore *store, ZDiag *diag) {
  ZProgramGraphStoreFormat format = store ? store->format : Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT;
  if (format == Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT && z_program_graph_store_path_is_binary(path)) format = Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY;
  return z_program_graph_store_write_path_format(path, store, format, diag);
}

bool z_program_graph_store_write_generated_path_format(const char *path, const ZProgramGraph *graph, ZProgramGraphStoreFormat format, ZProgramGraphStore *out, ZDiag *diag) {
  if (!graph) return store_diag(diag, path, 1, "repository graph store write requires a graph", "missing graph");
  ZProgramGraphStore generated;
  z_program_graph_store_init(&generated);
  generated.path = z_strdup(path ? path : "zero.graph");
  generated.root = store_dirname(generated.path);
  generated.present = true;
  generated.format = format;
  generated.schema_version = 1;
  store_copy_graph(graph, &generated.graph);
  store_normalize_graph_paths_preserving_ids_for_root(&generated.graph, generated.root && generated.root[0] ? generated.root : ".");
  store_collect_source_paths(&generated, &generated.graph);
  if (!store_collect_generated_source_projections(&generated, &generated.graph, diag)) {
    z_program_graph_store_free(&generated);
    return false;
  }
  bool ok = z_program_graph_store_write_path_format(generated.path, &generated, format, diag);
  if (ok && out) {
    ok = z_program_graph_store_load_path(generated.path, out, diag);
  }
  z_program_graph_store_free(&generated);
  return ok;
}

bool z_program_graph_store_write_generated_path(const char *path, const ZProgramGraph *graph, ZProgramGraphStore *out, ZDiag *diag) {
  ZProgramGraphStoreFormat format = z_program_graph_store_path_is_binary(path)
    ? Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY
    : Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT;
  return z_program_graph_store_write_generated_path_format(path, graph, format, out, diag);
}

bool z_program_graph_store_save_path_format(const char *path, const ZProgramGraph *graph, ZProgramGraphStoreFormat format, ZDiag *diag) {
  char *root = store_dirname(path ? path : "zero.graph");
  ZProgramGraph normalized;
  if (!store_normalized_source_graph(root, graph, &normalized)) {
    free(root);
    return store_diag(diag, path, 1, "repository graph store requires a source graph", "missing source graph");
  }
  if (!store_preserve_existing_source_identities(path ? path : "zero.graph", &normalized, diag)) {
    z_program_graph_free(&normalized);
    free(root);
    return false;
  }
  ZProgramGraphValidation validation = {0};
  if (!z_program_graph_validate(&normalized, &validation)) {
    z_program_graph_free(&normalized);
    free(root);
    return store_diag(diag, path, 1, "repository graph store failed graph validation", validation.code);
  }
  if (!normalized.graph_hash || !normalized.graph_hash[0]) {
    z_program_graph_free(&normalized);
    free(root);
    return store_diag(diag, path, 1, "repository graph store requires graph hashes", "missing graph hash");
  }
  for (size_t i = 0; i < normalized.node_len; i++) {
    if (!normalized.nodes[i].node_hash || !normalized.nodes[i].node_hash[0]) {
      char *actual = z_strdup(normalized.nodes[i].id ? normalized.nodes[i].id : "");
      z_program_graph_free(&normalized);
      free(root);
      bool ok = store_diag(diag, path, 1, "repository graph store requires node hashes", actual);
      free(actual);
      return ok;
    }
  }

  ZBuf first;
  zbuf_init(&first);
  ZProgramGraphStore projections;
  z_program_graph_store_init(&projections);
  projections.format = format;
  store_collect_source_paths(&projections, &normalized);
  store_collect_source_projections(&projections, root);
  store_append_format(&first, &normalized, &projections, format);

  ZProgramGraphStore parsed;
  if (!store_parse_format(path, (const unsigned char *)(first.data ? first.data : ""), first.len, format, &parsed, diag)) {
    z_program_graph_store_free(&projections);
    zbuf_free(&first);
    z_program_graph_free(&normalized);
    free(root);
    return false;
  }
  ZBuf second;
  zbuf_init(&second);
  store_append_format(&second, &parsed.graph, &parsed, format);
  bool stable = first.len == second.len && (first.len == 0 || memcmp(first.data, second.data, first.len) == 0);
  z_program_graph_store_free(&parsed);
  if (!stable) {
    z_program_graph_store_free(&projections);
    zbuf_free(&first);
    zbuf_free(&second);
    z_program_graph_free(&normalized);
    free(root);
    return store_byte_stability_fail(path, diag);
  }
  zbuf_free(&second);
  bool wrote = z_write_binary_file(path, (const unsigned char *)(first.data ? first.data : ""), first.len, diag);
  z_program_graph_store_free(&projections);
  zbuf_free(&first);
  z_program_graph_free(&normalized);
  free(root);
  return wrote;
}

bool z_program_graph_store_save_path(const char *path, const ZProgramGraph *graph, ZDiag *diag) {
  ZProgramGraphStoreFormat format = z_program_graph_store_path_is_binary(path)
    ? Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY
    : Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT;
  return z_program_graph_store_save_path_format(path, graph, format, diag);
}

bool z_program_graph_store_save_for_input_format(const char *input, const ZProgramGraph *graph, ZProgramGraphStoreFormat format, ZProgramGraphStore *out, ZDiag *diag) {
  char *root = z_program_graph_store_root_for_input(input);
  char *path = z_program_graph_store_path_for_root(root);
  bool saved = z_program_graph_store_save_path_format(path, graph, format, diag);
  if (saved && out) saved = z_program_graph_store_load_path(path, out, diag);
  free(root);
  free(path);
  return saved;
}

bool z_program_graph_store_save_for_input(const char *input, const ZProgramGraph *graph, ZProgramGraphStore *out, ZDiag *diag) {
  char *root = z_program_graph_store_root_for_input(input);
  char *path = z_program_graph_store_path_for_root(root);
  ZProgramGraphStoreFormat format = z_program_graph_store_path_is_binary(path)
    ? Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY
    : Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT;
  free(root);
  free(path);
  return z_program_graph_store_save_for_input_format(input, graph, format, out, diag);
}

bool z_program_graph_store_graph_matches_source(const ZProgramGraphStore *store, const ZProgramGraph *source_graph) {
  if (!store || !source_graph) return false;
  ZProgramGraph normalized;
  if (!store_normalized_source_graph(store->root, source_graph, &normalized)) return false;
  ZProgramGraphIdentityReconcile identity = {0};
  bool preserved = z_program_graph_preserve_source_node_ids(&store->graph, &normalized, &identity);
  ZProgramGraphCompare comparison = {0};
  bool semantic_match = preserved && z_program_graph_semantic_compare(&store->graph, &normalized, &comparison);
  bool ok = semantic_match && store_source_paths_match_graph(store, &normalized);
  z_program_graph_free(&normalized);
  return ok;
}
