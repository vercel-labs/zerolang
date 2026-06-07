#include "agent_repair.h"

#include <stdlib.h>
#include <string.h>

static bool repair_safety_is_behavior_preserving(const char *fix_safety) {
  return fix_safety && fix_safety[0] == 'b' && fix_safety[1] == 'e' && fix_safety[2] == 'h';
}

static bool find_make_binding_mutable_edit(const char *source, ZAgentRepairEdit *edit) {
  const char *cursor = source ? source : "";
  int line = 1;
  while (*cursor) {
    const char *line_start = cursor;
    const char *line_end = strchr(line_start, '\n');
    if (!line_end) line_end = line_start + strlen(line_start);
    char *line_text = z_strndup(line_start, (size_t)(line_end - line_start));
    char *let_pos = strstr(line_text, "let ");
    if (let_pos && !strstr(line_text, "var ") && strchr(line_text, '[')) {
      ZBuf replacement;
      zbuf_init(&replacement);
      char *prefix = z_strndup(line_text, (size_t)(let_pos - line_text));
      zbuf_append(&replacement, prefix);
      zbuf_append(&replacement, "var ");
      zbuf_append(&replacement, let_pos + strlen("let "));
      free(prefix);
      edit->line = line;
      edit->old_line = line_text;
      edit->new_line = replacement.data;
      return true;
    }
    free(line_text);
    if (!*line_end) break;
    cursor = line_end + 1;
    line++;
  }
  return false;
}

static bool find_match_binding_annotation_edit(const char *source, const ZDiag *diag, ZAgentRepairEdit *edit) {
  if (!source || !diag || diag->line <= 0 || !diag->actual[0]) return false;
  const char *cursor = source;
  for (int line = 1; *cursor; line++) {
    const char *line_start = cursor;
    const char *line_end = strchr(line_start, '\n');
    if (!line_end) line_end = line_start + strlen(line_start);
    if (line == diag->line) {
      char *line_text = z_strndup(line_start, (size_t)(line_end - line_start));
      char *colon = strchr(line_text, ':');
      char *equals = colon ? strchr(colon, '=') : NULL;
      if (!colon || !equals || equals <= colon) { free(line_text); return false; }
      while (equals > colon && (equals[-1] == ' ' || equals[-1] == '\t')) equals--;
      char *prefix = z_strndup(line_text, (size_t)(colon + 1 - line_text));
      ZBuf replacement;
      zbuf_init(&replacement);
      zbuf_append(&replacement, prefix);
      zbuf_append_char(&replacement, ' ');
      zbuf_append(&replacement, diag->actual);
      zbuf_append(&replacement, equals);
      free(prefix);
      edit->line = line;
      edit->old_line = line_text;
      edit->new_line = replacement.data;
      return true;
    }
    if (!*line_end) break;
    cursor = line_end + 1;
  }
  return false;
}

bool z_agent_repair_can_apply(const ZDiag *diag, const char *fix_safety) {
  return diag && (diag->code == 3006 || diag->code == 3010) && repair_safety_is_behavior_preserving(fix_safety);
}

bool z_agent_repair_find_edit(const char *source, const ZDiag *diag, ZAgentRepairEdit *edit) {
  if (!edit) return false;
  edit->line = 0; edit->old_line = NULL; edit->new_line = NULL;
  if (!source || !diag) return false;
  bool found = false;
  if (diag->code == 3010) found = find_make_binding_mutable_edit(source, edit);
  else if (diag->code == 3006) found = find_match_binding_annotation_edit(source, diag, edit);
  if (found && edit->old_line && edit->new_line) { const char *old = edit->old_line; const char *next = edit->new_line; while (*old && *old == *next) { old++; next++; } if (*old == *next) { z_agent_repair_edit_free(edit); return false; } }
  return found;
}

char *z_agent_repair_apply_single_line_edit(const char *source, int target_line, const char *new_line) {
  ZBuf out;
  zbuf_init(&out);
  const char *cursor = source ? source : "";
  int line = 1;
  while (*cursor) {
    const char *line_start = cursor;
    const char *line_end = strchr(line_start, '\n');
    bool has_newline = line_end != NULL;
    if (!line_end) line_end = line_start + strlen(line_start);
    if (line == target_line) zbuf_append(&out, new_line);
    else {
      char *line_text = z_strndup(line_start, (size_t)(line_end - line_start));
      zbuf_append(&out, line_text);
      free(line_text);
    }
    if (has_newline) zbuf_append_char(&out, '\n');
    if (!has_newline) break;
    cursor = line_end + 1;
    line++;
  }
  return out.data;
}

void z_agent_repair_edit_free(ZAgentRepairEdit *edit) {
  if (!edit) return;
  free(edit->old_line);
  free(edit->new_line);
  edit->old_line = NULL;
  edit->new_line = NULL;
  edit->line = 0;
}
