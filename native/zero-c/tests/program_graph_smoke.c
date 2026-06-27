#include "program_graph_format.h"
#include "program_graph_lower.h"
#include "program_graph_projection.h"
#include "program_graph_store_binary.h"

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect(int ok, const char *message);

void *z_checked_malloc(size_t size) {
  void *ptr = malloc(size ? size : 1);
  if (ptr) return ptr;
  fprintf(stderr, "out of memory\n");
  exit(1);
}

void *z_checked_calloc(size_t count, size_t item_size) {
  void *ptr = calloc(count ? count : 1, item_size ? item_size : 1);
  if (ptr) return ptr;
  fprintf(stderr, "out of memory\n");
  exit(1);
}

void *z_checked_reallocarray(void *ptr, size_t count, size_t item_size) {
  void *next = realloc(ptr, (count ? count : 1) * (item_size ? item_size : 1));
  if (next) return next;
  fprintf(stderr, "out of memory\n");
  exit(1);
}

size_t z_grow_capacity(size_t current, size_t required, size_t initial) {
  size_t next = current ? current : initial;
  while (next < required) next *= 2;
  return next;
}

char *z_strdup(const char *text) {
  size_t len = strlen(text ? text : "");
  return z_strndup(text ? text : "", len);
}

char *z_strndup(const char *text, size_t len) {
  char *copy = z_checked_malloc(len + 1);
  memcpy(copy, text ? text : "", len + 1);
  copy[len] = 0;
  return copy;
}

bool z_write_file(const char *path, const char *text, ZDiag *diag) {
  FILE *file = fopen(path, "wb");
  if (!file) {
    if (diag) snprintf(diag->message, sizeof(diag->message), "failed to write test graph");
    return false;
  }
  fputs(text ? text : "", file);
  fclose(file);
  return true;
}

void zbuf_init(ZBuf *buf) { *buf = (ZBuf){0}; }
void zbuf_free(ZBuf *buf) {
  free(buf->data);
  *buf = (ZBuf){0};
}

void zbuf_append_char(ZBuf *buf, char ch) {
  if (buf->len + 1 >= buf->cap) {
    buf->cap = z_grow_capacity(buf->cap, buf->len + 2, 64);
    buf->data = z_checked_reallocarray(buf->data, buf->cap, 1);
  }
  buf->data[buf->len++] = ch;
  buf->data[buf->len] = 0;
}

void zbuf_append(ZBuf *buf, const char *text) {
  for (const char *p = text ? text : ""; *p; p++) zbuf_append_char(buf, *p);
}

void zbuf_appendf(ZBuf *buf, const char *fmt, ...) {
  char stack[256];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(stack, sizeof(stack), fmt, args);
  va_end(args);
  expect(len >= 0 && (size_t)len < sizeof(stack), "formatted test buffer overflow");
  zbuf_append(buf, stack);
}

static void expect(int ok, const char *message) {
  if (ok) return;
  fprintf(stderr, "%s\n", message);
  exit(1);
}

static uint64_t read_le_u64(const unsigned char *bytes, size_t offset, size_t len) {
  expect(bytes != NULL && offset <= len && len - offset >= 8, "binary fixture read out of range");
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; i++) value |= (uint64_t)bytes[offset + i] << (i * 8);
  return value;
}

static void write_le_u64(unsigned char *bytes, size_t offset, size_t len, uint64_t value) {
  expect(bytes != NULL && offset <= len && len - offset >= 8, "binary fixture write out of range");
  for (unsigned i = 0; i < 8; i++) bytes[offset + i] = (unsigned char)((value >> (i * 8)) & 0xffu);
}

static unsigned char *copy_binary_fixture(const ZBuf *binary) {
  unsigned char *copy = z_checked_malloc(binary->len);
  memcpy(copy, binary->data, binary->len);
  return copy;
}

static void expect_binary_store_rejected(const unsigned char *bytes, size_t len, const char *message) {
  ZProgramGraphStore parsed;
  ZDiag diag = {0};
  expect(!z_program_graph_store_parse_binary("corrupt-zero.graph", bytes, len, &parsed, &diag), message);
  expect(strstr(diag.message, "invalid") != NULL || strstr(diag.message, "failed") != NULL, "binary rejection reported wrong diagnostic");
}

