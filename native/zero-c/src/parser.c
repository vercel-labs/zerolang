#include "zero.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  TokenVec *tokens;
  size_t index;
  ZDiag *diag;
} Parser;

static void *parser_grow_items(void *items, size_t len, size_t *cap, size_t initial, size_t item_size) {
  if (len + 1 > *cap) {
    *cap = z_grow_capacity(*cap, len + 1, initial);
    return z_checked_reallocarray(items, *cap, item_size);
  }
  return items;
}

static void push_function(FunctionVec *vec, Function fun) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 8, sizeof(Function));
  vec->items[vec->len++] = fun;
}

static void push_stmt(StmtVec *vec, Stmt *stmt) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 8, sizeof(Stmt *));
  vec->items[vec->len++] = stmt;
}

static void push_expr(ExprVec *vec, Expr *expr) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Expr *));
  vec->items[vec->len++] = expr;
}

static void push_param(ParamVec *vec, Param param) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Param));
  vec->items[vec->len++] = param;
}

static void push_type_arg(TypeArgVec *vec, TypeArg arg) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(TypeArg));
  vec->items[vec->len++] = arg;
}

static void push_shape(ShapeVec *vec, Shape shape) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Shape));
  vec->items[vec->len++] = shape;
}

static void push_interface(InterfaceVec *vec, InterfaceDecl interface) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(InterfaceDecl));
  vec->items[vec->len++] = interface;
}

static void push_method(FunctionVec *vec, Function fun) {
  push_function(vec, fun);
}

static void push_enum(EnumVec *vec, EnumDecl item) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(EnumDecl));
  vec->items[vec->len++] = item;
}

static void push_c_import(CImportVec *vec, CImport item) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(CImport));
  vec->items[vec->len++] = item;
}

static void push_use_import(UseImportVec *vec, UseImport item) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(UseImport));
  vec->items[vec->len++] = item;
}

static void push_choice(ChoiceVec *vec, Choice item) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(Choice));
  vec->items[vec->len++] = item;
}

static void push_const(ConstVec *vec, ConstDecl item) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(ConstDecl));
  vec->items[vec->len++] = item;
}

static void push_alias(TypeAliasVec *vec, TypeAlias item) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(TypeAlias));
  vec->items[vec->len++] = item;
}

static void push_field_init(FieldInitVec *vec, FieldInit field) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(FieldInit));
  vec->items[vec->len++] = field;
}

static void push_match_arm(MatchArmVec *vec, MatchArm arm) {
  vec->items = parser_grow_items(vec->items, vec->len, &vec->cap, 4, sizeof(MatchArm));
  vec->items[vec->len++] = arm;
}

static Token *current(Parser *parser) {
  return &parser->tokens->items[parser->index];
}

static Token *previous(Parser *parser) {
  return &parser->tokens->items[parser->index - 1];
}

static bool check(Parser *parser, const char *text) {
  return strcmp(current(parser)->text, text) == 0;
}

static bool match(Parser *parser, const char *text) {
  if (!check(parser, text)) return false;
  parser->index++;
  return true;
}

static bool match_kind(Parser *parser, TokenKind kind) {
  if (current(parser)->kind != kind) return false;
  parser->index++;
  return true;
}

static bool fail(Parser *parser, const char *message) {
  parser->diag->code = 100;
  parser->diag->line = current(parser)->line;
  parser->diag->column = current(parser)->column;
  snprintf(parser->diag->message, sizeof(parser->diag->message), "%s", message);
  return false;
}

static bool fail_at(Parser *parser, Token *token, const char *message) {
  parser->diag->code = 100;
  parser->diag->line = token ? token->line : current(parser)->line;
  parser->diag->column = token ? token->column : current(parser)->column;
  snprintf(parser->diag->message, sizeof(parser->diag->message), "%s", message);
  return false;
}

static bool token_on_line(Token *token, int line) {
  return token && token->kind != TOK_EOF && token->line == line;
}

static int token_end_column(Token *token) {
  if (!token) return 1;
  return token->column + (int)token->length;
}

static Token *expect(Parser *parser, const char *text, const char *message) {
  if (match(parser, text)) return previous(parser);
  fail(parser, message);
  return NULL;
}

static Token *expect_ident(Parser *parser, const char *message) {
  if (match_kind(parser, TOK_IDENT)) return previous(parser);
  fail(parser, message);
  return NULL;
}

static Expr *parse_expr(Parser *parser);
static StmtVec parse_block(Parser *parser);
static Function parse_function(Parser *parser);
static void free_expr(Expr *expr);

static char *parse_type(Parser *parser) {
  if (match(parser, "[")) {
    Token *length = current(parser);
    if (!match_kind(parser, TOK_NUMBER) && !match_kind(parser, TOK_IDENT)) {
      fail(parser, "expected array length");
      return z_strdup("[0]Void");
    }
    expect(parser, "]", "expected ']' after array length");
    char *element = parse_type(parser);
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append_char(&buf, '[');
    zbuf_append(&buf, length->text);
    zbuf_append_char(&buf, ']');
    zbuf_append(&buf, element);
    free(element);
    return buf.data;
  }
  bool is_const = match(parser, "const");
  Token *name = expect_ident(parser, "expected type name");
  if (!name) return z_strdup("Void");
  ZBuf buf;
  zbuf_init(&buf);
  if (is_const) zbuf_append(&buf, "const ");
  zbuf_append(&buf, name->text);
  if (match(parser, "<")) {
    int depth = 1;
    zbuf_append_char(&buf, '<');
    while (current(parser)->kind != TOK_EOF && depth > 0) {
      if (match(parser, "<")) {
        depth++;
        zbuf_append_char(&buf, '<');
      } else if (match(parser, ">")) {
        depth--;
        zbuf_append_char(&buf, '>');
      } else {
        zbuf_append(&buf, current(parser)->text);
        parser->index++;
      }
    }
  }
  return buf.data;
}

