#include "program_graph_repository_merge.h"

#include "program_graph_projection.h"
#include "program_graph_reconcile_apply.h"
#include "program_graph_view.h"
#include "zero.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool merge_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static char *merge_strdup(const char *text) {
  return text ? z_strdup(text) : NULL;
}

static char *merge_dirname(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  if (!slash) return z_strdup(".");
  if (slash == path) return z_strdup("/");
  return z_strndup(path, (size_t)(slash - path));
}

static bool merge_fail(
  ZRepositoryGraphMergeResult *result,
  ZDiag *diag,
  const char *code,
  const char *message,
  const char *node_id,
  const char *source_path,
  const char *semantic_object,
  const char *field
) {
  if (result) {
    result->ok = false;
    result->conflicts++;
    snprintf(result->code, sizeof(result->code), "%s", code ? code : "RGM000");
    snprintf(result->message, sizeof(result->message), "%s", message ? message : "repository graph merge conflict");
    snprintf(result->node_id, sizeof(result->node_id), "%s", node_id ? node_id : "");
    snprintf(result->source_path, sizeof(result->source_path), "%s", source_path ? source_path : "");
    snprintf(result->semantic_object, sizeof(result->semantic_object), "%s", semantic_object ? semantic_object : "");
    snprintf(result->field, sizeof(result->field), "%s", field ? field : "");
  }
  if (diag) {
    *diag = (ZDiag){0};
    diag->code = 1002;
    diag->path = source_path && source_path[0] ? source_path : "zero.graph";
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "repository graph merge conflict");
    snprintf(diag->expected, sizeof(diag->expected), "independent repository graph edits");
    snprintf(diag->actual, sizeof(diag->actual), "%s", node_id && node_id[0] ? node_id : "conflicting graph facts");
    snprintf(diag->help, sizeof(diag->help), "resolve the graph conflict by editing one side, then rerun zero graph merge");
  }
  return false;
}

static size_t merge_missing_index(void) {
  return (size_t)-1;
}

static size_t merge_find_node(const ZProgramGraph *graph, const char *id) {
  if (!graph || !id || !id[0]) return merge_missing_index();
  for (size_t i = 0; i < graph->node_len; i++) {
    if (merge_text_eq(graph->nodes[i].id, id)) return i;
  }
  return merge_missing_index();
}

static bool merge_graph_has_node(const ZProgramGraph *graph, const char *id) {
  return merge_find_node(graph, id) != merge_missing_index();
}

static const ZProgramGraphNode *merge_node_by_id(const ZProgramGraph *graph, const char *id) {
  size_t index = merge_find_node(graph, id);
  return index == merge_missing_index() ? NULL : &graph->nodes[index];
}

static void merge_copy_node(const ZProgramGraphNode *from, ZProgramGraphNode *to) {
  *to = (ZProgramGraphNode){0};
  to->id = merge_strdup(from->id);
  to->kind = from->kind;
  to->name = merge_strdup(from->name);
  to->type = merge_strdup(from->type);
  to->value = merge_strdup(from->value);
  to->path = merge_strdup(from->path);
  to->symbol_id = merge_strdup(from->symbol_id);
  to->type_id = merge_strdup(from->type_id);
  to->effect_id = merge_strdup(from->effect_id);
  to->node_hash = merge_strdup(from->node_hash);
  to->line = from->line;
  to->column = from->column;
  to->is_public = from->is_public;
  to->is_mutable = from->is_mutable;
  to->is_static = from->is_static;
  to->fallible = from->fallible;
  to->export_c = from->export_c;
}

static bool merge_add_node(ZProgramGraph *graph, const ZProgramGraphNode *node) {
  if (!graph || !node || !node->id || merge_graph_has_node(graph, node->id)) return true;
  if (graph->node_len == graph->node_cap) {
    size_t next = z_grow_capacity(graph->node_cap, graph->node_len + 1, 32);
    graph->nodes = z_checked_reallocarray(graph->nodes, next, sizeof(ZProgramGraphNode));
    graph->node_cap = next;
  }
  merge_copy_node(node, &graph->nodes[graph->node_len++]);
  return true;
}