enum {
  BINARY_SOURCE_COUNT_OFFSET = 24,
  BINARY_PROJECTION_COUNT_OFFSET = 32,
  BINARY_NODE_COUNT_OFFSET = 40,
  BINARY_EDGE_COUNT_OFFSET = 48,
  BINARY_STRING_BYTES_OFFSET = 64,
  BINARY_MODULE_IDENTITY_REF_OFFSET = 72,
};

static void set_node(ZProgramGraphNode *node, const char *id, ZProgramGraphNodeKind kind, const char *name, const char *type) {
  node->id = z_strdup(id);
  node->kind = kind;
  node->name = name ? z_strdup(name) : NULL;
  node->type = type ? z_strdup(type) : NULL;
  node->path = z_strdup("program-graph-smoke.0");
  node->line = 1;
  node->column = 1;
}

static void set_edge(ZProgramGraphEdge *edge, const char *from, const char *to, const char *kind, ZProgramGraphEdgeTarget target, size_t order) {
  edge->from = z_strdup(from);
  edge->to = z_strdup(to);
  edge->kind = z_strdup(kind);
  edge->target = target;
  edge->order = order;
}

static void expect_binary_store_corruption_rejected(const ZProgramGraph *graph) {
  ZProgramGraphStore projections;
  z_program_graph_store_init(&projections);
  projections.projection_paths = z_checked_calloc(1, sizeof(char *));
  projections.projection_texts = z_checked_calloc(1, sizeof(char *));
  projections.projection_cap = 1;
  projections.projection_len = 1;
  projections.projection_paths[0] = z_strdup("program-graph-smoke.0");
  projections.projection_texts[0] = z_strdup("pub fn main(world: World) -> Void raises {\n}\n");

  ZBuf binary;
  zbuf_init(&binary);
  z_program_graph_store_append_binary(&binary, graph, &projections);
  expect(z_program_graph_store_bytes_are_binary((const unsigned char *)binary.data, binary.len), "binary store fixture did not use binary magic");

  ZProgramGraphStore parsed;
  ZDiag valid_diag = {0};
  expect(z_program_graph_store_parse_binary("valid-zero.graph", (const unsigned char *)binary.data, binary.len, &parsed, &valid_diag), valid_diag.message);
  z_program_graph_store_free(&parsed);

  uint64_t string_bytes = read_le_u64((const unsigned char *)binary.data, BINARY_STRING_BYTES_OFFSET, binary.len);
  uint64_t module_identity_offset = read_le_u64((const unsigned char *)binary.data, BINARY_MODULE_IDENTITY_REF_OFFSET, binary.len);
  uint64_t module_identity_len = read_le_u64((const unsigned char *)binary.data, BINARY_MODULE_IDENTITY_REF_OFFSET + 8, binary.len);
  expect(string_bytes <= (uint64_t)binary.len, "binary fixture has invalid string table size");
  expect(module_identity_len > 2, "binary fixture module identity too small to corrupt");
  size_t strings_offset = binary.len - (size_t)string_bytes;
  expect(module_identity_offset + 1 < string_bytes, "binary fixture module identity offset out of range");

  unsigned char *embedded_nul = copy_binary_fixture(&binary);
  embedded_nul[strings_offset + (size_t)module_identity_offset + 1] = 0;
  expect_binary_store_rejected(embedded_nul, binary.len, "binary store with embedded NUL parsed");
  free(embedded_nul);

  unsigned char *too_many_sources = copy_binary_fixture(&binary);
  write_le_u64(too_many_sources, BINARY_SOURCE_COUNT_OFFSET, binary.len, 100001u);
  expect_binary_store_rejected(too_many_sources, binary.len, "binary store with oversized source count parsed");
  free(too_many_sources);

  unsigned char *too_many_projections = copy_binary_fixture(&binary);
  write_le_u64(too_many_projections, BINARY_PROJECTION_COUNT_OFFSET, binary.len, 100001u);
  expect_binary_store_rejected(too_many_projections, binary.len, "binary store with oversized projection count parsed");
  free(too_many_projections);

  unsigned char *too_many_nodes = copy_binary_fixture(&binary);
  write_le_u64(too_many_nodes, BINARY_NODE_COUNT_OFFSET, binary.len, 1000001u);
  expect_binary_store_rejected(too_many_nodes, binary.len, "binary store with oversized node count parsed");
  free(too_many_nodes);

  unsigned char *too_many_edges = copy_binary_fixture(&binary);
  write_le_u64(too_many_edges, BINARY_EDGE_COUNT_OFFSET, binary.len, 4000001u);
  expect_binary_store_rejected(too_many_edges, binary.len, "binary store with oversized edge count parsed");
  free(too_many_edges);

  unsigned char *too_many_string_bytes = copy_binary_fixture(&binary);
  write_le_u64(too_many_string_bytes, BINARY_STRING_BYTES_OFFSET, binary.len, 256u * 1024u * 1024u + 1u);
  expect_binary_store_rejected(too_many_string_bytes, binary.len, "binary store with oversized string table parsed");
  free(too_many_string_bytes);

  zbuf_free(&binary);
  z_program_graph_store_free(&projections);
}

