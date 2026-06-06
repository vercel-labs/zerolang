#include "program_graph_lower.h"
#include "std_source.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const ZProgramGraph *graph;
  ZDiag *diag;
  bool allow_internal_symbols;
  SourceInput *source;
  const char *artifact_path;
  const char *current_module_path;
  size_t module_count;
  int synthetic_line;
  bool preserve_graph_types;
} GraphLower;

static void *lower_grow_items(void *items, size_t len, size_t *cap, size_t initial, size_t item_size) {
  if (len + 1 > *cap) {
    *cap = z_grow_capacity(*cap, len + 1, initial);
    return z_checked_reallocarray(items, *cap, item_size);
  }
  return items;
}

static void lower_push_function(FunctionVec *vec, Function item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 8, sizeof(Function));
  vec->items[vec->len++] = item;
}

static void lower_push_stmt(StmtVec *vec, Stmt *item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 8, sizeof(Stmt *));
  vec->items[vec->len++] = item;
}

static void lower_push_expr(ExprVec *vec, Expr *item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Expr *));
  vec->items[vec->len++] = item;
}

static void lower_push_type_arg(TypeArgVec *vec, TypeArg item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(TypeArg));
  vec->items[vec->len++] = item;
}

static void lower_push_field(FieldInitVec *vec, FieldInit item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(FieldInit));
  vec->items[vec->len++] = item;
}

static void lower_push_param(ParamVec *vec, Param item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Param));
  vec->items[vec->len++] = item;
}

static void lower_push_use(UseImportVec *vec, UseImport item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(UseImport));
  vec->items[vec->len++] = item;
}

static void lower_push_c_import(CImportVec *vec, CImport item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(CImport));
  vec->items[vec->len++] = item;
}

static void lower_push_const(ConstVec *vec, ConstDecl item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(ConstDecl));
  vec->items[vec->len++] = item;
}

static void lower_push_alias(TypeAliasVec *vec, TypeAlias item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(TypeAlias));
  vec->items[vec->len++] = item;
}

static void lower_push_shape(ShapeVec *vec, Shape item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Shape));
  vec->items[vec->len++] = item;
}

static void lower_push_interface(InterfaceVec *vec, InterfaceDecl item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(InterfaceDecl));
  vec->items[vec->len++] = item;
}

static void lower_push_enum(EnumVec *vec, EnumDecl item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(EnumDecl));
  vec->items[vec->len++] = item;
}

static void lower_push_choice(ChoiceVec *vec, Choice item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Choice));
  vec->items[vec->len++] = item;
}

static void lower_push_match_arm(MatchArmVec *vec, MatchArm item) {
  vec->items = lower_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(MatchArm));
  vec->items[vec->len++] = item;
}

static bool lower_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static void lower_source_push_string(char ***items, size_t *len, const char *value) {
  *items = z_checked_reallocarray(*items, *len + 1, sizeof(char *));
  (*items)[(*len)++] = z_strdup(value ? value : "");
}

static void lower_source_push_module(SourceInput *input, const char *name, const char *path) {
  if (!input) return;
  input->module_names = z_checked_reallocarray(input->module_names, input->module_count + 1, sizeof(char *));
  input->module_paths = z_checked_reallocarray(input->module_paths, input->module_count + 1, sizeof(char *));
  input->module_names[input->module_count] = z_strdup(name && name[0] ? name : "main");
  input->module_paths[input->module_count] = z_strdup(path && path[0] ? path : (input->source_file ? input->source_file : ""));
  input->module_count++;
}

static void lower_source_push_line(GraphLower *lower, const ZProgramGraphNode *node) {
  if (!lower || !lower->source) return;
  const char *path = node && node->path && node->path[0] ? node->path : lower->current_module_path;
  if (!path || !path[0]) path = lower->artifact_path ? lower->artifact_path : "<program-graph>";
  int original_line = node && node->line > 0 ? node->line : 1;
  lower->source->source_line_paths = z_checked_reallocarray(lower->source->source_line_paths, lower->source->source_line_count + 1, sizeof(char *));
  lower->source->source_line_numbers = z_checked_reallocarray(lower->source->source_line_numbers, lower->source->source_line_count + 1, sizeof(int));
  lower->source->source_line_paths[lower->source->source_line_count] = z_strdup(path);
  lower->source->source_line_numbers[lower->source->source_line_count] = original_line;
  lower->source->source_line_count++;
}

static int lower_line(GraphLower *lower, const ZProgramGraphNode *node) {
  if (!lower || !lower->source) return node && node->line > 0 ? node->line : 1;
  if (lower->synthetic_line < INT_MAX) lower->synthetic_line++;
  lower_source_push_line(lower, node);
  return lower->synthetic_line > 0 ? lower->synthetic_line : 1;
}

static bool lower_has_diag(const GraphLower *lower) {
  return lower && lower->diag && lower->diag->code != 0;
}

static bool lower_fail(GraphLower *lower, const ZProgramGraphNode *node, const char *message, const char *expected, const char *actual, const char *help) {
  if (!lower || !lower->diag || lower->diag->code != 0) return false;
  lower->diag->code = 2002;
  lower->diag->path = node && node->path && node->path[0] ? node->path : "<program-graph>";
  lower->diag->line = node && node->line > 0 ? node->line : 1;
  lower->diag->column = node && node->column > 0 ? node->column : 1;
  lower->diag->length = 1;
  snprintf(lower->diag->message, sizeof(lower->diag->message), "%s", message ? message : "cannot lower program graph");
  snprintf(lower->diag->expected, sizeof(lower->diag->expected), "%s", expected ? expected : "supported ProgramGraph shape");
  snprintf(lower->diag->actual, sizeof(lower->diag->actual), "%s", actual ? actual : (node ? z_program_graph_node_kind_name(node->kind) : "missing graph node"));
  if (help) snprintf(lower->diag->help, sizeof(lower->diag->help), "%s", help);
  return false;
}

static const ZProgramGraphNode *lower_find_node(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (lower_text_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  }
  return NULL;
}

static const ZProgramGraphEdge *lower_ordered_edge(const ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        edge->order == order &&
        lower_text_eq(edge->from, from) &&
        lower_text_eq(edge->kind, kind)) {
      return edge;
    }
  }
  return NULL;
}

