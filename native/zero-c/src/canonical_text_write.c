#include "canonical_text.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  ZBuf *buf;
} CanonWriter;

static bool cw_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool cw_starts_with(const char *text, const char *prefix) {
  return text && prefix && strncmp(text, prefix, strlen(prefix)) == 0;
}

static void cw_indent(CanonWriter *writer, unsigned indent) {
  for (unsigned i = 0; i < indent; i++) zbuf_append(writer->buf, "    ");
}

static void cw_append_name(CanonWriter *writer, const char *name) {
  zbuf_append(writer->buf, name && name[0] ? name : "__unnamed");
}

static void cw_append_escaped_string(CanonWriter *writer, const char *text) {
  zbuf_append_char(writer->buf, '"');
  for (const char *p = text ? text : ""; *p; p++) {
    unsigned char ch = (unsigned char)*p;
    switch (ch) {
      case '\\': zbuf_append(writer->buf, "\\\\"); break;
      case '"': zbuf_append(writer->buf, "\\\""); break;
      case '\n': zbuf_append(writer->buf, "\\n"); break;
      case '\r': zbuf_append(writer->buf, "\\r"); break;
      case '\t': zbuf_append(writer->buf, "\\t"); break;
      default:
        if (ch < 0x20) zbuf_appendf(writer->buf, "\\x%02x", ch);
        else zbuf_append_char(writer->buf, (char)ch);
        break;
    }
  }
  zbuf_append_char(writer->buf, '"');
}

static void cw_append_char_literal(CanonWriter *writer, const char *text) {
  unsigned value = (unsigned)strtoul(text ? text : "0", NULL, 10);
  zbuf_append_char(writer->buf, '\'');
  if (value == '\n') zbuf_append(writer->buf, "\\n");
  else if (value == '\r') zbuf_append(writer->buf, "\\r");
  else if (value == '\t') zbuf_append(writer->buf, "\\t");
  else if (value == '\'') zbuf_append(writer->buf, "\\'");
  else if (value == '\\') zbuf_append(writer->buf, "\\\\");
  else if (value >= 32 && value < 127) zbuf_append_char(writer->buf, (char)value);
  else zbuf_appendf(writer->buf, "\\x%02x", value & 0xff);
  zbuf_append_char(writer->buf, '\'');
}

static void cw_append_type_args(CanonWriter *writer, const TypeArgVec *args) {
  if (!args || args->len == 0) return;
  zbuf_append_char(writer->buf, '<');
  for (size_t i = 0; i < args->len; i++) {
    if (i > 0) zbuf_append(writer->buf, ", ");
    zbuf_append(writer->buf, args->items[i].type ? args->items[i].type : "");
  }
  zbuf_append_char(writer->buf, '>');
}

static int cw_binary_precedence(const char *op) {
  if (cw_text_eq(op, "||")) return 1;
  if (cw_text_eq(op, "&&")) return 2;
  if (cw_text_eq(op, "==") || cw_text_eq(op, "!=")) return 3;
  if (cw_text_eq(op, "<") || cw_text_eq(op, ">") || cw_text_eq(op, "<=") || cw_text_eq(op, ">=")) return 4;
  if (cw_text_eq(op, "+") || cw_text_eq(op, "-") || cw_text_eq(op, "+%") || cw_text_eq(op, "+|")) return 5;
  if (cw_text_eq(op, "*") || cw_text_eq(op, "/") || cw_text_eq(op, "%")) return 6;
  return 0;
}

static int cw_expr_precedence(const Expr *expr) {
  if (!expr) return 10;
  if (expr->kind == EXPR_BINARY) return cw_binary_precedence(expr->text);
  if (expr->kind == EXPR_CAST) return 7;
  if (expr->kind == EXPR_BORROW || expr->kind == EXPR_CHECK || expr->kind == EXPR_RESCUE || expr->kind == EXPR_META) return 8;
  return 10;
}

