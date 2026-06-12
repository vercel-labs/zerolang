#include "program_graph_patch.h"
#include "program_graph_patch_internal.h"
#include "program_graph_order.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void patch_free_text(char **slot) {
  if (!slot) return;
  free(*slot);
  *slot = NULL;
}

void z_graph_patch_replace_text(char **slot, const char *value) {
  if (!slot) return;
  free(*slot);
  *slot = z_strdup(value ? value : "");
}

void z_graph_patch_result_fail(ZProgramGraphPatchResult *result, const char *code, const char *message, const char *expected, const char *actual) {
  if (!result) return;
  result->ok = false;
  snprintf(result->code, sizeof(result->code), "%s", code ? code : "GPH000");
  snprintf(result->message, sizeof(result->message), "%s", message ? message : "program graph patch failed");
  z_graph_patch_replace_text(&result->expected, expected);
  z_graph_patch_replace_text(&result->actual, actual);
}

static void patch_format_fail(ZProgramGraphPatchResult *result, const char *code, const char *message, const char *expected, const char *actual, int line) {
  z_graph_patch_result_fail(result, code, message, expected, actual);
  if (!result) return;
  result->line = line > 0 ? line : 1;
  result->format_error = true;
}

const char *z_program_graph_patch_minimal_file_example(void) {
  return "zero-program-graph-patch v1\n"
         "replaceFunctionBody main\n"
         "  check world.out.write \"hello\\n\"\n"
         "end\n";
}

static void patch_op_fail(ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, const char *code, const char *message, const char *expected, const char *actual) {
  char *expected_copy = z_strdup(expected ? expected : "");
  char *actual_copy = z_strdup(actual ? actual : "");
  if (op) {
    op->ok = false;
    snprintf(op->code, sizeof(op->code), "%s", code ? code : "GPH000");
    snprintf(op->message, sizeof(op->message), "%s", message ? message : "program graph patch operation failed");
    z_graph_patch_replace_text(&op->expected, expected_copy);
    z_graph_patch_replace_text(&op->actual, actual_copy);
  }
  z_graph_patch_result_fail(result, code, message, expected_copy, actual_copy);
  free(expected_copy);
  free(actual_copy);
}

static bool patch_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static char *patch_read_file(const char *path, size_t *out_len, ZDiag *diag) {
  unsigned char *bytes = NULL;
  size_t len = 0;
  if (!z_read_binary_file(path, &bytes, &len, diag)) return NULL;
  if (out_len) *out_len = len;
  return (char *)bytes;
}

static char *patch_read_stdin(size_t *out_len, ZDiag *diag) {
  size_t cap = 4096;
  size_t len = 0;
  char *data = z_checked_malloc(cap);
  for (;;) {
    if (len == cap) {
      cap *= 2;
      data = z_checked_reallocarray(data, cap, 1);
    }
    size_t got = fread(data + len, 1, cap - len, stdin);
    len += got;
    if (got == 0) {
      if (ferror(stdin)) {
        free(data);
        if (diag) {
          diag->code = 2002;
          diag->path = "<stdin>";
          diag->line = 1;
          diag->column = 1;
          diag->length = 1;
          snprintf(diag->message, sizeof(diag->message), "failed to read function body rows from stdin");
          snprintf(diag->expected, sizeof(diag->expected), "body rows on stdin terminated by EOF");
          snprintf(diag->actual, sizeof(diag->actual), "stdin read error");
        }
        return NULL;
      }
      break;
    }
  }
  data = z_checked_reallocarray(data, len + 1, 1);
  data[len] = '\0';
  if (out_len) *out_len = len;
  return data;
}

char *z_graph_patch_read_body_source(const char *path, size_t *out_len, ZDiag *diag) {
  if (path && strcmp(path, "-") == 0) return patch_read_stdin(out_len, diag);
  return patch_read_file(path, out_len, diag);
}

static char *patch_trim(char *line) {
  while (*line && isspace((unsigned char)*line)) line++;
  char *end = line + strlen(line);
  while (end > line && isspace((unsigned char)*(end - 1))) *(--end) = '\0';
  return line;
}

static void patch_skip_spaces(const char **cursor) {
  while (cursor && *cursor && isspace((unsigned char)**cursor)) (*cursor)++;
}

static int patch_hex_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

