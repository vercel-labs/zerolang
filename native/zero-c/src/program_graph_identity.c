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

typedef struct {
  const char *key;
  size_t index;
  bool used;
} GraphNodeIndexEntry;

typedef struct {
  const ZProgramGraph *graph;
  GraphNodeIndexEntry *node_index;
  size_t node_index_cap;
  size_t *owner_edge_by_node;
  size_t *owner_index_by_node;
  size_t *symbol_owner_index;
  bool *symbol_owner_resolved;
} GraphIdentityContext;

static size_t graph_identity_index_capacity(size_t len) {
  size_t cap = 1;
  while (cap < len * 2 + 1) cap *= 2;
  return cap;
}

static void graph_identity_index_put(GraphIdentityContext *context, const char *key, size_t index) {
  if (!context || !context->node_index || !key) return;
  size_t mask = context->node_index_cap - 1;
  size_t slot = (size_t)graph_hash_text(1469598103934665603ull, key) & mask;
  while (context->node_index[slot].used) slot = (slot + 1) & mask;
  context->node_index[slot] = (GraphNodeIndexEntry){.key = key, .index = index, .used = true};
}

static size_t graph_identity_index_find(const GraphIdentityContext *context, const char *key) {
  if (!context || !context->node_index || !key) return SIZE_MAX;
  size_t mask = context->node_index_cap - 1;
  size_t slot = (size_t)graph_hash_text(1469598103934665603ull, key) & mask;
  for (size_t probe = 0; probe < context->node_index_cap; probe++) {
    GraphNodeIndexEntry *entry = &context->node_index[slot];
    if (!entry->used) return SIZE_MAX;
    if (graph_text_eq(entry->key, key)) return entry->index;
    slot = (slot + 1) & mask;
  }
  return SIZE_MAX;
}

static void graph_identity_context_init(GraphIdentityContext *context, const ZProgramGraph *graph) {
  *context = (GraphIdentityContext){.graph = graph};
  size_t len = graph ? graph->node_len : 0;
  context->node_index_cap = graph_identity_index_capacity(len);
  context->node_index = z_checked_calloc(context->node_index_cap, sizeof(GraphNodeIndexEntry));
  context->owner_edge_by_node = z_checked_calloc(len ? len : 1, sizeof(size_t));
  context->owner_index_by_node = z_checked_calloc(len ? len : 1, sizeof(size_t));
  context->symbol_owner_index = z_checked_calloc(len ? len : 1, sizeof(size_t));
  context->symbol_owner_resolved = z_checked_calloc(len ? len : 1, sizeof(bool));
  for (size_t i = 0; i < len; i++) {
    context->owner_edge_by_node[i] = SIZE_MAX;
    context->owner_index_by_node[i] = SIZE_MAX;
    context->symbol_owner_index[i] = SIZE_MAX;
    graph_identity_index_put(context, graph->nodes[i].id, i);
  }
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !graph_owner_edge_kind(edge->kind)) continue;
    size_t target = graph_identity_index_find(context, edge->to);
    if (target == SIZE_MAX || context->owner_edge_by_node[target] != SIZE_MAX) continue;
    context->owner_edge_by_node[target] = i;
    context->owner_index_by_node[target] = graph_identity_index_find(context, edge->from);
  }
}

static void graph_identity_context_free(GraphIdentityContext *context) {
  if (!context) return;
  free(context->symbol_owner_resolved);
  free(context->symbol_owner_index);
  free(context->owner_index_by_node);
  free(context->owner_edge_by_node);
  free(context->node_index);
  *context = (GraphIdentityContext){0};
}

static size_t graph_symbol_owner_index(GraphIdentityContext *context, size_t node_index) {
  const ZProgramGraph *graph = context ? context->graph : NULL;
  if (!graph || node_index >= graph->node_len) return SIZE_MAX;
  if (context->symbol_owner_resolved[node_index]) return context->symbol_owner_index[node_index];
  size_t current = node_index;
  size_t result = SIZE_MAX;
  for (size_t depth = 0; depth < graph->node_len; depth++) {
    size_t owner_index = context->owner_index_by_node[current];
    if (owner_index == SIZE_MAX || owner_index >= graph->node_len) break;
    const ZProgramGraphNode *owner = &graph->nodes[owner_index];
    if (owner->kind == Z_PROGRAM_GRAPH_NODE_MODULE || graph_node_declares_symbol(owner)) {
      result = owner_index;
      break;
    }
    current = owner_index;
  }
  context->symbol_owner_resolved[node_index] = true;
  context->symbol_owner_index[node_index] = result;
  return result;
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

static char *graph_node_symbol_id_in_graph(GraphIdentityContext *context, size_t node_index, size_t depth) {
  const ZProgramGraph *graph = context ? context->graph : NULL;
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

  const ZProgramGraphEdge *owner_edge = context->owner_edge_by_node[node_index] == SIZE_MAX ? NULL : &graph->edges[context->owner_edge_by_node[node_index]];
  size_t owner_index = graph_symbol_owner_index(context, node_index);
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
    char *owner_symbol = owner->symbol_id && owner->symbol_id[0] ? z_strdup(owner->symbol_id) : graph_node_symbol_id_in_graph(context, owner_index, depth + 1);
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
  GraphIdentityContext identity;
  graph_identity_context_init(&identity, graph);
  for (size_t i = 0; i < graph->node_len; i++) {
    ZProgramGraphNode *node = &graph->nodes[i];
    free(node->symbol_id);
    free(node->type_id);
    free(node->effect_id);
    free(node->node_hash);
    node->symbol_id = NULL;
    node->type_id = NULL;
    node->effect_id = NULL;
    node->node_hash = NULL;
  }
  uint64_t graph_hash = 1469598103934665603ull;
  graph_hash = graph_hash_u64(graph_hash, graph->schema_version);
  graph_hash = graph_hash_text(graph_hash, graph->module_identity);
  for (size_t i = 0; i < graph->node_len; i++) {
    ZProgramGraphNode *node = &graph->nodes[i];
    node->symbol_id = graph_node_symbol_id_in_graph(&identity, i, 0);
    node->type_id = graph_node_type_id(node);
    node->effect_id = graph_node_effect_id(node);
    node->node_hash = graph_format_domain_id("nodehash", graph_node_hash_value(node));
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
  graph_identity_context_free(&identity);
}
