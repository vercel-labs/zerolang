#include "zero.h"
#include "specialize.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
  copy->left = clone_expr(expr->left);
  copy->right = clone_expr(expr->right);
  for (size_t i = 0; i < expr->args.len; i++) push_expr_clone(&copy->args, expr->args.items[i]);
  for (size_t i = 0; i < expr->type_args.len; i++) push_type_arg_clone(&copy->type_args, &expr->type_args.items[i]);
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

static IrTypeKind ir_type_kind(const char *type) {
  if (!type) return IR_TYPE_UNSUPPORTED;
  if (strcmp(type, "Void") == 0) return IR_TYPE_VOID;
  if (strcmp(type, "Bool") == 0 || strcmp(type, "bool") == 0) return IR_TYPE_BOOL;
  if (strcmp(type, "u8") == 0) return IR_TYPE_U8;
  if (strcmp(type, "u16") == 0) return IR_TYPE_U16;
  if (strcmp(type, "usize") == 0) return IR_TYPE_USIZE;
  if (strcmp(type, "i32") == 0) return IR_TYPE_I32;
  if (strcmp(type, "u32") == 0) return IR_TYPE_U32;
  if (strcmp(type, "i64") == 0) return IR_TYPE_I64;
  if (strcmp(type, "u64") == 0) return IR_TYPE_U64;
  if (strcmp(type, "Duration") == 0) return IR_TYPE_I64;
  if (strcmp(type, "RandSource") == 0) return IR_TYPE_U32;
  if (strcmp(type, "ProcStatus") == 0) return IR_TYPE_I32;
  if (strcmp(type, "Net") == 0 || strcmp(type, "HttpClient") == 0) return IR_TYPE_I32;
  if (strcmp(type, "HttpResult") == 0) return IR_TYPE_U64;
  if (strcmp(type, "HttpError") == 0) return IR_TYPE_U32;
  if (strcmp(type, "HttpHeaderValue") == 0) return IR_TYPE_U64;
  if (strcmp(type, "Fs") == 0 || strcmp(type, "File") == 0 || strcmp(type, "owned<File>") == 0) return IR_TYPE_I32;
  if (strcmp(type, "String") == 0 ||
      strcmp(type, "Span<u8>") == 0 ||
      strcmp(type, "Span<const u8>") == 0 ||
      strcmp(type, "MutSpan<u8>") == 0 ||
      strcmp(type, "ByteBuf") == 0 ||
      strcmp(type, "owned<ByteBuf>") == 0) {
    return IR_TYPE_BYTE_VIEW;
  }
  if (strcmp(type, "FixedBufAlloc") == 0) return IR_TYPE_ALLOC;
  if (strcmp(type, "Vec") == 0) return IR_TYPE_VEC;
  if (strcmp(type, "BufferedReader") == 0 || strcmp(type, "BufferedWriter") == 0) return IR_TYPE_BYTE_VIEW;
  if (strcmp(type, "Maybe<MutSpan<u8>>") == 0 || strcmp(type, "Maybe<String>") == 0 || strcmp(type, "Maybe<owned<ByteBuf>>") == 0) return IR_TYPE_MAYBE_BYTE_VIEW;
  if (strcmp(type, "Maybe<JsonDoc>") == 0 ||
      strcmp(type, "Maybe<u8>") == 0 ||
      strcmp(type, "Maybe<u16>") == 0 ||
      strcmp(type, "Maybe<usize>") == 0 ||
      strcmp(type, "Maybe<i32>") == 0 ||
      strcmp(type, "Maybe<u32>") == 0 ||
      strcmp(type, "Maybe<owned<File>>") == 0) return IR_TYPE_MAYBE_SCALAR;
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

static bool ir_type_is_direct_fallible_value(IrTypeKind type) {
  return type == IR_TYPE_VOID || type == IR_TYPE_BOOL || type == IR_TYPE_U8 ||
         type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_I32 ||
         type == IR_TYPE_U32;
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
  if (backing == IR_TYPE_USIZE) return IR_TYPE_U32;
  return IR_TYPE_UNSUPPORTED;
}

static IrTypeKind ir_enum_backing_type(const EnumDecl *item_enum) {
  const char *type = item_enum && item_enum->type ? item_enum->type : "u8";
  IrTypeKind backing = ir_type_kind(type);
  if (backing == IR_TYPE_U8 || backing == IR_TYPE_U16 || backing == IR_TYPE_U32) return backing;
  if (backing == IR_TYPE_USIZE) return IR_TYPE_U32;
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
    case IR_TYPE_USIZE:
    case IR_TYPE_U32: return 4;
    case IR_TYPE_I64:
    case IR_TYPE_U64: return 8;
    default: return 0;
  }
}

static unsigned ir_type_alignment(IrTypeKind type) {
  unsigned size = ir_type_byte_size(type);
  if (size >= 4) return 4;
  return size ? size : 1;
}

