#include "program_graph_store.h"

#include "program_graph_format.h"
#include "zero.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
  if (store_ends_with(input, "/zero.json") || strcmp(input, "zero.json") == 0) cursor = store_dirname(input);
  else if (store_path_is_dir(input)) cursor = z_strdup(input);
  else cursor = store_dirname(input);
  char *fallback = cursor && cursor[0] ? z_strdup(cursor) : z_strdup(".");

  while (cursor && cursor[0]) {
    char *manifest = store_join_path(cursor, "zero.json");
    bool found = z_program_graph_store_file_exists(manifest);
    free(manifest);
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

void z_program_graph_store_init(ZProgramGraphStore *store) { if (store) *store = (ZProgramGraphStore){.schema_version = 1}; }

void z_program_graph_store_free(ZProgramGraphStore *store) {
  if (!store) return;
  free(store->root);
  free(store->path);
  for (size_t i = 0; i < store->source_path_len; i++) free(store->source_paths[i]);
  free(store->source_paths);
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

static int store_text_cmp(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "");
}

static bool store_text_eq(const char *left, const char *right) { return store_text_cmp(left, right) == 0; }

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

static void store_collect_source_paths(ZProgramGraphStore *store, const ZProgramGraph *graph) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    store_add_source_path(store, graph->nodes[i].path);
  }
  store_sort_source_paths(store);
}

static bool store_source_path_present(const ZProgramGraphStore *store, const char *path) {
  if (!path || !path[0]) return true;
  for (size_t i = 0; store && i < store->source_path_len; i++) {
    if (store_text_eq(store->source_paths[i], path)) return true;
  }
  return false;
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
            path[0] != 0;
  if (ok) store_add_source_path(store, path);
  free(path);
  return ok;
}

static bool store_verify_metadata(const char *path, ZProgramGraphStore *store, const char *module_identity, const char *graph_hash, const char *module_hash, const StoreNodeHashVec *node_hashes, ZDiag *diag) {
  if (!store_text_eq(module_identity, store->graph.module_identity)) return store_diag(diag, path, 1, "repository graph module identity does not match stored graph", store->graph.module_identity);
  if (!store_text_eq(graph_hash, store->graph.graph_hash)) return store_diag(diag, path, 1, "repository graph hash does not match stored graph", store->graph.graph_hash);
  if (!store_text_eq(module_hash, store->graph.graph_hash)) return store_diag(diag, path, 1, "repository graph module hash does not match stored graph", store->graph.graph_hash);
  if (node_hashes->len != store->graph.node_len) return store_diag(diag, path, 1, "repository graph node hash table has the wrong size", "node hash count mismatch");
  for (size_t i = 0; i < store->graph.node_len; i++) {
    const ZProgramGraphNode *node = &store->graph.nodes[i];
    const char *expected_hash = store_node_hash_find(node_hashes, node->id);
    if (!expected_hash) return store_diag(diag, path, 1, "repository graph store is missing a node hash", node->id);
    if (!store_text_eq(expected_hash, node->node_hash)) return store_diag(diag, path, 1, "repository graph node hash does not match graph content", node->id);
    if (!store_source_path_present(store, node->path)) return store_diag(diag, path, 1, "repository graph source table is missing a node source path", node->path);
  }
  return true;
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
    } else if (strncmp(line, "source ", strlen("source ")) == 0) {
      if (!store_parse_source_line(line, out)) goto invalid;
    } else if (strncmp(line, "nodeHash ", strlen("nodeHash ")) == 0) {
      if (!store_parse_node_hash_line(line, &node_hashes)) goto invalid;
    } else {
      goto invalid;
    }
    free(line);
    line = NULL;
  }

  if (!source_projection || !module_identity || !graph_hash || !module_hash || !*cursor) goto invalid;
  if (!z_program_graph_parse_dump(cursor, &out->graph, diag)) goto fail;
  ZProgramGraphValidation validation = {0};
  if (!z_program_graph_validate(&out->graph, &validation)) {
    store_diag(diag, path, 1, "repository graph store failed graph validation", validation.code);
    goto fail;
  }
  if (!store_verify_metadata(path, out, module_identity, graph_hash, module_hash, &node_hashes, diag)) goto fail;

  free(source_projection);
  free(module_identity);
  free(graph_hash);
  free(module_hash);
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
  store_node_hash_vec_free(&node_hashes);
  z_program_graph_store_free(out);
  return false;
}