static ParamVec parse_params(Parser *parser) {
  ParamVec params = {0};
  if (!expect(parser, "(", "expected '(' before parameters")) return params;
  if (match(parser, ")")) return params;
  do {
    bool is_static = match(parser, "static");
    Token *name = expect_ident(parser, "expected parameter name");
    if (!name) return params;
    if (!expect(parser, ":", "expected ':' after parameter name")) return params;
    Param param = {
      .name = z_strdup(name->text),
      .type = parse_type(parser),
      .is_static = is_static,
      .line = name->line,
      .column = name->column
    };
    push_param(&params, param);
  } while (match(parser, ","));
  expect(parser, ")", "expected ')' after parameters");
  return params;
}

static ParamVec parse_type_params(Parser *parser) {
  ParamVec params = {0};
  if (!match(parser, "<")) return params;
  if (match(parser, ">")) return params;
  do {
    bool is_static = match(parser, "static");
    Token *name = expect_ident(parser, "expected generic parameter name");
    if (!name) return params;
    char *type = NULL;
    if (match(parser, ":")) {
      type = parse_type(parser);
    } else {
      type = z_strdup("Type");
    }
    Param param = {
      .name = z_strdup(name->text),
      .type = type,
      .is_static = is_static,
      .line = name->line,
      .column = name->column
    };
    push_param(&params, param);
  } while (match(parser, ","));
  expect(parser, ">", "expected '>' after generic parameters");
  return params;
}

static bool looks_like_type_args_before_call(Parser *parser) {
  if (!check(parser, "<")) return false;
  int depth = 0;
  for (size_t i = parser->index; i < parser->tokens->len; i++) {
    const char *text = parser->tokens->items[i].text;
    if (strcmp(text, "<") == 0) depth++;
    else if (strcmp(text, ">") == 0) {
      depth--;
      if (depth == 0) {
        return i + 1 < parser->tokens->len && strcmp(parser->tokens->items[i + 1].text, "(") == 0;
      }
      if (depth < 0) return false;
    } else if (strcmp(text, "{") == 0 || strcmp(text, "}") == 0 || strcmp(text, "=") == 0) {
      return false;
    }
  }
  return false;
}

static char *parse_generic_arg_text(Parser *parser) {
  ZBuf buf;
  zbuf_init(&buf);
  int depth = 0;
  while (current(parser)->kind != TOK_EOF) {
    if (depth == 0 && (check(parser, ",") || check(parser, ">"))) break;
    if (match(parser, "<")) {
      depth++;
      zbuf_append_char(&buf, '<');
      continue;
    }
    if (check(parser, ">")) {
      if (depth == 0) break;
      parser->index++;
      depth--;
      zbuf_append_char(&buf, '>');
      continue;
    }
    zbuf_append(&buf, current(parser)->text);
    parser->index++;
  }
  return buf.data ? buf.data : z_strdup("Void");
}

static TypeArgVec parse_type_args(Parser *parser) {
  TypeArgVec args = {0};
  if (!expect(parser, "<", "expected '<' before type arguments")) return args;
  if (match(parser, ">")) return args;
  do {
    Token *start = current(parser);
    TypeArg arg = {
      .type = parse_generic_arg_text(parser),
      .line = start->line,
      .column = start->column
    };
    push_type_arg(&args, arg);
  } while (match(parser, ","));
  expect(parser, ">", "expected '>' after type arguments");
  return args;
}

static ParamVec parse_error_set(Parser *parser) {
  ParamVec errors = {0};
  if (!expect(parser, "{", "expected '{' before error set")) return errors;
  if (match(parser, "}")) return errors;
  do {
    Token *name = expect_ident(parser, "expected error name");
    if (!name) return errors;
    Param error = {
      .name = z_strdup(name->text),
      .type = NULL,
      .line = name->line,
      .column = name->column
    };
    push_param(&errors, error);
  } while (match(parser, ","));
  expect(parser, "}", "expected '}' after error set");
  return errors;
}

static int precedence(const char *op) {
  if (strcmp(op, "||") == 0) return 1;
  if (strcmp(op, "&&") == 0) return 2;
  if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 || strcmp(op, "<") == 0 ||
      strcmp(op, "<=") == 0 || strcmp(op, ">") == 0 || strcmp(op, ">=") == 0) return 3;
  if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 || strcmp(op, "+%") == 0 || strcmp(op, "+|") == 0) return 4;
  if (strcmp(op, "*") == 0 || strcmp(op, "/") == 0 || strcmp(op, "%") == 0) return 5;
  return -1;
}

static Expr *new_expr(ExprKind kind, Token *token) {
  Expr *expr = z_checked_calloc(1, sizeof(Expr));
  expr->kind = kind;
  expr->line = token->line;
  expr->column = token->column;
  return expr;
}

