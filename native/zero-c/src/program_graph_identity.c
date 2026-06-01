#include "program_graph.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *z_program_graph_node_kind_name(ZProgramGraphNodeKind kind) {
  switch (kind) {
    case Z_PROGRAM_GRAPH_NODE_MODULE: return "Module";
    case Z_PROGRAM_GRAPH_NODE_IMPORT: return "Import";
    case Z_PROGRAM_GRAPH_NODE_C_IMPORT: return "CImport";
    case Z_PROGRAM_GRAPH_NODE_CONST: return "Const";
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS: return "TypeAlias";
    case Z_PROGRAM_GRAPH_NODE_SHAPE: return "Shape";
    case Z_PROGRAM_GRAPH_NODE_INTERFACE: return "Interface";
    case Z_PROGRAM_GRAPH_NODE_ENUM: return "Enum";
    case Z_PROGRAM_GRAPH_NODE_CHOICE: return "Choice";
    case Z_PROGRAM_GRAPH_NODE_FUNCTION: return "Function";
    case Z_PROGRAM_GRAPH_NODE_PARAM: return "Param";
    case Z_PROGRAM_GRAPH_NODE_FIELD: return "Field";
    case Z_PROGRAM_GRAPH_NODE_ENUM_CASE: return "EnumCase";
    case Z_PROGRAM_GRAPH_NODE_CHOICE_CASE: return "ChoiceCase";
    case Z_PROGRAM_GRAPH_NODE_BLOCK: return "Block";
    case Z_PROGRAM_GRAPH_NODE_LET: return "Let";
    case Z_PROGRAM_GRAPH_NODE_ASSIGNMENT: return "Assignment";
    case Z_PROGRAM_GRAPH_NODE_DEFER: return "Defer";
    case Z_PROGRAM_GRAPH_NODE_CHECK: return "Check";
    case Z_PROGRAM_GRAPH_NODE_RETURN: return "Return";
    case Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT: return "ExpressionStatement";
    case Z_PROGRAM_GRAPH_NODE_IF: return "If";
    case Z_PROGRAM_GRAPH_NODE_WHILE: return "While";
    case Z_PROGRAM_GRAPH_NODE_FOR: return "For";
    case Z_PROGRAM_GRAPH_NODE_BREAK: return "Break";
    case Z_PROGRAM_GRAPH_NODE_CONTINUE: return "Continue";
    case Z_PROGRAM_GRAPH_NODE_MATCH: return "Match";
    case Z_PROGRAM_GRAPH_NODE_RAISE: return "Raise";
    case Z_PROGRAM_GRAPH_NODE_MATCH_ARM: return "MatchArm";
    case Z_PROGRAM_GRAPH_NODE_IDENTIFIER: return "Identifier";
    case Z_PROGRAM_GRAPH_NODE_LITERAL: return "Literal";
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS: return "FieldAccess";
    case Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS: return "IndexAccess";
    case Z_PROGRAM_GRAPH_NODE_SLICE: return "Slice";
    case Z_PROGRAM_GRAPH_NODE_CALL: return "Call";
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL: return "MethodCall";
    case Z_PROGRAM_GRAPH_NODE_CAST: return "Cast";
    case Z_PROGRAM_GRAPH_NODE_BORROW: return "Borrow";
    case Z_PROGRAM_GRAPH_NODE_RESCUE: return "Rescue";
    case Z_PROGRAM_GRAPH_NODE_META: return "Meta";
    case Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL: return "ShapeLiteral";
    case Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL: return "ArrayLiteral";
    case Z_PROGRAM_GRAPH_NODE_FIELD_INIT: return "FieldInit";
    case Z_PROGRAM_GRAPH_NODE_TYPE_REF: return "TypeRef";
    case Z_PROGRAM_GRAPH_NODE_EFFECT_REF: return "EffectRef";
    case Z_PROGRAM_GRAPH_NODE_ERROR_VARIANT: return "ErrorVariant";
    case Z_PROGRAM_GRAPH_NODE_EXPRESSION: return "Expression";
    case Z_PROGRAM_GRAPH_NODE_STATEMENT: return "Statement";
  }
  return "Node";
}