static void expect_binary_store_rejects_user_std_basename_without_projection(void) {
  ZProgramGraph graph;
  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(1, sizeof(ZProgramGraphNode));
  graph.node_len = 1;
  graph.node_cap = 1;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "term", NULL);
  free(graph.nodes[0].path);
  graph.nodes[0].path = z_strdup("src/term.0");
  z_program_graph_finalize_identities(&graph);

  ZBuf binary;
  zbuf_init(&binary);
  z_program_graph_store_append_binary(&binary, &graph, NULL);
  ZProgramGraphStore parsed;
  ZDiag diag = {0};
  expect(!z_program_graph_store_parse_binary("user-std-basename.graph", (const unsigned char *)binary.data, binary.len, &parsed, &diag),
         "binary store accepted user source path matching std basename without projection");
  expect(strstr(diag.message, "projection table") != NULL || strstr(diag.message, "source table") != NULL,
         "binary store basename collision reported wrong diagnostic");

  zbuf_free(&binary);
  z_program_graph_free(&graph);
}

static void expect_binary_fast_sync_rejects_empty_projection_table(const ZProgramGraph *graph) {
  ZBuf binary;
  zbuf_init(&binary);
  z_program_graph_store_append_binary(&binary, graph, NULL);

  char storage_path[128];
  snprintf(storage_path, sizeof(storage_path), "/tmp/zero-program-graph-empty-projection-%p.graph", (void *)graph);
  ZDiag write_diag = {0};
  expect(z_write_binary_file(storage_path, (const unsigned char *)binary.data, binary.len, &write_diag), write_diag.message);

  ZProgramGraphProjectionSourceSync sync = Z_PROGRAM_GRAPH_PROJECTION_SYNC_DIVERGED;
  expect(!z_program_graph_projection_source_sync_state_binary_fast(storage_path, ".", &sync),
         "binary fast sync accepted store with empty projection table");

  remove(storage_path);
  zbuf_free(&binary);
}

static void expect_lowered_program(void) {
  ZProgramGraph graph;
  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(3, sizeof(ZProgramGraphNode));
  graph.node_len = 3;
  graph.node_cap = 3;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "main", "Void");
  graph.nodes[1].is_public = true;
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  graph.edges = z_checked_calloc(2, sizeof(ZProgramGraphEdge));
  graph.edge_len = 2;
  graph.edge_cap = 2;
  set_edge(&graph.edges[0], "#000001", "#000002", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  z_program_graph_finalize_identities(&graph);

  ZProgramGraphValidation validation = {0};
  expect(z_program_graph_validate(&graph, &validation), "lowerable graph failed validation");
  Program program = {0};
  ZDiag diag = {0};
  expect(z_program_graph_lower_to_program(&graph, &program, &diag), diag.message);
  expect(program.functions.len == 1, "lowered graph reported wrong function count");
  expect(strcmp(program.functions.items[0].name, "main") == 0, "lowered function kept wrong name");
  expect(strcmp(program.functions.items[0].return_type, "Void") == 0, "lowered function kept wrong return type");
  expect(program.functions.items[0].is_public, "lowered function lost public flag");
  expect(program.functions.items[0].body.len == 0, "lowered empty body gained statements");
  z_free_program(&program);
  z_program_graph_free(&graph);
}

