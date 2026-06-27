#include "program_graph_store_binary.h"

#include "program_graph_store_tables.h"
#include "program_graph_view.h"
#include "std_source.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char *id;
  char *hash;
} StoreBinaryNodeHash;

typedef struct {
  StoreBinaryNodeHash *items;
  size_t len;
  size_t cap;
} StoreBinaryNodeHashVec;

typedef struct {
  uint64_t offset;
  uint64_t len;
} StoreBinaryStringRef;

typedef struct {
  const unsigned char *data;
  size_t len;
  size_t cursor;
} StoreBinaryReader;

typedef struct {
  ZBuf bytes;
} StoreBinaryStringTable;

typedef struct {
  uint32_t schema;
  uint32_t flags;
  uint32_t validation_state;
  size_t source_count;
  size_t projection_count;
  size_t node_count;
  size_t edge_count;
  size_t next_id;
  size_t strings_offset;
  const unsigned char *strings;
  size_t strings_len;
  StoreBinaryStringRef module_identity_ref;
  StoreBinaryStringRef graph_hash_ref;
  StoreBinaryStringRef module_hash_ref;
  bool has_source_hash;
  StoreBinaryStringRef source_hash_ref;
  ZProgramGraphStoreTableCounts stored_counts;
} StoreBinaryHeader;

static const unsigned char STORE_BINARY_MAGIC[8] = {'Z', 'R', 'G', 'B', 'I', 'N', '1', 0};
static const uint64_t STORE_BINARY_NULL_LEN = UINT64_MAX;
static const size_t STORE_BINARY_MAX_SOURCE_COUNT = 100000;
static const size_t STORE_BINARY_MAX_PROJECTION_COUNT = 100000;
static const size_t STORE_BINARY_MAX_NODE_COUNT = 1000000;
static const size_t STORE_BINARY_MAX_EDGE_COUNT = 4000000;
static const size_t STORE_BINARY_MAX_STRING_BYTES = 256u * 1024u * 1024u;

static bool binary_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}
static const char *binary_basename(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  return slash ? slash + 1 : (path ? path : "");
}
static char *binary_dirname(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  if (!slash) return z_strdup(".");
  if (slash == path) return z_strdup("/");
  return z_strndup(path, (size_t)(slash - path));
}

static bool binary_diag(ZDiag *diag, const char *path, const char *message, const char *actual) {
  if (diag) {
    *diag = (ZDiag){0};
    diag->code = 1004;
    diag->path = path;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "invalid repository graph store");
    snprintf(diag->expected, sizeof(diag->expected), "valid zero.graph repository graph store");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "invalid binary store");
    snprintf(diag->help, sizeof(diag->help), "the store was likely written by a different zero build; rebuild it with this binary via `zero import .` or install the matching compiler");
  }
  return false;
}

bool z_program_graph_store_bytes_are_binary(const unsigned char *data, size_t len) {
  return data && len >= sizeof(STORE_BINARY_MAGIC) && memcmp(data, STORE_BINARY_MAGIC, sizeof(STORE_BINARY_MAGIC)) == 0;
}

static bool binary_add_source_path(ZProgramGraphStore *store, const char *path) {
  if (!store || !path || !path[0]) return true;
  if (!z_program_graph_store_source_path_is_local(path)) return false;
  for (size_t i = 0; i < store->source_path_len; i++) {
    if (binary_text_eq(store->source_paths[i], path)) return true;
  }
  if (store->source_path_len == store->source_path_cap) {
    size_t next = z_grow_capacity(store->source_path_cap, store->source_path_len + 1, 8);
    store->source_paths = z_checked_reallocarray(store->source_paths, next, sizeof(char *));
    store->source_path_cap = next;
  }
  store->source_paths[store->source_path_len++] = z_strdup(path);
  return true;
}

