#include "c_import.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool c_ident_char(char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
}

static char *trim_span_copy(const char *start, const char *end) {
  if (!start || !end || end < start) return z_strdup("");
  while (start < end && isspace((unsigned char)*start)) start++;
  while (end > start && isspace((unsigned char)end[-1])) end--;
  return z_strndup(start, (size_t)(end - start));
}

static const char *last_ident_start_before(const char *start, const char *end) {
  const char *cursor = end;
  while (cursor > start && !c_ident_char(cursor[-1])) cursor--;
  const char *ident_end = cursor;
  while (cursor > start && c_ident_char(cursor[-1])) cursor--;
  return ident_end > cursor ? cursor : NULL;
}

static void c_import_function_push_param(ZCImportFunction *function, ZCImportParam param) {
  if (function->param_len == function->param_cap) {
    function->param_cap = z_grow_capacity(function->param_cap, function->param_len + 1, 4);
    function->params = z_checked_reallocarray(function->params, function->param_cap, sizeof(ZCImportParam));
  }
  function->params[function->param_len++] = param;
}

static void c_import_function_vec_push(ZCImportFunctionVec *vec, ZCImportFunction function) {
  if (vec->len == vec->cap) {
    vec->cap = z_grow_capacity(vec->cap, vec->len + 1, 4);
    vec->items = z_checked_reallocarray(vec->items, vec->cap, sizeof(ZCImportFunction));
  }
  vec->items[vec->len++] = function;
}

void z_c_import_function_free(ZCImportFunction *function) {
  if (!function) return;
  free(function->name);
  free(function->return_c_type);
  free(function->return_zero_type);
  for (size_t i = 0; i < function->param_len; i++) {
    free(function->params[i].name);
    free(function->params[i].c_type);
    free(function->params[i].zero_type);
  }
  free(function->params);
  *function = (ZCImportFunction){0};
}

void z_c_import_function_vec_free(ZCImportFunctionVec *vec) {
  if (!vec) return;
  for (size_t i = 0; i < vec->len; i++) z_c_import_function_free(&vec->items[i]);
  free(vec->items);
  *vec = (ZCImportFunctionVec){0};
}

static bool normalized_c_type(const char *c_type, char *out, size_t out_len) {
  if (!c_type || !out || out_len == 0) return false;
  out[0] = 0;
  if (strchr(c_type, '*') || strchr(c_type, '[') || strchr(c_type, ']')) return false;
  char *copy = trim_span_copy(c_type, c_type + strlen(c_type));
  size_t used = 0;
  const char *cursor = copy;
  while (*cursor) {
    while (isspace((unsigned char)*cursor)) cursor++;
    if (!*cursor) break;
    const char *word_start = cursor;
    while (*cursor && !isspace((unsigned char)*cursor)) cursor++;
    char *word = z_strndup(word_start, (size_t)(cursor - word_start));
    bool skip = strcmp(word, "const") == 0 || strcmp(word, "volatile") == 0 ||
                strcmp(word, "register") == 0 || strcmp(word, "extern") == 0;
    if (!skip) {
      if (used > 0 && used + 1 < out_len) out[used++] = ' ';
      for (size_t i = 0; word[i] && used + 1 < out_len; i++) out[used++] = word[i];
    }
    free(word);
  }
  out[used < out_len ? used : out_len - 1] = 0;
  free(copy);
  return out[0] != 0;
}

bool z_c_type_to_zero(const char *c_type, char *out, size_t out_len) {
  if (!out || out_len == 0) return false;
  out[0] = 0;
  char normalized[128];
  if (!normalized_c_type(c_type, normalized, sizeof(normalized))) return false;
  if (strcmp(normalized, "void") == 0) snprintf(out, out_len, "Void");
  else if (strcmp(normalized, "bool") == 0 || strcmp(normalized, "_Bool") == 0) snprintf(out, out_len, "Bool");
  else if (strcmp(normalized, "char") == 0 || strcmp(normalized, "unsigned char") == 0 || strcmp(normalized, "uint8_t") == 0) snprintf(out, out_len, "u8");
  else if (strcmp(normalized, "unsigned short") == 0 || strcmp(normalized, "uint16_t") == 0) snprintf(out, out_len, "u16");
  else if (strcmp(normalized, "int") == 0 || strcmp(normalized, "signed int") == 0 || strcmp(normalized, "int32_t") == 0) snprintf(out, out_len, "i32");
  else if (strcmp(normalized, "unsigned") == 0 || strcmp(normalized, "unsigned int") == 0 || strcmp(normalized, "uint32_t") == 0) snprintf(out, out_len, "u32");
  else if (strcmp(normalized, "long long") == 0 || strcmp(normalized, "signed long long") == 0 || strcmp(normalized, "int64_t") == 0) snprintf(out, out_len, "i64");
  else if (strcmp(normalized, "unsigned long long") == 0 || strcmp(normalized, "uint64_t") == 0) snprintf(out, out_len, "u64");
  else if (strcmp(normalized, "size_t") == 0) snprintf(out, out_len, "usize");
  else return false;
  return true;
}