const char *z_program_graph_edge_target_name(ZProgramGraphEdgeTarget target) {
  switch (target) {
    case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE: return "node";
    case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL: return "symbol";
    case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE: return "type";
    case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT: return "effect";
  }
  return "unknown";
}

const char *z_program_graph_validation_state_name(ZProgramGraphValidationState state) {
  switch (state) {
    case Z_PROGRAM_GRAPH_VALIDATION_DECODED: return "decoded";
    case Z_PROGRAM_GRAPH_VALIDATION_SHAPE_VALID: return "shape-valid";
    case Z_PROGRAM_GRAPH_VALIDATION_RESOLVED: return "resolved";
    case Z_PROGRAM_GRAPH_VALIDATION_TYPED: return "typed";
    case Z_PROGRAM_GRAPH_VALIDATION_EFFECT_CHECKED: return "effect-checked";
    case Z_PROGRAM_GRAPH_VALIDATION_BUILDABLE: return "buildable";
  }
  return "unknown";
}

static uint64_t graph_hash_text(uint64_t hash, const char *text) {
  const unsigned char *p = (const unsigned char *)(text ? text : "");
  while (*p) {
    hash ^= (uint64_t)*p++;
    hash *= 1099511628211ull;
  }
  hash ^= 0xffu;
  hash *= 1099511628211ull;
  return hash;
}

static uint64_t graph_hash_u64(uint64_t hash, uint64_t value) {
  for (unsigned i = 0; i < 8; i++) {
    hash ^= (value >> (i * 8)) & 0xffu;
    hash *= 1099511628211ull;
  }
  return hash;
}

static char *graph_format_domain_id(const char *domain, uint64_t hash) {
  char text[40];
  snprintf(text, sizeof(text), "%s:%016llx", domain, (unsigned long long)hash);
  return z_strdup(text);
}

static bool graph_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool graph_node_declares_symbol(const ZProgramGraphNode *node) {
  if (!node || !node->name || !node->name[0]) return false;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_MODULE:
    case Z_PROGRAM_GRAPH_NODE_CONST:
    case Z_PROGRAM_GRAPH_NODE_C_IMPORT:
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
    case Z_PROGRAM_GRAPH_NODE_SHAPE:
    case Z_PROGRAM_GRAPH_NODE_INTERFACE:
    case Z_PROGRAM_GRAPH_NODE_ENUM:
    case Z_PROGRAM_GRAPH_NODE_CHOICE:
    case Z_PROGRAM_GRAPH_NODE_FUNCTION:
    case Z_PROGRAM_GRAPH_NODE_PARAM:
    case Z_PROGRAM_GRAPH_NODE_FIELD:
    case Z_PROGRAM_GRAPH_NODE_ENUM_CASE:
    case Z_PROGRAM_GRAPH_NODE_CHOICE_CASE:
    case Z_PROGRAM_GRAPH_NODE_LET:
    case Z_PROGRAM_GRAPH_NODE_ERROR_VARIANT:
      return true;
    default:
      return false;
  }
}