static bool patch_parse_quoted(const char **cursor, char **out) {
  patch_skip_spaces(cursor);
  if (!cursor || !*cursor || **cursor != '"') return false;
  (*cursor)++;
  ZBuf buf;
  zbuf_init(&buf);
  while (**cursor && **cursor != '"') {
    unsigned char ch = (unsigned char)**cursor;
    if (ch != '\\') {
      zbuf_append_char(&buf, (char)ch);
      (*cursor)++;
      continue;
    }
    (*cursor)++;
    switch (**cursor) {
      case '\\': zbuf_append_char(&buf, '\\'); (*cursor)++; break;
      case '"': zbuf_append_char(&buf, '"'); (*cursor)++; break;
      case 'n': zbuf_append_char(&buf, '\n'); (*cursor)++; break;
      case 'r': zbuf_append_char(&buf, '\r'); (*cursor)++; break;
      case 't': zbuf_append_char(&buf, '\t'); (*cursor)++; break;
      case 'u': {
        if ((*cursor)[1] != '0' || (*cursor)[2] != '0' || !(*cursor)[3] || !(*cursor)[4]) {
          zbuf_free(&buf);
          return false;
        }
        int high = patch_hex_value((*cursor)[3]);
        int low = patch_hex_value((*cursor)[4]);
        if (high < 0 || low < 0 || (high == 0 && low == 0)) {
          zbuf_free(&buf);
          return false;
        }
        zbuf_append_char(&buf, (char)((high << 4) | low));
        *cursor += 5;
        break;
      }
      default:
        zbuf_free(&buf);
        return false;
    }
  }
  if (**cursor != '"') {
    zbuf_free(&buf);
    return false;
  }
  (*cursor)++;
  *out = buf.data ? buf.data : z_strdup("");
  return true;
}

static bool patch_parse_attr(const char **cursor, char **key, char **value) {
  patch_skip_spaces(cursor);
  if (!cursor || !*cursor || !**cursor) return false;
  const char *start = *cursor;
  while (**cursor && (isalnum((unsigned char)**cursor) || **cursor == '-' || **cursor == '_')) (*cursor)++;
  if (*cursor == start || **cursor != '=') return false;
  *key = z_strndup(start, (size_t)(*cursor - start));
  (*cursor)++;
  if (!patch_parse_quoted(cursor, value)) {
    free(*key);
    *key = NULL;
    return false;
  }
  return true;
}

ZProgramGraphPatchOpResult *z_graph_patch_push_operation(ZProgramGraphPatchResult *result) {
  if (result->operation_len == result->operation_cap) {
    size_t next = result->operation_cap ? result->operation_cap * 2 : 8;
    result->operations = z_checked_reallocarray(result->operations, next, sizeof(ZProgramGraphPatchOpResult));
    for (size_t i = result->operation_cap; i < next; i++) result->operations[i] = (ZProgramGraphPatchOpResult){0};
    result->operation_cap = next;
  }
  ZProgramGraphPatchOpResult *op = &result->operations[result->operation_len];
  *op = (ZProgramGraphPatchOpResult){.index = result->operation_len};
  result->operation_len++;
  return op;
}

static bool patch_parse_expect_graph_hash(const char *line, ZProgramGraphPatchResult *result) {
  const char *cursor = line + strlen("expect");
  patch_skip_spaces(&cursor);
  if (strncmp(cursor, "graphHash", strlen("graphHash")) != 0) return false;
  cursor += strlen("graphHash");
  char *hash = NULL;
  if (!patch_parse_quoted(&cursor, &hash)) return false;
  patch_skip_spaces(&cursor);
  if (*cursor || result->expected_graph_hash) {
    free(hash);
    return false;
  }
  z_graph_patch_replace_text(&result->expected_graph_hash, hash);
  free(hash);
  return true;
}

static bool patch_assign_attr(char **slot, char *value) {
  if (*slot) return false;
  *slot = value;
  return true;
}

static bool patch_parse_size_value(const char *text, size_t *out) {
  if (!text || !text[0]) return false;
  unsigned long long value = 0;
  for (const char *cursor = text; *cursor; cursor++) {
    if (!isdigit((unsigned char)*cursor)) return false;
    unsigned digit = (unsigned)(*cursor - '0');
    if (value > (ULLONG_MAX - digit) / 10) return false;
    value = value * 10 + digit;
  }
  *out = (size_t)value;
  return (unsigned long long)*out == value;
}

static bool patch_parse_int_value(const char *text, int *out) {
  size_t value = 0;
  if (!patch_parse_size_value(text, &value) || value > (size_t)INT_MAX) return false;
  *out = (int)value;
  return true;
}

static bool patch_parse_bool(const char *text, bool *out) {
  if (patch_text_eq(text, "true")) {
    *out = true;
    return true;
  }
  if (patch_text_eq(text, "false")) {
    *out = false;
    return true;
  }
  return false;
}

