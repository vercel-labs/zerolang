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

char *z_c_header_strip_comments(const char *header) {
  ZBuf out;
  zbuf_init(&out);
  const char *cursor = header ? header : "";
  while (*cursor) {
    if (cursor[0] == '/' && cursor[1] == '/') {
      cursor += 2;
      while (*cursor && *cursor != '\n') cursor++;
      if (*cursor == '\n') zbuf_append_char(&out, *cursor++);
      continue;
    }
    if (cursor[0] == '/' && cursor[1] == '*') {
      zbuf_append_char(&out, ' ');
      cursor += 2;
      while (*cursor) {
        if (cursor[0] == '*' && cursor[1] == '/') {
          cursor += 2;
          break;
        }
        if (*cursor == '\n') zbuf_append_char(&out, '\n');
        cursor++;
      }
      continue;
    }
    if (*cursor == '"' || *cursor == '\'') {
      char quote = *cursor;
      zbuf_append_char(&out, *cursor++);
      while (*cursor) {
        char ch = *cursor++;
        zbuf_append_char(&out, ch);
        if (ch == '\\' && *cursor) {
          zbuf_append_char(&out, *cursor++);
          continue;
        }
        if (ch == quote) break;
      }
      continue;
    }
    zbuf_append_char(&out, *cursor++);
  }
  return out.data ? out.data : z_strdup("");
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

static const char *c_import_function_decl_close(const char *paren) {
  if (!paren) return NULL;
  int depth = 0;
  for (const char *cursor = paren; *cursor; cursor++) {
    if (*cursor == '(') depth++;
    else if (*cursor == ')' && depth > 0) {
      depth--;
      if (depth == 0) {
        const char *after = cursor + 1;
        while (isspace((unsigned char)*after)) after++;
        if (*after == ';') return cursor;
      }
    }
  }
  return NULL;
}

static bool c_import_parse_function_line(const char *line, ZCImportFunction *out) {
  const char *paren = strchr(line, '(');
  const char *close = c_import_function_decl_close(paren);
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
  char *params_text = trim_span_copy(paren + 1, close);
  out->old_style_params = params_text[0] == 0;
  free(params_text);
  if (!out->old_style_params) c_import_parse_params(out, paren + 1, close);
  return true;
}

static void c_import_append_declaration_fragment(ZBuf *decl, const char *line) {
  if (!decl || !line) return;
  const char *cursor = line;
  bool wrote_space = decl->len > 0;
  while (*cursor) {
    while (isspace((unsigned char)*cursor)) cursor++;
    if (!*cursor) break;
    const char *start = cursor;
    while (*cursor && !isspace((unsigned char)*cursor)) cursor++;
    if (wrote_space) zbuf_append_char(decl, ' ');
    char *word = z_strndup(start, (size_t)(cursor - start));
    zbuf_append(decl, word);
    free(word);
    wrote_space = true;
  }
}

typedef struct {
  bool parent_active;
  bool active;
  bool branch_taken;
} CImportPreprocFrame;

static bool c_import_parse_directive_word(const char *line, char *word, size_t word_len, const char **rest) {
  if (!line || line[0] != '#' || !word || word_len == 0) return false;
  const char *cursor = line + 1;
  while (isspace((unsigned char)*cursor)) cursor++;
  const char *start = cursor;
  while (c_ident_char(*cursor)) cursor++;
  size_t len = (size_t)(cursor - start);
  if (len == 0) return false;
  size_t copy_len = len < word_len - 1 ? len : word_len - 1;
  memcpy(word, start, copy_len);
  word[copy_len] = 0;
  while (isspace((unsigned char)*cursor)) cursor++;
  if (rest) *rest = cursor;
  return true;
}

static bool c_import_preproc_active(const CImportPreprocFrame *frames, size_t depth) {
  return depth == 0 || (frames[depth - 1].parent_active && frames[depth - 1].active);
}

static bool c_import_pp_expr_mentions_cplusplus(const char *expr) {
  return expr && strstr(expr, "__cplusplus") != NULL;
}

static bool c_import_pp_expr_active(const char *expr) {
  if (!expr) return false;
  char *trimmed = trim_span_copy(expr, expr + strlen(expr));
  bool active = false;
  if (strcmp(trimmed, "1") == 0 || strcmp(trimmed, "true") == 0 || strcmp(trimmed, "!defined(__cplusplus)") == 0 || strcmp(trimmed, "! defined(__cplusplus)") == 0) active = true;
  else if (strcmp(trimmed, "0") == 0 || strcmp(trimmed, "false") == 0 || c_import_pp_expr_mentions_cplusplus(trimmed)) active = false;
  free(trimmed);
  return active;
}

static void c_import_preproc_push(CImportPreprocFrame **frames, size_t *depth, size_t *cap, bool parent_active, bool active) {
  if (*depth == *cap) {
    *cap = z_grow_capacity(*cap, *depth + 1, 8);
    *frames = z_checked_reallocarray(*frames, *cap, sizeof(CImportPreprocFrame));
  }
  (*frames)[(*depth)++] = (CImportPreprocFrame){.parent_active = parent_active, .active = active, .branch_taken = active};
}

static bool c_import_handle_preprocessor_line(const char *line, CImportPreprocFrame **frames, size_t *depth, size_t *cap) {
  char directive[16];
  const char *rest = NULL;
  if (!c_import_parse_directive_word(line, directive, sizeof(directive), &rest)) return false;
  if (strcmp(directive, "if") == 0) {
    bool parent_active = c_import_preproc_active(*frames, *depth);
    c_import_preproc_push(frames, depth, cap, parent_active, parent_active && c_import_pp_expr_active(rest));
    return true;
  }
  if (strcmp(directive, "ifdef") == 0) {
    bool parent_active = c_import_preproc_active(*frames, *depth);
    c_import_preproc_push(frames, depth, cap, parent_active, false);
    return true;
  }
  if (strcmp(directive, "ifndef") == 0) {
    bool parent_active = c_import_preproc_active(*frames, *depth);
    c_import_preproc_push(frames, depth, cap, parent_active, parent_active);
    return true;
  }
  if (strcmp(directive, "elif") == 0 && *depth > 0) {
    CImportPreprocFrame *frame = &(*frames)[*depth - 1];
    bool active = frame->parent_active && !frame->branch_taken && c_import_pp_expr_active(rest);
    frame->active = active;
    frame->branch_taken = frame->branch_taken || active;
    return true;
  }
  if (strcmp(directive, "else") == 0 && *depth > 0) {
    CImportPreprocFrame *frame = &(*frames)[*depth - 1];
    frame->active = frame->parent_active && !frame->branch_taken;
    frame->branch_taken = true;
    return true;
  }
  if (strcmp(directive, "endif") == 0 && *depth > 0) {
    (*depth)--;
    return true;
  }
  return true;
}

static bool c_import_linkage_wrapper_line(const char *line) {
  if (!line) return false;
  if ((strstr(line, "extern \"C\"") || strstr(line, "extern \"C++\"")) && strchr(line, '{')) return true;
  const char *cursor = line;
  while (isspace((unsigned char)*cursor)) cursor++;
  if (*cursor != '}') return false;
  cursor++;
  while (isspace((unsigned char)*cursor)) cursor++;
  return *cursor == 0 || strncmp(cursor, "//", 2) == 0 || strncmp(cursor, "/*", 2) == 0;
}

bool z_c_header_parse_functions(const char *header, ZCImportFunctionVec *out) {
  if (!out) return false;
  char *stripped = z_c_header_strip_comments(header);
  const char *cursor = stripped ? stripped : "";
  CImportPreprocFrame *frames = NULL;
  size_t preproc_depth = 0;
  size_t preproc_cap = 0;
  ZBuf declaration;
  zbuf_init(&declaration);
  while (*cursor) {
    const char *line_end = strchr(cursor, '\n');
    if (!line_end) line_end = cursor + strlen(cursor);
    char *line = trim_span_copy(cursor, line_end);
    bool wrapper_line = c_import_linkage_wrapper_line(line);
    if (line[0] == '#') {
      c_import_handle_preprocessor_line(line, &frames, &preproc_depth, &preproc_cap);
    } else if (c_import_preproc_active(frames, preproc_depth) && !wrapper_line) {
      if (line[0]) c_import_append_declaration_fragment(&declaration, line);
      if (strchr(line, ';')) {
        ZCImportFunction function = {0};
        if (declaration.data && strchr(declaration.data, '(') && c_import_parse_function_line(declaration.data, &function)) c_import_function_vec_push(out, function);
        zbuf_free(&declaration);
        zbuf_init(&declaration);
      }
    } else if (!c_import_preproc_active(frames, preproc_depth)) {
      zbuf_free(&declaration);
      zbuf_init(&declaration);
    } else if (wrapper_line) {
      zbuf_free(&declaration);
      zbuf_init(&declaration);
    }
    free(line);
    cursor = *line_end ? line_end + 1 : line_end;
  }
  zbuf_free(&declaration);
  free(frames);
  free(stripped);
  return true;
}

static void c_import_function_clone(ZCImportFunction *out, const ZCImportFunction *source) {
  *out = (ZCImportFunction){
    .name = z_strdup(source->name),
    .return_c_type = z_strdup(source->return_c_type),
    .return_zero_type = z_strdup(source->return_zero_type),
    .old_style_params = source->old_style_params,
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
