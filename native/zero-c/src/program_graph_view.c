#include "program_graph_view.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool view_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool view_text_starts_with(const char *text, const char *prefix) {
  if (!text || !prefix) return false;
  size_t len = strlen(prefix);
  return strncmp(text, prefix, len) == 0;
}

static const ZProgramGraphNode *view_find_node(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (view_text_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  }
  return NULL;
}

static const ZProgramGraphEdge *view_ordered_edge(const ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        edge->order == order &&
        view_text_eq(edge->from, from) &&
        view_text_eq(edge->kind, kind)) {
      return edge;
    }
  }
  return NULL;
}

static const ZProgramGraphNode *view_ordered_node(const ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  const ZProgramGraphEdge *edge = view_ordered_edge(graph, from, kind, order);
  return edge ? view_find_node(graph, edge->to) : NULL;
}

static const ZProgramGraphEdge *view_next_edge_by_order(const ZProgramGraph *graph, const char *from, const char *kind, bool have_last, size_t last_order);

static const ZProgramGraphNode *view_nth_node_by_order(const ZProgramGraph *graph, const char *from, const char *kind, size_t index) {
  bool have_last = false;
  size_t last_order = 0;
  for (size_t rank = 0;; rank++) {
    const ZProgramGraphEdge *edge = view_next_edge_by_order(graph, from, kind, have_last, last_order);
    if (!edge) return NULL;
    last_order = edge->order;
    have_last = true;
    if (rank == index) return view_find_node(graph, edge->to);
  }
}

static const ZProgramGraphEdge *view_next_edge_by_order(const ZProgramGraph *graph, const char *from, const char *kind, bool have_last, size_t last_order) {
  const ZProgramGraphEdge *best = NULL;
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE ||
        !view_text_eq(edge->from, from) ||
        !view_text_eq(edge->kind, kind) ||
        (have_last && edge->order <= last_order)) {
      continue;
    }
    if (!best || edge->order < best->order) best = edge;
  }
  return best;
}

static size_t view_edge_count(const ZProgramGraph *graph, const char *from, const char *kind) {
  size_t count = 0;
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        view_text_eq(edge->from, from) &&
        view_text_eq(edge->kind, kind)) {
      count++;
    }
  }
  return count;
}

static bool view_literal_is_raw(const char *value) {
  if (!value || !value[0]) return false;
  if (view_text_eq(value, "true") || view_text_eq(value, "false") || view_text_eq(value, "null")) return true;
  const char *p = value;
  if (*p == '-') p++;
  bool digit = false;
  while (*p) {
    if (isdigit((unsigned char)*p)) {
      digit = true;
      p++;
      continue;
    }
    if (*p == '_' || *p == '.' || isalpha((unsigned char)*p)) {
      p++;
      continue;
    }
    if ((*p == '-' || *p == '+') && p > value && (*(p - 1) == 'e' || *(p - 1) == 'E')) {
      p++;
      continue;
    }
    return false;
  }
  return digit;
}

static void view_append_indent(ZBuf *buf, unsigned indent) {
  for (unsigned i = 0; i < indent; i++) zbuf_append(buf, "  ");
}

static void view_append_string(ZBuf *buf, const char *text) {
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
        if (ch < 0x20) zbuf_appendf(buf, "\\x%02x", ch);
        else zbuf_append_char(buf, (char)ch);
        break;
    }
  }
  zbuf_append_char(buf, '"');
}

static void view_append_char_literal(ZBuf *buf, const char *text) {
  unsigned value = (unsigned)strtoul(text ? text : "0", NULL, 10);
  zbuf_append_char(buf, '\'');
  if (value == '\n') zbuf_append(buf, "\\n");
  else if (value == '\r') zbuf_append(buf, "\\r");
  else if (value == '\t') zbuf_append(buf, "\\t");
  else if (value == '\'') zbuf_append(buf, "\\'");
  else if (value == '\\') zbuf_append(buf, "\\\\");
  else if (value >= 32 && value < 127) zbuf_append_char(buf, (char)value);
  else zbuf_appendf(buf, "\\x%02x", value & 0xff);
  zbuf_append_char(buf, '\'');
}