static const ZProgramGraphNode *lower_ordered_node(const ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  const ZProgramGraphEdge *edge = lower_ordered_edge(graph, from, kind, order);
  return edge ? lower_find_node(graph, edge->to) : NULL;
}

static const ZProgramGraphEdge *lower_next_edge_by_order(const ZProgramGraph *graph, const char *from, const char *kind, bool have_last, size_t last_order) {
  const ZProgramGraphEdge *best = NULL;
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE ||
        !lower_text_eq(edge->from, from) ||
        !lower_text_eq(edge->kind, kind) ||
        (have_last && edge->order <= last_order)) {
      continue;
    }
    if (!best || edge->order < best->order) best = edge;
  }
  return best;
}

static bool lower_binary_operator(const char *text) {
  const char *ops[] = {"+", "-", "*", "/", "%", "&&", "||", "==", "!=", "<", "<=", ">", ">=", "+%", "+|", NULL};
  for (int i = 0; ops[i]; i++) {
    if (lower_text_eq(text, ops[i])) return true;
  }
  return false;
}

static bool lower_literal_is_raw(const char *value) {
  if (!value || !value[0]) return false;
  if (lower_text_eq(value, "true") || lower_text_eq(value, "false") || lower_text_eq(value, "null")) return true;
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

static bool lower_starts_with(const char *text, const char *prefix) {
  if (!text || !prefix) return false;
  size_t len = strlen(prefix);
  return strncmp(text, prefix, len) == 0;
}

static bool lower_path_is_absolute(const char *path) {
  if (!path || !path[0]) return false;
  return path[0] == '/' || (strlen(path) > 2 && path[1] == ':');
}

static bool lower_file_exists(const char *path) {
  FILE *file = path && path[0] ? fopen(path, "rb") : NULL;
  if (!file) return false;
  fclose(file);
  return true;
}

static char *lower_dirname_of(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  if (!slash) return z_strdup(".");
  if (slash == path) return z_strdup("/");
  return z_strndup(path, (size_t)(slash - path));
}

static char *lower_join_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left && left[0] ? left : ".");
  if (buf.len == 0 || buf.data[buf.len - 1] != '/') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right ? right : "");
  return buf.data;
}

static char *lower_parent_dir(const char *dir) {
  if (!dir || !dir[0] || strcmp(dir, ".") == 0 || strcmp(dir, "/") == 0) return NULL;
  char *parent = lower_dirname_of(dir);
  if (strcmp(parent, dir) == 0) {
    free(parent);
    return NULL;
  }
  return parent;
}

static char *lower_find_manifest_for_source_path(const char *path) {
  if (!path || !path[0]) return NULL;
  char *dir = lower_dirname_of(path);
  while (dir && dir[0]) {
    char *manifest = lower_join_path(dir, "zero.json");
    if (lower_file_exists(manifest)) {
      free(dir);
      return manifest;
    }
    free(manifest);
    char *parent = lower_parent_dir(dir);
    free(dir);
    dir = parent;
  }
  return NULL;
}

static void lower_source_seed_package_manifest(SourceInput *source, const ZProgramGraph *graph, const char *artifact_path) {
  if (!source || source->manifest_path || !graph || !lower_starts_with(graph->module_identity, "package:")) return;
  char *manifest = lower_find_manifest_for_source_path(artifact_path);
  if (manifest) {
    source->manifest_path = manifest;
    source->package_root = lower_dirname_of(manifest);
    return;
  }
  for (size_t i = 0; i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (!node->path || !node->path[0]) continue;
    manifest = lower_find_manifest_for_source_path(node->path);
    if (!manifest) continue;
    source->manifest_path = manifest;
    source->package_root = lower_dirname_of(manifest);
    return;
  }
}

static char *lower_resolve_c_import_header_path(const GraphLower *lower, const ZProgramGraphNode *node, const char *header) {
  if (!header || !header[0]) return z_strdup(header ? header : "");
  if (lower_path_is_absolute(header)) return z_strdup(header);
  if (lower && lower->source && lower->source->package_root && lower->source->package_root[0]) {
    char *package_path = lower_join_path(lower->source->package_root, header);
    if (lower_file_exists(package_path)) return package_path;
    free(package_path);
  }
  const char *source_path = node && node->path && node->path[0] ? node->path : (lower ? lower->current_module_path : NULL);
  if (source_path && source_path[0]) {
    char *dir = lower_dirname_of(source_path);
    char *source_relative = lower_join_path(dir, header);
    free(dir);
    if (lower_file_exists(source_relative)) return source_relative;
    free(source_relative);
  }
  if (lower_file_exists(header)) return z_strdup(header);
  return z_strdup(header);
}

static bool lower_embedded_std_module(const ZProgramGraphNode *module) {
  if (!module || module->kind != Z_PROGRAM_GRAPH_NODE_MODULE) return false;
  for (size_t i = 0; i < z_std_source_module_count(); i++) {
    const ZStdSourceModule *std_module = z_std_source_module_at(i);
    if (!std_module) continue;
    const char *short_name = strrchr(std_module->module, '.');
    const char *module_name = short_name ? short_name + 1 : std_module->module;
    bool name_matches = lower_text_eq(module->name, std_module->module) || lower_text_eq(module->name, module_name);
    bool path_matches = !module->path || !module->path[0] || lower_text_eq(module->path, std_module->path);
    if (name_matches && path_matches) return true;
  }
  return false;
}

static bool lower_identifier_start(char ch) {
  return isalpha((unsigned char)ch) || ch == '_';
}

static bool lower_identifier_part(char ch) {
  return isalnum((unsigned char)ch) || ch == '_';
}

static bool lower_text_span_eq(const char *start, const char *end, const char *text) {
  if (!start || !end || !text) return false;
  while (start < end && *text) {
    if (*start++ != *text++) return false;
  }
  return start == end && *text == '\0';
}