static void expect_import_lowering_failure(const char *module, const char *alias, const char *message) {
  ZProgramGraph graph;
  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(4, sizeof(ZProgramGraphNode));
  graph.node_len = 4;
  graph.node_cap = 4;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_IMPORT, module, NULL);
  graph.nodes[1].value = alias ? z_strdup(alias) : NULL;
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_FUNCTION, "main", "Void");
  graph.nodes[2].is_public = true;
  set_node(&graph.nodes[3], "#000004", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  graph.edges = z_checked_calloc(3, sizeof(ZProgramGraphEdge));
  graph.edge_len = 3;
  graph.edge_cap = 3;
  set_edge(&graph.edges[0], "#000001", "#000002", "import", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000001", "#000003", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[2], "#000003", "#000004", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  z_program_graph_finalize_identities(&graph);

  ZProgramGraphValidation validation = {0};
  expect(z_program_graph_validate(&graph, &validation), "invalid import test graph should remain shape-valid");
  Program program = {0};
  ZDiag diag = {0};
  expect(!z_program_graph_lower_to_program(&graph, &program, &diag), "invalid import graph lowered successfully");
  expect(strstr(diag.message, message) != NULL, "invalid import graph reported wrong diagnostic");
  z_program_graph_free(&graph);
}

static void expect_lower_rejects_bad_imports(void) {
  expect_import_lowering_failure("+", NULL, "import module");
  expect_import_lowering_failure("pub", NULL, "import module");
  expect_import_lowering_failure("missing", NULL, "target module is missing");
  expect_import_lowering_failure("std.mem", "+", "import alias");
  expect_import_lowering_failure("std.mem", "pub", "import alias");
}

static void expect_lower_rejects_reserved_function_name(void) {
  ZProgramGraph graph;
  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(3, sizeof(ZProgramGraphNode));
  graph.node_len = 3;
  graph.node_cap = 3;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "pub", "Void");
  graph.nodes[1].is_public = true;
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  graph.edges = z_checked_calloc(2, sizeof(ZProgramGraphEdge));
  graph.edge_len = 2;
  graph.edge_cap = 2;
  set_edge(&graph.edges[0], "#000001", "#000002", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  z_program_graph_finalize_identities(&graph);

  ZProgramGraphValidation validation = {0};
  expect(z_program_graph_validate(&graph, &validation), "reserved function name graph should remain shape-valid");
  Program program = {0};
  ZDiag diag = {0};
  expect(!z_program_graph_lower_to_program(&graph, &program, &diag), "reserved function name lowered successfully");
  expect(strstr(diag.message, "function name") != NULL, "reserved function name reported wrong diagnostic");
  z_program_graph_free(&graph);
}

static void expect_lower_rejects_internal_function_name(void) {
  ZProgramGraph graph;
  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(3, sizeof(ZProgramGraphNode));
  graph.node_len = 3;
  graph.node_cap = 3;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "__zero_bad", "Void");
  graph.nodes[1].is_public = true;
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  graph.edges = z_checked_calloc(2, sizeof(ZProgramGraphEdge));
  graph.edge_len = 2;
  graph.edge_cap = 2;
  set_edge(&graph.edges[0], "#000001", "#000002", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  z_program_graph_finalize_identities(&graph);

  ZProgramGraphValidation validation = {0};
  expect(z_program_graph_validate(&graph, &validation), "internal function name graph should remain shape-valid");
  Program program = {0};
  ZDiag diag = {0};
  expect(!z_program_graph_lower_to_program(&graph, &program, &diag), "internal function name lowered successfully");
  expect(strstr(diag.message, "reserved compiler-internal") != NULL, "internal function name reported wrong diagnostic");
  z_program_graph_free(&graph);
}

static void expect_lower_allows_embedded_std_internal_function_name(void) {
  ZProgramGraph graph;
  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(3, sizeof(ZProgramGraphNode));
  graph.node_len = 3;
  graph.node_cap = 3;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "std.math", NULL);
  free(graph.nodes[0].path);
  graph.nodes[0].path = z_strdup("std/math.0");
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "__zero_std_math_local", "Void");
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  graph.edges = z_checked_calloc(2, sizeof(ZProgramGraphEdge));
  graph.edge_len = 2;
  graph.edge_cap = 2;
  set_edge(&graph.edges[0], "#000001", "#000002", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  z_program_graph_finalize_identities(&graph);

  ZProgramGraphValidation validation = {0};
  expect(z_program_graph_validate(&graph, &validation), "embedded std internal function graph should remain shape-valid");
  Program program = {0};
  ZDiag diag = {0};
  expect(z_program_graph_lower_to_program(&graph, &program, &diag), diag.message);
  expect(program.functions.len == 1, "embedded std internal function was not lowered");
  expect(strcmp(program.functions.items[0].name, "__zero_std_math_local") == 0, "embedded std internal function name changed");
  z_free_program(&program);
  z_program_graph_free(&graph);
}

