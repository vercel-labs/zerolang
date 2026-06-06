#include "program_graph_reconcile.h"

#include "program_graph_source_map.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool reconcile_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool reconcile_module_identity_changed(const ZProgramGraph *base, const ZProgramGraph *edited) {
  return !reconcile_text_eq(base ? base->module_identity : NULL, edited ? edited->module_identity : NULL);
}

static void reconcile_json_string(ZBuf *buf, const char *value) {
  zbuf_append_char(buf, '"');
  for (const unsigned char *cursor = (const unsigned char *)(value ? value : ""); *cursor; cursor++) {
    unsigned char ch = *cursor;
    switch (ch) {
      case '"': zbuf_append(buf, "\\\""); break;
      case '\\': zbuf_append(buf, "\\\\"); break;
      case '\n': zbuf_append(buf, "\\n"); break;
      case '\r': zbuf_append(buf, "\\r"); break;
      case '\t': zbuf_append(buf, "\\t"); break;
      default:
        if (ch < 0x20) zbuf_appendf(buf, "\\u%04x", (unsigned)ch);
        else zbuf_append_char(buf, (char)ch);
        break;
    }
  }
  zbuf_append_char(buf, '"');
}

static void reconcile_patch_string(ZBuf *buf, const char *value) {
  zbuf_append_char(buf, '"');
  for (const char *cursor = value ? value : ""; *cursor; cursor++) {
    switch (*cursor) {
      case '"': zbuf_append(buf, "\\\""); break;
      case '\\': zbuf_append(buf, "\\\\"); break;
      case '\n': zbuf_append(buf, "\\n"); break;
      case '\r': zbuf_append(buf, "\\r"); break;
      case '\t': zbuf_append(buf, "\\t"); break;
      default: zbuf_append_char(buf, *cursor); break;
    }
  }
  zbuf_append_char(buf, '"');
}