static bool binary_add_projection(ZProgramGraphStore *store, const char *path, const char *text) {
  if (!store || !path || !path[0] || !text) return true;
  if (!z_program_graph_store_source_path_is_local(path)) return false;
  for (size_t i = 0; i < store->projection_len; i++) {
    if (binary_text_eq(store->projection_paths[i], path)) return false;
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

static bool binary_source_path_present(const ZProgramGraphStore *store, const char *path) {
  if (!path || !path[0]) return true;
  for (size_t i = 0; store && i < store->source_path_len; i++) {
    if (binary_text_eq(store->source_paths[i], path)) return true;
  }
  return false;
}

static void binary_sort_source_paths(ZProgramGraphStore *store) {
  for (size_t i = 1; store && i < store->source_path_len; i++) {
    char *item = store->source_paths[i];
    size_t cursor = i;
    while (cursor > 0 && strcmp(item, store->source_paths[cursor - 1]) < 0) {
      store->source_paths[cursor] = store->source_paths[cursor - 1];
      cursor--;
    }
    store->source_paths[cursor] = item;
  }
}

static void binary_sort_projections(ZProgramGraphStore *store) {
  for (size_t i = 1; store && i < store->projection_len; i++) {
    char *path = store->projection_paths[i];
    char *text = store->projection_texts[i];
    size_t cursor = i;
    while (cursor > 0 && strcmp(path, store->projection_paths[cursor - 1]) < 0) {
      store->projection_paths[cursor] = store->projection_paths[cursor - 1];
      store->projection_texts[cursor] = store->projection_texts[cursor - 1];
      cursor--;
    }
    store->projection_paths[cursor] = path;
    store->projection_texts[cursor] = text;
  }
}

static const ZStdSourceModule *binary_std_source_module_for_path(const char *path) {
  if (!path) return NULL;
  const char *base = binary_basename(path);
  bool basename_candidate = base == path || (strncmp(path, "std/", 4) == 0 && strchr(path + 4, '/') == NULL);
  for (size_t i = 0; i < z_std_source_module_count(); i++) {
    const ZStdSourceModule *module = z_std_source_module_at(i);
    if (!module) continue;
    if (binary_text_eq(module->path, path)) return module;
    if (basename_candidate && binary_text_eq(binary_basename(module->path), base)) return module;
  }
  return NULL;
}

static bool binary_graph_source_path_is_embedded_std(const ZProgramGraph *graph, const char *path) {
  const ZStdSourceModule *module = binary_std_source_module_for_path(path);
  if (!graph || !module) return false;
  const char *short_name = strrchr(module->module ? module->module : "", '.');
  short_name = short_name ? short_name + 1 : module->module;
  for (size_t i = 0; i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!binary_text_eq(node->path, path)) continue;
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE &&
        (binary_text_eq(node->name, module->module) || binary_text_eq(node->name, short_name))) {
      return true;
    }
  }
  return false;
}

static void binary_collect_owned_source_paths(ZProgramGraphStore *store, const ZProgramGraph *graph, const ZProgramGraphStore *projections) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const char *path = graph->nodes[i].path;
    if (binary_graph_source_path_is_embedded_std(graph, path) &&
        !z_program_graph_store_projection_text(projections, path)) {
      continue;
    }
    binary_add_source_path(store, path);
  }
  binary_sort_source_paths(store);
}

static void binary_node_hash_vec_free(StoreBinaryNodeHashVec *hashes) {
  if (!hashes) return;
  for (size_t i = 0; i < hashes->len; i++) {
    free(hashes->items[i].id);
    free(hashes->items[i].hash);
  }
  free(hashes->items);
  *hashes = (StoreBinaryNodeHashVec){0};
}

static bool binary_node_hash_vec_add(StoreBinaryNodeHashVec *hashes, char *id, char *hash) {
  if (hashes->len == hashes->cap) {
    size_t next = z_grow_capacity(hashes->cap, hashes->len + 1, 32);
    hashes->items = z_checked_reallocarray(hashes->items, next, sizeof(StoreBinaryNodeHash));
    hashes->cap = next;
  }
  hashes->items[hashes->len++] = (StoreBinaryNodeHash){.id = id, .hash = hash};
  return true;
}

static const char *binary_node_hash_at(const StoreBinaryNodeHashVec *hashes, size_t index, const char *id) {
  if (!hashes || index >= hashes->len) return NULL;
  if (!binary_text_eq(hashes->items[index].id, id)) return NULL;
  return hashes->items[index].hash;
}