static void view_append_name(ZBuf *buf, const char *name) {
  zbuf_append(buf, name && name[0] ? name : "__unnamed");
}

static bool view_node_is_test(const ZProgramGraphNode *node) {
  return node && node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && view_text_starts_with(node->name, "__zero_test_");
}

static bool view_module_is_stdlib(const ZProgramGraphNode *node) {
  return node && node->kind == Z_PROGRAM_GRAPH_NODE_MODULE && view_text_starts_with(node->path, "std/");
}

static bool view_module_is_rendered(const ZProgramGraph *graph, const char *name) {
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE && !view_module_is_stdlib(node) && view_text_eq(node->name, name)) return true;
  }
  return false;
}

static bool view_expr_needs_group(const ZProgramGraphNode *node) {
  if (!node) return false;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_CALL:
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL:
    case Z_PROGRAM_GRAPH_NODE_CHECK:
    case Z_PROGRAM_GRAPH_NODE_RESCUE:
    case Z_PROGRAM_GRAPH_NODE_CAST:
    case Z_PROGRAM_GRAPH_NODE_META:
    case Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL:
    case Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL:
      return true;
    default:
      return false;
  }
}

static void view_append_expr(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node);

static void view_append_expr_arg(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  bool grouped = view_expr_needs_group(node);
  if (grouped) zbuf_append_char(buf, '(');
  view_append_expr(buf, graph, node);
  if (grouped) zbuf_append_char(buf, ')');
}