static bool cw_number_is_zero(const Expr *expr) {
  return expr && expr->kind == EXPR_NUMBER && expr->text && strcmp(expr->text, "0") == 0;
}

static bool cw_bool_is_false(const Expr *expr) {
  return expr && expr->kind == EXPR_BOOL && !expr->bool_value;
}

static bool cw_call_is_deref(const Expr *expr) {
  return expr && expr->kind == EXPR_CALL && expr->left && expr->left->kind == EXPR_IDENT &&
         cw_text_eq(expr->left->text, "deref") && expr->args.len == 1;
}

static void cw_append_expr_prec(CanonWriter *writer, const Expr *expr, int parent_prec, bool right_assoc);

static void cw_append_expr_grouped(CanonWriter *writer, const Expr *expr) {
  bool grouped = cw_expr_precedence(expr) < 10;
  if (grouped) zbuf_append_char(writer->buf, '(');
  cw_append_expr_prec(writer, expr, 0, false);
  if (grouped) zbuf_append_char(writer->buf, ')');
}

static void cw_append_call_args(CanonWriter *writer, const ExprVec *args) {
  zbuf_append_char(writer->buf, '(');
  for (size_t i = 0; args && i < args->len; i++) {
    if (i > 0) zbuf_append(writer->buf, ", ");
    cw_append_expr_prec(writer, args->items[i], 0, false);
  }
  zbuf_append_char(writer->buf, ')');
}

static void cw_append_shape_literal(CanonWriter *writer, const Expr *expr) {
  cw_append_name(writer, expr && expr->text ? expr->text : "Unknown");
  if (!expr || expr->fields.len == 0) {
    zbuf_append(writer->buf, " {}");
    return;
  }
  zbuf_append(writer->buf, " { ");
  for (size_t i = 0; i < expr->fields.len; i++) {
    if (i > 0) zbuf_append(writer->buf, ", ");
    cw_append_name(writer, expr->fields.items[i].name);
    zbuf_append(writer->buf, ": ");
    cw_append_expr_prec(writer, expr->fields.items[i].value, 0, false);
  }
  zbuf_append(writer->buf, " }");
}

static void cw_append_array_literal(CanonWriter *writer, const Expr *expr) {
  zbuf_append_char(writer->buf, '[');
  if (expr && expr->array_repeat) {
    cw_append_expr_prec(writer, expr->args.len > 0 ? expr->args.items[0] : NULL, 0, false);
    zbuf_append(writer->buf, "; ");
    cw_append_expr_prec(writer, expr->args.len > 1 ? expr->args.items[1] : NULL, 0, false);
  } else {
    for (size_t i = 0; expr && i < expr->args.len; i++) {
      if (i > 0) zbuf_append(writer->buf, ", ");
      cw_append_expr_prec(writer, expr->args.items[i], 0, false);
    }
  }
  zbuf_append_char(writer->buf, ']');
}