static void graph_append_symbol_component(ZBuf *buf, const char *text) {
  const unsigned char *cursor = (const unsigned char *)(text ? text : "");
  bool emitted = false;
  while (*cursor) {
    unsigned char ch = *cursor++;
    if ((ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') ||
        ch == '.' || ch == '_' || ch == '-' || ch == '@' || ch == '/') {
      zbuf_append_char(buf, (char)ch);
      emitted = true;
    } else {
      zbuf_append_char(buf, '_');
      emitted = true;
    }
  }
  if (!emitted) zbuf_append(buf, "main");
}

static const char *graph_identity_name(const char *identity) {
  if (!identity) return "main";
  if (strncmp(identity, "module:", strlen("module:")) == 0) return identity + strlen("module:");
  if (strncmp(identity, "package:", strlen("package:")) == 0) return identity + strlen("package:");
  return identity;
}

static char *graph_module_scope_name(const ZProgramGraph *graph, const ZProgramGraphNode *module) {
  ZBuf scope;
  zbuf_init(&scope);
  const char *identity = graph && graph->module_identity ? graph->module_identity : "module:main";
  const char *module_name = module && module->name && module->name[0] ? module->name : graph_identity_name(identity);
  if (strncmp(identity, "package:", strlen("package:")) == 0) {
    graph_append_symbol_component(&scope, graph_identity_name(identity));
    zbuf_append_char(&scope, '/');
    graph_append_symbol_component(&scope, module_name);
  } else {
    graph_append_symbol_component(&scope, module_name);
  }
  return scope.data ? scope.data : z_strdup("main");
}

static const char *graph_import_binding_name(const ZProgramGraphNode *node) {
  if (!node) return "";
  if (node->value && node->value[0]) return node->value;
  const char *module = node->name ? node->name : "";
  const char *last_dot = strrchr(module, '.');
  return last_dot && last_dot[1] ? last_dot + 1 : module;
}

static const char *graph_symbol_name(const ZProgramGraphNode *node) {
  if (!node) return "";
  if (node->kind == Z_PROGRAM_GRAPH_NODE_IMPORT) return graph_import_binding_name(node);
  return node->name ? node->name : "";
}

static bool graph_owner_edge_kind(const char *kind) {
  static const char *const owner_kinds[] = {
    "alias",
    "arg",
    "arm",
    "body",
    "cImport",
    "case",
    "choice",
    "const",
    "constraint",
    "declaredType",
    "default",
    "effect",
    "else",
    "enum",
    "error",
    "expr",
    "field",
    "function",
    "guard",
    "import",
    "interface",
    "left",
    "method",
    "param",
    "rangeEnd",
    "returnType",
    "right",
    "shape",
    "statement",
    "target",
    "then",
    "type",
    "typeArg",
    "typeParam",
    "value",
    "variant",
  };
  for (size_t i = 0; i < sizeof(owner_kinds) / sizeof(owner_kinds[0]); i++) {
    if (graph_text_eq(kind, owner_kinds[i])) return true;
  }
  return false;
}

static size_t graph_find_node_index(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (graph_text_eq(graph->nodes[i].id, id)) return i;
  }
  return SIZE_MAX;
}

static const ZProgramGraphEdge *graph_owner_edge_for_node(const ZProgramGraph *graph, const char *node_id) {
  for (size_t i = 0; graph && node_id && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        graph_text_eq(edge->to, node_id) &&
        graph_owner_edge_kind(edge->kind)) {
      return edge;
    }
  }
  return NULL;
}

static size_t graph_symbol_owner_index(const ZProgramGraph *graph, size_t node_index) {
  if (!graph || node_index >= graph->node_len) return SIZE_MAX;
  const char *current = graph->nodes[node_index].id;
  for (size_t depth = 0; current && depth < graph->node_len; depth++) {
    const ZProgramGraphEdge *edge = graph_owner_edge_for_node(graph, current);
    if (!edge) return SIZE_MAX;
    size_t owner_index = graph_find_node_index(graph, edge->from);
    if (owner_index == SIZE_MAX) return SIZE_MAX;
    const ZProgramGraphNode *owner = &graph->nodes[owner_index];
    if (owner->kind == Z_PROGRAM_GRAPH_NODE_MODULE || graph_node_declares_symbol(owner)) return owner_index;
    current = owner->id;
  }
  return SIZE_MAX;
}

static const char *graph_node_symbol_namespace(const ZProgramGraphNode *node, const ZProgramGraphEdge *owner_edge) {
  if (!node) return NULL;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_MODULE: return "module";
    case Z_PROGRAM_GRAPH_NODE_IMPORT: return "import";
    case Z_PROGRAM_GRAPH_NODE_C_IMPORT: return "c-import";
    case Z_PROGRAM_GRAPH_NODE_CONST: return "value";
    case Z_PROGRAM_GRAPH_NODE_FUNCTION: return owner_edge && graph_text_eq(owner_edge->kind, "method") ? "method" : "value";
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
    case Z_PROGRAM_GRAPH_NODE_SHAPE:
    case Z_PROGRAM_GRAPH_NODE_INTERFACE:
    case Z_PROGRAM_GRAPH_NODE_ENUM:
    case Z_PROGRAM_GRAPH_NODE_CHOICE:
      return "type";
    case Z_PROGRAM_GRAPH_NODE_PARAM: return "param";
    case Z_PROGRAM_GRAPH_NODE_FIELD: return "field";
    case Z_PROGRAM_GRAPH_NODE_ENUM_CASE:
    case Z_PROGRAM_GRAPH_NODE_CHOICE_CASE:
    case Z_PROGRAM_GRAPH_NODE_ERROR_VARIANT:
      return "variant";
    case Z_PROGRAM_GRAPH_NODE_LET: return "local";
    default: return NULL;
  }
}