static bool merge_node_semantic_eq(const ZProgramGraphNode *left, const ZProgramGraphNode *right) {
  return left &&
         right &&
         merge_text_eq(left->id, right->id) &&
         left->kind == right->kind &&
         merge_text_eq(left->name, right->name) &&
         merge_text_eq(left->type, right->type) &&
         merge_text_eq(left->value, right->value) &&
         left->is_public == right->is_public &&
         left->is_mutable == right->is_mutable &&
         left->is_static == right->is_static &&
         left->fallible == right->fallible &&
         left->export_c == right->export_c &&
         merge_text_eq(left->node_hash, right->node_hash);
}

static bool merge_node_projection_eq(const ZProgramGraphNode *left, const ZProgramGraphNode *right) {
  return merge_node_semantic_eq(left, right) && merge_text_eq(left->path, right->path);
}

static bool merge_node_payload_changed_from_base(const ZProgramGraphNode *base, const ZProgramGraphNode *side) {
  if (!base || !side) return base != side;
  return !merge_node_semantic_eq(base, side);
}

static bool merge_node_path_changed_from_base(const ZProgramGraphNode *base, const ZProgramGraphNode *side) {
  if (!base || !side) return base != side;
  return !merge_text_eq(base->path, side->path);
}

static void merge_semantic_object(const ZProgramGraphNode *node, char *buf, size_t len) {
  if (!buf || len == 0) return;
  if (!node) {
    snprintf(buf, len, "missing node");
    return;
  }
  snprintf(buf,
           len,
           "%s %s",
           z_program_graph_node_kind_name(node->kind),
           node->name && node->name[0] ? node->name : (node->value && node->value[0] ? node->value : ""));
}