static void cw_append_expr_prec(CanonWriter *writer, const Expr *expr, int parent_prec, bool right_assoc) {
  if (!expr) {
    zbuf_append(writer->buf, "__missing_expr__");
    return;
  }

  if (expr->kind == EXPR_BINARY && (cw_text_eq(expr->text, "-") || cw_text_eq(expr->text, "+")) && cw_number_is_zero(expr->left)) {
    zbuf_append(writer->buf, expr->text);
    cw_append_expr_prec(writer, expr->right, 8, false);
    return;
  }
  if (expr->kind == EXPR_BINARY && cw_text_eq(expr->text, "==") && cw_bool_is_false(expr->right)) {
    zbuf_append_char(writer->buf, '!');
    cw_append_expr_prec(writer, expr->left, 8, false);
    return;
  }
  if (cw_call_is_deref(expr)) {
    zbuf_append_char(writer->buf, '*');
    cw_append_expr_prec(writer, expr->args.items[0], 8, false);
    return;
  }

  int prec = cw_expr_precedence(expr);
  bool grouped = prec > 0 && (prec < parent_prec || (right_assoc && prec == parent_prec));
  if (grouped) zbuf_append_char(writer->buf, '(');
  switch (expr->kind) {
    case EXPR_IDENT:
      cw_append_name(writer, expr->text);
      cw_append_type_args(writer, &expr->type_args);
      break;
    case EXPR_STRING:
      cw_append_escaped_string(writer, expr->text);
      break;
    case EXPR_CHAR:
      cw_append_char_literal(writer, expr->text);
      break;
    case EXPR_NUMBER:
      zbuf_append(writer->buf, expr->text ? expr->text : "0");
      break;
    case EXPR_BOOL:
      zbuf_append(writer->buf, expr->bool_value ? "true" : "false");
      break;
    case EXPR_NULL:
      zbuf_append(writer->buf, "null");
      break;
    case EXPR_MEMBER:
      cw_append_expr_prec(writer, expr->left, 10, false);
      zbuf_append_char(writer->buf, '.');
      cw_append_name(writer, expr->text);
      cw_append_type_args(writer, &expr->type_args);
      break;
    case EXPR_INDEX:
      cw_append_expr_prec(writer, expr->left, 10, false);
      zbuf_append_char(writer->buf, '[');
      cw_append_expr_prec(writer, expr->right, 0, false);
      zbuf_append_char(writer->buf, ']');
      break;
    case EXPR_SLICE:
      cw_append_expr_prec(writer, expr->left, 10, false);
      zbuf_append_char(writer->buf, '[');
      if (expr->args.len > 0 && expr->args.items[0]) cw_append_expr_prec(writer, expr->args.items[0], 0, false);
      zbuf_append(writer->buf, "..");
      if (expr->args.len > 1 && expr->args.items[1]) cw_append_expr_prec(writer, expr->args.items[1], 0, false);
      zbuf_append_char(writer->buf, ']');
      break;
    case EXPR_CALL:
      cw_append_expr_prec(writer, expr->left, 10, false);
      cw_append_type_args(writer, &expr->type_args);
      cw_append_call_args(writer, &expr->args);
      break;
    case EXPR_BINARY:
      cw_append_expr_prec(writer, expr->left, prec, false);
      zbuf_append_char(writer->buf, ' ');
      zbuf_append(writer->buf, expr->text ? expr->text : "__op__");
      zbuf_append_char(writer->buf, ' ');
      cw_append_expr_prec(writer, expr->right, prec, true);
      break;
    case EXPR_CAST:
      cw_append_expr_prec(writer, expr->left, prec, false);
      zbuf_append(writer->buf, " as ");
      zbuf_append(writer->buf, expr->text ? expr->text : "");
      break;
    case EXPR_BORROW:
      zbuf_append(writer->buf, expr->mutable_borrow ? "&mut " : "&");
      cw_append_expr_prec(writer, expr->left, prec, false);
      break;
    case EXPR_CHECK:
      zbuf_append(writer->buf, "check ");
      cw_append_expr_prec(writer, expr->left, prec, false);
      break;
    case EXPR_RESCUE:
      zbuf_append(writer->buf, "rescue ");
      cw_append_expr_grouped(writer, expr->left);
      zbuf_append(writer->buf, " err ");
      cw_append_expr_grouped(writer, expr->right);
      break;
    case EXPR_META:
      zbuf_append(writer->buf, "meta ");
      cw_append_expr_prec(writer, expr->left, prec, false);
      break;
    case EXPR_SHAPE_LITERAL:
      cw_append_shape_literal(writer, expr);
      break;
    case EXPR_ARRAY_LITERAL:
      cw_append_array_literal(writer, expr);
      break;
  }
  if (grouped) zbuf_append_char(writer->buf, ')');
}