static bool reconcile_id_base(const char *id, char *out, size_t out_len) {
  if (!id || !out || out_len == 0) return false;
  const char *underscore = strchr(id, '_');
  if (!underscore) {
    snprintf(out, out_len, "%s", id);
    return true;
  }
  const char *cursor = underscore + 1;
  for (int i = 0; i < 8; i++) {
    if (!isxdigit((unsigned char)cursor[i])) {
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

static const ZProgramGraphNode *reconcile_find_node(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (reconcile_text_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  }
  return NULL;
}

static bool reconcile_node_hash_eq(const ZProgramGraphNode *left, const ZProgramGraphNode *right) {
  return reconcile_text_eq(left ? left->node_hash : NULL, right ? right->node_hash : NULL);
}

static size_t reconcile_count_base_candidates(const ZProgramGraph *graph, const char *base_id, const ZProgramGraphNode **first) {
  char base[80];
  reconcile_id_base(base_id, base, sizeof(base));
  size_t count = 0;
  if (first) *first = NULL;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    char candidate[80];
    reconcile_id_base(graph->nodes[i].id, candidate, sizeof(candidate));
    if (!reconcile_text_eq(base, candidate)) continue;
    count++;
    if (first && !*first) *first = &graph->nodes[i];
  }
  return count;
}

static bool reconcile_is_candidate_for_missing_base(const ZProgramGraph *base, const ZProgramGraph *edited, const ZProgramGraphNode *edited_node) {
  if (!base || !edited_node || reconcile_find_node(base, edited_node->id)) return false;
  char edited_base[80];
  reconcile_id_base(edited_node->id, edited_base, sizeof(edited_base));
  for (size_t i = 0; i < base->node_len; i++) {
    if (reconcile_find_node(edited, base->nodes[i].id)) continue;
    char missing_base[80];
    reconcile_id_base(base->nodes[i].id, missing_base, sizeof(missing_base));
    if (reconcile_text_eq(edited_base, missing_base)) return true;
  }
  return false;
}

static bool reconcile_same_field(const char *left, const char *right) {
  return reconcile_text_eq(left, right);
}

static bool reconcile_simple_text_patch_field(const ZProgramGraphNode *base, const ZProgramGraphNode *edited, const char **field) {
  if (!base || !edited || base->kind != edited->kind) return false;
  int changed = 0;
  const char *changed_field = NULL;
  if (!reconcile_same_field(base->name, edited->name)) {
    changed++;
    changed_field = "name";
  }
  if (!reconcile_same_field(base->type, edited->type)) {
    changed++;
    changed_field = "type";
  }
  if (!reconcile_same_field(base->value, edited->value)) {
    changed++;
    changed_field = "value";
  }
  if (base->is_public != edited->is_public || base->is_mutable != edited->is_mutable || base->is_static != edited->is_static ||
      base->fallible != edited->fallible || base->export_c != edited->export_c) {
    changed++;
  }
  if (changed != 1 || !changed_field) return false;
  if (field) *field = changed_field;
  return true;
}

static const char *reconcile_field_value(const ZProgramGraphNode *node, const char *field) {
  if (reconcile_text_eq(field, "name")) return node ? node->name : "";
  if (reconcile_text_eq(field, "type")) return node ? node->type : "";
  if (reconcile_text_eq(field, "value")) return node ? node->value : "";
  return "";
}

static bool reconcile_append_patch_text(ZBuf *buf, const ZProgramGraph *base, const ZProgramGraph *edited) {
  bool wrote_edit = false;
  zbuf_append(buf, "zero-program-graph-patch v1\n");
  zbuf_append(buf, "expect graphHash ");
  reconcile_patch_string(buf, base ? base->graph_hash : "");
  zbuf_append_char(buf, '\n');
  for (size_t i = 0; base && i < base->node_len; i++) {
    const ZProgramGraphNode *before = &base->nodes[i];
    const ZProgramGraphNode *after = reconcile_find_node(edited, before->id);
    if (!after || reconcile_node_hash_eq(before, after)) continue;
    const char *field = NULL;
    if (!reconcile_simple_text_patch_field(before, after, &field)) return false;
    zbuf_append(buf, reconcile_text_eq(field, "name") ? "rename node=" : "set node=");
    reconcile_patch_string(buf, before->id);
    if (!reconcile_text_eq(field, "name")) {
      zbuf_append(buf, " field=");
      reconcile_patch_string(buf, field);
    }
    zbuf_append(buf, " expect=");
    reconcile_patch_string(buf, reconcile_field_value(before, field));
    zbuf_append(buf, " value=");
    reconcile_patch_string(buf, reconcile_field_value(after, field));
    zbuf_append_char(buf, '\n');
    wrote_edit = true;
  }
  return wrote_edit;
}

void z_program_graph_reconcile_summary(const ZProgramGraph *base, const ZProgramGraph *edited, ZProgramGraphReconcileSummary *out) {
  if (!out) return;
  *out = (ZProgramGraphReconcileSummary){.ok = true};
  out->module_identity_changed = reconcile_module_identity_changed(base, edited);
  for (size_t i = 0; base && i < base->node_len; i++) {
    const ZProgramGraphNode *before = &base->nodes[i];
    const ZProgramGraphNode *after = reconcile_find_node(edited, before->id);
    if (after) {
      if (reconcile_node_hash_eq(before, after)) out->unchanged++;
      else out->edited++;
      continue;
    }
    const ZProgramGraphNode *first = NULL;
    size_t candidates = reconcile_count_base_candidates(edited, before->id, &first);
    (void)first;
    if (candidates == 0) out->deleted++;
    else if (candidates == 1) out->identity_changed++;
    else out->ambiguous++;
  }
  for (size_t i = 0; edited && i < edited->node_len; i++) {
    if (!reconcile_find_node(base, edited->nodes[i].id) && !reconcile_is_candidate_for_missing_base(base, edited, &edited->nodes[i])) out->inserted++;
  }
  out->ok = !out->module_identity_changed && out->ambiguous == 0 && out->identity_changed == 0;
  if (out->ok && out->deleted == 0 && out->inserted == 0 && out->edited > 0) {
    ZBuf patch;
    zbuf_init(&patch);
    out->patch_available = reconcile_append_patch_text(&patch, base, edited);
    zbuf_free(&patch);
  }
}

static void reconcile_append_decision_json(ZBuf *buf,
                                           const char *status,
                                           const ZProgramGraphNode *before,
                                           const ZProgramGraphNode *after,
                                           const ZProgramGraph *range_graph,
                                           const char *path) {
  zbuf_append(buf, "{\"status\":");
  reconcile_json_string(buf, status);
  zbuf_append(buf, ",\"nodeId\":");
  reconcile_json_string(buf, before ? before->id : (after ? after->id : ""));
  zbuf_append(buf, ",\"candidateId\":");
  if (after && before && !reconcile_text_eq(before->id, after->id)) reconcile_json_string(buf, after->id);
  else zbuf_append(buf, "null");
  zbuf_append(buf, ",\"kind\":");
  reconcile_json_string(buf, before ? z_program_graph_node_kind_name(before->kind) : (after ? z_program_graph_node_kind_name(after->kind) : ""));
  zbuf_append(buf, ",\"name\":");
  reconcile_json_string(buf, after ? after->name : (before ? before->name : ""));
  zbuf_append(buf, ",\"beforeHash\":");
  reconcile_json_string(buf, before ? before->node_hash : "");
  zbuf_append(buf, ",\"afterHash\":");
  reconcile_json_string(buf, after ? after->node_hash : "");
  zbuf_append(buf, ",\"sourceRange\":");
  z_program_graph_append_source_range_for_graph_json(buf, range_graph, after ? after : before, path);
  zbuf_append(buf, "}");
}

static void reconcile_append_diagnostic_json(ZBuf *buf, const char *code, const ZProgramGraphNode *node, size_t candidates) {
  zbuf_append(buf, "{\"code\":");
  reconcile_json_string(buf, code);
  zbuf_append(buf, ",\"message\":");
  reconcile_json_string(buf, reconcile_text_eq(code, "GRC001") ? "source edit has ambiguous graph identity" : "source edit changed a stable graph identity");
  zbuf_append(buf, ",\"node\":");
  reconcile_json_string(buf, node ? node->id : "");
  zbuf_append(buf, ",\"expected\":");
  reconcile_json_string(buf, reconcile_text_eq(code, "GRC001") ? "one edited node matching the previous graph identity" : "edited source preserves the previous graph node id");
  zbuf_append(buf, ",\"actual\":");
  zbuf_appendf(buf, "\"%zu candidate%s\"", candidates, candidates == 1 ? "" : "s");
  zbuf_append(buf, ",\"help\":");
  reconcile_json_string(buf, "make the edit through zero patch or split the change so identity is unambiguous");
  zbuf_append(buf, "}");
}

static void reconcile_append_module_diagnostic_json(ZBuf *buf, const ZProgramGraph *base, const ZProgramGraph *edited) {
  zbuf_append(buf, "{\"code\":\"GRC003\",\"message\":\"edited source has a different module identity\",\"node\":\"\",\"expected\":");
  reconcile_json_string(buf, base ? base->module_identity : "");
  zbuf_append(buf, ",\"actual\":");
  reconcile_json_string(buf, edited ? edited->module_identity : "");
  zbuf_append(buf, ",\"help\":\"reconcile the original source or package path, or capture a new base graph for the edited module\"}");
}

static void reconcile_append_diagnostics_json(ZBuf *buf, const ZProgramGraph *base, const ZProgramGraph *edited) {
  bool wrote = false;
  zbuf_append(buf, "[");
  if (reconcile_module_identity_changed(base, edited)) {
    reconcile_append_module_diagnostic_json(buf, base, edited);
    wrote = true;
  }
  for (size_t i = 0; base && i < base->node_len; i++) {
    const ZProgramGraphNode *before = &base->nodes[i];
    if (reconcile_find_node(edited, before->id)) continue;
    size_t candidates = reconcile_count_base_candidates(edited, before->id, NULL);
    if (candidates <= 0) continue;
    if (wrote) zbuf_append(buf, ", ");
    reconcile_append_diagnostic_json(buf, candidates > 1 ? "GRC001" : "GRC002", before, candidates);
    wrote = true;
  }
  zbuf_append(buf, "]");
}

static void reconcile_append_decisions_json(ZBuf *buf, const ZProgramGraph *base, const ZProgramGraph *edited, const char *edited_path) {
  bool wrote = false;
  zbuf_append(buf, "[");
  for (size_t i = 0; base && i < base->node_len; i++) {
    const ZProgramGraphNode *before = &base->nodes[i];
    const ZProgramGraphNode *after = reconcile_find_node(edited, before->id);
    const char *status = "deleted";
    const ZProgramGraphNode *candidate = NULL;
    if (after) status = reconcile_node_hash_eq(before, after) ? "unchanged" : "edited";
    else {
      size_t candidates = reconcile_count_base_candidates(edited, before->id, &candidate);
      if (candidates == 1) {
        status = "identity-changed";
        after = candidate;
      } else if (candidates > 1) {
        status = "ambiguous";
      }
    }
    if (wrote) zbuf_append(buf, ", ");
    reconcile_append_decision_json(buf, status, before, after, after ? edited : base, edited_path);
    wrote = true;
  }
  for (size_t i = 0; edited && i < edited->node_len; i++) {
    if (reconcile_find_node(base, edited->nodes[i].id) || reconcile_is_candidate_for_missing_base(base, edited, &edited->nodes[i])) continue;
    if (wrote) zbuf_append(buf, ", ");
    reconcile_append_decision_json(buf, "inserted", NULL, &edited->nodes[i], edited, edited_path);
    wrote = true;
  }
  zbuf_append(buf, "]");
}

void z_program_graph_append_reconcile_json(ZBuf *buf,
                                           const ZProgramGraph *base,
                                           const ZProgramGraph *edited,
                                           const char *base_path,
                                           const char *edited_path,
                                           const ZProgramGraphReconcileSummary *summary) {
  ZProgramGraphReconcileSummary local = {0};
  if (!summary) {
    z_program_graph_reconcile_summary(base, edited, &local);
    summary = &local;
  }
  ZBuf patch;
  zbuf_init(&patch);
  bool patch_available = summary->patch_available && reconcile_append_patch_text(&patch, base, edited);

  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": ");
  zbuf_append(buf, summary->ok ? "true" : "false");
  zbuf_append(buf, ",\n  \"base\": {\"path\": ");
  reconcile_json_string(buf, base_path ? base_path : "");
  zbuf_append(buf, ", \"graphHash\": ");
  reconcile_json_string(buf, base ? base->graph_hash : "");
  zbuf_append(buf, ", \"moduleIdentity\": ");
  reconcile_json_string(buf, base ? base->module_identity : "");
  zbuf_append(buf, "},\n  \"edited\": {\"path\": ");
  reconcile_json_string(buf, edited_path ? edited_path : "");
  zbuf_append(buf, ", \"graphHash\": ");
  reconcile_json_string(buf, edited ? edited->graph_hash : "");
  zbuf_append(buf, ", \"moduleIdentity\": ");
  reconcile_json_string(buf, edited ? edited->module_identity : "");
  zbuf_append(buf, "},\n  \"identity\": {");
  zbuf_appendf(buf,
               "\"unchanged\": %zu, \"edited\": %zu, \"inserted\": %zu, \"deleted\": %zu, \"ambiguous\": %zu, \"identityChanged\": %zu, \"moduleIdentityChanged\": %s",
               summary->unchanged,
               summary->edited,
               summary->inserted,
               summary->deleted,
               summary->ambiguous,
               summary->identity_changed,
               summary->module_identity_changed ? "true" : "false");
  zbuf_append(buf, "},\n  \"graphPatch\": {\"available\": ");
  zbuf_append(buf, patch_available ? "true" : "false");
  zbuf_append(buf, ", \"text\": ");
  if (patch_available) reconcile_json_string(buf, patch.data ? patch.data : "");
  else zbuf_append(buf, "null");
  zbuf_append(buf, "},\n  \"decisions\": ");
  reconcile_append_decisions_json(buf, base, edited, edited_path);
  zbuf_append(buf, ",\n  \"diagnostics\": ");
  reconcile_append_diagnostics_json(buf, base, edited);
  zbuf_append(buf, "\n}\n");

  zbuf_free(&patch);
}
