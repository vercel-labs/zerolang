#include "program_graph_format.h"
#include "program_graph_import.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool graph_format_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static void graph_format_append_quoted(ZBuf *buf, const char *text) {
  zbuf_append_char(buf, '"');
  for (const char *p = text ? text : ""; *p; p++) {
    unsigned char ch = (unsigned char)*p;
    switch (ch) {
      case '\\': zbuf_append(buf, "\\\\"); break;
      case '"': zbuf_append(buf, "\\\""); break;
      case '\n': zbuf_append(buf, "\\n"); break;
      case '\r': zbuf_append(buf, "\\r"); break;
      case '\t': zbuf_append(buf, "\\t"); break;
      default:
        if (ch < 0x20) {
          const char *hex = "0123456789abcdef";
          char escape[7] = {'\\', 'u', '0', '0', hex[ch >> 4], hex[ch & 0x0f], 0};
          zbuf_append(buf, escape);
        } else {
          zbuf_append_char(buf, (char)ch);
        }
        break;
    }
  }
  zbuf_append_char(buf, '"');
}

static void graph_format_append_validation_json(ZBuf *buf, const ZProgramGraphValidation *validation) {
  bool ok = !validation || validation->ok;
  zbuf_append(buf, "{\"state\":");
  graph_format_append_quoted(buf, z_program_graph_validation_state_name(validation ? validation->state : Z_PROGRAM_GRAPH_VALIDATION_SHAPE_VALID));
  zbuf_appendf(buf, ",\"ok\":%s,\"diagnostics\":[", ok ? "true" : "false");
  if (!ok) {
    zbuf_append(buf, "{\"code\":");
    graph_format_append_quoted(buf, validation->code);
    zbuf_append(buf, ",\"message\":");
    graph_format_append_quoted(buf, validation->message);
    zbuf_append(buf, ",\"node\":");
    graph_format_append_quoted(buf, validation->node_id);
    zbuf_append(buf, ",\"edge\":{\"from\":");
    graph_format_append_quoted(buf, validation->edge_from);
    zbuf_append(buf, ",\"to\":");
    graph_format_append_quoted(buf, validation->edge_to);
    zbuf_append(buf, ",\"target\":");
    graph_format_append_quoted(buf, validation->edge_target);
    zbuf_append(buf, "}}");
  }
  zbuf_append(buf, "]}");
}

static void graph_format_append_validation_dump(ZBuf *buf, const ZProgramGraphValidation *validation) {
  bool ok = !validation || validation->ok;
  zbuf_append(buf, "validation ");
  graph_format_append_quoted(buf, z_program_graph_validation_state_name(validation ? validation->state : Z_PROGRAM_GRAPH_VALIDATION_SHAPE_VALID));
  zbuf_appendf(buf, " %s\n", ok ? "ok" : "failed");
  if (!ok) {
    zbuf_append(buf, "diagnostic code=");
    graph_format_append_quoted(buf, validation->code);
    zbuf_append(buf, " message=");
    graph_format_append_quoted(buf, validation->message);
    if (validation->node_id[0]) {
      zbuf_append(buf, " node=");
      graph_format_append_quoted(buf, validation->node_id);
    }
    if (validation->edge_from[0] || validation->edge_to[0]) {
      zbuf_append(buf, " edgeFrom=");
      graph_format_append_quoted(buf, validation->edge_from);
      zbuf_append(buf, " edgeTo=");
      graph_format_append_quoted(buf, validation->edge_to);
      zbuf_append(buf, " edgeTarget=");
      graph_format_append_quoted(buf, validation->edge_target);
    }
    zbuf_append_char(buf, '\n');
  }
}

static void graph_format_append_failed_json(ZBuf *buf) {
  zbuf_append(buf, "{\"schemaVersion\":1,\"canonicalSource\":false,\"idStrategy\":\"deterministic-traversal-r0\",\"moduleIdentity\":\"module:main\",\"graphHash\":\"\",\"validation\":{\"state\":\"decoded\",\"ok\":false,\"diagnostics\":[{\"code\":\"GRF001\",\"message\":\"program graph construction failed\"}]},\"counts\":{\"nodes\":0,\"edges\":0},\"nodes\":[],\"edges\":[]}");
}