static bool binary_verify_graph_metadata(const char *path, ZProgramGraphStore *store, const char *module_identity, const char *graph_hash, const char *module_hash, const StoreBinaryNodeHashVec *node_hashes, ZDiag *diag) {
  if (!binary_text_eq(module_identity, store->graph.module_identity)) return binary_diag(diag, path, "repository graph module identity does not match stored graph", store->graph.module_identity);
  if (!binary_text_eq(graph_hash, store->graph.graph_hash)) return binary_diag(diag, path, "repository graph hash does not match stored graph", store->graph.graph_hash);
  if (!binary_text_eq(module_hash, store->graph.graph_hash)) return binary_diag(diag, path, "repository graph module hash does not match stored graph", store->graph.graph_hash);
  if (node_hashes->len != store->graph.node_len) return binary_diag(diag, path, "repository graph node hash table has the wrong size", "node hash count mismatch");
  for (size_t i = 0; i < store->graph.node_len; i++) {
    const ZProgramGraphNode *node = &store->graph.nodes[i];
    const char *expected_hash = binary_node_hash_at(node_hashes, i, node->id);
    if (!expected_hash) return binary_diag(diag, path, "repository graph store is missing a node hash", node->id);
    if (!binary_text_eq(expected_hash, node->node_hash)) return binary_diag(diag, path, "repository graph node hash does not match graph content", node->id);
    if (!binary_source_path_present(store, node->path) && !binary_graph_source_path_is_embedded_std(&store->graph, node->path)) return binary_diag(diag, path, "repository graph source table is missing a node source path", node->path);
  }
  for (size_t i = 0; i < store->source_path_len; i++) {
    if (binary_graph_source_path_is_embedded_std(&store->graph, store->source_paths[i])) continue;
    if (!z_program_graph_store_projection_text(store, store->source_paths[i])) return binary_diag(diag, path, "repository graph projection table is missing a source path", store->source_paths[i]);
  }
  for (size_t i = 0; i < store->projection_len; i++) {
    if (!binary_source_path_present(store, store->projection_paths[i])) return binary_diag(diag, path, "repository graph projection table references an unknown source path", store->projection_paths[i]);
  }
  return true;
}