static Expr *parse_primary(Parser *parser) {
  Token *token = current(parser);
  if (match_kind(parser, TOK_IDENT)) {
    if (token->text[0] >= 'A' && token->text[0] <= 'Z' && check(parser, "{")) {
      parser->index++;
      Expr *expr = new_expr(EXPR_SHAPE_LITERAL, token);
      expr->text = z_strdup(token->text);
      if (!match(parser, "}")) {
        do {
          Token *field_name = expect_ident(parser, "expected shape literal field name");
          if (!field_name) return expr;
          expect(parser, ":", "expected ':' after shape literal field name");
          FieldInit field = {
            .name = z_strdup(field_name->text),
            .value = parse_expr(parser),
            .line = field_name->line,
            .column = field_name->column
          };
          push_field_init(&expr->fields, field);
        } while (match(parser, ","));
        expect(parser, "}", "expected '}' after shape literal");
      }
      return expr;
    }
    Expr *expr = new_expr(EXPR_IDENT, token);
    expr->text = z_strdup(token->text);
    return expr;
  }
  if (match_kind(parser, TOK_STRING)) {
    Expr *expr = new_expr(EXPR_STRING, token);
    expr->text = z_strdup(token->text);
    return expr;
  }
  if (match_kind(parser, TOK_CHAR)) {
    Expr *expr = new_expr(EXPR_CHAR, token);
    expr->text = z_strdup(token->text);
    return expr;
  }
  if (match_kind(parser, TOK_NUMBER)) {
    Expr *expr = new_expr(EXPR_NUMBER, token);
    expr->text = z_strdup(token->text);
    return expr;
  }
  if (match(parser, "true") || match(parser, "false")) {
    Token *prev = previous(parser);
    Expr *expr = new_expr(EXPR_BOOL, prev);
    expr->bool_value = strcmp(prev->text, "true") == 0;
    return expr;
  }
  if (match(parser, "null")) {
    return new_expr(EXPR_NULL, previous(parser));
  }
  if (match(parser, "(")) {
    Expr *expr = parse_expr(parser);
    expect(parser, ")", "expected ')' after expression");
    return expr;
  }
  if (match(parser, "[")) {
    Expr *expr = new_expr(EXPR_ARRAY_LITERAL, token);
    if (!match(parser, "]")) {
      Expr *first = parse_expr(parser);
      if (first) push_expr(&expr->args, first);
      if (match(parser, ";")) {
        expr->array_repeat = true;
        Expr *count = parse_expr(parser);
        if (count) push_expr(&expr->args, count);
      } else {
        while (match(parser, ",")) {
          Expr *item = parse_expr(parser);
          if (item) push_expr(&expr->args, item);
        }
      }
      expect(parser, "]", "expected ']' after array literal");
    }
    return expr;
  }
  fail(parser, "expected expression");
  return NULL;
}

static Expr *parse_postfix(Parser *parser) {
  Expr *expr = parse_primary(parser);
  if (!expr) return NULL;
  while (true) {
    if (match(parser, ".")) {
      Token *property = expect_ident(parser, "expected field name after '.'");
      if (!property) return expr;
      Expr *member = new_expr(EXPR_MEMBER, property);
      member->left = expr;
      member->text = z_strdup(property->text);
      expr = member;
      continue;
    }
    if (looks_like_type_args_before_call(parser)) {
      expr->type_args = parse_type_args(parser);
      continue;
    }
    if (match(parser, "[")) {
      Token *index_token = previous(parser);
      if (match(parser, "..")) {
        Expr *slice = new_expr(EXPR_SLICE, index_token);
        slice->left = expr;
        push_expr(&slice->args, NULL);
        Expr *end = check(parser, "]") ? NULL : parse_expr(parser);
        push_expr(&slice->args, end);
        expect(parser, "]", "expected ']' after slice expression");
        expr = slice;
        continue;
      }
      Expr *start = parse_expr(parser);
      if (match(parser, "..")) {
        Expr *slice = new_expr(EXPR_SLICE, index_token);
        slice->left = expr;
        push_expr(&slice->args, start);
        Expr *end = check(parser, "]") ? NULL : parse_expr(parser);
        push_expr(&slice->args, end);
        expect(parser, "]", "expected ']' after slice expression");
        expr = slice;
        continue;
      }
      Expr *index = new_expr(EXPR_INDEX, index_token);
      index->left = expr;
      index->right = start;
      expect(parser, "]", "expected ']' after index expression");
      expr = index;
      continue;
    }
    if (match(parser, "(")) {
      Expr *call = new_expr(EXPR_CALL, expr ? &parser->tokens->items[parser->index - 1] : current(parser));
      call->left = expr;
      if (!match(parser, ")")) {
        do {
          Expr *arg = parse_expr(parser);
          if (arg) push_expr(&call->args, arg);
        } while (match(parser, ","));
        expect(parser, ")", "expected ')' after arguments");
      }
      expr = call;
      continue;
    }
    return expr;
  }
}

static Expr *parse_unary(Parser *parser) {
  if (match(parser, "meta")) {
    Token *meta_token = previous(parser);
    Expr *expr = new_expr(EXPR_META, meta_token);
    expr->left = parse_unary(parser);
    return expr;
  }
  if (match(parser, "check")) {
    Token *check_token = previous(parser);
    Expr *expr = new_expr(EXPR_CHECK, check_token);
    expr->left = parse_unary(parser);
    return expr;
  }
  if (match(parser, "&")) {
    Token *borrow_token = previous(parser);
    bool mutable_borrow = match(parser, "mut");
    Expr *expr = new_expr(EXPR_BORROW, borrow_token);
    expr->mutable_borrow = mutable_borrow;
    expr->left = parse_unary(parser);
    return expr;
  }
  return parse_postfix(parser);
}

static Expr *parse_binary(Parser *parser, int min_precedence) {
  Expr *left = parse_unary(parser);
  if (!left) return NULL;
  while (true) {
    int prec = precedence(current(parser)->text);
    if (prec < min_precedence) return left;
    Token *op = current(parser);
    parser->index++;
    Expr *right = parse_binary(parser, prec + 1);
    Expr *binary = new_expr(EXPR_BINARY, op);
    binary->left = left;
    binary->right = right;
    binary->text = z_strdup(op->text);
    left = binary;
  }
}

