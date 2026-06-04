#include "program_graph_reconcile_apply.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const ZProgramGraph *base;
  ZProgramGraph *edited;
  size_t *base_to_edited;
  bool *edited_matched;
  ZProgramGraphIdentityReconcile result;
} IdentityContext;

typedef bool (*IdentityCandidateFn)(IdentityContext *context, size_t base_index, size_t edited_index);

static size_t identity_missing(void) { return SIZE_MAX; }

static bool identity_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool identity_text_present(const char *text) { return text && text[0]; }

static void identity_fail(IdentityContext *context, const char *code, const char *message, const char *node_id, const char *candidate_id) {
  if (!context || !context->result.ok) return;
  context->result.ok = false;
  context->result.ambiguous = identity_text_eq(code, "GRC001");
  context->result.module_identity_changed = identity_text_eq(code, "GRC003");
  snprintf(context->result.code, sizeof(context->result.code), "%s", code ? code : "GRC000");
  snprintf(context->result.message, sizeof(context->result.message), "%s", message ? message : "program graph source identity could not be preserved");
  snprintf(context->result.node_id, sizeof(context->result.node_id), "%s", node_id ? node_id : "");
  snprintf(context->result.candidate_id, sizeof(context->result.candidate_id), "%s", candidate_id ? candidate_id : "");
}

static size_t identity_find_node(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (identity_text_eq(graph->nodes[i].id, id)) return i;
  }
  return identity_missing();
}

static bool identity_owner_edge_kind(const char *kind) {
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
    if (identity_text_eq(kind, owner_kinds[i])) return true;
  }
  return false;
}

static size_t identity_owner_edge_index(const ZProgramGraph *graph, size_t node_index) {
  if (!graph || node_index >= graph->node_len) return identity_missing();
  const char *node_id = graph->nodes[node_index].id;
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        identity_owner_edge_kind(edge->kind) &&
        identity_text_eq(edge->to, node_id)) {
      return i;
    }
  }
  return identity_missing();
}

static size_t identity_edge_source_index(const ZProgramGraph *graph, size_t edge_index) {
  if (!graph || edge_index >= graph->edge_len) return identity_missing();
  return identity_find_node(graph, graph->edges[edge_index].from);
}

static bool identity_id_base(const char *id, char *out, size_t out_len) {
  if (!id || !out || out_len == 0) return false;
  const char *underscore = strchr(id, '_');
  if (!underscore) {
    snprintf(out, out_len, "%s", id);
    return true;
  }
  const char *cursor = underscore + 1;
  for (int i = 0; i < 8; i++) {
    char ch = cursor[i];
    bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    if (!hex) {
      snprintf(out, out_len, "%s", id);
      return true;
    }
  }
  size_t len = (size_t)(cursor + 8 - id);
  if (len >= out_len) len = out_len - 1;
  memcpy(out, id, len);
  out[len] = 0;
  return true;
}

static bool identity_node_payload_eq(const ZProgramGraphNode *left, const ZProgramGraphNode *right) {
  return left &&
         right &&
         left->kind == right->kind &&
         identity_text_eq(left->name, right->name) &&
         identity_text_eq(left->type, right->type) &&
         identity_text_eq(left->value, right->value) &&
         left->is_public == right->is_public &&
         left->is_mutable == right->is_mutable &&
         left->is_static == right->is_static &&
         left->fallible == right->fallible &&
         left->export_c == right->export_c;
}

static bool identity_match_nodes(IdentityContext *context, size_t base_index, size_t edited_index) {
  if (!context || base_index >= context->base->node_len || edited_index >= context->edited->node_len) return false;
  if (context->base_to_edited[base_index] != identity_missing() && context->base_to_edited[base_index] != edited_index) {
    identity_fail(context, "GRC001", "source edit has ambiguous graph identity", context->base->nodes[base_index].id, context->edited->nodes[edited_index].id);
    return false;
  }
  if (context->edited_matched[edited_index] && context->base_to_edited[base_index] != edited_index) {
    identity_fail(context, "GRC001", "source edit has ambiguous graph identity", context->base->nodes[base_index].id, context->edited->nodes[edited_index].id);
    return false;
  }
  context->base_to_edited[base_index] = edited_index;
  context->edited_matched[edited_index] = true;
  return true;
}

