#include "program_graph_patch.h"

#include <ctype.h>
#include <errno.h>
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
    } else {
      patch_result_fail(result, "GPH001", "unknown program graph patch operation", "expect or set", trimmed);
      return false;
    }
  }
  if (!saw_header) {
    patch_result_fail(result, "GPH001", "program graph patch is empty", "zero-program-graph-patch v1", "");
    return false;
  }
  return true;
}

static ZProgramGraphNode *patch_find_node(ZProgramGraph *graph, const char *node_id) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (patch_text_eq(graph->nodes[i].id, node_id)) return &graph->nodes[i];
  }
  return NULL;
}

static char **patch_node_text_field(ZProgramGraphNode *node, const char *field) {
  if (!node || !field) return NULL;
  if (strcmp(field, "name") == 0) return &node->name;
  if (strcmp(field, "type") == 0) return &node->type;
  if (strcmp(field, "value") == 0) return &node->value;
  return NULL;
}

static bool *patch_node_bool_field(ZProgramGraphNode *node, const char *field) {
  if (!node || !field) return NULL;
  if (strcmp(field, "public") == 0) return &node->is_public;
  if (strcmp(field, "mutable") == 0) return &node->is_mutable;
  if (strcmp(field, "static") == 0) return &node->is_static;
  if (strcmp(field, "fallible") == 0) return &node->fallible;
  if (strcmp(field, "exportC") == 0) return &node->export_c;
  return NULL;
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

static bool patch_apply_operation(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_find_node(graph, op->node);
  if (!node) {
    patch_op_fail(result, op, "GPH004", "patch node was not found", op->node, "");
    return false;
  }

  char **text_slot = patch_node_text_field(node, op->field);
  if (text_slot) {
    patch_replace_text(&op->actual, *text_slot);
    if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
      patch_op_fail(result, op, "GPH005", "patch field precondition failed", op->expected, op->actual);
      return false;
    }
    patch_replace_text(text_slot, op->value);
    op->ok = true;
    return true;
  }

  bool *bool_slot = patch_node_bool_field(node, op->field);
  if (bool_slot) {
    bool next = false;
    if (!patch_parse_bool(op->value, &next)) {
      patch_op_fail(result, op, "GPH003", "patch flag value must be true or false", "true or false", op->value);
      return false;
    }
    const char *actual = *bool_slot ? "true" : "false";
    patch_replace_text(&op->actual, actual);
    if (op->has_expected && !patch_text_eq(op->expected, actual)) {
      patch_op_fail(result, op, "GPH005", "patch field precondition failed", op->expected, actual);
      return false;
    }
    *bool_slot = next;
    op->ok = true;
    return true;
  }

  patch_op_fail(result, op, "GPH003", "patch field is not editable", "name, type, value, public, mutable, static, fallible, or exportC", op->field);
  return false;
}

bool z_program_graph_apply_patch_file(const char *path, ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZDiag *diag) {
  if (!result) return false;
  *result = (ZProgramGraphPatchResult){0};
  result->actual_graph_hash = z_strdup(graph && graph->graph_hash ? graph->graph_hash : "");
  size_t text_len = 0;
  char *text = patch_read_file(path, &text_len, diag);
  if (!text) return false;
  if (memchr(text, '\0', text_len)) {
    patch_result_fail(result, "GPH001", "program graph patch contains NUL byte", "text without NUL bytes", "NUL byte");
    free(text);
    return false;
  }
  bool parsed = patch_parse_text(text, result);
  free(text);
  if (!parsed) return false;

  if (result->expected_graph_hash && !patch_text_eq(result->expected_graph_hash, result->actual_graph_hash)) {
    patch_result_fail(result, "GPH002", "graph hash precondition failed", result->expected_graph_hash, result->actual_graph_hash);
    return false;
  }

  for (size_t i = 0; i < result->operation_len; i++) {
    if (!patch_apply_operation(graph, result, &result->operations[i])) return false;
  }

  z_program_graph_finalize_identities(graph);
  patch_replace_text(&result->actual_graph_hash, graph->graph_hash);
  ZProgramGraphValidation validation = {0};
  if (!z_program_graph_validate(graph, &validation)) {
    patch_result_fail(result, "GPH006", validation.message, "shape-valid ProgramGraph", validation.code);
    return false;
  }
  result->ok = true;
  return true;
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
    patch_free_text(&op->field);
    patch_free_text(&op->expected);
    patch_free_text(&op->actual);
    patch_free_text(&op->value);
  }
  free(result->operations);
  *result = (ZProgramGraphPatchResult){0};
}