static void graph_format_append_failed_dump(ZBuf *buf) {
  zbuf_append(buf, "zero-program-graph v1\n");
  zbuf_append(buf, "canonicalSource false\n");
  zbuf_append(buf, "idStrategy \"deterministic-traversal-r0\"\n");
  zbuf_append(buf, "moduleIdentity \"module:main\"\n");
  zbuf_append(buf, "graphHash \"\"\n");
  zbuf_append(buf, "validation \"decoded\" failed\n");
  zbuf_append(buf, "diagnostic code=\"GRF001\" message=\"program graph construction failed\"\n");
  zbuf_append(buf, "counts nodes=0 edges=0\n");
}

static bool graph_format_parse_fail(ZDiag *diag, size_t line, const char *message) {
  if (diag) {
    diag->code = 1;
    diag->path = "program graph dump";
    diag->line = (int)(line ? line : 1);
    diag->column = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "invalid program graph dump");
  }
  return false;
}

static bool graph_format_next_line(const char **cursor, char **out) {
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

static bool graph_format_parse_literal(const char **cursor, const char *literal) {
  size_t len = strlen(literal);
  if (strncmp(*cursor, literal, len) != 0) return false;
  *cursor += len;
  return true;
}

static int graph_format_hex_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

static bool graph_format_parse_quoted(const char **cursor, char **out) {
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
        int high = graph_format_hex_value((*cursor)[3]);
        int low = graph_format_hex_value((*cursor)[4]);
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

static bool graph_format_parse_size(const char **cursor, size_t *out) {
  if (!isdigit((unsigned char)**cursor)) return false;
  size_t value = 0;
  while (isdigit((unsigned char)**cursor)) {
    unsigned digit = (unsigned)(**cursor - '0');
    if (value > (SIZE_MAX - digit) / 10) return false;
    value = value * 10 + digit;
    (*cursor)++;
  }
  *out = value;
  return true;
}

static bool graph_format_parse_int(const char **cursor, int *out) {
  size_t value = 0;
  if (!graph_format_parse_size(cursor, &value) || value > INT_MAX) return false;
  *out = (int)value;
  return true;
}

static bool graph_format_parse_bool(const char **cursor, bool *out) {
  if (strncmp(*cursor, "true", 4) == 0) {
    *cursor += 4;
    *out = true;
    return true;
  }
  if (strncmp(*cursor, "false", 5) == 0) {
    *cursor += 5;
    *out = false;
    return true;
  }
  return false;
}

static bool graph_format_node_kind_from_name(const char *name, ZProgramGraphNodeKind *out) {
  for (int kind = Z_PROGRAM_GRAPH_NODE_MODULE; kind <= Z_PROGRAM_GRAPH_NODE_STATEMENT; kind++) {
    if (graph_format_text_eq(z_program_graph_node_kind_name((ZProgramGraphNodeKind)kind), name)) {
      *out = (ZProgramGraphNodeKind)kind;
      return true;
    }
  }
  return false;
}

static bool graph_format_edge_target_from_name(const char *name, ZProgramGraphEdgeTarget *out) {
  for (int target = Z_PROGRAM_GRAPH_EDGE_TARGET_NODE; target <= Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT; target++) {
    if (graph_format_text_eq(z_program_graph_edge_target_name((ZProgramGraphEdgeTarget)target), name)) {
      *out = (ZProgramGraphEdgeTarget)target;
      return true;
    }
  }
  return false;
}

static bool graph_format_validation_state_from_name(const char *name, ZProgramGraphValidationState *out) {
  for (int state = Z_PROGRAM_GRAPH_VALIDATION_DECODED; state <= Z_PROGRAM_GRAPH_VALIDATION_BUILDABLE; state++) {
    if (graph_format_text_eq(z_program_graph_validation_state_name((ZProgramGraphValidationState)state), name)) {
      *out = (ZProgramGraphValidationState)state;
      return true;
    }
  }
  return false;
}

static void graph_format_free_node_fields(ZProgramGraphNode *node) {
  free(node->id);
  free(node->name);
  free(node->type);
  free(node->value);
  free(node->path);
  free(node->symbol_id);
  free(node->type_id);
  free(node->effect_id);
  free(node->node_hash);
  *node = (ZProgramGraphNode){0};
}

static void graph_format_free_edge_fields(ZProgramGraphEdge *edge) {
  free(edge->from);
  free(edge->to);
  free(edge->kind);
  *edge = (ZProgramGraphEdge){0};
}

static bool graph_format_parse_node_line(const char *line, ZProgramGraphNode *out) {
  ZProgramGraphNode node = {0};
  char *kind_name = NULL;
  const char *cursor = line;
  bool ok = graph_format_parse_literal(&cursor, "node id=") &&
            graph_format_parse_quoted(&cursor, &node.id) &&
            graph_format_parse_literal(&cursor, " kind=") &&
            graph_format_parse_quoted(&cursor, &kind_name) &&
            graph_format_node_kind_from_name(kind_name, &node.kind) &&
            graph_format_parse_literal(&cursor, " name=") &&
            graph_format_parse_quoted(&cursor, &node.name) &&
            graph_format_parse_literal(&cursor, " type=") &&
            graph_format_parse_quoted(&cursor, &node.type) &&
            graph_format_parse_literal(&cursor, " value=") &&
            graph_format_parse_quoted(&cursor, &node.value) &&
            graph_format_parse_literal(&cursor, " symbolId=") &&
            graph_format_parse_quoted(&cursor, &node.symbol_id) &&
            graph_format_parse_literal(&cursor, " typeId=") &&
            graph_format_parse_quoted(&cursor, &node.type_id) &&
            graph_format_parse_literal(&cursor, " effectId=") &&
            graph_format_parse_quoted(&cursor, &node.effect_id) &&
            graph_format_parse_literal(&cursor, " nodeHash=") &&
            graph_format_parse_quoted(&cursor, &node.node_hash) &&
            graph_format_parse_literal(&cursor, " path=") &&
            graph_format_parse_quoted(&cursor, &node.path) &&
            graph_format_parse_literal(&cursor, " line=") &&
            graph_format_parse_int(&cursor, &node.line) &&
            graph_format_parse_literal(&cursor, " column=") &&
            graph_format_parse_int(&cursor, &node.column) &&
            graph_format_parse_literal(&cursor, " public=") &&
            graph_format_parse_bool(&cursor, &node.is_public) &&
            graph_format_parse_literal(&cursor, " mutable=") &&
            graph_format_parse_bool(&cursor, &node.is_mutable) &&
            graph_format_parse_literal(&cursor, " static=") &&
            graph_format_parse_bool(&cursor, &node.is_static) &&
            graph_format_parse_literal(&cursor, " fallible=") &&
            graph_format_parse_bool(&cursor, &node.fallible) &&
            graph_format_parse_literal(&cursor, " exportC=") &&
            graph_format_parse_bool(&cursor, &node.export_c) &&
            *cursor == 0;
  free(kind_name);
  if (!ok) {
    graph_format_free_node_fields(&node);
    return false;
  }
  *out = node;
  return true;
}

static bool graph_format_parse_edge_line(const char *line, ZProgramGraphEdge *out) {
  ZProgramGraphEdge edge = {0};
  char *target_name = NULL;
  const char *cursor = line;
  bool ok = graph_format_parse_literal(&cursor, "edge from=") &&
            graph_format_parse_quoted(&cursor, &edge.from) &&
            graph_format_parse_literal(&cursor, " to=") &&
            graph_format_parse_quoted(&cursor, &edge.to) &&
            graph_format_parse_literal(&cursor, " kind=") &&
            graph_format_parse_quoted(&cursor, &edge.kind) &&
            graph_format_parse_literal(&cursor, " target=") &&
            graph_format_parse_quoted(&cursor, &target_name) &&
            graph_format_edge_target_from_name(target_name, &edge.target) &&
            graph_format_parse_literal(&cursor, " order=") &&
            graph_format_parse_size(&cursor, &edge.order) &&
            *cursor == 0;
  free(target_name);
  if (!ok) {
    graph_format_free_edge_fields(&edge);
    return false;
  }
  *out = edge;
  return true;
}

static bool graph_format_parse_validation_line(const char *line, ZProgramGraphValidationState *state, bool *ok_status) {
  char *state_name = NULL;
  const char *cursor = line;
  bool parsed = graph_format_parse_literal(&cursor, "validation ") &&
                graph_format_parse_quoted(&cursor, &state_name) &&
                graph_format_validation_state_from_name(state_name, state) &&
                graph_format_parse_literal(&cursor, " ");
  if (parsed && strncmp(cursor, "ok", 2) == 0) {
    cursor += 2;
    *ok_status = true;
  } else if (parsed && strncmp(cursor, "failed", 6) == 0) {
    cursor += 6;
    *ok_status = false;
  } else {
    parsed = false;
  }
  parsed = parsed && *cursor == 0;
  free(state_name);
  return parsed;
}

static bool graph_format_parse_counts_line(const char *line, size_t *node_count, size_t *edge_count) {
  const char *cursor = line;
  return graph_format_parse_literal(&cursor, "counts nodes=") &&
         graph_format_parse_size(&cursor, node_count) &&
         graph_format_parse_literal(&cursor, " edges=") &&
         graph_format_parse_size(&cursor, edge_count) &&
         *cursor == 0;
}

static void graph_format_copy_node_identity_input(ZProgramGraphNode *dst, const ZProgramGraphNode *src) {
  dst->id = z_strdup(src->id ? src->id : "");
  dst->kind = src->kind;
  dst->name = z_strdup(src->name ? src->name : "");
  dst->type = z_strdup(src->type ? src->type : "");
  dst->value = z_strdup(src->value ? src->value : "");
  dst->path = z_strdup(src->path ? src->path : "");
  dst->line = src->line;
  dst->column = src->column;
  dst->is_public = src->is_public;
  dst->is_mutable = src->is_mutable;
  dst->is_static = src->is_static;
  dst->fallible = src->fallible;
  dst->export_c = src->export_c;
}

static bool graph_format_identities_match(const ZProgramGraph *graph) {
  ZProgramGraph expected;
  z_program_graph_init(&expected);
  expected.schema_version = graph->schema_version;
  free(expected.module_identity);
  expected.module_identity = z_strdup(graph->module_identity ? graph->module_identity : "");
  expected.nodes = z_checked_calloc(graph->node_len, sizeof(ZProgramGraphNode));
  expected.node_len = graph->node_len;
  expected.node_cap = graph->node_len;
  expected.edges = z_checked_calloc(graph->edge_len, sizeof(ZProgramGraphEdge));
  expected.edge_len = graph->edge_len;
  expected.edge_cap = graph->edge_len;
  for (size_t i = 0; i < graph->node_len; i++) graph_format_copy_node_identity_input(&expected.nodes[i], &graph->nodes[i]);
  for (size_t i = 0; i < graph->edge_len; i++) {
    expected.edges[i].from = z_strdup(graph->edges[i].from ? graph->edges[i].from : "");
    expected.edges[i].to = z_strdup(graph->edges[i].to ? graph->edges[i].to : "");
    expected.edges[i].kind = z_strdup(graph->edges[i].kind ? graph->edges[i].kind : "");
    expected.edges[i].target = graph->edges[i].target;
    expected.edges[i].order = graph->edges[i].order;
  }
  z_program_graph_finalize_identities(&expected);
  bool ok = graph_format_text_eq(expected.graph_hash, graph->graph_hash);
  for (size_t i = 0; ok && i < graph->node_len; i++) {
    ok = graph_format_text_eq(expected.nodes[i].symbol_id, graph->nodes[i].symbol_id) &&
         graph_format_text_eq(expected.nodes[i].type_id, graph->nodes[i].type_id) &&
         graph_format_text_eq(expected.nodes[i].effect_id, graph->nodes[i].effect_id) &&
         graph_format_text_eq(expected.nodes[i].node_hash, graph->nodes[i].node_hash);
  }
  z_program_graph_free(&expected);
  return ok;
}

bool z_program_graph_parse_dump(const char *text, ZProgramGraph *out, ZDiag *diag) {
  if (!text || !out) return graph_format_parse_fail(diag, 1, "program graph dump is missing");
  z_program_graph_init(out);
  const char *cursor = text;
  char *line = NULL;
  size_t line_no = 0;
  size_t node_count = 0;
  size_t edge_count = 0;
  bool validation_ok = true;
  const char *error_message = "invalid program graph dump";
  size_t error_line = 1;

#define FAIL_AT(line, message) \
  do { \
    error_line = (line); \
    error_message = (message); \
    goto fail; \
  } while (0)
#define FAIL(message) FAIL_AT(line_no, message)
#define NEXT_REQUIRED_LINE() \
  do { \
    free(line); \
    line = NULL; \
    line_no++; \
    if (!graph_format_next_line(&cursor, &line)) FAIL("incomplete program graph dump"); \
  } while (0)

  NEXT_REQUIRED_LINE();
  if (!graph_format_text_eq(line, "zero-program-graph v1")) {
    if (strncmp(line, "zero-program-graph v", 20) == 0) FAIL("unknown program graph schema version");
    FAIL("expected zero-program-graph v1 header");
  }
  out->schema_version = 1;
  NEXT_REQUIRED_LINE();
  if (graph_format_text_eq(line, "canonicalSource true")) out->canonical_source = true;
  else if (!graph_format_text_eq(line, "canonicalSource false")) FAIL("expected canonicalSource field");
  NEXT_REQUIRED_LINE();
  const char *strategy_cursor = line;
  char *strategy = NULL;
  if (!graph_format_parse_literal(&strategy_cursor, "idStrategy ") ||
      !graph_format_parse_quoted(&strategy_cursor, &strategy) ||
      !graph_format_text_eq(strategy, "deterministic-traversal-r0") ||
      *strategy_cursor != 0) {
    free(strategy);
    FAIL("expected deterministic graph id strategy");
  }
  free(strategy);
  NEXT_REQUIRED_LINE();
  const char *module_cursor = line;
  free(out->module_identity);
  out->module_identity = NULL;
  if (!graph_format_parse_literal(&module_cursor, "moduleIdentity ") ||
      !graph_format_parse_quoted(&module_cursor, &out->module_identity) ||
      *module_cursor != 0) FAIL("expected moduleIdentity field");
  NEXT_REQUIRED_LINE();
  const char *hash_cursor = line;
  if (!graph_format_parse_literal(&hash_cursor, "graphHash ") ||
      !graph_format_parse_quoted(&hash_cursor, &out->graph_hash) ||
      *hash_cursor != 0) FAIL("expected graphHash field");
  NEXT_REQUIRED_LINE();
  if (!graph_format_parse_validation_line(line, &out->validation_state, &validation_ok)) FAIL("expected validation field");
  if (!validation_ok) {
    size_t validation_line = line_no;
    NEXT_REQUIRED_LINE();
    if (strncmp(line, "diagnostic ", 11) != 0) FAIL("expected diagnostic field after failed validation");
    FAIL_AT(validation_line, "program graph artifact reports failed validation");
  }
  NEXT_REQUIRED_LINE();
  if (!graph_format_parse_counts_line(line, &node_count, &edge_count)) FAIL("expected node and edge counts");
  out->nodes = z_checked_calloc(node_count, sizeof(ZProgramGraphNode));
  out->node_cap = node_count;
  out->edges = z_checked_calloc(edge_count, sizeof(ZProgramGraphEdge));
  out->edge_cap = edge_count;
  for (size_t i = 0; i < node_count; i++) {
    NEXT_REQUIRED_LINE();
    if (!graph_format_parse_node_line(line, &out->nodes[i])) FAIL("invalid node record");
    out->node_len++;
  }
  for (size_t i = 0; i < edge_count; i++) {
    NEXT_REQUIRED_LINE();
    if (!graph_format_parse_edge_line(line, &out->edges[i])) FAIL("invalid edge record");
    out->edge_len++;
  }
  free(line);
  line = NULL;
  while (graph_format_next_line(&cursor, &line)) {
    line_no++;
    bool extra = line[0] != 0; free(line); line = NULL;
    if (extra) FAIL_AT(line_no, "unexpected content after graph dump");
  }
  if (validation_ok && !graph_format_identities_match(out)) FAIL_AT(1, "program graph identities do not match graph content");
#undef NEXT_REQUIRED_LINE
#undef FAIL
#undef FAIL_AT
  return true;

fail:
  z_program_graph_free(out);
  free(line);
  return graph_format_parse_fail(diag, error_line, error_message);
}

static bool graph_format_storage_validation_fail(const char *path, const ZProgramGraphValidation *validation, ZDiag *diag) {
  if (diag) {
    *diag = (ZDiag){0};
    diag->code = 1001;
    diag->path = path;
    diag->line = 1;
    diag->column = 1;
    snprintf(diag->message, sizeof(diag->message), "stored program graph failed validation: %s",
             validation && validation->message[0] ? validation->message : "invalid graph shape");
    snprintf(diag->expected, sizeof(diag->expected), "shape-valid program graph");
    snprintf(diag->actual, sizeof(diag->actual), "%s", validation && validation->code[0] ? validation->code : "invalid graph");
  }
  return false;
}

bool z_program_graph_load(const char *path, ZProgramGraph *out, ZDiag *diag) {
  char *text = z_read_file(path, diag);
  if (!text) return false;
  bool parsed = z_program_graph_parse_dump(text, out, diag);
  free(text);
  if (!parsed) {
    if (diag) diag->path = path;
    return false;
  }
  ZProgramGraphValidation validation = {0};
  if (!z_program_graph_validate(out, &validation)) {
    z_program_graph_free(out);
    return graph_format_storage_validation_fail(path, &validation, diag);
  }
  return true;
}

bool z_program_graph_save(const char *path, const ZProgramGraph *graph, ZDiag *diag) {
  ZProgramGraphValidation validation = {0};
  if (!z_program_graph_validate(graph, &validation)) return graph_format_storage_validation_fail(path, &validation, diag);
  ZProgramGraph storage = graph ? *graph : (ZProgramGraph){0};
  z_program_graph_apply_storage_metadata(path, &storage);
  ZBuf dump; zbuf_init(&dump);
  z_program_graph_append_dump(&dump, &storage, &validation);
  ZProgramGraph parsed;
  if (!z_program_graph_parse_dump(dump.data ? dump.data : "", &parsed, diag)) {
    if (diag) diag->path = path;
    zbuf_free(&dump);
    return false;
  }
  z_program_graph_free(&parsed);
  bool ok = z_write_file(path, dump.data ? dump.data : "", diag);
  zbuf_free(&dump);
  return ok;
}

void z_program_graph_append_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphValidation *validation) {
  zbuf_appendf(buf, "{\"schemaVersion\":%u,\"canonicalSource\":%s,\"idStrategy\":", graph ? graph->schema_version : 1, graph && graph->canonical_source ? "true" : "false");
  graph_format_append_quoted(buf, graph ? graph->id_strategy : "deterministic-traversal-r0");
  zbuf_append(buf, ",\"moduleIdentity\":");
  graph_format_append_quoted(buf, graph ? graph->module_identity : "module:main");
  zbuf_append(buf, ",\"graphHash\":");
  graph_format_append_quoted(buf, graph ? graph->graph_hash : NULL);
  zbuf_append(buf, ",\"validation\":");
  graph_format_append_validation_json(buf, validation);
  zbuf_appendf(buf, ",\"counts\":{\"nodes\":%zu,\"edges\":%zu},\"nodes\":[", graph ? graph->node_len : 0, graph ? graph->edge_len : 0);
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (i > 0) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"id\":");
    graph_format_append_quoted(buf, node->id);
    zbuf_append(buf, ",\"kind\":");
    graph_format_append_quoted(buf, z_program_graph_node_kind_name(node->kind));
    zbuf_append(buf, ",\"name\":");
    graph_format_append_quoted(buf, node->name);
    zbuf_append(buf, ",\"type\":");
    graph_format_append_quoted(buf, node->type);
    zbuf_append(buf, ",\"value\":");
    graph_format_append_quoted(buf, node->value);
    zbuf_append(buf, ",\"symbolId\":");
    graph_format_append_quoted(buf, node->symbol_id);
    zbuf_append(buf, ",\"typeId\":");
    graph_format_append_quoted(buf, node->type_id);
    zbuf_append(buf, ",\"effectId\":");
    graph_format_append_quoted(buf, node->effect_id);
    zbuf_append(buf, ",\"nodeHash\":");
    graph_format_append_quoted(buf, node->node_hash);
    zbuf_append(buf, ",\"path\":");
    graph_format_append_quoted(buf, node->path);
    zbuf_appendf(buf, ",\"line\":%d,\"column\":%d,\"public\":%s,\"mutable\":%s,\"static\":%s,\"fallible\":%s,\"exportC\":%s}",
                 node->line, node->column, node->is_public ? "true" : "false", node->is_mutable ? "true" : "false", node->is_static ? "true" : "false", node->fallible ? "true" : "false", node->export_c ? "true" : "false");
  }
  zbuf_append(buf, "],\"edges\":[");
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (i > 0) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"from\":");
    graph_format_append_quoted(buf, edge->from);
    zbuf_append(buf, ",\"to\":");
    graph_format_append_quoted(buf, edge->to);
    zbuf_append(buf, ",\"kind\":");
    graph_format_append_quoted(buf, edge->kind);
    zbuf_append(buf, ",\"target\":");
    graph_format_append_quoted(buf, z_program_graph_edge_target_name(edge->target));
    zbuf_appendf(buf, ",\"order\":%zu}", edge->order);
  }
  zbuf_append(buf, "]}");
}