static void identity_match_exact_ids(IdentityContext *context) {
  for (size_t i = 0; context && context->result.ok && i < context->base->node_len; i++) {
    size_t edited = identity_find_node(context->edited, context->base->nodes[i].id);
    if (edited != identity_missing()) identity_match_nodes(context, i, edited);
  }
}

static bool identity_source_anchor_candidate(IdentityContext *context, size_t base_index, size_t edited_index) {
  const ZProgramGraphNode *base = &context->base->nodes[base_index];
  const ZProgramGraphNode *edited = &context->edited->nodes[edited_index];
  return base->kind == edited->kind &&
         base->line == edited->line &&
         base->column == edited->column &&
         identity_text_eq(base->path, edited->path);
}

static bool identity_symbol_candidate(IdentityContext *context, size_t base_index, size_t edited_index) {
  const ZProgramGraphNode *base = &context->base->nodes[base_index];
  const ZProgramGraphNode *edited = &context->edited->nodes[edited_index];
  return base->kind == edited->kind &&
         identity_text_present(base->symbol_id) &&
         identity_text_eq(base->symbol_id, edited->symbol_id);
}

static bool identity_relaxed_node_key_eq(const ZProgramGraphNode *base, const ZProgramGraphNode *edited) {
  if (!base || !edited || base->kind != edited->kind) return false;
  if (identity_text_present(base->name) || identity_text_present(edited->name)) return identity_text_eq(base->name, edited->name);
  if (identity_text_present(base->value) || identity_text_present(edited->value)) return identity_text_eq(base->value, edited->value);
  if (identity_text_present(base->type) || identity_text_present(edited->type)) return identity_text_eq(base->type, edited->type);
  return true;
}

static bool identity_owner_child_candidate(IdentityContext *context, size_t base_index, size_t edited_index) {
  size_t base_edge_index = identity_owner_edge_index(context->base, base_index);
  size_t edited_edge_index = identity_owner_edge_index(context->edited, edited_index);
  if (base_edge_index == identity_missing() || edited_edge_index == identity_missing()) return false;
  const ZProgramGraphEdge *base_edge = &context->base->edges[base_edge_index];
  const ZProgramGraphEdge *edited_edge = &context->edited->edges[edited_edge_index];
  if (!identity_text_eq(base_edge->kind, edited_edge->kind)) return false;
  size_t base_owner = identity_edge_source_index(context->base, base_edge_index);
  size_t edited_owner = identity_edge_source_index(context->edited, edited_edge_index);
  if (base_owner == identity_missing() || edited_owner == identity_missing()) return false;
  if (context->base_to_edited[base_owner] != edited_owner) return false;
  return identity_relaxed_node_key_eq(&context->base->nodes[base_index], &context->edited->nodes[edited_index]);
}

static bool identity_base_id_candidate(IdentityContext *context, size_t base_index, size_t edited_index) {
  const ZProgramGraphNode *base = &context->base->nodes[base_index];
  const ZProgramGraphNode *edited = &context->edited->nodes[edited_index];
  char base_id[80];
  char edited_id[80];
  return base->kind == edited->kind &&
         identity_id_base(base->id, base_id, sizeof(base_id)) &&
         identity_id_base(edited->id, edited_id, sizeof(edited_id)) &&
         identity_text_eq(base_id, edited_id);
}

static size_t identity_count_edited_base_id_candidates(IdentityContext *context, const char *base_id, size_t *first) {
  char base[80];
  if (first) *first = identity_missing();
  if (!identity_id_base(base_id, base, sizeof(base))) return 0;
  size_t count = 0;
  for (size_t i = 0; context && i < context->edited->node_len; i++) {
    char edited_base[80];
    if (!identity_id_base(context->edited->nodes[i].id, edited_base, sizeof(edited_base))) continue;
    if (!identity_text_eq(base, edited_base)) continue;
    if (first && count == 0) *first = i;
    count++;
  }
  return count;
}

static bool identity_allows_import_name_disambiguated_base_collision(const ZProgramGraphNode *node) {
  return node &&
         identity_text_present(node->name) &&
         (node->kind == Z_PROGRAM_GRAPH_NODE_IMPORT || node->kind == Z_PROGRAM_GRAPH_NODE_C_IMPORT);
}