static bool lower_reserved_word_span(const char *start, const char *end) {
  const char *keywords[] = {
    "as", "break", "check", "choice", "const", "continue", "defer", "else", "enum", "export", "extern", "false",
    "fn", "for", "fun", "if", "import", "in", "let", "match", "meta", "mut", "null", "packed", "pub",
    "raise", "raises", "rescue", "ret", "return", "shape", "static", "test", "true", "type",
    "use", "var", "while", NULL
  };
  for (size_t i = 0; keywords[i]; i++) {
    if (lower_text_span_eq(start, end, keywords[i])) return true;
  }
  return false;
}

static bool lower_identifier_valid(const char *text) {
  if (!text || !lower_identifier_start(*text)) return false;
  const char *cursor = text + 1;
  for (; *cursor; cursor++) {
    if (!lower_identifier_part(*cursor)) return false;
  }
  return !lower_reserved_word_span(text, cursor);
}

static bool lower_require_identifier(GraphLower *lower, const ZProgramGraphNode *node, const char *text, const char *message) {
  if (lower_identifier_valid(text)) return true;
  return lower_fail(lower, node,
                    message ? message : "program graph name is not valid Zero identifier syntax",
                    "identifier",
                    text && text[0] ? text : "missing name",
                    "use a non-keyword identifier");
}

static bool lower_require_top_level_identifier(GraphLower *lower, const ZProgramGraphNode *node, const char *kind, const char *syntax_message, bool allow_test_name) {
  const char *name = node ? node->name : NULL;
  if (!lower_require_identifier(lower, node, name, syntax_message)) return false;
  if ((lower && lower->allow_internal_symbols) || !lower_starts_with(name, "__zero_")) return true;
  if (allow_test_name && lower_starts_with(name, "__zero_test_")) return true;
  char expected[128];
  snprintf(expected, sizeof(expected), "top-level %s name without the __zero_ prefix", kind ? kind : "declaration");
  return lower_fail(lower,
                    node,
                    "program graph declaration uses a reserved compiler-internal symbol name",
                    expected,
                    name,
                    "rename the declaration; __zero_ names are reserved for compiler-provided helpers");
}

static bool lower_module_name_valid(const char *text) {
  if (!text || !*text) return false;
  const char *cursor = text;
  for (;;) {
    const char *segment = cursor;
    if (!lower_identifier_start(*cursor)) return false;
    cursor++;
    while (lower_identifier_part(*cursor)) cursor++;
    if (lower_reserved_word_span(segment, cursor)) return false;
    if (*cursor == '\0') return true;
    if (*cursor != '.') return false;
    cursor++;
    if (*cursor == '\0') return false;
  }
}

static bool lower_module_is_stdlib(const char *module) { return module && strncmp(module, "std.", 4) == 0; }

static const ZProgramGraphNode *lower_find_module_named(const ZProgramGraph *graph, const char *name) {
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE && lower_text_eq(node->name, name)) return node;
  }
  return NULL;
}

static bool lower_match_case_valid(const char *text) {
  if (!text || !text[0]) return false;
  if (lower_text_eq(text, "true") || lower_text_eq(text, "false")) return true;
  if (lower_identifier_valid(text)) return true;
  const char *cursor = text;
  if (*cursor == '-') cursor++;
  if (!isdigit((unsigned char)*cursor)) return false;
  return lower_literal_is_raw(text);
}

static Expr *lower_new_expr(GraphLower *lower, ExprKind kind, const ZProgramGraphNode *node) {
  Expr *expr = z_checked_malloc(sizeof(Expr));
  memset(expr, 0, sizeof(*expr));
  expr->kind = kind;
  expr->line = lower_line(lower, node);
  expr->column = node && node->column > 0 ? node->column : 1;
  if (lower && lower->preserve_graph_types && node && node->type && node->type[0]) expr->resolved_type = z_strdup(node->type);
  return expr;
}

static Stmt *lower_new_stmt(GraphLower *lower, StmtKind kind, const ZProgramGraphNode *node) {
  Stmt *stmt = z_checked_malloc(sizeof(Stmt));
  memset(stmt, 0, sizeof(*stmt));
  stmt->kind = kind;
  stmt->line = lower_line(lower, node);
  stmt->column = node && node->column > 0 ? node->column : 1;
  if (lower && lower->preserve_graph_types && node && node->type && node->type[0]) stmt->resolved_type = z_strdup(node->type);
  return stmt;
}

static Expr *lower_expr(GraphLower *lower, const ZProgramGraphNode *node);
static StmtVec lower_block(GraphLower *lower, const ZProgramGraphNode *block);

static Expr *lower_required_expr(GraphLower *lower, const ZProgramGraphNode *owner, const char *edge_kind, size_t order, const char *context) {
  const ZProgramGraphNode *node = lower_ordered_node(lower->graph, owner ? owner->id : NULL, edge_kind, order);
  if (!node) {
    lower_fail(lower, owner, "program graph is missing required expression edge", context ? context : "expression edge", edge_kind, NULL);
    return NULL;
  }
  return lower_expr(lower, node);
}

static Expr *lower_optional_expr(GraphLower *lower, const ZProgramGraphNode *owner, const char *edge_kind, size_t order) {
  const ZProgramGraphNode *node = lower_ordered_node(lower->graph, owner ? owner->id : NULL, edge_kind, order);
  return node ? lower_expr(lower, node) : NULL;
}

static void lower_type_args(GraphLower *lower, const ZProgramGraphNode *node, TypeArgVec *out) {
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = lower_next_edge_by_order(lower->graph, node ? node->id : NULL, "typeArg", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *type = lower_find_node(lower->graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!type) continue;
    lower_push_type_arg(out, (TypeArg){
      .type = z_strdup(type->type && type->type[0] ? type->type : ""),
      .line = lower_line(lower, type),
      .column = type->column > 0 ? type->column : 1,
    });
  }
}

static void lower_args(GraphLower *lower, const ZProgramGraphNode *node, ExprVec *out) {
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = lower_next_edge_by_order(lower->graph, node ? node->id : NULL, "arg", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *arg = lower_find_node(lower->graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!arg) continue;
    lower_push_expr(out, lower_expr(lower, arg));
    if (lower_has_diag(lower)) return;
  }
}

