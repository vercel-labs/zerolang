#include "program_graph_patch.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void patch_free_text(char **slot) {
  if (!slot) return;
  free(*slot);
  *slot = NULL;
}

static void patch_replace_text(char **slot, const char *value) {
  if (!slot) return;
  free(*slot);
  *slot = z_strdup(value ? value : "");
}

static void patch_result_fail(ZProgramGraphPatchResult *result, const char *code, const char *message, const char *expected, const char *actual) {
  if (!result) return;
  result->ok = false;
  snprintf(result->code, sizeof(result->code), "%s", code ? code : "GPH000");
  snprintf(result->message, sizeof(result->message), "%s", message ? message : "program graph patch failed");
  patch_replace_text(&result->expected, expected);
  patch_replace_text(&result->actual, actual);
}

static void patch_op_fail(ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, const char *code, const char *message, const char *expected, const char *actual) {
  char *expected_copy = z_strdup(expected ? expected : "");
  char *actual_copy = z_strdup(actual ? actual : "");
  if (op) {
    op->ok = false;
    snprintf(op->code, sizeof(op->code), "%s", code ? code : "GPH000");
    snprintf(op->message, sizeof(op->message), "%s", message ? message : "program graph patch operation failed");
    patch_replace_text(&op->expected, expected_copy);
    patch_replace_text(&op->actual, actual_copy);
  }
  patch_result_fail(result, code, message, expected_copy, actual_copy);
  free(expected_copy);
  free(actual_copy);
}

static bool patch_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool patch_io_fail(ZDiag *diag, const char *path, const char *action) {
  if (diag) {
    diag->code = 1;
    diag->path = path;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "failed to %s '%s': %s", action, path ? path : "<patch>", strerror(errno));
  }
  return false;
}

static char *patch_read_file(const char *path, size_t *out_len, ZDiag *diag) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    patch_io_fail(diag, path, "read");
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    patch_io_fail(diag, path, "read");
    fclose(file);
    return NULL;
  }
  long size = ftell(file);
  if (size < 0) {
    patch_io_fail(diag, path, "read");
    fclose(file);
    return NULL;
  }
  rewind(file);
  char *data = z_checked_malloc((size_t)size + 1);
  size_t read = fread(data, 1, (size_t)size, file);
  if (read != (size_t)size) {
    if (!ferror(file)) errno = EIO;
    patch_io_fail(diag, path, "read");
    fclose(file);
    free(data);
    return NULL;
  }
  fclose(file);
  data[read] = '\0';
  if (out_len) *out_len = read;
  return data;
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

static ZProgramGraphPatchOpResult *patch_push_operation(ZProgramGraphPatchResult *result) {
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
  patch_replace_text(&result->expected_graph_hash, hash);
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
  if (strcmp(text ? text : "", "true") == 0) {
    *out = true;
    return true;
  }
  if (strcmp(text ? text : "", "false") == 0) {
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
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
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
    else if (strcmp(key, "params") == 0) ok = patch_assign_attr(&op->params, value);
    else if (strcmp(key, "path") == 0) ok = patch_assign_attr(&op->path, value);
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
          op->params || op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
          op->has_export_c_value);
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
  bool allow_payload
) {
  if ((!allow_node && op->node) ||
      (!allow_parent && op->parent) ||
      (!allow_from_to && (op->from || op->to)) ||
      (!allow_edge && op->edge) ||
      (!allow_kind && op->kind) ||
      (!allow_target && op->target) ||
      (!allow_order && op->has_order) ||
      (!allow_expected && op->has_expected) ||
      (!allow_payload && patch_has_node_payload(op))) {
    patch_op_fail(result, op, "GPH001", "patch operation has unsupported attributes", "attributes supported by the operation", line);
    return false;
  }
  return true;
}

static bool patch_parse_insert(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("insert");
  if (!patch_parse_structural_attrs(line, "insert", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, true, false, true, true, false, true, false, true)) return false;
  if (!op->node || !op->kind || !op->parent || !op->edge || !op->has_order) {
    patch_op_fail(result, op, "GPH001", "insert operation is missing required attributes", "node, kind, parent, edge, and order", line);
    return false;
  }
  return true;
}