static void graph_append_local_symbol_suffix(ZBuf *buf, const ZProgramGraphNode *node) {
  const char *id = node && node->id ? node->id : "";
  const char *last_dot = strrchr(id, '.');
  const char *suffix = last_dot && last_dot[1] ? last_dot + 1 : id;
  if (!suffix[0]) return;
  zbuf_append_char(buf, '@');
  graph_append_symbol_component(buf, suffix);
}

static char *graph_node_symbol_id_in_graph(const ZProgramGraph *graph, size_t node_index, size_t depth) {
  if (!graph || node_index >= graph->node_len || depth > graph->node_len) return NULL;
  const ZProgramGraphNode *node = &graph->nodes[node_index];
  if (!graph_node_declares_symbol(node)) return NULL;
  const char *name = graph_symbol_name(node);
  if (!name || !name[0]) return NULL;

  if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE) {
    char *scope = graph_module_scope_name(graph, node);
    ZBuf out;
    zbuf_init(&out);
    zbuf_append(&out, "symbol:");
    graph_append_symbol_component(&out, scope);
    zbuf_append(&out, "::module");
    free(scope);
    return out.data ? out.data : z_strdup("symbol:main::module");
  }

  const ZProgramGraphEdge *owner_edge = graph_owner_edge_for_node(graph, node->id);
  size_t owner_index = graph_symbol_owner_index(graph, node_index);
  const ZProgramGraphNode *owner = owner_index != SIZE_MAX ? &graph->nodes[owner_index] : NULL;
  const char *symbol_namespace = graph_node_symbol_namespace(node, owner_edge);
  if (!symbol_namespace) return NULL;

  ZBuf out;
  zbuf_init(&out);
  if (owner && owner->kind == Z_PROGRAM_GRAPH_NODE_MODULE) {
    char *scope = graph_module_scope_name(graph, owner);
    zbuf_append(&out, "symbol:");
    graph_append_symbol_component(&out, scope);
    zbuf_append(&out, "::");
    free(scope);
  } else if (owner) {
    char *owner_symbol = graph_node_symbol_id_in_graph(graph, owner_index, depth + 1);
    if (owner_symbol && owner_symbol[0]) {
      zbuf_append(&out, owner_symbol);
      zbuf_append_char(&out, '/');
    }
    free(owner_symbol);
  }

  if (out.len == 0) {
    zbuf_append(&out, "symbol:");
    graph_append_symbol_component(&out, graph_identity_name(graph->module_identity));
    zbuf_append(&out, "::");
  }

  graph_append_symbol_component(&out, symbol_namespace);
  zbuf_append_char(&out, '.');
  graph_append_symbol_component(&out, name);
  if (node->kind == Z_PROGRAM_GRAPH_NODE_LET) graph_append_local_symbol_suffix(&out, node);
  return out.data ? out.data : z_strdup("");
}

static uint64_t graph_node_hash_value(const ZProgramGraphNode *node) {
  uint64_t hash = 1469598103934665603ull;
  hash = graph_hash_text(hash, z_program_graph_node_kind_name(node->kind));
  hash = graph_hash_text(hash, node->name);
  hash = graph_hash_text(hash, node->type);
  hash = graph_hash_text(hash, node->value);
  hash = graph_hash_u64(hash, node->is_public ? 1 : 0);
  hash = graph_hash_u64(hash, node->is_mutable ? 1 : 0);
  hash = graph_hash_u64(hash, node->is_static ? 1 : 0);
  hash = graph_hash_u64(hash, node->fallible ? 1 : 0);
  hash = graph_hash_u64(hash, node->export_c ? 1 : 0);
  return hash;
}