static void lower_shape_fields(GraphLower *lower, const ZProgramGraphNode *node, FieldInitVec *out) {
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = lower_next_edge_by_order(lower->graph, node ? node->id : NULL, "field", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *field = lower_find_node(lower->graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!field) continue;
    if (!lower_require_identifier(lower, field, field->name, "program graph field initializer name is not valid Zero identifier syntax")) return;
    Expr *value = lower_required_expr(lower, field, "value", 0, "field initializer value");
    if (lower_has_diag(lower)) return;
    lower_push_field(out, (FieldInit){
      .name = z_strdup(field->name && field->name[0] ? field->name : ""),
      .value = value,
      .line = lower_line(lower, field),
      .column = field->column > 0 ? field->column : 1,
    });
  }
}

static Expr *lower_literal_expr(GraphLower *lower, const ZProgramGraphNode *node) {
  if (lower_text_eq(node->type, "String")) {
    Expr *expr = lower_new_expr(lower, EXPR_STRING, node);
    expr->text = z_strdup(node->value ? node->value : "");
    return expr;
  }
  if (lower_text_eq(node->type, "char")) {
    Expr *expr = lower_new_expr(lower, EXPR_CHAR, node);
    expr->text = z_strdup(node->value ? node->value : "0");
    return expr;
  }
  if (lower_text_eq(node->value, "true") || lower_text_eq(node->value, "false")) {
    Expr *expr = lower_new_expr(lower, EXPR_BOOL, node);
    expr->bool_value = lower_text_eq(node->value, "true");
    return expr;
  }
  if (lower_text_eq(node->value, "null")) return lower_new_expr(lower, EXPR_NULL, node);
  if (!lower_literal_is_raw(node->value)) {
    Expr *expr = lower_new_expr(lower, EXPR_STRING, node);
    expr->text = z_strdup(node->value ? node->value : "");
    return expr;
  }
  Expr *expr = lower_new_expr(lower, EXPR_NUMBER, node);
  expr->text = z_strdup(node->value ? node->value : "0");
  return expr;
}

static Expr *lower_expr(GraphLower *lower, const ZProgramGraphNode *node) {
  if (!node) {
    lower_fail(lower, NULL, "program graph is missing expression node", "expression node", "missing node", NULL);
    return NULL;
  }
  Expr *expr = NULL;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
      if (!lower_require_identifier(lower, node, node->name, "program graph identifier name is not valid Zero identifier syntax")) return NULL;
      expr = lower_new_expr(lower, EXPR_IDENT, node);
      expr->text = z_strdup(node->name && node->name[0] ? node->name : "");
      lower_type_args(lower, node, &expr->type_args);
      return expr;
    case Z_PROGRAM_GRAPH_NODE_LITERAL:
      return lower_literal_expr(lower, node);
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
      if (!lower_require_identifier(lower, node, node->name, "program graph field access name is not valid Zero identifier syntax")) return NULL;
      expr = lower_new_expr(lower, EXPR_MEMBER, node);
      expr->text = z_strdup(node->name && node->name[0] ? node->name : "");
      expr->left = lower_required_expr(lower, node, "left", 0, "field access receiver");
      lower_type_args(lower, node, &expr->type_args);
      return expr;
    case Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS:
      expr = lower_new_expr(lower, EXPR_INDEX, node);
      expr->left = lower_required_expr(lower, node, "left", 0, "index receiver");
      expr->right = lower_required_expr(lower, node, "right", 1, "index expression");
      return expr;
    case Z_PROGRAM_GRAPH_NODE_SLICE:
      expr = lower_new_expr(lower, EXPR_SLICE, node);
      expr->left = lower_required_expr(lower, node, "left", 0, "slice receiver");
      lower_push_expr(&expr->args, lower_optional_expr(lower, node, "arg", 0));
      lower_push_expr(&expr->args, lower_optional_expr(lower, node, "arg", 1));
      return expr;
    case Z_PROGRAM_GRAPH_NODE_CALL:
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL:
      if (lower_binary_operator(node->name) &&
          lower_ordered_node(lower->graph, node->id, "left", 0) &&
          lower_ordered_node(lower->graph, node->id, "right", 1)) {
        expr = lower_new_expr(lower, EXPR_BINARY, node);
        expr->text = z_strdup(node->name);
        expr->left = lower_required_expr(lower, node, "left", 0, "binary left operand");
        expr->right = lower_required_expr(lower, node, "right", 1, "binary right operand");
        return expr;
      }
      if (node->name && node->name[0] &&
          lower_ordered_node(lower->graph, node->id, "left", 0) &&
          lower_ordered_node(lower->graph, node->id, "right", 1)) {
        lower_fail(lower, node,
                   "program graph call operator is not valid Zero operator syntax",
                   "supported binary operator",
                   node->name,
                   "use a supported operator or remove the right operand edge");
        return NULL;
      }
      expr = lower_new_expr(lower, EXPR_CALL, node);
      expr->left = lower_optional_expr(lower, node, "left", 0);
      if (!expr->left && node->name && node->name[0]) {
        if (!lower_require_identifier(lower, node, node->name, "program graph call callee name is not valid Zero identifier syntax")) return NULL;
        expr->left = lower_new_expr(lower, EXPR_IDENT, node);
        expr->left->text = z_strdup(node->name);
      }
      if (!expr->left) lower_fail(lower, node, "program graph call is missing callee", "left edge or callee name", "missing callee", NULL);
      expr->prefix_deref = lower_text_eq(node->value, "prefix-deref");
      lower_type_args(lower, node, &expr->type_args);
      lower_args(lower, node, &expr->args);
      return expr;
    case Z_PROGRAM_GRAPH_NODE_CAST:
      expr = lower_new_expr(lower, EXPR_CAST, node);
      expr->text = z_strdup(node->name && node->name[0] ? node->name : (node->type ? node->type : ""));
      expr->left = lower_required_expr(lower, node, "left", 0, "cast operand");
      return expr;
    case Z_PROGRAM_GRAPH_NODE_BORROW:
      expr = lower_new_expr(lower, EXPR_BORROW, node);
      expr->mutable_borrow = node->is_mutable;
      expr->left = lower_required_expr(lower, node, "left", 0, "borrow operand");
      return expr;
    case Z_PROGRAM_GRAPH_NODE_CHECK:
      expr = lower_new_expr(lower, EXPR_CHECK, node);
      expr->left = lower_required_expr(lower, node, "left", 0, "checked expression");
      return expr;
    case Z_PROGRAM_GRAPH_NODE_RESCUE:
      if (!lower_require_identifier(lower, node, node->name, "program graph rescue binding name is not valid Zero identifier syntax")) return NULL;
      expr = lower_new_expr(lower, EXPR_RESCUE, node);
      expr->text = z_strdup(node->name && node->name[0] ? node->name : "");
      expr->left = lower_required_expr(lower, node, "left", 0, "rescue expression");
      expr->right = lower_required_expr(lower, node, "right", 1, "rescue fallback expression");
      return expr;
    case Z_PROGRAM_GRAPH_NODE_META:
      expr = lower_new_expr(lower, EXPR_META, node);
      expr->text = z_strdup(node->name && node->name[0] ? node->name : "");
      expr->left = lower_required_expr(lower, node, "left", 0, "meta expression");
      return expr;
    case Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL:
      expr = lower_new_expr(lower, EXPR_SHAPE_LITERAL, node);
      expr->text = z_strdup(node->name && node->name[0] ? node->name : "");
      lower_shape_fields(lower, node, &expr->fields);
      return expr;
    case Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL:
      expr = lower_new_expr(lower, EXPR_ARRAY_LITERAL, node);
      expr->array_repeat = lower_text_eq(node->value, "repeat");
      lower_args(lower, node, &expr->args);
      return expr;
    default:
      lower_fail(lower, node, "program graph expression kind is not supported by direct lowering", "expression node", z_program_graph_node_kind_name(node->kind), NULL);
      return NULL;
  }
}