static Expr *parse_expr(Parser *parser) {
  Expr *expr = parse_binary(parser, 0);
  if (!expr) return NULL;
  while (match(parser, "as")) {
    Token *cast_token = previous(parser);
    Expr *cast = new_expr(EXPR_CAST, cast_token);
    cast->left = expr;
    cast->text = parse_type(parser);
    expr = cast;
  }
  if (match(parser, "rescue")) {
    Token *rescue_token = previous(parser);
    Expr *rescue = new_expr(EXPR_RESCUE, rescue_token);
    rescue->left = expr;
    Token *name = expect_ident(parser, "expected error binding after rescue");
    if (name) rescue->text = z_strdup(name->text);
    expect(parser, "{", "expected '{' before rescue fallback");
    rescue->right = parse_expr(parser);
    expect(parser, "}", "expected '}' after rescue fallback");
    expr = rescue;
  }
  return expr;
}

static Stmt *new_stmt(StmtKind kind, Token *token) {
  Stmt *stmt = z_checked_calloc(1, sizeof(Stmt));
  stmt->kind = kind;
  stmt->line = token->line;
  stmt->column = token->column;
  return stmt;
}

static Stmt *parse_statement(Parser *parser) {
  Token *start = current(parser);
  if (match(parser, "let")) {
    Stmt *stmt = new_stmt(STMT_LET, start);
    stmt->mutable_binding = match(parser, "mut");
    Token *name = expect_ident(parser, "expected binding name");
    if (!name) return stmt;
    stmt->name = z_strdup(name->text);
    if (match(parser, ":")) {
      stmt->type = parse_type(parser);
    }
    expect(parser, "=", "expected '=' in let binding");
    stmt->expr = parse_expr(parser);
    return stmt;
  }
  if (current(parser)->kind == TOK_IDENT) {
    size_t target_start = parser->index;
    Expr *target = parse_postfix(parser);
    if (target && match(parser, "=")) {
      Stmt *stmt = new_stmt(STMT_ASSIGN, start);
      stmt->target = target;
      if (target->kind == EXPR_IDENT) stmt->name = z_strdup(target->text);
      stmt->expr = parse_expr(parser);
      return stmt;
    }
    free_expr(target);
    parser->index = target_start;
  }
  if (match(parser, "defer")) {
    Stmt *stmt = new_stmt(STMT_DEFER, start);
    stmt->expr = parse_expr(parser);
    return stmt;
  }
  if (match(parser, "check")) {
    Stmt *stmt = new_stmt(STMT_CHECK, start);
    stmt->expr = parse_expr(parser);
    return stmt;
  }
  if (match(parser, "with")) {
    Stmt *stmt = new_stmt(STMT_WITH, start);
    Token *effect_name = expect_ident(parser, "expected effect name after with");
    if (effect_name) stmt->name = z_strdup(effect_name->text);
    expect(parser, "handledBy", "expected 'handledBy' after effect name");
    expect(parser, "{", "expected '{' before handler body");
    while (!check(parser, "}") && current(parser)->kind != TOK_EOF && parser->diag->code == 0) {
      if (check(parser, "pub") || check(parser, "fun")) {
        Function op = parse_function(parser);
        if (!stmt->handler_ops) stmt->handler_ops = calloc(1, sizeof(FunctionVec));
        push_function((FunctionVec *)stmt->handler_ops, op);
        continue;
      }
      fail(parser, "expected handler function declaration");
      break;
    }
    expect(parser, "}", "expected '}' after handler body");
    return stmt;
  }
  if (match(parser, "return")) {
    Stmt *stmt = new_stmt(STMT_RETURN, start);
    if (!check(parser, "}")) stmt->expr = parse_expr(parser);
    return stmt;
  }
  if (match(parser, "raise")) {
    Stmt *stmt = new_stmt(STMT_RAISE, start);
    Token *name = expect_ident(parser, "expected error name after raise");
    if (name) stmt->name = z_strdup(name->text);
    return stmt;
  }
  if (match(parser, "if")) {
    Stmt *stmt = new_stmt(STMT_IF, start);
    stmt->expr = parse_expr(parser);
    stmt->then_body = parse_block(parser);
    if (match(parser, "else")) stmt->else_body = parse_block(parser);
    return stmt;
  }
  if (match(parser, "while")) {
    Stmt *stmt = new_stmt(STMT_WHILE, start);
    stmt->expr = parse_expr(parser);
    stmt->then_body = parse_block(parser);
    return stmt;
  }
  if (match(parser, "for")) {
    Stmt *stmt = new_stmt(STMT_FOR, start);
    Token *name = expect_ident(parser, "expected loop binding name");
    if (!name) return stmt;
    stmt->name = z_strdup(name->text);
    expect(parser, "in", "expected 'in' after loop binding");
    stmt->expr = parse_expr(parser);
    expect(parser, "..", "expected '..' in range loop");
    stmt->range_end = parse_expr(parser);
    stmt->then_body = parse_block(parser);
    return stmt;
  }
  if (match(parser, "break")) {
    return new_stmt(STMT_BREAK, start);
  }
  if (match(parser, "continue")) {
    return new_stmt(STMT_CONTINUE, start);
  }
  if (match(parser, "match")) {
    Stmt *stmt = new_stmt(STMT_MATCH, start);
    stmt->expr = parse_expr(parser);
    expect(parser, "{", "expected '{' before match arms");
    while (!check(parser, "}") && current(parser)->kind != TOK_EOF && parser->diag->code == 0) {
      match(parser, ".");
      Token *case_name = current(parser);
      if (case_name->kind == TOK_IDENT || case_name->kind == TOK_NUMBER ||
          (case_name->kind == TOK_KEYWORD && (strcmp(case_name->text, "true") == 0 || strcmp(case_name->text, "false") == 0))) {
        parser->index++;
      } else {
        fail(parser, "expected match case name");
        break;
      }
      if (!case_name) break;
      char *range_end = NULL;
      if (case_name->kind == TOK_NUMBER && match(parser, "..")) {
        Token *end = current(parser);
        if (end->kind != TOK_NUMBER) {
          fail(parser, "expected integer range end");
          break;
        }
        parser->index++;
        range_end = z_strdup(end->text);
      }
      char *payload_name = NULL;
      if (match(parser, "=>")) {
        Token *payload = expect_ident(parser, "expected payload binding name");
        if (!payload) break;
        payload_name = z_strdup(payload->text);
      }
      Expr *guard = NULL;
      if (match(parser, "if")) guard = parse_expr(parser);
      MatchArm arm = {
        .case_name = z_strdup(case_name->text),
        .range_end = range_end,
        .payload_name = payload_name,
        .guard = guard,
        .body = parse_block(parser),
        .line = case_name->line,
        .column = case_name->column
      };
      push_match_arm(&stmt->match_arms, arm);
    }
    expect(parser, "}", "expected '}' after match arms");
    return stmt;
  }
  Stmt *stmt = new_stmt(STMT_EXPR, start);
  stmt->expr = parse_expr(parser);
  return stmt;
}