void z_program_graph_append_dump(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphValidation *validation) {
  zbuf_appendf(buf, "zero-program-graph v%u\n", graph ? graph->schema_version : 1);
  zbuf_appendf(buf, "canonicalSource %s\n", graph && graph->canonical_source ? "true" : "false");
  zbuf_append(buf, "idStrategy ");
  graph_format_append_quoted(buf, graph ? graph->id_strategy : "deterministic-traversal-r0");
  zbuf_append_char(buf, '\n');
  zbuf_append(buf, "moduleIdentity ");
  graph_format_append_quoted(buf, graph ? graph->module_identity : "module:main");
  zbuf_append_char(buf, '\n');
  zbuf_append(buf, "graphHash ");
  graph_format_append_quoted(buf, graph ? graph->graph_hash : NULL);
  zbuf_append_char(buf, '\n');
  graph_format_append_validation_dump(buf, validation);
  zbuf_appendf(buf, "counts nodes=%zu edges=%zu\n", graph ? graph->node_len : 0, graph ? graph->edge_len : 0);
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    zbuf_append(buf, "node id=");
    graph_format_append_quoted(buf, node->id);
    zbuf_append(buf, " kind=");
    graph_format_append_quoted(buf, z_program_graph_node_kind_name(node->kind));
    zbuf_append(buf, " name=");
    graph_format_append_quoted(buf, node->name);
    zbuf_append(buf, " type=");
    graph_format_append_quoted(buf, node->type);
    zbuf_append(buf, " value=");
    graph_format_append_quoted(buf, node->value);
    zbuf_append(buf, " symbolId=");
    graph_format_append_quoted(buf, node->symbol_id);
    zbuf_append(buf, " typeId=");
    graph_format_append_quoted(buf, node->type_id);
    zbuf_append(buf, " effectId=");
    graph_format_append_quoted(buf, node->effect_id);
    zbuf_append(buf, " nodeHash=");
    graph_format_append_quoted(buf, node->node_hash);
    zbuf_append(buf, " path=");
    graph_format_append_quoted(buf, node->path);
    zbuf_appendf(buf, " line=%d column=%d public=%s mutable=%s static=%s fallible=%s exportC=%s\n",
                 node->line, node->column, node->is_public ? "true" : "false", node->is_mutable ? "true" : "false", node->is_static ? "true" : "false", node->fallible ? "true" : "false", node->export_c ? "true" : "false");
  }
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    zbuf_append(buf, "edge from=");
    graph_format_append_quoted(buf, edge->from);
    zbuf_append(buf, " to=");
    graph_format_append_quoted(buf, edge->to);
    zbuf_append(buf, " kind=");
    graph_format_append_quoted(buf, edge->kind);
    zbuf_append(buf, " target=");
    graph_format_append_quoted(buf, z_program_graph_edge_target_name(edge->target));
    zbuf_appendf(buf, " order=%zu\n", edge->order);
  }
}

void z_append_program_graph_json(ZBuf *buf, const SourceInput *input, const Program *program) {
  ZProgramGraph graph;
  if (!z_program_graph_from_program(input, program, &graph)) {
    graph_format_append_failed_json(buf);
    return;
  }
  ZProgramGraphValidation validation = {0};
  z_program_graph_validate(&graph, &validation);
  z_program_graph_append_json(buf, &graph, &validation);
  z_program_graph_free(&graph);
}

void z_append_program_graph_dump(ZBuf *buf, const SourceInput *input, const Program *program, bool json) {
  ZProgramGraph graph;
  if (!z_program_graph_from_program(input, program, &graph)) {
    if (json) graph_format_append_failed_json(buf);
    else graph_format_append_failed_dump(buf);
    return;
  }
  ZProgramGraphValidation validation = {0};
  z_program_graph_validate(&graph, &validation);
  if (json) z_program_graph_append_json(buf, &graph, &validation);
  else z_program_graph_append_dump(buf, &graph, &validation);
  z_program_graph_free(&graph);
}