static void cw_append_param(CanonWriter *writer, const Param *param, bool include_default) {
  if (param && param->is_static) zbuf_append(writer->buf, "static ");
  cw_append_name(writer, param ? param->name : NULL);
  if (param && param->type && param->type[0]) {
    zbuf_append(writer->buf, ": ");
    zbuf_append(writer->buf, param->type);
  }
  if (include_default && param && param->default_value) {
    zbuf_append(writer->buf, " = ");
    cw_append_expr_prec(writer, param->default_value, 0, false);
  }
}

static void cw_append_type_param_list(CanonWriter *writer, const ParamVec *params) {
  if (!params || params->len == 0) return;
  zbuf_append_char(writer->buf, '<');
  for (size_t i = 0; i < params->len; i++) {
    if (i > 0) zbuf_append(writer->buf, ", ");
    cw_append_param(writer, &params->items[i], false);
  }
  zbuf_append_char(writer->buf, '>');
}

static void cw_append_stmt_vec(CanonWriter *writer, const StmtVec *body, unsigned indent);

static void cw_append_block(CanonWriter *writer, const StmtVec *body, unsigned indent) {
  zbuf_append(writer->buf, " {\n");
  cw_append_stmt_vec(writer, body, indent + 1);
  cw_indent(writer, indent);
  zbuf_append(writer->buf, "}\n");
}

static void cw_append_match_arm(CanonWriter *writer, const MatchArm *arm, unsigned indent) {
  cw_indent(writer, indent);
  if (arm && arm->payload_name) {
    zbuf_append_char(writer->buf, '.');
    cw_append_name(writer, arm->case_name);
    zbuf_append_char(writer->buf, '(');
    cw_append_name(writer, arm->payload_name);
    zbuf_append_char(writer->buf, ')');
  } else {
    cw_append_name(writer, arm ? arm->case_name : NULL);
  }
  if (arm && arm->range_end) {
    zbuf_append(writer->buf, "..");
    zbuf_append(writer->buf, arm->range_end);
  }
  if (arm && arm->guard) {
    zbuf_append(writer->buf, " if ");
    cw_append_expr_prec(writer, arm->guard, 0, false);
  }
  cw_append_block(writer, arm ? &arm->body : NULL, indent);
}