static bool binary_verify_table_counts(const char *path, const ZProgramGraphStore *store, const ZProgramGraphStoreTableCounts *stored, ZDiag *diag) {
  ZProgramGraphStoreTableCounts expected;
  z_program_graph_store_table_counts_for_graph(store ? &store->graph : NULL, store ? store->source_path_len : 0, store ? store->projection_len : 0, &expected);
#define CHECK_BINARY_COUNT(field) \
  do { \
    if (stored->field != expected.field) return binary_diag(diag, path, "repository graph binary compiler table counts do not match stored graph", #field); \
  } while (0)
  CHECK_BINARY_COUNT(schema);
  CHECK_BINARY_COUNT(package);
  CHECK_BINARY_COUNT(module);
  CHECK_BINARY_COUNT(declaration);
  CHECK_BINARY_COUNT(scope);
  CHECK_BINARY_COUNT(import);
  CHECK_BINARY_COUNT(symbol);
  CHECK_BINARY_COUNT(type);
  CHECK_BINARY_COUNT(effect);
  CHECK_BINARY_COUNT(capability);
  CHECK_BINARY_COUNT(ownership);
  CHECK_BINARY_COUNT(resource);
  CHECK_BINARY_COUNT(node);
  CHECK_BINARY_COUNT(edge);
  CHECK_BINARY_COUNT(projection);
  CHECK_BINARY_COUNT(source_map);
#undef CHECK_BINARY_COUNT
  return true;
}

static void binary_append_bytes(ZBuf *buf, const void *data, size_t len) {
  const unsigned char *bytes = (const unsigned char *)data;
  for (size_t i = 0; i < len; i++) zbuf_append_char(buf, (char)bytes[i]);
}

static void binary_put_u32(ZBuf *buf, uint32_t value) {
  for (unsigned i = 0; i < 4; i++) zbuf_append_char(buf, (char)((value >> (i * 8)) & 0xffu));
}

static void binary_put_i32(ZBuf *buf, int32_t value) {
  binary_put_u32(buf, (uint32_t)value);
}

static void binary_put_u64(ZBuf *buf, uint64_t value) {
  for (unsigned i = 0; i < 8; i++) zbuf_append_char(buf, (char)((value >> (i * 8)) & 0xffu));
}

static void binary_put_ref(ZBuf *buf, StoreBinaryStringRef ref) {
  binary_put_u64(buf, ref.offset);
  binary_put_u64(buf, ref.len);
}

static StoreBinaryStringRef binary_add_string(StoreBinaryStringTable *table, const char *text) {
  if (!text) return (StoreBinaryStringRef){.offset = 0, .len = STORE_BINARY_NULL_LEN};
  size_t len = strlen(text);
  StoreBinaryStringRef ref = {.offset = (uint64_t)table->bytes.len, .len = (uint64_t)len};
  binary_append_bytes(&table->bytes, text, len);
  return ref;
}

static void binary_put_counts(ZBuf *buf, const ZProgramGraphStoreTableCounts *counts) {
  binary_put_u64(buf, (uint64_t)counts->schema);
  binary_put_u64(buf, (uint64_t)counts->package);
  binary_put_u64(buf, (uint64_t)counts->module);
  binary_put_u64(buf, (uint64_t)counts->declaration);
  binary_put_u64(buf, (uint64_t)counts->scope);
  binary_put_u64(buf, (uint64_t)counts->import);
  binary_put_u64(buf, (uint64_t)counts->symbol);
  binary_put_u64(buf, (uint64_t)counts->type);
  binary_put_u64(buf, (uint64_t)counts->effect);
  binary_put_u64(buf, (uint64_t)counts->capability);
  binary_put_u64(buf, (uint64_t)counts->ownership);
  binary_put_u64(buf, (uint64_t)counts->resource);
  binary_put_u64(buf, (uint64_t)counts->node);
  binary_put_u64(buf, (uint64_t)counts->edge);
  binary_put_u64(buf, (uint64_t)counts->projection);
  binary_put_u64(buf, (uint64_t)counts->source_map);
}

static void binary_append_source_records(ZBuf *records, StoreBinaryStringTable *strings, const ZProgramGraphStore *metadata) {
  for (size_t i = 0; metadata && i < metadata->source_path_len; i++) {
    binary_put_ref(records, binary_add_string(strings, metadata->source_paths[i]));
  }
}

static void binary_append_projection_records(ZBuf *records, StoreBinaryStringTable *strings, const ZProgramGraphStore *projections) {
  for (size_t i = 0; projections && i < projections->projection_len; i++) {
    binary_put_ref(records, binary_add_string(strings, projections->projection_paths[i]));
    binary_put_ref(records, binary_add_string(strings, projections->projection_texts[i]));
  }
}

static void binary_append_node_record(ZBuf *records, StoreBinaryStringTable *strings, const ZProgramGraphNode *node) {
  binary_put_ref(records, binary_add_string(strings, node->id));
  binary_put_ref(records, binary_add_string(strings, node->name));
  binary_put_ref(records, binary_add_string(strings, node->type));
  binary_put_ref(records, binary_add_string(strings, node->value));
  binary_put_ref(records, binary_add_string(strings, node->path));
  binary_put_ref(records, binary_add_string(strings, node->symbol_id));
  binary_put_ref(records, binary_add_string(strings, node->type_id));
  binary_put_ref(records, binary_add_string(strings, node->effect_id));
  binary_put_ref(records, binary_add_string(strings, node->node_hash));
  binary_put_u32(records, (uint32_t)node->kind);
  binary_put_i32(records, (int32_t)node->line);
  binary_put_i32(records, (int32_t)node->column);
  uint32_t flags = 0;
  if (node->is_public) flags |= 1u << 0;
  if (node->is_mutable) flags |= 1u << 1;
  if (node->is_static) flags |= 1u << 2;
  if (node->fallible) flags |= 1u << 3;
  if (node->export_c) flags |= 1u << 4;
  binary_put_u32(records, flags);
}

static void binary_append_graph_records(ZBuf *records, StoreBinaryStringTable *strings, const ZProgramGraph *graph) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    binary_append_node_record(records, strings, &graph->nodes[i]);
  }
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    binary_put_ref(records, binary_add_string(strings, edge->from));
    binary_put_ref(records, binary_add_string(strings, edge->to));
    binary_put_ref(records, binary_add_string(strings, edge->kind));
    binary_put_u32(records, (uint32_t)edge->target);
    binary_put_u32(records, 0);
    binary_put_u64(records, (uint64_t)edge->order);
  }
}