static bool patch_parse_insert_edge(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("insertEdge");
  if (!patch_parse_structural_attrs(line, "insertEdge", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, false, false, true, true, false, true, true, false, false)) return false;
  if (!op->from || !op->to || !op->edge || !op->target || !op->has_order) {
    patch_op_fail(result, op, "GPH001", "insertEdge operation is missing required attributes", "from, to, edge, target, and order", line);
    return false;
  }
  return true;
}

static bool patch_parse_replace(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("replace");
  if (!patch_parse_structural_attrs(line, "replace", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, true, false, false, true, true)) return false;
  if (!op->node) {
    patch_op_fail(result, op, "GPH001", "replace operation is missing required attributes", "node", line);
    return false;
  }
  return true;
}

static bool patch_parse_delete(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("delete");
  if (!patch_parse_structural_attrs(line, "delete", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, false)) return false;
  if (!op->node) {
    patch_op_fail(result, op, "GPH001", "delete operation is missing required attributes", "node", line);
    return false;
  }
  return true;
}

static bool patch_parse_rename(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("rename");
  if (!patch_parse_structural_attrs(line, "rename", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, true)) return false;
  if (op->name || op->type || op->params || op->path || op->has_line_value || op->has_column_value ||
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

static bool patch_parse_rename_symbol(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("renameSymbol");
  if (!patch_parse_structural_attrs(line, "renameSymbol", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, true)) return false;
  if (op->name || op->type || op->params || op->path || op->has_line_value || op->has_column_value ||
      op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
      op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "renameSymbol operation has unsupported attributes", "node, expect, and value", line);
    return false;
  }
  if (!op->node || !op->value) {
    patch_op_fail(result, op, "GPH001", "renameSymbol operation is missing required attributes", "node and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_replace_callee(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("replaceCallee");
  if (!patch_parse_structural_attrs(line, "replaceCallee", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, true)) return false;
  if (op->name || op->type || op->params || op->path || op->has_line_value || op->has_column_value ||
      op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
      op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "replaceCallee operation has unsupported attributes", "node, expect, and value", line);
    return false;
  }
  if (!op->node || !op->value) {
    patch_op_fail(result, op, "GPH001", "replaceCallee operation is missing required attributes", "node and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_add_import(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("addImport");
  if (!patch_parse_structural_attrs(line, "addImport", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, true, false, false, false, false, true, true, true)) return false;
  if (op->type || op->params || op->path || op->has_line_value || op->has_column_value ||
      op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
      op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "addImport operation has unsupported attributes", "name, optional node, parent, value, order, and expect", line);
    return false;
  }
  if (!op->name) {
    patch_op_fail(result, op, "GPH001", "addImport operation is missing required attributes", "name", line);
    return false;
  }
  return true;
}

static bool patch_parse_add_function(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("addFunction");
  if (!patch_parse_structural_attrs(line, "addFunction", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, true, false, false, false, false, true, true, true)) return false;
  if (op->path || op->has_line_value || op->has_column_value || op->has_mutable_value || op->has_static_value) {
    patch_op_fail(result, op, "GPH001", "addFunction operation has unsupported attributes", "name, type, optional params, value, node, parent, order, public, fallible, exportC, and expect", line);
    return false;
  }
  if (!op->name || !op->type) {
    patch_op_fail(result, op, "GPH001", "addFunction operation is missing required attributes", "name and type", line);
    return false;
  }
  return true;
}

static bool patch_parse_add_param(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("addParam");
  if (!patch_parse_structural_attrs(line, "addParam", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, true, true, true)) return false;
  if (op->params || op->path || op->has_line_value || op->has_column_value || op->has_public_value ||
      op->has_mutable_value || op->has_static_value || op->has_fallible_value || op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "addParam operation has unsupported attributes", "node, name, type, optional value, order, and expect", line);
    return false;
  }
  if (!op->node || !op->name || !op->type) {
    patch_op_fail(result, op, "GPH001", "addParam operation is missing required attributes", "node, name, and type", line);
    return false;
  }
  return true;
}

static bool patch_parse_remove_param(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("removeParam");
  if (!patch_parse_structural_attrs(line, "removeParam", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, false)) return false;
  if (!op->node) {
    patch_op_fail(result, op, "GPH001", "removeParam operation is missing required attributes", "node", line);
    return false;
  }
  return true;
}

static bool patch_parse_remove_function(const char *line, int line_number, ZProgramGraphPatchResult *result) { ZProgramGraphPatchOpResult *op = patch_push_operation(result); op->line = line_number; op->op = z_strdup("removeFunction"); if (!patch_parse_structural_attrs(line, "removeFunction", result, op)) return false; if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, false)) return false; if (!op->node) { patch_op_fail(result, op, "GPH001", "removeFunction operation is missing required attributes", "node", line); return false; } return true; }