static void expect_lower_rejects_reserved_call_name(void) {
  ZProgramGraph graph;
  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(5, sizeof(ZProgramGraphNode));
  graph.node_len = 5;
  graph.node_cap = 5;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "main", "Void");
  graph.nodes[1].is_public = true;
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  set_node(&graph.nodes[3], "#000004", Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT, NULL, NULL);
  set_node(&graph.nodes[4], "#000005", Z_PROGRAM_GRAPH_NODE_CALL, "pub", NULL);
  graph.edges = z_checked_calloc(4, sizeof(ZProgramGraphEdge));
  graph.edge_len = 4;
  graph.edge_cap = 4;
  set_edge(&graph.edges[0], "#000001", "#000002", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[2], "#000003", "#000004", "statement", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[3], "#000004", "#000005", "expr", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  z_program_graph_finalize_identities(&graph);

  ZProgramGraphValidation validation = {0};
  expect(z_program_graph_validate(&graph, &validation), "reserved call name graph should remain shape-valid");
  Program program = {0};
  ZDiag diag = {0};
  expect(!z_program_graph_lower_to_program(&graph, &program, &diag), "reserved call name lowered successfully");
  expect(strstr(diag.message, "call callee") != NULL, "reserved call name reported wrong diagnostic");
  z_program_graph_free(&graph);
}

static void expect_validation_code(ZProgramGraph *graph, const char *code, const char *message) {
  z_program_graph_finalize_identities(graph);
  ZProgramGraphValidation validation = {0};
  expect(!z_program_graph_validate(graph, &validation), message);
  expect(strcmp(validation.code, code) == 0, "validation reported wrong code");
}

static void expect_validation_allows_unconstrained_type_params(void) {
  ZProgramGraph graph;
  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(5, sizeof(ZProgramGraphNode));
  graph.node_len = 5;
  graph.node_cap = 5;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_SHAPE, "Box", NULL);
  graph.nodes[1].value = z_strdup("auto");
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_PARAM, "T", NULL);
  set_node(&graph.nodes[3], "#000004", Z_PROGRAM_GRAPH_NODE_FIELD, "value", "T");
  set_node(&graph.nodes[4], "#000005", Z_PROGRAM_GRAPH_NODE_TYPE_REF, NULL, "T");
  graph.edges = z_checked_calloc(4, sizeof(ZProgramGraphEdge));
  graph.edge_len = 4;
  graph.edge_cap = 4;
  set_edge(&graph.edges[0], "#000001", "#000002", "shape", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "typeParam", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[2], "#000002", "#000004", "field", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[3], "#000004", "#000005", "type", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  z_program_graph_finalize_identities(&graph);

  ZProgramGraphValidation validation = {0};
  expect(z_program_graph_validate(&graph, &validation), "unconstrained type param failed validation");
  z_program_graph_free(&graph);
}