static StmtVec parse_block(Parser *parser) {
  StmtVec body = {0};
  if (!expect(parser, "{", "expected '{' before block")) return body;
  while (!check(parser, "}") && current(parser)->kind != TOK_EOF && parser->diag->code == 0) {
    push_stmt(&body, parse_statement(parser));
  }
  expect(parser, "}", "expected '}' after block");
  return body;
}

static Function parse_function(Parser *parser) {
  Token *start = current(parser);
  Function fun = {0};
  if (match(parser, "export")) {
    expect(parser, "c", "expected 'c' after export");
    fun.export_c = true;
  }
  fun.is_public = match(parser, "pub");
  expect(parser, "fun", "expected function declaration");
  Token *name = expect_ident(parser, "expected function name");
  fun.name = name ? z_strdup(name->text) : z_strdup("<error>");
  fun.line = start->line;
  fun.column = start->column;
  fun.type_params = parse_type_params(parser);
  fun.params = parse_params(parser);
  expect(parser, "->", "expected return type after parameters");
  fun.return_type = parse_type(parser);
  if (match(parser, "raises")) {
    fun.raises = true;
    if (check(parser, "{") &&
        parser->tokens->items[parser->index + 1].text &&
        parser->tokens->items[parser->index + 1].text[0] >= 'A' &&
        parser->tokens->items[parser->index + 1].text[0] <= 'Z') {
      fun.has_error_set = true;
      fun.errors = parse_error_set(parser);
    }
  }
  fun.body = parse_block(parser);
  return fun;
}

static Function parse_interface_method(Parser *parser) {
  Token *start = current(parser);
  Function fun = {0};
  fun.is_public = match(parser, "pub");
  expect(parser, "fun", "expected interface method declaration");
  Token *name = expect_ident(parser, "expected interface method name");
  fun.name = name ? z_strdup(name->text) : z_strdup("<error>");
  fun.line = start->line;
  fun.column = start->column;
  fun.type_params = parse_type_params(parser);
  fun.params = parse_params(parser);
  expect(parser, "->", "expected return type after interface method parameters");
  fun.return_type = parse_type(parser);
  if (match(parser, "raises")) {
    fun.raises = true;
    if (check(parser, "{") &&
        parser->tokens->items[parser->index + 1].text &&
        parser->tokens->items[parser->index + 1].text[0] >= 'A' &&
        parser->tokens->items[parser->index + 1].text[0] <= 'Z') {
      fun.has_error_set = true;
      fun.errors = parse_error_set(parser);
    }
  }
  return fun;
}

static Function parse_test_block(Parser *parser) {
  Token *start = current(parser);
  Function fun = {0};
  expect(parser, "test", "expected test block");
  Token *name = current(parser);
  if (name->kind == TOK_STRING) parser->index++;
  static size_t test_counter = 0;
  ZBuf generated;
  zbuf_init(&generated);
  zbuf_appendf(&generated, "__zero_test_%zu", test_counter++);
  fun.name = generated.data;
  fun.test_name = name->kind == TOK_STRING ? z_strdup(name->text) : z_strdup(fun.name);
  fun.return_type = z_strdup("Void");
  fun.is_test = true;
  fun.line = start->line;
  fun.column = start->column;
  (void)name;
  fun.body = parse_block(parser);
  return fun;
}

static ConstDecl parse_const(Parser *parser, bool is_public) {
  Token *start = current(parser);
  ConstDecl item = {0};
  item.is_public = is_public;
  expect(parser, "const", "expected const declaration");
  Token *name = expect_ident(parser, "expected const name");
  item.name = name ? z_strdup(name->text) : z_strdup("<error>");
  item.line = start->line;
  item.column = start->column;
  if (match(parser, ":")) item.type = parse_type(parser);
  expect(parser, "=", "expected '=' in const declaration");
  item.expr = parse_expr(parser);
  return item;
}