static bool merge_nodes(
  const ZProgramGraph *base,
  const ZProgramGraph *left,
  const ZProgramGraph *right,
  ZProgramGraph *out,
  ZRepositoryGraphMergeResult *result,
  ZDiag *diag
) {
  for (size_t i = 0; base && i < base->node_len; i++) {
    const ZProgramGraphNode *base_node = &base->nodes[i];
    const ZProgramGraphNode *left_node = merge_node_by_id(left, base_node->id);
    const ZProgramGraphNode *right_node = merge_node_by_id(right, base_node->id);
    bool left_deleted = left_node == NULL;
    bool right_deleted = right_node == NULL;
    bool left_payload_changed = merge_node_payload_changed_from_base(base_node, left_node);
    bool right_payload_changed = merge_node_payload_changed_from_base(base_node, right_node);
    bool left_path_changed = merge_node_path_changed_from_base(base_node, left_node);
    bool right_path_changed = merge_node_path_changed_from_base(base_node, right_node);
    bool left_changed = left_payload_changed || left_path_changed;
    bool right_changed = right_payload_changed || right_path_changed;
    const ZProgramGraphNode *chosen = base_node;
    if (left_deleted && right_deleted) {
      result->left_changes++;
      result->right_changes++;
      continue;
    }
    if ((left_deleted && right_changed) || (right_deleted && left_changed)) {
      char object[160];
      merge_semantic_object(left_node ? left_node : (right_node ? right_node : base_node), object, sizeof(object));
      return merge_fail(result,
                        diag,
                        "RGM002",
                        "repository graph node was changed on both sides",
                        base_node->id,
                        base_node->path,
                        object,
                        "node");
    }
    if (left_deleted || right_deleted) {
      if (left_deleted) result->left_changes++;
      if (right_deleted) result->right_changes++;
      continue;
    }
    if (left_payload_changed && right_payload_changed) {
      if (merge_node_semantic_eq(left_node, right_node)) {
        chosen = left_node;
      } else {
        char object[160];
        merge_semantic_object(left_node ? left_node : (right_node ? right_node : base_node), object, sizeof(object));
        return merge_fail(result,
                          diag,
                          "RGM002",
                          "repository graph node was changed on both sides",
                          base_node->id,
                          base_node->path,
                          object,
                          "node");
      }
    } else if (left_payload_changed) {
      chosen = left_node;
    } else if (right_payload_changed) {
      chosen = right_node;
    }
    if (left_path_changed && right_path_changed && !merge_text_eq(left_node->path, right_node->path)) {
      char object[160];
      merge_semantic_object(chosen, object, sizeof(object));
      return merge_fail(result,
                        diag,
                        "RGM002",
                        "repository graph node source path changed on both sides",
                        base_node->id,
                        base_node->path,
                        object,
                        "path");
    }
    ZProgramGraphNode merged_node = *chosen;
    if (left_path_changed) merged_node.path = left_node->path;
    else if (right_path_changed) merged_node.path = right_node->path;
    if (left_changed) result->left_changes++;
    if (right_changed) result->right_changes++;
    merge_add_node(out, &merged_node);
  }

  for (size_t i = 0; left && i < left->node_len; i++) {
    const ZProgramGraphNode *left_node = &left->nodes[i];
    if (merge_graph_has_node(base, left_node->id) || merge_graph_has_node(out, left_node->id)) continue;
    const ZProgramGraphNode *right_node = merge_node_by_id(right, left_node->id);
    if (right_node && !merge_node_projection_eq(left_node, right_node)) {
      char object[160];
      merge_semantic_object(left_node, object, sizeof(object));
      return merge_fail(result,
                        diag,
                        "RGM003",
                        "repository graph inserted the same node id with different content",
                        left_node->id,
                        left_node->path,
                        object,
                        "node");
    }
    result->left_changes++;
    if (right_node) result->right_changes++;
    merge_add_node(out, left_node);
  }
  for (size_t i = 0; right && i < right->node_len; i++) {
    const ZProgramGraphNode *right_node = &right->nodes[i];
    if (merge_graph_has_node(base, right_node->id) || merge_graph_has_node(out, right_node->id)) continue;
    result->right_changes++;
    merge_add_node(out, right_node);
  }
  return true;
}

static bool merge_edge_exact_eq(const ZProgramGraphEdge *left, const ZProgramGraphEdge *right) {
  return left &&
         right &&
         merge_text_eq(left->from, right->from) &&
         merge_text_eq(left->to, right->to) &&
         merge_text_eq(left->kind, right->kind) &&
         left->target == right->target &&
         left->order == right->order;
}

static bool merge_node_is_source_path(const ZProgramGraphNode *node, const char *path) {
  return node && path && path[0] && merge_text_eq(node->path, path);
}

static bool merge_edge_is_source_path(const ZProgramGraph *graph, const ZProgramGraphEdge *edge, const char *path) {
  if (!graph || !edge || !path || !path[0]) return false;
  const ZProgramGraphNode *from = merge_node_by_id(graph, edge->from);
  if (!merge_node_is_source_path(from, path)) return false;
  if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) return true;
  const ZProgramGraphNode *to = merge_node_by_id(graph, edge->to);
  return merge_node_is_source_path(to, path);
}

static bool merge_graph_has_exact_edge(const ZProgramGraph *graph, const ZProgramGraphEdge *edge) {
  for (size_t i = 0; graph && edge && i < graph->edge_len; i++) {
    if (merge_edge_exact_eq(&graph->edges[i], edge)) return true;
  }
  return false;
}