static bool patch_parse_remove_import(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("removeImport");
  if (!patch_parse_structural_attrs(line, "removeImport", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, true, false, false, false, false, false, true, true)) return false;
  if (op->type || op->params || op->path || op->has_order || op->has_line_value || op->has_column_value ||
      op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
      op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "removeImport operation has unsupported attributes", "node or name, optional parent, value, and expect", line);
    return false;
  }
  if (!op->node && !op->name) {
    patch_op_fail(result, op, "GPH001", "removeImport operation is missing required attributes", "node or name", line);
    return false;
  }
  return true;
}

static bool patch_parse_replace_import(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("replaceImport");
  if (!patch_parse_structural_attrs(line, "replaceImport", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, true, false, false, false, false, false, true, true)) return false;
  if (op->type || op->params || op->path || op->has_order || op->has_line_value || op->has_column_value ||
      op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
      op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "replaceImport operation has unsupported attributes", "node or name, value, optional parent, and expect", line);
    return false;
  }
  if ((!op->node && !op->name) || !op->value) {
    patch_op_fail(result, op, "GPH001", "replaceImport operation is missing required attributes", "node or name, and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_rename_import_alias(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("renameImportAlias");
  if (!patch_parse_structural_attrs(line, "renameImportAlias", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, true, false, false, false, false, false, true, true)) return false;
  if (op->type || op->params || op->path || op->has_order || op->has_line_value || op->has_column_value ||
      op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
      op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "renameImportAlias operation has unsupported attributes", "node or name, value, optional parent, and expect", line);
    return false;
  }
  if ((!op->node && !op->name) || !op->value) {
    patch_op_fail(result, op, "GPH001", "renameImportAlias operation is missing required attributes", "node or name, and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_change_return_type(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("changeReturnType");
  if (!patch_parse_structural_attrs(line, "changeReturnType", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, true)) return false;
  if (op->name || op->type || op->params || op->path || op->has_order || op->has_line_value || op->has_column_value ||
      op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
      op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "changeReturnType operation has unsupported attributes", "node, value, and optional expect", line);
    return false;
  }
  if (!op->node || !op->value) {
    patch_op_fail(result, op, "GPH001", "changeReturnType operation is missing required attributes", "node and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_change_param_type(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("changeParamType");
  if (!patch_parse_structural_attrs(line, "changeParamType", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, true)) return false;
  if (op->name || op->type || op->params || op->path || op->has_order || op->has_line_value || op->has_column_value ||
      op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
      op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "changeParamType operation has unsupported attributes", "node, value, and optional expect", line);
    return false;
  }
  if (!op->node || !op->value) {
    patch_op_fail(result, op, "GPH001", "changeParamType operation is missing required attributes", "node and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_change_field_type(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("changeFieldType");
  if (!patch_parse_structural_attrs(line, "changeFieldType", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, true)) return false;
  if (op->name || op->type || op->params || op->path || op->has_order || op->has_line_value || op->has_column_value ||
      op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
      op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "changeFieldType operation has unsupported attributes", "node, value, and optional expect", line);
    return false;
  }
  if (!op->node || !op->value) {
    patch_op_fail(result, op, "GPH001", "changeFieldType operation is missing required attributes", "node and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_change_local_type(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("changeLocalType");
  if (!patch_parse_structural_attrs(line, "changeLocalType", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, true)) return false;
  if (op->name || op->type || op->params || op->path || op->has_order || op->has_line_value || op->has_column_value ||
      op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
      op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "changeLocalType operation has unsupported attributes", "node, value, and optional expect", line);
    return false;
  }
  if (!op->node || !op->value) {
    patch_op_fail(result, op, "GPH001", "changeLocalType operation is missing required attributes", "node and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_rename_param(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("renameParam");
  if (!patch_parse_structural_attrs(line, "renameParam", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, true)) return false;
  if (op->name || op->type || op->params || op->path || op->has_order || op->has_line_value || op->has_column_value ||
      op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
      op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "renameParam operation has unsupported attributes", "node, value, and optional expect", line);
    return false;
  }
  if (!op->node || !op->value) {
    patch_op_fail(result, op, "GPH001", "renameParam operation is missing required attributes", "node and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_rename_field(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("renameField");
  if (!patch_parse_structural_attrs(line, "renameField", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, true)) return false;
  if (op->name || op->type || op->params || op->path || op->has_order || op->has_line_value || op->has_column_value ||
      op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
      op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "renameField operation has unsupported attributes", "node, value, and optional expect", line);
    return false;
  }
  if (!op->node || !op->value) {
    patch_op_fail(result, op, "GPH001", "renameField operation is missing required attributes", "node and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_rename_local(const char *line, int line_number, ZProgramGraphPatchResult *result) {
  ZProgramGraphPatchOpResult *op = patch_push_operation(result);
  op->line = line_number;
  op->op = z_strdup("renameLocal");
  if (!patch_parse_structural_attrs(line, "renameLocal", result, op)) return false;
  if (!patch_reject_attrs(op, result, line, true, false, false, false, false, false, false, true, true)) return false;
  if (op->name || op->type || op->params || op->path || op->has_order || op->has_line_value || op->has_column_value ||
      op->has_public_value || op->has_mutable_value || op->has_static_value || op->has_fallible_value ||
      op->has_export_c_value) {
    patch_op_fail(result, op, "GPH001", "renameLocal operation has unsupported attributes", "node, value, and optional expect", line);
    return false;
  }
  if (!op->node || !op->value) {
    patch_op_fail(result, op, "GPH001", "renameLocal operation is missing required attributes", "node and value", line);
    return false;
  }
  return true;
}

static bool patch_parse_text(char *text, ZProgramGraphPatchResult *result) {
  bool saw_header = false;
  int line_number = 0;
  char *cursor = text ? text : "";
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
        patch_result_fail(result, "GPH001", "unknown program graph patch schema", "zero-program-graph-patch v1", trimmed);
        return false;
      }
      saw_header = true;
      continue;
    }
    if (strncmp(trimmed, "expect", strlen("expect")) == 0 && isspace((unsigned char)trimmed[strlen("expect")])) {
      if (!patch_parse_expect_graph_hash(trimmed, result)) {
        patch_result_fail(result, "GPH001", "invalid graph hash precondition", "expect graphHash \"graph:<hash>\"", trimmed);
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
    } else if (strncmp(trimmed, "renameSymbol", strlen("renameSymbol")) == 0 && isspace((unsigned char)trimmed[strlen("renameSymbol")])) {
      if (!patch_parse_rename_symbol(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "replaceCallee", strlen("replaceCallee")) == 0 && isspace((unsigned char)trimmed[strlen("replaceCallee")])) {
      if (!patch_parse_replace_callee(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "addImport", strlen("addImport")) == 0 && isspace((unsigned char)trimmed[strlen("addImport")])) {
      if (!patch_parse_add_import(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "addFunction", strlen("addFunction")) == 0 && isspace((unsigned char)trimmed[strlen("addFunction")])) {
      if (!patch_parse_add_function(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "addParam", strlen("addParam")) == 0 && isspace((unsigned char)trimmed[strlen("addParam")])) {
      if (!patch_parse_add_param(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "removeParam", strlen("removeParam")) == 0 && isspace((unsigned char)trimmed[strlen("removeParam")])) { if (!patch_parse_remove_param(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "removeFunction", strlen("removeFunction")) == 0 && isspace((unsigned char)trimmed[strlen("removeFunction")])) { if (!patch_parse_remove_function(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "removeImport", strlen("removeImport")) == 0 && isspace((unsigned char)trimmed[strlen("removeImport")])) { if (!patch_parse_remove_import(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "replaceImport", strlen("replaceImport")) == 0 && isspace((unsigned char)trimmed[strlen("replaceImport")])) { if (!patch_parse_replace_import(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "renameImportAlias", strlen("renameImportAlias")) == 0 && isspace((unsigned char)trimmed[strlen("renameImportAlias")])) {
      if (!patch_parse_rename_import_alias(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "changeReturnType", strlen("changeReturnType")) == 0 && isspace((unsigned char)trimmed[strlen("changeReturnType")])) {
      if (!patch_parse_change_return_type(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "changeParamType", strlen("changeParamType")) == 0 && isspace((unsigned char)trimmed[strlen("changeParamType")])) {
      if (!patch_parse_change_param_type(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "changeFieldType", strlen("changeFieldType")) == 0 && isspace((unsigned char)trimmed[strlen("changeFieldType")])) {
      if (!patch_parse_change_field_type(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "changeLocalType", strlen("changeLocalType")) == 0 && isspace((unsigned char)trimmed[strlen("changeLocalType")])) {
      if (!patch_parse_change_local_type(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "renameParam", strlen("renameParam")) == 0 && isspace((unsigned char)trimmed[strlen("renameParam")])) {
      if (!patch_parse_rename_param(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "renameField", strlen("renameField")) == 0 && isspace((unsigned char)trimmed[strlen("renameField")])) {
      if (!patch_parse_rename_field(trimmed, line_number, result)) return false;
    } else if (strncmp(trimmed, "renameLocal", strlen("renameLocal")) == 0 && isspace((unsigned char)trimmed[strlen("renameLocal")])) {
      if (!patch_parse_rename_local(trimmed, line_number, result)) return false;
    } else {
      patch_result_fail(result, "GPH001", "unknown program graph patch operation", "expect, set, insert, insertEdge, replace, delete, rename, renameSymbol, renameParam, renameLocal, renameField, replaceCallee, addImport, addFunction, addParam, removeParam, removeFunction, removeImport, replaceImport, renameImportAlias, changeReturnType, changeParamType, changeFieldType, or changeLocalType", trimmed);
      return false;
    }
  }
  if (!saw_header) {
    patch_result_fail(result, "GPH001", "program graph patch is empty", "zero-program-graph-patch v1", "");
    return false;
  }
  return true;
}
bool z_program_graph_apply_patch_text(const char *label, const char *text, size_t text_len, ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZDiag *diag) {
  if (!result) return false;
  *result = (ZProgramGraphPatchResult){0};
  result->actual_graph_hash = z_strdup(graph && graph->graph_hash ? graph->graph_hash : "");
  if (!text) { text = ""; text_len = 0; }
  if (memchr(text, '\0', text_len)) {
    patch_result_fail(result, "GPH001", "program graph patch contains NUL byte", "text without NUL bytes", "NUL byte");
    if (diag && label) diag->path = label;
    return false;
  }
  char *copy = z_strndup(text, text_len);
  bool parsed = patch_parse_text(copy, result); free(copy); if (!parsed) return false;
  if (result->expected_graph_hash && !patch_text_eq(result->expected_graph_hash, result->actual_graph_hash)) {
    patch_result_fail(result, "GPH002", "graph hash precondition failed", result->expected_graph_hash, result->actual_graph_hash);
    return false;
  }
  for (size_t i = 0; i < result->operation_len; i++) {
    if (!z_program_graph_patch_apply_operation(graph, result, &result->operations[i])) return false;
    z_program_graph_finalize_identities(graph);
  }
  patch_replace_text(&result->actual_graph_hash, graph->graph_hash);
  ZProgramGraphValidation validation = {0};
  if (!z_program_graph_validate(graph, &validation)) {
    patch_result_fail(result, "GPH006", validation.message, "shape-valid ProgramGraph", validation.code);
    return false;
  }
  result->ok = true; return true;
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
    patch_free_text(&op->params);
    patch_free_text(&op->path);
  }
  free(result->operations);
  *result = (ZProgramGraphPatchResult){0};
}