static void view_append_type_args(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  if (view_edge_count(graph, node ? node->id : NULL, "typeArg") == 0) return;
  zbuf_append_char(buf, '<');
  bool have_last = false;
  size_t last_order = 0;
  bool first = true;
  for (;;) {
    const ZProgramGraphEdge *edge = view_next_edge_by_order(graph, node->id, "typeArg", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *type = view_find_node(graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!type) continue;
    if (!first) zbuf_append(buf, ", ");
    first = false;
    view_append_name(buf, type->type);
  }
  zbuf_append_char(buf, '>');
}

static void view_append_identifier(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  view_append_name(buf, node ? node->name : NULL);
  view_append_type_args(buf, graph, node);
}

static void view_append_call_like(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node, bool paren_args) {
  const ZProgramGraphNode *left = view_ordered_node(graph, node->id, "left", 0);
  const ZProgramGraphNode *right = view_ordered_node(graph, node->id, "right", 1);
  if (node->name && node->name[0] && left && right) {
    view_append_name(buf, node->name);
    zbuf_append_char(buf, ' ');
    view_append_expr_arg(buf, graph, left);
    zbuf_append_char(buf, ' ');
    view_append_expr_arg(buf, graph, right);
    return;
  }
  if (left) view_append_expr(buf, graph, left);
  else view_append_name(buf, node->name);
  view_append_type_args(buf, graph, node);
  if (paren_args) {
    zbuf_append_char(buf, '(');
    bool have_last = false;
    size_t last_order = 0;
    bool first = true;
    for (;;) {
      const ZProgramGraphEdge *edge = view_next_edge_by_order(graph, node->id, "arg", have_last, last_order);
      if (!edge) break;
      const ZProgramGraphNode *arg = view_find_node(graph, edge->to);
      last_order = edge->order;
      have_last = true;
      if (!arg) continue;
      if (!first) zbuf_append(buf, ", ");
      first = false;
      view_append_expr(buf, graph, arg);
    }
    zbuf_append_char(buf, ')');
    return;
  }
  if (view_edge_count(graph, node->id, "arg") == 0) {
    zbuf_append(buf, "()");
    return;
  }
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = view_next_edge_by_order(graph, node->id, "arg", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *arg = view_find_node(graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!arg) continue;
    zbuf_append_char(buf, ' ');
    view_append_expr_arg(buf, graph, arg);
  }
}

static void view_append_postfix_left(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  if (node && (node->kind == Z_PROGRAM_GRAPH_NODE_CALL || node->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL)) {
    view_append_call_like(buf, graph, node, true);
    return;
  }
  view_append_expr(buf, graph, node);
}

static void view_append_shape_literal(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  view_append_name(buf, node->name);
  zbuf_append(buf, " .");
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = view_next_edge_by_order(graph, node->id, "field", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *field = view_find_node(graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!field) continue;
    zbuf_append_char(buf, ' ');
    view_append_name(buf, field->name);
    zbuf_append_char(buf, ' ');
    view_append_expr_arg(buf, graph, view_ordered_node(graph, field->id, "value", 0));
  }
}

static void view_append_array_literal(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  if (view_text_eq(node->value, "repeat")) {
    zbuf_append_char(buf, '[');
    view_append_expr(buf, graph, view_nth_node_by_order(graph, node->id, "arg", 0));
    zbuf_append_char(buf, ';');
    view_append_expr(buf, graph, view_nth_node_by_order(graph, node->id, "arg", 1));
    zbuf_append_char(buf, ']');
    return;
  }
  zbuf_append(buf, "([");
  bool have_last = false;
  size_t last_order = 0;
  bool first = true;
  for (;;) {
    const ZProgramGraphEdge *edge = view_next_edge_by_order(graph, node->id, "arg", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *item = view_find_node(graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!item) continue;
    if (!first) zbuf_append(buf, ", ");
    first = false;
    view_append_expr(buf, graph, item);
  }
  zbuf_append(buf, "])");
}

static void view_append_expr(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  if (!node) {
    zbuf_append(buf, "__missing_graph_expr__");
    return;
  }
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
      view_append_identifier(buf, graph, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_LITERAL:
      if (view_text_eq(node->type, "String")) view_append_string(buf, node->value);
      else if (view_text_eq(node->type, "char")) view_append_char_literal(buf, node->value);
      else if (view_literal_is_raw(node->value)) zbuf_append(buf, node->value);
      else view_append_string(buf, node->value);
      break;
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS: {
      const ZProgramGraphNode *left = view_ordered_node(graph, node->id, "left", 0);
      if (left) {
        view_append_postfix_left(buf, graph, left);
        zbuf_append_char(buf, '.');
      }
      view_append_name(buf, node->name);
      view_append_type_args(buf, graph, node);
      break;
    }
    case Z_PROGRAM_GRAPH_NODE_CALL:
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL:
      view_append_call_like(buf, graph, node, false);
      break;
    case Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS: {
      const ZProgramGraphNode *left = view_ordered_node(graph, node->id, "left", 0);
      const ZProgramGraphNode *index = view_ordered_node(graph, node->id, "right", 1);
      view_append_postfix_left(buf, graph, left);
      zbuf_append_char(buf, '[');
      view_append_expr(buf, graph, index);
      zbuf_append_char(buf, ']');
      break;
    }
    case Z_PROGRAM_GRAPH_NODE_SLICE: {
      const ZProgramGraphNode *left = view_ordered_node(graph, node->id, "left", 0);
      const ZProgramGraphNode *start = view_ordered_node(graph, node->id, "arg", 0);
      const ZProgramGraphNode *end = view_ordered_node(graph, node->id, "arg", 1);
      view_append_postfix_left(buf, graph, left);
      zbuf_append_char(buf, '[');
      if (start) view_append_expr(buf, graph, start);
      zbuf_append(buf, "..");
      if (end) view_append_expr(buf, graph, end);
      zbuf_append_char(buf, ']');
      break;
    }
    case Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL:
      view_append_shape_literal(buf, graph, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL:
      view_append_array_literal(buf, graph, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_BORROW:
      zbuf_append(buf, node->is_mutable ? "&mut " : "&");
      view_append_expr_arg(buf, graph, view_ordered_node(graph, node->id, "left", 0));
      break;
    case Z_PROGRAM_GRAPH_NODE_CHECK:
      zbuf_append(buf, "check ");
      view_append_expr(buf, graph, view_ordered_node(graph, node->id, "left", 0));
      break;
    case Z_PROGRAM_GRAPH_NODE_CAST:
      view_append_expr_arg(buf, graph, view_ordered_node(graph, node->id, "left", 0));
      zbuf_append(buf, " as ");
      view_append_name(buf, node->name ? node->name : node->type);
      break;
    case Z_PROGRAM_GRAPH_NODE_RESCUE:
      zbuf_append(buf, "rescue ");
      view_append_expr_arg(buf, graph, view_ordered_node(graph, node->id, "left", 0));
      zbuf_append_char(buf, ' ');
      view_append_name(buf, node->name);
      zbuf_append_char(buf, ' ');
      view_append_expr_arg(buf, graph, view_ordered_node(graph, node->id, "right", 1));
      break;
    case Z_PROGRAM_GRAPH_NODE_META:
      zbuf_append(buf, "meta ");
      view_append_expr_arg(buf, graph, view_ordered_node(graph, node->id, "left", 0));
      break;
    default:
      zbuf_append(buf, "__graph_expr__");
      break;
  }
}

static void view_append_block(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *block, unsigned indent);

static void view_append_match_arm(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *arm, unsigned indent) {
  view_append_indent(buf, indent);
  if (!arm) {
    zbuf_append(buf, "# missing graph match arm\n");
    return;
  }
  view_append_name(buf, arm->name);
  const ZProgramGraphNode *range_end = view_ordered_node(graph, arm->id, "rangeEnd", 0);
  if (range_end) {
    zbuf_append(buf, "..");
    view_append_expr(buf, graph, range_end);
  }
  if (arm->value && arm->value[0]) {
    zbuf_append_char(buf, ' ');
    view_append_name(buf, arm->value);
  }
  const ZProgramGraphNode *guard = view_ordered_node(graph, arm->id, "guard", 0);
  if (guard) {
    zbuf_append(buf, " if ");
    view_append_expr(buf, graph, guard);
  }
  zbuf_append_char(buf, '\n');
  view_append_block(buf, graph, view_ordered_node(graph, arm->id, "body", 0), indent + 1);
}

static void view_append_stmt(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node, unsigned indent) {
  view_append_indent(buf, indent);
  if (!node) {
    zbuf_append(buf, "# unrendered graph statement: missing\n");
    return;
  }
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_LET:
      zbuf_append(buf, node->is_mutable ? "mut " : "let ");
      view_append_name(buf, node->name);
      if (node->type && node->type[0]) {
        zbuf_append_char(buf, ' ');
        zbuf_append(buf, node->type);
      }
      if (view_edge_count(graph, node->id, "expr") > 0) {
        zbuf_append_char(buf, ' ');
        view_append_expr(buf, graph, view_ordered_node(graph, node->id, "expr", 0));
      }
      zbuf_append_char(buf, '\n');
      break;
    case Z_PROGRAM_GRAPH_NODE_ASSIGNMENT:
      zbuf_append(buf, "set ");
      view_append_expr(buf, graph, view_ordered_node(graph, node->id, "target", 0));
      zbuf_append_char(buf, ' ');
      view_append_expr(buf, graph, view_ordered_node(graph, node->id, "expr", 0));
      zbuf_append_char(buf, '\n');
      break;
    case Z_PROGRAM_GRAPH_NODE_CHECK:
      zbuf_append(buf, "check ");
      view_append_expr(buf, graph, view_ordered_node(graph, node->id, "expr", 0));
      zbuf_append_char(buf, '\n');
      break;
    case Z_PROGRAM_GRAPH_NODE_DEFER:
      zbuf_append(buf, "defer ");
      view_append_expr(buf, graph, view_ordered_node(graph, node->id, "expr", 0));
      zbuf_append_char(buf, '\n');
      break;
    case Z_PROGRAM_GRAPH_NODE_RETURN:
      zbuf_append(buf, "ret");
      if (view_edge_count(graph, node->id, "expr") > 0) {
        zbuf_append_char(buf, ' ');
        view_append_expr(buf, graph, view_ordered_node(graph, node->id, "expr", 0));
      }
      zbuf_append_char(buf, '\n');
      break;
    case Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT:
      view_append_expr(buf, graph, view_ordered_node(graph, node->id, "expr", 0));
      zbuf_append_char(buf, '\n');
      break;
    case Z_PROGRAM_GRAPH_NODE_IF:
      zbuf_append(buf, "if ");
      view_append_expr(buf, graph, view_ordered_node(graph, node->id, "expr", 0));
      zbuf_append_char(buf, '\n');
      view_append_block(buf, graph, view_ordered_node(graph, node->id, "then", 0), indent + 1);
      if (view_edge_count(graph, node->id, "else") > 0) {
        view_append_indent(buf, indent);
        zbuf_append(buf, "else\n");
        view_append_block(buf, graph, view_ordered_node(graph, node->id, "else", 1), indent + 1);
      }
      break;
    case Z_PROGRAM_GRAPH_NODE_WHILE:
      zbuf_append(buf, "while ");
      view_append_expr(buf, graph, view_ordered_node(graph, node->id, "expr", 0));
      zbuf_append_char(buf, '\n');
      view_append_block(buf, graph, view_ordered_node(graph, node->id, "then", 0), indent + 1);
      break;
    case Z_PROGRAM_GRAPH_NODE_FOR:
      zbuf_append(buf, "for ");
      view_append_name(buf, node->name);
      zbuf_append_char(buf, ' ');
      view_append_expr(buf, graph, view_ordered_node(graph, node->id, "expr", 0));
      zbuf_append(buf, " .. ");
      view_append_expr(buf, graph, view_ordered_node(graph, node->id, "rangeEnd", 1));
      zbuf_append_char(buf, '\n');
      view_append_block(buf, graph, view_ordered_node(graph, node->id, "then", 0), indent + 1);
      break;
    case Z_PROGRAM_GRAPH_NODE_MATCH:
      zbuf_append(buf, "match ");
      view_append_expr(buf, graph, view_ordered_node(graph, node->id, "expr", 0));
      zbuf_append_char(buf, '\n');
      bool have_last = false;
      size_t last_order = 0;
      for (;;) {
        const ZProgramGraphEdge *edge = view_next_edge_by_order(graph, node->id, "arm", have_last, last_order);
        if (!edge) break;
        const ZProgramGraphNode *arm = view_find_node(graph, edge->to);
        last_order = edge->order;
        have_last = true;
        if (!arm) continue;
        view_append_match_arm(buf, graph, arm, indent + 1);
      }
      break;
    case Z_PROGRAM_GRAPH_NODE_BREAK:
      zbuf_append(buf, "break\n");
      break;
    case Z_PROGRAM_GRAPH_NODE_CONTINUE:
      zbuf_append(buf, "continue\n");
      break;
    case Z_PROGRAM_GRAPH_NODE_RAISE:
      zbuf_append(buf, "raise ");
      view_append_name(buf, node->name);
      zbuf_append_char(buf, '\n');
      break;
    default:
      zbuf_appendf(buf, "# unrendered graph statement: %s %s\n", z_program_graph_node_kind_name(node->kind), node->id ? node->id : "");
      break;
  }
}

static void view_append_block(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *block, unsigned indent) {
  if (!block) {
    view_append_indent(buf, indent);
    zbuf_append(buf, "# missing graph block\n");
    return;
  }
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = view_next_edge_by_order(graph, block->id, "statement", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *stmt = view_find_node(graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!stmt) continue;
    view_append_stmt(buf, graph, stmt, indent);
  }
}

static void view_append_type_param_list(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *owner) {
  if (view_edge_count(graph, owner ? owner->id : NULL, "typeParam") == 0) return;
  zbuf_append_char(buf, '<');
  bool have_last = false;
  size_t last_order = 0;
  bool first = true;
  for (;;) {
    const ZProgramGraphEdge *edge = view_next_edge_by_order(graph, owner->id, "typeParam", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *param = view_find_node(graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!param) continue;
    if (!first) zbuf_append(buf, ", ");
    first = false;
    if (param->is_static) zbuf_append(buf, "static ");
    view_append_name(buf, param->name);
    if (param->type && param->type[0]) {
      zbuf_append(buf, ": ");
      zbuf_append(buf, param->type);
    }
  }
  zbuf_append_char(buf, '>');
}

static void view_append_signature_params(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *fun) {
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = view_next_edge_by_order(graph, fun->id, "param", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *param = view_find_node(graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!param) continue;
    zbuf_append_char(buf, ' ');
    view_append_name(buf, param->name);
    if (param->type && param->type[0]) {
      zbuf_append_char(buf, ' ');
      zbuf_append(buf, param->type);
    }
  }
}

static void view_append_effects(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *fun) {
  if (!fun->fallible) return;
  size_t errors = view_edge_count(graph, fun->id, "error");
  if (errors == 0) {
    zbuf_append(buf, " !");
    return;
  }
  zbuf_append(buf, " ![");
  bool have_last = false;
  size_t last_order = 0;
  bool first = true;
  for (;;) {
    const ZProgramGraphEdge *edge = view_next_edge_by_order(graph, fun->id, "error", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *error = view_find_node(graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!error) continue;
    if (!first) zbuf_append_char(buf, ' ');
    first = false;
    view_append_name(buf, error->name);
  }
  zbuf_append_char(buf, ']');
}

static void view_append_function(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *fun, unsigned indent) {
  view_append_indent(buf, indent);
  if (view_node_is_test(fun)) {
    zbuf_append(buf, "test ");
    view_append_string(buf, fun->value ? fun->value : "");
    zbuf_append_char(buf, '\n');
    view_append_block(buf, graph, view_ordered_node(graph, fun->id, "body", 0), indent + 1);
    return;
  }
  if (fun->is_public) zbuf_append(buf, "pub ");
  if (fun->export_c) zbuf_append(buf, "export c ");
  zbuf_append(buf, "fn ");
  view_append_name(buf, fun->name);
  view_append_type_param_list(buf, graph, fun);
  zbuf_append_char(buf, ' ');
  zbuf_append(buf, fun->type && fun->type[0] ? fun->type : "Void");
  view_append_signature_params(buf, graph, fun);
  view_append_effects(buf, graph, fun);
  zbuf_append_char(buf, '\n');
  view_append_block(buf, graph, view_ordered_node(graph, fun->id, "body", 0), indent + 1);
}

static void view_append_const(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  if (node->is_public) zbuf_append(buf, "pub ");
  zbuf_append(buf, "const ");
  view_append_name(buf, node->name);
  if (node->type && node->type[0]) {
    zbuf_append_char(buf, ' ');
    zbuf_append(buf, node->type);
  }
  if (view_edge_count(graph, node->id, "value") > 0) {
    zbuf_append_char(buf, ' ');
    view_append_expr(buf, graph, view_ordered_node(graph, node->id, "value", 0));
  }
  zbuf_append_char(buf, '\n');
}

static void view_append_alias(ZBuf *buf, const ZProgramGraphNode *node) {
  if (node->is_public) zbuf_append(buf, "pub ");
  zbuf_append(buf, "alias ");
  view_append_name(buf, node->name);
  zbuf_append_char(buf, ' ');
  view_append_name(buf, node->type);
  zbuf_append_char(buf, '\n');
}

static void view_append_field_like(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node, unsigned indent) {
  view_append_indent(buf, indent);
  view_append_name(buf, node->name);
  if (node->type && node->type[0]) {
    zbuf_append_char(buf, ' ');
    zbuf_append(buf, node->type);
  }
  if (view_edge_count(graph, node->id, "default") > 0) {
    zbuf_append_char(buf, ' ');
    view_append_expr(buf, graph, view_ordered_node(graph, node->id, "default", 0));
  }
  zbuf_append_char(buf, '\n');
}

static void view_append_type_like(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node, const char *keyword, const char *edge_kind) {
  if (node->is_public) zbuf_append(buf, "pub ");
  if (node->kind == Z_PROGRAM_GRAPH_NODE_SHAPE && (view_text_eq(node->value, "extern") || view_text_eq(node->value, "packed"))) {
    zbuf_append(buf, node->value);
    zbuf_append(buf, " type");
  } else {
    zbuf_append(buf, keyword);
  }
  zbuf_append_char(buf, ' ');
  view_append_name(buf, node->name);
  view_append_type_param_list(buf, graph, node);
  if (node->type && node->type[0]) {
    zbuf_append_char(buf, ' ');
    zbuf_append(buf, node->type);
  }
  zbuf_append_char(buf, '\n');
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = view_next_edge_by_order(graph, node->id, edge_kind, have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *child = view_find_node(graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!child) continue;
    view_append_field_like(buf, graph, child, 1);
  }
  have_last = false;
  last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = view_next_edge_by_order(graph, node->id, "method", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *method = view_find_node(graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!method) continue;
    view_append_function(buf, graph, method, 1);
  }
}

static void view_append_import(ZBuf *buf, const ZProgramGraphNode *node) {
  zbuf_append(buf, "use ");
  view_append_name(buf, node->name);
  if (node->value && node->value[0]) {
    zbuf_append(buf, " as ");
    zbuf_append(buf, node->value);
  }
  zbuf_append_char(buf, '\n');
}

static void view_append_c_import(ZBuf *buf, const ZProgramGraphNode *node) {
  zbuf_append(buf, "extern c ");
  view_append_string(buf, node->value ? node->value : "");
  zbuf_append(buf, " as ");
  view_append_name(buf, node->name);
  zbuf_append_char(buf, '\n');
}

static void view_append_decl(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_IMPORT:
      if (view_module_is_rendered(graph, node->name)) break;
      view_append_import(buf, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_C_IMPORT:
      view_append_c_import(buf, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_CONST:
      view_append_const(buf, graph, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
      view_append_alias(buf, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_SHAPE:
      view_append_type_like(buf, graph, node, "type", "field");
      break;
    case Z_PROGRAM_GRAPH_NODE_INTERFACE:
      view_append_type_like(buf, graph, node, "interface", NULL);
      break;
    case Z_PROGRAM_GRAPH_NODE_ENUM:
      view_append_type_like(buf, graph, node, "enum", "case");
      break;
    case Z_PROGRAM_GRAPH_NODE_CHOICE:
      view_append_type_like(buf, graph, node, "choice", "case");
      break;
    case Z_PROGRAM_GRAPH_NODE_FUNCTION:
      view_append_function(buf, graph, node, 0);
      break;
    default:
      zbuf_appendf(buf, "# unrendered graph declaration: %s %s\n", z_program_graph_node_kind_name(node->kind), node->id ? node->id : "");
      break;
  }
}

static void view_append_top_level(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *module, const char *edge_kind) {
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = view_next_edge_by_order(graph, module ? module->id : NULL, edge_kind, have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *node = view_find_node(graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!node) continue;
    view_append_decl(buf, graph, node);
  }
}

static void view_append_module(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphNode *module, bool label) {
  if (label) {
    zbuf_append(buf, "# Module: ");
    view_append_name(buf, module->name);
    zbuf_append_char(buf, '\n');
  }
  view_append_top_level(buf, graph, module, "cImport");
  view_append_top_level(buf, graph, module, "import");
  view_append_top_level(buf, graph, module, "const");
  view_append_top_level(buf, graph, module, "alias");
  view_append_top_level(buf, graph, module, "shape");
  view_append_top_level(buf, graph, module, "interface");
  view_append_top_level(buf, graph, module, "enum");
  view_append_top_level(buf, graph, module, "choice");
  view_append_top_level(buf, graph, module, "function");
}

void z_program_graph_append_view(ZBuf *buf, const ZProgramGraph *graph) {
  zbuf_append(buf, "# Generated by zero graph view. Do not edit.\n");
  zbuf_append(buf, "# Source graph: ");
  zbuf_append(buf, graph && graph->graph_hash ? graph->graph_hash : "");
  zbuf_append(buf, "\n");
  zbuf_appendf(buf, "# canonicalSource %s\n\n", graph && graph->canonical_source ? "true" : "false");
  size_t modules = 0;
  size_t renderable_modules = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    modules++;
    if (!view_module_is_stdlib(&graph->nodes[i])) renderable_modules++;
  }
  bool first = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    if (renderable_modules > 0 && view_module_is_stdlib(node)) continue;
    if (!first) zbuf_append_char(buf, '\n');
    view_append_module(buf, graph, node, modules > 1);
    first = false;
  }
}
