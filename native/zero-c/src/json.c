#include "zero.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void z_json_string_append(ZBuf* buf, const char* value) {
  zbuf_append_char(buf, '"');
  for (const char* cursor = value ? value : ""; *cursor; cursor++) {
    if (*cursor == '"')
      zbuf_append(buf, "\\\"");
    else if (*cursor == '\\')
      zbuf_append(buf, "\\\\");
    else if (*cursor == '\n')
      zbuf_append(buf, "\\n");
    else if (*cursor == '\r')
      zbuf_append(buf, "\\r");
    else if (*cursor == '\t')
      zbuf_append(buf, "\\t");
    else
      zbuf_append_char(buf, *cursor);
  }
  zbuf_append_char(buf, '"');
}

const char* z_json_whitespace_skip(const char* cursor) {
  while (*cursor && isspace((unsigned char)*cursor)) cursor++;
  return cursor;
}

static const char* json_string_skip(const char* cursor) {
  if (*cursor != '"') return NULL;
  cursor++;
  while (*cursor) {
    if (*cursor == '\\' && cursor[1]) {
      cursor += 2;
      continue;
    }
    if (*cursor == '"') return cursor + 1;
    cursor++;
  }
  return NULL;
}

char* z_json_string_parse_copy(const char* cursor, const char** end_out) {
  if (*cursor != '"') return NULL;
  cursor++;
  ZBuf out;
  zbuf_init(&out);
  while (*cursor) {
    if (*cursor == '"') {
      if (end_out) *end_out = cursor + 1;
      if (!out.data) return z_strdup("");
      return out.data;
    }
    if (*cursor == '\\' && cursor[1]) {
      cursor++;
      switch (*cursor) {
      case '"':
        zbuf_append_char(&out, '"');
        break;
      case '\\':
        zbuf_append_char(&out, '\\');
        break;
      case '/':
        zbuf_append_char(&out, '/');
        break;
      case 'b':
        zbuf_append_char(&out, '\b');
        break;
      case 'f':
        zbuf_append_char(&out, '\f');
        break;
      case 'n':
        zbuf_append_char(&out, '\n');
        break;
      case 'r':
        zbuf_append_char(&out, '\r');
        break;
      case 't':
        zbuf_append_char(&out, '\t');
        break;
      case 'u':
        for (int i = 0; i < 4 && cursor[1]; i++) cursor++;
        zbuf_append_char(&out, '?');
        break;
      default:
        zbuf_append_char(&out, *cursor);
        break;
      }
      cursor++;
      continue;
    }
    zbuf_append_char(&out, *cursor++);
  }
  zbuf_free(&out);
  return NULL;
}

const char* z_json_value_skip(const char* cursor) {
  cursor = z_json_whitespace_skip(cursor);
  if (*cursor == '"') return json_string_skip(cursor);
  if (*cursor == '{') {
    cursor++;
    while (*cursor) {
      cursor = z_json_whitespace_skip(cursor);
      if (*cursor == '}') return cursor + 1;
      const char* key_end = json_string_skip(cursor);
      if (!key_end) return NULL;
      cursor = z_json_whitespace_skip(key_end);
      if (*cursor != ':') return NULL;
      cursor = z_json_value_skip(cursor + 1);
      if (!cursor) return NULL;
      cursor = z_json_whitespace_skip(cursor);
      if (*cursor == ',') {
        cursor++;
        continue;
      }
      if (*cursor == '}') return cursor + 1;
      return NULL;
    }
    return NULL;
  }
  if (*cursor == '[') {
    cursor++;
    while (*cursor) {
      cursor = z_json_whitespace_skip(cursor);
      if (*cursor == ']') return cursor + 1;
      cursor = z_json_value_skip(cursor);
      if (!cursor) return NULL;
      cursor = z_json_whitespace_skip(cursor);
      if (*cursor == ',') {
        cursor++;
        continue;
      }
      if (*cursor == ']') return cursor + 1;
      return NULL;
    }
    return NULL;
  }
  if (!*cursor) return NULL;
  while (*cursor && *cursor != ',' && *cursor != '}' && *cursor != ']') cursor++;
  return cursor;
}

static bool json_member_span_find(const char* object, const char* name, const char** value_start,
                                  const char** value_end) {
  const char* cursor = z_json_whitespace_skip(object);
  if (*cursor != '{') return false;
  cursor++;
  while (*cursor) {
    cursor = z_json_whitespace_skip(cursor);
    if (*cursor == '}') return false;
    const char* key_end = NULL;
    char* key = z_json_string_parse_copy(cursor, &key_end);
    if (!key) return false;
    cursor = z_json_whitespace_skip(key_end);
    if (*cursor != ':') {
      free(key);
      return false;
    }
    cursor = z_json_whitespace_skip(cursor + 1);
    const char* end = z_json_value_skip(cursor);
    if (!end) {
      free(key);
      return false;
    }
    bool matched = strcmp(key, name) == 0;
    free(key);
    if (matched) {
      *value_start = cursor;
      *value_end = end;
      return true;
    }
    cursor = z_json_whitespace_skip(end);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == '}') return false;
    return false;
  }
  return false;
}

bool z_json_span_path_get(const char* json, const char** path, size_t path_len, const char** value_start,
                          const char** value_end) {
  const char* start = json;
  const char* end = NULL;
  for (size_t i = 0; i < path_len; i++) {
    if (!json_member_span_find(start, path[i], &start, &end)) return false;
  }
  *value_start = start;
  *value_end = end;
  return true;
}

char* z_json_string_path_get(const char* json, const char** path, size_t path_len) {
  const char* start = NULL;
  const char* end = NULL;
  if (!z_json_span_path_get(json, path, path_len, &start, &end)) return NULL;
  (void)end;
  return z_json_string_parse_copy(start, NULL);
}

char* z_json_span_copy(const char* start, const char* end) {
  while (end > start && isspace((unsigned char)end[-1])) end--;
  return z_strndup(start, (size_t)(end - start));
}

bool z_json_number_from_span(const char* start, const char* end, long long* value_out) {
  char* text = z_json_span_copy(start, end);
  if (!text) return false;
  char* end_ptr = NULL;
  long long value = strtoll(text, &end_ptr, 10);
  bool ok = end_ptr && *end_ptr == 0;
  free(text);
  if (!ok) return false;
  if (value_out) *value_out = value;
  return true;
}

static char* json_array_from_span(const char* start, const char* end) {
  start = z_json_whitespace_skip(start);
  if (*start == '[') return z_json_span_copy(start, end);
  if (*start == '"') {
    char* value = z_json_span_copy(start, end);
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append_char(&buf, '[');
    zbuf_append(&buf, value);
    zbuf_append_char(&buf, ']');
    free(value);
    return buf.data;
  }
  return z_strdup("[]");
}

char* z_json_array_path_get(const char* json, const char** path, size_t path_len) {
  const char* start = NULL;
  const char* end = NULL;
  if (!z_json_span_path_get(json, path, path_len, &start, &end)) return z_strdup("[]");
  return json_array_from_span(start, end);
}