static void cw_append_stmt(CanonWriter *writer, const Stmt *stmt, unsigned indent) {
  cw_indent(writer, indent);
  if (!stmt) {
    zbuf_append(writer->buf, "__missing_stmt__\n");
    return;
  }
  switch (stmt->kind) {
    case STMT_LET:
      zbuf_append(writer->buf, stmt->mutable_binding ? "var " : "let ");
      cw_append_name(writer, stmt->name);
      zbuf_append(writer->buf, ": ");
      zbuf_append(writer->buf, stmt->type ? stmt->type : "Unknown");
      zbuf_append(writer->buf, " = ");
      cw_append_expr_prec(writer, stmt->expr, 0, false);
      zbuf_append_char(writer->buf, '\n');
      break;
    case STMT_ASSIGN:
      cw_append_expr_prec(writer, stmt->target, 0, false);
      zbuf_append(writer->buf, " = ");
      cw_append_expr_prec(writer, stmt->expr, 0, false);
      zbuf_append_char(writer->buf, '\n');
      break;
    case STMT_DEFER:
      zbuf_append(writer->buf, "defer ");
      cw_append_expr_prec(writer, stmt->expr, 0, false);
      zbuf_append_char(writer->buf, '\n');
      break;
    case STMT_CHECK:
      zbuf_append(writer->buf, "check ");
      cw_append_expr_prec(writer, stmt->expr, 0, false);
      zbuf_append_char(writer->buf, '\n');
      break;
    case STMT_RETURN:
      zbuf_append(writer->buf, "return");
      if (stmt->expr) {
        zbuf_append_char(writer->buf, ' ');
        cw_append_expr_prec(writer, stmt->expr, 0, false);
      }
      zbuf_append_char(writer->buf, '\n');
      break;
    case STMT_EXPR:
      cw_append_expr_prec(writer, stmt->expr, 0, false);
      zbuf_append_char(writer->buf, '\n');
      break;
    case STMT_IF:
      zbuf_append(writer->buf, "if ");
      cw_append_expr_prec(writer, stmt->expr, 0, false);
      cw_append_block(writer, &stmt->then_body, indent);
      if (stmt->else_body.len > 0) {
        if (stmt->else_body.len == 1 && stmt->else_body.items[0] && stmt->else_body.items[0]->kind == STMT_IF) {
          cw_indent(writer, indent);
          zbuf_append(writer->buf, "else ");
          cw_append_stmt(writer, stmt->else_body.items[0], 0);
        } else {
          cw_indent(writer, indent);
          zbuf_append(writer->buf, "else");
          cw_append_block(writer, &stmt->else_body, indent);
        }
      }
      break;
    case STMT_WHILE:
      zbuf_append(writer->buf, "while ");
      cw_append_expr_prec(writer, stmt->expr, 0, false);
      cw_append_block(writer, &stmt->then_body, indent);
      break;
    case STMT_FOR:
      zbuf_append(writer->buf, "for ");
      cw_append_name(writer, stmt->name);
      zbuf_append(writer->buf, " in ");
      cw_append_expr_prec(writer, stmt->expr, 0, false);
      zbuf_append(writer->buf, "..");
      cw_append_expr_prec(writer, stmt->range_end, 0, false);
      cw_append_block(writer, &stmt->then_body, indent);
      break;
    case STMT_BREAK:
      zbuf_append(writer->buf, "break\n");
      break;
    case STMT_CONTINUE:
      zbuf_append(writer->buf, "continue\n");
      break;
    case STMT_MATCH:
      zbuf_append(writer->buf, "match ");
      cw_append_expr_prec(writer, stmt->expr, 0, false);
      zbuf_append(writer->buf, " {\n");
      for (size_t i = 0; i < stmt->match_arms.len; i++) cw_append_match_arm(writer, &stmt->match_arms.items[i], indent + 1);
      cw_indent(writer, indent);
      zbuf_append(writer->buf, "}\n");
      break;
    case STMT_RAISE:
      zbuf_append(writer->buf, "raise ");
      cw_append_name(writer, stmt->name);
      zbuf_append_char(writer->buf, '\n');
      break;
  }
}

static void cw_append_stmt_vec(CanonWriter *writer, const StmtVec *body, unsigned indent) {
  for (size_t i = 0; body && i < body->len; i++) cw_append_stmt(writer, body->items[i], indent);
}

static void cw_append_function_signature(CanonWriter *writer, const Function *fun) {
  if (fun->is_public) zbuf_append(writer->buf, "pub ");
  if (fun->export_c) zbuf_append(writer->buf, "export c ");
  zbuf_append(writer->buf, "fn ");
  cw_append_name(writer, fun->name);
  cw_append_type_param_list(writer, &fun->type_params);
  zbuf_append_char(writer->buf, '(');
  for (size_t i = 0; i < fun->params.len; i++) {
    if (i > 0) zbuf_append(writer->buf, ", ");
    cw_append_param(writer, &fun->params.items[i], false);
  }
  zbuf_append(writer->buf, ") -> ");
  zbuf_append(writer->buf, fun->return_type && fun->return_type[0] ? fun->return_type : "Void");
  if (fun->raises) {
    zbuf_append(writer->buf, " raises");
    if (fun->errors.len > 0) {
      zbuf_append(writer->buf, " [");
      for (size_t i = 0; i < fun->errors.len; i++) {
        if (i > 0) zbuf_append(writer->buf, ", ");
        cw_append_name(writer, fun->errors.items[i].name);
      }
      zbuf_append_char(writer->buf, ']');
    }
  }
}