static void identity_reject_ambiguous_missing_base_ids(IdentityContext *context) {
  for (size_t i = 0; context && context->result.ok && i < context->base->node_len; i++) {
    if (context->base_to_edited[i] != identity_missing()) continue;
    if (identity_allows_import_name_disambiguated_base_collision(&context->base->nodes[i])) continue;
    size_t first = identity_missing();
    size_t count = identity_count_edited_base_id_candidates(context, context->base->nodes[i].id, &first);
    if (count <= 1) continue;
    identity_fail(context,
                  "GRC001",
                  "source edit has ambiguous graph identity",
                  context->base->nodes[i].id,
                  first == identity_missing() ? "" : context->edited->nodes[first].id);
    return;
  }
}

static size_t identity_count_base_candidates(IdentityContext *context, IdentityCandidateFn candidate, size_t edited_index, size_t *first) {
  size_t count = 0;
  if (first) *first = identity_missing();
  for (size_t i = 0; context && i < context->base->node_len; i++) {
    if (context->base_to_edited[i] != identity_missing() || !candidate(context, i, edited_index)) continue;
    if (first && count == 0) *first = i;
    count++;
  }
  return count;
}

static void identity_apply_unique_pass(IdentityContext *context, IdentityCandidateFn candidate) {
  for (size_t i = 0; context && context->result.ok && i < context->base->node_len; i++) {
    if (context->base_to_edited[i] != identity_missing()) continue;
    size_t first = identity_missing();
    size_t count = 0;
    for (size_t j = 0; j < context->edited->node_len; j++) {
      if (context->edited_matched[j] || !candidate(context, i, j)) continue;
      if (count == 0) first = j;
      count++;
    }
    if (count == 0) continue;
    if (count > 1) {
      identity_fail(context, "GRC001", "source edit has ambiguous graph identity", context->base->nodes[i].id, "");
      return;
    }
    size_t reverse_first = identity_missing();
    size_t reverse_count = identity_count_base_candidates(context, candidate, first, &reverse_first);
    if (reverse_count != 1 || reverse_first != i) {
      identity_fail(context, "GRC001", "source edit has ambiguous graph identity", context->base->nodes[i].id, context->edited->nodes[first].id);
      return;
    }
    identity_match_nodes(context, i, first);
  }
}

static void identity_detect_ordered_payload_swap(IdentityContext *context) {
  for (size_t i = 0; context && context->result.ok && i < context->base->node_len; i++) {
    size_t edited_i = context->base_to_edited[i];
    if (edited_i == identity_missing()) continue;
    const ZProgramGraphNode *base_i = &context->base->nodes[i];
    const ZProgramGraphNode *edited_node_i = &context->edited->nodes[edited_i];
    if (identity_node_payload_eq(base_i, edited_node_i)) continue;
    size_t base_edge_i = identity_owner_edge_index(context->base, i);
    size_t edited_edge_i = identity_owner_edge_index(context->edited, edited_i);
    if (base_edge_i == identity_missing() || edited_edge_i == identity_missing()) continue;
    for (size_t k = i + 1; k < context->base->node_len; k++) {
      size_t edited_k = context->base_to_edited[k];
      if (edited_k == identity_missing()) continue;
      const ZProgramGraphNode *base_k = &context->base->nodes[k];
      const ZProgramGraphNode *edited_node_k = &context->edited->nodes[edited_k];
      if (base_i->kind != base_k->kind || identity_node_payload_eq(base_k, edited_node_k)) continue;
      size_t base_edge_k = identity_owner_edge_index(context->base, k);
      size_t edited_edge_k = identity_owner_edge_index(context->edited, edited_k);
      if (base_edge_k == identity_missing() || edited_edge_k == identity_missing()) continue;
      const ZProgramGraphEdge *base_owner_i = &context->base->edges[base_edge_i];
      const ZProgramGraphEdge *base_owner_k = &context->base->edges[base_edge_k];
      const ZProgramGraphEdge *edited_owner_i = &context->edited->edges[edited_edge_i];
      const ZProgramGraphEdge *edited_owner_k = &context->edited->edges[edited_edge_k];
      if (!identity_text_eq(base_owner_i->from, base_owner_k->from) ||
          !identity_text_eq(base_owner_i->kind, base_owner_k->kind) ||
          !identity_text_eq(edited_owner_i->from, edited_owner_k->from) ||
          !identity_text_eq(edited_owner_i->kind, edited_owner_k->kind)) {
        continue;
      }
      if (identity_node_payload_eq(base_i, edited_node_k) && identity_node_payload_eq(base_k, edited_node_i)) {
        identity_fail(context, "GRC001", "source edit has ambiguous graph identity", base_i->id, edited_node_i->id);
        return;
      }
    }
  }
}