static bool merge_graph_source_path_eq(const ZProgramGraph *merged, const ZProgramGraph *side, const char *path) {
  size_t merged_nodes = 0;
  size_t side_nodes = 0;
  for (size_t i = 0; merged && i < merged->node_len; i++) {
    const ZProgramGraphNode *node = &merged->nodes[i];
    if (!merge_node_is_source_path(node, path)) continue;
    const ZProgramGraphNode *side_node = merge_node_by_id(side, node->id);
    if (!merge_node_projection_eq(node, side_node)) return false;
    merged_nodes++;
  }
  for (size_t i = 0; side && i < side->node_len; i++) {
    if (merge_node_is_source_path(&side->nodes[i], path)) side_nodes++;
  }
  if (merged_nodes != side_nodes) return false;

  size_t merged_edges = 0;
  size_t side_edges = 0;
  for (size_t i = 0; merged && i < merged->edge_len; i++) {
    const ZProgramGraphEdge *edge = &merged->edges[i];
    if (!merge_edge_is_source_path(merged, edge, path)) continue;
    if (!merge_graph_has_exact_edge(side, edge)) return false;
    merged_edges++;
  }
  for (size_t i = 0; side && i < side->edge_len; i++) {
    if (merge_edge_is_source_path(side, &side->edges[i], path)) side_edges++;
  }
  return merged_edges == side_edges;
}

static bool merge_edge_same_slot(const ZProgramGraphEdge *left, const ZProgramGraphEdge *right) {
  return left &&
         right &&
         merge_text_eq(left->from, right->from) &&
         merge_text_eq(left->kind, right->kind) &&
         left->target == right->target &&
         left->order == right->order;
}

static bool merge_edge_node_targets_present(const ZProgramGraph *graph, const ZProgramGraphEdge *edge) {
  if (!graph || !edge) return false;
  if (!merge_graph_has_node(graph, edge->from)) return false;
  if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && !merge_graph_has_node(graph, edge->to)) return false;
  return true;
}

static bool merge_edge_exists(const ZProgramGraph *graph, const ZProgramGraphEdge *edge) {
  for (size_t i = 0; graph && edge && i < graph->edge_len; i++) {
    if (merge_edge_exact_eq(&graph->edges[i], edge)) return true;
  }
  return false;
}

static const ZProgramGraphEdge *merge_edge_slot_conflict(const ZProgramGraph *graph, const ZProgramGraphEdge *edge) {
  for (size_t i = 0; graph && edge && i < graph->edge_len; i++) {
    if (merge_edge_same_slot(&graph->edges[i], edge) && !merge_edge_exact_eq(&graph->edges[i], edge)) return &graph->edges[i];
  }
  return NULL;
}

static const ZProgramGraphEdge *merge_edge_by_slot(const ZProgramGraph *graph, const ZProgramGraphEdge *edge) {
  for (size_t i = 0; graph && edge && i < graph->edge_len; i++) {
    if (merge_edge_same_slot(&graph->edges[i], edge)) return &graph->edges[i];
  }
  return NULL;
}

static bool merge_edge_slot_exists(const ZProgramGraph *graph, const ZProgramGraphEdge *edge) {
  return merge_edge_by_slot(graph, edge) != NULL;
}

static bool merge_edge_changed_from_base(const ZProgramGraphEdge *base, const ZProgramGraphEdge *side) {
  return !merge_edge_exact_eq(base, side);
}

static void merge_copy_edge(const ZProgramGraphEdge *from, ZProgramGraphEdge *to) {
  *to = (ZProgramGraphEdge){0};
  to->from = merge_strdup(from->from);
  to->to = merge_strdup(from->to);
  to->kind = merge_strdup(from->kind);
  to->target = from->target;
  to->order = from->order;
}

static bool merge_add_edge(
  ZProgramGraph *graph,
  const ZProgramGraphEdge *edge,
  ZRepositoryGraphMergeResult *result,
  ZDiag *diag
) {
  if (!merge_edge_node_targets_present(graph, edge) || merge_edge_exists(graph, edge)) return true;
  const ZProgramGraphEdge *conflict = merge_edge_slot_conflict(graph, edge);
  if (conflict) {
    const ZProgramGraphNode *from = merge_node_by_id(graph, edge->from);
    char object[160];
    merge_semantic_object(from, object, sizeof(object));
    (void)conflict;
    return merge_fail(result,
                      diag,
                      "RGM004",
                      "repository graph edge order slot changed on both sides",
                      edge->from,
                      from ? from->path : "",
                      object,
                      edge->kind);
  }
  if (graph->edge_len == graph->edge_cap) {
    size_t next = z_grow_capacity(graph->edge_cap, graph->edge_len + 1, 32);
    graph->edges = z_checked_reallocarray(graph->edges, next, sizeof(ZProgramGraphEdge));
    graph->edge_cap = next;
  }
  merge_copy_edge(edge, &graph->edges[graph->edge_len++]);
  return true;
}