static TypeAlias parse_type_alias(Parser *parser, bool is_public) {
  Token *start = current(parser);
  TypeAlias item = {0};
  item.is_public = is_public;
  expect(parser, "type", "expected type alias declaration");
  Token *name = expect_ident(parser, "expected type alias name");
  item.name = name ? z_strdup(name->text) : z_strdup("<error>");
  item.line = start->line;
  item.column = start->column;
  expect(parser, "=", "expected '=' in type alias declaration");
  item.target = parse_type(parser);
  return item;
}

static Shape parse_shape(Parser *parser, bool is_public) {
  Token *start = current(parser);
  Shape shape = {0};
  shape.is_public = is_public;
  shape.layout = z_strdup("auto");
  if (match(parser, "extern")) {
    free(shape.layout);
    shape.layout = z_strdup("extern");
  } else if (match(parser, "packed")) {
    free(shape.layout);
    shape.layout = z_strdup("packed");
  }
  expect(parser, "shape", "expected shape declaration");
  Token *name = expect_ident(parser, "expected shape name");
  shape.name = name ? z_strdup(name->text) : z_strdup("<error>");
  shape.line = start->line;
  shape.column = start->column;
  shape.type_params = parse_type_params(parser);
  expect(parser, "{", "expected '{' before shape body");
  while (!check(parser, "}") && current(parser)->kind != TOK_EOF && parser->diag->code == 0) {
    if (check(parser, "pub") || check(parser, "fun")) {
      push_method(&shape.methods, parse_function(parser));
      continue;
    }
    Token *field_name = expect_ident(parser, "expected field name");
    if (!field_name) break;
    expect(parser, ":", "expected ':' after field name");
    Param field = {
      .name = z_strdup(field_name->text),
      .type = parse_type(parser),
      .line = field_name->line,
      .column = field_name->column
    };
    if (match(parser, "=")) field.default_value = parse_expr(parser);
    push_param(&shape.fields, field);
    match(parser, ",");
  }
  expect(parser, "}", "expected '}' after shape body");
  return shape;
}

static InterfaceDecl parse_interface(Parser *parser, bool is_public) {
  Token *start = current(parser);
  InterfaceDecl interface = {0};
  interface.is_public = is_public;
  expect(parser, "interface", "expected interface declaration");
  Token *name = expect_ident(parser, "expected interface name");
  interface.name = name ? z_strdup(name->text) : z_strdup("<error>");
  interface.line = start->line;
  interface.column = start->column;
  interface.type_params = parse_type_params(parser);
  expect(parser, "{", "expected '{' before interface body");
  while (!check(parser, "}") && current(parser)->kind != TOK_EOF && parser->diag->code == 0) {
    if (check(parser, "pub") || check(parser, "fun")) {
      push_method(&interface.methods, parse_interface_method(parser));
      continue;
    }
    fail(parser, "expected interface method declaration");
    break;
  }
  expect(parser, "}", "expected '}' after interface body");
  return interface;
}

static EnumDecl parse_enum(Parser *parser) {
  Token *start = current(parser);
  EnumDecl item = {0};
  expect(parser, "enum", "expected enum declaration");
  Token *name = expect_ident(parser, "expected enum name");
  item.name = name ? z_strdup(name->text) : z_strdup("<error>");
  item.line = start->line;
  item.column = start->column;
  if (match(parser, ":")) {
    Token *backing = expect_ident(parser, "expected enum backing type");
    item.type = backing ? z_strdup(backing->text) : z_strdup("u32");
  }
  expect(parser, "{", "expected '{' before enum body");
  while (!check(parser, "}") && current(parser)->kind != TOK_EOF && parser->diag->code == 0) {
    Token *case_name = expect_ident(parser, "expected enum case name");
    if (!case_name) break;
    Param item_case = {.name = z_strdup(case_name->text), .type = NULL, .line = case_name->line, .column = case_name->column};
    if (match(parser, "=")) {
      while (!check(parser, ",") && !check(parser, "}") && current(parser)->kind != TOK_EOF) parser->index++;
    }
    push_param(&item.cases, item_case);
    match(parser, ",");
  }
  expect(parser, "}", "expected '}' after enum body");
  return item;
}

static Choice parse_choice(Parser *parser) {
  Token *start = current(parser);
  Choice item = {0};
  expect(parser, "choice", "expected choice declaration");
  Token *name = expect_ident(parser, "expected choice name");
  item.name = name ? z_strdup(name->text) : z_strdup("<error>");
  item.line = start->line;
  item.column = start->column;
  expect(parser, "{", "expected '{' before choice body");
  while (!check(parser, "}") && current(parser)->kind != TOK_EOF && parser->diag->code == 0) {
    Token *case_name = expect_ident(parser, "expected choice case name");
    if (!case_name) break;
    Param item_case = {.name = z_strdup(case_name->text), .type = NULL, .line = case_name->line, .column = case_name->column};
    if (match(parser, ":")) item_case.type = parse_type(parser);
    push_param(&item.cases, item_case);
    match(parser, ",");
  }
  expect(parser, "}", "expected '}' after choice body");
  return item;
}