static bool patch_assign_bool_value(bool *slot, bool *has_slot, char *value) {
  bool parsed = false;
  if (*has_slot || !patch_parse_bool(value, &parsed)) return false;
  *slot = parsed;
  *has_slot = true;
  free(value);
  return true;
}

static bool patch_parse_set(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("set");

  const char *cursor = line + strlen("set");
  while (true) {
    patch_skip_spaces(&cursor);
    if (!*cursor) break;
    char *key = NULL;
    char *value = NULL;
    if (!patch_parse_attr(&cursor, &key, &value)) {
      free(key);
      free(value);
      patch_op_fail(result, op, "GPH001", "invalid set operation attribute", "key=\"value\"", line);
      return false;
    }
    bool ok = false;
    if (strcmp(key, "node") == 0) ok = patch_assign_attr(&op->node, value);
    else if (strcmp(key, "field") == 0) ok = patch_assign_attr(&op->field, value);
    else if (strcmp(key, "expect") == 0) {
      ok = !op->has_expected;
      if (ok) {
        op->expected = value;
        op->has_expected = true;
      }
    } else if (strcmp(key, "value") == 0) ok = patch_assign_attr(&op->value, value);
    else {
      ok = false;
    }
    free(key);
    if (!ok) {
      free(value);
      patch_op_fail(result, op, "GPH001", "duplicate or unknown set operation attribute", "node, field, expect, or value", line);
      return false;
    }
  }
  if (!op->node || !op->field || !op->value) {
    patch_op_fail(result, op, "GPH001", "set operation is missing required attributes", "node, field, and value", line);
    return false;
  }
  return true;
}

static bool patch_assign_order(ZProgramGraphPatchOpResult *op, char *value) {
  if (op->has_order || !patch_parse_size_value(value, &op->order)) return false;
  op->has_order = true;
  free(value);
  return true;
}

static bool patch_assign_line_value(ZProgramGraphPatchOpResult *op, char *value) {
  if (op->has_line_value || !patch_parse_int_value(value, &op->line_value)) return false;
  op->has_line_value = true;
  free(value);
  return true;
}

static bool patch_assign_column_value(ZProgramGraphPatchOpResult *op, char *value) {
  if (op->has_column_value || !patch_parse_int_value(value, &op->column_value)) return false;
  op->has_column_value = true;
  free(value);
  return true;
}