static bool merge_edges(
  const ZProgramGraph *base,
  const ZProgramGraph *left,
  const ZProgramGraph *right,
  ZProgramGraph *out,
  ZRepositoryGraphMergeResult *result,
  ZDiag *diag
) {
  for (size_t i = 0; base && i < base->edge_len; i++) {
    const ZProgramGraphEdge *base_edge = &base->edges[i];
    const ZProgramGraphEdge *left_edge = merge_edge_by_slot(left, base_edge);
    const ZProgramGraphEdge *right_edge = merge_edge_by_slot(right, base_edge);
    bool left_changed = merge_edge_changed_from_base(base_edge, left_edge);
    bool right_changed = merge_edge_changed_from_base(base_edge, right_edge);
    const ZProgramGraphEdge *chosen = base_edge;
    if (left_changed && right_changed) {
      if (!left_edge && !right_edge) {
        result->left_changes++;
        result->right_changes++;
        continue;
      }
      if (left_edge && right_edge && merge_edge_exact_eq(left_edge, right_edge)) {
        chosen = left_edge;
        result->left_changes++;
        result->right_changes++;
      } else {
        const ZProgramGraphNode *from = merge_node_by_id(out, base_edge->from);
        char object[160];
        merge_semantic_object(from, object, sizeof(object));
        return merge_fail(result,
                          diag,
                          "RGM004",
                          "repository graph edge was changed on both sides",
                          base_edge->from,
                          from ? from->path : "",
                          object,
                          base_edge->kind);
      }
    } else if (left_changed) {
      result->left_changes++;
      if (!left_edge) continue;
      chosen = left_edge;
    } else if (right_changed) {
      result->right_changes++;
      if (!right_edge) continue;
      chosen = right_edge;
    }
    if (!merge_add_edge(out, chosen, result, diag)) return false;
  }
  for (size_t i = 0; left && i < left->edge_len; i++) {
    const ZProgramGraphEdge *left_edge = &left->edges[i];
    if (merge_edge_slot_exists(base, left_edge) || merge_edge_exists(out, left_edge)) continue;
    const ZProgramGraphEdge *right_edge = merge_edge_by_slot(right, left_edge);
    if (right_edge && !merge_edge_exact_eq(left_edge, right_edge)) {
      const ZProgramGraphNode *from = merge_node_by_id(out, left_edge->from);
      char object[160];
      merge_semantic_object(from, object, sizeof(object));
      return merge_fail(result,
                        diag,
                        "RGM004",
                        "repository graph inserted the same edge slot with different content",
                        left_edge->from,
                        from ? from->path : "",
                        object,
                        left_edge->kind);
    }
    result->left_changes++;
    if (right_edge) result->right_changes++;
    if (!merge_add_edge(out, &left->edges[i], result, diag)) return false;
  }
  for (size_t i = 0; right && i < right->edge_len; i++) {
    if (merge_edge_slot_exists(base, &right->edges[i]) || merge_edge_exists(out, &right->edges[i])) continue;
    result->right_changes++;
    if (!merge_add_edge(out, &right->edges[i], result, diag)) return false;
  }
  return true;
}

static bool merge_projection_path_seen(char **paths, size_t len, const char *path) {
  for (size_t i = 0; i < len; i++) {
    if (merge_text_eq(paths[i], path)) return true;
  }
  return false;
}