static void expect_validation_rejects_malformed_graphs(void) {
  ZProgramGraph graph;
  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(4, sizeof(ZProgramGraphNode));
  graph.node_len = 4;
  graph.node_cap = 4;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "main", "Void");
  graph.nodes[1].is_public = true;
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  set_node(&graph.nodes[3], "#000004", Z_PROGRAM_GRAPH_NODE_CHECK, NULL, NULL);
  graph.edges = z_checked_calloc(4, sizeof(ZProgramGraphEdge));
  graph.edge_len = 4;
  graph.edge_cap = 4;
  set_edge(&graph.edges[0], "#000001", "#000002", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[2], "#000003", "#000004", "statement", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[3], "#000003", "#000004", "statement", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 2);
  expect_validation_code(&graph, "GRF013", "sparse ordered statement edges validated");
  z_program_graph_free(&graph);

  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(3, sizeof(ZProgramGraphNode));
  graph.node_len = 3;
  graph.node_cap = 3;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "main", "Void");
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  graph.edges = z_checked_calloc(2, sizeof(ZProgramGraphEdge));
  graph.edge_len = 2;
  graph.edge_cap = 2;
  set_edge(&graph.edges[0], "#000001", "#000002", "statement", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  expect_validation_code(&graph, "GRF012", "invalid module child edge validated");
  z_program_graph_free(&graph);

  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(2, sizeof(ZProgramGraphNode));
  graph.node_len = 2;
  graph.node_cap = 2;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "main", "Void");
  graph.edges = z_checked_calloc(1, sizeof(ZProgramGraphEdge));
  graph.edge_len = 1;
  graph.edge_cap = 1;
  set_edge(&graph.edges[0], "#000001", "#000002", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  expect_validation_code(&graph, "GRF016", "function without body validated");
  z_program_graph_free(&graph);

  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(4, sizeof(ZProgramGraphNode));
  graph.node_len = 4;
  graph.node_cap = 4;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "main", "Void");
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_PARAM, "world", NULL);
  set_node(&graph.nodes[3], "#000004", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  graph.edges = z_checked_calloc(3, sizeof(ZProgramGraphEdge));
  graph.edge_len = 3;
  graph.edge_cap = 3;
  set_edge(&graph.edges[0], "#000001", "#000002", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "param", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[2], "#000002", "#000004", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  expect_validation_code(&graph, "GRF014", "value param without type validated");
  z_program_graph_free(&graph);

  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(1, sizeof(ZProgramGraphNode));
  graph.node_len = 1;
  graph.node_cap = 1;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_LITERAL, NULL, NULL);
  expect_validation_code(&graph, "GRF014", "literal without value validated");
  z_program_graph_free(&graph);

  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(5, sizeof(ZProgramGraphNode));
  graph.node_len = 5;
  graph.node_cap = 5;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "main", "Void");
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  set_node(&graph.nodes[3], "#000004", Z_PROGRAM_GRAPH_NODE_CHECK, NULL, NULL);
  set_node(&graph.nodes[4], "#000005", Z_PROGRAM_GRAPH_NODE_LITERAL, NULL, NULL);
  graph.nodes[4].value = z_strdup("true");
  graph.edges = z_checked_calloc(4, sizeof(ZProgramGraphEdge));
  graph.edge_len = 4;
  graph.edge_cap = 4;
  set_edge(&graph.edges[0], "#000001", "#000002", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[2], "#000003", "#000004", "statement", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[3], "#000004", "#000005", "left", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  expect_validation_code(&graph, "GRF016", "statement check with left edge validated");
  z_program_graph_free(&graph);

  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(6, sizeof(ZProgramGraphNode));
  graph.node_len = 6;
  graph.node_cap = 6;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "main", "Void");
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  set_node(&graph.nodes[3], "#000004", Z_PROGRAM_GRAPH_NODE_LET, "value", NULL);
  set_node(&graph.nodes[4], "#000005", Z_PROGRAM_GRAPH_NODE_CHECK, NULL, NULL);
  set_node(&graph.nodes[5], "#000006", Z_PROGRAM_GRAPH_NODE_LITERAL, NULL, NULL);
  graph.nodes[5].value = z_strdup("true");
  graph.edges = z_checked_calloc(5, sizeof(ZProgramGraphEdge));
  graph.edge_len = 5;
  graph.edge_cap = 5;
  set_edge(&graph.edges[0], "#000001", "#000002", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[2], "#000003", "#000004", "statement", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[3], "#000004", "#000005", "expr", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[4], "#000005", "#000006", "expr", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  expect_validation_code(&graph, "GRF016", "expression check with expr edge validated");
  z_program_graph_free(&graph);

  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(7, sizeof(ZProgramGraphNode));
  graph.node_len = 7;
  graph.node_cap = 7;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "main", "Void");
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  set_node(&graph.nodes[3], "#000004", Z_PROGRAM_GRAPH_NODE_CHECK, NULL, NULL);
  set_node(&graph.nodes[4], "#000005", Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS, NULL, NULL);
  set_node(&graph.nodes[5], "#000006", Z_PROGRAM_GRAPH_NODE_IDENTIFIER, "world", NULL);
  set_node(&graph.nodes[6], "#000007", Z_PROGRAM_GRAPH_NODE_LITERAL, NULL, NULL);
  graph.nodes[6].value = z_strdup("0");
  graph.edges = z_checked_calloc(6, sizeof(ZProgramGraphEdge));
  graph.edge_len = 6;
  graph.edge_cap = 6;
  set_edge(&graph.edges[0], "#000001", "#000002", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[2], "#000003", "#000004", "statement", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[3], "#000004", "#000005", "expr", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[4], "#000005", "#000006", "left", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[5], "#000005", "#000007", "right", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  expect_validation_code(&graph, "GRF016", "right edge at order zero validated");
  z_program_graph_free(&graph);

  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(5, sizeof(ZProgramGraphNode));
  graph.node_len = 5;
  graph.node_cap = 5;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "main", "u8");
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  set_node(&graph.nodes[3], "#000004", Z_PROGRAM_GRAPH_NODE_RETURN, NULL, NULL);
  set_node(&graph.nodes[4], "#000005", Z_PROGRAM_GRAPH_NODE_RESCUE, NULL, "u8");
  graph.edges = z_checked_calloc(3, sizeof(ZProgramGraphEdge));
  graph.edge_len = 3;
  graph.edge_cap = 3;
  set_edge(&graph.edges[0], "#000001", "#000002", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[2], "#000003", "#000004", "statement", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  expect_validation_code(&graph, "GRF014", "rescue without binding name validated");
  z_program_graph_free(&graph);

  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(5, sizeof(ZProgramGraphNode));
  graph.node_len = 5;
  graph.node_cap = 5;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "main", "u8");
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  set_node(&graph.nodes[3], "#000004", Z_PROGRAM_GRAPH_NODE_RETURN, NULL, NULL);
  set_node(&graph.nodes[4], "#000005", Z_PROGRAM_GRAPH_NODE_CAST, NULL, NULL);
  graph.edges = z_checked_calloc(3, sizeof(ZProgramGraphEdge));
  graph.edge_len = 3;
  graph.edge_cap = 3;
  set_edge(&graph.edges[0], "#000001", "#000002", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[2], "#000003", "#000004", "statement", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  expect_validation_code(&graph, "GRF014", "cast without target type validated");
  z_program_graph_free(&graph);

  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(7, sizeof(ZProgramGraphNode));
  graph.node_len = 7;
  graph.node_cap = 7;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "main", "Void");
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  set_node(&graph.nodes[3], "#000004", Z_PROGRAM_GRAPH_NODE_RETURN, NULL, NULL);
  set_node(&graph.nodes[4], "#000005", Z_PROGRAM_GRAPH_NODE_SLICE, NULL, NULL);
  set_node(&graph.nodes[5], "#000006", Z_PROGRAM_GRAPH_NODE_IDENTIFIER, "bytes", NULL);
  set_node(&graph.nodes[6], "#000007", Z_PROGRAM_GRAPH_NODE_LITERAL, NULL, NULL);
  graph.nodes[6].value = z_strdup("1");
  graph.edges = z_checked_calloc(6, sizeof(ZProgramGraphEdge));
  graph.edge_len = 6;
  graph.edge_cap = 6;
  set_edge(&graph.edges[0], "#000001", "#000002", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[2], "#000003", "#000004", "statement", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[3], "#000004", "#000005", "expr", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[4], "#000005", "#000006", "left", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  set_edge(&graph.edges[5], "#000005", "#000007", "arg", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 2);
  expect_validation_code(&graph, "GRF016", "slice arg outside supported order range validated");
  z_program_graph_free(&graph);
}

