#include "zero.h"
#include "c_import.h"
#include "mir_verify.h"
#include "specialize.h"
#include "std_source.h"
#include "type_core.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/mman.h>
#include <unistd.h>
#endif

#define IR_READONLY_DATA_BASE 1024u
#define IR_READONLY_DATA_LIMIT 65536u
#define IR_SPECIALIZATION_PLAN_LIMIT 1024u

static Expr *clone_expr(const Expr *expr);
static Stmt *clone_stmt(const Stmt *stmt);
static void push_function_clone(FunctionVec *vec, const Function *source);

static void *ir_grow_items(void *items, size_t len, size_t *cap, size_t initial, size_t item_size) {
  if (len + 1 > *cap) {
    *cap = z_grow_capacity(*cap, len + 1, initial);
    return z_checked_reallocarray(items, *cap, item_size);
  }
  return items;
}

static void *ir_grow_tracked_items(IrProgram *ir, void *items, size_t len, size_t *cap, size_t initial, size_t item_size) {
  if (len + 1 > *cap) {
    size_t next_cap = z_grow_capacity(*cap, len + 1, initial);
    void *next_items = z_checked_reallocarray(items, next_cap, item_size);
    if (ir) ir->mir_bytes += next_cap * item_size;
    *cap = next_cap;
    return next_items;
  }
  return items;
}

static void push_param_clone(ParamVec *vec, const Param *param) {
  vec->items = ir_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Param));
  vec->items[vec->len++] = (Param){
    .name = z_strdup(param->name),
    .type = param->type ? z_strdup(param->type) : NULL,
    .default_value = clone_expr(param->default_value),
    .is_static = param->is_static,
    .line = param->line,
    .column = param->column
  };
}

static ParamVec clone_params(const ParamVec *params) {
  ParamVec result = {0};
  for (size_t i = 0; i < params->len; i++) push_param_clone(&result, &params->items[i]);
  return result;
}

static void push_expr_clone(ExprVec *vec, const Expr *expr) {
  vec->items = ir_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Expr *));
  vec->items[vec->len++] = clone_expr(expr);
}

static void push_type_arg_clone(TypeArgVec *vec, const TypeArg *arg) {
  vec->items = ir_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(TypeArg));
  vec->items[vec->len++] = (TypeArg){
    .type = arg->type ? z_strdup(arg->type) : NULL,
    .line = arg->line,
    .column = arg->column
  };
}

static void push_field_clone(FieldInitVec *vec, const FieldInit *field) {
  vec->items = ir_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(FieldInit));
  vec->items[vec->len++] = (FieldInit){
    .name = z_strdup(field->name),
    .value = clone_expr(field->value),
    .line = field->line,
    .column = field->column
  };
}

static Expr *clone_expr(const Expr *expr) {
  if (!expr) return NULL;
  Expr *copy = z_checked_calloc(1, sizeof(Expr));
  copy->kind = expr->kind;
  copy->text = expr->text ? z_strdup(expr->text) : NULL;
  copy->resolved_type = expr->resolved_type ? z_strdup(expr->resolved_type) : NULL;
  copy->moves_ownership = expr->moves_ownership;
  copy->mutable_borrow = expr->mutable_borrow;
  copy->bool_value = expr->bool_value;
  copy->array_repeat = expr->array_repeat;
  copy->prefix_deref = expr->prefix_deref;
  copy->left = clone_expr(expr->left);
  copy->right = clone_expr(expr->right);
  for (size_t i = 0; i < expr->args.len; i++) push_expr_clone(&copy->args, expr->args.items[i]);
  for (size_t i = 0; i < expr->type_args.len; i++) push_type_arg_clone(&copy->type_args, &expr->type_args.items[i]);
  for (size_t i = 0; i < expr->checked_type_args.len; i++) push_type_arg_clone(&copy->checked_type_args, &expr->checked_type_args.items[i]);
  for (size_t i = 0; i < expr->fields.len; i++) push_field_clone(&copy->fields, &expr->fields.items[i]);
  copy->line = expr->line;
  copy->column = expr->column;
  return copy;
}

static void push_stmt_clone(StmtVec *vec, const Stmt *stmt) {
  vec->items = ir_grow_items(vec->items, vec->len, &vec->cap, 8, sizeof(Stmt *));
  vec->items[vec->len++] = clone_stmt(stmt);
}

static StmtVec clone_stmts(const StmtVec *stmts) {
  StmtVec result = {0};
  for (size_t i = 0; i < stmts->len; i++) push_stmt_clone(&result, stmts->items[i]);
  return result;
}

static void push_match_arm_clone(MatchArmVec *vec, const MatchArm *arm) {
  vec->items = ir_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(MatchArm));
  vec->items[vec->len++] = (MatchArm){
    .case_name = z_strdup(arm->case_name),
    .range_end = arm->range_end ? z_strdup(arm->range_end) : NULL,
    .payload_name = arm->payload_name ? z_strdup(arm->payload_name) : NULL,
    .guard = clone_expr(arm->guard),
    .body = clone_stmts(&arm->body),
    .line = arm->line,
    .column = arm->column
  };
}

static Stmt *clone_stmt(const Stmt *stmt) {
  if (!stmt) return NULL;
  Stmt *copy = z_checked_calloc(1, sizeof(Stmt));
  copy->kind = stmt->kind;
  copy->name = stmt->name ? z_strdup(stmt->name) : NULL;
  copy->type = stmt->type ? z_strdup(stmt->type) : NULL;
  copy->resolved_type = stmt->resolved_type ? z_strdup(stmt->resolved_type) : NULL;
  copy->mutable_binding = stmt->mutable_binding;
  copy->target = clone_expr(stmt->target);
  copy->expr = clone_expr(stmt->expr);
  copy->range_end = clone_expr(stmt->range_end);
  copy->then_body = clone_stmts(&stmt->then_body);
  copy->else_body = clone_stmts(&stmt->else_body);
  for (size_t i = 0; i < stmt->match_arms.len; i++) push_match_arm_clone(&copy->match_arms, &stmt->match_arms.items[i]);
  copy->line = stmt->line;
  copy->column = stmt->column;
  return copy;
}

typedef struct { const char *name; IrTypeKind kind; } IrTypeName;

static IrTypeKind ir_type_name_lookup(const IrTypeName *items, size_t len, const char *type) {
  if (!type) return IR_TYPE_UNSUPPORTED;
  for (size_t i = 0; i < len; i++) {
    if (strcmp(type, items[i].name) == 0) return items[i].kind;
  }
  return IR_TYPE_UNSUPPORTED;
}

static bool ir_type_name_is_one_of(const char *const *items, size_t len, const char *type) {
  if (!type) return false;
  for (size_t i = 0; i < len; i++) {
    if (strcmp(type, items[i]) == 0) return true;
  }
  return false;
}

static const IrTypeName ir_scalar_type_names[] = {
  {"Bool", IR_TYPE_BOOL}, {"bool", IR_TYPE_BOOL}, {"u8", IR_TYPE_U8}, {"u16", IR_TYPE_U16}, {"usize", IR_TYPE_USIZE}, {"i32", IR_TYPE_I32}, {"u32", IR_TYPE_U32}, {"i64", IR_TYPE_I64}, {"u64", IR_TYPE_U64},
};

static const IrTypeName ir_builtin_type_names[] = {
  {"Void", IR_TYPE_VOID}, {"Duration", IR_TYPE_I64}, {"RandSource", IR_TYPE_U32}, {"ProcStatus", IR_TYPE_I32}, {"ProcChild", IR_TYPE_I32}, {"Net", IR_TYPE_I32}, {"Conn", IR_TYPE_I32}, {"Listener", IR_TYPE_I32}, {"HttpMethod", IR_TYPE_U32}, {"HttpClient", IR_TYPE_I32}, {"HttpServer", IR_TYPE_I32},
  {"HttpResult", IR_TYPE_U64}, {"HttpError", IR_TYPE_U32}, {"HttpHeaderValue", IR_TYPE_U64}, {"Fs", IR_TYPE_I32}, {"File", IR_TYPE_I32}, {"owned<File>", IR_TYPE_I32},
  {"FixedBufAlloc", IR_TYPE_ALLOC}, {"Vec", IR_TYPE_VEC}, {"BufferedReader", IR_TYPE_BYTE_VIEW}, {"BufferedWriter", IR_TYPE_BYTE_VIEW},
};

static const char *const ir_byte_view_type_names[] = {"String", "Span<const u8>", "Address", "ByteBuf", "owned<ByteBuf>"};
static const char *const ir_maybe_byte_view_type_names[] = {"Maybe<MutSpan<u8>>", "Maybe<Span<u8>>", "Maybe<String>", "Maybe<owned<ByteBuf>>"};
static const char *const ir_maybe_scalar_type_names[] = {"Maybe<JsonDoc>", "Maybe<Bool>", "Maybe<u8>", "Maybe<u16>", "Maybe<usize>", "Maybe<i32>", "Maybe<u32>", "Maybe<i64>", "Maybe<u64>", "Maybe<Duration>", "Maybe<Conn>", "Maybe<Listener>", "Maybe<owned<File>>"};

static IrTypeKind ir_span_element_kind(const char *type) { return ir_type_name_lookup(ir_scalar_type_names, sizeof(ir_scalar_type_names) / sizeof(ir_scalar_type_names[0]), type); }

static bool ir_span_type_element(const char *type, bool *is_mutable, IrTypeKind *element_type) {
  if (!type) return false;
  const char *inner = NULL;
  size_t inner_len = 0;
  size_t type_len = strlen(type);
  bool mut = false;
  if (type_len > strlen("Span<>") && strncmp(type, "Span<", strlen("Span<")) == 0) {
    inner = type + strlen("Span<");
    inner_len = type_len - strlen("Span<") - 1;
  } else if (type_len > strlen("MutSpan<>") && strncmp(type, "MutSpan<", strlen("MutSpan<")) == 0) {
    inner = type + strlen("MutSpan<");
    inner_len = type_len - strlen("MutSpan<") - 1;
    mut = true;
  } else {
    return false;
  }
  if (inner_len == 0 || type[type_len - 1] != '>') return false;
  char *element_text = z_strndup(inner, inner_len);
  IrTypeKind element = ir_span_element_kind(element_text);
  free(element_text);
  if (element == IR_TYPE_UNSUPPORTED) return false;
  if (is_mutable) *is_mutable = mut;
  if (element_type) *element_type = element;
  return true;
}

static bool ir_type_name_is_mutable_byte_view(const char *type) {
  bool is_mutable = false;
  return ir_span_type_element(type, &is_mutable, NULL) && is_mutable;
}

static IrTypeKind ir_type_kind(const char *type);

static IrTypeKind ir_view_element_type_for_type(const char *type) {
  IrTypeKind element = IR_TYPE_UNSUPPORTED;
  if (ir_span_type_element(type, NULL, &element)) return element;
  if (type && ir_type_kind(type) == IR_TYPE_BYTE_VIEW) return IR_TYPE_U8;
  return IR_TYPE_UNSUPPORTED;
}

static IrTypeKind ir_type_kind(const char *type) {
  if (!type) return IR_TYPE_UNSUPPORTED;
  IrTypeKind scalar_type = ir_span_element_kind(type);
  if (scalar_type != IR_TYPE_UNSUPPORTED) return scalar_type;
  IrTypeKind builtin_type = ir_type_name_lookup(ir_builtin_type_names, sizeof(ir_builtin_type_names) / sizeof(ir_builtin_type_names[0]), type);
  if (builtin_type != IR_TYPE_UNSUPPORTED) return builtin_type;
  if (ir_type_name_is_one_of(ir_byte_view_type_names, sizeof(ir_byte_view_type_names) / sizeof(ir_byte_view_type_names[0]), type) ||
      ir_span_type_element(type, NULL, NULL)) {
    return IR_TYPE_BYTE_VIEW;
  }
  if (ir_type_name_is_one_of(ir_maybe_byte_view_type_names, sizeof(ir_maybe_byte_view_type_names) / sizeof(ir_maybe_byte_view_type_names[0]), type)) return IR_TYPE_MAYBE_BYTE_VIEW;
  if (ir_type_name_is_one_of(ir_maybe_scalar_type_names, sizeof(ir_maybe_scalar_type_names) / sizeof(ir_maybe_scalar_type_names[0]), type)) return IR_TYPE_MAYBE_SCALAR;
  return IR_TYPE_UNSUPPORTED;
}

static int ir_std_http_error_code(const char *name) {
  if (!name) return -1;
  if (strcmp(name, "std.http.errorNone") == 0) return 0;
  if (strcmp(name, "std.http.errorInvalidUrl") == 0) return 1;
  if (strcmp(name, "std.http.errorUnsupportedProtocol") == 0) return 2;
  if (strcmp(name, "std.http.errorDns") == 0) return 3;
  if (strcmp(name, "std.http.errorConnect") == 0) return 4;
  if (strcmp(name, "std.http.errorTls") == 0) return 5;
  if (strcmp(name, "std.http.errorTimeout") == 0) return 6;
  if (strcmp(name, "std.http.errorTooLarge") == 0) return 7;
  if (strcmp(name, "std.http.errorProviderUnavailable") == 0) return 8;
  if (strcmp(name, "std.http.errorIo") == 0) return 9;
  if (strcmp(name, "std.http.errorInvalidRequest") == 0) return 10;
  return -1;
}

static int ir_std_json_error_code(const char *name) {
  if (!name) return -1;
  if (strcmp(name, "std.json.errorNone") == 0) return 0;
  if (strcmp(name, "std.json.errorInvalid") == 0) return 1;
  if (strcmp(name, "std.json.errorTrailing") == 0) return 2;
  return -1;
}

static const char *ir_std_json_error_label(unsigned long long code, bool expected) {
  if (expected) {
    if (code == 0) return "none";
    if (code == 1) return "valid-json";
    if (code == 2) return "end-of-input";
    return "unknown";
  }
  if (code == 0) return "ok";
  if (code == 1) return "invalid";
  if (code == 2) return "trailing";
  return "unknown";
}

typedef struct {
  const char *sequence;
  unsigned long long key_code;
  IrTermOp term_op;
  IrTypeKind runtime_type;
  size_t runtime_args;
  bool has_key_code;
  bool has_runtime;
} IrStdTermHelper;

static IrStdTermHelper ir_std_term_helper(const char *name) {
  static const struct { const char *name; const char *sequence; unsigned long long key_code; IrTermOp term_op; IrTypeKind runtime_type; size_t runtime_args; bool has_key_code; bool has_runtime; } entries[] = {
    {"std.term.reset", "\x1b[0m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.bold", "\x1b[1m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.dim", "\x1b[2m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.underline", "\x1b[4m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.inverse", "\x1b[7m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.fgDefault", "\x1b[39m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.fgBlack", "\x1b[30m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.fgRed", "\x1b[31m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.fgGreen", "\x1b[32m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.fgYellow", "\x1b[33m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.fgBlue", "\x1b[34m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.fgMagenta", "\x1b[35m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.fgCyan", "\x1b[36m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.fgWhite", "\x1b[37m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.bgDefault", "\x1b[49m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.bgBlack", "\x1b[40m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.bgRed", "\x1b[41m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.bgGreen", "\x1b[42m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.bgYellow", "\x1b[43m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.bgBlue", "\x1b[44m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.bgMagenta", "\x1b[45m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.bgCyan", "\x1b[46m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.bgWhite", "\x1b[47m", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.clearScreen", "\x1b[2J", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.clearScreenDown", "\x1b[0J", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.clearScreenUp", "\x1b[1J", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.clearLine", "\x1b[2K", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.clearLineRight", "\x1b[0K", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.clearLineLeft", "\x1b[1K", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.cursorHome", "\x1b[H", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.saveCursor", "\x1b[s", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.restoreCursor", "\x1b[u", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.hideCursor", "\x1b[?25l", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.showCursor", "\x1b[?25h", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.enterAltScreen", "\x1b[?1049h", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.leaveAltScreen", "\x1b[?1049l", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.enterBracketedPaste", "\x1b[?2004h", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.leaveBracketedPaste", "\x1b[?2004l", 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false},
    {"std.term.keyNone", NULL, 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyEscape", NULL, 27ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyEnter", NULL, 13ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyTab", NULL, 9ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyBackspace", NULL, 127ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyCtrlA", NULL, 1ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyCtrlC", NULL, 3ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyCtrlD", NULL, 4ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyCtrlE", NULL, 5ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyCtrlK", NULL, 11ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyCtrlL", NULL, 12ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyCtrlN", NULL, 14ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyCtrlP", NULL, 16ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyCtrlR", NULL, 18ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyCtrlU", NULL, 21ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyCtrlW", NULL, 23ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyArrowUp", NULL, 1114113ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyArrowDown", NULL, 1114114ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyArrowRight", NULL, 1114115ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyArrowLeft", NULL, 1114116ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyDelete", NULL, 1114117ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyHome", NULL, 1114118ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyEnd", NULL, 1114119ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyPageUp", NULL, 1114120ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyPageDown", NULL, 1114121ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyInsert", NULL, 1114122ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyShiftTab", NULL, 1114123ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyF1", NULL, 1114124ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyF2", NULL, 1114125ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyF3", NULL, 1114126ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyF4", NULL, 1114127ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyF5", NULL, 1114128ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyF6", NULL, 1114129ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyF7", NULL, 1114130ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyF8", NULL, 1114131ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyF9", NULL, 1114132ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyF10", NULL, 1114133ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyF11", NULL, 1114134ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyF12", NULL, 1114135ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyPasteStart", NULL, 1114136ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.keyPasteEnd", NULL, 1114137ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, true, false},
    {"std.term.stdinIsTty", NULL, 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_BOOL, 0, false, true},
    {"std.term.stdoutIsTty", NULL, 0ull, IR_TERM_OP_STDOUT_IS_TTY, IR_TYPE_BOOL, 0, false, true},
    {"std.term.widthOr", NULL, 0ull, IR_TERM_OP_WIDTH_OR, IR_TYPE_USIZE, 1, false, true},
    {"std.term.heightOr", NULL, 0ull, IR_TERM_OP_HEIGHT_OR, IR_TYPE_USIZE, 1, false, true},
    {"std.term.enterRawMode", NULL, 0ull, IR_TERM_OP_ENTER_RAW_MODE, IR_TYPE_BOOL, 0, false, true},
    {"std.term.leaveRawMode", NULL, 0ull, IR_TERM_OP_LEAVE_RAW_MODE, IR_TYPE_BOOL, 0, false, true},
    {"std.term.readInput", NULL, 0ull, IR_TERM_OP_READ_INPUT, IR_TYPE_MAYBE_SCALAR, 1, false, true},
  };
  IrStdTermHelper missing = {NULL, 0ull, IR_TERM_OP_STDIN_IS_TTY, IR_TYPE_UNSUPPORTED, 0, false, false};
  if (!name) return missing;
  for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
    if (strcmp(name, entries[i].name) == 0) {
      return (IrStdTermHelper){entries[i].sequence, entries[i].key_code, entries[i].term_op, entries[i].runtime_type, entries[i].runtime_args, entries[i].has_key_code, entries[i].has_runtime};
    }
  }
  return missing;
}

static bool ir_std_http_status_class_bounds(const char *name, unsigned *lower, unsigned *upper) {
  if (!name || !lower || !upper) return false;
  if (strcmp(name, "std.http.statusIsInformational") == 0) { *lower = 100; *upper = 200; return true; }
  if (strcmp(name, "std.http.statusIsSuccess") == 0) { *lower = 200; *upper = 300; return true; }
  if (strcmp(name, "std.http.statusIsRedirect") == 0) { *lower = 300; *upper = 400; return true; }
  if (strcmp(name, "std.http.statusIsClientError") == 0) { *lower = 400; *upper = 500; return true; }
  if (strcmp(name, "std.http.statusIsServerError") == 0) { *lower = 500; *upper = 600; return true; }
  return false;
}

static bool ir_type_is_value(IrTypeKind type) {
  return type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_I32 || type == IR_TYPE_U32 || type == IR_TYPE_I64 || type == IR_TYPE_U64;
}

static bool ir_type_is_direct_local(IrTypeKind type) {
  return type == IR_TYPE_BOOL || ir_type_is_value(type) || type == IR_TYPE_BYTE_VIEW ||
         type == IR_TYPE_ALLOC || type == IR_TYPE_VEC || type == IR_TYPE_MAYBE_BYTE_VIEW || type == IR_TYPE_MAYBE_SCALAR;
}

static bool ir_type_is_direct_abi(IrTypeKind type) {
  return type == IR_TYPE_BOOL || ir_type_is_value(type);
}

static bool ir_type_is_direct_param_abi(IrTypeKind type) {
  return ir_type_is_direct_abi(type) || type == IR_TYPE_BYTE_VIEW;
}

static bool ir_type_is_direct_return_abi(IrTypeKind type) {
  return ir_type_is_direct_abi(type) || type == IR_TYPE_BYTE_VIEW || type == IR_TYPE_MAYBE_BYTE_VIEW || type == IR_TYPE_MAYBE_SCALAR;
}

static bool ir_type_is_direct_fallible_value(IrTypeKind type) {
  return type == IR_TYPE_VOID || type == IR_TYPE_BOOL || type == IR_TYPE_U8 ||
         type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_I32 ||
         type == IR_TYPE_U32;
}

static IrTypeKind ir_maybe_scalar_element_type(const char *type) {
  const char *prefix = "Maybe<";
  size_t prefix_len = strlen(prefix);
  size_t len = type ? strlen(type) : 0;
  if (len <= prefix_len + 1 || strncmp(type, prefix, prefix_len) != 0 || type[len - 1] != '>') return IR_TYPE_UNSUPPORTED;
  char *inner = z_strndup(type + prefix_len, len - prefix_len - 1);
  IrTypeKind element = ir_type_kind(inner);
  free(inner);
  return (ir_type_is_value(element) || element == IR_TYPE_BOOL) ? element : IR_TYPE_UNSUPPORTED;
}

static unsigned ir_error_code_for_name(const char *name) {
  if (!name) return IR_ERROR_UNKNOWN;
  if (strcmp(name, "NotFound") == 0) return IR_ERROR_NOT_FOUND;
  if (strcmp(name, "TooLarge") == 0) return IR_ERROR_TOO_LARGE;
  if (strcmp(name, "Io") == 0) return IR_ERROR_IO;
  return IR_ERROR_UNKNOWN;
}

static const EnumDecl *ir_find_enum(const Program *program, const char *name);
static IrTypeKind ir_type_kind_for_program(const Program *program, const char *type);
static bool ir_parse_fixed_array_type_for_program(const Program *program, const char *type, unsigned *out_len, IrTypeKind *out_element);

static const Shape *ir_find_shape(const Program *program, const char *name) {
  if (!program || !name) return NULL;
  for (size_t i = 0; i < program->shapes.len; i++) {
    if (strcmp(program->shapes.items[i].name, name) == 0) return &program->shapes.items[i];
  }
  return NULL;
}

static void ir_type_arg_vec_free(TypeArgVec *args) {
  if (!args) return;
  for (size_t i = 0; i < args->len; i++) free(args->items[i].type);
  free(args->items);
  *args = (TypeArgVec){0};
}

static void ir_type_arg_vec_push_owned(TypeArgVec *args, char *type) {
  args->items = ir_grow_items(args->items, args->len, &args->cap, 4, sizeof(TypeArg));
  args->items[args->len++] = (TypeArg){.type = type};
}

static bool ir_split_generic_args(const char *start, const char *end, TypeArgVec *out_args) {
  const char *arg_start = start;
  int angle_depth = 0;
  int array_depth = 0;
  for (const char *cursor = start; cursor <= end; cursor++) {
    char ch = cursor < end ? *cursor : ',';
    if (ch == '<') angle_depth++;
    else if (ch == '>') angle_depth--;
    else if (ch == '[') array_depth++;
    else if (ch == ']') array_depth--;
    if (ch == ',' && angle_depth == 0 && array_depth == 0) {
      const char *arg_end = cursor;
      while (arg_start < arg_end && (*arg_start == ' ' || *arg_start == '\t')) arg_start++;
      while (arg_end > arg_start && (arg_end[-1] == ' ' || arg_end[-1] == '\t')) arg_end--;
      if (arg_end == arg_start) return false;
      ir_type_arg_vec_push_owned(out_args, z_strndup(arg_start, (size_t)(arg_end - arg_start)));
      arg_start = cursor + 1;
    }
  }
  return angle_depth == 0 && array_depth == 0;
}

static bool ir_shape_instance(const Program *program, const char *type, const Shape **out_shape, TypeArgVec *out_args) {
  const Shape *exact = ir_find_shape(program, type);
  if (exact && exact->type_params.len == 0) {
    if (out_shape) *out_shape = exact;
    return true;
  }
  const char *open = type ? strchr(type, '<') : NULL;
  const char *close = type ? strrchr(type, '>') : NULL;
  if (!open || !close || close[1] != '\0' || close < open) return false;
  char *base = z_strndup(type, (size_t)(open - type));
  const Shape *shape = ir_find_shape(program, base);
  free(base);
  if (!shape || shape->type_params.len == 0) return false;
  TypeArgVec args = {0};
  if (!ir_split_generic_args(open + 1, close, &args) || args.len != shape->type_params.len) {
    ir_type_arg_vec_free(&args);
    return false;
  }
  if (out_shape) *out_shape = shape;
  if (out_args) *out_args = args;
  else ir_type_arg_vec_free(&args);
  return true;
}

static const char *ir_shape_arg_for_param(const Shape *shape, const TypeArgVec *args, const char *name) {
  if (!shape || !args || !name) return NULL;
  for (size_t i = 0; i < shape->type_params.len && i < args->len; i++) {
    if (shape->type_params.items[i].name && strcmp(shape->type_params.items[i].name, name) == 0) {
      return args->items[i].type;
    }
  }
  return NULL;
}

static char *ir_shape_substitute_type(const Shape *shape, const TypeArgVec *args, const char *type) {
  if (!type) return z_strdup("Unknown");
  const char *direct = ir_shape_arg_for_param(shape, args, type);
  if (direct) return z_strdup(direct);
  if (type[0] == '[') {
    const char *close = strchr(type, ']');
    if (close && close[1]) {
      char *len_text = z_strndup(type + 1, (size_t)(close - type - 1));
      const char *len_sub = ir_shape_arg_for_param(shape, args, len_text);
      char *elem = ir_shape_substitute_type(shape, args, close + 1);
      ZBuf buf;
      zbuf_init(&buf);
      zbuf_append_char(&buf, '[');
      zbuf_append(&buf, len_sub ? len_sub : len_text);
      zbuf_append_char(&buf, ']');
      zbuf_append(&buf, elem);
      free(len_text);
      free(elem);
      return buf.data;
    }
  }
  const char *open = strchr(type, '<');
  const char *close = strrchr(type, '>');
  if (open && close && close[1] == '\0' && open < close) {
    TypeArgVec inner_args = {0};
    if (ir_split_generic_args(open + 1, close, &inner_args)) {
      ZBuf buf;
      zbuf_init(&buf);
      for (const char *cursor = type; cursor < open; cursor++) zbuf_append_char(&buf, *cursor);
      zbuf_append_char(&buf, '<');
      for (size_t i = 0; i < inner_args.len; i++) {
        if (i > 0) zbuf_append(&buf, ", ");
        char *inner = ir_shape_substitute_type(shape, args, inner_args.items[i].type);
        zbuf_append(&buf, inner);
        free(inner);
      }
      zbuf_append_char(&buf, '>');
      ir_type_arg_vec_free(&inner_args);
      return buf.data;
    }
    ir_type_arg_vec_free(&inner_args);
  }
  return z_strdup(type);
}

static const EnumDecl *ir_find_enum(const Program *program, const char *name) {
  if (!program || !name) return NULL;
  for (size_t i = 0; i < program->enums.len; i++) {
    if (strcmp(program->enums.items[i].name, name) == 0) return &program->enums.items[i];
  }
  return NULL;
}

static IrTypeKind ir_type_kind_for_program(const Program *program, const char *type) {
  IrTypeKind kind = ir_type_kind(type);
  if (kind != IR_TYPE_UNSUPPORTED) return kind;
  const EnumDecl *item_enum = ir_find_enum(program, type);
  if (!item_enum) return IR_TYPE_UNSUPPORTED;
  IrTypeKind backing = ir_type_kind(item_enum->type ? item_enum->type : "u8");
  if (backing == IR_TYPE_U8 || backing == IR_TYPE_U16 || backing == IR_TYPE_U32) return backing;
  if (backing == IR_TYPE_USIZE) return IR_TYPE_U64;
  return IR_TYPE_UNSUPPORTED;
}

static IrTypeKind ir_enum_backing_type(const EnumDecl *item_enum) {
  const char *type = item_enum && item_enum->type ? item_enum->type : "u8";
  IrTypeKind backing = ir_type_kind(type);
  if (backing == IR_TYPE_U8 || backing == IR_TYPE_U16 || backing == IR_TYPE_U32) return backing;
  if (backing == IR_TYPE_USIZE) return IR_TYPE_U64;
  return IR_TYPE_UNSUPPORTED;
}

static bool ir_enum_case_value(const EnumDecl *item_enum, const char *case_name, unsigned long long *out) {
  if (!item_enum || !case_name) return false;
  for (size_t i = 0; i < item_enum->cases.len; i++) {
    if (strcmp(item_enum->cases.items[i].name, case_name) == 0) {
      if (out) *out = (unsigned long long)i;
      return true;
    }
  }
  return false;
}

static unsigned ir_type_byte_size(IrTypeKind type) {
  switch (type) {
    case IR_TYPE_BOOL:
    case IR_TYPE_U8: return 1;
    case IR_TYPE_U16: return 2;
    case IR_TYPE_I32:
    case IR_TYPE_U32: return 4;
    case IR_TYPE_I64:
    case IR_TYPE_USIZE:
    case IR_TYPE_U64: return 8;
    default: return 0;
  }
}

static unsigned ir_type_alignment(IrTypeKind type) {
  unsigned size = ir_type_byte_size(type);
  return size >= 8 ? 8 : (size >= 4 ? 4 : (size ? size : 1));
}

static bool ir_parse_fixed_array_type(const char *type, unsigned *out_len, IrTypeKind *out_element);

static size_t ir_align_to(size_t value, size_t alignment) {
  size_t remainder = alignment ? value % alignment : 0;
  return remainder == 0 ? value : value + (alignment - remainder);
}

static bool ir_field_storage_info_for_type(const Program *program, const char *type_text, unsigned *out_byte_size, unsigned *out_align, IrTypeKind *out_type, bool *out_is_array, unsigned *out_array_len, IrTypeKind *out_element_type) {
  IrTypeKind type = ir_type_kind_for_program(program, type_text);
  unsigned array_len = 0;
  IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
  bool is_array = ir_parse_fixed_array_type_for_program(program, type_text, &array_len, &element_type);
  unsigned byte_size = 0;
  unsigned align = 0;
  if (is_array) {
    byte_size = ir_type_byte_size(element_type) * array_len;
    align = ir_type_alignment(element_type);
  } else if (type == IR_TYPE_BYTE_VIEW) {
    byte_size = 16;
    align = 8;
    element_type = ir_view_element_type_for_type(type_text);
  } else if (type == IR_TYPE_BOOL || ir_type_is_value(type)) {
    byte_size = ir_type_byte_size(type);
    align = ir_type_alignment(type);
  } else {
    return false;
  }
  if (!byte_size || !align) return false;
  if (out_byte_size) *out_byte_size = byte_size;
  if (out_align) *out_align = align;
  if (out_type) *out_type = type;
  if (out_is_array) *out_is_array = is_array;
  if (out_array_len) *out_array_len = array_len;
  if (out_element_type) *out_element_type = element_type;
  return true;
}

static bool ir_shape_layout(const Program *program, const char *shape_name, unsigned *out_size, unsigned *out_align) {
  const Shape *shape = NULL;
  TypeArgVec args = {0};
  if (!ir_shape_instance(program, shape_name, &shape, &args)) return false;
  size_t offset = 0;
  unsigned max_align = 1;
  for (size_t i = 0; i < shape->fields.len; i++) {
    char *field_type_text = ir_shape_substitute_type(shape, &args, shape->fields.items[i].type);
    unsigned byte_size = 0;
    unsigned align = 0;
    bool ok = ir_field_storage_info_for_type(program, field_type_text, &byte_size, &align, NULL, NULL, NULL, NULL);
    if (!ok) {
      free(field_type_text);
      ir_type_arg_vec_free(&args);
      return false;
    }
    free(field_type_text);
    offset = ir_align_to(offset, align);
    offset += byte_size;
    if (align > max_align) max_align = align;
  }
  offset = ir_align_to(offset, max_align);
  if (out_size) *out_size = (unsigned)offset;
  if (out_align) *out_align = max_align;
  ir_type_arg_vec_free(&args);
  return offset <= UINT_MAX;
}

static bool ir_shape_field_info(const Program *program, const char *shape_name, const char *field_name, unsigned *out_offset, IrTypeKind *out_type, IrTypeKind *out_element_type) {
  const Shape *shape = NULL;
  TypeArgVec args = {0};
  if (!field_name || !ir_shape_instance(program, shape_name, &shape, &args)) return false;
  size_t offset = 0;
  for (size_t i = 0; i < shape->fields.len; i++) {
    const Param *field = &shape->fields.items[i];
    char *field_type_text = ir_shape_substitute_type(shape, &args, field->type);
    IrTypeKind field_type = IR_TYPE_UNSUPPORTED;
    IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
    bool is_array = false;
    unsigned byte_size = 0;
    unsigned align = 0;
    bool ok = ir_field_storage_info_for_type(program, field_type_text, &byte_size, &align, &field_type, &is_array, NULL, &element_type);
    if (!ok) {
      free(field_type_text);
      ir_type_arg_vec_free(&args);
      return false;
    }
    offset = ir_align_to(offset, align);
    if (strcmp(field->name, field_name) == 0) {
      free(field_type_text);
      if (is_array) {
        ir_type_arg_vec_free(&args);
        return false;
      }
      if (out_offset) *out_offset = (unsigned)offset;
      if (out_type) *out_type = field_type;
      if (out_element_type) *out_element_type = element_type;
      ir_type_arg_vec_free(&args);
      return true;
    }
    free(field_type_text);
    offset += byte_size;
  }
  ir_type_arg_vec_free(&args);
  return false;
}

static bool ir_shape_field_storage_info(const Program *program, const char *shape_name, const char *field_name, unsigned *out_offset, IrTypeKind *out_type, bool *out_is_array, unsigned *out_array_len, IrTypeKind *out_element_type) {
  const Shape *shape = NULL;
  TypeArgVec args = {0};
  if (!field_name || !ir_shape_instance(program, shape_name, &shape, &args)) return false;
  size_t offset = 0;
  for (size_t i = 0; i < shape->fields.len; i++) {
    const Param *field = &shape->fields.items[i];
    char *field_type_text = ir_shape_substitute_type(shape, &args, field->type);
    IrTypeKind field_type = IR_TYPE_UNSUPPORTED;
    bool is_array = false;
    unsigned array_len = 0;
    IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
    unsigned byte_size = 0;
    unsigned align = 0;
    bool ok = ir_field_storage_info_for_type(program, field_type_text, &byte_size, &align, &field_type, &is_array, &array_len, &element_type);
    if (!ok) {
      free(field_type_text);
      ir_type_arg_vec_free(&args);
      return false;
    }
    offset = ir_align_to(offset, align);
    if (strcmp(field->name, field_name) == 0) {
      free(field_type_text);
      if (out_offset) *out_offset = (unsigned)offset;
      if (out_type) *out_type = field_type;
      if (out_is_array) *out_is_array = is_array;
      if (out_array_len) *out_array_len = array_len;
      if (out_element_type) *out_element_type = element_type;
      ir_type_arg_vec_free(&args);
      return true;
    }
    free(field_type_text);
    offset += byte_size;
  }
  ir_type_arg_vec_free(&args);
  return false;
}

static bool ir_parse_fixed_array_type_for_program(const Program *program, const char *type, unsigned *out_len, IrTypeKind *out_element) {
  if (!type || type[0] != '[') return false;
  const char *close = strchr(type, ']');
  if (!close || close == type + 1 || !close[1]) return false;
  unsigned long long len = 0;
  for (const char *ch = type + 1; ch < close; ch++) {
    if (*ch < '0' || *ch > '9') return false;
    len = len * 10 + (unsigned)(*ch - '0');
    if (len > UINT_MAX) return false;
  }
  IrTypeKind element = ir_type_kind_for_program(program, close + 1);
  if (element != IR_TYPE_BOOL && element != IR_TYPE_U8 && element != IR_TYPE_U16 && element != IR_TYPE_I32 && element != IR_TYPE_U32 &&
      element != IR_TYPE_USIZE && element != IR_TYPE_I64 && element != IR_TYPE_U64) {
    return false;
  }
  if (out_len) *out_len = (unsigned)len;
  if (out_element) *out_element = element;
  return len > 0;
}

static bool ir_parse_fixed_array_type(const char *type, unsigned *out_len, IrTypeKind *out_element) {
  return ir_parse_fixed_array_type_for_program(NULL, type, out_len, out_element);
}

void z_backend_blocker_set(ZBackendBlocker *blocker, const char *target, const char *object_format, const char *backend, const char *stage, const char *unsupported_feature) {
  if (!blocker) return;
  memset(blocker, 0, sizeof(*blocker));
  blocker->present = true;
  snprintf(blocker->target, sizeof(blocker->target), "%s", target ? target : "");
  snprintf(blocker->object_format, sizeof(blocker->object_format), "%s", object_format ? object_format : "");
  snprintf(blocker->backend, sizeof(blocker->backend), "%s", backend ? backend : "");
  snprintf(blocker->stage, sizeof(blocker->stage), "%s", stage ? stage : "");
  snprintf(blocker->unsupported_feature, sizeof(blocker->unsupported_feature), "%s", unsupported_feature ? unsupported_feature : "");
}

void z_diag_set_backend_blocker(ZDiag *diag, const ZBackendBlocker *blocker) {
  if (!diag || !blocker || !blocker->present) return;
  diag->backend_blocker = *blocker;
}

static void ir_mark_unsupported(IrProgram *ir, const char *message, int line, int column, const char *actual) {
  if (!ir || !ir->mir_valid) return;
  ir->mir_valid = false;
  ir->mir_line = line > 0 ? line : 1;
  ir->mir_column = column > 0 ? column : 1;
  snprintf(ir->mir_message, sizeof(ir->mir_message), "%s", message ? message : "direct backend lowering failed");
  snprintf(ir->mir_expected, sizeof(ir->mir_expected), "direct backend MVP subset");
  snprintf(ir->mir_actual, sizeof(ir->mir_actual), "%s", actual ? actual : "unsupported construct");
  snprintf(ir->mir_help, sizeof(ir->mir_help), "restrict this program to exported primitive arithmetic functions or choose another supported direct target");
  z_backend_blocker_set(&ir->backend_blocker, NULL, NULL, NULL, "lower", ir->mir_actual);
}

static bool ir_parse_integer_literal(const char *text, unsigned long long *out) {
  if (!text || !text[0]) return false;
  size_t text_len = strlen(text);
  size_t body_len = text_len;
  const char *last_underscore = strrchr(text, '_');
  if (last_underscore && (last_underscore[1] == 'i' || last_underscore[1] == 'u')) body_len = (size_t)(last_underscore - text);
  if (body_len == 0) return false;
  unsigned radix = 10;
  size_t index = 0;
  if (body_len > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    radix = 16;
    index = 2;
  } else if (body_len > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
    radix = 2;
    index = 2;
  } else if (body_len > 2 && text[0] == '0' && (text[1] == 'o' || text[1] == 'O')) {
    radix = 8;
    index = 2;
  }
  unsigned long long value = 0;
  bool saw_digit = false;
  for (; index < body_len; index++) {
    char ch = text[index];
    if (ch == '_') continue;
    int digit = -1;
    if (ch >= '0' && ch <= '9') digit = ch - '0';
    else if (ch >= 'a' && ch <= 'f') digit = 10 + (ch - 'a');
    else if (ch >= 'A' && ch <= 'F') digit = 10 + (ch - 'A');
    if (digit < 0 || (unsigned)digit >= radix) return false;
    if (value > (ULLONG_MAX - (unsigned)digit) / radix) return false;
    value = value * radix + (unsigned)digit;
    saw_digit = true;
  }
  if (!saw_digit) return false;
  if (out) *out = value;
  return true;
}

static bool ir_string_literal_bytes(const Expr *expr, const unsigned char **out_bytes, size_t *out_len) {
  if (!expr || expr->kind != EXPR_STRING || !expr->text) return false;
  if (out_bytes) *out_bytes = (const unsigned char *)expr->text;
  if (out_len) *out_len = strlen(expr->text);
  return true;
}

static bool ir_parse_decimal_bytes(const unsigned char *bytes, size_t len, unsigned long long max, unsigned long long *out) {
  if (!bytes || len == 0) return false;
  unsigned long long value = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char ch = bytes[i];
    if (ch < '0' || ch > '9') return false;
    unsigned digit = (unsigned)(ch - '0');
    if (value > (max - digit) / 10ull) return false;
    value = value * 10ull + digit;
  }
  if (out) *out = value;
  return true;
}

static size_t ir_scan_digits_bytes(const unsigned char *bytes, size_t len) {
  size_t count = 0;
  while (count < len && bytes[count] >= '0' && bytes[count] <= '9') count++;
  return count;
}

static size_t ir_scan_identifier_bytes(const unsigned char *bytes, size_t len) {
  size_t count = 0;
  while (count < len) {
    unsigned char ch = bytes[count];
    bool ident = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                 (count > 0 && ch >= '0' && ch <= '9') || ch == '_';
    if (!ident) break;
    count++;
  }
  return count;
}

static IrValue *ir_new_value(IrProgram *ir, IrValueKind kind, IrTypeKind type, int line, int column);

static IrValue *ir_new_bool_literal(IrProgram *ir, bool enabled, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_BOOL, IR_TYPE_BOOL, line, column);
  value->int_value = enabled ? 1 : 0;
  return value;
}

static IrValue *ir_new_integer_literal_value(IrProgram *ir, IrTypeKind type, unsigned long long number, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_INT, type, line, column);
  value->int_value = number;
  return value;
}

static IrValue *ir_new_maybe_scalar_literal(IrProgram *ir, bool has, IrTypeKind element_type, unsigned long long number, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_SCALAR_LITERAL, IR_TYPE_MAYBE_SCALAR, line, column);
  value->data_len = has ? 1u : 0u;
  value->element_type = element_type;
  value->int_value = number;
  return value;
}

static IrValue *ir_new_maybe_byte_view_literal(IrProgram *ir, bool has, IrValue *view, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_BYTE_VIEW_LITERAL, IR_TYPE_MAYBE_BYTE_VIEW, line, column);
  value->data_len = has ? 1u : 0u;
  value->element_type = view && view->element_type != IR_TYPE_UNSUPPORTED ? view->element_type : IR_TYPE_U8;
  value->left = view;
  return value;
}

static IrValue *ir_new_value(IrProgram *ir, IrValueKind kind, IrTypeKind type, int line, int column) {
  IrValue *value = z_checked_calloc(1, sizeof(IrValue));
  value->kind = kind;
  value->type = type;
  value->line = line;
  value->column = column;
  if (ir) ir->mir_bytes += sizeof(IrValue);
  return value;
}

static IrValue *ir_new_cast_value(IrProgram *ir, IrValue *inner, IrTypeKind type, int line, int column) {
  if (inner->type == type) return inner;
  IrValue *value = ir_new_value(ir, IR_VALUE_CAST, type, line, column);
  value->left = inner;
  return value;
}

static IrValue *ir_new_binary_value(IrProgram *ir, IrBinaryOp op, IrTypeKind type, IrValue *left, IrValue *right, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_BINARY, type, line, column);
  value->binary_op = op;
  value->left = left;
  value->right = right;
  return value;
}

static IrValue *ir_new_compare_value(IrProgram *ir, IrCompareOp op, IrValue *left, IrValue *right, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_COMPARE, IR_TYPE_BOOL, line, column);
  value->compare_op = op;
  value->left = left;
  value->right = right;
  return value;
}

typedef enum {
  IR_VEC_HELPER_NONE = 0,
  IR_VEC_HELPER_LEN,
  IR_VEC_HELPER_CAPACITY,
  IR_VEC_HELPER_REMAINING,
  IR_VEC_HELPER_IS_EMPTY,
  IR_VEC_HELPER_IS_FULL,
  IR_VEC_HELPER_BYTES
} IrVecHelper;

#define IR_LITERAL_EQ(text, literal) ((text) && strlen(text) == sizeof(literal) - 1 && memcmp((text), (literal), sizeof(literal) - 1) == 0)

static IrVecHelper ir_std_mem_vec_helper(const char *callee_name) {
  if (IR_LITERAL_EQ(callee_name, "std.mem.vecLen")) return IR_VEC_HELPER_LEN;
  if (IR_LITERAL_EQ(callee_name, "std.mem.vecCapacity")) return IR_VEC_HELPER_CAPACITY;
  if (IR_LITERAL_EQ(callee_name, "std.mem.vecRemaining")) return IR_VEC_HELPER_REMAINING;
  if (IR_LITERAL_EQ(callee_name, "std.mem.vecIsEmpty")) return IR_VEC_HELPER_IS_EMPTY;
  if (IR_LITERAL_EQ(callee_name, "std.mem.vecIsFull")) return IR_VEC_HELPER_IS_FULL;
  if (IR_LITERAL_EQ(callee_name, "std.mem.vecBytes")) return IR_VEC_HELPER_BYTES;
  return IR_VEC_HELPER_NONE;
}

static IrValue *ir_new_vec_helper_value(IrProgram *ir, IrVecHelper helper, size_t local_index, int line, int column) {
  if (helper == IR_VEC_HELPER_BYTES) {
    IrValue *bytes = ir_new_value(ir, IR_VALUE_VEC_BYTES, IR_TYPE_BYTE_VIEW, line, column);
    bytes->local_index = local_index;
    bytes->element_type = IR_TYPE_U8;
    return bytes;
  }
  if (helper == IR_VEC_HELPER_CAPACITY) {
    IrValue *capacity = ir_new_value(ir, IR_VALUE_VEC_CAPACITY, IR_TYPE_USIZE, line, column);
    capacity->local_index = local_index;
    return capacity;
  }
  IrValue *len = ir_new_value(ir, IR_VALUE_VEC_LEN, IR_TYPE_USIZE, line, column);
  len->local_index = local_index;
  if (helper == IR_VEC_HELPER_LEN) return len;
  if (helper == IR_VEC_HELPER_IS_EMPTY) return ir_new_compare_value(ir, IR_CMP_EQ, len, ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 0, line, column), line, column);
  IrValue *capacity = ir_new_value(ir, IR_VALUE_VEC_CAPACITY, IR_TYPE_USIZE, line, column);
  capacity->local_index = local_index;
  if (helper == IR_VEC_HELPER_REMAINING) return ir_new_binary_value(ir, IR_BIN_SUB, IR_TYPE_USIZE, capacity, len, line, column);
  if (helper == IR_VEC_HELPER_IS_FULL) return ir_new_compare_value(ir, IR_CMP_EQ, len, capacity, line, column);
  return len;
}

static void ir_free_value(IrValue *value) {
  if (!value) return;
  ir_free_value(value->index);
  ir_free_value(value->left);
  ir_free_value(value->right);
  for (size_t i = 0; i < value->arg_len; i++) ir_free_value(value->args[i]);
  free(value->args);
  free(value);
}

static void ir_free_instrs(IrInstr *instrs, size_t len) {
  if (!instrs) return;
  for (size_t i = 0; i < len; i++) {
    ir_free_value(instrs[i].value);
    ir_free_value(instrs[i].index);
    ir_free_instrs(instrs[i].then_instrs, instrs[i].then_len);
    ir_free_instrs(instrs[i].else_instrs, instrs[i].else_len);
    free(instrs[i].then_instrs);
    free(instrs[i].else_instrs);
  }
}

static void ir_function_push_local(IrProgram *ir, IrFunction *fun, const char *name, IrTypeKind type, bool is_param, bool is_array, bool is_record, const char *shape_name, IrTypeKind element_type, unsigned array_len, unsigned byte_size_override, unsigned alignment_override, bool is_mutable, int line, int column) {
  fun->locals = ir_grow_tracked_items(ir, fun->locals, fun->local_len, &fun->local_cap, 4, sizeof(IrLocal));
  unsigned byte_size = byte_size_override ? byte_size_override : (is_array ? ir_type_byte_size(element_type) * array_len : (type == IR_TYPE_BYTE_VIEW || type == IR_TYPE_ALLOC || type == IR_TYPE_VEC ? 16 : (type == IR_TYPE_MAYBE_BYTE_VIEW ? 24 : (type == IR_TYPE_MAYBE_SCALAR ? 16 : 8))));
  unsigned alignment = alignment_override ? alignment_override : (is_array ? ir_type_alignment(element_type) : 8);
  fun->locals[fun->local_len] = (IrLocal){
    .name = z_strdup(name),
    .type = type,
    .element_type = element_type,
    .index = (unsigned)fun->local_len,
    .array_len = array_len,
    .byte_size = byte_size,
    .alignment = alignment,
    .is_param = is_param,
    .is_array = is_array,
    .is_record = is_record,
    .is_mutable = is_mutable,
    .shape_name = shape_name ? z_strdup(shape_name) : NULL,
    .line = line,
    .column = column
  };
  fun->local_len++;
  if (is_param) fun->param_count++;
}

static const IrLocal *ir_function_find_local(const IrFunction *fun, const char *name) {
  for (size_t i = fun ? fun->local_len : 0; i > 0; i--) {
    if (strcmp(fun->locals[i - 1].name, name) == 0) return &fun->locals[i - 1];
  }
  return NULL;
}

static size_t ir_active_local_mark(const IrProgram *ir) {
  return ir ? ir->active_local_len : 0;
}

static void ir_active_local_restore(IrProgram *ir, size_t mark) {
  if (!ir) return;
  while (ir->active_local_len > mark) {
    free(ir->active_local_names[--ir->active_local_len]);
    ir->active_local_names[ir->active_local_len] = NULL;
  }
}

static void ir_active_local_push(IrProgram *ir, const char *name) {
  if (!ir || !name) return;
  ir->active_local_names = ir_grow_items(ir->active_local_names, ir->active_local_len, &ir->active_local_cap, 8, sizeof(char *));
  ir->active_local_names[ir->active_local_len++] = z_strdup(name);
}

static bool ir_active_local_has(const IrProgram *ir, const char *name) {
  for (size_t i = ir ? ir->active_local_len : 0; i > 0; i--) {
    if (ir->active_local_names[i - 1] && name && strcmp(ir->active_local_names[i - 1], name) == 0) return true;
  }
  return false;
}

static const IrLocal *ir_find_mutable_rand_source_local(IrProgram *ir, const IrFunction *fun, const Expr *arg, const char *helper_name) {
  if (!arg || arg->kind != EXPR_BORROW || !arg->mutable_borrow || !arg->left || arg->left->kind != EXPR_IDENT) {
    ir_mark_unsupported(ir, "direct backend std.rand helper expects a mutable RandSource local", arg ? arg->line : 1, arg ? arg->column : 1, helper_name ? helper_name : "std.rand");
    return NULL;
  }
  const IrLocal *rng = ir_function_find_local(fun, arg->left->text);
  if (!rng || rng->type != IR_TYPE_U32 || !rng->is_mutable) {
    ir_mark_unsupported(ir, "direct backend std.rand helper expects a mutable RandSource local", arg->line, arg->column, "non-mutable RandSource");
    return NULL;
  }
  return rng;
}

static const Function *ir_find_source_function(const Program *program, const char *name, unsigned *out_index) {
  if (!program || !name) return NULL;
  for (size_t i = 0; i < program->functions.len; i++) {
    if (strcmp(program->functions.items[i].name, name) == 0) {
      if (out_index) *out_index = (unsigned)i;
      return &program->functions.items[i];
    }
  }
  return NULL;
}

static bool ir_find_function_index(const IrProgram *ir, const char *name, unsigned *out_index) {
  if (!ir || !name) return false;
  for (size_t i = 0; i < ir->function_len; i++) {
    if (strcmp(ir->functions[i].name, name) == 0) {
      if (out_index) *out_index = (unsigned)i;
      return true;
    }
  }
  return false;
}

static void ir_external_function_push_param(IrProgram *ir, IrExternalFunction *function, IrTypeKind type) {
  function->param_types = ir_grow_tracked_items(ir, function->param_types, function->param_len, &function->param_cap, 4, sizeof(IrTypeKind));
  function->param_types[function->param_len++] = type;
}

static bool ir_find_external_function_index(const IrProgram *ir, const ZCImportFunction *function, unsigned *out_index) {
  if (!ir || !function || !function->name) return false;
  for (size_t i = 0; i < ir->external_function_len; i++) {
    const IrExternalFunction *candidate = &ir->external_functions[i];
    const char *candidate_header = candidate->import_resolved_header && candidate->import_resolved_header[0] ? candidate->import_resolved_header : candidate->import_header;
    const char *function_header = function->import_resolved_header && function->import_resolved_header[0] ? function->import_resolved_header : function->import_header;
    if (!candidate->symbol || strcmp(candidate->symbol, function->name) != 0 || candidate->return_type != ir_type_kind(function->return_zero_type) || candidate->param_len != function->param_len) continue;
    if ((candidate_header && !function_header) || (!candidate_header && function_header) ||
        (candidate_header && function_header && strcmp(candidate_header, function_header) != 0)) continue;
    bool params_match = true;
    for (size_t p = 0; p < function->param_len; p++) {
      if (candidate->param_types[p] != ir_type_kind(function->params[p].zero_type)) {
        params_match = false;
        break;
      }
    }
    if (params_match) {
      if (out_index) *out_index = (unsigned)i;
      return true;
    }
  }
  return false;
}

static unsigned ir_add_external_function(IrProgram *ir, const ZCImportFunction *function) {
  unsigned index = 0;
  if (ir_find_external_function_index(ir, function, &index)) return index;
  ir->external_functions = ir_grow_tracked_items(ir, ir->external_functions, ir->external_function_len, &ir->external_function_cap, 4, sizeof(IrExternalFunction));
  IrExternalFunction *external = &ir->external_functions[ir->external_function_len];
  *external = (IrExternalFunction){
    .symbol = z_strdup(function->name),
    .import_header = function->import_header ? z_strdup(function->import_header) : NULL,
    .import_resolved_header = function->import_resolved_header ? z_strdup(function->import_resolved_header) : NULL,
    .return_type = ir_type_kind(function->return_zero_type)
  };
  for (size_t i = 0; i < function->param_len; i++) {
    ir_external_function_push_param(ir, external, ir_type_kind(function->params[i].zero_type));
  }
  return (unsigned)ir->external_function_len++;
}

typedef struct {
  const char *name;
  const char *stable_id;
  size_t source_index;
} IrFunctionOrder;

static int ir_function_order_compare(const void *left, const void *right) {
  const IrFunctionOrder *a = (const IrFunctionOrder *)left;
  const IrFunctionOrder *b = (const IrFunctionOrder *)right;
  int stable_cmp = strcmp(a->stable_id ? a->stable_id : "", b->stable_id ? b->stable_id : "");
  if (stable_cmp != 0) return stable_cmp;
  if (a->source_index < b->source_index) return -1;
  if (a->source_index > b->source_index) return 1;
  return 0;
}

static char *ir_stable_id_for_source_function(const SourceInput *input, const Function *source) {
  if (input && source && source->name) {
    for (size_t i = 0; i < input->symbol_count; i++) {
      if (input->symbol_names[i] && input->symbol_kinds[i] &&
          strcmp(input->symbol_names[i], source->name) == 0 &&
          strcmp(input->symbol_kinds[i], "function") == 0) {
        ZBuf id;
        zbuf_init(&id);
        zbuf_append(&id, input->symbol_modules[i] ? input->symbol_modules[i] : "main");
        zbuf_append_char(&id, '.');
        zbuf_append(&id, source->name);
        return id.data;
      }
    }
  }
  ZBuf fallback;
  zbuf_init(&fallback);
  zbuf_append(&fallback, "main.");
  zbuf_append(&fallback, source && source->name ? source->name : "");
  return fallback.data;
}

static void ir_value_push_arg(IrProgram *ir, IrValue *value, IrValue *arg) {
  value->args = ir_grow_tracked_items(ir, value->args, value->arg_len, &value->arg_cap, 4, sizeof(IrValue *));
  value->args[value->arg_len++] = arg;
}

static IrValue *ir_new_index_literal(IrProgram *ir, unsigned index, int line, int column) {
  IrValue *value = ir_new_value(ir, IR_VALUE_INT, IR_TYPE_USIZE, line, column);
  value->int_value = index;
  return value;
}

static bool ir_add_readonly_data(IrProgram *ir, const unsigned char *bytes, unsigned len, int line, int column, unsigned *out_offset) {
  for (size_t i = 0; i < ir->data_segment_len; i++) {
    IrDataSegment *segment = &ir->data_segments[i];
    if (segment->len == len && (len == 0 || memcmp(segment->bytes, bytes, len) == 0)) {
      if (out_offset) *out_offset = segment->offset;
      return true;
    }
  }

  size_t offset = IR_READONLY_DATA_BASE + ir->readonly_data_bytes;
  if (offset + len >= IR_READONLY_DATA_LIMIT) {
    ir_mark_unsupported(ir, "direct backend readonly data exceeds the bootstrap data limit", line, column, "string/data segment");
    return false;
  }
  ir->data_segments = ir_grow_tracked_items(ir, ir->data_segments, ir->data_segment_len, &ir->data_segment_cap, 4, sizeof(IrDataSegment));
  IrDataSegment *segment = &ir->data_segments[ir->data_segment_len++];
  segment->offset = (unsigned)offset;
  segment->len = len;
  segment->bytes = z_checked_malloc(len == 0 ? 1 : len);
  if (len > 0) memcpy(segment->bytes, bytes, len);
  ir->readonly_data_bytes += len;
  ir->direct_readonly_data_bytes = ir->readonly_data_bytes;
  if (out_offset) *out_offset = segment->offset;
  return true;
}

static char *ir_expr_callee_name(const Expr *expr);

static bool ir_expr_is_byte_view_source(const Expr *expr) {
  if (expr && expr->kind == EXPR_CALL && expr->args.len == 1) {
    char *callee = ir_expr_callee_name(expr->left);
    bool is_span = callee && (strcmp(callee, "std.mem.span") == 0 || strcmp(callee, "std.mem.bufBytes") == 0);
    free(callee);
    if (is_span) return true;
  }
  if (expr && expr->resolved_type) {
    unsigned array_len = 0;
    IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
    if (ir_parse_fixed_array_type(expr->resolved_type, &array_len, &element_type) && (ir_type_is_value(element_type) || element_type == IR_TYPE_BOOL)) return true;
    if (ir_type_kind(expr->resolved_type) == IR_TYPE_BYTE_VIEW) return true;
  }
  return expr && (expr->kind == EXPR_STRING || expr->kind == EXPR_SLICE ||
                  expr->kind == EXPR_BORROW ||
                  (expr->kind == EXPR_IDENT && expr->resolved_type && ir_type_kind(expr->resolved_type) == IR_TYPE_BYTE_VIEW) ||
                  (expr->kind == EXPR_MEMBER && expr->resolved_type && ir_type_kind(expr->resolved_type) == IR_TYPE_BYTE_VIEW));
}

static bool ir_expr_is_mutable_record_byte_field_dest(const Program *program, const IrFunction *fun, const Expr *expr) {
  if (!program || !fun || !expr || expr->kind != EXPR_MEMBER || !expr->left || expr->left->kind != EXPR_IDENT) return false;
  const IrLocal *record = ir_function_find_local(fun, expr->left->text);
  unsigned field_offset = 0;
  bool field_is_array = false;
  unsigned field_array_len = 0;
  IrTypeKind field_element_type = IR_TYPE_UNSUPPORTED;
  if (!record || !record->is_record || !record->is_mutable ||
      !ir_shape_field_storage_info(program, record->shape_name, expr->text, &field_offset, NULL, &field_is_array, &field_array_len, &field_element_type) ||
      !field_is_array || field_element_type != IR_TYPE_U8) {
    return false;
  }
  return field_offset <= record->byte_size && field_array_len <= record->byte_size - field_offset;
}

static bool ir_expr_is_mutable_byte_view_dest(const Program *program, const IrFunction *fun, const Expr *expr) {
  if (!fun || !expr) return false;
  if (expr->kind == EXPR_SLICE) return ir_expr_is_mutable_byte_view_dest(program, fun, expr->left);
  if (expr->kind == EXPR_MEMBER && expr->left && expr->left->kind == EXPR_IDENT && strcmp(expr->text ? expr->text : "", "value") == 0) {
    const IrLocal *local = ir_function_find_local(fun, expr->left->text);
    return local && local->type == IR_TYPE_MAYBE_BYTE_VIEW;
  }
  if (ir_expr_is_mutable_record_byte_field_dest(program, fun, expr)) return true;
  if (expr->kind != EXPR_IDENT) return false;
  const IrLocal *local = ir_function_find_local(fun, expr->text);
  if (!local) return false;
  if (local->is_array) return local->is_mutable && local->element_type == IR_TYPE_U8;
  return local->type == IR_TYPE_BYTE_VIEW && local->is_mutable;
}

static bool ir_is_hosted_world_main(const Function *source) {
  return source &&
         source->is_public &&
         source->name && strcmp(source->name, "main") == 0 &&
         source->params.len == 1 &&
         source->params.items[0].type && strcmp(source->params.items[0].type, "World") == 0 &&
         source->return_type && strcmp(source->return_type, "Void") == 0;
}

static bool ir_is_world_stream_write(const IrFunction *fun, const Expr *expr, const char *stream) {
  if (!expr || expr->kind != EXPR_CALL || expr->args.len != 1) return false;
  const Expr *write = expr->left;
  if (!write || write->kind != EXPR_MEMBER || strcmp(write->text ? write->text : "", "write") != 0) return false;
  const Expr *stream_expr = write->left;
  if (!stream_expr || stream_expr->kind != EXPR_MEMBER || strcmp(stream_expr->text ? stream_expr->text : "", stream) != 0) return false;
  const Expr *world = stream_expr->left;
  return fun && fun->world_param_name && world && world->kind == EXPR_IDENT && strcmp(world->text ? world->text : "", fun->world_param_name) == 0;
}

static bool ir_lower_expr(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *expr, IrValue **out);

static bool ir_lower_time_duration_constructor(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *arg, unsigned long long scale, int line, int column, IrValue **out) {
  if (!arg) {
    ir_mark_unsupported(ir, "direct backend time duration constructor is missing an argument", line, column, "missing argument");
    return false;
  }
  unsigned long long literal = 0;
  if (arg->kind == EXPR_NUMBER && ir_parse_integer_literal(arg->text ? arg->text : "0", &literal)) {
    if (scale != 0 && literal > (unsigned long long)INT64_MAX / scale) {
      ir_mark_unsupported(ir, "direct backend time duration literal exceeds i64 nanoseconds", arg->line, arg->column, arg->text);
      return false;
    }
    *out = ir_new_integer_literal_value(ir, IR_TYPE_I64, literal * scale, line, column);
    return true;
  }

  IrValue *value = NULL;
  if (!ir_lower_expr(program, ir, fun, arg, &value)) return false;
  if (!ir_type_is_value(value->type)) {
    ir_free_value(value);
    ir_mark_unsupported(ir, "direct backend time duration constructor argument must be an integer value", arg->line, arg->column, "non-integer duration");
    return false;
  }
  value = ir_new_cast_value(ir, value, IR_TYPE_I64, line, column);
  if (scale != 1) {
    IrValue *factor = ir_new_integer_literal_value(ir, IR_TYPE_I64, scale, line, column);
    value = ir_new_binary_value(ir, IR_BIN_MUL, IR_TYPE_I64, value, factor, line, column);
  }
  *out = value;
  return true;
}

static bool ir_lower_time_duration_div_floor(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *arg, unsigned long long divisor, IrTypeKind result_type, int line, int column, IrValue **out) {
  if (!arg) {
    ir_mark_unsupported(ir, "direct backend time conversion is missing a Duration argument", line, column, "missing argument");
    return false;
  }
  IrValue *duration = NULL;
  if (!ir_lower_expr(program, ir, fun, arg, &duration)) return false;
  duration = ir_new_cast_value(ir, duration, IR_TYPE_I64, line, column);
  IrValue *value = duration;
  if (divisor != 1) {
    IrValue *factor = ir_new_integer_literal_value(ir, IR_TYPE_I64, divisor, line, column);
    value = ir_new_binary_value(ir, IR_BIN_DIV, IR_TYPE_I64, duration, factor, line, column);
  }
  *out = result_type == IR_TYPE_I64 ? value : ir_new_cast_value(ir, value, result_type, line, column);
  return true;
}

static bool ir_std_time_duration_scale(const char *name, unsigned long long *out_scale) {
  static const struct { const char *name; unsigned long long scale; } entries[] = {
    {"std.time.ns", 1ull},
    {"std.time.us", 1000ull},
    {"std.time.ms", 1000000ull},
    {"std.time.seconds", 1000000000ull},
    {"std.time.minutes", 60000000000ull},
    {"std.time.hours", 3600000000000ull},
  };
  for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
    if (strcmp(name, entries[i].name) == 0) {
      *out_scale = entries[i].scale;
      return true;
    }
  }
  return false;
}

static bool ir_std_time_conversion(const char *name, unsigned long long *out_divisor, IrTypeKind *out_type) {
  static const struct { const char *name; unsigned long long divisor; IrTypeKind type; } entries[] = {
    {"std.time.asNs", 1ull, IR_TYPE_I64},
  };
  for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
    if (strcmp(name, entries[i].name) == 0) {
      *out_divisor = entries[i].divisor;
      *out_type = entries[i].type;
      return true;
    }
  }
  return false;
}

static bool ir_std_time_binary_op(const char *name, IrBinaryOp *out_op) {
  if (strcmp(name, "std.time.add") == 0) {
    *out_op = IR_BIN_ADD;
    return true;
  }
  if (strcmp(name, "std.time.sub") == 0) {
    *out_op = IR_BIN_SUB;
    return true;
  }
  return false;
}

static bool ir_is_std_rand_entropy_call(const char *name) {
  static const char *names[] = {
    "std.rand.entropyU32",
    "std.rand.entropySeed",
    "std.crypto.secureRandomU32",
  };
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    if (strcmp(name, names[i]) == 0) return true;
  }
  return false;
}

static bool ir_u8_literal_byte(const Expr *expr, unsigned char *out) {
  if (!expr || !out) return false;
  if (expr->kind != EXPR_NUMBER && expr->kind != EXPR_CHAR) return false;
  unsigned long long parsed = 0;
  if (!ir_parse_integer_literal(expr->text ? expr->text : "0", &parsed) || parsed > 255) return false;
  *out = (unsigned char)parsed;
  return true;
}

static bool ir_lower_array_literal_byte_view(IrProgram *ir, const Expr *expr, IrValue **out) {
  if (!expr || expr->kind != EXPR_ARRAY_LITERAL) return false;
  unsigned array_len = 0;
  IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
  if (!ir_parse_fixed_array_type(expr->resolved_type, &array_len, &element_type) || element_type != IR_TYPE_U8) {
    ir_mark_unsupported(ir, "direct backend inline byte array span requires a fixed [N]u8 literal", expr->line, expr->column, expr->resolved_type ? expr->resolved_type : "unknown array literal");
    return false;
  }
  if (expr->array_repeat) {
    if (expr->args.len != 2) {
      ir_mark_unsupported(ir, "direct backend inline byte array repeat literal requires value and count", expr->line, expr->column, "array literal");
      return false;
    }
  } else if (expr->args.len != array_len) {
    ir_mark_unsupported(ir, "direct backend inline byte array literal length must match resolved type", expr->line, expr->column, expr->resolved_type ? expr->resolved_type : "array literal");
    return false;
  }

  unsigned char *bytes = z_checked_malloc(array_len == 0 ? 1 : array_len);
  if (expr->array_repeat) {
    unsigned char byte = 0;
    if (!ir_u8_literal_byte(expr->args.items[0], &byte)) {
      free(bytes);
      ir_mark_unsupported(ir, "direct backend inline byte array repeat requires a byte literal value", expr->args.items[0] ? expr->args.items[0]->line : expr->line, expr->args.items[0] ? expr->args.items[0]->column : expr->column, "non-byte literal");
      return false;
    }
    memset(bytes, byte, array_len);
  } else {
    for (unsigned i = 0; i < array_len; i++) {
      unsigned char byte = 0;
      if (!ir_u8_literal_byte(expr->args.items[i], &byte)) {
        free(bytes);
        ir_mark_unsupported(ir, "direct backend inline byte array span requires byte literal elements", expr->args.items[i] ? expr->args.items[i]->line : expr->line, expr->args.items[i] ? expr->args.items[i]->column : expr->column, "non-byte literal");
        return false;
      }
      bytes[i] = byte;
    }
  }

  unsigned offset = 0;
  bool added = ir_add_readonly_data(ir, bytes, array_len, expr->line, expr->column, &offset);
  free(bytes);
  if (!added) return false;
  IrValue *value = ir_new_value(ir, IR_VALUE_STRING_LITERAL, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
  value->data_offset = offset;
  value->data_len = array_len;
  value->element_type = IR_TYPE_U8;
  *out = value;
  return true;
}

static bool ir_make_string_literal_value(IrProgram *ir, const char *text, int line, int column, IrValue **out) {
  unsigned offset = 0;
  text = text ? text : "";
  unsigned len = (unsigned)strlen(text);
  unsigned char *nul_terminated = z_checked_malloc((size_t)len + 1);
  memcpy(nul_terminated, text, len);
  nul_terminated[len] = 0;
  bool added = ir_add_readonly_data(ir, nul_terminated, len + 1, line, column, &offset);
  free(nul_terminated);
  if (!added) return false;
  IrValue *value = ir_new_value(ir, IR_VALUE_STRING_LITERAL, IR_TYPE_BYTE_VIEW, line, column);
  value->data_offset = offset;
  value->data_len = len;
  value->element_type = IR_TYPE_U8;
  *out = value;
  return true;
}

static bool ir_make_json_error_label_value(IrProgram *ir, IrValue *code, bool expected, int line, int column, IrValue **out) {
  IrValue *value = ir_new_value(ir, IR_VALUE_JSON_ERROR_LABEL, IR_TYPE_BYTE_VIEW, line, column);
  value->left = code;
  value->int_value = expected ? 1u : 0u;
  value->element_type = IR_TYPE_U8;
  for (unsigned i = 0; i < 4; i++) {
    const char *label = NULL;
    if (i < 3) label = ir_std_json_error_label(i, expected);
    else label = "unknown";
    IrValue *literal = NULL;
    if (!ir_make_string_literal_value(ir, label, line, column, &literal)) {
      ir_free_value(value);
      return false;
    }
    ir_value_push_arg(ir, value, literal);
  }
  *out = value;
  return true;
}

static bool ir_lower_string_literal_byte_view(IrProgram *ir, const Expr *expr, IrValue **out) {
  return ir_make_string_literal_value(ir, expr && expr->text ? expr->text : "", expr ? expr->line : 1, expr ? expr->column : 1, out);
}

static bool ir_lower_record_array_field_byte_view(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *expr, IrValue **out) {
  if (!expr || expr->kind != EXPR_MEMBER || !expr->left || expr->left->kind != EXPR_IDENT) return false;
  const IrLocal *record = ir_function_find_local(fun, expr->left->text);
  unsigned field_offset = 0;
  bool field_is_array = false;
  unsigned field_array_len = 0;
  IrTypeKind field_element_type = IR_TYPE_UNSUPPORTED;
  if (!record || !record->is_record ||
      !ir_shape_field_storage_info(program, record->shape_name, expr->text, &field_offset, NULL, &field_is_array, &field_array_len, &field_element_type) ||
      !field_is_array) {
    return false;
  }
  unsigned element_size = ir_type_byte_size(field_element_type);
  if (!(ir_type_is_value(field_element_type) || field_element_type == IR_TYPE_BOOL) ||
      element_size == 0 ||
      field_array_len > UINT_MAX / element_size ||
      field_offset > record->byte_size ||
      field_array_len * element_size > record->byte_size - field_offset) {
    ir_mark_unsupported(ir, "direct backend record array field byte view has unsupported storage", expr->line, expr->column, expr->text);
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
  value->array_index = record->index;
  value->field_offset = field_offset;
  value->data_len = field_array_len;
  value->element_type = field_element_type;
  *out = value;
  return true;
}

static bool ir_lower_byte_view(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *expr, IrValue **out) {
  if (!expr) {
    ir_mark_unsupported(ir, "direct backend byte view is missing", 1, 1, "missing expression");
    return false;
  }
  if (expr->kind == EXPR_STRING) return ir_lower_string_literal_byte_view(ir, expr, out);
  if (expr->kind == EXPR_ARRAY_LITERAL) {
    return ir_lower_array_literal_byte_view(ir, expr, out);
  }
  if (expr->kind == EXPR_CALL && expr->args.len == 1) {
    char *callee = ir_expr_callee_name(expr->left);
    bool member_span = expr->left && expr->left->kind == EXPR_MEMBER && strcmp(expr->left->text ? expr->left->text : "", "span") == 0;
    bool member_buf_bytes = expr->left && expr->left->kind == EXPR_MEMBER && strcmp(expr->left->text ? expr->left->text : "", "bufBytes") == 0;
    bool is_span = (callee && strcmp(callee, "std.mem.span") == 0) || member_span;
    bool is_buf_bytes = (callee && strcmp(callee, "std.mem.bufBytes") == 0) || member_buf_bytes;
    bool is_io_buffer = callee && (strcmp(callee, "std.io.bufferedReader") == 0 || strcmp(callee, "std.io.bufferedWriter") == 0);
    free(callee);
    if (is_span || is_io_buffer) return ir_lower_byte_view(program, ir, fun, expr->args.items[0], out);
    if (is_buf_bytes) {
      const Expr *arg = expr->args.items[0];
      if (arg && arg->kind == EXPR_BORROW) arg = arg->left;
      if (arg && arg->kind == EXPR_IDENT) {
        const IrLocal *buf = ir_function_find_local(fun, arg->text);
        if (buf && buf->type == IR_TYPE_BYTE_VIEW) {
          IrValue *value = ir_new_value(ir, IR_VALUE_LOCAL, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
          value->local_index = buf->index;
          value->element_type = buf->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : buf->element_type;
          *out = value;
          return true;
        }
      }
    }
  }
  if (expr->kind == EXPR_CALL && expr->resolved_type && ir_type_kind(expr->resolved_type) == IR_TYPE_BYTE_VIEW) {
    return ir_lower_expr(program, ir, fun, expr, out);
  }
  if (expr->kind == EXPR_BORROW) {
    return ir_lower_byte_view(program, ir, fun, expr->left, out);
  }
  if (expr->kind == EXPR_IDENT) {
    const IrLocal *local = ir_function_find_local(fun, expr->text);
    if (local && local->type == IR_TYPE_BYTE_VIEW) {
      IrValue *value = ir_new_value(ir, IR_VALUE_LOCAL, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
      value->local_index = local->index;
      value->element_type = local->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : local->element_type;
      *out = value;
      return true;
    }
    if (local && local->type == IR_TYPE_MAYBE_BYTE_VIEW) {
      IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_VALUE, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
      value->local_index = local->index;
      value->element_type = IR_TYPE_U8;
      *out = value;
      return true;
    }
    if (local && local->is_array && (ir_type_is_value(local->element_type) || local->element_type == IR_TYPE_BOOL)) {
      IrValue *value = ir_new_value(ir, IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
      value->array_index = local->index;
      value->data_len = local->array_len;
      value->element_type = local->element_type;
      *out = value;
      return true;
    }
    if (!local || local->type != IR_TYPE_BYTE_VIEW) {
      ir_mark_unsupported(ir, "direct backend byte view identifier is not a byte-view local", expr->line, expr->column, expr->text);
      return false;
    }
  }
  if (ir_lower_record_array_field_byte_view(program, ir, fun, expr, out)) return true;
  if (expr->kind == EXPR_MEMBER && expr->left && expr->left->kind == EXPR_IDENT && strcmp(expr->text ? expr->text : "", "value") == 0) {
    const IrLocal *local = ir_function_find_local(fun, expr->left->text);
    if (!local || local->type != IR_TYPE_MAYBE_BYTE_VIEW) {
      ir_mark_unsupported(ir, "direct backend maybe byte-view value requires a Maybe byte-view local", expr->line, expr->column, expr->left->text);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_VALUE, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
    value->local_index = local->index;
    value->element_type = IR_TYPE_U8;
    *out = value;
    return true;
  }
  if (expr->kind == EXPR_SLICE) {
    if (!ir_expr_is_byte_view_source(expr->left)) {
      ir_mark_unsupported(ir, "direct backend slicing currently supports only string literal byte views", expr->line, expr->column, "non-string slice base");
      return false;
    }
    IrValue *base = NULL;
    if (!ir_lower_byte_view(program, ir, fun, expr->left, &base)) return false;
    IrValue *start = NULL;
    IrValue *end = NULL;
    if (expr->args.len > 0 && expr->args.items[0] &&
        !ir_lower_expr(program, ir, fun, expr->args.items[0], &start)) {
      ir_free_value(base);
      return false;
    }
    if (expr->args.len > 1 && expr->args.items[1] &&
        !ir_lower_expr(program, ir, fun, expr->args.items[1], &end)) {
      ir_free_value(base);
      ir_free_value(start);
      return false;
    }
    if ((start && !ir_type_is_value(start->type)) || (end && !ir_type_is_value(end->type))) {
      ir_free_value(base);
      ir_free_value(start);
      ir_free_value(end);
      ir_mark_unsupported(ir, "direct backend slice bounds must be integer values", expr->line, expr->column, "non-integer slice bound");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_SLICE, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
    value->left = base;
    value->index = start;
    value->right = end;
    value->element_type = base->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : base->element_type;
    *out = value;
    return true;
  }
  ir_mark_unsupported(ir, "direct backend byte views currently support string literals and slices", expr->line, expr->column, "unsupported byte view source");
  return false;
}

static bool ir_lower_call_arg(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *expr, IrTypeKind expected, IrValue **out) {
  return expected == IR_TYPE_BYTE_VIEW ? ir_lower_byte_view(program, ir, fun, expr, out) : ir_lower_expr(program, ir, fun, expr, out);
}

static void ir_require_runtime_helper(IrProgram *ir) {
  if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
  if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
}

static void ir_require_helper_counts(IrProgram *ir, unsigned runtime_count, unsigned host_count) {
  if (ir->direct_runtime_helper_count < runtime_count) ir->direct_runtime_helper_count = runtime_count;
  if (ir->direct_host_runtime_import_count < host_count) ir->direct_host_runtime_import_count = host_count;
}

static bool ir_lower_std_str_arg(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, size_t index, IrTypeKind expected, IrValue **out) {
  if (!call || index >= call->args.len) {
    ir_mark_unsupported(ir, "direct backend std.str helper argument is missing", call ? call->line : 1, call ? call->column : 1, "missing std.str argument");
    return false;
  }
  const Expr *arg_expr = call->args.items[index];
  if (!ir_lower_call_arg(program, ir, fun, arg_expr, expected, out)) return false;
  if (expected == IR_TYPE_BYTE_VIEW) {
    if (!*out || (*out)->type != IR_TYPE_BYTE_VIEW) {
      ir_free_value(*out);
      *out = NULL;
      ir_mark_unsupported(ir, "direct backend std.str argument must be a byte view", arg_expr ? arg_expr->line : call->line, arg_expr ? arg_expr->column : call->column, "non-byte-view argument");
      return false;
    }
    return true;
  }
  if (*out && (*out)->type == expected) return true;
  ir_free_value(*out);
  *out = NULL;
  ir_mark_unsupported(ir, "direct backend std.str argument type does not match helper", arg_expr ? arg_expr->line : call->line, arg_expr ? arg_expr->column : call->column, arg_expr && arg_expr->resolved_type ? arg_expr->resolved_type : "unknown argument type");
  return false;
}

static bool ir_make_std_str_runtime_value(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrStrOp op, IrTypeKind return_type, const IrTypeKind *arg_types, size_t arg_count, bool first_arg_mutable_buffer, IrValue **out) {
  if (!call || call->args.len != arg_count) {
    ir_mark_unsupported(ir, "direct backend std.str helper argument count does not match signature", call ? call->line : 1, call ? call->column : 1, "wrong std.str arity");
    return false;
  }
  if (first_arg_mutable_buffer && !ir_expr_is_mutable_byte_view_dest(program, fun, call->args.items[0])) {
    ir_mark_unsupported(ir, "direct backend std.str helper expects a mutable byte destination", call->args.items[0] ? call->args.items[0]->line : call->line, call->args.items[0] ? call->args.items[0]->column : call->column, "non-mutable byte destination");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_STR_RUNTIME, return_type, call->line, call->column);
  value->int_value = (unsigned long long)op;
  if (return_type == IR_TYPE_BYTE_VIEW || return_type == IR_TYPE_MAYBE_BYTE_VIEW) value->element_type = IR_TYPE_U8;
  for (size_t i = 0; i < arg_count; i++) {
    IrValue *arg = NULL;
    if (!ir_lower_std_str_arg(program, ir, fun, call, i, arg_types[i], &arg)) {
      ir_free_value(value);
      return false;
    }
    ir_value_push_arg(ir, value, arg);
  }
  ir_require_runtime_helper(ir);
  *out = value;
  return true;
}

typedef struct {
  const char *name;
  IrStrOp op;
  IrTypeKind return_type;
  const IrTypeKind *arg_types;
  size_t arg_count;
  bool first_arg_mutable_buffer;
} IrStdStrSpec;

static const IrStdStrSpec *ir_std_str_spec(const char *callee_name) {
  static const IrTypeKind one_view[] = {IR_TYPE_BYTE_VIEW};
  static const IrTypeKind two_views[] = {IR_TYPE_BYTE_VIEW, IR_TYPE_BYTE_VIEW};
  static const IrTypeKind view_byte[] = {IR_TYPE_BYTE_VIEW, IR_TYPE_U8};
  static const IrTypeKind three_views[] = {IR_TYPE_BYTE_VIEW, IR_TYPE_BYTE_VIEW, IR_TYPE_BYTE_VIEW};
  static const IrTypeKind view_view_count[] = {IR_TYPE_BYTE_VIEW, IR_TYPE_BYTE_VIEW, IR_TYPE_USIZE};
  static const IrStdStrSpec specs[] = {
    {"std.str.reverse", IR_STR_OP_REVERSE, IR_TYPE_MAYBE_BYTE_VIEW, two_views, 2, true},
    {"std.str.copy", IR_STR_OP_COPY, IR_TYPE_MAYBE_BYTE_VIEW, two_views, 2, true},
    {"std.str.concat", IR_STR_OP_CONCAT, IR_TYPE_MAYBE_BYTE_VIEW, three_views, 3, true},
    {"std.str.repeat", IR_STR_OP_REPEAT, IR_TYPE_MAYBE_BYTE_VIEW, view_view_count, 3, true},
    {"std.str.toLowerAscii", IR_STR_OP_TO_LOWER_ASCII, IR_TYPE_MAYBE_BYTE_VIEW, two_views, 2, true},
    {"std.str.toUpperAscii", IR_STR_OP_TO_UPPER_ASCII, IR_TYPE_MAYBE_BYTE_VIEW, two_views, 2, true},
    {"std.str.trimAscii", IR_STR_OP_TRIM_ASCII, IR_TYPE_BYTE_VIEW, one_view, 1, false},
    {"std.str.trimStartAscii", IR_STR_OP_TRIM_START_ASCII, IR_TYPE_BYTE_VIEW, one_view, 1, false},
    {"std.str.trimEndAscii", IR_STR_OP_TRIM_END_ASCII, IR_TYPE_BYTE_VIEW, one_view, 1, false},
    {"std.str.countByte", IR_STR_OP_COUNT_BYTE, IR_TYPE_USIZE, view_byte, 2, false},
    {"std.str.startsWith", IR_STR_OP_STARTS_WITH, IR_TYPE_BOOL, two_views, 2, false},
    {"std.str.endsWith", IR_STR_OP_ENDS_WITH, IR_TYPE_BOOL, two_views, 2, false},
    {"std.str.contains", IR_STR_OP_CONTAINS, IR_TYPE_BOOL, two_views, 2, false},
    {"std.str.count", IR_STR_OP_COUNT, IR_TYPE_USIZE, two_views, 2, false},
    {"std.str.indexOf", IR_STR_OP_INDEX_OF, IR_TYPE_USIZE, two_views, 2, false},
    {"std.str.lastIndexOf", IR_STR_OP_LAST_INDEX_OF, IR_TYPE_USIZE, two_views, 2, false},
    {"std.str.eqlIgnoreAsciiCase", IR_STR_OP_EQL_IGNORE_ASCII_CASE, IR_TYPE_BOOL, two_views, 2, false},
    {"std.str.wordCountAscii", IR_STR_OP_WORD_COUNT_ASCII, IR_TYPE_USIZE, one_view, 1, false},
    {"std.path.basename", IR_STR_OP_PATH_BASENAME, IR_TYPE_BYTE_VIEW, one_view, 1, false},
    {"std.path.dirname", IR_STR_OP_PATH_DIRNAME, IR_TYPE_BYTE_VIEW, one_view, 1, false},
    {"std.path.extension", IR_STR_OP_PATH_EXTENSION, IR_TYPE_BYTE_VIEW, one_view, 1, false},
    {"std.crypto.sha256", IR_STR_OP_CRYPTO_SHA256, IR_TYPE_MAYBE_BYTE_VIEW, two_views, 2, true},
    {"std.crypto.sha256Hex", IR_STR_OP_CRYPTO_SHA256_HEX, IR_TYPE_MAYBE_BYTE_VIEW, two_views, 2, true},
    {"std.crypto.hmacSha256", IR_STR_OP_CRYPTO_HMAC_SHA256, IR_TYPE_MAYBE_BYTE_VIEW, three_views, 3, true},
    {"std.crypto.hmacSha256Hex", IR_STR_OP_CRYPTO_HMAC_SHA256_HEX, IR_TYPE_MAYBE_BYTE_VIEW, three_views, 3, true},
  };
  for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
    if (strcmp(callee_name, specs[i].name) == 0) return &specs[i];
  }
  return NULL;
}

static bool ir_lower_std_str_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, const char *callee_name, bool *handled, IrValue **out) {
  const IrStdStrSpec *spec = ir_std_str_spec(callee_name);
  if (!spec) {
    *handled = false;
    return true;
  }
  *handled = true;
  return ir_make_std_str_runtime_value(program, ir, fun, call, spec->op, spec->return_type, spec->arg_types, spec->arg_count, spec->first_arg_mutable_buffer, out);
}

typedef struct {
  const char *name;
  IrAsciiOp op;
  IrTypeKind return_type;
} IrStdAsciiSpec;

static const IrStdAsciiSpec *ir_std_ascii_spec(const char *callee_name) {
  static const IrStdAsciiSpec specs[] = {
    {"std.ascii.isDigit", IR_ASCII_OP_IS_DIGIT, IR_TYPE_BOOL},
    {"std.ascii.isLower", IR_ASCII_OP_IS_LOWER, IR_TYPE_BOOL},
    {"std.ascii.isUpper", IR_ASCII_OP_IS_UPPER, IR_TYPE_BOOL},
    {"std.ascii.isAlpha", IR_ASCII_OP_IS_ALPHA, IR_TYPE_BOOL},
    {"std.ascii.isAlnum", IR_ASCII_OP_IS_ALNUM, IR_TYPE_BOOL},
    {"std.ascii.isWhitespace", IR_ASCII_OP_IS_WHITESPACE, IR_TYPE_BOOL},
    {"std.ascii.isHexDigit", IR_ASCII_OP_IS_HEX_DIGIT, IR_TYPE_BOOL},
    {"std.ascii.toLower", IR_ASCII_OP_TO_LOWER, IR_TYPE_U8},
    {"std.ascii.toUpper", IR_ASCII_OP_TO_UPPER, IR_TYPE_U8},
    {"std.ascii.digitValue", IR_ASCII_OP_DIGIT_VALUE, IR_TYPE_MAYBE_SCALAR},
    {"std.ascii.hexValue", IR_ASCII_OP_HEX_VALUE, IR_TYPE_MAYBE_SCALAR},
  };
  for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
    if (strcmp(callee_name, specs[i].name) == 0) return &specs[i];
  }
  return NULL;
}

typedef struct {
  const char *name;
  IrTextOp op;
  IrTypeKind return_type;
} IrStdTextSpec;

static const IrStdTextSpec *ir_std_text_spec(const char *callee_name) {
  static const IrStdTextSpec specs[] = {
    {"std.text.isAscii", IR_TEXT_OP_IS_ASCII, IR_TYPE_BOOL},
    {"std.text.utf8Valid", IR_TEXT_OP_UTF8_VALID, IR_TYPE_BOOL},
    {"std.codec.utf8Valid", IR_TEXT_OP_UTF8_VALID, IR_TYPE_BOOL},
    {"std.text.utf8Len", IR_TEXT_OP_UTF8_LEN, IR_TYPE_MAYBE_SCALAR},
  };
  for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
    if (strcmp(callee_name, specs[i].name) == 0) return &specs[i];
  }
  return NULL;
}

typedef struct {
  const char *name;
  IrParseOp op;
  IrTypeKind return_type;
  IrTypeKind element_type;
  size_t expected_args;
} IrStdParseSpec;

static const IrStdParseSpec *ir_std_parse_spec(const char *callee_name) {
  static const IrStdParseSpec specs[] = {
    {"std.parse.isAsciiDigit", IR_PARSE_OP_IS_ASCII_DIGIT, IR_TYPE_BOOL, IR_TYPE_UNSUPPORTED, 1},
    {"std.parse.isAsciiAlpha", IR_PARSE_OP_IS_ASCII_ALPHA, IR_TYPE_BOOL, IR_TYPE_UNSUPPORTED, 1},
    {"std.parse.isIdentifierStart", IR_PARSE_OP_IS_IDENTIFIER_START, IR_TYPE_BOOL, IR_TYPE_UNSUPPORTED, 1},
    {"std.parse.isWhitespace", IR_PARSE_OP_IS_WHITESPACE, IR_TYPE_BOOL, IR_TYPE_UNSUPPORTED, 1},
    {"std.parse.scanDigits", IR_PARSE_OP_SCAN_DIGITS, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, 1},
    {"std.parse.scanIdentifier", IR_PARSE_OP_SCAN_IDENTIFIER, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, 1},
    {"std.parse.scanUntilByte", IR_PARSE_OP_SCAN_UNTIL_BYTE, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, 2},
    {"std.parse.scanWhitespace", IR_PARSE_OP_SCAN_WHITESPACE, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, 1},
    {"std.parse.parseBool", IR_PARSE_OP_PARSE_BOOL, IR_TYPE_MAYBE_SCALAR, IR_TYPE_BOOL, 1},
    {"std.parse.parseU8", IR_PARSE_OP_PARSE_U8, IR_TYPE_MAYBE_SCALAR, IR_TYPE_U8, 1},
    {"std.parse.parseU16", IR_PARSE_OP_PARSE_U16, IR_TYPE_MAYBE_SCALAR, IR_TYPE_U16, 1},
    {"std.parse.parseUsize", IR_PARSE_OP_PARSE_USIZE, IR_TYPE_MAYBE_SCALAR, IR_TYPE_USIZE, 1},
    {"std.term.keyCode", IR_PARSE_OP_TERM_KEY_CODE, IR_TYPE_U32, IR_TYPE_UNSUPPORTED, 1},
    {"std.term.keyByteLen", IR_PARSE_OP_TERM_KEY_BYTE_LEN, IR_TYPE_USIZE, IR_TYPE_UNSUPPORTED, 1},
  };
  for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
    if (strcmp(callee_name, specs[i].name) == 0) return &specs[i];
  }
  return NULL;
}

static bool ir_make_std_ascii_runtime_value(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrAsciiOp op, IrTypeKind return_type, IrValue **out) {
  if (!call || call->args.len != 1) {
    ir_mark_unsupported(ir, "direct backend std.ascii helper expects one byte argument", call ? call->line : 1, call ? call->column : 1, "wrong std.ascii arity");
    return false;
  }
  IrValue *arg = NULL;
  if (!ir_lower_call_arg(program, ir, fun, call->args.items[0], IR_TYPE_U8, &arg)) return false;
  if (!arg || arg->type != IR_TYPE_U8) {
    ir_free_value(arg);
    ir_mark_unsupported(ir, "direct backend std.ascii helper argument must be u8", call->args.items[0] ? call->args.items[0]->line : call->line, call->args.items[0] ? call->args.items[0]->column : call->column, call->args.items[0] && call->args.items[0]->resolved_type ? call->args.items[0]->resolved_type : "non-u8 argument");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_ASCII_RUNTIME, return_type, call->line, call->column);
  value->int_value = (unsigned long long)op;
  if (return_type == IR_TYPE_MAYBE_SCALAR) value->element_type = IR_TYPE_U8;
  ir_value_push_arg(ir, value, arg);
  ir_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_lower_std_ascii_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, const char *callee_name, bool *handled, IrValue **out) {
  const IrStdAsciiSpec *spec = ir_std_ascii_spec(callee_name);
  if (!spec) {
    *handled = false;
    return true;
  }
  *handled = true;
  return ir_make_std_ascii_runtime_value(program, ir, fun, call, spec->op, spec->return_type, out);
}

static bool ir_make_std_text_runtime_value(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrTextOp op, IrTypeKind return_type, IrValue **out) {
  if (!call || call->args.len != 1) {
    ir_mark_unsupported(ir, "direct backend std.text helper expects one byte-view argument", call ? call->line : 1, call ? call->column : 1, "wrong std.text arity");
    return false;
  }
  IrValue *arg = NULL;
  if (!ir_lower_byte_view(program, ir, fun, call->args.items[0], &arg)) return false;
  if (!arg || arg->type != IR_TYPE_BYTE_VIEW) {
    ir_free_value(arg);
    ir_mark_unsupported(ir, "direct backend std.text helper argument must be a byte view", call->args.items[0] ? call->args.items[0]->line : call->line, call->args.items[0] ? call->args.items[0]->column : call->column, "non-byte-view argument");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_TEXT_RUNTIME, return_type, call->line, call->column);
  value->int_value = (unsigned long long)op;
  if (return_type == IR_TYPE_MAYBE_SCALAR) value->element_type = IR_TYPE_USIZE;
  ir_value_push_arg(ir, value, arg);
  ir_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_lower_std_text_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, const char *callee_name, bool *handled, IrValue **out) {
  const IrStdTextSpec *spec = ir_std_text_spec(callee_name);
  if (!spec) {
    *handled = false;
    return true;
  }
  *handled = true;
  return ir_make_std_text_runtime_value(program, ir, fun, call, spec->op, spec->return_type, out);
}

static bool ir_make_std_parse_runtime_value(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrParseOp op, IrTypeKind return_type, IrTypeKind element_type, size_t expected_args, IrValue **out) {
  if (!call || call->args.len != expected_args || expected_args < 1 || expected_args > 2) {
    ir_mark_unsupported(ir, "direct backend std.parse helper has unsupported arity", call ? call->line : 1, call ? call->column : 1, "wrong std.parse arity");
    return false;
  }
  IrValue *input = NULL;
  if (!ir_lower_byte_view(program, ir, fun, call->args.items[0], &input)) return false;
  IrValue *value = ir_new_value(ir, IR_VALUE_PARSE_RUNTIME, return_type, call->line, call->column);
  value->int_value = (unsigned long long)op;
  if (return_type == IR_TYPE_MAYBE_SCALAR) value->element_type = element_type;
  ir_value_push_arg(ir, value, input);
  if (expected_args == 2) {
    IrValue *byte = NULL;
    if (!ir_lower_call_arg(program, ir, fun, call->args.items[1], IR_TYPE_U8, &byte)) {
      ir_free_value(value);
      return false;
    }
    if (!byte || byte->type != IR_TYPE_U8) {
      ir_free_value(byte);
      ir_free_value(value);
      ir_mark_unsupported(ir, "direct backend std.parse byte argument must be u8", call->args.items[1] ? call->args.items[1]->line : call->line, call->args.items[1] ? call->args.items[1]->column : call->column, "non-u8 argument");
      return false;
    }
    ir_value_push_arg(ir, value, byte);
  }
  ir_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_lower_std_parse_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, const char *callee_name, bool *handled, IrValue **out) {
  const IrStdParseSpec *spec = ir_std_parse_spec(callee_name);
  if (spec) {
    *handled = true;
    return ir_make_std_parse_runtime_value(program, ir, fun, call, spec->op, spec->return_type, spec->element_type, spec->expected_args, out);
  }
  if (strcmp(callee_name, "std.parse.tokenAscii") == 0) {
    const IrTypeKind one_view[] = {IR_TYPE_BYTE_VIEW};
    *handled = true;
    return ir_make_std_str_runtime_value(program, ir, fun, call, IR_STR_OP_PARSE_TOKEN_ASCII, IR_TYPE_BYTE_VIEW, one_view, 1, false, out);
  }
  *handled = false;
  return true;
}

static bool ir_make_std_time_runtime_value(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrTimeOp op, IrTypeKind return_type, size_t expected_args, IrValue **out) {
  if (!call || call->args.len != expected_args) {
    ir_mark_unsupported(ir, "direct backend std.time helper has unsupported arity", call ? call->line : 1, call ? call->column : 1, "wrong std.time arity");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_TIME_RUNTIME, return_type, call->line, call->column);
  value->int_value = (unsigned long long)op;
  for (size_t i = 0; i < expected_args; i++) {
    IrValue *arg = NULL;
    if (!ir_lower_call_arg(program, ir, fun, call->args.items[i], IR_TYPE_I64, &arg)) {
      ir_free_value(value);
      return false;
    }
    if (!arg || !ir_type_is_value(arg->type)) {
      ir_free_value(arg);
      ir_free_value(value);
      ir_mark_unsupported(ir, "direct backend std.time helper argument must be a Duration", call->args.items[i] ? call->args.items[i]->line : call->line, call->args.items[i] ? call->args.items[i]->column : call->column, "non-Duration argument");
      return false;
    }
    ir_value_push_arg(ir, value, ir_new_cast_value(ir, arg, IR_TYPE_I64, call->line, call->column));
  }
  ir_require_runtime_helper(ir);
  *out = value;
  return true;
}

typedef enum {
  IR_STD_TIME_EXTRA_RUNTIME,
  IR_STD_TIME_EXTRA_ZERO,
  IR_STD_TIME_EXTRA_IS_ZERO,
} IrStdTimeExtraKind;

typedef struct {
  const char *name;
  IrStdTimeExtraKind kind;
  IrTimeOp op;
  IrTypeKind return_type;
  size_t expected_args;
} IrStdTimeExtraSpec;

static const IrStdTimeExtraSpec *ir_std_time_extra_spec(const char *callee_name) {
  static const IrStdTimeExtraSpec specs[] = {
    {"std.time.zero", IR_STD_TIME_EXTRA_ZERO, IR_TIME_OP_AS_US_FLOOR, IR_TYPE_I64, 0},
    {"std.time.asUsFloor", IR_STD_TIME_EXTRA_RUNTIME, IR_TIME_OP_AS_US_FLOOR, IR_TYPE_I64, 1},
    {"std.time.asMsFloor", IR_STD_TIME_EXTRA_RUNTIME, IR_TIME_OP_AS_MS_FLOOR, IR_TYPE_I32, 1},
    {"std.time.asSecondsFloor", IR_STD_TIME_EXTRA_RUNTIME, IR_TIME_OP_AS_SECONDS_FLOOR, IR_TYPE_I64, 1},
    {"std.time.min", IR_STD_TIME_EXTRA_RUNTIME, IR_TIME_OP_MIN, IR_TYPE_I64, 2},
    {"std.time.max", IR_STD_TIME_EXTRA_RUNTIME, IR_TIME_OP_MAX, IR_TYPE_I64, 2},
    {"std.time.clamp", IR_STD_TIME_EXTRA_RUNTIME, IR_TIME_OP_CLAMP, IR_TYPE_I64, 3},
    {"std.time.sleep", IR_STD_TIME_EXTRA_RUNTIME, IR_TIME_OP_SLEEP, IR_TYPE_BOOL, 1},
    {"std.time.wallSeconds", IR_STD_TIME_EXTRA_RUNTIME, IR_TIME_OP_WALL_SECONDS, IR_TYPE_I64, 0},
    {"std.time.monotonic", IR_STD_TIME_EXTRA_RUNTIME, IR_TIME_OP_MONOTONIC, IR_TYPE_I64, 0},
    {"std.time.isZero", IR_STD_TIME_EXTRA_IS_ZERO, IR_TIME_OP_AS_US_FLOOR, IR_TYPE_BOOL, 1},
  };
  for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
    if (strcmp(callee_name, specs[i].name) == 0) return &specs[i];
  }
  return NULL;
}

static bool ir_lower_std_time_extra_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, const char *callee_name, bool *handled, IrValue **out) {
  const IrStdTimeExtraSpec *spec = ir_std_time_extra_spec(callee_name);
  if (!spec) {
    *handled = false;
    return true;
  }
  *handled = true;
  if (spec->kind == IR_STD_TIME_EXTRA_ZERO && call->args.len == 0) {
    *out = ir_new_integer_literal_value(ir, IR_TYPE_I64, 0, call->line, call->column);
    return true;
  }
  if (spec->kind == IR_STD_TIME_EXTRA_RUNTIME) {
    return ir_make_std_time_runtime_value(program, ir, fun, call, spec->op, spec->return_type, spec->expected_args, out);
  }
  if (spec->kind == IR_STD_TIME_EXTRA_IS_ZERO && call->args.len == 1) {
    IrValue *arg = NULL;
    if (!ir_lower_call_arg(program, ir, fun, call->args.items[0], IR_TYPE_I64, &arg)) return false;
    arg = ir_new_cast_value(ir, arg, IR_TYPE_I64, call->line, call->column);
    *out = ir_new_compare_value(ir, IR_CMP_EQ, arg, ir_new_integer_literal_value(ir, IR_TYPE_I64, 0, call->line, call->column), call->line, call->column);
    return true;
  }
  ir_mark_unsupported(ir, "direct backend std.time helper has unsupported arity", call ? call->line : 1, call ? call->column : 1, "wrong std.time arity");
  return false;
}

static bool ir_make_std_term_runtime_value(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrTermOp op, IrTypeKind return_type, size_t expected_args, IrValue **out) {
  if (!call || call->args.len != expected_args || expected_args > 1) {
    ir_mark_unsupported(ir, "direct backend std.term helper has unsupported arity", call ? call->line : 1, call ? call->column : 1, "wrong std.term arity");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_TERM_RUNTIME, return_type, call->line, call->column);
  value->int_value = (unsigned long long)op;
  if (op == IR_TERM_OP_READ_INPUT) {
    IrValue *buffer = NULL;
    if (!ir_lower_byte_view(program, ir, fun, call->args.items[0], &buffer)) {
      ir_free_value(value);
      return false;
    }
    value->left = buffer;
    value->element_type = IR_TYPE_USIZE;
  } else if (expected_args == 1) {
    IrValue *fallback = NULL;
    if (!ir_lower_call_arg(program, ir, fun, call->args.items[0], IR_TYPE_USIZE, &fallback)) {
      ir_free_value(value);
      return false;
    }
    if (!fallback || fallback->type != IR_TYPE_USIZE) {
      ir_free_value(fallback);
      ir_free_value(value);
      ir_mark_unsupported(ir, "direct backend std.term fallback argument must be usize", call->args.items[0] ? call->args.items[0]->line : call->line, call->args.items[0] ? call->args.items[0]->column : call->column, "non-usize argument");
      return false;
    }
    ir_value_push_arg(ir, value, fallback);
  }
  ir_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_lower_std_term_runtime_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, const char *callee_name, bool *handled, IrValue **out) {
  IrStdTermHelper helper = ir_std_term_helper(callee_name);
  if (!helper.has_runtime) {
    *handled = false;
    return true;
  }
  *handled = true;
  return ir_make_std_term_runtime_value(program, ir, fun, call, helper.term_op, helper.runtime_type, helper.runtime_args, out);
}

static bool ir_std_math_runtime_spec(const char *callee_name, size_t arg_count, IrMathOp *op, IrTypeKind *arg_type, IrTypeKind *return_type, IrTypeKind *return_element_type, size_t *expected_args) {
  if (!callee_name || !op || !arg_type || !return_type || !return_element_type || !expected_args) return false;
  *expected_args = 2;
  *return_element_type = IR_TYPE_UNSUPPORTED;
  if (strcmp(callee_name, "std.math.minI32") == 0) { *op = IR_MATH_OP_MIN_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_I32; }
  else if (strcmp(callee_name, "std.math.maxI32") == 0) { *op = IR_MATH_OP_MAX_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_I32; }
  else if (strcmp(callee_name, "std.math.clampI32") == 0) { *op = IR_MATH_OP_CLAMP_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_I32; *expected_args = 3; }
  else if (strcmp(callee_name, "std.math.minI64") == 0) { *op = IR_MATH_OP_MIN_I64; *arg_type = IR_TYPE_I64; *return_type = IR_TYPE_I64; }
  else if (strcmp(callee_name, "std.math.maxI64") == 0) { *op = IR_MATH_OP_MAX_I64; *arg_type = IR_TYPE_I64; *return_type = IR_TYPE_I64; }
  else if (strcmp(callee_name, "std.math.clampI64") == 0) { *op = IR_MATH_OP_CLAMP_I64; *arg_type = IR_TYPE_I64; *return_type = IR_TYPE_I64; *expected_args = 3; }
  else if (strcmp(callee_name, "std.math.minU32") == 0) { *op = IR_MATH_OP_MIN_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (strcmp(callee_name, "std.math.maxU32") == 0) { *op = IR_MATH_OP_MAX_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (strcmp(callee_name, "std.math.clampU32") == 0) { *op = IR_MATH_OP_CLAMP_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; *expected_args = 3; }
  else if (strcmp(callee_name, "std.math.minU64") == 0) { *op = IR_MATH_OP_MIN_U64; *arg_type = IR_TYPE_U64; *return_type = IR_TYPE_U64; }
  else if (strcmp(callee_name, "std.math.maxU64") == 0) { *op = IR_MATH_OP_MAX_U64; *arg_type = IR_TYPE_U64; *return_type = IR_TYPE_U64; }
  else if (strcmp(callee_name, "std.math.clampU64") == 0) { *op = IR_MATH_OP_CLAMP_U64; *arg_type = IR_TYPE_U64; *return_type = IR_TYPE_U64; *expected_args = 3; }
  else if (strcmp(callee_name, "std.math.minUsize") == 0) { *op = IR_MATH_OP_MIN_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_USIZE; }
  else if (strcmp(callee_name, "std.math.maxUsize") == 0) { *op = IR_MATH_OP_MAX_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_USIZE; }
  else if (strcmp(callee_name, "std.math.clampUsize") == 0) { *op = IR_MATH_OP_CLAMP_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_USIZE; *expected_args = 3; }
  else if (strcmp(callee_name, "std.math.absI32") == 0) { *op = IR_MATH_OP_ABS_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_U32; *expected_args = 1; }
  else if (strcmp(callee_name, "std.math.absI64") == 0) { *op = IR_MATH_OP_ABS_I64; *arg_type = IR_TYPE_I64; *return_type = IR_TYPE_U64; *expected_args = 1; }
  else if (strcmp(callee_name, "std.math.checkedAddU32") == 0) { *op = IR_MATH_OP_CHECKED_ADD_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_U32; }
  else if (strcmp(callee_name, "std.math.checkedSubU32") == 0) { *op = IR_MATH_OP_CHECKED_SUB_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_U32; }
  else if (strcmp(callee_name, "std.math.checkedMulU32") == 0) { *op = IR_MATH_OP_CHECKED_MUL_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_U32; }
  else if (strcmp(callee_name, "std.math.saturatingAddU32") == 0) { *op = IR_MATH_OP_SATURATING_ADD_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (strcmp(callee_name, "std.math.saturatingSubU32") == 0) { *op = IR_MATH_OP_SATURATING_SUB_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (strcmp(callee_name, "std.math.saturatingMulU32") == 0) { *op = IR_MATH_OP_SATURATING_MUL_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (strcmp(callee_name, "std.math.checkedAddI32") == 0) { *op = IR_MATH_OP_CHECKED_ADD_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_I32; }
  else if (strcmp(callee_name, "std.math.checkedSubI32") == 0) { *op = IR_MATH_OP_CHECKED_SUB_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_I32; }
  else if (strcmp(callee_name, "std.math.checkedMulI32") == 0) { *op = IR_MATH_OP_CHECKED_MUL_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_I32; }
  else if (strcmp(callee_name, "std.math.saturatingAddI32") == 0) { *op = IR_MATH_OP_SATURATING_ADD_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_I32; }
  else if (strcmp(callee_name, "std.math.saturatingSubI32") == 0) { *op = IR_MATH_OP_SATURATING_SUB_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_I32; }
  else if (strcmp(callee_name, "std.math.saturatingMulI32") == 0) { *op = IR_MATH_OP_SATURATING_MUL_I32; *arg_type = IR_TYPE_I32; *return_type = IR_TYPE_I32; }
  else if (strcmp(callee_name, "std.math.gcdU32") == 0) { *op = IR_MATH_OP_GCD_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (strcmp(callee_name, "std.math.lcmU32") == 0) { *op = IR_MATH_OP_LCM_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (strcmp(callee_name, "std.math.checkedLcmU32") == 0) { *op = IR_MATH_OP_CHECKED_LCM_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_U32; }
  else if (strcmp(callee_name, "std.math.powU32") == 0) { *op = IR_MATH_OP_POW_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; }
  else if (strcmp(callee_name, "std.math.checkedPowU32") == 0) { *op = IR_MATH_OP_CHECKED_POW_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_U32; }
  else if (strcmp(callee_name, "std.math.modPowU32") == 0) { *op = IR_MATH_OP_MOD_POW_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; *expected_args = 3; }
  else if (strcmp(callee_name, "std.math.isPrimeU32") == 0) { *op = IR_MATH_OP_IS_PRIME_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_BOOL; *expected_args = 1; }
  else if (strcmp(callee_name, "std.math.sqrtFloorU32") == 0) { *op = IR_MATH_OP_SQRT_FLOOR_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; *expected_args = 1; }
  else if (strcmp(callee_name, "std.math.factorialU32") == 0) { *op = IR_MATH_OP_FACTORIAL_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_U32; *expected_args = 1; }
  else if (strcmp(callee_name, "std.math.binomialU32") == 0) { *op = IR_MATH_OP_BINOMIAL_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_U32; }
  else if (strcmp(callee_name, "std.math.divisorCountU32") == 0) { *op = IR_MATH_OP_DIVISOR_COUNT_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; *expected_args = 1; }
  else if (strcmp(callee_name, "std.math.properDivisorSumU32") == 0) { *op = IR_MATH_OP_PROPER_DIVISOR_SUM_U32; *arg_type = IR_TYPE_U32; *return_type = IR_TYPE_U32; *expected_args = 1; }
  else if (strcmp(callee_name, "std.math.checkedAddUsize") == 0) { *op = IR_MATH_OP_CHECKED_ADD_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_USIZE; }
  else if (strcmp(callee_name, "std.math.checkedSubUsize") == 0) { *op = IR_MATH_OP_CHECKED_SUB_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_USIZE; }
  else if (strcmp(callee_name, "std.math.checkedMulUsize") == 0) { *op = IR_MATH_OP_CHECKED_MUL_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_MAYBE_SCALAR; *return_element_type = IR_TYPE_USIZE; }
  else if (strcmp(callee_name, "std.math.saturatingAddUsize") == 0) { *op = IR_MATH_OP_SATURATING_ADD_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_USIZE; }
  else if (strcmp(callee_name, "std.math.saturatingSubUsize") == 0) { *op = IR_MATH_OP_SATURATING_SUB_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_USIZE; }
  else if (strcmp(callee_name, "std.math.saturatingMulUsize") == 0) { *op = IR_MATH_OP_SATURATING_MUL_USIZE; *arg_type = IR_TYPE_USIZE; *return_type = IR_TYPE_USIZE; }
  else return false;
  return arg_count == *expected_args;
}

static bool ir_make_std_math_runtime_value(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrMathOp op, IrTypeKind arg_type, IrTypeKind return_type, IrTypeKind return_element_type, size_t expected_args, IrValue **out) {
  if (!call || call->args.len != expected_args) {
    ir_mark_unsupported(ir, "direct backend std.math helper has unsupported arity", call ? call->line : 1, call ? call->column : 1, "wrong std.math arity");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_MATH_RUNTIME, return_type, call->line, call->column);
  value->int_value = (unsigned long long)op;
  if (return_type == IR_TYPE_MAYBE_SCALAR) value->element_type = return_element_type;
  for (size_t i = 0; i < expected_args; i++) {
    IrValue *arg = NULL;
    if (!ir_lower_call_arg(program, ir, fun, call->args.items[i], arg_type, &arg)) {
      ir_free_value(value);
      return false;
    }
    if (!arg || arg->type != arg_type) {
      ir_free_value(arg);
      ir_free_value(value);
      ir_mark_unsupported(ir, "direct backend std.math helper argument has wrong type", call->args.items[i] ? call->args.items[i]->line : call->line, call->args.items[i] ? call->args.items[i]->column : call->column, "wrong std.math argument type");
      return false;
    }
    ir_value_push_arg(ir, value, ir_new_cast_value(ir, arg, IR_TYPE_I64, call->line, call->column));
  }
  ir_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_std_search_runtime_spec(const char *callee_name, IrSearchOp *op, IrTypeKind *element_type) {
  if (!callee_name || !op || !element_type) return false;
  if (strcmp(callee_name, "std.search.lowerBoundI32") == 0) { *op = IR_SEARCH_OP_LOWER_BOUND_I32; *element_type = IR_TYPE_I32; return true; }
  if (strcmp(callee_name, "std.search.binaryI32") == 0) { *op = IR_SEARCH_OP_BINARY_I32; *element_type = IR_TYPE_I32; return true; }
  if (strcmp(callee_name, "std.search.lowerBoundU32") == 0) { *op = IR_SEARCH_OP_LOWER_BOUND_U32; *element_type = IR_TYPE_U32; return true; }
  if (strcmp(callee_name, "std.search.binaryU32") == 0) { *op = IR_SEARCH_OP_BINARY_U32; *element_type = IR_TYPE_U32; return true; }
  if (strcmp(callee_name, "std.search.lowerBoundUsize") == 0) { *op = IR_SEARCH_OP_LOWER_BOUND_USIZE; *element_type = IR_TYPE_USIZE; return true; }
  if (strcmp(callee_name, "std.search.binaryUsize") == 0) { *op = IR_SEARCH_OP_BINARY_USIZE; *element_type = IR_TYPE_USIZE; return true; }
  if (strcmp(callee_name, "std.search.upperBoundI32") == 0) { *op = IR_SEARCH_OP_UPPER_BOUND_I32; *element_type = IR_TYPE_I32; return true; }
  if (strcmp(callee_name, "std.search.upperBoundU32") == 0) { *op = IR_SEARCH_OP_UPPER_BOUND_U32; *element_type = IR_TYPE_U32; return true; }
  if (strcmp(callee_name, "std.search.upperBoundUsize") == 0) { *op = IR_SEARCH_OP_UPPER_BOUND_USIZE; *element_type = IR_TYPE_USIZE; return true; }
  return false;
}

static bool ir_lower_std_search_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, const char *callee_name, bool *handled, IrValue **out) {
  *handled = false;
  IrSearchOp op = IR_SEARCH_OP_LOWER_BOUND_I32;
  IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
  if (!ir_std_search_runtime_spec(callee_name, &op, &element_type)) return true;
  *handled = true;
  if (!call || call->args.len != 2) {
    ir_mark_unsupported(ir, "direct backend std.search helper has unsupported arity", call ? call->line : 1, call ? call->column : 1, "wrong std.search arity");
    return false;
  }
  IrValue *items = NULL;
  if (!ir_lower_byte_view(program, ir, fun, call->args.items[0], &items)) return false;
  IrTypeKind actual_element = items && items->element_type != IR_TYPE_UNSUPPORTED ? items->element_type : IR_TYPE_U8;
  if (!items || items->type != IR_TYPE_BYTE_VIEW || actual_element != element_type) {
    ir_free_value(items);
    ir_mark_unsupported(ir, "direct backend std.search helper requires a typed span matching the helper width", call->args.items[0] ? call->args.items[0]->line : call->line, call->args.items[0] ? call->args.items[0]->column : call->column, "wrong std.search span type");
    return false;
  }
  IrValue *needle = NULL;
  if (!ir_lower_call_arg(program, ir, fun, call->args.items[1], element_type, &needle)) {
    ir_free_value(items);
    return false;
  }
  if (!needle || needle->type != element_type) {
    ir_free_value(needle);
    ir_free_value(items);
    ir_mark_unsupported(ir, "direct backend std.search helper needle has wrong type", call->args.items[1] ? call->args.items[1]->line : call->line, call->args.items[1] ? call->args.items[1]->column : call->column, "wrong std.search needle type");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_SEARCH_RUNTIME, IR_TYPE_USIZE, call->line, call->column);
  value->int_value = (unsigned long long)op;
  value->left = items;
  value->right = ir_new_cast_value(ir, needle, IR_TYPE_I64, call->line, call->column);
  ir_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_std_sort_runtime_spec(const char *callee_name, IrSortOp *op, IrTypeKind *element_type, IrTypeKind *return_type) {
  if (!callee_name || !op || !element_type || !return_type) return false;
  if (strcmp(callee_name, "std.sort.insertionI32") == 0) { *op = IR_SORT_OP_INSERTION_I32; *element_type = IR_TYPE_I32; *return_type = IR_TYPE_VOID; return true; }
  if (strcmp(callee_name, "std.sort.isSortedI32") == 0) { *op = IR_SORT_OP_IS_SORTED_I32; *element_type = IR_TYPE_I32; *return_type = IR_TYPE_BOOL; return true; }
  if (strcmp(callee_name, "std.sort.insertionU32") == 0) { *op = IR_SORT_OP_INSERTION_U32; *element_type = IR_TYPE_U32; *return_type = IR_TYPE_VOID; return true; }
  if (strcmp(callee_name, "std.sort.isSortedU32") == 0) { *op = IR_SORT_OP_IS_SORTED_U32; *element_type = IR_TYPE_U32; *return_type = IR_TYPE_BOOL; return true; }
  if (strcmp(callee_name, "std.sort.insertionUsize") == 0) { *op = IR_SORT_OP_INSERTION_USIZE; *element_type = IR_TYPE_USIZE; *return_type = IR_TYPE_VOID; return true; }
  if (strcmp(callee_name, "std.sort.isSortedUsize") == 0) { *op = IR_SORT_OP_IS_SORTED_USIZE; *element_type = IR_TYPE_USIZE; *return_type = IR_TYPE_BOOL; return true; }
  return false;
}

static bool ir_lower_std_sort_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, const char *callee_name, bool *handled, IrValue **out) {
  *handled = false;
  IrSortOp op = IR_SORT_OP_INSERTION_I32;
  IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
  IrTypeKind return_type = IR_TYPE_UNSUPPORTED;
  if (!ir_std_sort_runtime_spec(callee_name, &op, &element_type, &return_type)) return true;
  *handled = true;
  if (!call || call->args.len != 1) {
    ir_mark_unsupported(ir, "direct backend std.sort helper has unsupported arity", call ? call->line : 1, call ? call->column : 1, "wrong std.sort arity");
    return false;
  }
  IrValue *items = NULL;
  if (!ir_lower_byte_view(program, ir, fun, call->args.items[0], &items)) return false;
  IrTypeKind actual_element = items && items->element_type != IR_TYPE_UNSUPPORTED ? items->element_type : IR_TYPE_U8;
  if (!items || items->type != IR_TYPE_BYTE_VIEW || actual_element != element_type) {
    ir_free_value(items);
    ir_mark_unsupported(ir, "direct backend std.sort helper requires a typed span matching the helper width", call->args.items[0] ? call->args.items[0]->line : call->line, call->args.items[0] ? call->args.items[0]->column : call->column, "wrong std.sort span type");
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_SORT_RUNTIME, return_type, call->line, call->column);
  value->int_value = (unsigned long long)op;
  value->left = items;
  ir_require_runtime_helper(ir);
  *out = value;
  return true;
}

static bool ir_lower_std_testing_arg(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, size_t index, IrTypeKind expected, IrValue **out) {
  if (!call || index >= call->args.len) {
    ir_mark_unsupported(ir, "direct backend std.testing helper argument is missing", call ? call->line : 1, call ? call->column : 1, "missing std.testing argument");
    return false;
  }
  const Expr *arg_expr = call->args.items[index];
  if (!ir_lower_call_arg(program, ir, fun, arg_expr, expected, out)) return false;
  if (*out && (*out)->type == expected) return true;
  ir_free_value(*out);
  *out = NULL;
  ir_mark_unsupported(ir, "direct backend std.testing argument type does not match helper", arg_expr ? arg_expr->line : call->line, arg_expr ? arg_expr->column : call->column, arg_expr && arg_expr->resolved_type ? arg_expr->resolved_type : "unknown argument type");
  return false;
}

static bool ir_lower_std_testing_equal_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrTypeKind type, IrValue **out) {
  if (!call || call->args.len != 2) {
    ir_mark_unsupported(ir, "direct backend std.testing equality helper expects two arguments", call ? call->line : 1, call ? call->column : 1, "wrong std.testing arity");
    return false;
  }
  IrValue *left = NULL;
  IrValue *right = NULL;
  if (!ir_lower_std_testing_arg(program, ir, fun, call, 0, type, &left) ||
      !ir_lower_std_testing_arg(program, ir, fun, call, 1, type, &right)) {
    ir_free_value(left);
    ir_free_value(right);
    return false;
  }
  *out = ir_new_compare_value(ir, IR_CMP_EQ, left, right, call->line, call->column);
  return true;
}

static bool ir_lower_std_testing_byte_pair_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrStrOp op, bool byte_equal, IrValue **out) {
  if (!call || call->args.len != 2) {
    ir_mark_unsupported(ir, "direct backend std.testing byte helper expects two arguments", call ? call->line : 1, call ? call->column : 1, "wrong std.testing arity");
    return false;
  }
  if (byte_equal) {
    IrValue *left = NULL;
    IrValue *right = NULL;
    if (!ir_lower_std_testing_arg(program, ir, fun, call, 0, IR_TYPE_BYTE_VIEW, &left) ||
        !ir_lower_std_testing_arg(program, ir, fun, call, 1, IR_TYPE_BYTE_VIEW, &right)) {
      ir_free_value(left);
      ir_free_value(right);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_EQ, IR_TYPE_BOOL, call->line, call->column);
    value->left = left;
    value->right = right;
    *out = value;
    return true;
  }
  const IrTypeKind two_views[] = {IR_TYPE_BYTE_VIEW, IR_TYPE_BYTE_VIEW};
  return ir_make_std_str_runtime_value(program, ir, fun, call, op, IR_TYPE_BOOL, two_views, 2, false, out);
}

typedef enum {
  IR_STD_TESTING_IS_TRUE,
  IR_STD_TESTING_IS_FALSE,
  IR_STD_TESTING_EQUAL,
  IR_STD_TESTING_BYTE_PAIR,
} IrStdTestingKind;

typedef struct {
  const char *name;
  IrStdTestingKind kind;
  IrTypeKind type;
  IrStrOp byte_pair_op;
  bool byte_equal;
} IrStdTestingSpec;

static const IrStdTestingSpec *ir_std_testing_spec(const char *callee_name) {
  static const IrStdTestingSpec specs[] = {
    {"std.testing.isTrue", IR_STD_TESTING_IS_TRUE, IR_TYPE_BOOL, IR_STR_OP_CONTAINS, false},
    {"std.testing.isFalse", IR_STD_TESTING_IS_FALSE, IR_TYPE_BOOL, IR_STR_OP_CONTAINS, false},
    {"std.testing.equalBool", IR_STD_TESTING_EQUAL, IR_TYPE_BOOL, IR_STR_OP_CONTAINS, false},
    {"std.testing.equalUsize", IR_STD_TESTING_EQUAL, IR_TYPE_USIZE, IR_STR_OP_CONTAINS, false},
    {"std.testing.equalU32", IR_STD_TESTING_EQUAL, IR_TYPE_U32, IR_STR_OP_CONTAINS, false},
    {"std.testing.equalI32", IR_STD_TESTING_EQUAL, IR_TYPE_I32, IR_STR_OP_CONTAINS, false},
    {"std.testing.equalBytes", IR_STD_TESTING_BYTE_PAIR, IR_TYPE_BYTE_VIEW, IR_STR_OP_CONTAINS, true},
    {"std.testing.containsBytes", IR_STD_TESTING_BYTE_PAIR, IR_TYPE_BYTE_VIEW, IR_STR_OP_CONTAINS, false},
    {"std.testing.startsWith", IR_STD_TESTING_BYTE_PAIR, IR_TYPE_BYTE_VIEW, IR_STR_OP_STARTS_WITH, false},
    {"std.testing.endsWith", IR_STD_TESTING_BYTE_PAIR, IR_TYPE_BYTE_VIEW, IR_STR_OP_ENDS_WITH, false},
  };
  for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
    if (strcmp(callee_name, specs[i].name) == 0) return &specs[i];
  }
  return NULL;
}

static bool ir_lower_std_testing_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, const char *callee_name, bool *handled, IrValue **out) {
  const IrStdTestingSpec *spec = ir_std_testing_spec(callee_name);
  if (!spec) {
    *handled = false;
    return true;
  }
  *handled = true;
  if (spec->kind == IR_STD_TESTING_IS_TRUE) {
    if (!call || call->args.len != 1) {
      ir_mark_unsupported(ir, "direct backend std.testing.isTrue expects one argument", call ? call->line : 1, call ? call->column : 1, "wrong std.testing arity");
      return false;
    }
    return ir_lower_std_testing_arg(program, ir, fun, call, 0, IR_TYPE_BOOL, out);
  }
  if (spec->kind == IR_STD_TESTING_IS_FALSE) {
    if (!call || call->args.len != 1) {
      ir_mark_unsupported(ir, "direct backend std.testing.isFalse expects one argument", call ? call->line : 1, call ? call->column : 1, "wrong std.testing arity");
      return false;
    }
    IrValue *arg = NULL;
    if (!ir_lower_std_testing_arg(program, ir, fun, call, 0, IR_TYPE_BOOL, &arg)) return false;
    IrValue *false_value = ir_new_value(ir, IR_VALUE_BOOL, IR_TYPE_BOOL, call->line, call->column);
    false_value->int_value = 0;
    *out = ir_new_compare_value(ir, IR_CMP_EQ, arg, false_value, call->line, call->column);
    return true;
  }
  if (spec->kind == IR_STD_TESTING_EQUAL) return ir_lower_std_testing_equal_call(program, ir, fun, call, spec->type, out);
  return ir_lower_std_testing_byte_pair_call(program, ir, fun, call, spec->byte_pair_op, spec->byte_equal, out);
}

static void ir_instr_vec_push(IrProgram *ir, IrInstr **items, size_t *len, size_t *cap, IrInstr instr) {
  *items = ir_grow_tracked_items(ir, *items, *len, cap, 4, sizeof(IrInstr));
  (*items)[(*len)++] = instr;
}

static bool ir_binary_op(const char *text, IrBinaryOp *out) {
  if (!text) return false;
  if (strcmp(text, "+") == 0) *out = IR_BIN_ADD;
  else if (strcmp(text, "-") == 0) *out = IR_BIN_SUB;
  else if (strcmp(text, "*") == 0) *out = IR_BIN_MUL;
  else if (strcmp(text, "/") == 0) *out = IR_BIN_DIV;
  else if (strcmp(text, "%") == 0) *out = IR_BIN_MOD;
  else if (strcmp(text, "&&") == 0) *out = IR_BIN_AND;
  else if (strcmp(text, "||") == 0) *out = IR_BIN_OR;
  else return false;
  return true;
}

static bool ir_compare_op(const char *text, IrCompareOp *out) {
  if (!text) return false;
  if (strcmp(text, "==") == 0) *out = IR_CMP_EQ;
  else if (strcmp(text, "!=") == 0) *out = IR_CMP_NE;
  else if (strcmp(text, "<") == 0) *out = IR_CMP_LT;
  else if (strcmp(text, "<=") == 0) *out = IR_CMP_LE;
  else if (strcmp(text, ">") == 0) *out = IR_CMP_GT;
  else if (strcmp(text, ">=") == 0) *out = IR_CMP_GE;
  else return false;
  return true;
}

static const TypeArgVec *ir_call_type_args(const Expr *call) {
  if (!call) return NULL;
  if (call->checked_type_args.len > 0) return &call->checked_type_args;
  if (call->type_args.len > 0) return &call->type_args;
  if (call->kind == EXPR_CALL && call->left && call->left->checked_type_args.len > 0) return &call->left->checked_type_args;
  if (call->kind == EXPR_CALL && call->left && call->left->type_args.len > 0) return &call->left->type_args;
  return &call->type_args;
}

static char *ir_specialized_function_name(const Function *fun, const TypeArgVec *type_args) {
  return z_specialized_function_name(fun, type_args);
}

static char *ir_specialize_type_text(const char *type, const Function *fun, const TypeArgVec *type_args);

static bool ir_program_scope_name_shadows_c_import_alias(const Program *program, const char *alias) {
  if (!program || !alias) return false;
  for (size_t i = 0; i < program->consts.len; i++) {
    if (program->consts.items[i].name && strcmp(program->consts.items[i].name, alias) == 0) return true;
  }
  for (size_t i = 0; i < program->shapes.len; i++) {
    if (program->shapes.items[i].name && strcmp(program->shapes.items[i].name, alias) == 0) return true;
  }
  for (size_t i = 0; i < program->interfaces.len; i++) {
    if (program->interfaces.items[i].name && strcmp(program->interfaces.items[i].name, alias) == 0) return true;
  }
  for (size_t i = 0; i < program->enums.len; i++) {
    if (program->enums.items[i].name && strcmp(program->enums.items[i].name, alias) == 0) return true;
  }
  for (size_t i = 0; i < program->choices.len; i++) {
    if (program->choices.items[i].name && strcmp(program->choices.items[i].name, alias) == 0) return true;
  }
  for (size_t i = 0; i < program->aliases.len; i++) {
    if (program->aliases.items[i].name && strcmp(program->aliases.items[i].name, alias) == 0) return true;
  }
  return false;
}

static bool ir_lower_c_import_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *expr, IrValue **out) {
  if (!program || !expr || expr->kind != EXPR_CALL || !expr->left || expr->left->kind != EXPR_MEMBER ||
      !expr->left->left || expr->left->left->kind != EXPR_IDENT) {
    return false;
  }
  const char *alias = expr->left->left->text;
  const char *symbol = expr->left->text;
  if (ir_active_local_has(ir, alias)) return false;
  if (ir_program_scope_name_shadows_c_import_alias(program, alias)) return false;
  if (!z_c_import_alias_exists(program, alias)) return false;
  ZCImportFunction function = {0};
  if (!z_c_import_find_function_for_target(program, ir->target, alias, symbol, &function, NULL)) {
    ir_mark_unsupported(ir, "direct backend extern c symbol is missing from imported header", expr->line, expr->column, symbol ? symbol : "missing C symbol");
    return true;
  }
  if (function.old_style_params) {
    ir_mark_unsupported(ir, "direct backend extern c function uses an old-style empty C parameter list", expr->line, expr->column, symbol ? symbol : "C function");
    z_c_import_function_free(&function);
    return true;
  }
  IrTypeKind return_type = ir_type_kind(function.return_zero_type);
  if (return_type != IR_TYPE_VOID && !ir_type_is_direct_abi(return_type)) {
    ir_mark_unsupported(ir, "direct backend extern c return type is unsupported", expr->line, expr->column, function.return_c_type ? function.return_c_type : "unsupported C return type");
    z_c_import_function_free(&function);
    return true;
  }
  if (expr->args.len != function.param_len) {
    ir_mark_unsupported(ir, "direct backend extern c call argument count does not match header", expr->line, expr->column, symbol ? symbol : "C function");
    z_c_import_function_free(&function);
    return true;
  }
  for (size_t i = 0; i < function.param_len; i++) {
    IrTypeKind param_type = ir_type_kind(function.params[i].zero_type);
    if (!ir_type_is_direct_abi(param_type)) {
      ir_mark_unsupported(ir, "direct backend extern c parameter type is unsupported", expr->line, expr->column, function.params[i].c_type ? function.params[i].c_type : "unsupported C parameter type");
      z_c_import_function_free(&function);
      return true;
    }
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_CALL, return_type, expr->line, expr->column);
  value->external_call = true;
  value->external_index = ir_add_external_function(ir, &function);
  value->element_type = return_type;
  for (size_t i = 0; i < expr->args.len; i++) {
    IrTypeKind expected = ir_type_kind(function.params[i].zero_type);
    IrValue *arg = NULL;
    if (!ir_lower_call_arg(program, ir, fun, expr->args.items[i], expected, &arg)) {
      ir_free_value(value);
      z_c_import_function_free(&function);
      return true;
    }
    if (arg->type != expected) {
      ir_free_value(arg);
      ir_free_value(value);
      ir_mark_unsupported(ir, "direct backend extern c argument type does not match parameter", expr->args.items[i]->line, expr->args.items[i]->column, function.params[i].zero_type ? function.params[i].zero_type : "Unknown");
      z_c_import_function_free(&function);
      return true;
    }
    ir_value_push_arg(ir, value, arg);
  }
  ir->direct_c_import_call_count++;
  *out = value;
  z_c_import_function_free(&function);
  return true;
}

static bool ir_lower_named_direct_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *expr, const char *source_name, const char *display_name, IrValue **out) {
  unsigned callee_index = 0;
  const Function *callee = ir_find_source_function(program, source_name, NULL);
  if (!callee) {
    ir_mark_unsupported(ir, "direct backend call target is not a same-file function", expr->line, expr->column, display_name ? display_name : source_name);
    return false;
  }
  const TypeArgVec *type_args = ir_call_type_args(expr);
  bool generic_call = callee->type_params.len > 0;
  char *specialized_name = NULL;
  const char *lookup_name = source_name;
  if (generic_call) {
    if (!type_args || type_args->len != callee->type_params.len) {
      ir_mark_unsupported(ir, "direct backend generic calls require explicit type arguments", expr->line, expr->column, callee->name);
      return false;
    }
    specialized_name = ir_specialized_function_name(callee, type_args);
    lookup_name = specialized_name;
  } else if (type_args && type_args->len > 0) {
    ir_mark_unsupported(ir, "direct backend non-generic call cannot use type arguments", expr->line, expr->column, display_name ? display_name : source_name);
    return false;
  }
  if (!ir_find_function_index(ir, lookup_name, &callee_index)) {
    free(specialized_name);
    ir_mark_unsupported(ir, "direct backend call target is missing from MIR function table", expr->line, expr->column, display_name ? display_name : source_name);
    return false;
  }
  if (callee->type_params.len > 0 || callee->is_test) {
    if (!generic_call || callee->is_test) {
      free(specialized_name);
      ir_mark_unsupported(ir, "direct backend calls do not support tests", expr->line, expr->column, callee->name);
      return false;
    }
  }
  if (callee->params.len != expr->args.len) {
    free(specialized_name);
    ir_mark_unsupported(ir, "direct backend call argument count does not match callee", expr->line, expr->column, callee->name);
    return false;
  }
  char *specialized_return_type = generic_call ? ir_specialize_type_text(callee->return_type, callee, type_args) : NULL;
  const char *return_type_text = generic_call ? specialized_return_type : callee->return_type;
  IrTypeKind type = ir_type_kind(return_type_text);
  if (callee->raises && !ir_type_is_direct_fallible_value(type)) {
    free(specialized_return_type);
    free(specialized_name);
    ir_mark_unsupported(ir, "direct backend fallible call return type is unsupported", expr->line, expr->column, callee->return_type);
    return false;
  }
  if (!callee->raises && type != IR_TYPE_VOID && !ir_type_is_direct_return_abi(type)) {
    free(specialized_return_type);
    free(specialized_name);
    ir_mark_unsupported(ir, "direct backend call return type is unsupported", expr->line, expr->column, callee->return_type);
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_CALL, callee->raises ? IR_TYPE_I64 : type, expr->line, expr->column);
  value->callee_index = callee_index;
  value->element_type = type == IR_TYPE_BYTE_VIEW ? ir_view_element_type_for_type(return_type_text) : type;
  if (type == IR_TYPE_MAYBE_SCALAR) value->element_type = ir_maybe_scalar_element_type(return_type_text);
  for (size_t i = 0; i < expr->args.len; i++) {
    char *specialized_param_type = generic_call ? ir_specialize_type_text(callee->params.items[i].type, callee, type_args) : NULL;
    const char *param_type_text = generic_call ? specialized_param_type : callee->params.items[i].type;
    IrTypeKind expected = ir_type_kind(param_type_text);
    if (!ir_type_is_direct_param_abi(expected)) {
      free(specialized_param_type);
      free(specialized_return_type);
      free(specialized_name);
      ir_free_value(value);
      ir_mark_unsupported(ir, "direct backend call parameter type is unsupported", callee->params.items[i].line, callee->params.items[i].column, callee->params.items[i].type);
      return false;
    }
    IrValue *arg = NULL;
    if (!ir_lower_call_arg(program, ir, fun, expr->args.items[i], expected, &arg)) {
      free(specialized_param_type);
      free(specialized_return_type);
      free(specialized_name);
      ir_free_value(value);
      return false;
    }
    if (arg->type != expected) {
      ir_free_value(arg);
      ir_free_value(value);
      free(specialized_param_type);
      free(specialized_return_type);
      free(specialized_name);
      ir_mark_unsupported(ir, "direct backend call argument type does not match parameter", expr->args.items[i]->line, expr->args.items[i]->column, callee->params.items[i].type);
      return false;
    }
    if (expected == IR_TYPE_BYTE_VIEW) {
      IrTypeKind expected_element = ir_view_element_type_for_type(param_type_text);
      IrTypeKind actual_element = arg->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : arg->element_type;
      if (expected_element == IR_TYPE_UNSUPPORTED) expected_element = IR_TYPE_U8;
      if (actual_element != expected_element) {
        ir_free_value(arg);
        ir_free_value(value);
        free(specialized_param_type);
        free(specialized_return_type);
        free(specialized_name);
        ir_mark_unsupported(ir, "direct backend call byte-view argument element type does not match parameter", expr->args.items[i]->line, expr->args.items[i]->column, callee->params.items[i].type);
        return false;
      }
    }
    ir_value_push_arg(ir, value, arg);
    free(specialized_param_type);
  }
  free(specialized_return_type);
  free(specialized_name);
  *out = value;
  return true;
}

static const char *ir_substitute_type_param(const Function *fun, const TypeArgVec *type_args, const char *type) {
  if (!fun || !type_args || !type) return type;
  for (size_t i = 0; i < fun->type_params.len && i < type_args->len; i++) {
    if (fun->type_params.items[i].name && strcmp(fun->type_params.items[i].name, type) == 0) {
      return type_args->items[i].type;
    }
  }
  return type;
}

static char *ir_expr_callee_name(const Expr *expr) {
  if (!expr) return z_strdup("");
  if (expr->kind == EXPR_IDENT) return z_strdup(expr->text ? expr->text : "");
  if (expr->kind == EXPR_MEMBER) {
    char *left = ir_expr_callee_name(expr->left);
    size_t left_len = strlen(left);
    size_t text_len = strlen(expr->text ? expr->text : "");
    char *name = z_checked_malloc(left_len + text_len + 2);
    snprintf(name, left_len + text_len + 2, "%s.%s", left, expr->text ? expr->text : "");
    free(left);
    return name;
  }
  return z_strdup("");
}

typedef enum {
  IR_DIRECT_STD_CALL_UNKNOWN,
  IR_DIRECT_STD_PROC_SPAWN,
  IR_DIRECT_STD_PROC_SPAWN_INHERIT,
  IR_DIRECT_STD_PROC_SPAWN_INHERIT_ARGS,
  IR_DIRECT_STD_PROC_CAPTURE,
  IR_DIRECT_STD_PROC_CAPTURE_ARGS,
  IR_DIRECT_STD_PROC_CAPTURE_FILES,
  IR_DIRECT_STD_PROC_CAPTURE_FILES_ARGS,
  IR_DIRECT_STD_PROC_SPAWN_CHILD,
  IR_DIRECT_STD_PROC_SPAWN_CHILD_IN,
  IR_DIRECT_STD_PROC_SPAWN_CHILD_IN_ENV,
  IR_DIRECT_STD_PROC_SPAWN_CHILD_ARGS,
  IR_DIRECT_STD_PROC_CHILD_VALID,
  IR_DIRECT_STD_PROC_RUNNING,
  IR_DIRECT_STD_PROC_WAIT,
  IR_DIRECT_STD_PROC_KILL,
  IR_DIRECT_STD_PROC_INTERRUPT,
  IR_DIRECT_STD_PROC_CLOSE,
  IR_DIRECT_STD_PROC_CLOSE_STDIN,
  IR_DIRECT_STD_PROC_PID,
  IR_DIRECT_STD_PROC_PID_RUNNING,
  IR_DIRECT_STD_PROC_KILL_PID,
  IR_DIRECT_STD_PROC_INTERRUPT_PID,
  IR_DIRECT_STD_PROC_READ_STDOUT,
  IR_DIRECT_STD_PROC_READ_STDERR,
  IR_DIRECT_STD_PROC_WRITE_STDIN,
  IR_DIRECT_STD_PROC_EXIT_CODE,
  IR_DIRECT_STD_PROC_SUCCEEDED,
  IR_DIRECT_STD_PROC_FAILED,
  IR_DIRECT_STD_ARGS_LEN,
  IR_DIRECT_STD_ARGS_GET,
  IR_DIRECT_STD_ARGS_HAS,
  IR_DIRECT_STD_ARGS_GET_OR,
  IR_DIRECT_STD_ARGS_FIND,
  IR_DIRECT_STD_ARGS_VALUE_AFTER,
  IR_DIRECT_STD_ARGS_PARSE_U32,
  IR_DIRECT_STD_PARSE_I32,
  IR_DIRECT_STD_PARSE_U32,
  IR_DIRECT_STD_FMT_BOOL,
  IR_DIRECT_STD_FMT_HEX_LOWER_U32,
  IR_DIRECT_STD_FMT_I32,
  IR_DIRECT_STD_FMT_U32,
  IR_DIRECT_STD_FMT_USIZE,
  IR_DIRECT_STD_CLI_OPTION_VALUE,
  IR_DIRECT_STD_CLI_OPTION_VALUE_OR,
  IR_DIRECT_STD_CLI_OPTION_U32,
  IR_DIRECT_STD_CLI_HAS_FLAG,
  IR_DIRECT_STD_CLI_SUCCESS_EXIT_CODE,
  IR_DIRECT_STD_CLI_USAGE_EXIT_CODE,
  IR_DIRECT_STD_CLI_ARG_EQUALS,
  IR_DIRECT_STD_CLI_COMMAND,
  IR_DIRECT_STD_CLI_COMMAND_OR,
  IR_DIRECT_STD_CLI_COMMAND_EQUALS,
  IR_DIRECT_STD_CLI_ARG_OR,
} IrDirectStdCallId;

typedef struct {
  const char *name;
  IrDirectStdCallId id;
} IrDirectStdCallSpec;

static IrDirectStdCallId ir_direct_std_call_id(const char *callee_name) {
  static const IrDirectStdCallSpec specs[] = {
    {"std.proc.spawn", IR_DIRECT_STD_PROC_SPAWN},
    {"std.proc.spawnInherit", IR_DIRECT_STD_PROC_SPAWN_INHERIT},
    {"std.proc.spawnInheritArgs", IR_DIRECT_STD_PROC_SPAWN_INHERIT_ARGS},
    {"std.proc.capture", IR_DIRECT_STD_PROC_CAPTURE},
    {"std.proc.captureArgs", IR_DIRECT_STD_PROC_CAPTURE_ARGS},
    {"std.proc.captureFiles", IR_DIRECT_STD_PROC_CAPTURE_FILES},
    {"std.proc.captureFilesArgs", IR_DIRECT_STD_PROC_CAPTURE_FILES_ARGS},
    {"std.proc.spawnChild", IR_DIRECT_STD_PROC_SPAWN_CHILD},
    {"std.proc.spawnChildIn", IR_DIRECT_STD_PROC_SPAWN_CHILD_IN},
    {"std.proc.spawnChildInEnv", IR_DIRECT_STD_PROC_SPAWN_CHILD_IN_ENV},
    {"std.proc.spawnChildArgs", IR_DIRECT_STD_PROC_SPAWN_CHILD_ARGS},
    {"std.proc.childValid", IR_DIRECT_STD_PROC_CHILD_VALID},
    {"std.proc.running", IR_DIRECT_STD_PROC_RUNNING},
    {"std.proc.wait", IR_DIRECT_STD_PROC_WAIT},
    {"std.proc.kill", IR_DIRECT_STD_PROC_KILL},
    {"std.proc.interrupt", IR_DIRECT_STD_PROC_INTERRUPT},
    {"std.proc.close", IR_DIRECT_STD_PROC_CLOSE},
    {"std.proc.closeStdin", IR_DIRECT_STD_PROC_CLOSE_STDIN},
    {"std.proc.pid", IR_DIRECT_STD_PROC_PID},
    {"std.proc.pidRunning", IR_DIRECT_STD_PROC_PID_RUNNING},
    {"std.proc.killPid", IR_DIRECT_STD_PROC_KILL_PID},
    {"std.proc.interruptPid", IR_DIRECT_STD_PROC_INTERRUPT_PID},
    {"std.proc.readStdout", IR_DIRECT_STD_PROC_READ_STDOUT},
    {"std.proc.readStderr", IR_DIRECT_STD_PROC_READ_STDERR},
    {"std.proc.writeStdin", IR_DIRECT_STD_PROC_WRITE_STDIN},
    {"std.proc.exitCode", IR_DIRECT_STD_PROC_EXIT_CODE},
    {"std.proc.succeeded", IR_DIRECT_STD_PROC_SUCCEEDED},
    {"std.proc.failed", IR_DIRECT_STD_PROC_FAILED},
    {"std.args.len", IR_DIRECT_STD_ARGS_LEN},
    {"std.args.get", IR_DIRECT_STD_ARGS_GET},
    {"std.args.has", IR_DIRECT_STD_ARGS_HAS},
    {"std.args.getOr", IR_DIRECT_STD_ARGS_GET_OR},
    {"std.args.find", IR_DIRECT_STD_ARGS_FIND},
    {"std.args.valueAfter", IR_DIRECT_STD_ARGS_VALUE_AFTER},
    {"std.args.parseU32", IR_DIRECT_STD_ARGS_PARSE_U32},
    {"std.parse.parseI32", IR_DIRECT_STD_PARSE_I32},
    {"std.parse.parseU32", IR_DIRECT_STD_PARSE_U32},
    {"std.fmt.bool", IR_DIRECT_STD_FMT_BOOL},
    {"std.fmt.hexLowerU32", IR_DIRECT_STD_FMT_HEX_LOWER_U32},
    {"std.fmt.i32", IR_DIRECT_STD_FMT_I32},
    {"std.fmt.u32", IR_DIRECT_STD_FMT_U32},
    {"std.fmt.usize", IR_DIRECT_STD_FMT_USIZE},
    {"std.cli.optionValue", IR_DIRECT_STD_CLI_OPTION_VALUE},
    {"std.cli.optionValueOr", IR_DIRECT_STD_CLI_OPTION_VALUE_OR},
    {"std.cli.optionU32", IR_DIRECT_STD_CLI_OPTION_U32},
    {"std.cli.hasFlag", IR_DIRECT_STD_CLI_HAS_FLAG},
    {"std.cli.successExitCode", IR_DIRECT_STD_CLI_SUCCESS_EXIT_CODE},
    {"std.cli.usageExitCode", IR_DIRECT_STD_CLI_USAGE_EXIT_CODE},
    {"std.cli.argEquals", IR_DIRECT_STD_CLI_ARG_EQUALS},
    {"std.cli.command", IR_DIRECT_STD_CLI_COMMAND},
    {"std.cli.commandOr", IR_DIRECT_STD_CLI_COMMAND_OR},
    {"std.cli.commandEquals", IR_DIRECT_STD_CLI_COMMAND_EQUALS},
    {"std.cli.argOr", IR_DIRECT_STD_CLI_ARG_OR},
  };
  for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
    if (strcmp(callee_name, specs[i].name) == 0) return specs[i].id;
  }
  return IR_DIRECT_STD_CALL_UNKNOWN;
}

static bool ir_lower_integer_value_arg(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, size_t index, const char *message, IrValue **out) {
  if (!call || index >= call->args.len) {
    ir_mark_unsupported(ir, message, call ? call->line : 1, call ? call->column : 1, "missing argument");
    return false;
  }
  IrValue *value = NULL;
  if (!ir_lower_expr(program, ir, fun, call->args.items[index], &value)) return false;
  if (!value || !ir_type_is_value(value->type)) {
    ir_free_value(value);
    ir_mark_unsupported(ir, message, call->args.items[index] ? call->args.items[index]->line : call->line, call->args.items[index] ? call->args.items[index]->column : call->column, "non-integer index");
    return false;
  }
  *out = value;
  return true;
}

static bool ir_lower_std_proc_child_spawn_direct_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, bool with_cwd, bool with_env, bool *handled, IrValue **out) {
  IrValue *command = NULL;
  IrValue *cwd = NULL;
  IrValue *env = NULL;
  if (!ir_lower_byte_view(program, ir, fun, call->args.items[0], &command) ||
      (with_cwd && !ir_lower_byte_view(program, ir, fun, call->args.items[1], &cwd)) ||
      (with_env && !ir_lower_byte_view(program, ir, fun, call->args.items[2], &env))) {
    ir_free_value(command);
    ir_free_value(cwd);
    ir_free_value(env);
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CHILD_SPAWN, IR_TYPE_I32, call->line, call->column);
  value->left = command;
  value->right = cwd;
  value->index = env;
  ir_require_helper_counts(ir, 1, 0);
  *handled = true;
  *out = value;
  return true;
}

static bool ir_lower_std_proc_push_argv4(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrValue *value) {
  IrValue *items[4] = {0};
  for (size_t i = 0; i < 4; i++) {
    if (!ir_lower_byte_view(program, ir, fun, call->args.items[i], &items[i])) {
      for (size_t j = 0; j < 4; j++) ir_free_value(items[j]);
      return false;
    }
  }
  for (size_t i = 0; i < 4; i++) ir_value_push_arg(ir, value, items[i]);
  return true;
}

static bool ir_lower_std_proc_push_argv2(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrValue *value) {
  IrValue *items[2] = {0};
  for (size_t i = 0; i < 2; i++) {
    if (!ir_lower_byte_view(program, ir, fun, call->args.items[i], &items[i])) {
      for (size_t j = 0; j < 2; j++) ir_free_value(items[j]);
      return false;
    }
  }
  for (size_t i = 0; i < 2; i++) ir_value_push_arg(ir, value, items[i]);
  return true;
}

static bool ir_lower_std_proc_child_spawn_args_direct_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, bool *handled, IrValue **out) {
  IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CHILD_SPAWN, IR_TYPE_I32, call->line, call->column);
  if (!ir_lower_std_proc_push_argv4(program, ir, fun, call, value)) {
    ir_free_value(value);
    return false;
  }
  ir_require_helper_counts(ir, 1, 0);
  *handled = true;
  *out = value;
  return true;
}

static bool ir_lower_std_proc_direct_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrDirectStdCallId id, bool *handled, IrValue **out) {
  *handled = false;
  if (id == IR_DIRECT_STD_CALL_UNKNOWN) return true;
  if (id == IR_DIRECT_STD_PROC_SPAWN && call->args.len == 1) {
    IrValue *value = ir_new_value(ir, IR_VALUE_INT, IR_TYPE_I32, call->line, call->column);
    value->int_value = 0;
    *handled = true;
    *out = value;
    return true;
  }
  if (id == IR_DIRECT_STD_PROC_SPAWN_INHERIT && call->args.len == 1) {
    IrValue *command = NULL;
    if (!ir_lower_byte_view(program, ir, fun, call->args.items[0], &command)) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_PROC_SPAWN_INHERIT, IR_TYPE_I32, call->line, call->column);
    value->left = command;
    ir_require_helper_counts(ir, 1, 0);
    *handled = true;
    *out = value;
    return true;
  }
  if (id == IR_DIRECT_STD_PROC_SPAWN_INHERIT_ARGS && call->args.len == 4) {
    IrValue *value = ir_new_value(ir, IR_VALUE_PROC_SPAWN_INHERIT, IR_TYPE_I32, call->line, call->column);
    if (!ir_lower_std_proc_push_argv4(program, ir, fun, call, value)) {
      ir_free_value(value);
      return false;
    }
    ir_require_helper_counts(ir, 1, 0);
    *handled = true;
    *out = value;
    return true;
  }
  if (id == IR_DIRECT_STD_PROC_CAPTURE && call->args.len == 2) {
    IrValue *command = NULL;
    IrValue *buffer = NULL;
    if (!ir_lower_byte_view(program, ir, fun, call->args.items[0], &command) ||
        !ir_lower_byte_view(program, ir, fun, call->args.items[1], &buffer)) {
      ir_free_value(command);
      ir_free_value(buffer);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CAPTURE, IR_TYPE_MAYBE_SCALAR, call->line, call->column);
    value->left = command;
    value->right = buffer;
    value->element_type = IR_TYPE_USIZE;
    ir_require_helper_counts(ir, 1, 0);
    *handled = true;
    *out = value;
    return true;
  }
  if (id == IR_DIRECT_STD_PROC_CAPTURE_ARGS && call->args.len == 3) {
    IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CAPTURE, IR_TYPE_MAYBE_SCALAR, call->line, call->column);
    value->element_type = IR_TYPE_USIZE;
    if (!ir_lower_std_proc_push_argv2(program, ir, fun, call, value) ||
        !ir_lower_byte_view(program, ir, fun, call->args.items[2], &value->right)) {
      ir_free_value(value);
      return false;
    }
    ir_require_helper_counts(ir, 1, 0);
    *handled = true;
    *out = value;
    return true;
  }
  if (id == IR_DIRECT_STD_PROC_CAPTURE_FILES && call->args.len == 3) {
    IrValue *command = NULL;
    IrValue *stdout_path = NULL;
    IrValue *stderr_path = NULL;
    if (!ir_lower_byte_view(program, ir, fun, call->args.items[0], &command) ||
        !ir_lower_byte_view(program, ir, fun, call->args.items[1], &stdout_path) ||
        !ir_lower_byte_view(program, ir, fun, call->args.items[2], &stderr_path)) {
      ir_free_value(command);
      ir_free_value(stdout_path);
      ir_free_value(stderr_path);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CAPTURE_FILES, IR_TYPE_I32, call->line, call->column);
    value->left = command;
    value->right = stdout_path;
    value->index = stderr_path;
    ir_require_helper_counts(ir, 1, 0);
    *handled = true;
    *out = value;
    return true;
  }
  if (id == IR_DIRECT_STD_PROC_CAPTURE_FILES_ARGS && call->args.len == 4) {
    IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CAPTURE_FILES, IR_TYPE_I32, call->line, call->column);
    if (!ir_lower_std_proc_push_argv2(program, ir, fun, call, value) ||
        !ir_lower_byte_view(program, ir, fun, call->args.items[2], &value->right) ||
        !ir_lower_byte_view(program, ir, fun, call->args.items[3], &value->index)) {
      ir_free_value(value);
      return false;
    }
    ir_require_helper_counts(ir, 1, 0);
    *handled = true;
    *out = value;
    return true;
  }
  if (id == IR_DIRECT_STD_PROC_SPAWN_CHILD && call->args.len == 1) {
    return ir_lower_std_proc_child_spawn_direct_call(program, ir, fun, call, false, false, handled, out);
  }
  if (id == IR_DIRECT_STD_PROC_SPAWN_CHILD_IN && call->args.len == 2) {
    return ir_lower_std_proc_child_spawn_direct_call(program, ir, fun, call, true, false, handled, out);
  }
  if (id == IR_DIRECT_STD_PROC_SPAWN_CHILD_IN_ENV && call->args.len == 3) {
    return ir_lower_std_proc_child_spawn_direct_call(program, ir, fun, call, true, true, handled, out);
  }
  if (id == IR_DIRECT_STD_PROC_SPAWN_CHILD_ARGS && call->args.len == 4) {
    return ir_lower_std_proc_child_spawn_args_direct_call(program, ir, fun, call, handled, out);
  }
  if ((id == IR_DIRECT_STD_PROC_CHILD_VALID ||
       id == IR_DIRECT_STD_PROC_RUNNING ||
       id == IR_DIRECT_STD_PROC_WAIT ||
       id == IR_DIRECT_STD_PROC_KILL ||
       id == IR_DIRECT_STD_PROC_INTERRUPT ||
       id == IR_DIRECT_STD_PROC_CLOSE ||
       id == IR_DIRECT_STD_PROC_CLOSE_STDIN ||
       id == IR_DIRECT_STD_PROC_PID ||
       id == IR_DIRECT_STD_PROC_PID_RUNNING ||
       id == IR_DIRECT_STD_PROC_KILL_PID ||
       id == IR_DIRECT_STD_PROC_INTERRUPT_PID) && call->args.len == 1) {
    IrValue *child = NULL;
    if (!ir_lower_expr(program, ir, fun, call->args.items[0], &child)) return false;
    if (!child || child->type != IR_TYPE_I32) {
      ir_free_value(child);
      ir_mark_unsupported(ir, "direct backend std.proc child helper expects ProcChild", call->args.items[0] ? call->args.items[0]->line : call->line, call->args.items[0] ? call->args.items[0]->column : call->column, call->args.items[0] && call->args.items[0]->resolved_type ? call->args.items[0]->resolved_type : "unknown child type");
      return false;
    }
    IrProcChildOp op = IR_PROC_CHILD_OP_VALID;
    IrTypeKind result_type = IR_TYPE_BOOL;
    if (id == IR_DIRECT_STD_PROC_RUNNING) op = IR_PROC_CHILD_OP_RUNNING;
    else if (id == IR_DIRECT_STD_PROC_WAIT) { op = IR_PROC_CHILD_OP_WAIT; result_type = IR_TYPE_I32; }
    else if (id == IR_DIRECT_STD_PROC_KILL) op = IR_PROC_CHILD_OP_KILL;
    else if (id == IR_DIRECT_STD_PROC_INTERRUPT) op = IR_PROC_CHILD_OP_INTERRUPT;
    else if (id == IR_DIRECT_STD_PROC_CLOSE) op = IR_PROC_CHILD_OP_CLOSE;
    else if (id == IR_DIRECT_STD_PROC_CLOSE_STDIN) op = IR_PROC_CHILD_OP_CLOSE_STDIN;
    else if (id == IR_DIRECT_STD_PROC_PID) { op = IR_PROC_CHILD_OP_PID; result_type = IR_TYPE_I32; }
    else if (id == IR_DIRECT_STD_PROC_PID_RUNNING) op = IR_PROC_CHILD_OP_PID_RUNNING;
    else if (id == IR_DIRECT_STD_PROC_KILL_PID) op = IR_PROC_CHILD_OP_KILL_PID;
    else if (id == IR_DIRECT_STD_PROC_INTERRUPT_PID) op = IR_PROC_CHILD_OP_INTERRUPT_PID;
    IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CHILD_OP, result_type, call->line, call->column);
    value->left = child;
    value->int_value = (unsigned long long)op;
    ir_require_helper_counts(ir, 1, 0);
    *handled = true;
    *out = value;
    return true;
  }
  if ((id == IR_DIRECT_STD_PROC_READ_STDOUT ||
       id == IR_DIRECT_STD_PROC_READ_STDERR ||
       id == IR_DIRECT_STD_PROC_WRITE_STDIN) && call->args.len == 2) {
    IrValue *child = NULL;
    IrValue *bytes = NULL;
    if (!ir_lower_expr(program, ir, fun, call->args.items[0], &child) ||
        !ir_lower_byte_view(program, ir, fun, call->args.items[1], &bytes)) {
      ir_free_value(child);
      ir_free_value(bytes);
      return false;
    }
    if (!child || child->type != IR_TYPE_I32) {
      ir_free_value(child);
      ir_free_value(bytes);
      ir_mark_unsupported(ir, "direct backend std.proc stream helper expects ProcChild", call->args.items[0] ? call->args.items[0]->line : call->line, call->args.items[0] ? call->args.items[0]->column : call->column, call->args.items[0] && call->args.items[0]->resolved_type ? call->args.items[0]->resolved_type : "unknown child type");
      return false;
    }
    IrProcChildIoOp op = IR_PROC_CHILD_IO_READ_STDOUT;
    if (id == IR_DIRECT_STD_PROC_READ_STDERR) op = IR_PROC_CHILD_IO_READ_STDERR;
    else if (id == IR_DIRECT_STD_PROC_WRITE_STDIN) op = IR_PROC_CHILD_IO_WRITE_STDIN;
    IrValue *value = ir_new_value(ir, IR_VALUE_PROC_CHILD_IO, IR_TYPE_MAYBE_SCALAR, call->line, call->column);
    value->left = child;
    value->right = bytes;
    value->int_value = (unsigned long long)op;
    value->element_type = IR_TYPE_USIZE;
    ir_require_helper_counts(ir, 1, 0);
    *handled = true;
    *out = value;
    return true;
  }
  if (id == IR_DIRECT_STD_PROC_EXIT_CODE && call->args.len == 1) {
    *handled = true;
    return ir_lower_expr(program, ir, fun, call->args.items[0], out);
  }
  if ((id == IR_DIRECT_STD_PROC_SUCCEEDED || id == IR_DIRECT_STD_PROC_FAILED) && call->args.len == 1) {
    IrValue *status = NULL;
    if (!ir_lower_expr(program, ir, fun, call->args.items[0], &status)) return false;
    if (!status || status->type != IR_TYPE_I32) {
      ir_free_value(status);
      ir_mark_unsupported(ir, "direct backend std.proc status helper expects ProcStatus", call->args.items[0] ? call->args.items[0]->line : call->line, call->args.items[0] ? call->args.items[0]->column : call->column, call->args.items[0] && call->args.items[0]->resolved_type ? call->args.items[0]->resolved_type : "unknown status type");
      return false;
    }
    IrCompareOp op = id == IR_DIRECT_STD_PROC_SUCCEEDED ? IR_CMP_EQ : IR_CMP_NE;
    *handled = true;
    *out = ir_new_compare_value(ir, op, status, ir_new_integer_literal_value(ir, IR_TYPE_I32, 0, call->line, call->column), call->line, call->column);
    return true;
  }
  return true;
}

static bool ir_std_fmt_call_spec(IrDirectStdCallId id, IrValueKind *kind, IrTypeKind *number_type, const char **actual) {
  *kind = IR_VALUE_FMT_U32;
  *number_type = IR_TYPE_U32;
  *actual = "non-u32 value";
  switch (id) {
    case IR_DIRECT_STD_FMT_BOOL:
      *kind = IR_VALUE_FMT_BOOL;
      *number_type = IR_TYPE_BOOL;
      *actual = "non-Bool value";
      return true;
    case IR_DIRECT_STD_FMT_HEX_LOWER_U32:
      *kind = IR_VALUE_FMT_HEX_U32;
      return true;
    case IR_DIRECT_STD_FMT_I32:
      *kind = IR_VALUE_FMT_I32;
      *number_type = IR_TYPE_I32;
      *actual = "non-i32 value";
      return true;
    case IR_DIRECT_STD_FMT_U32:
      return true;
    case IR_DIRECT_STD_FMT_USIZE:
      *kind = IR_VALUE_FMT_USIZE;
      *number_type = IR_TYPE_USIZE;
      *actual = "non-usize value";
      return true;
    default:
      return false;
  }
}

static bool ir_lower_std_fmt_direct_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrDirectStdCallId id, bool *handled, IrValue **out) {
  IrValueKind kind = IR_VALUE_FMT_U32;
  IrTypeKind number_type = IR_TYPE_U32;
  const char *actual = "non-u32 value";
  if (!ir_std_fmt_call_spec(id, &kind, &number_type, &actual)) {
    *handled = false;
    return true;
  }
  *handled = true;
  if (call->args.len != 2) {
    *handled = false;
    return true;
  }
  IrValue *buffer = NULL;
  IrValue *number = NULL;
  if (!ir_lower_byte_view(program, ir, fun, call->args.items[0], &buffer) ||
      !ir_lower_expr(program, ir, fun, call->args.items[1], &number)) {
    ir_free_value(buffer);
    ir_free_value(number);
    return false;
  }
  if (!number || number->type != number_type) {
    ir_free_value(buffer);
    ir_free_value(number);
    ir_mark_unsupported(ir, "direct backend std.fmt value has unsupported type", call->args.items[1]->line, call->args.items[1]->column, actual);
    return false;
  }
  IrValue *value = ir_new_value(ir, kind, IR_TYPE_MAYBE_BYTE_VIEW, call->line, call->column);
  value->left = buffer;
  value->right = number;
  value->element_type = IR_TYPE_U8;
  ir_require_helper_counts(ir, 1, 0);
  *out = value;
  return true;
}

static bool ir_lower_std_indexed_arg_direct_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrDirectStdCallId id, bool *handled, IrValue **out) {
  if (id != IR_DIRECT_STD_ARGS_GET && id != IR_DIRECT_STD_ARGS_HAS && id != IR_DIRECT_STD_ARGS_PARSE_U32) {
    *handled = false;
    return true;
  }
  *handled = true;
  if (call->args.len != 1) {
    *handled = false;
    return true;
  }
  const char *message = id == IR_DIRECT_STD_ARGS_GET ? "direct backend std.args.get index must be an integer value" :
                        id == IR_DIRECT_STD_ARGS_HAS ? "direct backend std.args.has index must be an integer value" :
                        "direct backend std.args.parseU32 index must be an integer value";
  IrValue *index = NULL;
  if (!ir_lower_integer_value_arg(program, ir, fun, call, 0, message, &index)) return false;
  if (id == IR_DIRECT_STD_ARGS_HAS) {
    IrValue *len = ir_new_value(ir, IR_VALUE_ARGS_LEN, IR_TYPE_USIZE, call->line, call->column);
    *out = ir_new_compare_value(ir, IR_CMP_LT, index, len, call->line, call->column);
    return true;
  }
  IrValueKind kind = id == IR_DIRECT_STD_ARGS_GET ? IR_VALUE_ARGS_GET : IR_VALUE_ARGS_PARSE_U32;
  IrTypeKind type = id == IR_DIRECT_STD_ARGS_GET ? IR_TYPE_MAYBE_BYTE_VIEW : IR_TYPE_MAYBE_SCALAR;
  IrValue *value = ir_new_value(ir, kind, type, call->line, call->column);
  value->left = index;
  if (id == IR_DIRECT_STD_ARGS_PARSE_U32) {
    value->element_type = IR_TYPE_U32;
    ir_require_helper_counts(ir, 1, 1);
  }
  *out = value;
  return true;
}

static bool ir_lower_std_args_get_or_direct_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, bool *handled, IrValue **out) {
  *handled = true;
  if (call->args.len != 2) {
    *handled = false;
    return true;
  }
  IrValue *index = NULL;
  IrValue *fallback = NULL;
  if (!ir_lower_integer_value_arg(program, ir, fun, call, 0, "direct backend std.args.getOr index must be an integer value", &index) ||
      !ir_lower_byte_view(program, ir, fun, call->args.items[1], &fallback)) {
    ir_free_value(index);
    ir_free_value(fallback);
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_GET_OR, IR_TYPE_BYTE_VIEW, call->line, call->column);
  value->left = index;
  value->right = fallback;
  value->element_type = IR_TYPE_U8;
  *out = value;
  return true;
}

static bool ir_lower_std_named_arg_direct_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrDirectStdCallId id, bool *handled, IrValue **out) {
  if (id != IR_DIRECT_STD_ARGS_FIND && id != IR_DIRECT_STD_ARGS_VALUE_AFTER && id != IR_DIRECT_STD_CLI_OPTION_VALUE &&
      id != IR_DIRECT_STD_CLI_OPTION_U32 && id != IR_DIRECT_STD_CLI_HAS_FLAG) {
    *handled = false;
    return true;
  }
  *handled = true;
  if (call->args.len != 1) {
    *handled = false;
    return true;
  }
  IrValue *name = NULL;
  if (!ir_lower_byte_view(program, ir, fun, call->args.items[0], &name)) return false;
  IrValueKind kind = IR_VALUE_ARGS_FIND;
  IrTypeKind type = IR_TYPE_MAYBE_SCALAR;
  IrTypeKind element_type = IR_TYPE_USIZE;
  unsigned host_helpers = 1;
  if (id == IR_DIRECT_STD_ARGS_VALUE_AFTER || id == IR_DIRECT_STD_CLI_OPTION_VALUE) {
    kind = IR_VALUE_ARGS_VALUE_AFTER;
    type = IR_TYPE_MAYBE_BYTE_VIEW;
    element_type = IR_TYPE_U8;
  } else if (id == IR_DIRECT_STD_CLI_OPTION_U32) {
    kind = IR_VALUE_ARGS_VALUE_AFTER_PARSE_U32;
    element_type = IR_TYPE_U32;
    host_helpers = 2;
  } else if (id == IR_DIRECT_STD_CLI_HAS_FLAG) {
    kind = IR_VALUE_ARGS_CONTAINS;
    type = IR_TYPE_BOOL;
    element_type = IR_TYPE_UNSUPPORTED;
  }
  IrValue *value = ir_new_value(ir, kind, type, call->line, call->column);
  value->left = name;
  value->element_type = element_type;
  ir_require_helper_counts(ir, 1, host_helpers);
  *out = value;
  return true;
}

static bool ir_lower_std_option_value_or_direct_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, bool *handled, IrValue **out) {
  *handled = true;
  if (call->args.len != 2) {
    *handled = false;
    return true;
  }
  IrValue *name = NULL;
  IrValue *fallback = NULL;
  if (!ir_lower_byte_view(program, ir, fun, call->args.items[0], &name) ||
      !ir_lower_byte_view(program, ir, fun, call->args.items[1], &fallback)) {
    ir_free_value(name);
    ir_free_value(fallback);
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_VALUE_AFTER_OR, IR_TYPE_BYTE_VIEW, call->line, call->column);
  value->left = name;
  value->right = fallback;
  value->element_type = IR_TYPE_U8;
  ir_require_helper_counts(ir, 1, 1);
  *out = value;
  return true;
}

static bool ir_lower_std_parse_direct_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrDirectStdCallId id, bool *handled, IrValue **out) {
  if (id != IR_DIRECT_STD_PARSE_I32 && id != IR_DIRECT_STD_PARSE_U32) {
    *handled = false;
    return true;
  }
  *handled = true;
  if (call->args.len != 1) {
    *handled = false;
    return true;
  }
  IrValue *text = NULL;
  if (!ir_lower_byte_view(program, ir, fun, call->args.items[0], &text)) return false;
  bool signed_parse = id == IR_DIRECT_STD_PARSE_I32;
  IrValue *value = ir_new_value(ir, signed_parse ? IR_VALUE_PARSE_I32 : IR_VALUE_PARSE_U32, IR_TYPE_MAYBE_SCALAR, call->line, call->column);
  value->left = text;
  value->element_type = signed_parse ? IR_TYPE_I32 : IR_TYPE_U32;
  ir_require_helper_counts(ir, 1, 0);
  *out = value;
  return true;
}

static bool ir_lower_std_arg_equals_direct_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, bool *handled, IrValue **out) {
  *handled = true;
  if (call->args.len != 2) {
    *handled = false;
    return true;
  }
  IrValue *index = NULL;
  IrValue *expected = NULL;
  if (!ir_lower_integer_value_arg(program, ir, fun, call, 0, "direct backend std.cli.argEquals index must be an integer value", &index) ||
      !ir_lower_byte_view(program, ir, fun, call->args.items[1], &expected)) {
    ir_free_value(index);
    ir_free_value(expected);
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_EQ, IR_TYPE_BOOL, call->line, call->column);
  value->left = index;
  value->right = expected;
  *out = value;
  return true;
}

static bool ir_lower_std_cli_command_direct_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrDirectStdCallId id, bool *handled, IrValue **out) {
  (void)program;
  (void)fun;
  if (id != IR_DIRECT_STD_CLI_COMMAND && id != IR_DIRECT_STD_CLI_COMMAND_OR && id != IR_DIRECT_STD_CLI_COMMAND_EQUALS) {
    *handled = false;
    return true;
  }
  *handled = true;
  if (id == IR_DIRECT_STD_CLI_COMMAND) {
    if (call->args.len != 0) {
      *handled = false;
      return true;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_GET, IR_TYPE_MAYBE_BYTE_VIEW, call->line, call->column);
    value->left = ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 1, call->line, call->column);
    *out = value;
    return true;
  }
  if (call->args.len != 1) {
    *handled = false;
    return true;
  }
  IrValue *text = NULL;
  if (!ir_lower_byte_view(program, ir, fun, call->args.items[0], &text)) return false;
  if (id == IR_DIRECT_STD_CLI_COMMAND_EQUALS) {
    IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_EQ, IR_TYPE_BOOL, call->line, call->column);
    value->left = ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 1, call->line, call->column);
    value->right = text;
    *out = value;
    return true;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_GET_OR, IR_TYPE_BYTE_VIEW, call->line, call->column);
  value->left = ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 1, call->line, call->column);
  value->right = text;
  value->element_type = IR_TYPE_U8;
  *out = value;
  return true;
}

static bool ir_lower_std_cli_arg_or_direct_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrDirectStdCallId id, bool *handled, IrValue **out) {
  if (id != IR_DIRECT_STD_CLI_ARG_OR) {
    *handled = false;
    return true;
  }
  *handled = true;
  if (call->args.len != 2) {
    *handled = false;
    return true;
  }
  IrValue *index = NULL;
  IrValue *fallback = NULL;
  if (!ir_lower_integer_value_arg(program, ir, fun, call, 0, "direct backend std.cli.argOr index must be an integer value", &index) ||
      !ir_lower_byte_view(program, ir, fun, call->args.items[1], &fallback)) {
    ir_free_value(index);
    ir_free_value(fallback);
    return false;
  }
  IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_GET_OR, IR_TYPE_BYTE_VIEW, call->line, call->column);
  value->left = index;
  value->right = fallback;
  value->element_type = IR_TYPE_U8;
  *out = value;
  return true;
}

static bool ir_lower_std_args_cli_direct_call(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *call, IrDirectStdCallId id, bool *handled, IrValue **out) {
  if (id == IR_DIRECT_STD_ARGS_LEN) {
    *handled = call->args.len == 0;
    if (*handled) *out = ir_new_value(ir, IR_VALUE_ARGS_LEN, IR_TYPE_USIZE, call->line, call->column);
    return true;
  }
  if (!ir_lower_std_cli_command_direct_call(program, ir, fun, call, id, handled, out)) return false;
  if (*handled) return true;
  if (!ir_lower_std_cli_arg_or_direct_call(program, ir, fun, call, id, handled, out)) return false;
  if (*handled) return true;
  if (!ir_lower_std_indexed_arg_direct_call(program, ir, fun, call, id, handled, out)) return false;
  if (*handled) return true;
  if (id == IR_DIRECT_STD_ARGS_GET_OR) return ir_lower_std_args_get_or_direct_call(program, ir, fun, call, handled, out);
  if (!ir_lower_std_named_arg_direct_call(program, ir, fun, call, id, handled, out)) return false;
  if (*handled) return true;
  if (id == IR_DIRECT_STD_CLI_OPTION_VALUE_OR) return ir_lower_std_option_value_or_direct_call(program, ir, fun, call, handled, out);
  if (!ir_lower_std_parse_direct_call(program, ir, fun, call, id, handled, out)) return false;
  if (*handled) return true;
  if (id == IR_DIRECT_STD_CLI_SUCCESS_EXIT_CODE || id == IR_DIRECT_STD_CLI_USAGE_EXIT_CODE) {
    *handled = call->args.len == 0;
    if (*handled) *out = ir_new_integer_literal_value(ir, IR_TYPE_I32, id == IR_DIRECT_STD_CLI_SUCCESS_EXIT_CODE ? 0 : 2, call->line, call->column);
    return true;
  }
  if (id == IR_DIRECT_STD_CLI_ARG_EQUALS) return ir_lower_std_arg_equals_direct_call(program, ir, fun, call, handled, out);
  *handled = false;
  return true;
}

static bool ir_lower_expr(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *expr, IrValue **out) {
  if (!expr) {
    ir_mark_unsupported(ir, "direct backend expression is missing", 1, 1, "missing expression");
    return false;
  }
  switch (expr->kind) {
    case EXPR_BOOL: {
      IrValue *value = ir_new_value(ir, IR_VALUE_BOOL, IR_TYPE_BOOL, expr->line, expr->column);
      value->int_value = expr->bool_value ? 1 : 0;
      *out = value;
      return true;
    }
    case EXPR_CHAR: {
      IrValue *value = ir_new_value(ir, IR_VALUE_INT, IR_TYPE_U8, expr->line, expr->column);
      unsigned long long parsed = 0;
      if (!ir_parse_integer_literal(expr->text ? expr->text : "0", &parsed) || parsed > 255) {
        ir_free_value(value);
        ir_mark_unsupported(ir, "direct backend character literal is malformed", expr->line, expr->column, expr->text ? expr->text : "missing char");
        return false;
      }
      value->int_value = parsed;
      *out = value;
      return true;
    }
    case EXPR_CAST: {
      IrValue *inner = NULL;
      if (!ir_lower_expr(program, ir, fun, expr->left, &inner)) return false;
      IrTypeKind cast_type = ir_type_kind(expr->resolved_type);
      if (!ir_type_is_value(cast_type) && cast_type != IR_TYPE_BOOL) {
        ir_free_value(inner);
        ir_mark_unsupported(ir, "direct backend cast target type is unsupported", expr->line, expr->column, expr->resolved_type ? expr->resolved_type : "unknown cast type");
        return false;
      }
      *out = ir_new_cast_value(ir, inner, cast_type, expr->line, expr->column);
      return true;
    }
    case EXPR_NUMBER: {
      IrTypeKind type = ir_type_kind(expr->resolved_type);
      if (!ir_type_is_value(type)) {
        ir_mark_unsupported(ir, "direct backend numeric expression type is unsupported", expr->line, expr->column, expr->resolved_type);
        return false;
      }
      unsigned long long parsed = 0;
      if (!ir_parse_integer_literal(expr->text, &parsed)) {
        ir_mark_unsupported(ir, "direct backend integer literal is malformed", expr->line, expr->column, expr->text);
        return false;
      }
      IrValue *value = ir_new_value(ir, IR_VALUE_INT, type, expr->line, expr->column);
      value->int_value = parsed;
      *out = value;
      return true;
    }
    case EXPR_NULL: {
      if (ir_type_kind(expr->resolved_type) == IR_TYPE_MAYBE_SCALAR) {
        IrTypeKind element = ir_maybe_scalar_element_type(expr->resolved_type);
        if (element == IR_TYPE_UNSUPPORTED) element = IR_TYPE_I32;
        *out = ir_new_maybe_scalar_literal(ir, false, element, 0, expr->line, expr->column);
        return true;
      }
      if (ir_type_kind(expr->resolved_type) == IR_TYPE_MAYBE_BYTE_VIEW) {
        *out = ir_new_maybe_byte_view_literal(ir, false, NULL, expr->line, expr->column);
        return true;
      }
      ir_mark_unsupported(ir, "direct backend null expression type is unsupported", expr->line, expr->column, expr->resolved_type ? expr->resolved_type : "unknown null type");
      return false;
    }
    case EXPR_IDENT: {
      const IrLocal *local = ir_function_find_local(fun, expr->text);
      if (!local) {
        const ConstDecl *const_decl = NULL;
        for (size_t i = 0; expr->text && i < program->consts.len; i++) {
          if (program->consts.items[i].name && strcmp(program->consts.items[i].name, expr->text) == 0) { const_decl = &program->consts.items[i]; break; }
        }
        if (const_decl && const_decl->expr) {
          if (ir->const_lower_depth >= 16) {
            ir_mark_unsupported(ir, "direct backend const reference chain is too deep", expr->line, expr->column, expr->text);
            return false;
          }
          ir->const_lower_depth++;
          bool lowered = ir_lower_expr(program, ir, fun, const_decl->expr, out);
          ir->const_lower_depth--;
          return lowered;
        }
        ir_mark_unsupported(ir, "direct backend identifier is not a local", expr->line, expr->column, expr->text);
        return false;
      }
      if (local->type == IR_TYPE_BYTE_VIEW) return ir_lower_byte_view(program, ir, fun, expr, out);
      IrValue *value = ir_new_value(ir, IR_VALUE_LOCAL, local->type, expr->line, expr->column);
      value->local_index = local->index;
      value->element_type = local->element_type;
      *out = value;
      return true;
    }
    case EXPR_MEMBER: {
      if (expr->left && expr->left->kind == EXPR_IDENT) {
        const EnumDecl *item_enum = ir_find_enum(program, expr->left->text);
        unsigned long long case_value = 0;
        if (item_enum && ir_enum_case_value(item_enum, expr->text, &case_value)) {
          IrTypeKind backing = ir_enum_backing_type(item_enum);
          if (!ir_type_is_value(backing)) {
            ir_mark_unsupported(ir, "direct backend enum backing type is unsupported", expr->line, expr->column, item_enum->type ? item_enum->type : "u8");
            return false;
          }
          IrValue *value = ir_new_value(ir, IR_VALUE_INT, backing, expr->line, expr->column);
          value->int_value = case_value;
          *out = value;
          return true;
        }
      }
      if (expr->left && expr->left->kind == EXPR_IDENT && strcmp(expr->text ? expr->text : "", "has") == 0) {
        const IrLocal *local = ir_function_find_local(fun, expr->left->text);
        if (local && (local->type == IR_TYPE_MAYBE_BYTE_VIEW || local->type == IR_TYPE_MAYBE_SCALAR)) {
          IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_HAS, IR_TYPE_BOOL, expr->line, expr->column);
          value->local_index = local->index;
          *out = value;
          return true;
        }
      }
      if (expr->left && expr->left->kind == EXPR_IDENT && strcmp(expr->text ? expr->text : "", "value") == 0) {
        const IrLocal *local = ir_function_find_local(fun, expr->left->text);
        if (local && local->type == IR_TYPE_MAYBE_SCALAR) {
          IrTypeKind value_type = ir_type_kind(expr->resolved_type);
          if (!(value_type == IR_TYPE_BOOL || ir_type_is_value(value_type))) value_type = IR_TYPE_I32;
          IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_VALUE, value_type, expr->line, expr->column);
          value->local_index = local->index;
          *out = value;
          return true;
        }
      }
      if (expr->left && expr->left->kind == EXPR_IDENT) {
        const IrLocal *local = ir_function_find_local(fun, expr->left->text);
        unsigned field_offset = 0;
        IrTypeKind field_type = IR_TYPE_UNSUPPORTED;
        IrTypeKind field_element_type = IR_TYPE_UNSUPPORTED;
        if (local && local->is_record && ir_shape_field_info(program, local->shape_name, expr->text, &field_offset, &field_type, &field_element_type)) {
          IrValue *value = ir_new_value(ir, IR_VALUE_FIELD_LOAD, field_type, expr->line, expr->column);
          value->local_index = local->index;
          value->field_offset = field_offset;
          value->element_type = field_type == IR_TYPE_BYTE_VIEW ? field_element_type : field_type;
          *out = value;
          return true;
        }
      }
      if (ir_expr_is_byte_view_source(expr)) return ir_lower_byte_view(program, ir, fun, expr, out);
      ir_mark_unsupported(ir, "direct backend member access supports Maybe<MutSpan<u8>> .has and .value", expr->line, expr->column, expr->text);
      return false;
    }
    case EXPR_STRING:
    case EXPR_SLICE:
      return ir_lower_byte_view(program, ir, fun, expr, out);
    case EXPR_INDEX: {
      if (expr->left && expr->left->kind == EXPR_MEMBER &&
          expr->left->left && expr->left->left->kind == EXPR_IDENT) {
        const IrLocal *record = ir_function_find_local(fun, expr->left->left->text);
        unsigned field_offset = 0;
        bool field_is_array = false;
        unsigned field_array_len = 0;
        IrTypeKind field_element_type = IR_TYPE_UNSUPPORTED;
        if (record && record->is_record &&
            ir_shape_field_storage_info(program, record->shape_name, expr->left->text, &field_offset, NULL, &field_is_array, &field_array_len, &field_element_type) &&
            field_is_array) {
          unsigned long long const_index = 0;
          if (!expr->right || expr->right->kind != EXPR_NUMBER ||
              !ir_parse_integer_literal(expr->right->text, &const_index)) {
            ir_mark_unsupported(ir, "direct backend record array field index currently requires a constant index", expr->right ? expr->right->line : expr->line, expr->right ? expr->right->column : expr->column, expr->left->text);
            return false;
          }
          if (const_index >= field_array_len) {
            ir_mark_unsupported(ir, "direct backend record array field index is out of bounds", expr->right->line, expr->right->column, expr->left->text);
            return false;
          }
          IrValue *value = ir_new_value(ir, IR_VALUE_FIELD_LOAD, field_element_type, expr->line, expr->column);
          value->local_index = record->index;
          value->field_offset = field_offset + (unsigned)const_index * ir_type_byte_size(field_element_type);
          *out = value;
          return true;
        }
      }
      if (ir_expr_is_byte_view_source(expr->left)) {
        IrValue *view = NULL;
        IrValue *index = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->left, &view) ||
            !ir_lower_expr(program, ir, fun, expr->right, &index)) {
          ir_free_value(view);
          ir_free_value(index);
          return false;
        }
        if (!ir_type_is_value(index->type)) {
          ir_free_value(view);
          ir_free_value(index);
          ir_mark_unsupported(ir, "direct backend string index must be an integer value", expr->line, expr->column, "non-integer index");
          return false;
        }
        IrTypeKind element_type = view->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : view->element_type;
        IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_INDEX_LOAD, element_type, expr->line, expr->column);
        value->left = view;
        value->index = index;
        value->element_type = element_type;
        *out = value;
        return true;
      }
      if (!expr->left || expr->left->kind != EXPR_IDENT) {
        ir_mark_unsupported(ir, "direct backend indexing currently supports only local fixed arrays", expr->line, expr->column, "non-local index base");
        return false;
      }
      const IrLocal *local = ir_function_find_local(fun, expr->left->text);
      if (!local || !local->is_array) {
        ir_mark_unsupported(ir, "direct backend indexing currently supports only fixed array locals", expr->line, expr->column, expr->left->text);
        return false;
      }
      IrValue *index = NULL;
      if (!ir_lower_expr(program, ir, fun, expr->right, &index)) return false;
      if (!ir_type_is_value(index->type)) {
        ir_free_value(index);
        ir_mark_unsupported(ir, "direct backend array index must be an integer value", expr->line, expr->column, "non-integer index");
        return false;
      }
      IrValue *value = ir_new_value(ir, IR_VALUE_INDEX_LOAD, local->element_type, expr->line, expr->column);
      value->array_index = local->index;
      value->index = index;
      *out = value;
      return true;
    }
    case EXPR_CALL: {
      char *callee_name = ir_expr_callee_name(expr->left);
      IrDirectStdCallId std_call = ir_direct_std_call_id(callee_name);
      bool handled = false;
      if (ir_lower_c_import_call(program, ir, fun, expr, out)) {
        free(callee_name);
        return *out != NULL;
      }
      if ((strcmp(callee_name, "std.codec.readU8") == 0 ||
           strcmp(callee_name, "std.codec.readU16") == 0 ||
           strcmp(callee_name, "std.codec.readU32") == 0) &&
          expr->args.len == 1) {
        const unsigned char *bytes = NULL;
        size_t len = 0;
        if (!ir_string_literal_bytes(expr->args.items[0], &bytes, &len)) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.codec.readU* currently requires literal bytes", expr->line, expr->column, "non-literal codec input");
          return false;
        }
        IrTypeKind type = IR_TYPE_U8;
        unsigned needed = 1;
        unsigned long long number = bytes[0];
        if (strcmp(callee_name, "std.codec.readU16") == 0) {
          type = IR_TYPE_U16;
          needed = 2;
          number = len >= 2 ? ((unsigned long long)bytes[0] | ((unsigned long long)bytes[1] << 8)) : 0;
        } else if (strcmp(callee_name, "std.codec.readU32") == 0) {
          type = IR_TYPE_U32;
          needed = 4;
          number = len >= 4 ? ((unsigned long long)bytes[0] |
                               ((unsigned long long)bytes[1] << 8) |
                               ((unsigned long long)bytes[2] << 16) |
                               ((unsigned long long)bytes[3] << 24)) : 0;
        }
        if (len < needed) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.codec.readU* input is too short", expr->line, expr->column, "short codec input");
          return false;
        }
        free(callee_name);
        *out = ir_new_integer_literal_value(ir, type, number, expr->line, expr->column);
        return true;
      }
      if (strcmp(callee_name, "std.codec.encodedVarintLen") == 0 && expr->args.len == 1) {
        unsigned long long number = 0;
        if (expr->args.items[0] && expr->args.items[0]->kind == EXPR_NUMBER &&
            ir_parse_integer_literal(expr->args.items[0]->text, &number)) {
          unsigned long long len = 1;
          while (number >= 128ull) {
            number >>= 7;
            len++;
          }
          free(callee_name);
          *out = ir_new_integer_literal_value(ir, IR_TYPE_USIZE, len, expr->line, expr->column);
          return true;
        }
      }
      if (strncmp(callee_name, "std.parse.", strlen("std.parse.")) == 0 && expr->args.len == 1) {
        const unsigned char *bytes = NULL;
        size_t len = 0;
        if (ir_string_literal_bytes(expr->args.items[0], &bytes, &len)) {
          unsigned char first = len > 0 ? bytes[0] : 0;
          if (strcmp(callee_name, "std.parse.isAsciiDigit") == 0) {
            bool ok = first >= '0' && first <= '9';
            free(callee_name);
            *out = ir_new_bool_literal(ir, ok, expr->line, expr->column);
            return true;
          }
          if (strcmp(callee_name, "std.parse.isAsciiAlpha") == 0) {
            bool ok = (first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z');
            free(callee_name);
            *out = ir_new_bool_literal(ir, ok, expr->line, expr->column);
            return true;
          }
          if (strcmp(callee_name, "std.parse.isIdentifierStart") == 0) {
            bool ok = (first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_';
            free(callee_name);
            *out = ir_new_bool_literal(ir, ok, expr->line, expr->column);
            return true;
          }
          if (strcmp(callee_name, "std.parse.isWhitespace") == 0) {
            bool ok = first == ' ' || first == '\n' || first == '\r' || first == '\t';
            free(callee_name);
            *out = ir_new_bool_literal(ir, ok, expr->line, expr->column);
            return true;
          }
          if (strcmp(callee_name, "std.parse.scanDigits") == 0) {
            free(callee_name);
            *out = ir_new_integer_literal_value(ir, IR_TYPE_USIZE, ir_scan_digits_bytes(bytes, len), expr->line, expr->column);
            return true;
          }
          if (strcmp(callee_name, "std.parse.scanIdentifier") == 0) {
            free(callee_name);
            *out = ir_new_integer_literal_value(ir, IR_TYPE_USIZE, ir_scan_identifier_bytes(bytes, len), expr->line, expr->column);
            return true;
          }
          if (strcmp(callee_name, "std.parse.parseU8") == 0 ||
              strcmp(callee_name, "std.parse.parseU16") == 0 ||
              strcmp(callee_name, "std.parse.parseU32") == 0) {
            unsigned long long max = UINT32_MAX;
            IrTypeKind element_type = IR_TYPE_U32;
            if (strcmp(callee_name, "std.parse.parseU8") == 0) {
              max = UINT8_MAX;
              element_type = IR_TYPE_U8;
            } else if (strcmp(callee_name, "std.parse.parseU16") == 0) {
              max = UINT16_MAX;
              element_type = IR_TYPE_U16;
            }
            unsigned long long number = 0;
            bool ok = ir_parse_decimal_bytes(bytes, len, max, &number);
            free(callee_name);
            *out = ir_new_maybe_scalar_literal(ir, ok, element_type, number, expr->line, expr->column);
            return true;
          }
        }
      }
      if (strcmp(callee_name, "std.mem.fixedBufAlloc") == 0 &&
          expr->args.len == 1 &&
          ir_expr_is_mutable_byte_view_dest(program, fun, expr->args.items[0])) {
        IrValue *bytes = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &bytes)) {
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_FIXED_BUF_ALLOC, IR_TYPE_ALLOC, expr->line, expr->column);
        value->left = bytes;
        ir->direct_allocator_helper_count = ir->direct_allocator_helper_count < 1 ? 1 : ir->direct_allocator_helper_count;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.mem.allocBytes") == 0 &&
          expr->args.len == 2 &&
          expr->args.items[0] &&
          expr->args.items[0]->kind == EXPR_IDENT) {
        const IrLocal *alloc = ir_function_find_local(fun, expr->args.items[0]->text);
        if (!alloc || alloc->type != IR_TYPE_ALLOC || !alloc->is_mutable) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem.allocBytes expects a mutable FixedBufAlloc local", expr->args.items[0]->line, expr->args.items[0]->column, "non-mutable allocator");
          return false;
        }
        IrValue *len = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[1], &len)) {
          free(callee_name);
          return false;
        }
        if (!ir_type_is_value(len->type)) {
          ir_free_value(len);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem.allocBytes length must be an integer value", expr->args.items[1]->line, expr->args.items[1]->column, "non-integer length");
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_ALLOC_BYTES, IR_TYPE_MAYBE_BYTE_VIEW, expr->line, expr->column);
        value->local_index = alloc->index;
        value->left = len;
        ir->direct_allocator_helper_count = ir->direct_allocator_helper_count < 2 ? 2 : ir->direct_allocator_helper_count;
        free(callee_name);
        *out = value;
        return true;
      }
      unsigned long long time_scale = 0;
      if (expr->args.len == 1 && ir_std_time_duration_scale(callee_name, &time_scale)) {
        bool ok = ir_lower_time_duration_constructor(program, ir, fun, expr->args.items[0], time_scale, expr->line, expr->column, out);
        free(callee_name);
        return ok;
      }
      IrBinaryOp time_op = IR_BIN_ADD;
      if (expr->args.len == 2 && ir_std_time_binary_op(callee_name, &time_op)) {
        IrValue *left = NULL;
        IrValue *right = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[0], &left) ||
            !ir_lower_expr(program, ir, fun, expr->args.items[1], &right)) {
          ir_free_value(left);
          ir_free_value(right);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_binary_value(ir, time_op, IR_TYPE_I64, left, right, expr->line, expr->column);
        free(callee_name);
        *out = value;
        return true;
      }
      unsigned long long time_divisor = 0;
      IrTypeKind time_result_type = IR_TYPE_I64;
      if (expr->args.len == 1 && ir_std_time_conversion(callee_name, &time_divisor, &time_result_type)) {
        bool ok = ir_lower_time_duration_div_floor(program, ir, fun, expr->args.items[0], time_divisor, time_result_type, expr->line, expr->column, out);
        free(callee_name);
        return ok;
      }
      if (!ir_lower_std_time_extra_call(program, ir, fun, expr, callee_name, &handled, out)) {
        free(callee_name);
        return false;
      }
      if (handled) {
        free(callee_name);
        return true;
      }
      if (strcmp(callee_name, "std.time.lessThan") == 0 && expr->args.len == 2) {
        IrValue *left = NULL;
        IrValue *right = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[0], &left) ||
            !ir_lower_expr(program, ir, fun, expr->args.items[1], &right)) {
          ir_free_value(left);
          ir_free_value(right);
          free(callee_name);
          return false;
        }
        left = ir_new_cast_value(ir, left, IR_TYPE_I64, expr->line, expr->column);
        right = ir_new_cast_value(ir, right, IR_TYPE_I64, expr->line, expr->column);
        IrValue *value = ir_new_compare_value(ir, IR_CMP_LT, left, right, expr->line, expr->column);
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.rand.seed") == 0 && expr->args.len == 1) {
        IrValue *seed = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[0], &seed)) {
          free(callee_name);
          return false;
        }
        free(callee_name);
        *out = seed;
        return true;
      }
      if (strcmp(callee_name, "std.rand.nextU32") == 0 && expr->args.len == 1) {
        const IrLocal *rng = ir_find_mutable_rand_source_local(ir, fun, expr->args.items[0], callee_name);
        if (!rng) {
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_RAND_NEXT_U32, IR_TYPE_U32, expr->line, expr->column);
        value->local_index = rng->index;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.rand.nextBool") == 0 && expr->args.len == 1) {
        const IrLocal *rng = ir_find_mutable_rand_source_local(ir, fun, expr->args.items[0], callee_name);
        if (!rng) {
          free(callee_name);
          return false;
        }
        IrValue *next = ir_new_value(ir, IR_VALUE_RAND_NEXT_U32, IR_TYPE_U32, expr->line, expr->column);
        next->local_index = rng->index;
        IrValue *value = ir_new_compare_value(ir, IR_CMP_GE, next, ir_new_integer_literal_value(ir, IR_TYPE_U32, 2147483648u, expr->line, expr->column), expr->line, expr->column);
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.rand.nextBelow") == 0 && expr->args.len == 2) {
        const IrLocal *rng = ir_find_mutable_rand_source_local(ir, fun, expr->args.items[0], callee_name);
        if (!rng) {
          free(callee_name);
          return false;
        }
        IrValue *bound = NULL;
        if (!ir_lower_call_arg(program, ir, fun, expr->args.items[1], IR_TYPE_U32, &bound)) {
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_RAND_NEXT_BELOW, IR_TYPE_MAYBE_SCALAR, expr->line, expr->column);
        value->element_type = IR_TYPE_U32;
        value->local_index = rng->index;
        value->left = bound;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.rand.rangeU32") == 0 && expr->args.len == 3) {
        const IrLocal *rng = ir_find_mutable_rand_source_local(ir, fun, expr->args.items[0], callee_name);
        if (!rng) {
          free(callee_name);
          return false;
        }
        IrValue *low = NULL;
        IrValue *high = NULL;
        if (!ir_lower_call_arg(program, ir, fun, expr->args.items[1], IR_TYPE_U32, &low) ||
            !ir_lower_call_arg(program, ir, fun, expr->args.items[2], IR_TYPE_U32, &high)) {
          ir_free_value(low);
          ir_free_value(high);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_RAND_RANGE_U32, IR_TYPE_MAYBE_SCALAR, expr->line, expr->column);
        value->element_type = IR_TYPE_U32;
        value->local_index = rng->index;
        value->left = low;
        value->right = high;
        free(callee_name);
        *out = value;
        return true;
      }
      if (expr->args.len == 0 && ir_is_std_rand_entropy_call(callee_name)) {
        IrValue *value = ir_new_value(ir, IR_VALUE_RAND_ENTROPY_U32, IR_TYPE_U32, expr->line, expr->column);
        free(callee_name);
        *out = value;
        return true;
      }
      if (!ir_lower_std_proc_direct_call(program, ir, fun, expr, std_call, &handled, out)) {
        free(callee_name);
        return false;
      }
      if (handled) {
        free(callee_name);
        return true;
      }
      {
        IrMathOp math_op = IR_MATH_OP_MIN_I32;
        IrTypeKind math_arg_type = IR_TYPE_UNSUPPORTED;
        IrTypeKind math_return_type = IR_TYPE_UNSUPPORTED;
        IrTypeKind math_return_element_type = IR_TYPE_UNSUPPORTED;
        size_t math_expected_args = 0;
        if (ir_std_math_runtime_spec(callee_name, expr->args.len, &math_op, &math_arg_type, &math_return_type, &math_return_element_type, &math_expected_args)) {
          bool ok = ir_make_std_math_runtime_value(program, ir, fun, expr, math_op, math_arg_type, math_return_type, math_return_element_type, math_expected_args, out);
          free(callee_name);
          return ok;
        }
      }
      if ((strcmp(callee_name, "std.math.isEvenU32") == 0 || strcmp(callee_name, "std.math.isOddU32") == 0) && expr->args.len == 1) {
        IrValue *number = NULL;
        if (!ir_lower_call_arg(program, ir, fun, expr->args.items[0], IR_TYPE_U32, &number)) {
          free(callee_name);
          return false;
        }
        if (!number || number->type != IR_TYPE_U32) {
          ir_free_value(number);
          ir_mark_unsupported(ir, "direct backend std.math parity helper expects u32", expr->args.items[0] ? expr->args.items[0]->line : expr->line, expr->args.items[0] ? expr->args.items[0]->column : expr->column, expr->args.items[0] && expr->args.items[0]->resolved_type ? expr->args.items[0]->resolved_type : "unknown parity argument");
          free(callee_name);
          return false;
        }
        IrValue *remainder = ir_new_binary_value(ir, IR_BIN_MOD, IR_TYPE_U32, number, ir_new_integer_literal_value(ir, IR_TYPE_U32, 2, expr->line, expr->column), expr->line, expr->column);
        IrCompareOp op = strcmp(callee_name, "std.math.isEvenU32") == 0 ? IR_CMP_EQ : IR_CMP_NE;
        *out = ir_new_compare_value(ir, op, remainder, ir_new_integer_literal_value(ir, IR_TYPE_U32, 0, expr->line, expr->column), expr->line, expr->column);
        free(callee_name);
        return true;
      }
      if ((strcmp(callee_name, "std.codec.crc32") == 0 ||
           strcmp(callee_name, "std.codec.crc32Bytes") == 0 ||
           strcmp(callee_name, "std.crypto.hash32") == 0) && expr->args.len == 1) {
        IrValue *view = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &view)) {
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_CRC32_BYTES, IR_TYPE_U32, expr->line, expr->column);
        value->left = view;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.crypto.hmac32") == 0 && expr->args.len == 2) {
        IrValue *left = NULL;
        IrValue *right = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &left) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &right)) {
          ir_free_value(left);
          ir_free_value(right);
          free(callee_name);
          return false;
        }
        IrValue *left_len = ir_new_value(ir, IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_U32, expr->line, expr->column);
        left_len->left = left;
        IrValue *right_len = ir_new_value(ir, IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_U32, expr->line, expr->column);
        right_len->left = right;
        IrValue *value = ir_new_value(ir, IR_VALUE_BINARY, IR_TYPE_U32, expr->line, expr->column);
        value->binary_op = IR_BIN_ADD;
        value->left = left_len;
        value->right = right_len;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.crypto.constantTimeEql") == 0 && expr->args.len == 2) {
        IrValue *left = NULL;
        IrValue *right = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &left) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &right)) {
          ir_free_value(left);
          ir_free_value(right);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_EQ, IR_TYPE_BOOL, expr->line, expr->column);
        value->left = left;
        value->right = right;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.json.parse") == 0 ||
           strcmp(callee_name, "std.json.parseBytes") == 0) &&
          expr->args.len == 2) {
        if (!expr->args.items[0] || expr->args.items[0]->kind != EXPR_IDENT) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.json.parse expects a mutable FixedBufAlloc local", expr->args.items[0] ? expr->args.items[0]->line : expr->line, expr->args.items[0] ? expr->args.items[0]->column : expr->column, "non-local allocator");
          return false;
        }
        const IrLocal *alloc = ir_function_find_local(fun, expr->args.items[0]->text);
        if (!alloc || alloc->type != IR_TYPE_ALLOC || !alloc->is_mutable) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.json.parse expects a mutable FixedBufAlloc local", expr->args.items[0]->line, expr->args.items[0]->column, "non-mutable allocator");
          return false;
        }
        IrValue *view = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[1], &view)) {
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_JSON_PARSE_BYTES, IR_TYPE_I64, expr->line, expr->column);
        value->local_index = alloc->index;
        value->left = view;
        ir->direct_allocator_helper_count = ir->direct_allocator_helper_count < 2 ? 2 : ir->direct_allocator_helper_count;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.json.validate") == 0 ||
           strcmp(callee_name, "std.json.validateBytes") == 0 ||
           strcmp(callee_name, "std.json.streamTokens") == 0 ||
           strcmp(callee_name, "std.json.streamTokensBytes") == 0 ||
           strcmp(callee_name, "std.json.validateError") == 0 ||
           strcmp(callee_name, "std.json.errorOffset") == 0 ||
           strcmp(callee_name, "std.json.errorLine") == 0 ||
           strcmp(callee_name, "std.json.errorColumn") == 0) &&
          expr->args.len == 1) {
        IrValue *view = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &view)) {
          free(callee_name);
          return false;
        }
        bool validate = strcmp(callee_name, "std.json.validate") == 0 || strcmp(callee_name, "std.json.validateBytes") == 0;
        bool stream = strcmp(callee_name, "std.json.streamTokens") == 0 || strcmp(callee_name, "std.json.streamTokensBytes") == 0;
        IrValueKind kind = validate ? IR_VALUE_JSON_VALIDATE_BYTES : (stream ? IR_VALUE_JSON_STREAM_TOKENS_BYTES : IR_VALUE_JSON_DIAGNOSTIC_BYTES);
        IrTypeKind type = validate ? IR_TYPE_BOOL : (stream ? IR_TYPE_USIZE : (strcmp(callee_name, "std.json.validateError") == 0 ? IR_TYPE_U32 : IR_TYPE_USIZE));
        IrValue *value = ir_new_value(ir, kind, type, expr->line, expr->column);
        if (kind == IR_VALUE_JSON_DIAGNOSTIC_BYTES) {
          if (strcmp(callee_name, "std.json.validateError") == 0) value->int_value = 0;
          else if (strcmp(callee_name, "std.json.errorOffset") == 0) value->int_value = 1;
          else if (strcmp(callee_name, "std.json.errorLine") == 0) value->int_value = 2;
          else value->int_value = 3;
        }
        value->left = view;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.json.field") == 0 && expr->args.len == 2) {
        IrValue *input = NULL;
        IrValue *key = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &input) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &key)) {
          ir_free_value(input);
          ir_free_value(key);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_JSON_FIELD, IR_TYPE_MAYBE_BYTE_VIEW, expr->line, expr->column);
        value->left = input;
        value->right = key;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.json.u32") == 0 ||
           strcmp(callee_name, "std.json.bool") == 0) &&
          expr->args.len == 2) {
        IrValue *input = NULL;
        IrValue *key = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &input) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &key)) {
          ir_free_value(input);
          ir_free_value(key);
          free(callee_name);
          return false;
        }
        IrTypeKind element = strcmp(callee_name, "std.json.bool") == 0 ? IR_TYPE_BOOL : IR_TYPE_U32;
        IrValue *value = ir_new_value(ir, IR_VALUE_JSON_LOOKUP_SCALAR, IR_TYPE_MAYBE_SCALAR, expr->line, expr->column);
        value->element_type = element;
        value->int_value = element == IR_TYPE_BOOL ? 1 : 0;
        value->left = input;
        value->right = key;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.json.stringDecode") == 0 && expr->args.len == 2) {
        IrValue *buffer = NULL;
        IrValue *raw = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &buffer) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &raw)) {
          ir_free_value(buffer);
          ir_free_value(raw);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_JSON_STRING_DECODE, IR_TYPE_MAYBE_BYTE_VIEW, expr->line, expr->column);
        value->left = buffer;
        value->right = raw;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.json.writeStringBytes") == 0 && expr->args.len == 2) {
        IrValue *buffer = NULL;
        IrValue *text = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &buffer) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &text)) {
          ir_free_value(buffer);
          ir_free_value(text);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_JSON_WRITE_STRING, IR_TYPE_MAYBE_BYTE_VIEW, expr->line, expr->column);
        value->left = buffer;
        value->right = text;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      IrJsonWriteOp json_write_op = IR_JSON_WRITE_FIELD_RAW;
      bool json_write = true;
      if (strcmp(callee_name, "std.json.writeFieldRaw") == 0 && expr->args.len == 3) json_write_op = IR_JSON_WRITE_FIELD_RAW;
      else if (strcmp(callee_name, "std.json.writeFieldString") == 0 && expr->args.len == 3) json_write_op = IR_JSON_WRITE_FIELD_STRING;
      else if (strcmp(callee_name, "std.json.writeFieldU32") == 0 && expr->args.len == 3) json_write_op = IR_JSON_WRITE_FIELD_U32;
      else if (strcmp(callee_name, "std.json.writeFieldBool") == 0 && expr->args.len == 3) json_write_op = IR_JSON_WRITE_FIELD_BOOL;
      else if (strcmp(callee_name, "std.json.writeObject1String") == 0 && expr->args.len == 3) json_write_op = IR_JSON_WRITE_OBJECT1_STRING;
      else if (strcmp(callee_name, "std.json.writeObject1U32") == 0 && expr->args.len == 3) json_write_op = IR_JSON_WRITE_OBJECT1_U32;
      else if (strcmp(callee_name, "std.json.writeObject1Bool") == 0 && expr->args.len == 3) json_write_op = IR_JSON_WRITE_OBJECT1_BOOL;
      else if (strcmp(callee_name, "std.json.writeObject2Fields") == 0 && expr->args.len == 3) json_write_op = IR_JSON_WRITE_OBJECT2_FIELDS;
      else if (strcmp(callee_name, "std.json.writeObject2StringField") == 0 && expr->args.len == 4) json_write_op = IR_JSON_WRITE_OBJECT2_STRING_FIELD;
      else if (strcmp(callee_name, "std.json.writeObject2U32Field") == 0 && expr->args.len == 4) json_write_op = IR_JSON_WRITE_OBJECT2_U32_FIELD;
      else if (strcmp(callee_name, "std.json.writeObject2BoolField") == 0 && expr->args.len == 4) json_write_op = IR_JSON_WRITE_OBJECT2_BOOL_FIELD;
      else if (strcmp(callee_name, "std.json.writeArray2Strings") == 0 && expr->args.len == 3) json_write_op = IR_JSON_WRITE_ARRAY2_STRINGS;
      else if (strcmp(callee_name, "std.json.writeArray2U32") == 0 && expr->args.len == 3) json_write_op = IR_JSON_WRITE_ARRAY2_U32;
      else if (strcmp(callee_name, "std.json.writeArray2Bools") == 0 && expr->args.len == 3) json_write_op = IR_JSON_WRITE_ARRAY2_BOOLS;
      else json_write = false;
      if (json_write) {
        IrValue *value = ir_new_value(ir, IR_VALUE_JSON_WRITE_RUNTIME, IR_TYPE_MAYBE_BYTE_VIEW, expr->line, expr->column);
        value->int_value = (unsigned long long)json_write_op;
        for (size_t i = 0; i < expr->args.len; i++) {
          IrTypeKind type = IR_TYPE_BYTE_VIEW;
          if ((json_write_op == IR_JSON_WRITE_FIELD_U32 || json_write_op == IR_JSON_WRITE_OBJECT1_U32) && i == 2) type = IR_TYPE_U32;
          if ((json_write_op == IR_JSON_WRITE_FIELD_BOOL || json_write_op == IR_JSON_WRITE_OBJECT1_BOOL) && i == 2) type = IR_TYPE_BOOL;
          if ((json_write_op == IR_JSON_WRITE_OBJECT2_U32_FIELD || json_write_op == IR_JSON_WRITE_OBJECT2_BOOL_FIELD) && i == 2) type = json_write_op == IR_JSON_WRITE_OBJECT2_U32_FIELD ? IR_TYPE_U32 : IR_TYPE_BOOL;
          if ((json_write_op == IR_JSON_WRITE_ARRAY2_U32 || json_write_op == IR_JSON_WRITE_ARRAY2_BOOLS) && i > 0) type = json_write_op == IR_JSON_WRITE_ARRAY2_U32 ? IR_TYPE_U32 : IR_TYPE_BOOL;
          IrValue *arg = NULL;
          bool ok = type == IR_TYPE_BYTE_VIEW ?
            ir_lower_byte_view(program, ir, fun, expr->args.items[i], &arg) :
            ir_lower_call_arg(program, ir, fun, expr->args.items[i], type, &arg);
          if (!ok) {
            ir_free_value(value);
            free(callee_name);
            return false;
          }
          ir_value_push_arg(ir, value, arg);
        }
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.json.string") == 0 && expr->args.len == 3) {
        IrValue *buffer = NULL;
        IrValue *input = NULL;
        IrValue *key = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &buffer) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &input) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[2], &key)) {
          ir_free_value(buffer);
          ir_free_value(input);
          ir_free_value(key);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_JSON_STRING_FIELD, IR_TYPE_MAYBE_BYTE_VIEW, expr->line, expr->column);
        value->left = buffer;
        value->right = input;
        value->index = key;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.json.decodeBoundary") == 0 && expr->args.len == 0) {
        IrValue *value = NULL;
        if (!ir_make_string_literal_value(ir, "typed-decode-explicit-shape", expr->line, expr->column, &value)) {
          free(callee_name);
          return false;
        }
        free(callee_name);
        *out = value;
        return true;
      }
      bool json_error_expected = strcmp(callee_name, "std.json.errorExpected") == 0;
      if ((json_error_expected || strcmp(callee_name, "std.json.errorName") == 0) && expr->args.len == 1) {
        IrValue *code = NULL;
        if (!ir_lower_call_arg(program, ir, fun, expr->args.items[0], IR_TYPE_U32, &code)) {
          free(callee_name);
          return false;
        }
        if (code && code->kind == IR_VALUE_INT) {
          const char *label = ir_std_json_error_label(code->int_value, json_error_expected);
          ir_free_value(code);
          bool ok = ir_make_string_literal_value(ir, label, expr->line, expr->column, out);
          free(callee_name);
          return ok;
        }
        bool ok = ir_make_json_error_label_value(ir, code, json_error_expected, expr->line, expr->column, out);
        free(callee_name);
        return ok;
      }
      int json_error_code = ir_std_json_error_code(callee_name);
      if (json_error_code >= 0 && expr->args.len == 0) {
        free(callee_name);
        *out = ir_new_integer_literal_value(ir, IR_TYPE_U32, (unsigned long long)json_error_code, expr->line, expr->column);
        return true;
      }
      if (strcmp(callee_name, "std.net.host") == 0 && expr->args.len == 0) {
        IrValue *value = ir_new_value(ir, IR_VALUE_INT, IR_TYPE_I32, expr->line, expr->column);
        value->int_value = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      int http_error_code = ir_std_http_error_code(callee_name);
      if (http_error_code >= 0 && expr->args.len == 0) {
        IrValue *value = ir_new_value(ir, IR_VALUE_INT, IR_TYPE_U32, expr->line, expr->column);
        value->int_value = (unsigned long long)http_error_code;
        free(callee_name);
        *out = value;
        return true;
      }
      unsigned http_status_lower = 0;
      unsigned http_status_upper = 0;
      if (ir_std_http_status_class_bounds(callee_name, &http_status_lower, &http_status_upper) && expr->args.len == 1) {
        IrValue *status = NULL;
        if (!ir_lower_call_arg(program, ir, fun, expr->args.items[0], IR_TYPE_U16, &status)) {
          free(callee_name);
          return false;
        }
        if (!status || status->type != IR_TYPE_U16) {
          ir_free_value(status);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.http status predicate expects a u16 status", expr->args.items[0] ? expr->args.items[0]->line : expr->line, expr->args.items[0] ? expr->args.items[0]->column : expr->column, "non-u16 status");
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_HTTP_STATUS_CLASS, IR_TYPE_BOOL, expr->line, expr->column);
        value->left = status;
        value->int_value = http_status_lower;
        value->data_len = http_status_upper;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.http.client") == 0 && expr->args.len == 1) {
        IrValue *net = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[0], &net)) {
          free(callee_name);
          return false;
        }
        ir_free_value(net);
        IrValue *value = ir_new_value(ir, IR_VALUE_INT, IR_TYPE_I32, expr->line, expr->column);
        value->int_value = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.http.listen") == 0 && (expr->args.len == 1 || expr->args.len == 2)) {
        IrValue *value = ir_new_integer_literal_value(ir, IR_TYPE_I64, 0, expr->line, expr->column);
        value->element_type = IR_TYPE_VOID;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.http.fetch") == 0 && expr->args.len == 4) {
        IrValue *client = NULL;
        IrValue *request = NULL;
        IrValue *response = NULL;
        IrValue *timeout = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[0], &client) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &request) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[2], &response) ||
            !ir_lower_expr(program, ir, fun, expr->args.items[3], &timeout)) {
          ir_free_value(client);
          ir_free_value(request);
          ir_free_value(response);
          ir_free_value(timeout);
          free(callee_name);
          return false;
        }
        ir_free_value(client);
        if (timeout->type != IR_TYPE_I64) {
          ir_free_value(request);
          ir_free_value(response);
          ir_free_value(timeout);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.http.fetch timeout must be a Duration", expr->args.items[3]->line, expr->args.items[3]->column, "non-Duration timeout");
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_HTTP_FETCH, IR_TYPE_U64, expr->line, expr->column);
        value->left = request;
        value->right = response;
        value->index = timeout;
        if (ir->direct_runtime_helper_count < 2) ir->direct_runtime_helper_count = 2;
        if (ir->direct_host_runtime_import_count < 2) ir->direct_host_runtime_import_count = 2;
        if (ir->direct_http_runtime_import_count < 1) ir->direct_http_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.http.resultOk") == 0 ||
           strcmp(callee_name, "std.http.resultStatus") == 0 ||
           strcmp(callee_name, "std.http.resultBodyLen") == 0 ||
           strcmp(callee_name, "std.http.resultError") == 0) && expr->args.len == 1) {
        IrValue *result = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[0], &result)) {
          free(callee_name);
          return false;
        }
        if (result->type != IR_TYPE_U64) {
          ir_free_value(result);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend HTTP result helper expects HttpResult", expr->args.items[0]->line, expr->args.items[0]->column, "non-HttpResult argument");
          return false;
        }
        IrValueKind kind = IR_VALUE_HTTP_RESULT_ERROR;
        IrTypeKind type = IR_TYPE_U32;
        if (strcmp(callee_name, "std.http.resultOk") == 0) {
          kind = IR_VALUE_HTTP_RESULT_OK;
          type = IR_TYPE_BOOL;
        } else if (strcmp(callee_name, "std.http.resultStatus") == 0) {
          kind = IR_VALUE_HTTP_RESULT_STATUS;
          type = IR_TYPE_U16;
        } else if (strcmp(callee_name, "std.http.resultBodyLen") == 0) {
          kind = IR_VALUE_HTTP_RESULT_BODY_LEN;
          type = IR_TYPE_USIZE;
        }
        IrValue *value = ir_new_value(ir, kind, type, expr->line, expr->column);
        value->left = result;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.http.responseLen") == 0 ||
           strcmp(callee_name, "std.http.responseHeadersLen") == 0 ||
           strcmp(callee_name, "std.http.responseBodyOffset") == 0) && expr->args.len == 1) {
        IrValue *response = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &response)) {
          free(callee_name);
          return false;
        }
        IrValueKind kind = IR_VALUE_HTTP_RESPONSE_LEN;
        if (strcmp(callee_name, "std.http.responseHeadersLen") == 0) {
          kind = IR_VALUE_HTTP_RESPONSE_HEADERS_LEN;
        } else if (strcmp(callee_name, "std.http.responseBodyOffset") == 0) {
          kind = IR_VALUE_HTTP_RESPONSE_BODY_OFFSET;
        }
        IrValue *value = ir_new_value(ir, kind, IR_TYPE_USIZE, expr->line, expr->column);
        value->left = response;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.http.headerValue") == 0 && expr->args.len == 2) {
        IrValue *headers = NULL;
        IrValue *name = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &headers) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &name)) {
          ir_free_value(headers);
          ir_free_value(name);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_HTTP_HEADER_VALUE, IR_TYPE_U64, expr->line, expr->column);
        value->left = headers;
        value->right = name;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.http.requestMethodName") == 0 ||
           strcmp(callee_name, "std.http.requestPath") == 0) && expr->args.len == 1) {
        IrValue *request = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &request)) {
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, strcmp(callee_name, "std.http.requestMethodName") == 0 ? IR_VALUE_HTTP_REQUEST_METHOD_NAME : IR_VALUE_HTTP_REQUEST_PATH, IR_TYPE_MAYBE_BYTE_VIEW, expr->line, expr->column);
        value->left = request;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.http.writeJsonResponse") == 0 && expr->args.len == 3) {
        IrValue *buffer = NULL;
        IrValue *status = NULL;
        IrValue *body = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &buffer) ||
            !ir_lower_expr(program, ir, fun, expr->args.items[1], &status) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[2], &body)) {
          ir_free_value(buffer);
          ir_free_value(status);
          ir_free_value(body);
          free(callee_name);
          return false;
        }
        if (status->type != IR_TYPE_U16) {
          ir_free_value(buffer);
          ir_free_value(status);
          ir_free_value(body);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.http.writeJsonResponse status must be u16", expr->args.items[1]->line, expr->args.items[1]->column, "non-u16 status");
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_HTTP_WRITE_JSON_RESPONSE, IR_TYPE_MAYBE_BYTE_VIEW, expr->line, expr->column);
        value->left = buffer;
        value->index = status;
        value->right = body;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.http.headerFound") == 0 ||
           strcmp(callee_name, "std.http.headerOffset") == 0 ||
           strcmp(callee_name, "std.http.headerLen") == 0) && expr->args.len == 1) {
        IrValue *header = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[0], &header)) {
          free(callee_name);
          return false;
        }
        if (header->type != IR_TYPE_U64) {
          ir_free_value(header);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend HTTP header helper expects HttpHeaderValue", expr->args.items[0]->line, expr->args.items[0]->column, "non-HttpHeaderValue argument");
          return false;
        }
        IrValueKind kind = IR_VALUE_HTTP_HEADER_LEN;
        IrTypeKind type = IR_TYPE_USIZE;
        if (strcmp(callee_name, "std.http.headerFound") == 0) {
          kind = IR_VALUE_HTTP_HEADER_FOUND;
          type = IR_TYPE_BOOL;
        } else if (strcmp(callee_name, "std.http.headerOffset") == 0) {
          kind = IR_VALUE_HTTP_HEADER_OFFSET;
        }
        IrValue *value = ir_new_value(ir, kind, type, expr->line, expr->column);
        value->left = header;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      IrStdTermHelper term_helper = ir_std_term_helper(callee_name);
      if (term_helper.sequence && expr->args.len == 0) {
        bool ok = ir_make_string_literal_value(ir, term_helper.sequence, expr->line, expr->column, out);
        free(callee_name);
        return ok;
      }
      if (expr->args.len == 0 && term_helper.has_key_code) {
        *out = ir_new_integer_literal_value(ir, IR_TYPE_U32, term_helper.key_code, expr->line, expr->column);
        free(callee_name);
        return true;
      }
      if (!ir_lower_std_term_runtime_call(program, ir, fun, expr, callee_name, &handled, out)) {
        free(callee_name);
        return false;
      }
      if (handled) {
        free(callee_name);
        return true;
      }
      if (!ir_lower_std_fmt_direct_call(program, ir, fun, expr, std_call, &handled, out)) {
        free(callee_name);
        return false;
      }
      if (handled) {
        free(callee_name);
        return true;
      }
      if (!ir_lower_std_args_cli_direct_call(program, ir, fun, expr, std_call, &handled, out)) {
        free(callee_name);
        return false;
      }
      if (handled) {
        free(callee_name);
        return true;
      }
      if (strcmp(callee_name, "std.env.get") == 0 && expr->args.len == 1) {
        IrValue *key = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &key)) {
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_ENV_GET, IR_TYPE_MAYBE_BYTE_VIEW, expr->line, expr->column);
        value->left = key;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.fs.host") == 0 && expr->args.len == 0) {
        IrValue *value = ir_new_value(ir, IR_VALUE_FS_HOST, IR_TYPE_I32, expr->line, expr->column);
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.fs.open") == 0 || strcmp(callee_name, "std.fs.create") == 0 ||
           strcmp(callee_name, "std.fs.openOrRaise") == 0 || strcmp(callee_name, "std.fs.createOrRaise") == 0) &&
          expr->args.len == 2) {
        IrValue *path = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[1], &path)) {
          free(callee_name);
          return false;
        }
        bool raises = strstr(callee_name, "OrRaise") != NULL;
        IrValueKind kind = strstr(callee_name, "create") ? IR_VALUE_FS_CREATE : IR_VALUE_FS_OPEN;
        IrValue *value = ir_new_value(ir, kind, raises ? IR_TYPE_I64 : IR_TYPE_MAYBE_SCALAR, expr->line, expr->column);
        value->left = path;
        value->element_type = IR_TYPE_I32;
        if (raises) value->error_code = kind == IR_VALUE_FS_OPEN ? IR_ERROR_NOT_FOUND : IR_ERROR_IO;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.fs.read") == 0 || strcmp(callee_name, "std.fs.readOrRaise") == 0) && expr->args.len == 2) {
        bool raises = strcmp(callee_name, "std.fs.readOrRaise") == 0;
        IrValue *buf = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[1], &buf)) {
          free(callee_name);
          return false;
        }
        const Expr *first = expr->args.items[0];
        if (first && first->kind == EXPR_BORROW && first->left && first->left->kind == EXPR_IDENT) {
          const IrLocal *file = ir_function_find_local(fun, first->left->text);
          if (!file || file->type != IR_TYPE_I32) {
            ir_free_value(buf);
            free(callee_name);
            ir_mark_unsupported(ir, "direct backend std.fs.read expects a File local", first->line, first->column, "non-File read");
            return false;
          }
          IrValue *value = ir_new_value(ir, IR_VALUE_FS_READ_FILE, raises ? IR_TYPE_I64 : IR_TYPE_MAYBE_SCALAR, expr->line, expr->column);
          value->local_index = file->index;
          value->left = buf;
          value->element_type = IR_TYPE_USIZE;
          if (raises) value->error_code = IR_ERROR_IO;
          free(callee_name);
          *out = value;
          return true;
        }
        IrValue *path = NULL;
        if (!ir_lower_byte_view(program, ir, fun, first, &path)) {
          ir_free_value(buf);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_FS_READ_PATH, IR_TYPE_USIZE, expr->line, expr->column);
        value->left = path;
        value->right = buf;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.fs.write") == 0 || strcmp(callee_name, "std.fs.writeBytes") == 0 || strcmp(callee_name, "std.fs.appendBytes") == 0) && expr->args.len == 2) {
        IrValue *path = NULL;
        IrValue *bytes = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &path) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &bytes)) {
          ir_free_value(path);
          ir_free_value(bytes);
          free(callee_name);
          return false;
        }
        bool maybe = strcmp(callee_name, "std.fs.writeBytes") == 0;
        bool append = strcmp(callee_name, "std.fs.appendBytes") == 0;
        IrValueKind kind = append ? IR_VALUE_FS_APPEND_BYTES_PATH : (maybe ? IR_VALUE_FS_WRITE_BYTES_PATH : IR_VALUE_FS_WRITE_PATH);
        IrValue *value = ir_new_value(ir, kind, maybe || append ? IR_TYPE_MAYBE_SCALAR : IR_TYPE_USIZE, expr->line, expr->column);
        value->left = path;
        value->right = bytes;
        value->element_type = IR_TYPE_USIZE;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.fs.readBytes") == 0 && expr->args.len == 2) {
        IrValue *path = NULL;
        IrValue *buf = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &path) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &buf)) {
          ir_free_value(path);
          ir_free_value(buf);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_FS_READ_BYTES_PATH, IR_TYPE_MAYBE_SCALAR, expr->line, expr->column);
        value->left = path;
        value->right = buf;
        value->element_type = IR_TYPE_USIZE;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.fs.readBytesAt") == 0 && expr->args.len == 3) {
        IrValue *path = NULL;
        IrValue *offset = NULL;
        IrValue *buf = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &path) ||
            !ir_lower_expr(program, ir, fun, expr->args.items[1], &offset) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[2], &buf)) {
          ir_free_value(path);
          ir_free_value(offset);
          ir_free_value(buf);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_FS_READ_BYTES_AT_PATH, IR_TYPE_MAYBE_SCALAR, expr->line, expr->column);
        value->left = path;
        value->index = offset;
        value->right = buf;
        value->element_type = IR_TYPE_USIZE;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.fs.readAll") == 0 || strcmp(callee_name, "std.fs.readAllOrRaise") == 0) &&
          expr->args.len == 4 && expr->args.items[0] && expr->args.items[0]->kind == EXPR_IDENT) {
        bool raises = strcmp(callee_name, "std.fs.readAllOrRaise") == 0;
        const IrLocal *alloc = ir_function_find_local(fun, expr->args.items[0]->text);
        if (!alloc || alloc->type != IR_TYPE_ALLOC || !alloc->is_mutable) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.fs.readAll expects a mutable FixedBufAlloc local", expr->args.items[0]->line, expr->args.items[0]->column, "non-mutable allocator");
          return false;
        }
        IrValue *path = NULL;
        IrValue *limit = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[2], &path) ||
            !ir_lower_expr(program, ir, fun, expr->args.items[3], &limit)) {
          ir_free_value(path);
          ir_free_value(limit);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_FS_READ_ALL, raises ? IR_TYPE_I64 : IR_TYPE_MAYBE_BYTE_VIEW, expr->line, expr->column);
        value->local_index = alloc->index;
        value->left = path;
        value->right = limit;
        value->element_type = IR_TYPE_BYTE_VIEW;
        if (raises) value->error_code = IR_ERROR_IO;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.fs.writeAll") == 0 || strcmp(callee_name, "std.fs.writeAllOrRaise") == 0) && expr->args.len == 2) {
        const Expr *first = expr->args.items[0];
        if (!first || first->kind != EXPR_BORROW || !first->left || first->left->kind != EXPR_IDENT) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.fs.writeAll expects a File local", expr->line, expr->column, "non-File writeAll");
          return false;
        }
        const IrLocal *file = ir_function_find_local(fun, first->left->text);
        IrValue *bytes = NULL;
        if (!file || file->type != IR_TYPE_I32 || !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &bytes)) {
          free(callee_name);
          return false;
        }
        bool raises = strcmp(callee_name, "std.fs.writeAllOrRaise") == 0;
        IrValue *value = ir_new_value(ir, IR_VALUE_FS_WRITE_ALL_FILE, raises ? IR_TYPE_I64 : IR_TYPE_BOOL, expr->line, expr->column);
        value->local_index = file->index;
        value->left = bytes;
        value->element_type = IR_TYPE_VOID;
        if (raises) value->error_code = IR_ERROR_IO;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.fs.close") == 0 && expr->args.len == 1) {
        const Expr *arg = expr->args.items[0];
        if (arg && arg->kind == EXPR_BORROW) arg = arg->left;
        const IrLocal *file = arg && arg->kind == EXPR_IDENT ? ir_function_find_local(fun, arg->text) : NULL;
        if (!file || file->type != IR_TYPE_I32) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.fs.close expects a File local", expr->line, expr->column, "non-File close");
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_FS_CLOSE_FILE, IR_TYPE_VOID, expr->line, expr->column);
        value->local_index = file->index;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.fs.exists") == 0 || strcmp(callee_name, "std.fs.remove") == 0 ||
           strcmp(callee_name, "std.fs.makeDir") == 0 || strcmp(callee_name, "std.fs.removeDir") == 0 ||
           strcmp(callee_name, "std.fs.isDir") == 0) && expr->args.len == 1) {
        IrValue *path = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &path)) {
          free(callee_name);
          return false;
        }
        IrValueKind kind = IR_VALUE_FS_EXISTS;
        if (strcmp(callee_name, "std.fs.remove") == 0) kind = IR_VALUE_FS_REMOVE;
        else if (strcmp(callee_name, "std.fs.makeDir") == 0) kind = IR_VALUE_FS_MAKE_DIR;
        else if (strcmp(callee_name, "std.fs.removeDir") == 0) kind = IR_VALUE_FS_REMOVE_DIR;
        else if (strcmp(callee_name, "std.fs.isDir") == 0) kind = IR_VALUE_FS_IS_DIR;
        IrValue *value = ir_new_value(ir, kind, IR_TYPE_BOOL, expr->line, expr->column);
        value->left = path;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.fs.rename") == 0 && expr->args.len == 2) {
        IrValue *from = NULL;
        IrValue *to = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &from) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &to)) {
          ir_free_value(from);
          ir_free_value(to);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_FS_RENAME, IR_TYPE_BOOL, expr->line, expr->column);
        value->left = from;
        value->right = to;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.fs.dirEntryCount") == 0 && expr->args.len == 1) {
        IrValue *path = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &path)) {
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_FS_DIR_ENTRY_COUNT, IR_TYPE_MAYBE_SCALAR, expr->line, expr->column);
        value->left = path;
        value->element_type = IR_TYPE_USIZE;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.fs.tempName") == 0 && expr->args.len == 2) {
        IrValue *buf = NULL;
        IrValue *prefix = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &buf) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &prefix)) {
          ir_free_value(buf);
          ir_free_value(prefix);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_FS_TEMP_NAME, IR_TYPE_MAYBE_BYTE_VIEW, expr->line, expr->column);
        value->left = buf;
        value->right = prefix;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.fs.atomicWrite") == 0 && expr->args.len == 3) {
        IrValue *path = NULL;
        IrValue *tmp_path = NULL;
        IrValue *bytes = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &path) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &tmp_path) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[2], &bytes)) {
          ir_free_value(path);
          ir_free_value(tmp_path);
          ir_free_value(bytes);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_FS_ATOMIC_WRITE, IR_TYPE_BOOL, expr->line, expr->column);
        value->left = path;
        value->right = tmp_path;
        value->index = bytes;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.fs.fileLen") == 0 || strcmp(callee_name, "std.fs.fileLenOrRaise") == 0) && expr->args.len == 1) {
        const Expr *arg = expr->args.items[0];
        if (arg && arg->kind == EXPR_BORROW) arg = arg->left;
        const IrLocal *file = arg && arg->kind == EXPR_IDENT ? ir_function_find_local(fun, arg->text) : NULL;
        if (!file || file->type != IR_TYPE_I32) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.fs.fileLen expects a File local", expr->line, expr->column, "non-File fileLen");
          return false;
        }
        bool raises = strcmp(callee_name, "std.fs.fileLenOrRaise") == 0;
        IrValue *value = ir_new_value(ir, IR_VALUE_FS_FILE_LEN, raises ? IR_TYPE_I64 : IR_TYPE_MAYBE_SCALAR, expr->line, expr->column);
        value->local_index = file->index;
        value->element_type = IR_TYPE_USIZE;
        if (raises) value->error_code = IR_ERROR_IO;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.io.bufferedReader") == 0 || strcmp(callee_name, "std.io.bufferedWriter") == 0) &&
          expr->args.len == 1) {
        IrValue *buf = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &buf)) {
          free(callee_name);
          return false;
        }
        free(callee_name);
        *out = buf;
        return true;
      }
      if ((strcmp(callee_name, "std.io.readerCapacity") == 0 || strcmp(callee_name, "std.io.writerCapacity") == 0) &&
          expr->args.len == 1) {
        IrValue *buf = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &buf)) {
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_USIZE, expr->line, expr->column);
        value->left = buf;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.io.copy") == 0 &&
          expr->args.len == 2 &&
          expr->args.items[0] &&
          ir_expr_is_byte_view_source(expr->args.items[1])) {
        if (!ir_expr_is_mutable_byte_view_dest(program, fun, expr->args.items[0])) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.io.copy expects a mutable byte destination", expr->args.items[0]->line, expr->args.items[0]->column, "non-mutable byte destination");
          return false;
        }
        IrValue *dst = NULL;
        IrValue *src = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &dst) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &src)) {
          ir_free_value(dst);
          ir_free_value(src);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_COPY, IR_TYPE_USIZE, expr->line, expr->column);
        value->left = src;
        value->right = dst;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.mem.vec") == 0 &&
          expr->args.len == 1 &&
          ir_expr_is_mutable_byte_view_dest(program, fun, expr->args.items[0])) {
        IrValue *bytes = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &bytes)) {
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_VEC_INIT, IR_TYPE_VEC, expr->line, expr->column);
        value->left = bytes;
        ir->direct_buffer_helper_count = ir->direct_buffer_helper_count < 1 ? 1 : ir->direct_buffer_helper_count;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.mem.bufBytes") == 0 && expr->args.len == 1) {
        const Expr *arg = expr->args.items[0];
        if (arg && arg->kind == EXPR_BORROW) arg = arg->left;
        if (arg && arg->kind == EXPR_IDENT) {
          const IrLocal *buf = ir_function_find_local(fun, arg->text);
          if (buf && buf->type == IR_TYPE_BYTE_VIEW) {
            IrValue *value = ir_new_value(ir, IR_VALUE_LOCAL, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
            value->local_index = buf->index;
            free(callee_name);
            *out = value;
            return true;
          }
        }
      }
      if (strcmp(callee_name, "std.mem.bufLen") == 0 && expr->args.len == 1) {
        const Expr *arg = expr->args.items[0];
        if (arg && arg->kind == EXPR_BORROW) arg = arg->left;
        if (arg && arg->kind == EXPR_IDENT) {
          const IrLocal *buf = ir_function_find_local(fun, arg->text);
          if (buf && buf->type == IR_TYPE_BYTE_VIEW) {
            IrValue *view = ir_new_value(ir, IR_VALUE_LOCAL, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
            view->local_index = buf->index;
            IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_USIZE, expr->line, expr->column);
            value->left = view;
            free(callee_name);
            *out = value;
            return true;
          }
        }
      }
      if (strcmp(callee_name, "std.mem.vecPush") == 0 &&
          expr->args.len == 2 &&
          expr->args.items[0] &&
          expr->args.items[0]->kind == EXPR_BORROW &&
          expr->args.items[0]->mutable_borrow &&
          expr->args.items[0]->left &&
          expr->args.items[0]->left->kind == EXPR_IDENT) {
        const IrLocal *vec = ir_function_find_local(fun, expr->args.items[0]->left->text);
        if (!vec || vec->type != IR_TYPE_VEC || !vec->is_mutable) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem.vecPush expects a mutable Vec local", expr->args.items[0]->line, expr->args.items[0]->column, "non-mutable Vec");
          return false;
        }
        IrValue *item = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[1], &item)) {
          free(callee_name);
          return false;
        }
        if (!item || item->type != IR_TYPE_U8) {
          ir_free_value(item);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem.vecPush currently supports only u8 values", expr->args.items[1]->line, expr->args.items[1]->column, "non-u8 value");
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_VEC_PUSH, IR_TYPE_BOOL, expr->line, expr->column);
        value->local_index = vec->index;
        value->left = item;
        ir->direct_buffer_helper_count = ir->direct_buffer_helper_count < 2 ? 2 : ir->direct_buffer_helper_count;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.mem.vecClear") == 0 || strcmp(callee_name, "std.mem.vecPop") == 0) &&
          expr->args.len == 1 &&
          expr->args.items[0] &&
          expr->args.items[0]->kind == EXPR_BORROW &&
          expr->args.items[0]->mutable_borrow &&
          expr->args.items[0]->left &&
          expr->args.items[0]->left->kind == EXPR_IDENT) {
        const IrLocal *vec = ir_function_find_local(fun, expr->args.items[0]->left->text);
        if (!vec || vec->type != IR_TYPE_VEC || !vec->is_mutable) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem Vec mutation expects a mutable Vec local", expr->args.items[0]->line, expr->args.items[0]->column, "non-mutable Vec");
          return false;
        }
        bool pop = strcmp(callee_name, "std.mem.vecPop") == 0;
        IrValue *value = ir_new_value(ir, pop ? IR_VALUE_VEC_POP : IR_VALUE_VEC_CLEAR, pop ? IR_TYPE_BOOL : IR_TYPE_USIZE, expr->line, expr->column);
        value->local_index = vec->index;
        ir->direct_buffer_helper_count = ir->direct_buffer_helper_count < 4 ? 4 : ir->direct_buffer_helper_count;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.mem.vecTruncate") == 0 &&
          expr->args.len == 2 &&
          expr->args.items[0] &&
          expr->args.items[0]->kind == EXPR_BORROW &&
          expr->args.items[0]->mutable_borrow &&
          expr->args.items[0]->left &&
          expr->args.items[0]->left->kind == EXPR_IDENT) {
        const IrLocal *vec = ir_function_find_local(fun, expr->args.items[0]->left->text);
        if (!vec || vec->type != IR_TYPE_VEC || !vec->is_mutable) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem.vecTruncate expects a mutable Vec local", expr->args.items[0]->line, expr->args.items[0]->column, "non-mutable Vec");
          return false;
        }
        IrValue *len = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[1], &len)) {
          free(callee_name);
          return false;
        }
        if (!len || len->type != IR_TYPE_USIZE) {
          ir_free_value(len);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem.vecTruncate expects a usize length", expr->args.items[1]->line, expr->args.items[1]->column, "non-usize length");
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_VEC_TRUNCATE, IR_TYPE_USIZE, expr->line, expr->column);
        value->local_index = vec->index;
        value->left = len;
        ir->direct_buffer_helper_count = ir->direct_buffer_helper_count < 4 ? 4 : ir->direct_buffer_helper_count;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.mem.vecRemoveSwap") == 0 &&
          expr->args.len == 2 &&
          expr->args.items[0] &&
          expr->args.items[0]->kind == EXPR_BORROW &&
          expr->args.items[0]->mutable_borrow &&
          expr->args.items[0]->left &&
          expr->args.items[0]->left->kind == EXPR_IDENT) {
        const IrLocal *vec = ir_function_find_local(fun, expr->args.items[0]->left->text);
        if (!vec || vec->type != IR_TYPE_VEC || !vec->is_mutable) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem.vecRemoveSwap expects a mutable Vec local", expr->args.items[0]->line, expr->args.items[0]->column, "non-mutable Vec");
          return false;
        }
        IrValue *index = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[1], &index)) {
          free(callee_name);
          return false;
        }
        if (!index || index->type != IR_TYPE_USIZE) {
          ir_free_value(index);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem.vecRemoveSwap expects a usize index", expr->args.items[1]->line, expr->args.items[1]->column, "non-usize index");
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_VEC_REMOVE_SWAP, IR_TYPE_BOOL, expr->line, expr->column);
        value->element_type = IR_TYPE_U8;
        value->local_index = vec->index;
        value->left = index;
        ir->direct_buffer_helper_count = ir->direct_buffer_helper_count < 5 ? 5 : ir->direct_buffer_helper_count;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.mem.vecInsertUnique") == 0 || strcmp(callee_name, "std.mem.vecRemoveValue") == 0) &&
          expr->args.len == 2 &&
          expr->args.items[0] &&
          expr->args.items[0]->kind == EXPR_BORROW &&
          expr->args.items[0]->mutable_borrow &&
          expr->args.items[0]->left &&
          expr->args.items[0]->left->kind == EXPR_IDENT) {
        const IrLocal *vec = ir_function_find_local(fun, expr->args.items[0]->left->text);
        if (!vec || vec->type != IR_TYPE_VEC || !vec->is_mutable) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem Vec value mutation expects a mutable Vec local", expr->args.items[0]->line, expr->args.items[0]->column, "non-mutable Vec");
          return false;
        }
        IrValue *item = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[1], &item)) {
          free(callee_name);
          return false;
        }
        if (!item || item->type != IR_TYPE_U8) {
          ir_free_value(item);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem Vec value mutation currently supports only u8 values", expr->args.items[1]->line, expr->args.items[1]->column, "non-u8 value");
          return false;
        }
        bool insert_unique = strcmp(callee_name, "std.mem.vecInsertUnique") == 0;
        IrValue *value = ir_new_value(ir, insert_unique ? IR_VALUE_VEC_INSERT_UNIQUE : IR_VALUE_VEC_REMOVE_VALUE, IR_TYPE_BOOL, expr->line, expr->column);
        value->element_type = IR_TYPE_U8;
        value->local_index = vec->index;
        value->left = item;
        ir->direct_buffer_helper_count = ir->direct_buffer_helper_count < 6 ? 6 : ir->direct_buffer_helper_count;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.mem.vecIndex") == 0 || strcmp(callee_name, "std.mem.vecContains") == 0) &&
          expr->args.len == 2) {
        const Expr *arg = expr->args.items[0];
        if (arg && arg->kind == EXPR_BORROW) arg = arg->left;
        if (arg && arg->kind == EXPR_IDENT) {
          const IrLocal *vec = ir_function_find_local(fun, arg->text);
          if (vec && vec->type == IR_TYPE_VEC) {
            IrValue *item = NULL;
            if (!ir_lower_expr(program, ir, fun, expr->args.items[1], &item)) {
              free(callee_name);
              return false;
            }
            if (!item || item->type != IR_TYPE_U8) {
              ir_free_value(item);
              free(callee_name);
              ir_mark_unsupported(ir, "direct backend std.mem Vec lookup currently supports only u8 values", expr->args.items[1]->line, expr->args.items[1]->column, "non-u8 value");
              return false;
            }
            bool contains = strcmp(callee_name, "std.mem.vecContains") == 0;
            IrValue *value = ir_new_value(ir, contains ? IR_VALUE_VEC_CONTAINS : IR_VALUE_VEC_INDEX, contains ? IR_TYPE_BOOL : IR_TYPE_USIZE, expr->line, expr->column);
            value->element_type = IR_TYPE_U8;
            value->local_index = vec->index;
            value->left = item;
            ir->direct_buffer_helper_count = ir->direct_buffer_helper_count < 6 ? 6 : ir->direct_buffer_helper_count;
            free(callee_name);
            *out = value;
            return true;
          }
        }
      }
      if (strcmp(callee_name, "std.mem.vecGet") == 0 &&
          expr->args.len == 2) {
        const Expr *arg = expr->args.items[0];
        if (arg && arg->kind == EXPR_BORROW) arg = arg->left;
        if (arg && arg->kind == EXPR_IDENT) {
          const IrLocal *vec = ir_function_find_local(fun, arg->text);
          if (vec && vec->type == IR_TYPE_VEC) {
            IrValue *index = NULL;
            if (!ir_lower_expr(program, ir, fun, expr->args.items[1], &index)) {
              free(callee_name);
              return false;
            }
            if (!index || index->type != IR_TYPE_USIZE) {
              ir_free_value(index);
              free(callee_name);
              ir_mark_unsupported(ir, "direct backend std.mem.vecGet expects a usize index", expr->args.items[1]->line, expr->args.items[1]->column, "non-usize index");
              return false;
            }
            IrValue *value = ir_new_value(ir, IR_VALUE_VEC_GET, IR_TYPE_MAYBE_SCALAR, expr->line, expr->column);
            value->element_type = IR_TYPE_U8;
            value->local_index = vec->index;
            value->left = index;
            ir->direct_buffer_helper_count = ir->direct_buffer_helper_count < 5 ? 5 : ir->direct_buffer_helper_count;
            free(callee_name);
            *out = value;
            return true;
          }
        }
      }
      if (strcmp(callee_name, "std.mem.vecSet") == 0 &&
          expr->args.len == 3 &&
          expr->args.items[0] &&
          expr->args.items[0]->kind == EXPR_BORROW &&
          expr->args.items[0]->mutable_borrow &&
          expr->args.items[0]->left &&
          expr->args.items[0]->left->kind == EXPR_IDENT) {
        const IrLocal *vec = ir_function_find_local(fun, expr->args.items[0]->left->text);
        if (!vec || vec->type != IR_TYPE_VEC || !vec->is_mutable) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem.vecSet expects a mutable Vec local", expr->args.items[0]->line, expr->args.items[0]->column, "non-mutable Vec");
          return false;
        }
        IrValue *index = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[1], &index)) {
          free(callee_name);
          return false;
        }
        if (!index || index->type != IR_TYPE_USIZE) {
          ir_free_value(index);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem.vecSet expects a usize index", expr->args.items[1]->line, expr->args.items[1]->column, "non-usize index");
          return false;
        }
        IrValue *item = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[2], &item)) {
          ir_free_value(index);
          free(callee_name);
          return false;
        }
        if (!item || item->type != IR_TYPE_U8) {
          ir_free_value(index);
          ir_free_value(item);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem.vecSet currently supports only u8 values", expr->args.items[2]->line, expr->args.items[2]->column, "non-u8 value");
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_VEC_SET, IR_TYPE_BOOL, expr->line, expr->column);
        value->element_type = IR_TYPE_U8;
        value->local_index = vec->index;
        value->left = index;
        value->right = item;
        ir->direct_buffer_helper_count = ir->direct_buffer_helper_count < 5 ? 5 : ir->direct_buffer_helper_count;
        free(callee_name);
        *out = value;
        return true;
      }
      IrVecHelper vec_helper = ir_std_mem_vec_helper(callee_name);
      if (vec_helper != IR_VEC_HELPER_NONE && expr->args.len == 1) {
        const Expr *arg = expr->args.items[0];
        if (arg && arg->kind == EXPR_BORROW) arg = arg->left;
        if (arg && arg->kind == EXPR_IDENT) {
          const IrLocal *vec = ir_function_find_local(fun, arg->text);
          if (vec && vec->type == IR_TYPE_VEC) {
            IrValue *value = ir_new_vec_helper_value(ir, vec_helper, vec->index, expr->line, expr->column);
            ir->direct_buffer_helper_count = ir->direct_buffer_helper_count < (vec_helper == IR_VEC_HELPER_BYTES ? 4 : 3) ? (vec_helper == IR_VEC_HELPER_BYTES ? 4 : 3) : ir->direct_buffer_helper_count;
            free(callee_name);
            *out = value;
            return true;
          }
        }
      }
      if (strcmp(callee_name, "std.mem.copy") == 0 &&
          expr->args.len == 2 &&
          expr->args.items[0] &&
          ir_expr_is_byte_view_source(expr->args.items[1])) {
        if (!ir_expr_is_mutable_byte_view_dest(program, fun, expr->args.items[0])) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem.copy expects a mutable byte destination", expr->args.items[0]->line, expr->args.items[0]->column, "non-mutable byte destination");
          return false;
        }
        IrValue *dst = NULL;
        IrValue *src = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &dst) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &src)) {
          ir_free_value(dst);
          ir_free_value(src);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_COPY, IR_TYPE_USIZE, expr->line, expr->column);
        value->left = src;
        value->right = dst;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.mem.fill") == 0 &&
          expr->args.len == 2 &&
          expr->args.items[0]) {
        if (!ir_expr_is_mutable_byte_view_dest(program, fun, expr->args.items[0])) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem.fill expects a mutable byte destination", expr->args.items[0]->line, expr->args.items[0]->column, "non-mutable byte destination");
          return false;
        }
        IrValue *dst = NULL;
        IrValue *fill = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &dst) ||
            !ir_lower_expr(program, ir, fun, expr->args.items[1], &fill)) {
          ir_free_value(dst);
          ir_free_value(fill);
          free(callee_name);
          return false;
        }
        if (fill->type != IR_TYPE_U8) {
          ir_free_value(dst);
          ir_free_value(fill);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.mem.fill value must be u8", expr->args.items[1]->line, expr->args.items[1]->column, "non-u8 fill value");
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_FILL, IR_TYPE_USIZE, expr->line, expr->column);
        value->left = fill;
        value->right = dst;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.mem.eqlBytes") == 0 || strcmp(callee_name, "std.mem.eql") == 0) &&
          expr->args.len == 2 &&
          ir_expr_is_byte_view_source(expr->args.items[0]) &&
          ir_expr_is_byte_view_source(expr->args.items[1])) {
        IrValue *left = NULL;
        IrValue *right = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &left) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &right)) {
          ir_free_value(left);
          ir_free_value(right);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_EQ, IR_TYPE_BOOL, expr->line, expr->column);
        value->left = left;
        value->right = right;
        free(callee_name);
        *out = value;
        return true;
      }
      bool std_str_handled = false;
      if (!ir_lower_std_str_call(program, ir, fun, expr, callee_name, &std_str_handled, out)) {
        free(callee_name);
        return false;
      }
      if (std_str_handled) {
        free(callee_name);
        return true;
      }
      bool std_ascii_handled = false;
      if (!ir_lower_std_ascii_call(program, ir, fun, expr, callee_name, &std_ascii_handled, out)) {
        free(callee_name);
        return false;
      }
      if (std_ascii_handled) {
        free(callee_name);
        return true;
      }
      bool std_text_handled = false;
      if (!ir_lower_std_text_call(program, ir, fun, expr, callee_name, &std_text_handled, out)) {
        free(callee_name);
        return false;
      }
      if (std_text_handled) {
        free(callee_name);
        return true;
      }
      bool std_parse_handled = false;
      if (!ir_lower_std_parse_call(program, ir, fun, expr, callee_name, &std_parse_handled, out)) {
        free(callee_name);
        return false;
      }
      if (std_parse_handled) {
        free(callee_name);
        return true;
      }
      bool std_search_handled = false;
      if (!ir_lower_std_search_call(program, ir, fun, expr, callee_name, &std_search_handled, out)) {
        free(callee_name);
        return false;
      }
      if (std_search_handled) {
        free(callee_name);
        return true;
      }
      bool std_sort_handled = false;
      if (!ir_lower_std_sort_call(program, ir, fun, expr, callee_name, &std_sort_handled, out)) {
        free(callee_name);
        return false;
      }
      if (std_sort_handled) {
        free(callee_name);
        return true;
      }
      bool std_testing_handled = false;
      if (!ir_lower_std_testing_call(program, ir, fun, expr, callee_name, &std_testing_handled, out)) {
        free(callee_name);
        return false;
      }
      if (std_testing_handled) {
        free(callee_name);
        return true;
      }
      if (strcmp(callee_name, "std.str.contains") == 0 && expr->args.len == 2) {
        IrValue *left = NULL;
        IrValue *right = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &left) ||
            !ir_lower_byte_view(program, ir, fun, expr->args.items[1], &right)) {
          ir_free_value(left);
          ir_free_value(right);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_STR_CONTAINS, IR_TYPE_BOOL, expr->line, expr->column);
        value->left = left;
        value->right = right;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.mem.len") == 0 &&
          expr->args.len == 1 &&
          expr->args.items[0] &&
          ir_expr_is_byte_view_source(expr->args.items[0])) {
        IrValue *view = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &view)) {
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_USIZE, expr->line, expr->column);
        value->left = view;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.mem.len") == 0 &&
          expr->args.len == 1 &&
          expr->args.items[0] &&
          expr->args.items[0]->kind == EXPR_IDENT) {
        const IrLocal *local = ir_function_find_local(fun, expr->args.items[0]->text);
        if (local && local->type == IR_TYPE_BYTE_VIEW) {
          IrValue *view = NULL;
          if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &view)) {
            free(callee_name);
            return false;
          }
          IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_USIZE, expr->line, expr->column);
          value->left = view;
          free(callee_name);
          *out = value;
          return true;
        }
        if (local && local->is_array) {
          IrValue *value = ir_new_value(ir, IR_VALUE_INT, IR_TYPE_USIZE, expr->line, expr->column);
          value->int_value = local->array_len;
          free(callee_name);
          *out = value;
          return true;
        }
      }
      if (strcmp(callee_name, "std.mem.isEmpty") == 0 && expr->args.len == 1) {
        IrValue *view = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &view)) {
          free(callee_name);
          return false;
        }
        IrValue *len = ir_new_value(ir, IR_VALUE_BYTE_VIEW_LEN, IR_TYPE_USIZE, expr->line, expr->column);
        len->left = view;
        *out = ir_new_compare_value(
            ir,
            IR_CMP_EQ,
            len,
            ir_new_integer_literal_value(ir, IR_TYPE_USIZE, 0, expr->line, expr->column),
            expr->line,
            expr->column);
        free(callee_name);
        return true;
      }
      if (strcmp(callee_name, "std.io.remaining") == 0 && expr->args.len == 2) {
        IrValue *view = NULL;
        IrValue *offset = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &view) ||
            !ir_lower_expr(program, ir, fun, expr->args.items[1], &offset)) {
          ir_free_value(view);
          ir_free_value(offset);
          free(callee_name);
          return false;
        }
        if (!ir_type_is_value(offset->type)) {
          ir_free_value(view);
          ir_free_value(offset);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.io.remaining offset must be an integer value", expr->args.items[1]->line, expr->args.items[1]->column, "non-integer offset");
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_REMAINING, IR_TYPE_USIZE, expr->line, expr->column);
        value->left = view;
        value->index = offset;
        *out = value;
        free(callee_name);
        return true;
      }
      const char *stdlib_graph_target = z_std_source_target_for_public_call(callee_name);
      if (stdlib_graph_target) {
        bool ok = ir_lower_named_direct_call(program, ir, fun, expr, stdlib_graph_target, callee_name, out);
        free(callee_name);
        return ok;
      }
      if (!expr->left || expr->left->kind != EXPR_IDENT) {
        ir_mark_unsupported(ir, "direct backend calls currently support only same-file function identifiers", expr->line, expr->column, callee_name && callee_name[0] ? callee_name : "non-identifier callee");
        free(callee_name);
        return false;
      }
      bool ok = ir_lower_named_direct_call(program, ir, fun, expr, expr->left->text, expr->left->text, out);
      free(callee_name);
      return ok;
    }
    case EXPR_CHECK: {
      IrValue *checked = NULL;
      if (!ir_lower_expr(program, ir, fun, expr->left, &checked)) return false;
      if (!checked || checked->type != IR_TYPE_I64) {
        ir_free_value(checked);
        ir_mark_unsupported(ir, "direct backend check currently supports only fallible values", expr->line, expr->column, "non-fallible check");
        return false;
      }
      IrValue *value = ir_new_value(ir, IR_VALUE_CHECK, checked->element_type, expr->line, expr->column);
      value->left = checked;
      *out = value;
      return true;
    }
    case EXPR_RESCUE: {
      IrValue *fallible = NULL;
      IrValue *fallback = NULL;
      if (!ir_lower_expr(program, ir, fun, expr->left, &fallible) ||
          !ir_lower_expr(program, ir, fun, expr->right, &fallback)) {
        ir_free_value(fallible);
        ir_free_value(fallback);
        return false;
      }
      if (!fallible || fallible->kind != IR_VALUE_CALL || fallible->type != IR_TYPE_I64 ||
          !fallback || fallback->type != fallible->element_type) {
        ir_free_value(fallible);
        ir_free_value(fallback);
        ir_mark_unsupported(ir, "direct backend rescue currently supports fallible function calls with primitive fallbacks", expr->line, expr->column, "unsupported rescue");
        return false;
      }
      IrValue *value = ir_new_value(ir, IR_VALUE_RESCUE, fallible->element_type, expr->line, expr->column);
      value->left = fallible;
      value->right = fallback;
      *out = value;
      return true;
    }
    case EXPR_BINARY: {
      IrBinaryOp op = IR_BIN_ADD;
      IrCompareOp cmp = IR_CMP_EQ;
      if (ir_compare_op(expr->text, &cmp)) {
        IrValue *left = NULL;
        IrValue *right = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->left, &left) || !ir_lower_expr(program, ir, fun, expr->right, &right)) {
          ir_free_value(left);
          ir_free_value(right);
          return false;
        }
        if (left->type != right->type && ir_type_is_value(left->type) && ir_type_is_value(right->type)) {
          if (right->kind == IR_VALUE_INT) right->type = left->type;
          else if (left->kind == IR_VALUE_INT) left->type = right->type;
        }
        if (!(left->type == IR_TYPE_BOOL || ir_type_is_value(left->type)) || left->type != right->type) {
          ir_free_value(left);
          ir_free_value(right);
          ir_mark_unsupported(ir, "direct backend comparison operands must have the same primitive integer type", expr->line, expr->column, expr->text);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_COMPARE, IR_TYPE_BOOL, expr->line, expr->column);
        value->compare_op = cmp;
        value->left = left;
        value->right = right;
        *out = value;
        return true;
      }
      if (!ir_binary_op(expr->text, &op)) {
        ir_mark_unsupported(ir, "direct backend binary operator is unsupported", expr->line, expr->column, expr->text);
        return false;
      }
      IrTypeKind type = ir_type_kind(expr->resolved_type);
      if (!(type == IR_TYPE_BOOL || ir_type_is_value(type))) {
        ir_mark_unsupported(ir, "direct backend binary expression type is unsupported", expr->line, expr->column, expr->resolved_type);
        return false;
      }
      IrValue *left = NULL;
      IrValue *right = NULL;
      if (!ir_lower_expr(program, ir, fun, expr->left, &left) || !ir_lower_expr(program, ir, fun, expr->right, &right)) {
        ir_free_value(left);
        ir_free_value(right);
        return false;
      }
      IrValue *value = ir_new_value(ir, IR_VALUE_BINARY, type, expr->line, expr->column);
      value->binary_op = op;
      value->left = left;
      value->right = right;
      *out = value;
      return true;
    }
    default:
      ir_mark_unsupported(ir, "direct backend expression kind is unsupported", expr->line, expr->column, "unsupported expression");
      return false;
  }
}

static bool ir_lower_stmt_vec(const Program *program, IrProgram *ir, IrFunction *mir_fun, const StmtVec *body, IrInstr **out_items, size_t *out_len, size_t *out_cap, bool *saw_return);

static bool ir_lower_index_store(const Program *program, IrProgram *ir, IrFunction *mir_fun, const Expr *target, const Expr *source, IrInstr **out_items, size_t *out_len, size_t *out_cap, int line, int column) {
  if (!target || target->kind != EXPR_INDEX || !target->left || target->left->kind != EXPR_IDENT) {
    ir_mark_unsupported(ir, "direct backend indexed assignment supports only local fixed arrays and mutable byte views", line, column, "non-local indexed assignment");
    return false;
  }
  const IrLocal *local = ir_function_find_local(mir_fun, target->left->text);
  if (!local || (!local->is_array && local->type != IR_TYPE_BYTE_VIEW)) {
    ir_mark_unsupported(ir, "direct backend indexed assignment target is not a fixed array or byte view local", line, column, target->left->text);
    return false;
  }
  if (!local->is_mutable) {
    ir_mark_unsupported(ir, "direct backend indexed assignment target must be mutable", line, column, target->left->text);
    return false;
  }
  IrValue *index = NULL;
  IrValue *value = NULL;
  if (!ir_lower_expr(program, ir, mir_fun, target->right, &index) ||
      !ir_lower_expr(program, ir, mir_fun, source, &value)) {
    ir_free_value(index);
      ir_free_value(value);
      return false;
  }
  IrTypeKind element_type = local->type == IR_TYPE_BYTE_VIEW ? (local->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : local->element_type) : local->element_type;
  if (!ir_type_is_value(index->type) || value->type != element_type) {
    ir_free_value(index);
    ir_free_value(value);
    ir_mark_unsupported(ir, "direct backend indexed assignment type does not match target element", line, column, local->name);
    return false;
  }
  ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_INDEX_STORE, .array_index = local->index, .index = index, .value = value, .line = line, .column = column});
  return true;
}

static bool ir_lower_array_initializer(const Program *program, IrProgram *ir, IrFunction *mir_fun, const IrLocal *local, const Expr *expr, IrInstr **out_items, size_t *out_len, size_t *out_cap, int line, int column) {
  if (!expr || expr->kind != EXPR_ARRAY_LITERAL) {
    ir_mark_unsupported(ir, "direct backend fixed array locals require array literal initialization", line, column, local ? local->name : "array local");
    return false;
  }
  if (expr->array_repeat) {
    if (expr->args.len != 2) {
      ir_mark_unsupported(ir, "direct backend fixed array repeat literal requires value and count", line, column, local->name);
      return false;
    }
    const Expr *count_expr = expr->args.items[1];
    unsigned long long repeat_count = 0;
    if (count_expr && count_expr->kind == EXPR_NUMBER && ir_parse_integer_literal(count_expr->text ? count_expr->text : "", &repeat_count) && repeat_count != (unsigned long long)local->array_len) {
      ir_mark_unsupported(ir, "direct backend fixed array repeat count must match local type", count_expr->line, count_expr->column, local->name);
      return false;
    }
    const Expr *value_expr = expr->args.items[0];
    for (size_t i = 0; i < local->array_len; i++) {
      IrValue *index = ir_new_index_literal(ir, (unsigned)i, value_expr->line, value_expr->column);
      IrValue *value = NULL;
      if (!ir_lower_expr(program, ir, mir_fun, value_expr, &value)) {
        ir_free_value(index);
        return false;
      }
      if (value->type != local->element_type) {
        ir_free_value(index);
        ir_free_value(value);
        ir_mark_unsupported(ir, "direct backend array repeat literal element type does not match local type", value_expr->line, value_expr->column, local->name);
        return false;
      }
      ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_INDEX_STORE, .array_index = local->index, .index = index, .value = value, .line = value_expr->line, .column = value_expr->column});
    }
    return true;
  }
  if (expr->args.len != local->array_len) {
    ir_mark_unsupported(ir, "direct backend fixed array literal length must match local type", line, column, local->name);
    return false;
  }
  for (size_t i = 0; i < expr->args.len; i++) {
    IrValue *index = ir_new_index_literal(ir, (unsigned)i, expr->args.items[i]->line, expr->args.items[i]->column);
    IrValue *value = NULL;
    if (!ir_lower_expr(program, ir, mir_fun, expr->args.items[i], &value)) {
      ir_free_value(index);
      return false;
    }
    if (value->type != local->element_type) {
      ir_free_value(index);
      ir_free_value(value);
      ir_mark_unsupported(ir, "direct backend array literal element type does not match local type", expr->args.items[i]->line, expr->args.items[i]->column, local->name);
      return false;
    }
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_INDEX_STORE, .array_index = local->index, .index = index, .value = value, .line = expr->args.items[i]->line, .column = expr->args.items[i]->column});
  }
  return true;
}

static const FieldInit *ir_shape_literal_find_field(const Expr *expr, const char *name) {
  if (!expr || !name) return NULL;
  for (size_t i = 0; i < expr->fields.len; i++) {
    if (strcmp(expr->fields.items[i].name, name) == 0) return &expr->fields.items[i];
  }
  return NULL;
}

static bool ir_lower_shape_initializer(const Program *program, IrProgram *ir, IrFunction *mir_fun, const IrLocal *local, const Expr *expr, IrInstr **out_items, size_t *out_len, size_t *out_cap, int line, int column) {
  if (!local || !local->is_record || !expr || expr->kind != EXPR_SHAPE_LITERAL) {
    ir_mark_unsupported(ir, "direct backend record locals require shape literal initialization", line, column, local ? local->name : "record local");
    return false;
  }
  const Shape *shape = NULL;
  TypeArgVec shape_args = {0};
  if (!ir_shape_instance(program, local->shape_name, &shape, &shape_args)) {
    ir_mark_unsupported(ir, "direct backend record shape is unknown", line, column, local->shape_name);
    return false;
  }
  for (size_t i = 0; i < shape->fields.len; i++) {
    const Param *field = &shape->fields.items[i];
    const FieldInit *init = ir_shape_literal_find_field(expr, field->name);
    const Expr *field_expr = init ? init->value : field->default_value;
    if (!field_expr) {
      ir_type_arg_vec_free(&shape_args);
      ir_mark_unsupported(ir, "direct backend record field requires an explicit initializer or default", line, column, field->name);
      return false;
    }
    unsigned field_offset = 0;
    IrTypeKind field_type = IR_TYPE_UNSUPPORTED;
    bool field_is_array = false;
    unsigned field_array_len = 0;
    IrTypeKind field_element_type = IR_TYPE_UNSUPPORTED;
    if (!ir_shape_field_storage_info(program, local->shape_name, field->name, &field_offset, &field_type, &field_is_array, &field_array_len, &field_element_type)) {
      ir_type_arg_vec_free(&shape_args);
      ir_mark_unsupported(ir, "direct backend record field type is unsupported", field->line, field->column, field->type);
      return false;
    }
    if (field_is_array) {
      if (!field_expr || field_expr->kind != EXPR_ARRAY_LITERAL) {
        ir_type_arg_vec_free(&shape_args);
        ir_mark_unsupported(ir, "direct backend record array field requires a matching array literal", field_expr ? field_expr->line : line, field_expr ? field_expr->column : column, field->name);
        return false;
      }
      if (field_expr->array_repeat) {
        if (field_expr->args.len != 2) {
          ir_type_arg_vec_free(&shape_args);
          ir_mark_unsupported(ir, "direct backend record array field repeat literal requires value and count", field_expr->line, field_expr->column, field->name);
          return false;
        }
        const Expr *field_count_expr = field_expr->args.items[1];
        unsigned long long field_repeat_count = 0;
        if (field_count_expr && field_count_expr->kind == EXPR_NUMBER && ir_parse_integer_literal(field_count_expr->text ? field_count_expr->text : "", &field_repeat_count) && field_repeat_count != (unsigned long long)field_array_len) {
          ir_type_arg_vec_free(&shape_args);
          ir_mark_unsupported(ir, "direct backend record array field repeat count must match field type", field_count_expr->line, field_count_expr->column, field->name);
          return false;
        }
      } else if (field_expr->args.len != field_array_len) {
        ir_type_arg_vec_free(&shape_args);
        ir_mark_unsupported(ir, "direct backend record array field requires a matching array literal", field_expr->line, field_expr->column, field->name);
        return false;
      }
      for (size_t element_index = 0; element_index < field_array_len; element_index++) {
        const Expr *element_expr = field_expr->array_repeat ? field_expr->args.items[0] : field_expr->args.items[element_index];
        IrValue *element = NULL;
        if (!ir_lower_expr(program, ir, mir_fun, element_expr, &element)) {
          ir_type_arg_vec_free(&shape_args);
          return false;
        }
        if (element->type != field_element_type) {
          ir_free_value(element);
          ir_type_arg_vec_free(&shape_args);
          ir_mark_unsupported(ir, "direct backend record array field initializer type does not match element", element_expr->line, element_expr->column, field->name);
          return false;
        }
        unsigned element_offset = field_offset + (unsigned)element_index * ir_type_byte_size(field_element_type);
        ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_FIELD_STORE, .local_index = local->index, .field_offset = element_offset, .value = element, .line = element_expr->line, .column = element_expr->column});
      }
      continue;
    }
    IrValue *value = NULL;
    bool uses_default = init == NULL && field->default_value == field_expr;
    if (uses_default && field_expr->kind == EXPR_IDENT) {
      const char *static_arg = ir_shape_arg_for_param(shape, &shape_args, field_expr->text);
      unsigned long long static_value = 0;
      if (static_arg && ir_parse_integer_literal(static_arg, &static_value)) {
        value = ir_new_value(ir, IR_VALUE_INT, field_type, field_expr->line, field_expr->column);
        value->int_value = static_value;
      }
    }
    if (!value && !ir_lower_expr(program, ir, mir_fun, field_expr, &value)) {
      ir_type_arg_vec_free(&shape_args);
      return false;
    }
    if (value->type != field_type) {
      ir_free_value(value);
      ir_type_arg_vec_free(&shape_args);
      ir_mark_unsupported(ir, "direct backend record field initializer type does not match field", field_expr->line, field_expr->column, field->name);
      return false;
    }
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_FIELD_STORE, .local_index = local->index, .field_offset = field_offset, .value = value, .line = field_expr->line, .column = field_expr->column});
  }
  ir_type_arg_vec_free(&shape_args);
  return true;
}

static bool ir_lower_enum_match(const Program *program, IrProgram *ir, IrFunction *mir_fun, const Stmt *stmt, IrInstr **out_items, size_t *out_len, size_t *out_cap, bool *saw_return) {
  const EnumDecl *item_enum = ir_find_enum(program, stmt->resolved_type);
  if (!item_enum) {
    ir_mark_unsupported(ir, "direct backend match currently supports only enums", stmt->line, stmt->column, stmt->resolved_type ? stmt->resolved_type : "unknown match type");
    return false;
  }
  IrTypeKind backing = ir_enum_backing_type(item_enum);
  if (!ir_type_is_value(backing)) {
    ir_mark_unsupported(ir, "direct backend enum match backing type is unsupported", stmt->line, stmt->column, item_enum->type ? item_enum->type : "u8");
    return false;
  }
  IrInstr *nested_items = NULL;
  size_t nested_len = 0;
  size_t nested_cap = 0;
  for (size_t reverse_index = stmt->match_arms.len; reverse_index > 0; reverse_index--) {
    const MatchArm *arm = &stmt->match_arms.items[reverse_index - 1];
    if (arm->guard || arm->payload_name || arm->range_end) {
      ir_free_instrs(nested_items, nested_len);
      free(nested_items);
      ir_mark_unsupported(ir, "direct backend enum match does not support guards, payload bindings, or ranges", arm->line, arm->column, arm->case_name);
      return false;
    }
    if (strcmp(arm->case_name, "_") == 0) {
      if (nested_len > 0) {
        ir_free_instrs(nested_items, nested_len);
        free(nested_items);
        ir_mark_unsupported(ir, "direct backend enum match fallback must be the final arm", arm->line, arm->column, "fallback before concrete arms");
        return false;
      }
      size_t scope_mark = ir_active_local_mark(ir);
      bool body_ok = ir_lower_stmt_vec(program, ir, mir_fun, &arm->body, &nested_items, &nested_len, &nested_cap, saw_return);
      ir_active_local_restore(ir, scope_mark);
      if (!body_ok) {
        ir_free_instrs(nested_items, nested_len);
        free(nested_items);
        return false;
      }
      continue;
    }
    unsigned long long case_value = 0;
    if (!ir_enum_case_value(item_enum, arm->case_name, &case_value)) {
      ir_free_instrs(nested_items, nested_len);
      free(nested_items);
      ir_mark_unsupported(ir, "direct backend enum match case is unknown", arm->line, arm->column, arm->case_name);
      return false;
    }
    IrValue *matched = NULL;
    if (!ir_lower_expr(program, ir, mir_fun, stmt->expr, &matched)) {
      ir_free_instrs(nested_items, nested_len);
      free(nested_items);
      return false;
    }
    if (matched->type != backing) {
      ir_free_value(matched);
      ir_free_instrs(nested_items, nested_len);
      free(nested_items);
      ir_mark_unsupported(ir, "direct backend enum match value does not match enum backing type", stmt->line, stmt->column, stmt->resolved_type);
      return false;
    }
    IrValue *case_literal = ir_new_value(ir, IR_VALUE_INT, backing, arm->line, arm->column);
    case_literal->int_value = case_value;
    IrValue *cond = ir_new_value(ir, IR_VALUE_COMPARE, IR_TYPE_BOOL, arm->line, arm->column);
    cond->compare_op = IR_CMP_EQ;
    cond->left = matched;
    cond->right = case_literal;

    IrInstr instr = {.kind = IR_INSTR_IF, .value = cond, .else_instrs = nested_items, .else_len = nested_len, .else_cap = nested_cap, .line = arm->line, .column = arm->column};
    size_t scope_mark = ir_active_local_mark(ir);
    bool body_ok = ir_lower_stmt_vec(program, ir, mir_fun, &arm->body, &instr.then_instrs, &instr.then_len, &instr.then_cap, saw_return);
    ir_active_local_restore(ir, scope_mark);
    if (!body_ok) {
      ir_free_value(cond);
      ir_free_instrs(nested_items, nested_len);
      free(nested_items);
      return false;
    }
    nested_items = NULL;
    nested_len = 0;
    nested_cap = 0;
    ir_instr_vec_push(ir, &nested_items, &nested_len, &nested_cap, instr);
  }
  for (size_t i = 0; i < nested_len; i++) {
    ir_instr_vec_push(ir, out_items, out_len, out_cap, nested_items[i]);
  }
  free(nested_items);
  return true;
}

static bool ir_lower_expr_for_type(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *expr, IrTypeKind target_type, bool allow_i32_default, unsigned line, unsigned column, IrValue **out) {
  *out = NULL;
  if (!expr && allow_i32_default && target_type == IR_TYPE_I32) {
    *out = ir_new_value(ir, IR_VALUE_INT, IR_TYPE_I32, line, column);
    (*out)->int_value = 0;
    return true;
  }
  if (target_type == IR_TYPE_BYTE_VIEW && expr && expr->kind == EXPR_CHECK) {
    return ir_lower_expr(program, ir, fun, expr, out);
  }
  if (target_type == IR_TYPE_BYTE_VIEW) {
    return ir_lower_byte_view(program, ir, fun, expr, out);
  }
  if (target_type == IR_TYPE_MAYBE_BYTE_VIEW && expr && expr->resolved_type &&
      ir_type_kind(expr->resolved_type) == IR_TYPE_BYTE_VIEW) {
    IrValue *view = NULL;
    if (!ir_lower_byte_view(program, ir, fun, expr, &view)) return false;
    *out = ir_new_maybe_byte_view_literal(ir, true, view, line, column);
    return true;
  }
  return ir_lower_expr(program, ir, fun, expr, out);
}

static bool ir_lower_stmt_to_vec(const Program *program, IrProgram *ir, IrFunction *mir_fun, const Stmt *stmt, IrInstr **out_items, size_t *out_len, size_t *out_cap, bool *saw_return) {
  if (stmt->kind == STMT_LET) {
    const IrLocal *local = ir_function_find_local(mir_fun, stmt->name);
    if (local && local->is_array) {
      if (!ir_lower_array_initializer(program, ir, mir_fun, local, stmt->expr, out_items, out_len, out_cap, stmt->line, stmt->column)) return false;
      ir_active_local_push(ir, stmt->name);
      return true;
    }
    if (local && local->is_record) {
      if (!ir_lower_shape_initializer(program, ir, mir_fun, local, stmt->expr, out_items, out_len, out_cap, stmt->line, stmt->column)) return false;
      ir_active_local_push(ir, stmt->name);
      return true;
    }
    IrValue *value = NULL;
    if (!local) return false;
    if (!ir_lower_expr_for_type(program, ir, mir_fun, stmt->expr, local->type, false, stmt->line, stmt->column, &value)) return false;
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_LOCAL_SET, .local_index = local->index, .value = value, .line = stmt->line, .column = stmt->column});
    ir_active_local_push(ir, stmt->name);
    return true;
  }
  if (stmt->kind == STMT_ASSIGN) {
    if (stmt->target && stmt->target->kind == EXPR_INDEX) {
      return ir_lower_index_store(program, ir, mir_fun, stmt->target, stmt->expr, out_items, out_len, out_cap, stmt->line, stmt->column);
    }
    if (stmt->target && stmt->target->kind == EXPR_MEMBER && stmt->target->left && stmt->target->left->kind == EXPR_IDENT) {
      const IrLocal *local = ir_function_find_local(mir_fun, stmt->target->left->text);
      unsigned field_offset = 0;
      IrTypeKind field_type = IR_TYPE_UNSUPPORTED;
      if (local && local->is_record && ir_shape_field_info(program, local->shape_name, stmt->target->text, &field_offset, &field_type, NULL)) {
        if (!local->is_mutable) {
          ir_mark_unsupported(ir, "direct backend record field assignment target must be mutable", stmt->line, stmt->column, local->name);
          return false;
        }
        IrValue *value = NULL;
        if (!ir_lower_expr(program, ir, mir_fun, stmt->expr, &value)) return false;
        if (value->type != field_type) {
          ir_free_value(value);
          ir_mark_unsupported(ir, "direct backend record field assignment type does not match field", stmt->line, stmt->column, stmt->target->text);
          return false;
        }
        ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_FIELD_STORE, .local_index = local->index, .field_offset = field_offset, .value = value, .line = stmt->line, .column = stmt->column});
        return true;
      }
    }
    if (!stmt->target || stmt->target->kind != EXPR_IDENT) {
      ir_mark_unsupported(ir, "direct backend assignment currently supports only local variables", stmt->line, stmt->column, "non-local assignment");
      return false;
    }
    const IrLocal *local = ir_function_find_local(mir_fun, stmt->target->text);
    IrValue *value = NULL;
    if (!local) return false;
    if (!ir_lower_expr_for_type(program, ir, mir_fun, stmt->expr, local->type, mir_fun->return_type == IR_TYPE_I32, stmt->line, stmt->column, &value)) return false;
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_LOCAL_SET, .local_index = local->index, .value = value, .line = stmt->line, .column = stmt->column});
    return true;
  }
  if (stmt->kind == STMT_RETURN) {
    *saw_return = true;
    IrValue *value = NULL;
    if (mir_fun->return_type == IR_TYPE_VOID) {
      if (stmt->expr) {
        ir_mark_unsupported(ir, "direct backend void function cannot return a value", stmt->line, stmt->column, mir_fun->name);
        return false;
      }
    } else {
      if (!ir_lower_expr_for_type(program, ir, mir_fun, stmt->expr, mir_fun->return_type, true, stmt->line, stmt->column, &value)) return false;
    }
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_RETURN, .value = value, .line = stmt->line, .column = stmt->column});
    return true;
  }
  if (stmt->kind == STMT_RAISE) {
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_RAISE, .field_offset = 1, .error_code = ir_error_code_for_name(stmt->name), .line = stmt->line, .column = stmt->column});
    return true;
  }
  if (stmt->kind == STMT_CHECK && (ir_is_world_stream_write(mir_fun, stmt->expr, "out") || ir_is_world_stream_write(mir_fun, stmt->expr, "err"))) {
    IrValue *bytes = NULL;
    if (!ir_lower_byte_view(program, ir, mir_fun, stmt->expr->args.items[0], &bytes)) return false;
    if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){
      .kind = IR_INSTR_WORLD_WRITE,
      .field_offset = ir_is_world_stream_write(mir_fun, stmt->expr, "err") ? 2 : 1,
      .value = bytes,
      .line = stmt->line,
      .column = stmt->column
    });
    return true;
  }
  if (stmt->kind == STMT_CHECK) {
    IrValue *checked = NULL;
    if (!ir_lower_expr(program, ir, mir_fun, stmt->expr, &checked)) return false;
    if (!checked || checked->type != IR_TYPE_I64) {
      ir_free_value(checked);
      ir_mark_unsupported(ir, "direct backend check statement requires a fallible value", stmt->line, stmt->column, "non-fallible check");
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_CHECK, checked->element_type, stmt->line, stmt->column);
    value->left = checked;
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_EXPR, .value = value, .line = stmt->line, .column = stmt->column});
    return true;
  }
  if (stmt->kind == STMT_EXPR) {
    IrValue *value = NULL;
    if (!stmt->expr || stmt->expr->kind != EXPR_CALL) {
      ir_mark_unsupported(ir, "direct backend expression statements currently support only Void helper calls", stmt->line, stmt->column, "non-call expression statement");
      return false;
    }
    if (!ir_lower_expr(program, ir, mir_fun, stmt->expr, &value)) return false;
    if (!value || value->type != IR_TYPE_VOID) {
      ir_free_value(value);
      ir_mark_unsupported(ir, "direct backend expression statement call must return Void", stmt->line, stmt->column, "non-Void call");
      return false;
    }
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_EXPR, .value = value, .line = stmt->line, .column = stmt->column});
    return true;
  }
  if (stmt->kind == STMT_IF) {
    IrValue *cond = NULL;
    if (!ir_lower_expr(program, ir, mir_fun, stmt->expr, &cond)) return false;
    if (cond->type != IR_TYPE_BOOL) {
      ir_free_value(cond);
      ir_mark_unsupported(ir, "direct backend if condition must be Bool", stmt->line, stmt->column, "non-Bool condition");
      return false;
    }
    IrInstr instr = {.kind = IR_INSTR_IF, .value = cond, .line = stmt->line, .column = stmt->column};
    size_t scope_mark = ir_active_local_mark(ir);
    bool then_ok = ir_lower_stmt_vec(program, ir, mir_fun, &stmt->then_body, &instr.then_instrs, &instr.then_len, &instr.then_cap, saw_return);
    ir_active_local_restore(ir, scope_mark);
    bool else_ok = then_ok && ir_lower_stmt_vec(program, ir, mir_fun, &stmt->else_body, &instr.else_instrs, &instr.else_len, &instr.else_cap, saw_return);
    ir_active_local_restore(ir, scope_mark);
    if (!then_ok || !else_ok) {
      ir_free_value(cond);
      ir_free_instrs(instr.then_instrs, instr.then_len);
      ir_free_instrs(instr.else_instrs, instr.else_len);
      free(instr.then_instrs);
      free(instr.else_instrs);
      return false;
    }
    ir_instr_vec_push(ir, out_items, out_len, out_cap, instr);
    return true;
  }
  if (stmt->kind == STMT_WHILE) {
    IrValue *cond = NULL;
    if (!ir_lower_expr(program, ir, mir_fun, stmt->expr, &cond)) return false;
    if (cond->type != IR_TYPE_BOOL) {
      ir_free_value(cond);
      ir_mark_unsupported(ir, "direct backend while condition must be Bool", stmt->line, stmt->column, "non-Bool condition");
      return false;
    }
    IrInstr instr = {.kind = IR_INSTR_WHILE, .value = cond, .line = stmt->line, .column = stmt->column};
    size_t scope_mark = ir_active_local_mark(ir);
    bool body_ok = ir_lower_stmt_vec(program, ir, mir_fun, &stmt->then_body, &instr.then_instrs, &instr.then_len, &instr.then_cap, saw_return);
    ir_active_local_restore(ir, scope_mark);
    if (!body_ok) {
      ir_free_value(cond);
      ir_free_instrs(instr.then_instrs, instr.then_len);
      free(instr.then_instrs);
      return false;
    }
    ir_instr_vec_push(ir, out_items, out_len, out_cap, instr);
    return true;
  }
  if (stmt->kind == STMT_MATCH) {
    return ir_lower_enum_match(program, ir, mir_fun, stmt, out_items, out_len, out_cap, saw_return);
  }
  if (stmt->kind == STMT_BREAK || stmt->kind == STMT_CONTINUE) {
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = stmt->kind == STMT_BREAK ? IR_INSTR_BREAK : IR_INSTR_CONTINUE, .line = stmt->line, .column = stmt->column});
    return true;
  }
  ir_mark_unsupported(ir, "direct backend statement kind is unsupported", stmt->line, stmt->column, mir_fun->name);
  return false;
}

static bool ir_lower_stmt_vec(const Program *program, IrProgram *ir, IrFunction *mir_fun, const StmtVec *body, IrInstr **out_items, size_t *out_len, size_t *out_cap, bool *saw_return) {
  for (size_t i = 0; i < body->len; i++) {
    if (!ir_lower_stmt_to_vec(program, ir, mir_fun, body->items[i], out_items, out_len, out_cap, saw_return)) return false;
  }
  return true;
}

static IrFunction *ir_program_push_function(IrProgram *ir, const Function *source, const char *stable_id_text) {
  ir->functions = ir_grow_tracked_items(ir, ir->functions, ir->function_len, &ir->function_cap, 4, sizeof(IrFunction));
  IrFunction *fun = &ir->functions[ir->function_len++];
  bool hosted_world_main = ir_is_hosted_world_main(source);
  IrTypeKind source_return_type = ir_type_kind(source->return_type);
  IrTypeKind mir_return_type = hosted_world_main ? IR_TYPE_I32 : (source->raises ? IR_TYPE_I64 : source_return_type);
  IrTypeKind return_element_type = source_return_type == IR_TYPE_BYTE_VIEW ? ir_view_element_type_for_type(source->return_type) : IR_TYPE_UNSUPPORTED;
  if (source_return_type == IR_TYPE_MAYBE_SCALAR) return_element_type = ir_maybe_scalar_element_type(source->return_type);
  ZBuf stable_id;
  zbuf_init(&stable_id);
  zbuf_append(&stable_id, stable_id_text ? stable_id_text : "main.");
  if (!stable_id_text) zbuf_append(&stable_id, source->name ? source->name : "");
  *fun = (IrFunction){
    .name = z_strdup(source->name),
    .stable_id = stable_id.data,
    .world_param_name = hosted_world_main && source->params.items[0].name ? z_strdup(source->params.items[0].name) : NULL,
    .return_type = mir_return_type,
    .value_return_type = source_return_type,
    .return_element_type = return_element_type,
    .is_exported = source->export_c || hosted_world_main,
    .raises = hosted_world_main ? false : source->raises,
    .line = source->line,
    .column = source->column
  };
  return fun;
}

static bool ir_collect_stmt_locals(const Program *program, IrProgram *ir, IrFunction *mir_fun, const StmtVec *body);

static bool ir_collect_function_locals(const Program *program, IrProgram *ir, IrFunction *mir_fun, const Function *source) {
  bool hosted_world_main = ir_is_hosted_world_main(source);
  for (size_t i = 0; i < source->params.len; i++) {
    const Param *param = &source->params.items[i];
    if (hosted_world_main && i == 0 && strcmp(param->type ? param->type : "", "World") == 0) continue;
    IrTypeKind type = ir_type_kind(param->type);
    if (!ir_type_is_direct_param_abi(type)) {
      ir_mark_unsupported(ir, "direct backend parameter type is unsupported", param->line, param->column, param->type);
      return false;
    }
    IrTypeKind element_type = type == IR_TYPE_BYTE_VIEW ? ir_view_element_type_for_type(param->type) : IR_TYPE_UNSUPPORTED;
    ir_function_push_local(ir, mir_fun, param->name, type, true, false, false, NULL, element_type, 0, 0, 0, ir_type_name_is_mutable_byte_view(param->type), param->line, param->column);
  }
  if (!ir_collect_stmt_locals(program, ir, mir_fun, &source->body)) return false;

  size_t offset = 0;
  for (size_t i = 0; i < mir_fun->local_len; i++) {
    IrLocal *local = &mir_fun->locals[i];
    offset = ir_align_to(offset, local->alignment);
    offset += local->byte_size;
    local->frame_offset = (unsigned)offset;
  }
  mir_fun->frame_bytes = ir_align_to(offset, 16);
  ir->direct_stack_bytes += mir_fun->frame_bytes;
  if (mir_fun->frame_bytes > ir->direct_max_frame_bytes) ir->direct_max_frame_bytes = mir_fun->frame_bytes;
  return true;
}

static bool ir_collect_stmt_locals(const Program *program, IrProgram *ir, IrFunction *mir_fun, const StmtVec *body) {
  for (size_t i = 0; i < body->len; i++) {
    const Stmt *stmt = body->items[i];
    if (stmt->kind == STMT_LET) {
      const char *stmt_type = stmt->resolved_type ? stmt->resolved_type : stmt->type;
      IrTypeKind type = ir_type_kind(stmt_type);
      unsigned array_len = 0;
      IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
      if (ir_parse_fixed_array_type(stmt_type, &array_len, &element_type)) {
        ir_function_push_local(ir, mir_fun, stmt->name, IR_TYPE_UNSUPPORTED, false, true, false, NULL, element_type, array_len, 0, 0, stmt->mutable_binding, stmt->line, stmt->column);
        continue;
      }
      unsigned record_size = 0;
      unsigned record_align = 0;
      if (ir_shape_layout(program, stmt_type, &record_size, &record_align)) {
        ir_function_push_local(ir, mir_fun, stmt->name, IR_TYPE_RECORD, false, false, true, stmt_type, IR_TYPE_UNSUPPORTED, 0, record_size, record_align, stmt->mutable_binding, stmt->line, stmt->column);
        continue;
      }
      const EnumDecl *item_enum = ir_find_enum(program, stmt_type);
      if (item_enum) {
        IrTypeKind backing = ir_enum_backing_type(item_enum);
        if (!ir_type_is_value(backing)) {
          ir_mark_unsupported(ir, "direct backend enum backing type is unsupported", stmt->line, stmt->column, item_enum->type ? item_enum->type : "u8");
          return false;
        }
        ir_function_push_local(ir, mir_fun, stmt->name, backing, false, false, false, NULL, IR_TYPE_UNSUPPORTED, 0, 0, 0, stmt->mutable_binding, stmt->line, stmt->column);
        continue;
      }
      if (!ir_type_is_direct_local(type)) {
        if (stmt_type && (strcmp(stmt_type, "PageAlloc") == 0 || strcmp(stmt_type, "GeneralAlloc") == 0 || strcmp(stmt_type, "NullAlloc") == 0)) {
          ir_mark_unsupported(ir, "direct backend allocator local requires FixedBufAlloc", stmt->line, stmt->column, stmt_type);
          snprintf(ir->mir_help, sizeof(ir->mir_help), "allocate from a fixed array with std.mem.fixedBufAlloc, or split large buffers across helper functions with smaller frames; PageAlloc, GeneralAlloc, and NullAlloc locals do not lower to direct backends yet");
          return false;
        }
        ir_mark_unsupported(ir, "direct backend local type is unsupported", stmt->line, stmt->column, stmt_type ? stmt_type : "inferred unknown");
        return false;
      }
      bool mutable_byte_view = ir_type_name_is_mutable_byte_view(stmt_type);
      IrTypeKind view_element_type = type == IR_TYPE_BYTE_VIEW ? ir_view_element_type_for_type(stmt_type) : IR_TYPE_UNSUPPORTED;
      if (type == IR_TYPE_MAYBE_SCALAR) view_element_type = ir_maybe_scalar_element_type(stmt_type);
      ir_function_push_local(ir, mir_fun, stmt->name, type, false, false, false, NULL, view_element_type, 0, 0, 0, stmt->mutable_binding || mutable_byte_view, stmt->line, stmt->column);
    } else if (stmt->kind == STMT_IF) {
      if (!ir_collect_stmt_locals(program, ir, mir_fun, &stmt->then_body) || !ir_collect_stmt_locals(program, ir, mir_fun, &stmt->else_body)) return false;
    } else if (stmt->kind == STMT_WHILE) {
      if (!ir_collect_stmt_locals(program, ir, mir_fun, &stmt->then_body)) return false;
    }
  }
  return true;
}

static size_t ir_estimate_local_bytes(const Program *program, const char *type_text, unsigned *out_align) {
  unsigned array_len = 0;
  IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
  if (out_align) *out_align = 8;
  if (ir_parse_fixed_array_type_for_program(program, type_text, &array_len, &element_type)) {
    if (out_align) *out_align = ir_type_alignment(element_type);
    return (size_t)ir_type_byte_size(element_type) * (size_t)array_len;
  }
  unsigned record_size = 0;
  unsigned record_align = 0;
  if (ir_shape_layout(program, type_text, &record_size, &record_align)) {
    if (out_align) *out_align = record_align ? record_align : 8;
    return record_size;
  }
  IrTypeKind kind = ir_type_kind_for_program(program, type_text);
  if (kind == IR_TYPE_BYTE_VIEW || kind == IR_TYPE_ALLOC || kind == IR_TYPE_VEC || kind == IR_TYPE_MAYBE_SCALAR) return 16;
  if (kind == IR_TYPE_MAYBE_BYTE_VIEW) return 24;
  return 8;
}

static size_t ir_estimate_stmt_frame_bytes(const Program *program, const StmtVec *body, size_t offset, size_t limit, const Stmt **out_over) {
  for (size_t i = 0; body && i < body->len; i++) {
    const Stmt *stmt = body->items[i];
    if (stmt->kind == STMT_LET) {
      unsigned align = 8;
      size_t byte_size = ir_estimate_local_bytes(program, stmt->resolved_type ? stmt->resolved_type : stmt->type, &align);
      offset = ir_align_to(offset, align) + byte_size;
      if (offset > limit && out_over && !*out_over) *out_over = stmt;
    } else if (stmt->kind == STMT_IF) {
      offset = ir_estimate_stmt_frame_bytes(program, &stmt->then_body, offset, limit, out_over);
      offset = ir_estimate_stmt_frame_bytes(program, &stmt->else_body, offset, limit, out_over);
    } else if (stmt->kind == STMT_WHILE) {
      offset = ir_estimate_stmt_frame_bytes(program, &stmt->then_body, offset, limit, out_over);
    }
  }
  return offset;
}

bool z_function_frame_locals_within_limit(const Program *program, const Function *fun, size_t limit, size_t *out_total, const Stmt **out_over) {
  if (out_total) *out_total = 0;
  if (out_over) *out_over = NULL;
  if (!fun || fun->type_params.len > 0) return true;
  size_t offset = 0;
  for (size_t i = 0; i < fun->params.len; i++) {
    const Param *param = &fun->params.items[i];
    if (param->type && strcmp(param->type, "World") == 0) continue;
    unsigned align = 8;
    size_t byte_size = ir_estimate_local_bytes(program, param->type, &align);
    offset = ir_align_to(offset, align) + byte_size;
  }
  offset = ir_estimate_stmt_frame_bytes(program, &fun->body, offset, limit, out_over);
  size_t total = ir_align_to(offset, 16);
  if (out_total) *out_total = total;
  return total <= limit;
}

static bool ir_lower_function_body(const Program *program, IrProgram *ir, IrFunction *mir_fun, const Function *source) {
  if (source->type_params.len > 0 || source->is_test) {
    ir_mark_unsupported(ir, "direct backend MVP does not support generics or tests", source->line, source->column, source->name);
    return false;
  }
  bool hosted_world_main = ir_is_hosted_world_main(source);
  IrTypeKind return_type = hosted_world_main ? IR_TYPE_I32 : ir_type_kind(source->return_type);
  if (!hosted_world_main && source->raises && !ir_type_is_direct_fallible_value(return_type)) {
    ir_mark_unsupported(ir, "direct backend fallible return type is unsupported", source->line, source->column, source->return_type);
    return false;
  }
  if (!hosted_world_main && !source->raises && return_type != IR_TYPE_VOID && !ir_type_is_direct_return_abi(return_type)) {
    ir_mark_unsupported(ir, "direct backend return type is unsupported", source->line, source->column, source->return_type);
    return false;
  }
  if (!ir_collect_function_locals(program, ir, mir_fun, source)) return false;
  size_t scope_mark = ir_active_local_mark(ir);
  for (size_t i = 0; i < source->params.len; i++) {
    ir_active_local_push(ir, source->params.items[i].name);
  }
  bool saw_return = false;
  bool lowered = ir_lower_stmt_vec(program, ir, mir_fun, &source->body, &mir_fun->instrs, &mir_fun->instr_len, &mir_fun->instr_cap, &saw_return);
  ir_active_local_restore(ir, scope_mark);
  if (!lowered) return false;
  if (hosted_world_main && !saw_return) {
    IrValue *exit_code = ir_new_value(ir, IR_VALUE_INT, IR_TYPE_I32, source->line, source->column);
    exit_code->int_value = 0;
    ir_instr_vec_push(ir, &mir_fun->instrs, &mir_fun->instr_len, &mir_fun->instr_cap, (IrInstr){
      .kind = IR_INSTR_RETURN,
      .value = exit_code,
      .line = source->line,
      .column = source->column
    });
    saw_return = true;
  }
  if (return_type != IR_TYPE_VOID && !saw_return) {
    ir_mark_unsupported(ir, "direct backend function must return a value", source->line, source->column, source->name);
    return false;
  }
  return true;
}

static void ir_free_param_vec_shallow(ParamVec *params) {
  for (size_t i = 0; params && i < params->len; i++) {
    free(params->items[i].name);
    free(params->items[i].type);
  }
  free(params ? params->items : NULL);
  if (params) *params = (ParamVec){0};
}

static const char *ir_specialization_arg_for_binder(const Function *fun, const TypeArgVec *type_args, ZTypeBinderId binder) {
  if (!fun || !type_args || binder == Z_TYPE_BINDER_ID_INVALID) return NULL;
  size_t index = (size_t)binder - 1;
  if (index >= fun->type_params.len || index >= type_args->len) return NULL;
  return type_args->items[index].type;
}

static ZTypeBinderDecl *ir_specialization_binder_decls(const Function *fun) {
  if (!fun || fun->type_params.len == 0) return NULL;
  ZTypeBinderDecl *decls = z_checked_calloc(fun->type_params.len, sizeof(ZTypeBinderDecl));
  for (size_t i = 0; i < fun->type_params.len; i++) {
    decls[i] = (ZTypeBinderDecl){
      .name = fun->type_params.items[i].name,
      .kind = fun->type_params.items[i].is_static ? Z_TYPE_BINDER_STATIC : Z_TYPE_BINDER_TYPE,
      .id = (ZTypeBinderId)(i + 1),
      .static_type = fun->type_params.items[i].type ? fun->type_params.items[i].type : "usize"
    };
  }
  return decls;
}

static void ir_specialize_type_core_into(ZBuf *buf, const ZTypeArena *arena, ZTypeId type, const Function *fun, const TypeArgVec *type_args);

static void ir_specialize_static_value_into(ZBuf *buf, const ZStaticValue *value, const Function *fun, const TypeArgVec *type_args) {
  if (value && value->kind == Z_STATIC_VALUE_BINDER) {
    const char *arg = ir_specialization_arg_for_binder(fun, type_args, value->binder);
    if (arg) {
      zbuf_append(buf, arg);
      return;
    }
  }
  char *formatted = z_static_value_format(value);
  zbuf_append(buf, formatted ? formatted : "Unknown");
  free(formatted);
}

static void ir_specialize_type_arg_into(ZBuf *buf, const ZTypeArena *arena, const ZTypeArg *arg, const Function *fun, const TypeArgVec *type_args) {
  if (!arg) return;
  if (arg->kind == Z_TYPE_ARG_STATIC) {
    ir_specialize_static_value_into(buf, &arg->as.static_value, fun, type_args);
  } else {
    ir_specialize_type_core_into(buf, arena, arg->as.type, fun, type_args);
  }
}

static void ir_specialize_type_core_into(ZBuf *buf, const ZTypeArena *arena, ZTypeId type, const Function *fun, const TypeArgVec *type_args) {
  ZTypeNodeKind kind = z_type_kind(arena, type);
  if (kind == Z_TYPE_NODE_NAME) {
    zbuf_append(buf, z_type_name(arena, type) ? z_type_name(arena, type) : "Unknown");
  } else if (kind == Z_TYPE_NODE_BINDER) {
    const char *arg = ir_specialization_arg_for_binder(fun, type_args, z_type_binder(arena, type));
    zbuf_append(buf, arg ? arg : (z_type_name(arena, type) ? z_type_name(arena, type) : "Unknown"));
  } else if (kind == Z_TYPE_NODE_CONST) {
    zbuf_append(buf, "const ");
    ir_specialize_type_core_into(buf, arena, z_type_const_inner(arena, type), fun, type_args);
  } else if (kind == Z_TYPE_NODE_ARRAY) {
    zbuf_append_char(buf, '[');
    ir_specialize_static_value_into(buf, z_type_array_length(arena, type), fun, type_args);
    zbuf_append_char(buf, ']');
    ir_specialize_type_core_into(buf, arena, z_type_array_element(arena, type), fun, type_args);
  } else if (kind == Z_TYPE_NODE_APPLY) {
    zbuf_append(buf, z_type_name(arena, type) ? z_type_name(arena, type) : "Unknown");
    zbuf_append_char(buf, '<');
    for (size_t i = 0; i < z_type_apply_arg_len(arena, type); i++) {
      if (i > 0) zbuf_append_char(buf, ',');
      ir_specialize_type_arg_into(buf, arena, z_type_apply_arg(arena, type, i), fun, type_args);
    }
    zbuf_append_char(buf, '>');
  } else {
    zbuf_append(buf, "Unknown");
  }
}

static char *ir_specialize_static_arg_text(const char *text, const Function *fun, const TypeArgVec *type_args) {
  if (!text || !fun || !type_args || fun->type_params.len == 0) return NULL;
  ZTypeBinderDecl *decls = ir_specialization_binder_decls(fun);
  ZTypeBinderScope scope = {.items = decls, .len = fun->type_params.len};
  ZStaticValue value = {0};
  ZTypeParseError error = {0};
  if (!z_static_value_parse_with_binders(text, &scope, &value, &error) || value.kind != Z_STATIC_VALUE_BINDER) {
    z_static_value_free(&value);
    free(decls);
    return NULL;
  }
  ZBuf buf;
  zbuf_init(&buf);
  ir_specialize_static_value_into(&buf, &value, fun, type_args);
  z_static_value_free(&value);
  free(decls);
  return buf.data ? buf.data : z_strdup("");
}

static char *ir_specialize_type_text(const char *type, const Function *fun, const TypeArgVec *type_args) {
  if (!type) return z_strdup("Unknown");
  if (!fun || !type_args || fun->type_params.len == 0) return z_strdup(type);
  char *static_arg = ir_specialize_static_arg_text(type, fun, type_args);
  if (static_arg) return static_arg;
  ZTypeBinderDecl *decls = ir_specialization_binder_decls(fun);
  ZTypeBinderScope scope = {.items = decls, .len = fun->type_params.len};
  ZTypeArena arena;
  z_type_arena_init(&arena);
  ZTypeId parsed = Z_TYPE_ID_INVALID;
  ZTypeParseError error = {0};
  if (!z_type_parse_with_binders(&arena, type, &scope, &parsed, &error)) {
    z_type_arena_free(&arena);
    free(decls);
    return z_strdup(ir_substitute_type_param(fun, type_args, type));
  }
  ZBuf buf;
  zbuf_init(&buf);
  ir_specialize_type_core_into(&buf, &arena, parsed, fun, type_args);
  z_type_arena_free(&arena);
  free(decls);
  return buf.data ? buf.data : z_strdup("");
}

static void ir_specialize_stmt_types(StmtVec *body, const Function *fun, const TypeArgVec *type_args);

static void ir_specialize_expr_types(Expr *expr, const Function *fun, const TypeArgVec *type_args) {
  if (!expr) return;
  if (expr->resolved_type) {
    char *substituted = ir_specialize_type_text(expr->resolved_type, fun, type_args);
    free(expr->resolved_type);
    expr->resolved_type = substituted;
  }
  for (size_t i = 0; i < expr->type_args.len; i++) {
    char *substituted = ir_specialize_type_text(expr->type_args.items[i].type, fun, type_args);
    free(expr->type_args.items[i].type);
    expr->type_args.items[i].type = substituted;
  }
  for (size_t i = 0; i < expr->checked_type_args.len; i++) {
    char *substituted = ir_specialize_type_text(expr->checked_type_args.items[i].type, fun, type_args);
    free(expr->checked_type_args.items[i].type);
    expr->checked_type_args.items[i].type = substituted;
  }
  ir_specialize_expr_types(expr->left, fun, type_args);
  ir_specialize_expr_types(expr->right, fun, type_args);
  for (size_t i = 0; i < expr->args.len; i++) ir_specialize_expr_types(expr->args.items[i], fun, type_args);
  for (size_t i = 0; i < expr->fields.len; i++) ir_specialize_expr_types(expr->fields.items[i].value, fun, type_args);
}

static void ir_specialize_stmt_types(StmtVec *body, const Function *fun, const TypeArgVec *type_args) {
  for (size_t i = 0; body && i < body->len; i++) {
    Stmt *stmt = body->items[i];
    if (!stmt) continue;
    if (stmt->type) {
      char *substituted = ir_specialize_type_text(stmt->type, fun, type_args);
      free(stmt->type);
      stmt->type = substituted;
    }
    if (stmt->resolved_type) {
      char *substituted = ir_specialize_type_text(stmt->resolved_type, fun, type_args);
      free(stmt->resolved_type);
      stmt->resolved_type = substituted;
    }
    ir_specialize_expr_types(stmt->target, fun, type_args);
    ir_specialize_expr_types(stmt->expr, fun, type_args);
    ir_specialize_expr_types(stmt->range_end, fun, type_args);
    ir_specialize_stmt_types(&stmt->then_body, fun, type_args);
    ir_specialize_stmt_types(&stmt->else_body, fun, type_args);
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      ir_specialize_stmt_types(&stmt->match_arms.items[arm_index].body, fun, type_args);
    }
  }
}

static void ir_push_specialized_function(FunctionVec *functions, const Function *source, const TypeArgVec *type_args) {
  push_function_clone(functions, source);
  Function *fun = &functions->items[functions->len - 1];
  char *specialized_name = ir_specialized_function_name(source, type_args);
  free(fun->name);
  fun->name = specialized_name;
  char *return_type = ir_specialize_type_text(fun->return_type, source, type_args);
  free(fun->return_type);
  fun->return_type = return_type;
  for (size_t i = 0; i < fun->params.len; i++) {
    char *param_type = ir_specialize_type_text(fun->params.items[i].type, source, type_args);
    free(fun->params.items[i].type);
    fun->params.items[i].type = param_type;
  }
  ir_specialize_stmt_types(&fun->body, source, type_args);
  ir_free_param_vec_shallow(&fun->type_params);
  fun->export_c = false;
  fun->is_public = false;
}

static bool ir_function_vec_has_name(const FunctionVec *functions, const char *name) {
  for (size_t i = 0; functions && i < functions->len; i++) {
    if (functions->items[i].name && name && strcmp(functions->items[i].name, name) == 0) return true;
  }
  return false;
}

static bool ir_collect_generic_specializations_from_expr(FunctionVec *functions, ZSpecializationPlan *plan, IrProgram *ir, const Program *program, const Expr *expr);

static bool ir_collect_generic_specializations_from_stmt_vec(FunctionVec *functions, ZSpecializationPlan *plan, IrProgram *ir, const Program *program, const StmtVec *body) {
  for (size_t i = 0; body && i < body->len; i++) {
    const Stmt *stmt = body->items[i];
    if (!stmt) continue;
    if (!ir_collect_generic_specializations_from_expr(functions, plan, ir, program, stmt->target)) return false;
    if (!ir_collect_generic_specializations_from_expr(functions, plan, ir, program, stmt->expr)) return false;
    if (!ir_collect_generic_specializations_from_expr(functions, plan, ir, program, stmt->range_end)) return false;
    if (!ir_collect_generic_specializations_from_stmt_vec(functions, plan, ir, program, &stmt->then_body)) return false;
    if (!ir_collect_generic_specializations_from_stmt_vec(functions, plan, ir, program, &stmt->else_body)) return false;
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      if (!ir_collect_generic_specializations_from_stmt_vec(functions, plan, ir, program, &stmt->match_arms.items[arm_index].body)) return false;
    }
  }
  return true;
}

static bool ir_collect_generic_specializations_from_expr(FunctionVec *functions, ZSpecializationPlan *plan, IrProgram *ir, const Program *program, const Expr *expr) {
  if (!expr) return true;
  if (expr->kind == EXPR_CALL && expr->left) {
    const char *source_name = NULL;
    char *callee_name = NULL;
    if (expr->left->kind == EXPR_IDENT) {
      source_name = expr->left->text;
    } else {
      callee_name = ir_expr_callee_name(expr->left);
      source_name = z_std_source_target_for_public_call(callee_name);
    }
    const Function *callee = source_name ? ir_find_source_function(program, source_name, NULL) : NULL;
    const TypeArgVec *type_args = ir_call_type_args(expr);
    if (callee && callee->type_params.len > 0 && type_args && type_args->len == callee->type_params.len) {
      char *specialized_name = NULL;
      ZSpecializationAddResult add_result = z_specialization_plan_add(plan, callee, type_args, &specialized_name);
      if (add_result == Z_SPECIALIZATION_ADD_LIMIT) {
        free(specialized_name);
        free(callee_name);
        ir_mark_unsupported(ir, "direct backend generic specialization plan exceeded its instantiation limit", expr->line, expr->column, callee->name);
        return false;
      }
      if (add_result == Z_SPECIALIZATION_ADD_NAME_COLLISION) {
        free(specialized_name);
        free(callee_name);
        ir_mark_unsupported(ir, "direct backend generic specialization name is ambiguous", expr->line, expr->column, callee->name);
        return false;
      }
      if (add_result == Z_SPECIALIZATION_ADD_ADDED) {
        if (ir_function_vec_has_name(functions, specialized_name)) {
          free(specialized_name);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend generic specialization name collides with an existing function", expr->line, expr->column, callee->name);
          return false;
        }
        ir_push_specialized_function(functions, callee, type_args);
      }
      free(specialized_name);
    }
    free(callee_name);
  }
  if (!ir_collect_generic_specializations_from_expr(functions, plan, ir, program, expr->left)) return false;
  if (!ir_collect_generic_specializations_from_expr(functions, plan, ir, program, expr->right)) return false;
  for (size_t i = 0; i < expr->args.len; i++) {
    if (!ir_collect_generic_specializations_from_expr(functions, plan, ir, program, expr->args.items[i])) return false;
  }
  for (size_t i = 0; i < expr->fields.len; i++) {
    if (!ir_collect_generic_specializations_from_expr(functions, plan, ir, program, expr->fields.items[i].value)) return false;
  }
  return true;
}

static void ir_lower_direct_backend_subset(IrProgram *ir, const Program *program, const SourceInput *input) {
  ir->mir_valid = true;
  ir->mir_line = 1;
  ir->mir_column = 1;
  snprintf(ir->mir_expected, sizeof(ir->mir_expected), "direct backend MVP subset");
  snprintf(ir->mir_help, sizeof(ir->mir_help), "restrict this program to exported primitive arithmetic functions or choose another supported direct target");
  ir->mir_bytes = sizeof(IrProgram);
  if (program->choices.len > 0 || program->interfaces.len > 0 || program->aliases.len > 0) {
    ir_mark_unsupported(ir, "direct backend MVP does not support declarations other than functions", 1, 1, "unsupported top-level declaration");
    return;
  }
  FunctionVec direct_functions = {0};
  for (size_t i = 0; i < program->functions.len; i++) {
    if (!program->functions.items[i].is_test && program->functions.items[i].type_params.len == 0) push_function_clone(&direct_functions, &program->functions.items[i]);
  }
  ZSpecializationPlan specialization_plan;
  z_specialization_plan_init(&specialization_plan, IR_SPECIALIZATION_PLAN_LIMIT);
  for (size_t i = 0; i < direct_functions.len; i++) {
    StmtVec body = direct_functions.items[i].body;
    if (!ir_collect_generic_specializations_from_stmt_vec(&direct_functions, &specialization_plan, ir, program, &body)) {
      z_specialization_plan_free(&specialization_plan);
      Program temp_program = {0};
      temp_program.functions = direct_functions;
      z_free_program(&temp_program);
      return;
    }
  }
  IrFunctionOrder *order = z_checked_calloc(direct_functions.len ? direct_functions.len : 1, sizeof(IrFunctionOrder));
  char **stable_ids = z_checked_calloc(direct_functions.len ? direct_functions.len : 1, sizeof(char *));
  for (size_t i = 0; i < direct_functions.len; i++) {
    stable_ids[i] = ir_stable_id_for_source_function(input, &direct_functions.items[i]);
    order[i] = (IrFunctionOrder){.name = direct_functions.items[i].name, .stable_id = stable_ids[i], .source_index = i};
  }
  qsort(order, direct_functions.len, sizeof(IrFunctionOrder), ir_function_order_compare);
  for (size_t i = 0; i < direct_functions.len; i++) {
    ir_program_push_function(ir, &direct_functions.items[order[i].source_index], order[i].stable_id);
  }
  bool has_export = false;
  for (size_t i = 0; i < ir->function_len; i++) {
    if (ir->functions[i].is_exported) {
      has_export = true;
      ir->direct_export_count++;
    }
    if (!ir_lower_function_body(program, ir, &ir->functions[i], &direct_functions.items[order[i].source_index])) {
      free(order);
      for (size_t stable_index = 0; stable_index < direct_functions.len; stable_index++) free(stable_ids[stable_index]);
      free(stable_ids);
      z_specialization_plan_free(&specialization_plan);
      Program temp_program = {0};
      temp_program.functions = direct_functions;
      z_free_program(&temp_program);
      return;
    }
  }
  if (!z_mir_verify_direct_contracts(ir)) {
    free(order);
    for (size_t stable_index = 0; stable_index < direct_functions.len; stable_index++) free(stable_ids[stable_index]);
    free(stable_ids);
    z_specialization_plan_free(&specialization_plan);
    Program temp_program = {0};
    temp_program.functions = direct_functions;
    z_free_program(&temp_program);
    return;
  }
  free(order);
  for (size_t stable_index = 0; stable_index < direct_functions.len; stable_index++) free(stable_ids[stable_index]);
  free(stable_ids);
  z_specialization_plan_free(&specialization_plan);
  Program temp_program = {0};
  temp_program.functions = direct_functions;
  z_free_program(&temp_program);
  ir->direct_function_count = ir->function_len;
  if (!has_export) {
    ir_mark_unsupported(ir, "direct backend requires at least one exported C ABI entry function", 1, 1, "no exported function");
    return;
  }
}

static void push_function_clone(FunctionVec *vec, const Function *source) {
  vec->items = ir_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Function));
  vec->items[vec->len++] = (Function){
    .name = z_strdup(source->name),
    .test_name = source->test_name ? z_strdup(source->test_name) : NULL,
    .return_type = z_strdup(source->return_type),
    .type_params = clone_params(&source->type_params),
    .params = clone_params(&source->params),
    .is_public = source->is_public,
    .raises = source->raises,
    .has_error_set = source->has_error_set,
    .errors = clone_params(&source->errors),
    .is_test = source->is_test,
    .export_c = source->export_c,
    .body = clone_stmts(&source->body),
    .line = source->line,
    .column = source->column
  };
}

IrProgram z_lower_program_with_source(const Program *program, const SourceInput *input, const ZTargetInfo *target) {
  IrProgram ir = {0};
  ir.target = target;
  for (size_t i = 0; i < program->c_imports.len; i++) {
    CImport *source = &program->c_imports.items[i];
    ir.program.c_imports.items = ir_grow_items(ir.program.c_imports.items, ir.program.c_imports.len, &ir.program.c_imports.cap, 4, sizeof(CImport));
    ir.program.c_imports.items[ir.program.c_imports.len++] = (CImport){
      .header = source->header ? z_strdup(source->header) : NULL,
      .resolved_header = source->resolved_header ? z_strdup(source->resolved_header) : NULL,
      .alias = source->alias ? z_strdup(source->alias) : NULL,
      .line = source->line,
      .column = source->column
    };
  }
  for (size_t i = 0; i < program->consts.len; i++) {
    ConstDecl *source = &program->consts.items[i];
    ir.program.consts.items = ir_grow_items(ir.program.consts.items, ir.program.consts.len, &ir.program.consts.cap, 4, sizeof(ConstDecl));
    ir.program.consts.items[ir.program.consts.len++] = (ConstDecl){
      .name = z_strdup(source->name),
      .type = source->type ? z_strdup(source->type) : NULL,
      .expr = clone_expr(source->expr),
      .is_public = source->is_public,
      .line = source->line,
      .column = source->column
    };
  }
  for (size_t i = 0; i < program->aliases.len; i++) {
    TypeAlias *source = &program->aliases.items[i];
    ir.program.aliases.items = ir_grow_items(ir.program.aliases.items, ir.program.aliases.len, &ir.program.aliases.cap, 4, sizeof(TypeAlias));
    ir.program.aliases.items[ir.program.aliases.len++] = (TypeAlias){
      .name = z_strdup(source->name),
      .target = source->target ? z_strdup(source->target) : NULL,
      .is_public = source->is_public,
      .line = source->line,
      .column = source->column
    };
  }
  for (size_t i = 0; i < program->interfaces.len; i++) {
    InterfaceDecl *source = &program->interfaces.items[i];
    ir.program.interfaces.items = ir_grow_items(ir.program.interfaces.items, ir.program.interfaces.len, &ir.program.interfaces.cap, 4, sizeof(InterfaceDecl));
    InterfaceDecl cloned = {
      .name = z_strdup(source->name),
      .type_params = clone_params(&source->type_params),
      .is_public = source->is_public,
      .line = source->line,
      .column = source->column
    };
    for (size_t method_index = 0; method_index < source->methods.len; method_index++) {
      push_function_clone(&cloned.methods, &source->methods.items[method_index]);
    }
    ir.program.interfaces.items[ir.program.interfaces.len++] = cloned;
  }
  for (size_t i = 0; i < program->shapes.len; i++) {
    Shape *source = &program->shapes.items[i];
    ir.program.shapes.items = ir_grow_items(ir.program.shapes.items, ir.program.shapes.len, &ir.program.shapes.cap, 4, sizeof(Shape));
    Shape cloned = {
      .name = z_strdup(source->name),
      .layout = z_strdup(source->layout),
      .type_params = clone_params(&source->type_params),
      .fields = clone_params(&source->fields),
      .is_public = source->is_public,
      .line = source->line,
      .column = source->column
    };
    for (size_t method_index = 0; method_index < source->methods.len; method_index++) {
      push_function_clone(&cloned.methods, &source->methods.items[method_index]);
    }
    ir.program.shapes.items[ir.program.shapes.len++] = cloned;
  }
  for (size_t i = 0; i < program->enums.len; i++) {
    EnumDecl *source = &program->enums.items[i];
    ir.program.enums.items = ir_grow_items(ir.program.enums.items, ir.program.enums.len, &ir.program.enums.cap, 4, sizeof(EnumDecl));
    ir.program.enums.items[ir.program.enums.len++] = (EnumDecl){
      .name = z_strdup(source->name),
      .type = source->type ? z_strdup(source->type) : NULL,
      .cases = clone_params(&source->cases),
      .is_public = source->is_public,
      .line = source->line,
      .column = source->column
    };
  }
  for (size_t i = 0; i < program->choices.len; i++) {
    Choice *source = &program->choices.items[i];
    ir.program.choices.items = ir_grow_items(ir.program.choices.items, ir.program.choices.len, &ir.program.choices.cap, 4, sizeof(Choice));
    ir.program.choices.items[ir.program.choices.len++] = (Choice){
      .name = z_strdup(source->name),
      .cases = clone_params(&source->cases),
      .is_public = source->is_public,
      .line = source->line,
      .column = source->column
    };
  }
  for (size_t i = 0; i < program->functions.len; i++) {
    Function *source = &program->functions.items[i];
    push_function_clone(&ir.program.functions, source);
  }
  ir_lower_direct_backend_subset(&ir, program, input);
  return ir;
}

IrProgram z_lower_program(const Program *program) {
  return z_lower_program_with_source(program, NULL, NULL);
}

void z_free_ir_program(IrProgram *program) {
  if (!program) return;
  bool borrowed_binary_storage = program->mir_binary_storage_borrowed;
  for (size_t i = 0; i < program->function_len; i++) {
    IrFunction *fun = &program->functions[i];
    if (!borrowed_binary_storage) {
      free(fun->name);
      free(fun->stable_id);
      free(fun->world_param_name);
    }
    for (size_t binding_index = 0; binding_index < fun->generic_binding_len; binding_index++) {
      free(fun->generic_param_names[binding_index]);
      free(fun->generic_arg_types[binding_index]);
    }
    free(fun->generic_param_names);
    free(fun->generic_arg_types);
    for (size_t local_index = 0; local_index < fun->local_len; local_index++) {
      if (!borrowed_binary_storage) {
        free(fun->locals[local_index].name);
        free(fun->locals[local_index].shape_name);
      }
    }
    ir_free_instrs(fun->instrs, fun->instr_len);
    free(fun->locals);
    free(fun->instrs);
  }
  free(program->functions);
  for (size_t i = 0; i < program->external_function_len; i++) {
    if (!borrowed_binary_storage) {
      free(program->external_functions[i].symbol);
      free(program->external_functions[i].import_header);
      free(program->external_functions[i].import_resolved_header);
    }
    free(program->external_functions[i].param_types);
  }
  free(program->external_functions);
  free(program->mir_path); free(program->package_root);
  ir_active_local_restore(program, 0);
  free(program->active_local_names);
  for (size_t i = 0; i < program->data_segment_len; i++) {
    if (!borrowed_binary_storage) free(program->data_segments[i].bytes);
  }
  free(program->data_segments);
  z_free_program(&program->program);
#if !defined(_WIN32)
  if (program->mir_binary_storage && program->mir_binary_storage_mapped) {
    munmap((void *)program->mir_binary_storage, program->mir_binary_storage_len);
    if (program->mir_binary_storage_fd >= 0) close(program->mir_binary_storage_fd);
  }
#else
  if (program->mir_binary_storage && !program->mir_binary_storage_mapped) free((void *)program->mir_binary_storage);
#endif
}