static bool merge_add_projection(ZProgramGraphStore *store, const char *path, const char *text) {
  if (!store || !path || !path[0] || !text || !z_program_graph_store_source_path_is_local(path)) return true;
  if (merge_projection_path_seen(store->projection_paths, store->projection_len, path)) return true;
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

static void merge_sort_projections(ZProgramGraphStore *store) {
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

static bool merge_projection_record_eq(const char *left, const char *right) {
  if ((left == NULL) != (right == NULL)) return false;
  return left == NULL || merge_text_eq(left, right);
}

static bool merge_projection_candidate_valid(
  const ZProgramGraphStore *merged,
  const ZProgramGraphStore *side,
  bool side_projections_match_graph,
  const char *path,
  const char *text
) {
  return merged &&
         side &&
         side_projections_match_graph &&
         path &&
         path[0] &&
         text != NULL &&
         z_program_graph_store_projection_text(side, path) != NULL &&
         merge_graph_source_path_eq(&merged->graph, &side->graph, path);
}

static bool merge_projection_text_is_graph_view(const ZProgramGraphStore *store, const char *path, const char *text) {
  if (!store || !path || !path[0] || text == NULL || !z_program_graph_store_source_path_is_local(path)) return false;
  ZBuf view;
  zbuf_init(&view);
  bool ok = z_program_graph_append_source_view(&view, &store->graph, path, NULL);
  bool matches = ok && merge_text_eq(view.data ? view.data : "", text);
  zbuf_free(&view);
  return matches;
}

static bool merge_projection_has_source_only_change(
  const ZProgramGraphStore *side,
  bool side_projections_match_graph,
  const char *path,
  const char *base_text,
  const char *side_text
) {
  return side &&
         side_projections_match_graph &&
         !merge_projection_record_eq(base_text, side_text) &&
         side_text != NULL &&
         !merge_projection_text_is_graph_view(side, path, side_text);
}

static bool merge_store_projections_match_graph(const ZProgramGraphStore *store, const ZTargetInfo *target) {
  return store && store->projection_len > 0 && z_program_graph_projection_store_matches_graph(store, target, NULL);
}

static bool merge_refresh_graph_from_projections(ZProgramGraphStore *store, const ZTargetInfo *target, ZRepositoryGraphMergeResult *result, ZDiag *diag) {
  ZProgramGraph refreshed;
  z_program_graph_init(&refreshed);
  ZDiag projection_diag = {0};
  bool ok = z_program_graph_projection_graph_from_store(store, target, &refreshed, &projection_diag);
  if (!ok) {
    z_program_graph_free(&refreshed);
    return merge_fail(result,
                      diag,
                      "RGM005",
                      projection_diag.message[0] ? projection_diag.message : "repository graph source projection does not match stored graph",
                      "",
                      store ? store->path : "",
                      projection_diag.actual[0] ? projection_diag.actual : "projection graph mismatch",
                      "projection");
  }
  if (ok) {
    ZProgramGraphIdentityReconcile identity = {0};
    ok = z_program_graph_preserve_source_node_ids(&store->graph, &refreshed, &identity);
    if (!ok) {
      ok = merge_fail(result,
                      diag,
                      "RGM005",
                      identity.message[0] ? identity.message : "repository graph source projection identity is ambiguous",
                      identity.node_id,
                      store ? store->path : "",
                      identity.candidate_id[0] ? identity.candidate_id : "ambiguous projection identity",
                      "projection");
    }
  }
  if (ok) {
    z_program_graph_free(&store->graph);
    store->graph = refreshed;
  } else {
    z_program_graph_free(&refreshed);
  }
  return ok;
}

static bool merge_projection_conflict(
  ZRepositoryGraphMergeResult *result,
  ZDiag *diag,
  const char *path,
  const char *message
) {
  return merge_fail(result,
                    diag,
                    "RGM005",
                    message ? message : "repository graph projection changed on both sides",
                    "",
                    path,
                    path,
                    "projection");
}

static bool merge_choose_projection_text(
  const ZProgramGraphStore *base,
  const ZProgramGraphStore *left,
  const ZProgramGraphStore *right,
  const ZProgramGraphStore *merged,
  bool base_projections_match_graph,
  bool left_projections_match_graph,
  bool right_projections_match_graph,
  const char *path,
  const char **chosen,
  ZRepositoryGraphMergeResult *result,
  ZDiag *diag
) {
  if (chosen) *chosen = NULL;
  const char *base_text = z_program_graph_store_projection_text(base, path);
  const char *left_text = z_program_graph_store_projection_text(left, path);
  const char *right_text = z_program_graph_store_projection_text(right, path);
  bool left_changed = !merge_projection_record_eq(base_text, left_text);
  bool right_changed = !merge_projection_record_eq(base_text, right_text);
  bool base_valid = merge_projection_candidate_valid(merged, base, base_projections_match_graph, path, base_text);
  bool left_valid = merge_projection_candidate_valid(merged, left, left_projections_match_graph, path, left_text);
  bool right_valid = merge_projection_candidate_valid(merged, right, right_projections_match_graph, path, right_text);
  bool left_source_only = merge_projection_has_source_only_change(left, left_projections_match_graph, path, base_text, left_text);
  bool right_source_only = merge_projection_has_source_only_change(right, right_projections_match_graph, path, base_text, right_text);

  if ((left_source_only && !left_valid) || (right_source_only && !right_valid)) {
    return merge_projection_conflict(result, diag, path, "repository graph projection-only edit conflicts with graph changes");
  }

  if (!left_changed && !right_changed) {
    if (chosen && base_valid) *chosen = base_text;
    return true;
  }
  if (left_changed && !right_changed) {
    if (chosen && left_valid) *chosen = left_text;
    return true;
  }
  if (!left_changed && right_changed) {
    if (chosen && right_valid) *chosen = right_text;
    return true;
  }
  if (merge_projection_record_eq(left_text, right_text)) {
    if (chosen && left_valid) *chosen = left_text;
    return true;
  }
  if (left_valid && right_valid) {
    return merge_projection_conflict(result, diag, path, "repository graph projection changed on both sides");
  }
  if (chosen && left_valid) *chosen = left_text;
  else if (chosen && right_valid) *chosen = right_text;
  (void)base_valid;
  return true;
}

static bool merge_projection_from_graph(ZProgramGraphStore *store, const char *source_path, ZDiag *diag) {
  if (!source_path || !source_path[0] || !z_program_graph_store_source_path_is_local(source_path)) return true;
  ZBuf view;
  zbuf_init(&view);
  bool ok = z_program_graph_append_source_view(&view, &store->graph, source_path, diag);
  if (ok) merge_add_projection(store, source_path, view.data ? view.data : "");
  zbuf_free(&view);
  return ok;
}

static bool merge_build_projections(
  const ZProgramGraphStore *base,
  const ZProgramGraphStore *left,
  const ZProgramGraphStore *right,
  const ZTargetInfo *target,
  ZProgramGraphStore *store,
  ZRepositoryGraphMergeResult *result,
  ZDiag *diag
) {
  bool base_projections_match_graph = merge_store_projections_match_graph(base, target);
  bool left_projections_match_graph = merge_store_projections_match_graph(left, target);
  bool right_projections_match_graph = merge_store_projections_match_graph(right, target);
  for (size_t i = 0; store && i < store->graph.node_len; i++) {
    const char *path = store->graph.nodes[i].path;
    if (!path || !path[0] || merge_projection_path_seen(store->projection_paths, store->projection_len, path)) continue;
    const char *tracked_text = NULL;
    if (!merge_choose_projection_text(base,
                                      left,
                                      right,
                                      store,
                                      base_projections_match_graph,
                                      left_projections_match_graph,
                                      right_projections_match_graph,
                                      path,
                                      &tracked_text,
                                      result,
                                      diag)) {
      return false;
    }
    if (tracked_text != NULL) {
      merge_add_projection(store, path, tracked_text);
      continue;
    }
    if (!merge_projection_from_graph(store, path, diag)) {
      return merge_fail(result,
                        diag,
                        "RGM005",
                        "repository graph merge could not regenerate source projection",
                        store->graph.nodes[i].id,
                        path,
                        store->graph.nodes[i].name,
                        "projection");
    }
  }
  merge_sort_projections(store);
  if (store && store->projection_len > 0 && !merge_refresh_graph_from_projections(store, target, result, diag)) return false;
  if (store && store->projection_len > 0 && !z_program_graph_projection_store_matches_graph(store, target, diag)) {
    return merge_fail(result,
                      diag,
                      "RGM005",
                      "repository graph merge could not produce matching source projections",
                      "",
                      store->path,
                      diag && diag->actual[0] ? diag->actual : "source projection mismatch",
                      "projection");
  }
  return true;
}

static bool merge_graph_compatible(
  const ZProgramGraph *base,
  const ZProgramGraph *left,
  const ZProgramGraph *right,
  ZRepositoryGraphMergeResult *result,
  ZDiag *diag
) {
  if (!base || !left || !right) return merge_fail(result, diag, "RGM001", "repository graph merge input is missing", "", "", "", "store");
  if (base->schema_version != left->schema_version || base->schema_version != right->schema_version) {
    return merge_fail(result, diag, "RGM001", "repository graph schema version differs", "", "", "", "schemaVersion");
  }
  if (!merge_text_eq(base->module_identity, left->module_identity) ||
      !merge_text_eq(base->module_identity, right->module_identity)) {
    return merge_fail(result, diag, "RGM001", "repository graph module identity differs", "", "", "", "moduleIdentity");
  }
  return true;
}

bool z_repository_graph_merge_stores(
  const ZProgramGraphStore *base,
  const ZProgramGraphStore *left,
  const ZProgramGraphStore *right,
  const ZTargetInfo *target,
  const char *target_path,
  ZProgramGraphStore *merged,
  ZRepositoryGraphMergeResult *result,
  ZDiag *diag
) {
  if (result) *result = (ZRepositoryGraphMergeResult){.ok = true};
  if (!merged) return merge_fail(result, diag, "RGM001", "repository graph merge output is missing", "", "", "", "store");
  z_program_graph_store_init(merged);
  merged->path = z_strdup(target_path && target_path[0] ? target_path : "zero.graph");
  merged->root = merge_dirname(merged->path);
  merged->present = true;
  if (result) snprintf(result->target_path, sizeof(result->target_path), "%s", merged->path);

  if (!merge_graph_compatible(&base->graph, &left->graph, &right->graph, result, diag)) return false;

  ZProgramGraph *out = &merged->graph;
  z_program_graph_init(out);
  out->schema_version = base->graph.schema_version;
  out->validation_state = Z_PROGRAM_GRAPH_VALIDATION_SHAPE_VALID;
  out->canonical_source = base->graph.canonical_source || left->graph.canonical_source || right->graph.canonical_source;
  free(out->module_identity);
  out->module_identity = z_strdup(base->graph.module_identity ? base->graph.module_identity : "module:main");

  if (!merge_nodes(&base->graph, &left->graph, &right->graph, out, result, diag)) return false;
  if (!merge_edges(&base->graph, &left->graph, &right->graph, out, result, diag)) return false;

  z_program_graph_finalize_identities(out);
  if (!merge_build_projections(base, left, right, target, merged, result, diag)) return false;

  ZProgramGraphValidation validation = {0};
  if (!z_program_graph_validate(out, &validation)) {
    return merge_fail(result, diag, "RGM006", "merged repository graph failed validation", validation.node_id, "", validation.message, validation.code);
  }
  if (result) {
    result->ok = true;
    result->merged_nodes = out->node_len;
    result->merged_edges = out->edge_len;
  }
  return true;
}