static void store_append_text(ZBuf *buf, const ZProgramGraph *graph) {
  ZProgramGraphStore metadata;
  z_program_graph_store_init(&metadata);
  store_collect_source_paths(&metadata, graph);
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
  for (size_t i = 0; i < metadata.source_path_len; i++) {
    zbuf_append(buf, "source path:");
    store_append_quoted(buf, metadata.source_paths[i]);
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

bool z_program_graph_store_load_path(const char *path, ZProgramGraphStore *out, ZDiag *diag) {
  char *text = z_read_file(path, diag);
  if (!text) return false;
  bool ok = store_parse_text(path, text, out, diag);
  free(text);
  return ok;
}

bool z_program_graph_store_load_for_input(const char *input, ZProgramGraphStore *out, ZDiag *diag) {
  char *root = z_program_graph_store_root_for_input(input);
  char *path = z_program_graph_store_path_for_root(root);
  bool ok = z_program_graph_store_load_path(path, out, diag);
  free(root);
  free(path);
  return ok;
}

static bool store_byte_stability_fail(const char *path, ZDiag *diag) {
  return store_diag(diag, path, 1, "repository graph store is not byte-stable after reload", "different canonical bytes");
}

bool z_program_graph_store_save_path(const char *path, const ZProgramGraph *graph, ZDiag *diag) {
  ZProgramGraphValidation validation = {0};
  if (!z_program_graph_validate(graph, &validation)) return store_diag(diag, path, 1, "repository graph store failed graph validation", validation.code);
  if (!graph || !graph->graph_hash || !graph->graph_hash[0]) return store_diag(diag, path, 1, "repository graph store requires graph hashes", "missing graph hash");
  for (size_t i = 0; i < graph->node_len; i++) {
    if (!graph->nodes[i].node_hash || !graph->nodes[i].node_hash[0]) return store_diag(diag, path, 1, "repository graph store requires node hashes", graph->nodes[i].id);
  }

  ZBuf first;
  zbuf_init(&first);
  store_append_text(&first, graph);

  ZProgramGraphStore parsed;
  if (!store_parse_text(path, first.data ? first.data : "", &parsed, diag)) {
    zbuf_free(&first);
    return false;
  }
  ZBuf second;
  zbuf_init(&second);
  store_append_text(&second, &parsed.graph);
  bool stable = store_text_eq(first.data, second.data);
  z_program_graph_store_free(&parsed);
  if (!stable) {
    zbuf_free(&first);
    zbuf_free(&second);
    return store_byte_stability_fail(path, diag);
  }
  zbuf_free(&second);
  bool wrote = z_write_file(path, first.data ? first.data : "", diag);
  zbuf_free(&first);
  return wrote;
}

bool z_program_graph_store_save_for_input(const char *input, const ZProgramGraph *graph, ZProgramGraphStore *out, ZDiag *diag) {
  char *root = z_program_graph_store_root_for_input(input);
  char *path = z_program_graph_store_path_for_root(root);
  bool saved = z_program_graph_store_save_path(path, graph, diag);
  if (saved && out) saved = z_program_graph_store_load_path(path, out, diag);
  free(root);
  free(path);
  return saved;
}

bool z_program_graph_store_graph_matches_source(const ZProgramGraphStore *store, const ZProgramGraph *source_graph) {
  return store && source_graph &&
         store_text_eq(store->graph.module_identity, source_graph->module_identity) &&
         store_text_eq(store->graph.graph_hash, source_graph->graph_hash) &&
         store->graph.node_len == source_graph->node_len &&
         store->graph.edge_len == source_graph->edge_len;
}
