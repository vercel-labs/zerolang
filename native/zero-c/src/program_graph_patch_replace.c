#include "program_graph_patch_internal.h"
#include "program_graph_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool replace_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

/* Decodes the documented inline escapes (\n, \t, \\) in --old/--new text. */
static char *patch_inline_unescape(const char *text) {
  ZBuf out;
  zbuf_init(&out);
  for (const char *cursor = text ? text : ""; *cursor; cursor++) {
    if (*cursor == '\\' && (cursor[1] == 'n' || cursor[1] == 't' || cursor[1] == '\\')) {
      zbuf_append_char(&out, cursor[1] == 'n' ? '\n' : (cursor[1] == 't' ? '\t' : '\\'));
      cursor++;
      continue;
    }
    zbuf_append_char(&out, *cursor);
  }
  return out.data ? out.data : z_strdup("");
}

static size_t patch_count_occurrences(const char *haystack, const char *needle, const char **first) {
  size_t count = 0;
  if (first) *first = NULL;
  if (!haystack || !needle || !needle[0]) return 0;
  for (const char *cursor = strstr(haystack, needle); cursor; cursor = strstr(cursor + 1, needle)) {
    if (first && !*first) *first = cursor;
    count++;
  }
  return count;
}

/*
 * Surgical in-function replacement: project the function body to the same
 * canonical text `zero view --fn` prints, replace one unique literal
 * occurrence of the old text, and re-apply the whole body through the
 * replaceFunctionBody pipeline so the edit revalidates exactly like
 * --replace-fn. Inline --old/--new text is matched verbatim first; when the
 * verbatim form does not occur and contains backslash escapes, the decoded
 * form (\n, \t, \\) is tried so multi-line inline texts work without files.
 */
bool z_program_graph_apply_replace_in_fn(const char *function_name, const char *old_inline, const char *old_file, const char *new_inline, const char *new_file, const char *expect_graph_hash, ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZDiag *diag) {
  if (!result) return false;
  *result = (ZProgramGraphPatchResult){0};
  result->actual_graph_hash = z_strdup(graph && graph->graph_hash ? graph->graph_hash : "");
  if (expect_graph_hash) z_graph_patch_replace_text(&result->expected_graph_hash, expect_graph_hash);
  const char *name = function_name ? function_name : "";
  size_t old_len = 0;
  size_t new_len = 0;
  char *old_text = old_file ? z_graph_patch_read_body_source(old_file, &old_len, diag) : z_strdup(old_inline ? old_inline : "");
  if (!old_text) return false;
  if (!old_file) old_len = strlen(old_text);
  char *new_text = new_file ? z_graph_patch_read_body_source(new_file, &new_len, diag) : z_strdup(new_inline ? new_inline : "");
  if (!new_text) {
    free(old_text);
    return false;
  }
  if (!new_file) new_len = strlen(new_text);
  if (memchr(old_text, '\0', old_len) || memchr(new_text, '\0', new_len)) {
    free(old_text);
    free(new_text);
    z_graph_patch_result_fail(result, "GPH001", "replace-in-fn text contains NUL byte", "replacement text without NUL bytes", "NUL byte");
    return false;
  }
  if (old_len == 0) {
    free(old_text);
    free(new_text);
    z_graph_patch_result_fail(result, "GPH001", "replace-in-fn --old text is empty", "the exact body text to replace, as zero view --fn <name> prints it", old_file ? (strcmp(old_file, "-") == 0 ? "<stdin>" : old_file) : "--old");
    return false;
  }
  ZBuf view;
  zbuf_init(&view);
  ZDiag view_diag = {0};
  if (!z_program_graph_append_view_function(&view, graph, "src/main.0", name, &view_diag)) {
    zbuf_free(&view);
    free(old_text);
    free(new_text);
    char message[160];
    snprintf(message, sizeof(message), "replace-in-fn function '%s' was not found", name);
    z_graph_patch_result_fail(result, "GPH004", message, view_diag.help[0] ? view_diag.help : "an existing function name; zero query lists every function", name);
    return false;
  }
  /* The view prints "fn name(...) {\n<rows>\n}\n"; the body rows are the
   * lines between the signature line and the closing brace line. */
  const char *full_text = view.data ? view.data : "";
  const char *body_start = strchr(full_text, '\n');
  body_start = body_start ? body_start + 1 : full_text;
  size_t body_len = strlen(body_start);
  if (body_len >= 2 && body_start[body_len - 1] == '\n' && body_start[body_len - 2] == '}') body_len -= 2;
  char *body_rows = z_strndup(body_start, body_len);
  zbuf_free(&view);
  const char *first = NULL;
  size_t count = patch_count_occurrences(body_rows, old_text, &first);
  bool used_escapes = false;
  if (count == 0 && !old_file && strchr(old_text, '\\')) {
    char *decoded = patch_inline_unescape(old_text);
    if (!replace_text_eq(decoded, old_text)) {
      const char *decoded_first = NULL;
      size_t decoded_count = patch_count_occurrences(body_rows, decoded, &decoded_first);
      if (decoded_count > 0) {
        free(old_text);
        old_text = decoded;
        old_len = strlen(old_text);
        count = decoded_count;
        first = decoded_first;
        used_escapes = true;
      } else {
        free(decoded);
      }
    } else {
      free(decoded);
    }
  }
  if (used_escapes && !new_file) {
    char *decoded_new = patch_inline_unescape(new_text);
    free(new_text);
    new_text = decoded_new;
    new_len = strlen(new_text);
  }
  if (count != 1) {
    char message[160];
    char expected[256];
    if (count == 0) {
      snprintf(message, sizeof(message), "replace-in-fn --old text was not found in function '%s'", name);
      snprintf(expected, sizeof(expected), "text that appears in the body zero view --fn %s prints", name);
      z_graph_patch_result_fail(result, "GPH004", message, expected, old_text);
    } else {
      snprintf(message, sizeof(message), "replace-in-fn --old text matches %zu places in function '%s'", count, name);
      snprintf(expected, sizeof(expected), "a uniquely matching --old; extend it with surrounding lines from zero view --fn %s until it matches once", name);
      z_graph_patch_result_fail(result, "GPH003", message, expected, old_text);
    }
    free(body_rows);
    free(old_text);
    free(new_text);
    return false;
  }
  ZBuf rows;
  zbuf_init(&rows);
  char *prefix = z_strndup(body_rows, (size_t)(first - body_rows));
  zbuf_append(&rows, prefix);
  free(prefix);
  zbuf_append(&rows, new_text);
  zbuf_append(&rows, first + old_len);
  free(body_rows);
  free(old_text);
  free(new_text);
  ZProgramGraphPatchOpResult *op = z_graph_patch_push_operation(result);
  op->line = 1;
  op->op = z_strdup("replaceFunctionBody");
  op->function = z_strdup(name);
  op->value = rows.data ? rows.data : z_strdup("");
  return z_graph_patch_apply_operations(graph, result);
}