static bool c_import_parse_param(ZCImportFunction *function, const char *start, const char *end) {
  char *param_text = trim_span_copy(start, end);
  if (!param_text[0] || strcmp(param_text, "void") == 0) {
    free(param_text);
    return true;
  }
  char zero_type[128];
  if (z_c_type_to_zero(param_text, zero_type, sizeof(zero_type))) {
    char name[32];
    snprintf(name, sizeof(name), "arg%zu", function->param_len);
    c_import_function_push_param(function, (ZCImportParam){.name = z_strdup(name), .c_type = z_strdup(param_text), .zero_type = z_strdup(zero_type)});
    free(param_text);
    return true;
  }
  const char *param_end = param_text + strlen(param_text);
  const char *name_start = last_ident_start_before(param_text, param_end);
  if (!name_start) {
    free(param_text);
    return false;
  }
  const char *name_end = name_start;
  while (*name_end && c_ident_char(*name_end)) name_end++;
  char *name = z_strndup(name_start, (size_t)(name_end - name_start));
  char *c_type = trim_span_copy(param_text, name_start);
  char *mapped = z_c_type_to_zero(c_type, zero_type, sizeof(zero_type)) ? z_strdup(zero_type) : z_strdup("Unknown");
  c_import_function_push_param(function, (ZCImportParam){.name = name, .c_type = c_type, .zero_type = mapped});
  free(param_text);
  return true;
}

static void c_import_parse_params(ZCImportFunction *function, const char *start, const char *end) {
  const char *cursor = start;
  while (cursor && cursor < end) {
    const char *comma = cursor;
    while (comma < end && *comma != ',') comma++;
    if (!c_import_parse_param(function, cursor, comma)) break;
    cursor = comma < end ? comma + 1 : end;
  }
}

static bool c_import_parse_function_line(const char *line, ZCImportFunction *out) {
  const char *paren = strchr(line, '(');
  const char *close = paren ? strstr(paren, ");") : NULL;
  const char *name_start = paren ? last_ident_start_before(line, paren) : NULL;
  if (!paren || !close || !name_start || strstr(line, "typedef")) return false;
  const char *name_end = name_start;
  while (*name_end && c_ident_char(*name_end)) name_end++;
  char *name = z_strndup(name_start, (size_t)(name_end - name_start));
  char *return_c_type = trim_span_copy(line, name_start);
  char return_zero_type[128];
  char *mapped_return = z_c_type_to_zero(return_c_type, return_zero_type, sizeof(return_zero_type)) ? z_strdup(return_zero_type) : z_strdup("Unknown");
  *out = (ZCImportFunction){
    .name = name,
    .return_c_type = return_c_type,
    .return_zero_type = mapped_return,
  };
  c_import_parse_params(out, paren + 1, close);
  return true;
}

bool z_c_header_parse_functions(const char *header, ZCImportFunctionVec *out) {
  if (!out) return false;
  const char *cursor = header ? header : "";
  while (*cursor) {
    const char *line_end = strchr(cursor, '\n');
    if (!line_end) line_end = cursor + strlen(cursor);
    char *line = trim_span_copy(cursor, line_end);
    if (line[0] && line[0] != '#' && strchr(line, '(') && strstr(line, ");")) {
      ZCImportFunction function = {0};
      if (c_import_parse_function_line(line, &function)) c_import_function_vec_push(out, function);
    }
    free(line);
    cursor = *line_end ? line_end + 1 : line_end;
  }
  return true;
}

static void c_import_function_clone(ZCImportFunction *out, const ZCImportFunction *source) {
  *out = (ZCImportFunction){
    .name = z_strdup(source->name),
    .return_c_type = z_strdup(source->return_c_type),
    .return_zero_type = z_strdup(source->return_zero_type),
  };
  for (size_t i = 0; i < source->param_len; i++) {
    c_import_function_push_param(out, (ZCImportParam){
      .name = z_strdup(source->params[i].name),
      .c_type = z_strdup(source->params[i].c_type),
      .zero_type = z_strdup(source->params[i].zero_type),
    });
  }
}

bool z_c_import_alias_exists(const Program *program, const char *alias) {
  if (!program || !alias) return false;
  for (size_t i = 0; i < program->c_imports.len; i++) {
    const CImport *import = &program->c_imports.items[i];
    if (import->alias && strcmp(import->alias, alias) == 0) return true;
  }
  return false;
}

static const char *c_import_read_path(const CImport *import) {
  return import && import->resolved_header && import->resolved_header[0] ? import->resolved_header : (import ? import->header : NULL);
}

bool z_c_import_find_function(const Program *program, const char *alias, const char *symbol, ZCImportFunction *out, ZDiag *diag) {
  if (!program || !alias || !symbol || !out) return false;
  for (size_t i = 0; i < program->c_imports.len; i++) {
    const CImport *import = &program->c_imports.items[i];
    if (!import->alias || strcmp(import->alias, alias) != 0) continue;
    ZDiag read_diag = {0};
    const char *read_path = c_import_read_path(import);
    char *header = z_read_file(read_path, &read_diag);
    if (!header) {
      if (diag) {
        diag->code = 8001;
        diag->line = import->line;
        diag->column = import->column;
        diag->length = 1;
        snprintf(diag->message, sizeof(diag->message), "extern c header could not be read");
        snprintf(diag->expected, sizeof(diag->expected), "readable C header path");
        snprintf(diag->actual, sizeof(diag->actual), "%s", import->header ? import->header : "<missing>");
        snprintf(diag->help, sizeof(diag->help), "make the header path package-relative and target-specific");
      }
      return false;
    }
    ZCImportFunctionVec functions = {0};
    z_c_header_parse_functions(header, &functions);
    free(header);
    for (size_t fn = 0; fn < functions.len; fn++) {
      if (functions.items[fn].name && strcmp(functions.items[fn].name, symbol) == 0) {
        c_import_function_clone(out, &functions.items[fn]);
        z_c_import_function_vec_free(&functions);
        return true;
      }
    }
    z_c_import_function_vec_free(&functions);
  }
  return false;
}