static bool patch_parse_structural_attrs(const char *line, const char *verb, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  const char *cursor = line + strlen(verb);
  while (true) {
    patch_skip_spaces(&cursor);
    if (!*cursor) break;
    char *key = NULL;
    char *value = NULL;
    if (!patch_parse_attr(&cursor, &key, &value)) {
      free(key);
      free(value);
      patch_op_fail(result, op, "GPH001", "invalid patch operation attribute", "key=\"value\"", line);
      return false;
    }
    bool ok = false;
    if (strcmp(key, "node") == 0) ok = patch_assign_attr(&op->node, value);
    else if (strcmp(key, "parent") == 0) ok = patch_assign_attr(&op->parent, value);
    else if (strcmp(key, "from") == 0) ok = patch_assign_attr(&op->from, value);
    else if (strcmp(key, "to") == 0) ok = patch_assign_attr(&op->to, value);
    else if (strcmp(key, "edge") == 0) ok = patch_assign_attr(&op->edge, value);
    else if (strcmp(key, "kind") == 0) ok = patch_assign_attr(&op->kind, value);
    else if (strcmp(key, "target") == 0) ok = patch_assign_attr(&op->target, value);
    else if (strcmp(key, "expect") == 0) {
      ok = !op->has_expected;
      if (ok) {
        op->expected = value;
        op->has_expected = true;
      }
    } else if (strcmp(key, "value") == 0) ok = patch_assign_attr(&op->value, value);
    else if (strcmp(key, "name") == 0) ok = patch_assign_attr(&op->name, value);
    else if (strcmp(key, "type") == 0) ok = patch_assign_attr(&op->type, value);
    else if (strcmp(key, "ret") == 0) ok = patch_assign_attr(&op->type, value);
    else if (strcmp(key, "path") == 0) ok = patch_assign_attr(&op->path, value);
    else if (strcmp(key, "fn") == 0) ok = patch_assign_attr(&op->function, value);
    else if (strcmp(key, "function") == 0) ok = patch_assign_attr(&op->function, value);
    else if (strcmp(key, "left") == 0) ok = patch_assign_attr(&op->left, value);
    else if (strcmp(key, "receiver") == 0) ok = patch_assign_attr(&op->left, value);
    else if (strcmp(key, "world") == 0) ok = patch_assign_attr(&op->left, value);
    else if (strcmp(key, "right") == 0) ok = patch_assign_attr(&op->right, value);
    else if (strcmp(key, "arg0") == 0) ok = patch_assign_attr(&op->arg0, value);
    else if (strcmp(key, "arg1") == 0) ok = patch_assign_attr(&op->arg1, value);
    else if (strcmp(key, "call") == 0) ok = patch_assign_attr(&op->call, value);
    else if (strcmp(key, "operator") == 0) ok = patch_assign_attr(&op->call, value);
    else if (strcmp(key, "prefix") == 0) ok = patch_assign_attr(&op->value, value);
    else if (strcmp(key, "fallback") == 0) ok = patch_assign_attr(&op->right, value);
    else if (strcmp(key, "want") == 0) ok = patch_assign_attr(&op->value, value);
    else if (strcmp(key, "text") == 0) ok = patch_assign_attr(&op->value, value);
    else if (strcmp(key, "message") == 0) ok = patch_assign_attr(&op->value, value);
    else if (strcmp(key, "order") == 0) ok = patch_assign_order(op, value);
    else if (strcmp(key, "line") == 0) ok = patch_assign_line_value(op, value);
    else if (strcmp(key, "column") == 0) ok = patch_assign_column_value(op, value);
    else if (strcmp(key, "public") == 0) ok = patch_assign_bool_value(&op->public_value, &op->has_public_value, value);
    else if (strcmp(key, "mutable") == 0) ok = patch_assign_bool_value(&op->mutable_value, &op->has_mutable_value, value);
    else if (strcmp(key, "static") == 0) ok = patch_assign_bool_value(&op->static_value, &op->has_static_value, value);
    else if (strcmp(key, "fallible") == 0) ok = patch_assign_bool_value(&op->fallible_value, &op->has_fallible_value, value);
    else if (strcmp(key, "exportC") == 0) ok = patch_assign_bool_value(&op->export_c_value, &op->has_export_c_value, value);
    else ok = false;
    free(key);
    if (!ok) {
      free(value);
      patch_op_fail(result, op, "GPH001", "duplicate or unknown patch operation attribute", "supported operation attributes", line);
      return false;
    }
  }
  return true;
}

static bool patch_has_node_payload(const ZProgramGraphPatchOpResult *op) {
  return (op->name || op->type || op->value || op->path || op->has_line_value || op->has_column_value ||
          op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
          op->has_export_c_value);
}

static bool patch_has_authoring_payload(const ZProgramGraphPatchOpResult *op) {
  return op && (op->function || op->left || op->right || op->arg0 || op->arg1 || op->call);
}

static bool patch_reject_attrs(
  ZProgramGraphPatchOpResult *op,
  ZProgramGraphPatchResult *result,
  const char *line,
  bool allow_node,
  bool allow_parent,
  bool allow_from_to,
  bool allow_edge,
  bool allow_kind,
  bool allow_target,
  bool allow_order,
  bool allow_expected,
  bool allow_payload,
  bool allow_authoring_payload
) {
  if ((!allow_node && op->node) ||
      (!allow_parent && op->parent) ||
      (!allow_from_to && (op->from || op->to)) ||
      (!allow_edge && op->edge) ||
      (!allow_kind && op->kind) ||
      (!allow_target && op->target) ||
      (!allow_order && op->has_order) ||
      (!allow_expected && op->has_expected) ||
      (!allow_payload && patch_has_node_payload(op)) ||
      (!allow_authoring_payload && patch_has_authoring_payload(op))) {
    patch_op_fail(result, op, "GPH001", "patch operation has unsupported attributes", "attributes supported by the operation", line);
    return false;
  }
  return true;
}

static bool patch_parse_insert(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("insert");
  if (!patch_parse_structural_attrs(line, "insert", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, true, false, true, true, false, true, false, true, false)) return false;
  if (!op->has_order) op->has_order = true;
  if (!op->node || !op->kind || !op->parent || !op->edge) {
    patch_op_fail(result, op, "GPH001", "insert operation is missing required attributes", "node, kind, parent, and edge", line);
    return false;
  }
  return true;
}