static MatchArm lower_match_arm(GraphLower *lower, const ZProgramGraphNode *node) {
  if (!lower_match_case_valid(node ? node->name : NULL)) {
    lower_fail(lower,
               node,
               "program graph match case name is not valid Zero match syntax",
               "identifier, true, false, or number",
               node && node->name ? node->name : "missing name",
               "use a non-keyword case name");
  }
  if (!lower_has_diag(lower) && node && node->value && node->value[0]) {
    lower_require_identifier(lower, node, node->value, "program graph match payload name is not valid Zero identifier syntax");
  }
  MatchArm arm = {
    .case_name = z_strdup(node && node->name ? node->name : ""),
    .payload_name = node && node->value && node->value[0] ? z_strdup(node->value) : NULL,
    .line = lower_line(lower, node),
    .column = node && node->column > 0 ? node->column : 1,
  };
  const ZProgramGraphNode *range_end = lower_ordered_node(lower->graph, node ? node->id : NULL, "rangeEnd", 0);
  if (range_end) arm.range_end = z_strdup(range_end->value && range_end->value[0] ? range_end->value : (range_end->name ? range_end->name : ""));
  arm.guard = lower_optional_expr(lower, node, "guard", 0);
  const ZProgramGraphNode *body = lower_ordered_node(lower->graph, node ? node->id : NULL, "body", 0);
  if (!body) lower_fail(lower, node, "program graph match arm is missing body block", "body block edge", "missing body", NULL);
  else arm.body = lower_block(lower, body);
  return arm;
}

static void lower_match_arms(GraphLower *lower, const ZProgramGraphNode *node, MatchArmVec *out) {
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = lower_next_edge_by_order(lower->graph, node ? node->id : NULL, "arm", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *arm = lower_find_node(lower->graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!arm) continue;
    lower_push_match_arm(out, lower_match_arm(lower, arm));
    if (lower_has_diag(lower)) return;
  }
}

static Stmt *lower_stmt(GraphLower *lower, const ZProgramGraphNode *node) {
  if (!node) {
    lower_fail(lower, NULL, "program graph is missing statement node", "statement node", "missing node", NULL);
    return NULL;
  }
  Stmt *stmt = NULL;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_LET:
      if (!lower_require_identifier(lower, node, node->name, "program graph binding name is not valid Zero identifier syntax")) return NULL;
      stmt = lower_new_stmt(lower, STMT_LET, node);
      stmt->name = z_strdup(node->name && node->name[0] ? node->name : "");
      stmt->type = node->type && node->type[0] ? z_strdup(node->type) : NULL;
      stmt->mutable_binding = node->is_mutable;
      stmt->expr = lower_required_expr(lower, node, "expr", 0, "let initializer");
      return stmt;
    case Z_PROGRAM_GRAPH_NODE_ASSIGNMENT:
      stmt = lower_new_stmt(lower, STMT_ASSIGN, node);
      stmt->target = lower_required_expr(lower, node, "target", 0, "assignment target");
      stmt->expr = lower_required_expr(lower, node, "expr", 0, "assignment value");
      return stmt;
    case Z_PROGRAM_GRAPH_NODE_DEFER:
      stmt = lower_new_stmt(lower, STMT_DEFER, node);
      stmt->expr = lower_required_expr(lower, node, "expr", 0, "defer expression");
      return stmt;
    case Z_PROGRAM_GRAPH_NODE_CHECK:
      stmt = lower_new_stmt(lower, STMT_CHECK, node);
      stmt->expr = lower_required_expr(lower, node, "expr", 0, "check expression");
      return stmt;
    case Z_PROGRAM_GRAPH_NODE_RETURN:
      stmt = lower_new_stmt(lower, STMT_RETURN, node);
      stmt->expr = lower_optional_expr(lower, node, "expr", 0);
      return stmt;
    case Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT:
      stmt = lower_new_stmt(lower, STMT_EXPR, node);
      stmt->expr = lower_required_expr(lower, node, "expr", 0, "statement expression");
      return stmt;
    case Z_PROGRAM_GRAPH_NODE_IF:
      stmt = lower_new_stmt(lower, STMT_IF, node);
      stmt->expr = lower_required_expr(lower, node, "expr", 0, "if condition");
      stmt->then_body = lower_block(lower, lower_ordered_node(lower->graph, node->id, "then", 0));
      stmt->else_body = lower_block(lower, lower_ordered_node(lower->graph, node->id, "else", 1));
      return stmt;
    case Z_PROGRAM_GRAPH_NODE_WHILE:
      stmt = lower_new_stmt(lower, STMT_WHILE, node);
      stmt->expr = lower_required_expr(lower, node, "expr", 0, "while condition");
      stmt->then_body = lower_block(lower, lower_ordered_node(lower->graph, node->id, "then", 0));
      return stmt;
    case Z_PROGRAM_GRAPH_NODE_FOR:
      if (!lower_require_identifier(lower, node, node->name, "program graph loop binding name is not valid Zero identifier syntax")) return NULL;
      stmt = lower_new_stmt(lower, STMT_FOR, node);
      stmt->name = z_strdup(node->name && node->name[0] ? node->name : "");
      stmt->expr = lower_required_expr(lower, node, "expr", 0, "for range start");
      stmt->range_end = lower_required_expr(lower, node, "rangeEnd", 1, "for range end");
      stmt->then_body = lower_block(lower, lower_ordered_node(lower->graph, node->id, "then", 0));
      return stmt;
    case Z_PROGRAM_GRAPH_NODE_BREAK:
      return lower_new_stmt(lower, STMT_BREAK, node);
    case Z_PROGRAM_GRAPH_NODE_CONTINUE:
      return lower_new_stmt(lower, STMT_CONTINUE, node);
    case Z_PROGRAM_GRAPH_NODE_MATCH:
      stmt = lower_new_stmt(lower, STMT_MATCH, node);
      stmt->expr = lower_required_expr(lower, node, "expr", 0, "match expression");
      lower_match_arms(lower, node, &stmt->match_arms);
      return stmt;
    case Z_PROGRAM_GRAPH_NODE_RAISE:
      if (!lower_require_identifier(lower, node, node->name, "program graph raise error name is not valid Zero identifier syntax")) return NULL;
      stmt = lower_new_stmt(lower, STMT_RAISE, node);
      stmt->name = z_strdup(node->name && node->name[0] ? node->name : "");
      return stmt;
    default:
      lower_fail(lower, node, "program graph statement kind is not supported by direct lowering", "statement node", z_program_graph_node_kind_name(node->kind), NULL);
      return NULL;
  }
}