void z_program_graph_store_append_binary(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphStore *projections) {
  ZProgramGraphStore metadata;
  z_program_graph_store_init(&metadata);
  binary_collect_owned_source_paths(&metadata, graph, projections);
  StoreBinaryStringTable strings;
  zbuf_init(&strings.bytes);
  ZBuf records;
  zbuf_init(&records);
  StoreBinaryStringRef module_identity_ref = binary_add_string(&strings, graph ? graph->module_identity : "module:main");
  StoreBinaryStringRef graph_hash_ref = binary_add_string(&strings, graph ? graph->graph_hash : "");
  StoreBinaryStringRef module_hash_ref = binary_add_string(&strings, graph ? graph->graph_hash : "");
  const char *source_hash = projections && projections->source_projection_hash && projections->source_projection_hash[0] ? projections->source_projection_hash : NULL;
  StoreBinaryStringRef source_hash_ref = {0};
  if (source_hash) source_hash_ref = binary_add_string(&strings, source_hash);
  binary_append_source_records(&records, &strings, &metadata);
  binary_append_projection_records(&records, &strings, projections);
  binary_append_graph_records(&records, &strings, graph);
  ZProgramGraphStoreTableCounts counts;
  z_program_graph_store_table_counts_for_graph(graph, metadata.source_path_len, projections ? projections->projection_len : 0, &counts);
  uint32_t flags = graph && graph->canonical_source ? 1u : 0u;
  if (source_hash) flags |= 1u << 1;
  binary_append_bytes(buf, STORE_BINARY_MAGIC, sizeof(STORE_BINARY_MAGIC));
  binary_put_u32(buf, 1);
  binary_put_u32(buf, flags);
  binary_put_u32(buf, (uint32_t)(graph ? Z_PROGRAM_GRAPH_VALIDATION_SHAPE_VALID : Z_PROGRAM_GRAPH_VALIDATION_DECODED));
  binary_put_u32(buf, 0);
  binary_put_u64(buf, (uint64_t)metadata.source_path_len);
  binary_put_u64(buf, (uint64_t)(projections ? projections->projection_len : 0));
  binary_put_u64(buf, (uint64_t)(graph ? graph->node_len : 0));
  binary_put_u64(buf, (uint64_t)(graph ? graph->edge_len : 0));
  binary_put_u64(buf, (uint64_t)(graph ? graph->next_id : 0));
  binary_put_u64(buf, (uint64_t)strings.bytes.len);
  binary_put_ref(buf, module_identity_ref);
  binary_put_ref(buf, graph_hash_ref);
  binary_put_ref(buf, module_hash_ref);
  if (source_hash) binary_put_ref(buf, source_hash_ref);
  binary_put_counts(buf, &counts);
  binary_append_bytes(buf, records.data, records.len);
  binary_append_bytes(buf, strings.bytes.data, strings.bytes.len);
  zbuf_free(&records);
  zbuf_free(&strings.bytes);
  z_program_graph_store_free(&metadata);
}

static bool binary_get_bytes(StoreBinaryReader *reader, void *out, size_t len) {
  if (!reader || reader->cursor > reader->len || len > reader->len - reader->cursor) return false;
  memcpy(out, reader->data + reader->cursor, len);
  reader->cursor += len;
  return true;
}

static bool binary_get_u32(StoreBinaryReader *reader, uint32_t *out) {
  unsigned char bytes[4];
  if (!binary_get_bytes(reader, bytes, sizeof(bytes))) return false;
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; i++) value |= (uint32_t)bytes[i] << (i * 8);
  *out = value;
  return true;
}

static bool binary_get_i32(StoreBinaryReader *reader, int32_t *out) {
  uint32_t value = 0;
  if (!binary_get_u32(reader, &value)) return false;
  *out = (int32_t)value;
  return true;
}

static bool binary_get_u64(StoreBinaryReader *reader, uint64_t *out) {
  unsigned char bytes[8];
  if (!binary_get_bytes(reader, bytes, sizeof(bytes))) return false;
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; i++) value |= (uint64_t)bytes[i] << (i * 8);
  *out = value;
  return true;
}

static bool binary_get_ref(StoreBinaryReader *reader, StoreBinaryStringRef *out) {
  return binary_get_u64(reader, &out->offset) && binary_get_u64(reader, &out->len);
}

static bool binary_ref_string(const StoreBinaryHeader *header, StoreBinaryStringRef ref, char **out) {
  if (ref.len == STORE_BINARY_NULL_LEN) {
    *out = NULL;
    return true;
  }
  if (ref.offset > (uint64_t)header->strings_len || ref.len > (uint64_t)header->strings_len - ref.offset || ref.len > (uint64_t)SIZE_MAX) return false;
  const unsigned char *start = header->strings + (size_t)ref.offset;
  size_t len = (size_t)ref.len;
  if (memchr(start, 0, len) != NULL) return false;
  *out = z_strndup((const char *)start, len);
  return true;
}

static bool binary_get_count(StoreBinaryReader *reader, size_t *out) {
  uint64_t value = 0;
  if (!binary_get_u64(reader, &value) || value > (uint64_t)SIZE_MAX) return false;
  *out = (size_t)value;
  return true;
}