static bool identity_assignments_unique(IdentityContext *context) {
  for (size_t i = 0; context && i < context->edited->node_len; i++) {
    const char *id_i = context->edited->nodes[i].id;
    for (size_t j = i + 1; j < context->edited->node_len; j++) {
      if (!identity_text_eq(id_i, context->edited->nodes[j].id)) continue;
      identity_fail(context, "GRC001", "source edit has ambiguous graph identity", id_i, context->edited->nodes[j].id);
      return false;
    }
  }
  return true;
}

static size_t identity_find_old_id(char **old_ids, size_t len, const char *id) {
  for (size_t i = 0; old_ids && id && i < len; i++) {
    if (identity_text_eq(old_ids[i], id)) return i;
  }
  return identity_missing();
}

static void identity_apply_ids(IdentityContext *context) {
  char **old_ids = z_checked_calloc(context->edited->node_len ? context->edited->node_len : 1, sizeof(char *));
  for (size_t i = 0; i < context->edited->node_len; i++) old_ids[i] = z_strdup(context->edited->nodes[i].id ? context->edited->nodes[i].id : "");

  for (size_t i = 0; i < context->base->node_len; i++) {
    size_t edited_index = context->base_to_edited[i];
    if (edited_index == identity_missing()) continue;
    free(context->edited->nodes[edited_index].id);
    context->edited->nodes[edited_index].id = z_strdup(context->base->nodes[i].id ? context->base->nodes[i].id : "");
  }
  if (!identity_assignments_unique(context)) {
    for (size_t i = 0; i < context->edited->node_len; i++) free(old_ids[i]);
    free(old_ids);
    return;
  }
  for (size_t i = 0; i < context->edited->edge_len; i++) {
    ZProgramGraphEdge *edge = &context->edited->edges[i];
    size_t from = identity_find_old_id(old_ids, context->edited->node_len, edge->from);
    if (from != identity_missing()) {
      free(edge->from);
      edge->from = z_strdup(context->edited->nodes[from].id);
    }
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) continue;
    size_t to = identity_find_old_id(old_ids, context->edited->node_len, edge->to);
    if (to != identity_missing()) {
      free(edge->to);
      edge->to = z_strdup(context->edited->nodes[to].id);
    }
  }
  for (size_t i = 0; i < context->edited->node_len; i++) free(old_ids[i]);
  free(old_ids);
  z_program_graph_finalize_identities(context->edited);
}

bool z_program_graph_preserve_source_node_ids(const ZProgramGraph *base, ZProgramGraph *edited, ZProgramGraphIdentityReconcile *out) {
  if (out) *out = (ZProgramGraphIdentityReconcile){.ok = true};
  if (!base || !edited) return true;
  IdentityContext context = {
    .base = base,
    .edited = edited,
    .base_to_edited = z_checked_calloc(base->node_len ? base->node_len : 1, sizeof(size_t)),
    .edited_matched = z_checked_calloc(edited->node_len ? edited->node_len : 1, sizeof(bool)),
    .result = {.ok = true},
  };
  for (size_t i = 0; i < base->node_len; i++) context.base_to_edited[i] = identity_missing();
  if (!identity_text_eq(base->module_identity, edited->module_identity)) {
    identity_fail(&context, "GRC003", "edited source has a different module identity", base->module_identity, edited->module_identity);
  }
  identity_match_exact_ids(&context);
  identity_reject_ambiguous_missing_base_ids(&context);
  identity_apply_unique_pass(&context, identity_source_anchor_candidate);
  identity_apply_unique_pass(&context, identity_symbol_candidate);
  identity_apply_unique_pass(&context, identity_owner_child_candidate);
  identity_apply_unique_pass(&context, identity_base_id_candidate);
  identity_detect_ordered_payload_swap(&context);
  if (context.result.ok) {
    for (size_t i = 0; i < base->node_len; i++) {
      if (context.base_to_edited[i] == identity_missing()) context.result.deleted++;
      else context.result.preserved++;
    }
    for (size_t i = 0; i < edited->node_len; i++) {
      if (!context.edited_matched[i]) context.result.inserted++;
    }
    identity_apply_ids(&context);
  }
  if (out) *out = context.result;
  bool ok = context.result.ok;
  free(context.edited_matched);
  free(context.base_to_edited);
  return ok;
}