static StmtVec lower_block(GraphLower *lower, const ZProgramGraphNode *block) {
  StmtVec body = {0};
  if (!block) return body;
  if (block->kind != Z_PROGRAM_GRAPH_NODE_BLOCK) {
    lower_fail(lower, block, "program graph body edge does not point to a block", "Block node", z_program_graph_node_kind_name(block->kind), NULL);
    return body;
  }
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = lower_next_edge_by_order(lower->graph, block->id, "statement", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *stmt_node = lower_find_node(lower->graph, edge->to);
    last_order = edge->order;
    have_last = true;
    Stmt *stmt = lower_stmt(lower, stmt_node);
    if (lower_has_diag(lower)) return body;
    lower_push_stmt(&body, stmt);
  }
  return body;
}

static void lower_params(GraphLower *lower, const ZProgramGraphNode *owner, const char *edge_kind, ParamVec *out) {
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = lower_next_edge_by_order(lower->graph, owner ? owner->id : NULL, edge_kind, have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *node = lower_find_node(lower->graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!node) continue;
    if (!lower_require_identifier(lower, node, node->name, "program graph parameter name is not valid Zero identifier syntax")) return;
    Param param = {
      .name = z_strdup(node->name && node->name[0] ? node->name : ""),
      .type = node->type && node->type[0] ? z_strdup(node->type) : NULL,
      .default_value = lower_optional_expr(lower, node, "default", 0),
      .is_static = node->is_static,
      .line = lower_line(lower, node),
      .column = node->column > 0 ? node->column : 1,
    };
    lower_push_param(out, param);
    if (lower_has_diag(lower)) return;
  }
}

static Function lower_function(GraphLower *lower, const ZProgramGraphNode *node, bool top_level) {
  if (top_level) lower_require_top_level_identifier(lower, node, "function", "program graph function name is not valid Zero identifier syntax", true);
  else lower_require_identifier(lower, node, node ? node->name : NULL, "program graph function name is not valid Zero identifier syntax");
  Function fun = {
    .name = z_strdup(node && node->name ? node->name : ""),
    .test_name = node && lower_starts_with(node->name, "__zero_test_") ? z_strdup(node->value ? node->value : "") : NULL,
    .return_type = z_strdup(node && node->type && node->type[0] ? node->type : "Void"),
    .is_public = node && node->is_public,
    .raises = node && node->fallible,
    .is_test = node && lower_starts_with(node->name, "__zero_test_"),
    .export_c = node && node->export_c,
    .line = lower_line(lower, node),
    .column = node && node->column > 0 ? node->column : 1,
  };
  lower_params(lower, node, "typeParam", &fun.type_params);
  lower_params(lower, node, "param", &fun.params);
  lower_params(lower, node, "error", &fun.errors);
  fun.has_error_set = fun.errors.len > 0;
  const ZProgramGraphNode *body = lower_ordered_node(lower->graph, node ? node->id : NULL, "body", 0);
  if (!body) lower_fail(lower, node, "program graph function is missing body block", "body block edge", "missing body", NULL);
  else fun.body = lower_block(lower, body);
  return fun;
}

static void lower_methods(GraphLower *lower, const ZProgramGraphNode *owner, FunctionVec *out) {
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = lower_next_edge_by_order(lower->graph, owner ? owner->id : NULL, "method", have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *method = lower_find_node(lower->graph, edge->to);
    last_order = edge->order;
    have_last = true;
    if (!method) continue;
    if (method->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION) {
      lower_fail(lower, method, "program graph method edge does not point to a function", "Function node", z_program_graph_node_kind_name(method->kind), NULL);
      return;
    }
    lower_push_function(out, lower_function(lower, method, false));
    if (lower_has_diag(lower)) return;
  }
}