static bool binary_get_counts(StoreBinaryReader *reader, ZProgramGraphStoreTableCounts *counts) {
  return binary_get_count(reader, &counts->schema) &&
         binary_get_count(reader, &counts->package) &&
         binary_get_count(reader, &counts->module) &&
         binary_get_count(reader, &counts->declaration) &&
         binary_get_count(reader, &counts->scope) &&
         binary_get_count(reader, &counts->import) &&
         binary_get_count(reader, &counts->symbol) &&
         binary_get_count(reader, &counts->type) &&
         binary_get_count(reader, &counts->effect) &&
         binary_get_count(reader, &counts->capability) &&
         binary_get_count(reader, &counts->ownership) &&
         binary_get_count(reader, &counts->resource) &&
         binary_get_count(reader, &counts->node) &&
         binary_get_count(reader, &counts->edge) &&
         binary_get_count(reader, &counts->projection) &&
         binary_get_count(reader, &counts->source_map);
}

static bool binary_header_records_fit(const StoreBinaryHeader *header, const StoreBinaryReader *reader) {
  uint64_t record_bytes = 0;
  const uint64_t sizes[] = {16, 32, 160, 64};
  const uint64_t counts[] = {header->source_count, header->projection_count, header->node_count, header->edge_count};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    if (counts[i] > (UINT64_MAX - record_bytes) / sizes[i]) return false;
    record_bytes += counts[i] * sizes[i];
  }
  return record_bytes <= (uint64_t)(header->strings_offset - reader->cursor);
}

static bool binary_header_counts_are_reasonable(const StoreBinaryHeader *header) {
  return header->source_count <= STORE_BINARY_MAX_SOURCE_COUNT &&
         header->projection_count <= STORE_BINARY_MAX_PROJECTION_COUNT &&
         header->node_count <= STORE_BINARY_MAX_NODE_COUNT &&
         header->edge_count <= STORE_BINARY_MAX_EDGE_COUNT &&
         header->strings_len <= STORE_BINARY_MAX_STRING_BYTES;
}

static bool binary_read_header(StoreBinaryReader *reader, StoreBinaryHeader *header, size_t file_len) {
  unsigned char magic[sizeof(STORE_BINARY_MAGIC)];
  uint32_t reserved = 0;
  uint64_t next_id64 = 0;
  uint64_t string_bytes64 = 0;
  bool ok = binary_get_bytes(reader, magic, sizeof(magic)) &&
            memcmp(magic, STORE_BINARY_MAGIC, sizeof(magic)) == 0 &&
            binary_get_u32(reader, &header->schema) &&
            binary_get_u32(reader, &header->flags) &&
            binary_get_u32(reader, &header->validation_state) &&
            binary_get_u32(reader, &reserved) &&
            binary_get_count(reader, &header->source_count) &&
            binary_get_count(reader, &header->projection_count) &&
            binary_get_count(reader, &header->node_count) &&
            binary_get_count(reader, &header->edge_count) &&
            binary_get_u64(reader, &next_id64) &&
            binary_get_u64(reader, &string_bytes64) &&
            binary_get_ref(reader, &header->module_identity_ref) &&
            binary_get_ref(reader, &header->graph_hash_ref) &&
            binary_get_ref(reader, &header->module_hash_ref);
  if (ok) {
    header->has_source_hash = (header->flags & (1u << 1)) != 0;
    if (header->has_source_hash) ok = binary_get_ref(reader, &header->source_hash_ref);
  }
  ok = ok && binary_get_counts(reader, &header->stored_counts);
  if (!ok || header->schema != 1 || reserved != 0 || next_id64 > (uint64_t)SIZE_MAX ||
      string_bytes64 > (uint64_t)SIZE_MAX || string_bytes64 > (uint64_t)file_len ||
      (header->flags & ~3u) != 0 ||
      header->validation_state > (uint32_t)Z_PROGRAM_GRAPH_VALIDATION_BUILDABLE) {
    return false;
  }
  header->next_id = (size_t)next_id64;
  header->strings_offset = file_len - (size_t)string_bytes64;
  header->strings = reader->data + header->strings_offset;
  header->strings_len = (size_t)string_bytes64;
  reader->len = header->strings_offset;
  return header->strings_offset >= reader->cursor &&
         binary_header_counts_are_reasonable(header) &&
         binary_header_records_fit(header, reader);
}