static void cw_append_function(CanonWriter *writer, const Function *fun, unsigned indent, bool interface_method) {
  cw_indent(writer, indent);
  if (fun->is_test) {
    zbuf_append(writer->buf, "test ");
    cw_append_escaped_string(writer, fun->test_name ? fun->test_name : "");
    cw_append_block(writer, &fun->body, indent);
    return;
  }
  cw_append_function_signature(writer, fun);
  if (interface_method) zbuf_append_char(writer->buf, '\n');
  else cw_append_block(writer, &fun->body, indent);
}

static void cw_append_blank_between(CanonWriter *writer, bool *wrote_any) {
  if (*wrote_any) zbuf_append_char(writer->buf, '\n');
  *wrote_any = true;
}

static void cw_append_use(CanonWriter *writer, const UseImport *item) {
  zbuf_append(writer->buf, "use ");
  zbuf_append(writer->buf, item->module ? item->module : "");
  if (item->alias && item->alias[0]) {
    zbuf_append(writer->buf, " as ");
    cw_append_name(writer, item->alias);
  }
  zbuf_append_char(writer->buf, '\n');
}

static void cw_append_c_import(CanonWriter *writer, const CImport *item) {
  zbuf_append(writer->buf, "extern c ");
  cw_append_escaped_string(writer, item->header);
  zbuf_append(writer->buf, " as ");
  cw_append_name(writer, item->alias);
  zbuf_append_char(writer->buf, '\n');
}

static void cw_append_const(CanonWriter *writer, const ConstDecl *item) {
  if (item->is_public) zbuf_append(writer->buf, "pub ");
  zbuf_append(writer->buf, "const ");
  cw_append_name(writer, item->name);
  zbuf_append(writer->buf, ": ");
  zbuf_append(writer->buf, item->type ? item->type : "Unknown");
  zbuf_append(writer->buf, " = ");
  cw_append_expr_prec(writer, item->expr, 0, false);
  zbuf_append_char(writer->buf, '\n');
}

static void cw_append_alias(CanonWriter *writer, const TypeAlias *item) {
  if (item->is_public) zbuf_append(writer->buf, "pub ");
  zbuf_append(writer->buf, "alias ");
  cw_append_name(writer, item->name);
  zbuf_append(writer->buf, " = ");
  zbuf_append(writer->buf, item->target ? item->target : "Unknown");
  zbuf_append_char(writer->buf, '\n');
}

static void cw_append_shape(CanonWriter *writer, const Shape *shape) {
  if (shape->is_public) zbuf_append(writer->buf, "pub ");
  if (cw_text_eq(shape->layout, "extern")) zbuf_append(writer->buf, "extern type ");
  else if (cw_text_eq(shape->layout, "packed")) zbuf_append(writer->buf, "packed type ");
  else zbuf_append(writer->buf, "type ");
  cw_append_name(writer, shape->name);
  cw_append_type_param_list(writer, &shape->type_params);
  if (cw_text_eq(shape->layout, "extern")) {
    zbuf_append_char(writer->buf, '\n');
    return;
  }
  zbuf_append(writer->buf, " {\n");
  for (size_t i = 0; i < shape->fields.len; i++) {
    cw_indent(writer, 1);
    cw_append_param(writer, &shape->fields.items[i], true);
    zbuf_append(writer->buf, ",\n");
  }
  for (size_t i = 0; i < shape->methods.len; i++) cw_append_function(writer, &shape->methods.items[i], 1, false);
  zbuf_append(writer->buf, "}\n");
}

static void cw_append_interface(CanonWriter *writer, const InterfaceDecl *item) {
  if (item->is_public) zbuf_append(writer->buf, "pub ");
  zbuf_append(writer->buf, "interface ");
  cw_append_name(writer, item->name);
  cw_append_type_param_list(writer, &item->type_params);
  zbuf_append(writer->buf, " {\n");
  for (size_t i = 0; i < item->methods.len; i++) cw_append_function(writer, &item->methods.items[i], 1, true);
  zbuf_append(writer->buf, "}\n");
}