static bool ir_parse_fixed_array_type(const char *type, unsigned *out_len, IrTypeKind *out_element);

static size_t ir_align_to(size_t value, size_t alignment) {
  size_t remainder = alignment ? value % alignment : 0;
  return remainder == 0 ? value : value + (alignment - remainder);
}

static bool ir_shape_layout(const Program *program, const char *shape_name, unsigned *out_size, unsigned *out_align) {
  const Shape *shape = NULL;
  TypeArgVec args = {0};
  if (!ir_shape_instance(program, shape_name, &shape, &args)) return false;
  size_t offset = 0;
  unsigned max_align = 1;
  for (size_t i = 0; i < shape->fields.len; i++) {
    char *field_type_text = ir_shape_substitute_type(shape, &args, shape->fields.items[i].type);
    IrTypeKind field_type = ir_type_kind_for_program(program, field_type_text);
    unsigned array_len = 0;
    IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
    bool is_array = ir_parse_fixed_array_type_for_program(program, field_type_text, &array_len, &element_type);
    if (!(field_type == IR_TYPE_BOOL || ir_type_is_value(field_type) || is_array)) {
      free(field_type_text);
      ir_type_arg_vec_free(&args);
      return false;
    }
    unsigned align = is_array ? ir_type_alignment(element_type) : ir_type_alignment(field_type);
    unsigned byte_size = is_array ? ir_type_byte_size(element_type) * array_len : ir_type_byte_size(field_type);
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

static bool ir_shape_field_info(const Program *program, const char *shape_name, const char *field_name, unsigned *out_offset, IrTypeKind *out_type) {
  const Shape *shape = NULL;
  TypeArgVec args = {0};
  if (!field_name || !ir_shape_instance(program, shape_name, &shape, &args)) return false;
  size_t offset = 0;
  for (size_t i = 0; i < shape->fields.len; i++) {
    const Param *field = &shape->fields.items[i];
    char *field_type_text = ir_shape_substitute_type(shape, &args, field->type);
    IrTypeKind field_type = ir_type_kind_for_program(program, field_type_text);
    unsigned array_len = 0;
    IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
    bool is_array = ir_parse_fixed_array_type_for_program(program, field_type_text, &array_len, &element_type);
    if (!(field_type == IR_TYPE_BOOL || ir_type_is_value(field_type) || is_array)) {
      free(field_type_text);
      ir_type_arg_vec_free(&args);
      return false;
    }
    unsigned align = is_array ? ir_type_alignment(element_type) : ir_type_alignment(field_type);
    unsigned byte_size = is_array ? ir_type_byte_size(element_type) * array_len : ir_type_byte_size(field_type);
    offset = ir_align_to(offset, align);
    if (strcmp(field->name, field_name) == 0) {
      free(field_type_text);
      if (is_array) {
        ir_type_arg_vec_free(&args);
        return false;
      }
      if (out_offset) *out_offset = (unsigned)offset;
      if (out_type) *out_type = field_type;
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
    IrTypeKind field_type = ir_type_kind_for_program(program, field_type_text);
    unsigned array_len = 0;
    IrTypeKind element_type = IR_TYPE_UNSUPPORTED;
    bool is_array = ir_parse_fixed_array_type_for_program(program, field_type_text, &array_len, &element_type);
    if (!(field_type == IR_TYPE_BOOL || ir_type_is_value(field_type) || is_array)) {
      free(field_type_text);
      ir_type_arg_vec_free(&args);
      return false;
    }
    unsigned align = is_array ? ir_type_alignment(element_type) : ir_type_alignment(field_type);
    unsigned byte_size = is_array ? ir_type_byte_size(element_type) * array_len : ir_type_byte_size(field_type);
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
  if (element != IR_TYPE_I32 && element != IR_TYPE_U32 && element != IR_TYPE_U8) {
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
                 (ch >= '0' && ch <= '9') || ch == '_';
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

static IrValue *ir_new_value(IrProgram *ir, IrValueKind kind, IrTypeKind type, int line, int column) {
  IrValue *value = z_checked_calloc(1, sizeof(IrValue));
  value->kind = kind;
  value->type = type;
  value->line = line;
  value->column = column;
  if (ir) ir->mir_bytes += sizeof(IrValue);
  return value;
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
    if (ir_parse_fixed_array_type(expr->resolved_type, &array_len, &element_type) && element_type == IR_TYPE_U8) return true;
  }
  return expr && (expr->kind == EXPR_STRING || expr->kind == EXPR_SLICE ||
                  expr->kind == EXPR_BORROW ||
                  (expr->kind == EXPR_IDENT && expr->resolved_type && ir_type_kind(expr->resolved_type) == IR_TYPE_BYTE_VIEW) ||
                  (expr->kind == EXPR_MEMBER && expr->resolved_type && ir_type_kind(expr->resolved_type) == IR_TYPE_BYTE_VIEW));
}

static bool ir_expr_is_mutable_byte_view_dest(const IrFunction *fun, const Expr *expr) {
  if (!fun || !expr) return false;
  if (expr->kind == EXPR_MEMBER && expr->left && expr->left->kind == EXPR_IDENT && strcmp(expr->text ? expr->text : "", "value") == 0) {
    const IrLocal *local = ir_function_find_local(fun, expr->left->text);
    return local && local->type == IR_TYPE_MAYBE_BYTE_VIEW;
  }
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
  *out = value;
  return true;
}

static bool ir_lower_byte_view(const Program *program, IrProgram *ir, const IrFunction *fun, const Expr *expr, IrValue **out) {
  if (!expr) {
    ir_mark_unsupported(ir, "direct backend byte view is missing", 1, 1, "missing expression");
    return false;
  }
  if (expr->kind == EXPR_STRING) {
    const char *text = expr->text ? expr->text : "";
    unsigned offset = 0;
    unsigned len = (unsigned)strlen(text);
    unsigned char *nul_terminated = z_checked_malloc((size_t)len + 1);
    memcpy(nul_terminated, text, len);
    nul_terminated[len] = 0;
    bool added = ir_add_readonly_data(ir, nul_terminated, len + 1, expr->line, expr->column, &offset);
    free(nul_terminated);
    if (!added) return false;
    IrValue *value = ir_new_value(ir, IR_VALUE_STRING_LITERAL, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
    value->data_offset = offset;
    value->data_len = len;
    *out = value;
    return true;
  }
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
          *out = value;
          return true;
        }
      }
    }
  }
  if (expr->kind == EXPR_BORROW) {
    return ir_lower_byte_view(program, ir, fun, expr->left, out);
  }
  if (expr->kind == EXPR_IDENT) {
    const IrLocal *local = ir_function_find_local(fun, expr->text);
    if (local && local->type == IR_TYPE_BYTE_VIEW) {
      IrValue *value = ir_new_value(ir, IR_VALUE_LOCAL, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
      value->local_index = local->index;
      *out = value;
      return true;
    }
    if (local && local->type == IR_TYPE_MAYBE_BYTE_VIEW) {
      IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_VALUE, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
      value->local_index = local->index;
      *out = value;
      return true;
    }
    if (local && local->is_array && local->element_type == IR_TYPE_U8) {
      IrValue *value = ir_new_value(ir, IR_VALUE_ARRAY_BYTE_VIEW, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
      value->array_index = local->index;
      value->data_len = local->array_len;
      *out = value;
      return true;
    }
    if (!local || local->type != IR_TYPE_BYTE_VIEW) {
      ir_mark_unsupported(ir, "direct backend byte view identifier is not a byte-view local", expr->line, expr->column, expr->text);
      return false;
    }
  }
  if (expr->kind == EXPR_MEMBER && expr->left && expr->left->kind == EXPR_IDENT && strcmp(expr->text ? expr->text : "", "value") == 0) {
    const IrLocal *local = ir_function_find_local(fun, expr->left->text);
    if (!local || local->type != IR_TYPE_MAYBE_BYTE_VIEW) {
      ir_mark_unsupported(ir, "direct backend maybe byte-view value requires a Maybe byte-view local", expr->line, expr->column, expr->left->text);
      return false;
    }
    IrValue *value = ir_new_value(ir, IR_VALUE_MAYBE_VALUE, IR_TYPE_BYTE_VIEW, expr->line, expr->column);
    value->local_index = local->index;
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
    *out = value;
    return true;
  }
  ir_mark_unsupported(ir, "direct backend byte views currently support string literals and slices", expr->line, expr->column, "unsupported byte view source");
  return false;
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
  if (call->type_args.len > 0) return &call->type_args;
  if (call->kind == EXPR_CALL && call->left && call->left->type_args.len > 0) return &call->left->type_args;
  return &call->type_args;
}

static char *ir_specialized_function_name(const Function *fun, const TypeArgVec *type_args) {
  return z_specialized_function_name(fun, type_args);
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
      inner->type = cast_type;
      *out = inner;
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
    case EXPR_IDENT: {
      const IrLocal *local = ir_function_find_local(fun, expr->text);
      if (!local) {
        ir_mark_unsupported(ir, "direct backend identifier is not a local", expr->line, expr->column, expr->text);
        return false;
      }
      if (local->type == IR_TYPE_BYTE_VIEW) return ir_lower_byte_view(program, ir, fun, expr, out);
      IrValue *value = ir_new_value(ir, IR_VALUE_LOCAL, local->type, expr->line, expr->column);
      value->local_index = local->index;
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
          if (!ir_type_is_value(value_type)) value_type = IR_TYPE_I32;
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
        if (local && local->is_record && ir_shape_field_info(program, local->shape_name, expr->text, &field_offset, &field_type)) {
          IrValue *value = ir_new_value(ir, IR_VALUE_FIELD_LOAD, field_type, expr->line, expr->column);
          value->local_index = local->index;
          value->field_offset = field_offset;
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
        IrValue *value = ir_new_value(ir, IR_VALUE_BYTE_VIEW_INDEX_LOAD, IR_TYPE_U8, expr->line, expr->column);
        value->left = view;
        value->index = index;
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
        if (!expr->args.items[0] || expr->args.items[0]->kind != EXPR_NUMBER ||
            !ir_parse_integer_literal(expr->args.items[0]->text, &number)) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.codec.encodedVarintLen currently requires an integer literal", expr->line, expr->column, "non-literal varint value");
          return false;
        }
        unsigned long long len = 1;
        while (number >= 128ull) {
          number >>= 7;
          len++;
        }
        free(callee_name);
        *out = ir_new_integer_literal_value(ir, IR_TYPE_USIZE, len, expr->line, expr->column);
        return true;
      }
      if (strncmp(callee_name, "std.parse.", strlen("std.parse.")) == 0 && expr->args.len == 1) {
        const unsigned char *bytes = NULL;
        size_t len = 0;
        if (!ir_string_literal_bytes(expr->args.items[0], &bytes, &len)) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.parse helpers currently require literal text", expr->line, expr->column, "non-literal parse input");
          return false;
        }
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
      if (strcmp(callee_name, "std.mem.fixedBufAlloc") == 0 &&
          expr->args.len == 1 &&
          ir_expr_is_mutable_byte_view_dest(fun, expr->args.items[0])) {
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
      if ((strcmp(callee_name, "std.time.ms") == 0 || strcmp(callee_name, "std.time.seconds") == 0) && expr->args.len == 1) {
        unsigned long long n = 0;
        if (expr->args.items[0] && expr->args.items[0]->kind == EXPR_NUMBER && ir_parse_integer_literal(expr->args.items[0]->text, &n)) {
          IrValue *value = ir_new_value(ir, IR_VALUE_INT, IR_TYPE_I64, expr->line, expr->column);
          value->int_value = n * (strcmp(callee_name, "std.time.ms") == 0 ? 1000000ull : 1000000000ull);
          free(callee_name);
          *out = value;
          return true;
        }
      }
      if ((strcmp(callee_name, "std.time.add") == 0 || strcmp(callee_name, "std.time.sub") == 0) && expr->args.len == 2) {
        IrValue *left = NULL;
        IrValue *right = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[0], &left) ||
            !ir_lower_expr(program, ir, fun, expr->args.items[1], &right)) {
          ir_free_value(left);
          ir_free_value(right);
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_BINARY, IR_TYPE_I64, expr->line, expr->column);
        value->binary_op = strcmp(callee_name, "std.time.add") == 0 ? IR_BIN_ADD : IR_BIN_SUB;
        value->left = left;
        value->right = right;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.time.asMsFloor") == 0 && expr->args.len == 1) {
        IrValue *duration = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[0], &duration)) {
          free(callee_name);
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_TIME_AS_MS, IR_TYPE_I32, expr->line, expr->column);
        value->left = duration;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.time.wallSeconds") == 0 && expr->args.len == 0) {
        IrValue *value = ir_new_value(ir, IR_VALUE_TIME_WALL_SECONDS, IR_TYPE_I64, expr->line, expr->column);
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.time.monotonic") == 0 && expr->args.len == 0) {
        IrValue *value = ir_new_value(ir, IR_VALUE_TIME_MONOTONIC, IR_TYPE_I64, expr->line, expr->column);
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
      if (strcmp(callee_name, "std.rand.nextU32") == 0 && expr->args.len == 1 &&
          expr->args.items[0] && expr->args.items[0]->kind == EXPR_BORROW &&
          expr->args.items[0]->mutable_borrow && expr->args.items[0]->left &&
          expr->args.items[0]->left->kind == EXPR_IDENT) {
        const IrLocal *rng = ir_function_find_local(fun, expr->args.items[0]->left->text);
        if (!rng || rng->type != IR_TYPE_U32 || !rng->is_mutable) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.rand.nextU32 expects a mutable RandSource local", expr->args.items[0]->line, expr->args.items[0]->column, "non-mutable RandSource");
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_RAND_NEXT_U32, IR_TYPE_U32, expr->line, expr->column);
        value->local_index = rng->index;
        free(callee_name);
        *out = value;
        return true;
      }
      if ((strcmp(callee_name, "std.rand.entropyU32") == 0 || strcmp(callee_name, "std.crypto.secureRandomU32") == 0) && expr->args.len == 0) {
        IrValue *value = ir_new_value(ir, IR_VALUE_RAND_ENTROPY_U32, IR_TYPE_U32, expr->line, expr->column);
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.proc.spawn") == 0 && expr->args.len == 1) {
        IrValue *value = ir_new_value(ir, IR_VALUE_INT, IR_TYPE_I32, expr->line, expr->column);
        value->int_value = 0;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.proc.exitCode") == 0 && expr->args.len == 1) {
        IrValue *status = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[0], &status)) {
          free(callee_name);
          return false;
        }
        free(callee_name);
        *out = status;
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
      if ((strcmp(callee_name, "std.json.validateBytes") == 0 ||
           strcmp(callee_name, "std.json.streamTokensBytes") == 0) &&
          expr->args.len == 1) {
        IrValue *view = NULL;
        if (!ir_lower_byte_view(program, ir, fun, expr->args.items[0], &view)) {
          free(callee_name);
          return false;
        }
        IrValueKind kind = strcmp(callee_name, "std.json.validateBytes") == 0 ? IR_VALUE_JSON_VALIDATE_BYTES : IR_VALUE_JSON_STREAM_TOKENS_BYTES;
        IrTypeKind type = kind == IR_VALUE_JSON_VALIDATE_BYTES ? IR_TYPE_BOOL : IR_TYPE_USIZE;
        IrValue *value = ir_new_value(ir, kind, type, expr->line, expr->column);
        value->left = view;
        if (ir->direct_runtime_helper_count < 1) ir->direct_runtime_helper_count = 1;
        if (ir->direct_host_runtime_import_count < 1) ir->direct_host_runtime_import_count = 1;
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.json.parseBytes") == 0 && expr->args.len == 2) {
        if (!expr->args.items[0] || expr->args.items[0]->kind != EXPR_IDENT) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.json.parseBytes expects a mutable FixedBufAlloc local", expr->args.items[0] ? expr->args.items[0]->line : expr->line, expr->args.items[0] ? expr->args.items[0]->column : expr->column, "non-local allocator");
          return false;
        }
        const IrLocal *alloc = ir_function_find_local(fun, expr->args.items[0]->text);
        if (!alloc || alloc->type != IR_TYPE_ALLOC || !alloc->is_mutable) {
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.json.parseBytes expects a mutable FixedBufAlloc local", expr->args.items[0]->line, expr->args.items[0]->column, "non-mutable allocator");
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
      if (strcmp(callee_name, "std.args.len") == 0 && expr->args.len == 0) {
        IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_LEN, IR_TYPE_USIZE, expr->line, expr->column);
        free(callee_name);
        *out = value;
        return true;
      }
      if (strcmp(callee_name, "std.args.get") == 0 && expr->args.len == 1) {
        IrValue *index = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[0], &index)) {
          free(callee_name);
          return false;
        }
        if (!ir_type_is_value(index->type)) {
          ir_free_value(index);
          free(callee_name);
          ir_mark_unsupported(ir, "direct backend std.args.get index must be an integer value", expr->args.items[0]->line, expr->args.items[0]->column, "non-integer index");
          return false;
        }
        IrValue *value = ir_new_value(ir, IR_VALUE_ARGS_GET, IR_TYPE_MAYBE_BYTE_VIEW, expr->line, expr->column);
        value->left = index;
        free(callee_name);
        *out = value;
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
      if ((strcmp(callee_name, "std.fs.write") == 0 || strcmp(callee_name, "std.fs.writeBytes") == 0) && expr->args.len == 2) {
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
        IrValue *value = ir_new_value(ir, maybe ? IR_VALUE_FS_WRITE_BYTES_PATH : IR_VALUE_FS_WRITE_PATH, maybe ? IR_TYPE_MAYBE_SCALAR : IR_TYPE_USIZE, expr->line, expr->column);
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
        if (!ir_expr_is_mutable_byte_view_dest(fun, expr->args.items[0])) {
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
          ir_expr_is_mutable_byte_view_dest(fun, expr->args.items[0])) {
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
      if ((strcmp(callee_name, "std.mem.vecLen") == 0 || strcmp(callee_name, "std.mem.vecCapacity") == 0) &&
          expr->args.len == 1) {
        const Expr *arg = expr->args.items[0];
        if (arg && arg->kind == EXPR_BORROW) arg = arg->left;
        if (arg && arg->kind == EXPR_IDENT) {
          const IrLocal *vec = ir_function_find_local(fun, arg->text);
          if (vec && vec->type == IR_TYPE_VEC) {
            IrValueKind kind = strcmp(callee_name, "std.mem.vecLen") == 0 ? IR_VALUE_VEC_LEN : IR_VALUE_VEC_CAPACITY;
            IrValue *value = ir_new_value(ir, kind, IR_TYPE_USIZE, expr->line, expr->column);
            value->local_index = vec->index;
            ir->direct_buffer_helper_count = ir->direct_buffer_helper_count < 3 ? 3 : ir->direct_buffer_helper_count;
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
        if (!ir_expr_is_mutable_byte_view_dest(fun, expr->args.items[0])) {
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
        if (!ir_expr_is_mutable_byte_view_dest(fun, expr->args.items[0])) {
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
      free(callee_name);
      if (!expr->left || expr->left->kind != EXPR_IDENT) {
        ir_mark_unsupported(ir, "direct backend calls currently support only same-file function identifiers", expr->line, expr->column, "non-identifier callee");
        return false;
      }
      unsigned callee_index = 0;
      const Function *callee = ir_find_source_function(program, expr->left->text, NULL);
      if (!callee) {
        ir_mark_unsupported(ir, "direct backend call target is not a same-file function", expr->line, expr->column, expr->left->text);
        return false;
      }
      const TypeArgVec *type_args = ir_call_type_args(expr);
      bool generic_call = callee->type_params.len > 0;
      char *specialized_name = NULL;
      const char *lookup_name = expr->left->text;
      if (generic_call) {
        if (!type_args || type_args->len != callee->type_params.len) {
          ir_mark_unsupported(ir, "direct backend generic calls require explicit type arguments", expr->line, expr->column, callee->name);
          return false;
        }
        specialized_name = ir_specialized_function_name(callee, type_args);
        lookup_name = specialized_name;
      } else if (type_args && type_args->len > 0) {
        ir_mark_unsupported(ir, "direct backend non-generic call cannot use type arguments", expr->line, expr->column, expr->left->text);
        return false;
      }
      if (!ir_find_function_index(ir, lookup_name, &callee_index)) {
        free(specialized_name);
        ir_mark_unsupported(ir, "direct backend call target is missing from MIR function table", expr->line, expr->column, expr->left->text);
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
      const char *return_type_text = generic_call ? ir_substitute_type_param(callee, type_args, callee->return_type) : callee->return_type;
      IrTypeKind type = ir_type_kind(return_type_text);
      if (callee->raises && !ir_type_is_direct_fallible_value(type)) {
        free(specialized_name);
        ir_mark_unsupported(ir, "direct backend fallible call return type is unsupported", expr->line, expr->column, callee->return_type);
        return false;
      }
      if (!callee->raises && type != IR_TYPE_VOID && !ir_type_is_direct_abi(type)) {
        free(specialized_name);
        ir_mark_unsupported(ir, "direct backend call return type is unsupported", expr->line, expr->column, callee->return_type);
        return false;
      }
      IrValue *value = ir_new_value(ir, IR_VALUE_CALL, callee->raises ? IR_TYPE_I64 : type, expr->line, expr->column);
      value->callee_index = callee_index;
      value->element_type = type;
      for (size_t i = 0; i < expr->args.len; i++) {
        const char *param_type_text = generic_call ? ir_substitute_type_param(callee, type_args, callee->params.items[i].type) : callee->params.items[i].type;
        IrTypeKind expected = ir_type_kind(param_type_text);
        if (!ir_type_is_direct_abi(expected)) {
          free(specialized_name);
          ir_free_value(value);
          ir_mark_unsupported(ir, "direct backend call parameter type is unsupported", callee->params.items[i].line, callee->params.items[i].column, callee->params.items[i].type);
          return false;
        }
        IrValue *arg = NULL;
        if (!ir_lower_expr(program, ir, fun, expr->args.items[i], &arg)) {
          free(specialized_name);
          ir_free_value(value);
          return false;
        }
        if (arg->type != expected) {
          ir_free_value(arg);
          ir_free_value(value);
          free(specialized_name);
          ir_mark_unsupported(ir, "direct backend call argument type does not match parameter", expr->args.items[i]->line, expr->args.items[i]->column, callee->params.items[i].type);
          return false;
        }
        ir_value_push_arg(ir, value, arg);
      }
      free(specialized_name);
      *out = value;
      return true;
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
    ir_mark_unsupported(ir, "direct backend indexed assignment supports only local fixed arrays", line, column, "non-local indexed assignment");
    return false;
  }
  const IrLocal *local = ir_function_find_local(mir_fun, target->left->text);
  if (!local || !local->is_array) {
    ir_mark_unsupported(ir, "direct backend indexed assignment target is not a fixed array local", line, column, target->left->text);
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
  if (!ir_type_is_value(index->type) || value->type != local->element_type) {
    ir_free_value(index);
    ir_free_value(value);
    ir_mark_unsupported(ir, "direct backend indexed assignment type does not match array element", line, column, local->name);
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
      if (!ir_lower_stmt_vec(program, ir, mir_fun, &arm->body, &nested_items, &nested_len, &nested_cap, saw_return)) {
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
    if (!ir_lower_stmt_vec(program, ir, mir_fun, &arm->body, &instr.then_instrs, &instr.then_len, &instr.then_cap, saw_return)) {
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

static bool ir_lower_stmt_to_vec(const Program *program, IrProgram *ir, IrFunction *mir_fun, const Stmt *stmt, IrInstr **out_items, size_t *out_len, size_t *out_cap, bool *saw_return) {
  if (stmt->kind == STMT_LET) {
    const IrLocal *local = ir_function_find_local(mir_fun, stmt->name);
    if (local && local->is_array) {
      return ir_lower_array_initializer(program, ir, mir_fun, local, stmt->expr, out_items, out_len, out_cap, stmt->line, stmt->column);
    }
    if (local && local->is_record) {
      return ir_lower_shape_initializer(program, ir, mir_fun, local, stmt->expr, out_items, out_len, out_cap, stmt->line, stmt->column);
    }
    IrValue *value = NULL;
    if (!local) return false;
    if (local->type == IR_TYPE_BYTE_VIEW && stmt->expr && stmt->expr->kind == EXPR_CHECK) {
      if (!ir_lower_expr(program, ir, mir_fun, stmt->expr, &value)) return false;
    } else if (local->type == IR_TYPE_BYTE_VIEW) {
      if (!ir_lower_byte_view(program, ir, mir_fun, stmt->expr, &value)) return false;
    } else if (!ir_lower_expr(program, ir, mir_fun, stmt->expr, &value)) {
      return false;
    }
    ir_instr_vec_push(ir, out_items, out_len, out_cap, (IrInstr){.kind = IR_INSTR_LOCAL_SET, .local_index = local->index, .value = value, .line = stmt->line, .column = stmt->column});
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
      if (local && local->is_record && ir_shape_field_info(program, local->shape_name, stmt->target->text, &field_offset, &field_type)) {
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
    if (local->type == IR_TYPE_BYTE_VIEW && stmt->expr && stmt->expr->kind == EXPR_CHECK) {
      if (!ir_lower_expr(program, ir, mir_fun, stmt->expr, &value)) return false;
    } else if (local->type == IR_TYPE_BYTE_VIEW) {
      if (!ir_lower_byte_view(program, ir, mir_fun, stmt->expr, &value)) return false;
    } else if (!stmt->expr && mir_fun->return_type == IR_TYPE_I32) {
      value = ir_new_value(ir, IR_VALUE_INT, IR_TYPE_I32, stmt->line, stmt->column);
      value->int_value = 0;
    } else if (!ir_lower_expr(program, ir, mir_fun, stmt->expr, &value)) {
      return false;
    }
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
    } else if (!stmt->expr && mir_fun->return_type == IR_TYPE_I32) {
      value = ir_new_value(ir, IR_VALUE_INT, IR_TYPE_I32, stmt->line, stmt->column);
      value->int_value = 0;
    } else if (!ir_lower_expr(program, ir, mir_fun, stmt->expr, &value)) {
      return false;
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
    if (!ir_lower_stmt_vec(program, ir, mir_fun, &stmt->then_body, &instr.then_instrs, &instr.then_len, &instr.then_cap, saw_return) ||
        !ir_lower_stmt_vec(program, ir, mir_fun, &stmt->else_body, &instr.else_instrs, &instr.else_len, &instr.else_cap, saw_return)) {
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
    if (!ir_lower_stmt_vec(program, ir, mir_fun, &stmt->then_body, &instr.then_instrs, &instr.then_len, &instr.then_cap, saw_return)) {
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
  ZBuf stable_id;
  zbuf_init(&stable_id);
  zbuf_append(&stable_id, stable_id_text ? stable_id_text : "main.");
  if (!stable_id_text) zbuf_append(&stable_id, source->name ? source->name : "");
  *fun = (IrFunction){
    .name = z_strdup(source->name),
    .stable_id = stable_id.data,
    .world_param_name = ir_is_hosted_world_main(source) && source->params.items[0].name ? z_strdup(source->params.items[0].name) : NULL,
    .return_type = ir_is_hosted_world_main(source) ? IR_TYPE_I32 : (source->raises ? IR_TYPE_I64 : ir_type_kind(source->return_type)),
    .value_return_type = ir_type_kind(source->return_type),
    .is_exported = source->export_c || ir_is_hosted_world_main(source),
    .raises = ir_is_hosted_world_main(source) ? false : source->raises,
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
    if (!ir_type_is_direct_abi(type)) {
      ir_mark_unsupported(ir, "direct backend parameter type is unsupported", param->line, param->column, param->type);
      return false;
    }
    ir_function_push_local(ir, mir_fun, param->name, type, true, false, false, NULL, IR_TYPE_UNSUPPORTED, 0, 0, 0, false, param->line, param->column);
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
        ir_mark_unsupported(ir, "direct backend local type is unsupported", stmt->line, stmt->column, stmt_type ? stmt_type : "inferred unknown");
        return false;
      }
      bool mutable_byte_view = stmt_type && strcmp(stmt_type, "MutSpan<u8>") == 0;
      ir_function_push_local(ir, mir_fun, stmt->name, type, false, false, false, NULL, IR_TYPE_UNSUPPORTED, 0, 0, 0, stmt->mutable_binding || mutable_byte_view, stmt->line, stmt->column);
    } else if (stmt->kind == STMT_IF) {
      if (!ir_collect_stmt_locals(program, ir, mir_fun, &stmt->then_body) || !ir_collect_stmt_locals(program, ir, mir_fun, &stmt->else_body)) return false;
    } else if (stmt->kind == STMT_WHILE) {
      if (!ir_collect_stmt_locals(program, ir, mir_fun, &stmt->then_body)) return false;
    }
  }
  return true;
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
  if (!hosted_world_main && !source->raises && return_type != IR_TYPE_VOID && !ir_type_is_direct_abi(return_type)) {
    ir_mark_unsupported(ir, "direct backend return type is unsupported", source->line, source->column, source->return_type);
    return false;
  }
  if (!ir_collect_function_locals(program, ir, mir_fun, source)) return false;
  bool saw_return = false;
  if (!ir_lower_stmt_vec(program, ir, mir_fun, &source->body, &mir_fun->instrs, &mir_fun->instr_len, &mir_fun->instr_cap, &saw_return)) return false;
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

static char *ir_specialize_type_text(const char *type, const Function *fun, const TypeArgVec *type_args) {
  return z_strdup(ir_substitute_type_param(fun, type_args, type));
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
  if (expr->kind == EXPR_CALL && expr->left && expr->left->kind == EXPR_IDENT) {
    const Function *callee = ir_find_source_function(program, expr->left->text, NULL);
    const TypeArgVec *type_args = ir_call_type_args(expr);
    if (callee && callee->type_params.len > 0 && type_args && type_args->len == callee->type_params.len) {
      char *specialized_name = NULL;
      ZSpecializationAddResult add_result = z_specialization_plan_add(plan, callee, type_args, &specialized_name);
      if (add_result == Z_SPECIALIZATION_ADD_LIMIT) {
        free(specialized_name);
        ir_mark_unsupported(ir, "direct backend generic specialization plan exceeded its instantiation limit", expr->line, expr->column, callee->name);
        return false;
      }
      if (add_result == Z_SPECIALIZATION_ADD_NAME_COLLISION) {
        free(specialized_name);
        ir_mark_unsupported(ir, "direct backend generic specialization name is ambiguous", expr->line, expr->column, callee->name);
        return false;
      }
      if (add_result == Z_SPECIALIZATION_ADD_ADDED) {
        if (ir_function_vec_has_name(functions, specialized_name)) {
          free(specialized_name);
          ir_mark_unsupported(ir, "direct backend generic specialization name collides with an existing function", expr->line, expr->column, callee->name);
          return false;
        }
        ir_push_specialized_function(functions, callee, type_args);
      }
      free(specialized_name);
    }
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
  if (program->choices.len > 0 || program->interfaces.len > 0 || program->aliases.len > 0 ||
      program->consts.len > 0) {
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
    if (!ir_collect_generic_specializations_from_stmt_vec(&direct_functions, &specialization_plan, ir, program, &direct_functions.items[i].body)) {
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

IrProgram z_lower_program_with_source(const Program *program, const SourceInput *input) {
  IrProgram ir = {0};
  for (size_t i = 0; i < program->c_imports.len; i++) {
    CImport *source = &program->c_imports.items[i];
    ir.program.c_imports.items = ir_grow_items(ir.program.c_imports.items, ir.program.c_imports.len, &ir.program.c_imports.cap, 4, sizeof(CImport));
    ir.program.c_imports.items[ir.program.c_imports.len++] = (CImport){
      .header = source->header ? z_strdup(source->header) : NULL,
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
  return z_lower_program_with_source(program, NULL);
}

void z_free_ir_program(IrProgram *program) {
  if (!program) return;
  for (size_t i = 0; i < program->function_len; i++) {
    IrFunction *fun = &program->functions[i];
    free(fun->name);
    free(fun->stable_id);
    free(fun->world_param_name);
    for (size_t local_index = 0; local_index < fun->local_len; local_index++) {
      free(fun->locals[local_index].name);
      free(fun->locals[local_index].shape_name);
    }
    ir_free_instrs(fun->instrs, fun->instr_len);
    free(fun->locals);
    free(fun->instrs);
  }
  free(program->functions);
  for (size_t i = 0; i < program->data_segment_len; i++) {
    free(program->data_segments[i].bytes);
  }
  free(program->data_segments);
  z_free_program(&program->program);
}