static bool binary_parse_sources(StoreBinaryReader *reader, const StoreBinaryHeader *header, ZProgramGraphStore *out) {
  for (size_t i = 0; i < header->source_count; i++) {
    StoreBinaryStringRef path_ref = {0};
    char *source_path = NULL;
    bool ok = binary_get_ref(reader, &path_ref) &&
              binary_ref_string(header, path_ref, &source_path) &&
              source_path && source_path[0] &&
              binary_add_source_path(out, source_path);
    free(source_path);
    if (!ok) return false;
  }
  return true;
}

static bool binary_parse_projections(StoreBinaryReader *reader, const StoreBinaryHeader *header, ZProgramGraphStore *out) {
  for (size_t i = 0; i < header->projection_count; i++) {
    StoreBinaryStringRef path_ref = {0};
    StoreBinaryStringRef text_ref = {0};
    char *projection_path = NULL;
    char *projection_text = NULL;
    bool ok = binary_get_ref(reader, &path_ref) &&
              binary_get_ref(reader, &text_ref) &&
              binary_ref_string(header, path_ref, &projection_path) &&
              binary_ref_string(header, text_ref, &projection_text) &&
              projection_path && projection_path[0] && projection_text &&
              binary_add_projection(out, projection_path, projection_text);
    free(projection_path);
    free(projection_text);
    if (!ok) return false;
  }
  return true;
}

static bool binary_parse_node_strings(const StoreBinaryHeader *header, const StoreBinaryStringRef refs[9], ZProgramGraphNode *node) {
  return binary_ref_string(header, refs[0], &node->id) &&
         binary_ref_string(header, refs[1], &node->name) &&
         binary_ref_string(header, refs[2], &node->type) &&
         binary_ref_string(header, refs[3], &node->value) &&
         binary_ref_string(header, refs[4], &node->path) &&
         binary_ref_string(header, refs[5], &node->symbol_id) &&
         binary_ref_string(header, refs[6], &node->type_id) &&
         binary_ref_string(header, refs[7], &node->effect_id) &&
         binary_ref_string(header, refs[8], &node->node_hash) &&
         node->id && node->id[0] && node->node_hash && node->node_hash[0];
}

static bool binary_parse_nodes(StoreBinaryReader *reader, const StoreBinaryHeader *header, ZProgramGraphStore *out, StoreBinaryNodeHashVec *node_hashes) {
  if (header->node_count > 0) {
    out->graph.nodes = z_checked_calloc(header->node_count, sizeof(ZProgramGraphNode));
    out->graph.node_len = header->node_count;
    out->graph.node_cap = header->node_count;
  }
  for (size_t i = 0; i < out->graph.node_len; i++) {
    ZProgramGraphNode *node = &out->graph.nodes[i];
    StoreBinaryStringRef refs[9];
    uint32_t kind = 0;
    uint32_t node_flags = 0;
    int32_t line = 0;
    int32_t column = 0;
    for (size_t ref_index = 0; ref_index < 9; ref_index++) {
      if (!binary_get_ref(reader, &refs[ref_index])) return false;
    }
    if (!binary_get_u32(reader, &kind) || !binary_get_i32(reader, &line) ||
        !binary_get_i32(reader, &column) || !binary_get_u32(reader, &node_flags) ||
        kind > (uint32_t)Z_PROGRAM_GRAPH_NODE_STATEMENT || (node_flags & ~31u) != 0) return false;
    node->kind = (ZProgramGraphNodeKind)kind;
    node->line = (int)line;
    node->column = (int)column;
    node->is_public = (node_flags & (1u << 0)) != 0;
    node->is_mutable = (node_flags & (1u << 1)) != 0;
    node->is_static = (node_flags & (1u << 2)) != 0;
    node->fallible = (node_flags & (1u << 3)) != 0;
    node->export_c = (node_flags & (1u << 4)) != 0;
    if (!binary_parse_node_strings(header, refs, node) ||
        !binary_node_hash_vec_add(node_hashes, z_strdup(node->id), z_strdup(node->node_hash))) return false;
  }
  return true;
}