int main(void) {
  expect_lowered_program();
  expect_lower_rejects_bad_imports();
  expect_lower_rejects_reserved_function_name();
  expect_lower_rejects_internal_function_name();
  expect_lower_allows_embedded_std_internal_function_name();
  expect_lower_rejects_reserved_call_name();
  expect_validation_allows_unconstrained_type_params();
  expect_validation_rejects_malformed_graphs();

  ZProgramGraph graph;
  z_program_graph_init(&graph);
  graph.nodes = z_checked_calloc(3, sizeof(ZProgramGraphNode));
  graph.node_len = 3;
  graph.node_cap = 3;
  set_node(&graph.nodes[0], "#000001", Z_PROGRAM_GRAPH_NODE_MODULE, "smoke", NULL);
  set_node(&graph.nodes[1], "#000002", Z_PROGRAM_GRAPH_NODE_FUNCTION, "main", "Void");
  set_node(&graph.nodes[2], "#000003", Z_PROGRAM_GRAPH_NODE_BLOCK, "body", NULL);
  graph.edges = z_checked_calloc(2, sizeof(ZProgramGraphEdge));
  graph.edge_len = 2;
  graph.edge_cap = 2;
  set_edge(&graph.edges[0], "#000001", "symbol:missing", "function", Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL, 0);
  set_edge(&graph.edges[1], "#000002", "#000003", "body", Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, 0);
  z_program_graph_finalize_identities(&graph);

  ZProgramGraphValidation validation = {0};
  expect(!z_program_graph_validate(&graph, &validation), "missing symbol target validated");
  expect(strcmp(validation.code, "GRF005") == 0, "missing symbol target reported wrong code");
  expect(strcmp(validation.edge_target, "symbol") == 0, "missing symbol target reported wrong domain");

  free(graph.edges[0].to);
  graph.edges[0].to = z_strdup(graph.nodes[1].symbol_id);
  z_program_graph_finalize_identities(&graph);
  expect(z_program_graph_validate(&graph, &validation), "valid symbol target failed validation");
  expect(validation.state == Z_PROGRAM_GRAPH_VALIDATION_SHAPE_VALID, "valid graph reported wrong state");
  expect_binary_store_corruption_rejected(&graph);
  expect_binary_store_rejects_user_std_basename_without_projection();
  expect_binary_fast_sync_rejects_empty_projection_table(&graph);

  ZBuf dump;
  zbuf_init(&dump);
  z_program_graph_append_dump(&dump, &graph, &validation);
  ZProgramGraph parsed;
  ZDiag diag = {0};
  expect(z_program_graph_parse_dump(dump.data, &parsed, &diag), diag.message);
  ZProgramGraphValidation parsed_validation = {0};
  expect(z_program_graph_validate(&parsed, &parsed_validation), "parsed graph failed validation");
  ZBuf redump;
  zbuf_init(&redump);
  z_program_graph_append_dump(&redump, &parsed, &parsed_validation);
  expect(strcmp(dump.data, redump.data) == 0, "parsed graph dump was not byte-stable");

  char storage_path[128];
  snprintf(storage_path, sizeof(storage_path), "/tmp/zero-program-graph-smoke-%p.graph", (void *)&graph);
  ZDiag storage_diag = {0};
  expect(z_program_graph_save(storage_path, &graph, &storage_diag), storage_diag.message);
  ZProgramGraph loaded;
  expect(z_program_graph_load(storage_path, &loaded, &storage_diag), storage_diag.message);
  ZProgramGraphValidation loaded_validation = {0};
  expect(z_program_graph_validate(&loaded, &loaded_validation), "loaded graph failed validation");
  ZBuf loaded_dump;
  zbuf_init(&loaded_dump);
  z_program_graph_append_dump(&loaded_dump, &loaded, &loaded_validation);
  expect(strcmp(dump.data, loaded_dump.data) == 0, "loaded graph dump was not byte-stable");
  zbuf_free(&loaded_dump);
  z_program_graph_free(&loaded);
  remove(storage_path);

  char *corrupted = z_strdup(dump.data);
  char *corrupt_name = strstr(corrupted, "name:\"main\"");
  expect(corrupt_name != NULL, "expected main function name in graph dump");
  memcpy(corrupt_name, "name:\"fail\"", strlen("name:\"fail\""));
  ZProgramGraph rejected;
  ZDiag rejected_diag = {0};
  expect(!z_program_graph_parse_dump(corrupted, &rejected, &rejected_diag), "corrupt graph content parsed");
  expect(strstr(rejected_diag.message, "identities") != NULL, "corrupt graph content reported wrong diagnostic");
  free(corrupted);

  ZProgramGraph wrong_schema;
  ZDiag wrong_schema_diag = {0};
  expect(!z_program_graph_parse_dump("zero-graph v2\n", &wrong_schema, &wrong_schema_diag), "unknown schema parsed");
  expect(strstr(wrong_schema_diag.message, "schema") != NULL, "unknown schema reported wrong diagnostic");

  const char *failed_dump =
      "zero-graph v1\n"
      "origin source-text\n"
      "module \"main\"\n"
      "hash \"\"\n"
      "validation \"decoded\" failed\n"
      "diagnostic code:\"GRF001\" message:\"program graph construction failed\"\n";
  ZProgramGraph failed_artifact;
  ZDiag failed_artifact_diag = {0};
  expect(!z_program_graph_parse_dump(failed_dump, &failed_artifact, &failed_artifact_diag), "failed validation artifact parsed");
  expect(strstr(failed_artifact_diag.message, "failed validation") != NULL, "failed validation artifact reported wrong diagnostic");

  ZBuf trailing_extra;
  zbuf_init(&trailing_extra);
  zbuf_append(&trailing_extra, dump.data);
  zbuf_append(&trailing_extra, "\nextra\n");
  ZProgramGraph trailing_artifact;
  ZDiag trailing_artifact_diag = {0};
  expect(!z_program_graph_parse_dump(trailing_extra.data, &trailing_artifact, &trailing_artifact_diag), "trailing content parsed");
  expect(strstr(trailing_artifact_diag.message, "unexpected content") != NULL, "trailing content reported wrong diagnostic");

  zbuf_free(&trailing_extra);
  zbuf_free(&redump);
  z_program_graph_free(&parsed);
  zbuf_free(&dump);
  z_program_graph_free(&graph);
  puts("program graph smoke ok");
  return 0;
}