static UseImport parse_use_import(Parser *parser) {
  Token *start = current(parser);
  UseImport item = {0};
  expect(parser, "use", "expected use declaration");
  item.line = start->line;
  item.column = start->column;

  ZBuf module;
  zbuf_init(&module);
  if (!token_on_line(current(parser), start->line) || current(parser)->kind != TOK_IDENT) {
    fail_at(parser, current(parser), "expected import module name");
  }
  Token *segment = parser->diag->code == 0 ? expect_ident(parser, "expected import module name") : NULL;
  Token *end_token = segment;
  if (segment) zbuf_append(&module, segment->text);
  while (parser->diag->code == 0 && token_on_line(current(parser), start->line) && match(parser, ".")) {
    Token *dot = previous(parser);
    if (!token_on_line(current(parser), start->line) || current(parser)->kind != TOK_IDENT) {
      fail_at(parser, dot, "expected import module segment after '.'");
      break;
    }
    segment = expect_ident(parser, "expected import module segment");
    if (segment) {
      zbuf_append_char(&module, '.');
      zbuf_append(&module, segment->text);
      end_token = segment;
    }
  }
  if (parser->diag->code == 0 && check(parser, "as") && !token_on_line(current(parser), start->line)) {
    fail_at(parser, current(parser), "expected import alias on same line");
  }
  if (parser->diag->code == 0 && token_on_line(current(parser), start->line) && match(parser, "as")) {
    if (!token_on_line(current(parser), start->line) || current(parser)->kind != TOK_IDENT) {
      fail_at(parser, current(parser), "expected import alias");
    }
    Token *alias = parser->diag->code == 0 ? expect_ident(parser, "expected import alias") : NULL;
    if (alias) {
      item.alias = z_strdup(alias->text);
      end_token = alias;
    }
  }
  if (parser->diag->code == 0 && token_on_line(current(parser), start->line)) {
    fail_at(parser, current(parser), "expected end of line after use declaration");
  }
  item.module = module.data ? module.data : z_strdup("");
  item.end_column = token_end_column(end_token);
  return item;
}

Program z_parse(TokenVec *tokens, ZDiag *diag) {
  Parser parser = {tokens, 0, diag};
  Program program = {0};
  while (current(&parser)->kind != TOK_EOF && diag->code == 0) {
    if (check(&parser, "use")) {
      UseImport item = parse_use_import(&parser);
      if (diag->code == 0) push_use_import(&program.use_imports, item);
      else {
        free(item.module);
        free(item.alias);
      }
      continue;
    }
    if (check(&parser, "extern") && parser.tokens->items[parser.index + 1].text && strcmp(parser.tokens->items[parser.index + 1].text, "c") == 0) {
      Token *start = current(&parser);
      parser.index += 2;
      Token *header = current(&parser);
      if (header->kind == TOK_STRING) parser.index++;
      else fail(&parser, "expected extern c header string");
      Token *alias = NULL;
      if (match(&parser, "as")) alias = expect_ident(&parser, "expected extern alias");
      if (header && header->kind == TOK_STRING && alias) {
        push_c_import(&program.c_imports, (CImport){
          .header = z_strdup(header->text),
          .alias = z_strdup(alias->text),
          .line = start->line,
          .column = start->column
        });
      }
      continue;
    }
    bool is_public = match(&parser, "pub");
    if (check(&parser, "const")) {
      push_const(&program.consts, parse_const(&parser, is_public));
      continue;
    }
    if (check(&parser, "type")) {
      push_alias(&program.aliases, parse_type_alias(&parser, is_public));
      continue;
    }
    if (check(&parser, "interface")) {
      push_interface(&program.interfaces, parse_interface(&parser, is_public));
      continue;
    }
    if (check(&parser, "shape") || check(&parser, "extern") || check(&parser, "packed")) {
      push_shape(&program.shapes, parse_shape(&parser, is_public));
      continue;
    }
    if (check(&parser, "enum")) {
      push_enum(&program.enums, parse_enum(&parser));
      continue;
    }
    if (check(&parser, "choice")) {
      push_choice(&program.choices, parse_choice(&parser));
      continue;
    }
    if (check(&parser, "test")) {
      push_function(&program.functions, parse_test_block(&parser));
      continue;
    }
    if (is_public) parser.index--;
    push_function(&program.functions, parse_function(&parser));
  }
  return program;
}

static void free_expr(Expr *expr) {
  if (!expr) return;
  free(expr->text);
  free(expr->resolved_type);
  free_expr(expr->left);
  free_expr(expr->right);
  for (size_t i = 0; i < expr->args.len; i++) free_expr(expr->args.items[i]);
  free(expr->args.items);
  for (size_t i = 0; i < expr->type_args.len; i++) free(expr->type_args.items[i].type);
  free(expr->type_args.items);
  for (size_t i = 0; i < expr->fields.len; i++) {
    free(expr->fields.items[i].name);
    free_expr(expr->fields.items[i].value);
  }
  free(expr->fields.items);
  free(expr);
}

static void free_stmt_vec(StmtVec *vec) {
  for (size_t i = 0; i < vec->len; i++) {
    Stmt *stmt = vec->items[i];
    free(stmt->name);
    free(stmt->type);
    free(stmt->resolved_type);
    free_expr(stmt->target);
    free_expr(stmt->expr);
    free_expr(stmt->range_end);
    free_stmt_vec(&stmt->then_body);
    free_stmt_vec(&stmt->else_body);
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      free(stmt->match_arms.items[arm_index].case_name);
      free(stmt->match_arms.items[arm_index].range_end);
      free(stmt->match_arms.items[arm_index].payload_name);
      free_expr(stmt->match_arms.items[arm_index].guard);
      free_stmt_vec(&stmt->match_arms.items[arm_index].body);
    }
    free(stmt->match_arms.items);
    if (stmt->handler_ops) {
      FunctionVec *hops = (FunctionVec *)stmt->handler_ops;
      for (size_t op_index = 0; op_index < hops->len; op_index++) {
        Function *op = &hops->items[op_index];
        free(op->name);
        free(op->return_type);
        for (size_t p = 0; p < op->params.len; p++) {
          free(op->params.items[p].name);
          free(op->params.items[p].type);
        }
        free(op->params.items);
        free_stmt_vec(&op->body);
      }
      free(hops->items);
      free(stmt->handler_ops);
    }
    free(stmt);
  }
  free(vec->items);
}