static void lower_import(GraphLower *lower, Program *program, const ZProgramGraphNode *node) {
  const char *module = node && node->name ? node->name : "";
  const char *alias = node && node->value && node->value[0] ? node->value : NULL;
  if (!lower_module_name_valid(module)) {
    lower_fail(lower, node, "program graph import module is not valid Zero import syntax", "dot-separated module identifiers", module, "use module names like std.mem or package.local");
    return;
  }
  if (alias && !lower_identifier_valid(alias)) {
    lower_fail(lower, node, "program graph import alias is not valid Zero identifier syntax", "identifier", alias, "use an identifier alias or clear the alias value");
    return;
  }
  if (!lower_module_is_stdlib(module) && !lower_find_module_named(lower->graph, module)) {
    lower_fail(lower, node, "program graph import target module is missing", "Module node for package-local import or std.* import", module, "add the imported module to the artifact or remove the import");
    return;
  }
  lower_push_use(&program->use_imports, (UseImport){
    .module = z_strdup(module),
    .alias = alias ? z_strdup(alias) : NULL,
    .line = lower_line(lower, node),
    .column = node->column > 0 ? node->column : 1,
    .end_column = node->column > 0 ? node->column : 1,
  });
}

static void lower_c_import(GraphLower *lower, Program *program, const ZProgramGraphNode *node) {
  if (!lower_require_identifier(lower, node, node ? node->name : NULL, "program graph C import alias is not valid Zero identifier syntax")) return;
  const char *header = node->value && node->value[0] ? node->value : "";
  lower_push_c_import(&program->c_imports, (CImport){
    .header = z_strdup(header),
    .resolved_header = lower_resolve_c_import_header_path(lower, node, header),
    .alias = z_strdup(node->name && node->name[0] ? node->name : ""),
    .line = lower_line(lower, node),
    .column = node->column > 0 ? node->column : 1,
  });
}

static void lower_const(GraphLower *lower, Program *program, const ZProgramGraphNode *node) {
  if (!lower_require_top_level_identifier(lower, node, "const", "program graph const name is not valid Zero identifier syntax", false)) return;
  ConstDecl item = {
    .name = z_strdup(node->name && node->name[0] ? node->name : ""),
    .type = node->type && node->type[0] ? z_strdup(node->type) : NULL,
    .expr = lower_required_expr(lower, node, "value", 0, "const value"),
    .is_public = node->is_public,
    .line = lower_line(lower, node),
    .column = node->column > 0 ? node->column : 1,
  };
  lower_push_const(&program->consts, item);
}

static void lower_alias(GraphLower *lower, Program *program, const ZProgramGraphNode *node) {
  if (!lower_require_top_level_identifier(lower, node, "type-alias", "program graph alias name is not valid Zero identifier syntax", false)) return;
  lower_push_alias(&program->aliases, (TypeAlias){
    .name = z_strdup(node->name && node->name[0] ? node->name : ""),
    .target = z_strdup(node->type && node->type[0] ? node->type : ""),
    .is_public = node->is_public,
    .line = lower_line(lower, node),
    .column = node->column > 0 ? node->column : 1,
  });
}

static void lower_shape(GraphLower *lower, Program *program, const ZProgramGraphNode *node) {
  if (!lower_require_top_level_identifier(lower, node, "shape", "program graph type name is not valid Zero identifier syntax", false)) return;
  Shape shape = {
    .name = z_strdup(node->name && node->name[0] ? node->name : ""),
    .layout = z_strdup(node->value && node->value[0] ? node->value : "auto"),
    .is_public = node->is_public,
    .line = lower_line(lower, node),
    .column = node->column > 0 ? node->column : 1,
  };
  lower_params(lower, node, "typeParam", &shape.type_params);
  lower_params(lower, node, "field", &shape.fields);
  lower_methods(lower, node, &shape.methods);
  lower_push_shape(&program->shapes, shape);
}

static void lower_interface(GraphLower *lower, Program *program, const ZProgramGraphNode *node) {
  if (!lower_require_top_level_identifier(lower, node, "interface", "program graph interface name is not valid Zero identifier syntax", false)) return;
  InterfaceDecl item = {
    .name = z_strdup(node->name && node->name[0] ? node->name : ""),
    .is_public = node->is_public,
    .line = lower_line(lower, node),
    .column = node->column > 0 ? node->column : 1,
  };
  lower_params(lower, node, "typeParam", &item.type_params);
  lower_methods(lower, node, &item.methods);
  lower_push_interface(&program->interfaces, item);
}

static void lower_enum(GraphLower *lower, Program *program, const ZProgramGraphNode *node) {
  if (!lower_require_top_level_identifier(lower, node, "enum", "program graph enum name is not valid Zero identifier syntax", false)) return;
  EnumDecl item = {
    .name = z_strdup(node->name && node->name[0] ? node->name : ""),
    .type = node->type && node->type[0] ? z_strdup(node->type) : NULL,
    .is_public = node->is_public,
    .line = lower_line(lower, node),
    .column = node->column > 0 ? node->column : 1,
  };
  lower_params(lower, node, "case", &item.cases);
  lower_push_enum(&program->enums, item);
}

static void lower_choice(GraphLower *lower, Program *program, const ZProgramGraphNode *node) {
  if (!lower_require_top_level_identifier(lower, node, "choice", "program graph choice name is not valid Zero identifier syntax", false)) return;
  Choice item = {
    .name = z_strdup(node->name && node->name[0] ? node->name : ""),
    .is_public = node->is_public,
    .line = lower_line(lower, node),
    .column = node->column > 0 ? node->column : 1,
  };
  lower_params(lower, node, "case", &item.cases);
  lower_push_choice(&program->choices, item);
}