static bool binary_parse_edges(StoreBinaryReader *reader, const StoreBinaryHeader *header, ZProgramGraphStore *out) {
  if (header->edge_count > 0) {
    out->graph.edges = z_checked_calloc(header->edge_count, sizeof(ZProgramGraphEdge));
    out->graph.edge_len = header->edge_count;
    out->graph.edge_cap = header->edge_count;
  }
  for (size_t i = 0; i < out->graph.edge_len; i++) {
    ZProgramGraphEdge *edge = &out->graph.edges[i];
    StoreBinaryStringRef from_ref = {0};
    StoreBinaryStringRef to_ref = {0};
    StoreBinaryStringRef kind_ref = {0};
    uint32_t target = 0;
    uint32_t edge_reserved = 0;
    uint64_t order = 0;
    bool ok = binary_get_ref(reader, &from_ref) &&
              binary_get_ref(reader, &to_ref) &&
              binary_get_ref(reader, &kind_ref) &&
              binary_get_u32(reader, &target) &&
              binary_get_u32(reader, &edge_reserved) &&
              binary_get_u64(reader, &order) &&
              target <= (uint32_t)Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT &&
              edge_reserved == 0 &&
              order <= (uint64_t)SIZE_MAX &&
              binary_ref_string(header, from_ref, &edge->from) &&
              binary_ref_string(header, to_ref, &edge->to) &&
              binary_ref_string(header, kind_ref, &edge->kind) &&
              edge->from && edge->from[0] && edge->to && edge->to[0] && edge->kind && edge->kind[0];
    if (!ok) return false;
    edge->target = (ZProgramGraphEdgeTarget)target;
    edge->order = (size_t)order;
  }
  return true;
}

static bool binary_decode_module_strings(const StoreBinaryHeader *header, char **module_identity, char **graph_hash, char **module_hash) {
  return binary_ref_string(header, header->module_identity_ref, module_identity) &&
         binary_ref_string(header, header->graph_hash_ref, graph_hash) &&
         binary_ref_string(header, header->module_hash_ref, module_hash) &&
         *module_identity && (*module_identity)[0] &&
         *graph_hash && (*graph_hash)[0] &&
         *module_hash && (*module_hash)[0];
}

bool z_program_graph_store_parse_binary(const char *path, const unsigned char *data, size_t len, ZProgramGraphStore *out, ZDiag *diag) {
  z_program_graph_store_init(out);
  out->path = z_strdup(path ? path : "zero.graph");
  out->root = binary_dirname(out->path);
  out->present = true;
  out->format = Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY;
  StoreBinaryReader reader = {.data = data, .len = len, .cursor = 0};
  StoreBinaryHeader header = {0};
  StoreBinaryNodeHashVec node_hashes = {0};
  char *module_identity = NULL;
  char *graph_hash = NULL;
  char *module_hash = NULL;
  bool ok = binary_read_header(&reader, &header, len) &&
            binary_decode_module_strings(&header, &module_identity, &graph_hash, &module_hash);
  if (ok && header.has_source_hash) {
    ok = binary_ref_string(&header, header.source_hash_ref, &out->source_projection_hash) &&
         out->source_projection_hash && out->source_projection_hash[0];
  }
  if (ok) {
    out->schema_version = header.schema;
    out->graph.schema_version = header.schema;
    out->graph.canonical_source = (header.flags & 1u) != 0;
    out->graph.validation_state = (ZProgramGraphValidationState)header.validation_state;
    out->graph.next_id = header.next_id;
    free(out->graph.module_identity);
    out->graph.module_identity = z_strdup(module_identity);
    free(out->graph.graph_hash);
    out->graph.graph_hash = z_strdup(graph_hash);
    ok = binary_parse_sources(&reader, &header, out) &&
         binary_parse_projections(&reader, &header, out) &&
         binary_parse_nodes(&reader, &header, out, &node_hashes) &&
         binary_parse_edges(&reader, &header, out) &&
         reader.cursor == header.strings_offset;
  }
  if (ok) {
    binary_sort_projections(out);
    z_program_graph_finalize_identities(&out->graph);
    ZProgramGraphValidation validation = {0};
    ok = z_program_graph_validate(&out->graph, &validation);
    if (!ok) binary_diag(diag, path, "repository graph store failed graph validation", validation.code);
    if (ok) ok = binary_verify_table_counts(path, out, &header.stored_counts, diag);
    if (ok) ok = binary_verify_graph_metadata(path, out, module_identity, graph_hash, module_hash, &node_hashes, diag);
  } else {
    binary_diag(diag, path, "invalid binary repository graph store", "invalid binary store");
  }
  free(module_identity);
  free(graph_hash);
  free(module_hash);
  binary_node_hash_vec_free(&node_hashes);
  if (!ok) z_program_graph_store_free(out);
  return ok;
}