void z_free_program(Program *program) {
  for (size_t i = 0; i < program->use_imports.len; i++) {
    free(program->use_imports.items[i].module);
    free(program->use_imports.items[i].alias);
  }
  free(program->use_imports.items);
  for (size_t i = 0; i < program->consts.len; i++) {
    ConstDecl *item = &program->consts.items[i];
    free(item->name);
    free(item->type);
    free_expr(item->expr);
  }
  free(program->consts.items);
  for (size_t i = 0; i < program->aliases.len; i++) {
    TypeAlias *item = &program->aliases.items[i];
    free(item->name);
    free(item->target);
  }
  free(program->aliases.items);
  for (size_t i = 0; i < program->interfaces.len; i++) {
    InterfaceDecl *interface = &program->interfaces.items[i];
    free(interface->name);
    for (size_t param_index = 0; param_index < interface->type_params.len; param_index++) {
      free(interface->type_params.items[param_index].name);
      free(interface->type_params.items[param_index].type);
    }
    free(interface->type_params.items);
    for (size_t method_index = 0; method_index < interface->methods.len; method_index++) {
      Function *method = &interface->methods.items[method_index];
      free(method->name);
      free(method->return_type);
      for (size_t type_param_index = 0; type_param_index < method->type_params.len; type_param_index++) {
        free(method->type_params.items[type_param_index].name);
        free(method->type_params.items[type_param_index].type);
      }
      free(method->type_params.items);
      for (size_t error_index = 0; error_index < method->errors.len; error_index++) {
        free(method->errors.items[error_index].name);
        free(method->errors.items[error_index].type);
      }
      free(method->errors.items);
      for (size_t param_index = 0; param_index < method->params.len; param_index++) {
        free(method->params.items[param_index].name);
        free(method->params.items[param_index].type);
      }
      free(method->params.items);
      free_stmt_vec(&method->body);
    }
    free(interface->methods.items);
  }
  free(program->interfaces.items);
  for (size_t i = 0; i < program->shapes.len; i++) {
    Shape *shape = &program->shapes.items[i];
    free(shape->name);
    free(shape->layout);
    for (size_t param_index = 0; param_index < shape->type_params.len; param_index++) {
      free(shape->type_params.items[param_index].name);
      free(shape->type_params.items[param_index].type);
    }
    free(shape->type_params.items);
    for (size_t field_index = 0; field_index < shape->fields.len; field_index++) {
      free(shape->fields.items[field_index].name);
      free(shape->fields.items[field_index].type);
      free_expr(shape->fields.items[field_index].default_value);
    }
    free(shape->fields.items);
    for (size_t method_index = 0; method_index < shape->methods.len; method_index++) {
      Function *method = &shape->methods.items[method_index];
      free(method->name);
      free(method->return_type);
      for (size_t type_param_index = 0; type_param_index < method->type_params.len; type_param_index++) {
        free(method->type_params.items[type_param_index].name);
        free(method->type_params.items[type_param_index].type);
      }
      free(method->type_params.items);
      for (size_t error_index = 0; error_index < method->errors.len; error_index++) {
        free(method->errors.items[error_index].name);
        free(method->errors.items[error_index].type);
      }
      free(method->errors.items);
      for (size_t param_index = 0; param_index < method->params.len; param_index++) {
        free(method->params.items[param_index].name);
        free(method->params.items[param_index].type);
      }
      free(method->params.items);
      free_stmt_vec(&method->body);
    }
    free(shape->methods.items);
  }
  free(program->shapes.items);
  for (size_t i = 0; i < program->enums.len; i++) {
    EnumDecl *item = &program->enums.items[i];
    free(item->name);
    free(item->type);
    for (size_t case_index = 0; case_index < item->cases.len; case_index++) {
      free(item->cases.items[case_index].name);
      free(item->cases.items[case_index].type);
    }
    free(item->cases.items);
  }
  free(program->enums.items);
  for (size_t i = 0; i < program->choices.len; i++) {
    Choice *item = &program->choices.items[i];
    free(item->name);
    for (size_t case_index = 0; case_index < item->cases.len; case_index++) {
      free(item->cases.items[case_index].name);
      free(item->cases.items[case_index].type);
    }
    free(item->cases.items);
  }
  free(program->choices.items);
  for (size_t i = 0; i < program->c_imports.len; i++) {
    free(program->c_imports.items[i].header);
    free(program->c_imports.items[i].alias);
  }
  free(program->c_imports.items);
  for (size_t i = 0; i < program->functions.len; i++) {
    Function *fun = &program->functions.items[i];
    free(fun->name);
    free(fun->test_name);
    free(fun->return_type);
    for (size_t type_param_index = 0; type_param_index < fun->type_params.len; type_param_index++) {
      free(fun->type_params.items[type_param_index].name);
      free(fun->type_params.items[type_param_index].type);
    }
    free(fun->type_params.items);
    for (size_t error_index = 0; error_index < fun->errors.len; error_index++) {
      free(fun->errors.items[error_index].name);
      free(fun->errors.items[error_index].type);
    }
    free(fun->errors.items);
    for (size_t param_index = 0; param_index < fun->params.len; param_index++) {
      free(fun->params.items[param_index].name);
      free(fun->params.items[param_index].type);
    }
    free(fun->params.items);
    free_stmt_vec(&fun->body);
  }
  free(program->functions.items);
  program->functions.items = NULL;
  program->functions.len = 0;
  program->functions.cap = 0;
}