static void lower_top_level(GraphLower *lower, Program *program, const ZProgramGraphNode *node) {
  if (!node || lower_has_diag(lower)) return;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_IMPORT:
      lower_import(lower, program, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_C_IMPORT:
      lower_c_import(lower, program, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_CONST:
      lower_const(lower, program, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
      lower_alias(lower, program, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_SHAPE:
      lower_shape(lower, program, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_INTERFACE:
      lower_interface(lower, program, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_ENUM:
      lower_enum(lower, program, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_CHOICE:
      lower_choice(lower, program, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_FUNCTION:
      lower_push_function(&program->functions, lower_function(lower, node, true));
      break;
    default:
      lower_fail(lower, node, "program graph declaration kind is not supported by direct lowering", "declaration node", z_program_graph_node_kind_name(node->kind), NULL);
      break;
  }
}

static void lower_top_level_edges(GraphLower *lower, Program *program, const ZProgramGraphNode *module, const char *edge_kind) {
  bool have_last = false;
  size_t last_order = 0;
  for (;;) {
    const ZProgramGraphEdge *edge = lower_next_edge_by_order(lower->graph, module ? module->id : NULL, edge_kind, have_last, last_order);
    if (!edge) break;
    const ZProgramGraphNode *node = lower_find_node(lower->graph, edge->to);
    last_order = edge->order;
    have_last = true;
    lower_top_level(lower, program, node);
    if (lower_has_diag(lower)) return;
  }
}

static void lower_module(GraphLower *lower, Program *program, const ZProgramGraphNode *module) {
  bool previous_allow_internal_symbols = lower->allow_internal_symbols;
  const char *previous_module_path = lower->current_module_path;
  ZBuf module_path;
  zbuf_init(&module_path);
  if (module && module->path && module->path[0]) {
    zbuf_append(&module_path, module->path);
  } else {
    zbuf_append(&module_path, lower->artifact_path ? lower->artifact_path : "<program-graph>");
    if (lower->module_count > 1) {
      zbuf_append_char(&module_path, '#');
      zbuf_append(&module_path, module && module->name && module->name[0] ? module->name : "main");
    }
  }
  lower->allow_internal_symbols = previous_allow_internal_symbols || lower_embedded_std_module(module);
  lower->current_module_path = module_path.data;
  lower_top_level_edges(lower, program, module, "cImport");
  lower_top_level_edges(lower, program, module, "import");
  lower_top_level_edges(lower, program, module, "const");
  lower_top_level_edges(lower, program, module, "alias");
  lower_top_level_edges(lower, program, module, "shape");
  lower_top_level_edges(lower, program, module, "interface");
  lower_top_level_edges(lower, program, module, "enum");
  lower_top_level_edges(lower, program, module, "choice");
  lower_top_level_edges(lower, program, module, "function");
  lower->allow_internal_symbols = previous_allow_internal_symbols;
  lower->current_module_path = previous_module_path;
  zbuf_free(&module_path);
}

static bool lower_source_has_file(const SourceInput *source, const char *path) {
  for (size_t i = 0; source && path && i < source->source_file_count; i++) {
    if (lower_text_eq(source->source_files[i], path)) return true;
  }
  return false;
}

static void lower_source_add_file(SourceInput *source, const char *path) {
  if (!source) return;
  const char *stable_path = path && path[0] ? path : (source->source_file ? source->source_file : "");
  if (!lower_source_has_file(source, stable_path)) lower_source_push_string(&source->source_files, &source->source_file_count, stable_path);
}

static void lower_source_set_package_identity(SourceInput *source, const char *identity) {
  const char *prefix = "package:";
  size_t prefix_len = strlen(prefix);
  if (!source || !identity || strncmp(identity, prefix, prefix_len) != 0) return;
  const char *name = identity + prefix_len;
  const char *version = strrchr(name, '@');
  if (version && version > name) {
    source->package_name = z_strndup(name, (size_t)(version - name));
    source->package_version = z_strdup(version + 1);
  } else {
    source->package_name = z_strdup(name);
  }
}

static void lower_init_source_from_graph(SourceInput *source, const ZProgramGraph *graph, const char *artifact_path) {
  if (!source) return;
  *source = (SourceInput){0};
  source->source_file = z_strdup(artifact_path && artifact_path[0] ? artifact_path : "<program-graph>");
  source->source = z_strdup("");
  lower_source_set_package_identity(source, graph ? graph->module_identity : NULL);
  lower_source_seed_package_manifest(source, graph, artifact_path);
  size_t module_count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_MODULE) module_count++;
  }
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    ZBuf synthetic_path;
    zbuf_init(&synthetic_path);
    if (node->path && node->path[0]) {
      zbuf_append(&synthetic_path, node->path);
    } else {
      zbuf_append(&synthetic_path, source->source_file);
      if (module_count > 1) {
        zbuf_append_char(&synthetic_path, '#');
        zbuf_append(&synthetic_path, node->name && node->name[0] ? node->name : "main");
      }
    }
    const char *path = synthetic_path.data ? synthetic_path.data : source->source_file;
    lower_source_add_file(source, path);
    lower_source_push_module(source, node->name && node->name[0] ? node->name : "main", path);
    zbuf_free(&synthetic_path);
  }
  if (source->module_count == 0) {
    lower_source_add_file(source, source->source_file);
    lower_source_push_module(source, "main", source->source_file);
  }
}

static bool lower_to_program(GraphLower *lower, Program *out) {
  if (!out) return false;
  *out = (Program){0};
  if (!lower || !lower->graph) return lower_fail(lower, NULL, "program graph is missing", "ProgramGraph input", "missing graph", NULL);

  size_t module_count = 0;
  for (size_t i = 0; i < lower->graph->node_len; i++) {
    if (lower->graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_MODULE) module_count++;
  }
  lower->module_count = module_count;
  for (size_t i = 0; i < lower->graph->node_len; i++) {
    const ZProgramGraphNode *node = &lower->graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    lower_module(lower, out, node);
    if (lower_has_diag(lower)) {
      z_free_program(out);
      return false;
    }
  }
  if (module_count == 0) {
    lower_fail(lower, NULL, "program graph has no module node", "at least one Module node", "missing module", NULL);
    z_free_program(out);
    return false;
  }
  return true;
}

bool z_program_graph_lower_to_program(const ZProgramGraph *graph, Program *out, ZDiag *diag) {
  GraphLower lower = {.graph = graph, .diag = diag};
  return lower_to_program(&lower, out);
}

bool z_program_graph_lower_to_program_with_source(const ZProgramGraph *graph, const char *artifact_path, Program *out, SourceInput *source, ZDiag *diag) {
  lower_init_source_from_graph(source, graph, artifact_path);
  GraphLower lower = {.graph = graph, .diag = diag, .source = source, .artifact_path = artifact_path};
  return lower_to_program(&lower, out);
}

bool z_program_graph_lower_to_program_for_roundtrip(const ZProgramGraph *graph, const char *artifact_path, Program *out, SourceInput *source, ZDiag *diag) {
  lower_init_source_from_graph(source, graph, artifact_path);
  GraphLower lower = {.graph = graph, .diag = diag, .source = source, .artifact_path = artifact_path, .preserve_graph_types = true, .allow_internal_symbols = true};
  return lower_to_program(&lower, out);
}