static bool patch_parse_insert_edge(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("insertEdge");
  if (!patch_parse_structural_attrs(line, "insertEdge", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, false, false, true, true, false, true, true, false, false, false)) return false;
  if (!op->has_order) op->has_order = true;
  if (!op->from || !op->to || !op->edge || !op->target) {
    patch_op_fail(result, op, "GPH001", "insertEdge operation is missing required attributes", "from, to, edge, and target", line);
    return false;
  }
  return true;
}

static bool patch_parse_replace(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("replace");
  if (!patch_parse_structural_attrs(line, "replace", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, true, false, false, true, true, false)) return false;
  if (!op->node) {
    patch_op_fail(result, op, "GPH001", "replace operation is missing required attributes", "node", line);
    return false;
  }
  return true;
}

static bool patch_parse_delete(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("delete");
  if (!patch_parse_structural_attrs(line, "delete", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, false, false)) return false;
  if (!op->node) {
    patch_op_fail(result, op, "GPH001", "delete operation is missing required attributes", "node", line);
    return false;
  }
  return true;
}

static bool patch_parse_rename(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("rename");
  if (!patch_parse_structural_attrs(line, "rename", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, true, false)) return false;
  if (op->name || op->type || op->path || op->has_line_value || op->has_column_value ||
      op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
      op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "rename operation has unsupported attributes", "node, expect, and value", line);
    return false;
  }
  if (!op->node || !op->value) {
    patch_op_fail(result, op, "GPH001", "rename operation is missing required attributes", "node and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_add_function(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("addFunction");
  if (!patch_parse_structural_attrs(line, "addFunction", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, false, false, false, false, false, false, false, false, true, false)) return false;
  if (!op->name) {
    patch_op_fail(result, op, "GPH001", "addFunction operation is missing required attributes", "name", line);
    return false;
  }
  return true;
}

static bool patch_parse_add_main(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("addMain");
  if (!patch_parse_structural_attrs(line, "addMain", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, false, false, false, false, false, false, false, false, true, false)) return false;
  return true;
}

static bool patch_parse_add_param(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("addParam");
  if (!patch_parse_structural_attrs(line, "addParam", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, false, false, false, false, false, false, false, false, true, true)) return false;
  if (!op->function || !op->name || !op->type) {
    patch_op_fail(result, op, "GPH001", "addParam operation is missing required attributes", "fn, name, and type", line);
    return false;
  }
  return true;
}

static bool patch_parse_add_return_binary(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("addReturnBinary");
  if (!patch_parse_structural_attrs(line, "addReturnBinary", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, false, false, false, false, false, false, false, false, true, true)) return false;
  if (!op->function || !op->name || !op->left || !op->right) {
    patch_op_fail(result, op, "GPH001", "addReturnBinary operation is missing required attributes", "fn, name, left, and right", line);
    return false;
  }
  return true;
}

static bool patch_parse_add_let_literal(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("addLetLiteral");
  if (!patch_parse_structural_attrs(line, "addLetLiteral", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, false, false, false, false, false, false, false, false, true, true)) return false;
  if (!op->function || !op->name || !op->type || !op->value) {
    patch_op_fail(result, op, "GPH001", "addLetLiteral operation is missing required attributes", "fn, name, type, and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_add_let_binary(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("addLetBinary");
  if (!patch_parse_structural_attrs(line, "addLetBinary", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, false, false, false, false, false, false, false, false, true, true)) return false;
  if (!op->function || !op->name || !op->type || !op->call || !op->left || !op->right) {
    patch_op_fail(result, op, "GPH001", "addLetBinary operation is missing required attributes", "fn, name, type, operator, left, and right", line);
    return false;
  }
  return true;
}

static bool patch_parse_add_return_value(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("addReturnValue");
  if (!patch_parse_structural_attrs(line, "addReturnValue", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, false, false, false, false, false, false, false, false, true, true)) return false;
  if (!op->function || !op->value) {
    patch_op_fail(result, op, "GPH001", "addReturnValue operation is missing required attributes", "fn and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_add_check_write_value(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("addCheckWriteValue");
  if (!patch_parse_structural_attrs(line, "addCheckWriteValue", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, false, false, false, false, false, false, false, false, true, true)) return false;
  if (!op->function || !op->value) {
    patch_op_fail(result, op, "GPH001", "addCheckWriteValue operation is missing required attributes", "fn and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_add_check_write(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("addCheckWrite");
  if (!patch_parse_structural_attrs(line, "addCheckWrite", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, false, false, false, false, false, false, false, false, true, true)) return false;
  if (!op->function || !op->value) {
    patch_op_fail(result, op, "GPH001", "addCheckWrite operation is missing required attributes", "fn and text", line);
    return false;
  }
  return true;
}

static bool patch_parse_add_test(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("addTest");
  if (!patch_parse_structural_attrs(line, "addTest", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, false, false, false, false, false, false, false, true, true, true)) return false;
  if (!op->name || !op->call || !op->arg0 || !op->arg1 || (!op->value && !op->has_expected)) {
    patch_op_fail(result, op, "GPH001", "addTest operation is missing required attributes", "name, call, arg0, arg1, and expect", line);
    return false;
  }
  return true;
}

static bool patch_parse_replace_body_target(const char *line, int line_number, const char *body, bool block, ZProgramGraphPatchResult *result) {
  const char *operation = block ? "replaceBlockBody" : "replaceFunctionBody";
  const char *usage = block ? "replaceBlockBody #block_id" : "replaceFunctionBody main";
  const char *cursor = line + strlen(operation);
  patch_skip_spaces(&cursor);
  if (!*cursor) { z_graph_patch_result_fail(result, "GPH001", block ? "replaceBlockBody is missing a block node id" : "replaceFunctionBody is missing a function name", usage, line); return false; }
  const char *start = cursor;
  while (*cursor && !isspace((unsigned char)*cursor)) cursor++;
  char *target = z_strndup(start, (size_t)(cursor - start));
  patch_skip_spaces(&cursor);
  if (*cursor) { z_graph_patch_result_fail(result, "GPH001", block ? "replaceBlockBody has trailing header text" : "replaceFunctionBody has trailing header text", usage, line); free(target); return false; }
  if (block && target[0] != '#') { z_graph_patch_result_fail(result, "GPH003", "replaceBlockBody target must be a graph node id", "#block_id", target); free(target); return false; }
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup(operation);
  if (block) op->node = target;
  else op->function = target;
  op->value = z_strdup(body ? body : "");
  return true;
}

static bool patch_parse_replace_body_rows(char *header, int *line_number, char **cursor, bool block, ZProgramGraphPatchResult *result) {
  ZBuf body;
  zbuf_init(&body);
  bool ended = false;
  int header_line = *line_number;
  while (**cursor) {
    (*line_number)++;
    char *body_line = *cursor;
    char *body_end = strchr(*cursor, '\n');
    if (body_end) {
      *body_end = '\0';
      *cursor = body_end + 1;
    } else {
      *cursor += strlen(*cursor);
    }
    char *body_trimmed = patch_trim(body_line);
    if (strcmp(body_trimmed, "end") == 0) {
      ended = true;
      break;
    }
    char *append_line = body_line;
    if (append_line[0] == ' ' && append_line[1] == ' ') append_line += 2;
    else if (append_line[0] == '\t') append_line += 1;
    zbuf_append(&body, append_line);
    zbuf_append_char(&body, '\n');
  }
  if (!ended) {
    zbuf_free(&body);
    patch_format_fail(result, "GPH001", "body replacement is missing end marker; every body replacement closes with a line containing only `end`", block ? "replaceBlockBody ... end" : "replaceFunctionBody ... end", header, header_line);
    return false;
  }
  bool parsed_body = patch_parse_replace_body_target(header, header_line, body.data ? body.data : "", block, result);
  zbuf_free(&body);
  return parsed_body;
}

static bool patch_parse_text(char *text, ZProgramGraphPatchResult *result) {
  bool saw_header = false;
  int line_number = 0;
  char *cursor = text ? text : "";
  if (strncmp(cursor, "\xEF\xBB\xBF", 3) == 0) cursor += 3;
  while (*cursor) {
    line_number++;
    char *line = cursor;
    char *end = strchr(cursor, '\n');
    if (end) {
      *end = '\0';
      cursor = end + 1;
    } else {
      cursor += strlen(cursor);
    }
    char *trimmed = patch_trim(line);
    if (!*trimmed || *trimmed == '#') continue;
    if (!saw_header) {
      if (strcmp(trimmed, "zero-program-graph-patch v1") != 0) {
        patch_format_fail(result, "GPH001", "unknown program graph patch schema; the first non-comment line of a patch file is the `zero-program-graph-patch v1` header", "zero-program-graph-patch v1", trimmed, line_number);
        return false;
      }
      saw_header = true;
      continue;
    }
    if (strncmp(trimmed, "expect", strlen("expect")) == 0 && isspace((unsigned char)trimmed[strlen("expect")])) {
      if (!patch_parse_expect_graph_hash(trimmed, result)) {
        z_graph_patch_result_fail(result, "GPH001", "invalid graph hash precondition", "expect graphHash \"graph:<hash>\"", trimmed);
        return false;
      }
    } else if (strncmp(trimmed, "set", strlen("set")) == 0 && isspace((unsigned char)trimmed[strlen("set")])) {
      if (!patch_parse_set(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "insertEdge", strlen("insertEdge")) == 0 && isspace((unsigned char)trimmed[strlen("insertEdge")])) {
      if (!patch_parse_insert_edge(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "insert", strlen("insert")) == 0 && isspace((unsigned char)trimmed[strlen("insert")])) {
      if (!patch_parse_insert(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "replace", strlen("replace")) == 0 && isspace((unsigned char)trimmed[strlen("replace")])) {
      if (!patch_parse_replace(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "delete", strlen("delete")) == 0 && isspace((unsigned char)trimmed[strlen("delete")])) {
      if (!patch_parse_delete(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "rename", strlen("rename")) == 0 && isspace((unsigned char)trimmed[strlen("rename")])) {
      if (!patch_parse_rename(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "addFunction", strlen("addFunction")) == 0 && isspace((unsigned char)trimmed[strlen("addFunction")])) {
      if (!patch_parse_add_function(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "addMain", strlen("addMain")) == 0 && isspace((unsigned char)trimmed[strlen("addMain")])) {
      if (!patch_parse_add_main(trimmed, line_number, result)) return false;
    } else if (strcmp(trimmed, "addMain") == 0) {
      if (!patch_parse_add_main(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "addParam", strlen("addParam")) == 0 && isspace((unsigned char)trimmed[strlen("addParam")])) {
      if (!patch_parse_add_param(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "addReturnBinary", strlen("addReturnBinary")) == 0 && isspace((unsigned char)trimmed[strlen("addReturnBinary")])) {
      if (!patch_parse_add_return_binary(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "addLetLiteral", strlen("addLetLiteral")) == 0 && isspace((unsigned char)trimmed[strlen("addLetLiteral")])) {
      if (!patch_parse_add_let_literal(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "addLetBinary", strlen("addLetBinary")) == 0 && isspace((unsigned char)trimmed[strlen("addLetBinary")])) {
      if (!patch_parse_add_let_binary(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "addReturnValue", strlen("addReturnValue")) == 0 && isspace((unsigned char)trimmed[strlen("addReturnValue")])) {
      if (!patch_parse_add_return_value(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "addCheckWriteValue", strlen("addCheckWriteValue")) == 0 && isspace((unsigned char)trimmed[strlen("addCheckWriteValue")])) {
      if (!patch_parse_add_check_write_value(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "addCheckWrite", strlen("addCheckWrite")) == 0 && isspace((unsigned char)trimmed[strlen("addCheckWrite")])) {
      if (!patch_parse_add_check_write(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "addTest", strlen("addTest")) == 0 && isspace((unsigned char)trimmed[strlen("addTest")])) {
      if (!patch_parse_add_test(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "replaceFunctionBody", strlen("replaceFunctionBody")) == 0 && isspace((unsigned char)trimmed[strlen("replaceFunctionBody")])) {
      if (!patch_parse_replace_body_rows(trimmed, &line_number, &cursor, false, result)) return false;
    } else if (strncmp(trimmed, "replaceBlockBody", strlen("replaceBlockBody")) == 0 && isspace((unsigned char)trimmed[strlen("replaceBlockBody")])) {
      if (!patch_parse_replace_body_rows(trimmed, &line_number, &cursor, true, result)) return false;
    } else {
      patch_format_fail(result, "GPH001", "unknown program graph patch operation; run `zero patch --op help` for working examples of every operation", "expect, set, insert, insertEdge, replace, delete, rename, addFunction, addMain, addParam, addReturnBinary, addLetLiteral, addLetBinary, addReturnValue, addCheckWriteValue, addCheckWrite, addTest, replaceFunctionBody, or replaceBlockBody", trimmed, line_number);
      return false;
    }
  }
  if (!saw_header) {
    patch_format_fail(result, "GPH001", "program graph patch is empty", "zero-program-graph-patch v1", "", 1);
    return false;
  }
  return true;
}
bool z_graph_patch_apply_operations(ZProgramGraph *graph, ZProgramGraphPatchResult *result) {
  if (result->expected_graph_hash && !patch_text_eq(result->expected_graph_hash, result->actual_graph_hash)) {
    z_graph_patch_result_fail(result, "GPH002", "graph hash precondition failed", result->expected_graph_hash, result->actual_graph_hash);
    return false;
  }
  for (size_t i = 0; i < result->operation_len; i++) {
    if (!z_program_graph_patch_apply_operation(graph, result, &result->operations[i])) return false;
    z_program_graph_finalize_identities(graph);
  }
  z_program_graph_compact_ordered_edges(graph); z_program_graph_finalize_identities(graph);
  z_graph_patch_replace_text(&result->actual_graph_hash, graph->graph_hash);
  ZProgramGraphValidation validation = {0};
  if (!z_program_graph_validate(graph, &validation)) {
    z_graph_patch_result_fail(result, "GPH006", validation.message, "shape-valid ProgramGraph", validation.code);
    return false;
  }
  result->ok = true; return true;
}
bool z_program_graph_apply_patch_text(const char *label, const char *text, size_t text_len, ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZDiag *diag) {
  if (!result) return false;
  *result = (ZProgramGraphPatchResult){0};
  result->actual_graph_hash = z_strdup(graph && graph->graph_hash ? graph->graph_hash : "");
  if (!text) { text = ""; text_len = 0; }
  if (memchr(text, '\0', text_len)) {
    z_graph_patch_result_fail(result, "GPH001", "program graph patch contains NUL byte", "text without NUL bytes", "NUL byte");
    if (diag && label) diag->path = label;
    return false;
  }
  char *copy = z_strndup(text, text_len);
  bool parsed = patch_parse_text(copy, result); free(copy); if (!parsed) return false;
  return z_graph_patch_apply_operations(graph, result);
}
bool z_program_graph_apply_replace_fn_body_file(const char *function_name, const char *path, const char *expect_graph_hash, ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZDiag *diag) {
  if (!result) return false;
  *result = (ZProgramGraphPatchResult){0};
  result->actual_graph_hash = z_strdup(graph && graph->graph_hash ? graph->graph_hash : "");
  size_t text_len = 0;
  bool from_stdin = path && strcmp(path, "-") == 0;
  char *text = z_graph_patch_read_body_source(path, &text_len, diag);
  if (!text) return false;
  if (memchr(text, '\0', text_len)) {
    free(text);
    z_graph_patch_result_fail(result, "GPH001", "function body file contains NUL byte", "body rows without NUL bytes", "NUL byte");
    return false;
  }
  bool blank = true;
  for (size_t i = 0; blank && i < text_len; i++) blank = isspace((unsigned char)text[i]) != 0;
  if (blank) {
    free(text);
    z_graph_patch_result_fail(result, "GPH001", "function body file is empty", "body rows in zero view syntax", from_stdin ? "<stdin>" : path);
    return false;
  }
  if (expect_graph_hash) z_graph_patch_replace_text(&result->expected_graph_hash, expect_graph_hash);
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = 1;
  op->op = z_strdup("replaceFunctionBody");
  op->function = z_strdup(function_name ? function_name : "");
  op->value = z_strndup(text, text_len);
  free(text);
  return z_graph_patch_apply_operations(graph, result);
}
bool z_program_graph_apply_patch_file(const char *path, ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZDiag *diag) {
  if (!result) return false;
  size_t text_len = 0;
  char *text = patch_read_file(path, &text_len, diag);
  if (!text) { *result = (ZProgramGraphPatchResult){0}; return false; }
  bool ok = z_program_graph_apply_patch_text(path, text, text_len, graph, result, diag);
  free(text);
  return ok;
}
void z_program_graph_patch_result_free(ZProgramGraphPatchResult *result) {
  if (!result) return;
  patch_free_text(&result->expected);
  patch_free_text(&result->actual);
  patch_free_text(&result->expected_graph_hash);
  patch_free_text(&result->actual_graph_hash);
  for (size_t i = 0; i < result->operation_len; i++) {
    ZProgramGraphPatchOpResult *op = &result->operations[i];
    patch_free_text(&op->op);
    patch_free_text(&op->node);
    patch_free_text(&op->parent);
    patch_free_text(&op->from);
    patch_free_text(&op->to);
    patch_free_text(&op->edge);
    patch_free_text(&op->kind);
    patch_free_text(&op->target);
    patch_free_text(&op->field);
    patch_free_text(&op->expected);
    patch_free_text(&op->actual);
    patch_free_text(&op->value);
    patch_free_text(&op->name);
    patch_free_text(&op->type);
    patch_free_text(&op->path);
    patch_free_text(&op->function);
    patch_free_text(&op->left);
    patch_free_text(&op->right);
    patch_free_text(&op->arg0);
    patch_free_text(&op->arg1);
    patch_free_text(&op->call);
  }
  free(result->operations);
  *result = (ZProgramGraphPatchResult){0};
}