static void cw_append_enum(CanonWriter *writer, const EnumDecl *item) {
  zbuf_append(writer->buf, "enum ");
  cw_append_name(writer, item->name);
  if (item->type && item->type[0]) {
    zbuf_append(writer->buf, ": ");
    zbuf_append(writer->buf, item->type);
  }
  zbuf_append(writer->buf, " {\n");
  for (size_t i = 0; i < item->cases.len; i++) {
    cw_indent(writer, 1);
    cw_append_name(writer, item->cases.items[i].name);
    zbuf_append(writer->buf, ",\n");
  }
  zbuf_append(writer->buf, "}\n");
}

static void cw_append_choice(CanonWriter *writer, const Choice *item) {
  zbuf_append(writer->buf, "choice ");
  cw_append_name(writer, item->name);
  zbuf_append(writer->buf, " {\n");
  for (size_t i = 0; i < item->cases.len; i++) {
    cw_indent(writer, 1);
    cw_append_param(writer, &item->cases.items[i], false);
    zbuf_append(writer->buf, ",\n");
  }
  zbuf_append(writer->buf, "}\n");
}

static bool cw_format_raw_source(ZBuf *raw, ZBuf *out, ZDiag *diag) {
  ZBuf formatted;
  zbuf_init(&formatted);
  bool ok = z_canonical_text_format_source(raw->data ? raw->data : "", &formatted, diag);
  if (!ok) {
    zbuf_free(&formatted);
    return false;
  }
  *out = formatted;
  return true;
}

bool z_canonical_text_write_program(const Program *program, ZBuf *out, ZDiag *diag) {
  if (!program || !out) return false;
  ZBuf raw;
  zbuf_init(&raw);
  CanonWriter writer = {.buf = &raw};
  bool wrote_any = false;

  for (size_t i = 0; i < program->c_imports.len; i++) { cw_append_blank_between(&writer, &wrote_any); cw_append_c_import(&writer, &program->c_imports.items[i]); }
  for (size_t i = 0; i < program->use_imports.len; i++) { cw_append_blank_between(&writer, &wrote_any); cw_append_use(&writer, &program->use_imports.items[i]); }
  for (size_t i = 0; i < program->consts.len; i++) { cw_append_blank_between(&writer, &wrote_any); cw_append_const(&writer, &program->consts.items[i]); }
  for (size_t i = 0; i < program->aliases.len; i++) { cw_append_blank_between(&writer, &wrote_any); cw_append_alias(&writer, &program->aliases.items[i]); }
  for (size_t i = 0; i < program->shapes.len; i++) { cw_append_blank_between(&writer, &wrote_any); cw_append_shape(&writer, &program->shapes.items[i]); }
  for (size_t i = 0; i < program->interfaces.len; i++) { cw_append_blank_between(&writer, &wrote_any); cw_append_interface(&writer, &program->interfaces.items[i]); }
  for (size_t i = 0; i < program->enums.len; i++) { cw_append_blank_between(&writer, &wrote_any); cw_append_enum(&writer, &program->enums.items[i]); }
  for (size_t i = 0; i < program->choices.len; i++) { cw_append_blank_between(&writer, &wrote_any); cw_append_choice(&writer, &program->choices.items[i]); }
  for (size_t i = 0; i < program->functions.len; i++) {
    if (cw_starts_with(program->functions.items[i].name, "__zero_std_")) continue;
    cw_append_blank_between(&writer, &wrote_any);
    cw_append_function(&writer, &program->functions.items[i], 0, false);
  }

  bool ok = cw_format_raw_source(&raw, out, diag);
  zbuf_free(&raw);
  return ok;
}