static char *graph_node_type_id(const ZProgramGraphNode *node) {
  if (!node || !node->type || !node->type[0]) return NULL;
  uint64_t hash = graph_hash_text(1469598103934665603ull, node->type);
  return graph_format_domain_id("type", hash);
}

static char *graph_node_effect_id(const ZProgramGraphNode *node) {
  if (!node || node->kind != Z_PROGRAM_GRAPH_NODE_EFFECT_REF || !node->name || !node->name[0]) return NULL;
  uint64_t hash = graph_hash_text(1469598103934665603ull, node->name);
  return graph_format_domain_id("effect", hash);
}

void z_program_graph_finalize_identities(ZProgramGraph *graph) {
  if (!graph) return;
  uint64_t graph_hash = 1469598103934665603ull;
  graph_hash = graph_hash_u64(graph_hash, graph->schema_version);
  graph_hash = graph_hash_text(graph_hash, graph->module_identity);
  for (size_t i = 0; i < graph->node_len; i++) {
    ZProgramGraphNode *node = &graph->nodes[i];
    free(node->symbol_id);
    free(node->type_id);
    free(node->effect_id);
    free(node->node_hash);
    free(node->path_id);
    node->symbol_id = graph_node_symbol_id_in_graph(graph, i, 0);
    node->type_id = graph_node_type_id(node);
    node->effect_id = graph_node_effect_id(node);
    node->node_hash = graph_format_domain_id("nodehash", graph_node_hash_value(node));
    /* Structural identity is derived from edges/ids; kept out of graph_hash since
     * those inputs are already hashed below. */
    node->path_id = z_program_graph_node_path_id(graph, i);
    graph_hash = graph_hash_text(graph_hash, node->id);
    graph_hash = graph_hash_text(graph_hash, node->node_hash);
    graph_hash = graph_hash_text(graph_hash, node->symbol_id);
    graph_hash = graph_hash_text(graph_hash, node->type_id);
    graph_hash = graph_hash_text(graph_hash, node->effect_id);
  }
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    graph_hash = graph_hash_text(graph_hash, edge->from);
    graph_hash = graph_hash_text(graph_hash, edge->to);
    graph_hash = graph_hash_text(graph_hash, edge->kind);
    graph_hash = graph_hash_text(graph_hash, z_program_graph_edge_target_name(edge->target));
    graph_hash = graph_hash_u64(graph_hash, edge->order);
  }
  free(graph->graph_hash);
  graph->graph_hash = graph_format_domain_id("graph", graph_hash);
}

static void graph_append_path_id(ZBuf *out, const ZProgramGraph *graph, const char *node_id, size_t depth) {
  if (!graph || !node_id || depth > graph->node_len) return;
  const ZProgramGraphEdge *edge = graph_owner_edge_for_node(graph, node_id);
  if (edge) {
    graph_append_path_id(out, graph, edge->from, depth + 1);
    zbuf_append_char(out, '/');
    zbuf_append(out, edge->kind ? edge->kind : "");
    zbuf_appendf(out, "[%zu]", edge->order);
    return;
  }
  /* No owning edge: this is a root. Anchor on the module identity so the path is
   * content-independent and stable across edits. */
  size_t index = graph_find_node_index(graph, node_id);
  if (index != SIZE_MAX && graph->nodes[index].kind == Z_PROGRAM_GRAPH_NODE_MODULE) {
    zbuf_append(out, graph->module_identity ? graph->module_identity : "module:main");
  } else {
    zbuf_append(out, node_id);
  }
}

char *z_program_graph_node_path_id(const ZProgramGraph *graph, size_t node_index) {
  if (!graph || node_index >= graph->node_len) return NULL;
  ZBuf out;
  zbuf_init(&out);
  graph_append_path_id(&out, graph, graph->nodes[node_index].id, 0);
  return out.data ? out.data : z_strdup("");
}
