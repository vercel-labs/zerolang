#include "zero.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void z_allocation_fatal(const char *operation) {
  fprintf(stderr, "zero: fatal: out of memory while %s\n", operation ? operation : "allocating memory");
  exit(70);
}

static size_t z_checked_allocation_size(size_t count, size_t item_size, const char *operation) {
  if (count == 0 || item_size == 0) return 1;
  if (count > SIZE_MAX / item_size) z_allocation_fatal(operation);
  return count * item_size;
}

void *z_checked_malloc(size_t size) {
  void *ptr = malloc(size == 0 ? 1 : size);
  if (!ptr) z_allocation_fatal("allocating memory");
  return ptr;
}

void *z_checked_calloc(size_t count, size_t item_size) {
  size_t size = z_checked_allocation_size(count, item_size, "allocating memory");
  void *ptr = calloc(1, size);
  if (!ptr) z_allocation_fatal("allocating memory");
  return ptr;
}

void *z_checked_reallocarray(void *ptr, size_t count, size_t item_size) {
  size_t size = z_checked_allocation_size(count, item_size, "growing memory");
  void *next = realloc(ptr, size);
  if (!next) z_allocation_fatal("growing memory");
  return next;
}

size_t z_grow_capacity(size_t current, size_t required, size_t initial) {
  size_t next = current == 0 ? (initial == 0 ? 1 : initial) : current;
  while (next < required) {
    if (next > SIZE_MAX / 2) {
      next = required;
      break;
    }
    next *= 2;
  }
  return next;
}

void zbuf_init(ZBuf *buf) {
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}

void zbuf_append_char(ZBuf *buf, char ch) {
  if (buf->len > SIZE_MAX - 2) z_allocation_fatal("growing buffer");
  size_t required = buf->len + 2;
  if (required > buf->cap) {
    size_t next = z_grow_capacity(buf->cap, required, 64);
    buf->data = z_checked_reallocarray(buf->data, next, sizeof(char));
    buf->cap = next;
  }
  buf->data[buf->len++] = ch;
  buf->data[buf->len] = 0;
}

void zbuf_append(ZBuf *buf, const char *text) {
  while (*text) zbuf_append_char(buf, *text++);
}

void zbuf_appendf(ZBuf *buf, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  va_list copy;
  va_copy(copy, args);
  int needed = vsnprintf(NULL, 0, fmt, copy);
  va_end(copy);
  if (needed < 0) {
    va_end(args);
    return;
  }
  char *tmp = z_checked_malloc((size_t)needed + 1);
  vsnprintf(tmp, (size_t)needed + 1, fmt, args);
  va_end(args);
  zbuf_append(buf, tmp);
  free(tmp);
}

void zbuf_free(ZBuf *buf) {
  free(buf->data);
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}

char *z_strdup(const char *text) {
  size_t len = strlen(text);
  return z_strndup(text, len);
}

char *z_strndup(const char *text, size_t len) {
  if (len == SIZE_MAX) z_allocation_fatal("copying string");
  char *copy = z_checked_malloc(len + 1);
  memcpy(copy, text, len);
  copy[len] = 0;
  return copy;
}

static void diag_io(ZDiag *diag, const char *path, const char *action) {
  diag->code = 1;
  diag->path = path;
  diag->line = 1;
  diag->column = 1;
  snprintf(diag->message, sizeof(diag->message), "failed to %s '%s': %s", action, path, strerror(errno));
}

static int zero_mkdir(const char *path) {
#if defined(_WIN32)
  return mkdir(path);
#else
  return mkdir(path, 0777);
#endif
}

char *z_read_file(const char *path, ZDiag *diag) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    diag_io(diag, path, "read");
    return NULL;
  }
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  if (size < 0) {
    diag_io(diag, path, "read");
    fclose(file);
    return NULL;
  }
  rewind(file);
  char *data = z_checked_malloc((size_t)size + 1);
  size_t read = fread(data, 1, (size_t)size, file);
  fclose(file);
  data[read] = 0;
  return data;
}

static bool mkdir_parents(const char *path) {
  char *copy = z_strdup(path);
  for (char *cursor = copy + 1; *cursor; cursor++) {
    if (*cursor == '/') {
      *cursor = 0;
      zero_mkdir(copy);
      *cursor = '/';
    }
  }
  free(copy);
  return true;
}

bool z_write_file(const char *path, const char *text, ZDiag *diag) {
  mkdir_parents(path);
  FILE *file = fopen(path, "wb");
  if (!file) {
    diag_io(diag, path, "write");
    return false;
  }
  fputs(text, file);
  fclose(file);
  return true;
}

bool z_write_binary_file(const char *path, const unsigned char *data, size_t len, ZDiag *diag) {
  mkdir_parents(path);
  FILE *file = fopen(path, "wb");
  if (!file) {
    diag_io(diag, path, "write");
    return false;
  }
  if (len > 0 && fwrite(data, 1, len, file) != len) {
    diag_io(diag, path, "write");
    fclose(file);
    return false;
  }
  fclose(file);
  return true;
}

static bool is_directory(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool ends_with(const char *text, const char *suffix) {
  size_t text_len = strlen(text);
  size_t suffix_len = strlen(suffix);
  return text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0;
}

static char *dirname_of(const char *path) {
  const char *slash = strrchr(path, '/');
  if (!slash) return z_strdup(".");
  return z_strndup(path, (size_t)(slash - path));
}

static char *join_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left);
  if (left[strlen(left) - 1] != '/') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right);
  return buf.data;
}

static char *normalize_path_text(const char *path) {
  bool absolute = path && path[0] == '/';
  char *copy = z_strdup(path ? path : "");
  char **segments = NULL;
  size_t segment_count = 0;
  char *cursor = copy;
  while (*cursor) {
    while (*cursor == '/') cursor++;
    if (!*cursor) break;
    char *start = cursor;
    while (*cursor && *cursor != '/') cursor++;
    char saved = *cursor;
    *cursor = 0;
    if (strcmp(start, ".") == 0) {
      // skip
    } else if (strcmp(start, "..") == 0) {
      if (segment_count > 0 && strcmp(segments[segment_count - 1], "..") != 0) {
        segment_count--;
      } else if (!absolute) {
        segments = z_checked_reallocarray(segments, segment_count + 1, sizeof(char *));
        segments[segment_count++] = start;
      }
    } else {
      segments = z_checked_reallocarray(segments, segment_count + 1, sizeof(char *));
      segments[segment_count++] = start;
    }
    if (!saved) break;
    cursor++;
  }
  ZBuf out;
  zbuf_init(&out);
  if (absolute) zbuf_append_char(&out, '/');
  for (size_t i = 0; i < segment_count; i++) {
    if ((absolute && out.len > 1) || (!absolute && out.len > 0)) zbuf_append_char(&out, '/');
    zbuf_append(&out, segments[i]);
  }
  if (out.len == 0) zbuf_append(&out, absolute ? "/" : ".");
  free(segments);
  free(copy);
  return out.data;
}

static bool file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static unsigned long long fs_fnv1a_text(const char *text) {
  unsigned long long hash = 1469598103934665603ull;
  for (const unsigned char *cursor = (const unsigned char *)(text ? text : ""); *cursor; cursor++) {
    hash ^= (unsigned long long)*cursor;
    hash *= 1099511628211ull;
  }
  return hash;
}

static unsigned long long fs_mix_hash_text(unsigned long long hash, const char *text) {
  hash ^= fs_fnv1a_text(text ? text : "");
  hash *= 1099511628211ull;
  return hash;
}

static void source_input_push_file(SourceInput *input, const char *path) {
  input->source_files = z_checked_reallocarray(input->source_files, input->source_file_count + 1, sizeof(char *));
  input->source_files[input->source_file_count++] = z_strdup(path);
}

static void source_input_push_source_line(SourceInput *input, const char *path, int original_line) {
  input->source_line_paths = z_checked_reallocarray(input->source_line_paths, input->source_line_count + 1, sizeof(char *));
  input->source_line_numbers = z_checked_reallocarray(input->source_line_numbers, input->source_line_count + 1, sizeof(int));
  input->source_line_paths[input->source_line_count] = z_strdup(path);
  input->source_line_numbers[input->source_line_count] = original_line > 0 ? original_line : 1;
  input->source_line_count++;
}

static void source_input_push_final_source_line(SourceInput *input) {
  if (!input || input->source_line_count == 0) return;
  source_input_push_source_line(input, input->source_line_paths[input->source_line_count - 1], input->source_line_numbers[input->source_line_count - 1]);
}

static void source_input_push_import(SourceInput *input, const char *name) {
  input->imports = z_checked_reallocarray(input->imports, input->import_count + 1, sizeof(char *));
  input->imports[input->import_count++] = z_strdup(name);
}

static void source_input_push_module(SourceInput *input, const char *name, const char *path) {
  input->module_names = z_checked_reallocarray(input->module_names, input->module_count + 1, sizeof(char *));
  input->module_paths = z_checked_reallocarray(input->module_paths, input->module_count + 1, sizeof(char *));
  input->module_names[input->module_count] = z_strdup(name);
  input->module_paths[input->module_count] = z_strdup(path);
  input->module_count++;
}

static void source_input_push_import_edge(SourceInput *input, const char *from, const char *to, const char *path, const char *source_path, int line, int column, int length) {
  input->import_from = z_checked_reallocarray(input->import_from, input->import_edge_count + 1, sizeof(char *));
  input->import_to = z_checked_reallocarray(input->import_to, input->import_edge_count + 1, sizeof(char *));
  input->import_paths = z_checked_reallocarray(input->import_paths, input->import_edge_count + 1, sizeof(char *));
  input->import_source_paths = z_checked_reallocarray(input->import_source_paths, input->import_edge_count + 1, sizeof(char *));
  input->import_lines = z_checked_reallocarray(input->import_lines, input->import_edge_count + 1, sizeof(int));
  input->import_columns = z_checked_reallocarray(input->import_columns, input->import_edge_count + 1, sizeof(int));
  input->import_lengths = z_checked_reallocarray(input->import_lengths, input->import_edge_count + 1, sizeof(int));
  input->import_from[input->import_edge_count] = z_strdup(from);
  input->import_to[input->import_edge_count] = z_strdup(to);
  input->import_paths[input->import_edge_count] = z_strdup(path ? path : "");
  input->import_source_paths[input->import_edge_count] = z_strdup(source_path ? source_path : "");
  input->import_lines[input->import_edge_count] = line > 0 ? line : 1;
  input->import_columns[input->import_edge_count] = column > 0 ? column : 1;
  input->import_lengths[input->import_edge_count] = length > 0 ? length : 1;
  input->import_edge_count++;
}

static void source_input_push_dependency(SourceInput *input, const ZManifestDependency *dep, const char *resolved_manifest, const char *resolved_name, const char *resolved_version, const char *status, bool direct) {
  input->dependencies = z_checked_reallocarray(input->dependencies, input->dependency_count + 1, sizeof(SourceDependency));
  SourceDependency *item = &input->dependencies[input->dependency_count++];
  memset(item, 0, sizeof(*item));
  item->name = z_strdup(dep && dep->name ? dep->name : "");
  item->version = z_strdup(dep && dep->version ? dep->version : "");
  item->path = z_strdup(dep && dep->path ? dep->path : "");
  item->resolved_manifest = z_strdup(resolved_manifest ? resolved_manifest : "");
  item->resolved_name = z_strdup(resolved_name ? resolved_name : (dep && dep->name ? dep->name : ""));
  item->resolved_version = z_strdup(resolved_version ? resolved_version : (dep && dep->version ? dep->version : ""));
  item->targets_json = z_strdup(dep && dep->targets_json ? dep->targets_json : "[]");
  item->status = z_strdup(status ? status : "ok");
  item->direct = direct;
  unsigned long long hash = fs_fnv1a_text("dependency");
  hash = fs_mix_hash_text(hash, item->name);
  hash = fs_mix_hash_text(hash, item->version);
  hash = fs_mix_hash_text(hash, item->path);
  hash = fs_mix_hash_text(hash, item->resolved_manifest);
  hash = fs_mix_hash_text(hash, item->resolved_name);
  hash = fs_mix_hash_text(hash, item->resolved_version);
  hash = fs_mix_hash_text(hash, item->targets_json);
  item->fingerprint = hash;
}

static const char *source_input_package_version(const SourceInput *input, const char *package_name) {
  if (!package_name || !package_name[0]) return NULL;
  if (input->package_name && strcmp(input->package_name, package_name) == 0) return input->package_version ? input->package_version : "";
  for (size_t i = 0; i < input->dependency_count; i++) {
    if (input->dependencies[i].resolved_name && strcmp(input->dependencies[i].resolved_name, package_name) == 0) {
      return input->dependencies[i].resolved_version ? input->dependencies[i].resolved_version : "";
    }
  }
  return NULL;
}

static bool source_input_push_symbol(SourceInput *input, const char *module, const char *kind, const char *name, bool is_public, ZDiag *diag) {
  if (is_public) {
    for (size_t i = 0; i < input->symbol_count; i++) {
      if (input->symbol_public[i] && strcmp(input->symbol_names[i], name) == 0 && strcmp(input->symbol_modules[i], module) != 0) {
        diag->code = 7003;
        diag->path = input->source_file;
        diag->line = 1;
        diag->column = 1;
        snprintf(diag->message, sizeof(diag->message), "duplicate public symbol '%s'", name);
        snprintf(diag->expected, sizeof(diag->expected), "unique public symbol names across imported modules");
        snprintf(diag->actual, sizeof(diag->actual), "%s also exported by %s", name, input->symbol_modules[i]);
        snprintf(diag->help, sizeof(diag->help), "rename one symbol or keep it private inside its module");
        return false;
      }
    }
  }
  input->symbol_names = z_checked_reallocarray(input->symbol_names, input->symbol_count + 1, sizeof(char *));
  input->symbol_modules = z_checked_reallocarray(input->symbol_modules, input->symbol_count + 1, sizeof(char *));
  input->symbol_kinds = z_checked_reallocarray(input->symbol_kinds, input->symbol_count + 1, sizeof(char *));
  input->symbol_public = z_checked_reallocarray(input->symbol_public, input->symbol_count + 1, sizeof(bool));
  input->symbol_names[input->symbol_count] = z_strdup(name);
  input->symbol_modules[input->symbol_count] = z_strdup(module);
  input->symbol_kinds[input->symbol_count] = z_strdup(kind ? kind : "unknown");
  input->symbol_public[input->symbol_count] = is_public;
  input->symbol_count++;
  return true;
}

static char *module_name_from_path(const char *src_root, const char *path) {
  const char *relative = path;
  size_t root_len = src_root ? strlen(src_root) : 0;
  if (src_root && strncmp(path, src_root, root_len) == 0) {
    relative = path + root_len;
    if (*relative == '/') relative++;
  } else {
    const char *slash = strrchr(path, '/');
    relative = slash ? slash + 1 : path;
  }
  size_t len = strlen(relative);
  if (len > 2 && strcmp(relative + len - 2, ".0") == 0) len -= 2;
  if (len >= 4 && strcmp(relative + len - 4, "/mod") == 0) len -= 4;
  ZBuf buf;
  zbuf_init(&buf);
  for (size_t i = 0; i < len; i++) zbuf_append_char(&buf, relative[i] == '/' ? '.' : relative[i]);
  if (buf.len == 0) zbuf_append(&buf, "main");
  return buf.data;
}

static bool source_input_has_file(SourceInput *input, const char *path) {
  for (size_t i = 0; i < input->source_file_count; i++) {
    if (strcmp(input->source_files[i], path) == 0) return true;
  }
  return false;
}

static bool import_stack_contains(char **stack, size_t len, const char *path) {
  for (size_t i = 0; i < len; i++) {
    if (strcmp(stack[i], path) == 0) return true;
  }
  return false;
}

static char *module_path_to_source(const char *src_root, const char *module_name) {
  ZBuf relative;
  zbuf_init(&relative);
  for (const char *cursor = module_name; *cursor; cursor++) {
    zbuf_append_char(&relative, *cursor == '.' ? '/' : *cursor);
  }
  zbuf_append(&relative, ".0");
  char *file_path = join_path(src_root, relative.data);
  if (file_exists(file_path)) {
    zbuf_free(&relative);
    return file_path;
  }
  free(file_path);
  if (relative.len >= 2 && strcmp(relative.data + relative.len - 2, ".0") == 0) {
    relative.data[relative.len - 2] = 0;
    relative.len -= 2;
  }
  char *dir_path = join_path(src_root, relative.data);
  char *mod_path = join_path(dir_path, "mod.0");
  free(dir_path);
  zbuf_free(&relative);
  if (file_exists(mod_path)) return mod_path;
  free(mod_path);
  return NULL;
}

static void append_source_without_imports(SourceInput *input, ZBuf *buf, const char *path, const char *source) {
  zbuf_append(buf, "// file: ");
  zbuf_append(buf, path);
  zbuf_append(buf, "\n");
  source_input_push_source_line(input, path, 1);
  const char *line = source;
  int original_line = 1;
  while (*line) {
    const char *end = strchr(line, '\n');
    size_t len = end ? (size_t)(end - line) : strlen(line);
    const char *start = line;
    while (len > 0 && isspace((unsigned char)*start)) {
      start++;
      len--;
    }
    bool is_legacy_import = len >= 7 && strncmp(start, "import ", 7) == 0;
    if (!is_legacy_import) {
      zbuf_appendf(buf, "%.*s\n", (int)(end ? (size_t)(end - line) : strlen(line)), line);
      source_input_push_source_line(input, path, original_line);
    }
    original_line++;
    if (!end) break;
    line = end + 1;
  }
  zbuf_append(buf, "\n");
  source_input_push_source_line(input, path, original_line);
}

static bool resolve_imported_source(const char *path, const char *src_root, SourceInput *input, ZBuf *combined, ZDiag *diag, char ***stack, size_t *stack_len);

static bool scan_top_level_symbols(const char *source, const char *module_name, SourceInput *input, ZDiag *diag) {
  const char *line = source;
  while (*line && diag->code == 0) {
    const char *end = strchr(line, '\n');
    size_t len = end ? (size_t)(end - line) : strlen(line);
    const char *start = line;
    while (len > 0 && isspace((unsigned char)*start)) {
      start++;
      len--;
    }
    bool is_public = false;
    if (len > 4 && strncmp(start, "pub ", 4) == 0) {
      is_public = true;
      start += 4;
      len -= 4;
      while (len > 0 && isspace((unsigned char)*start)) {
        start++;
        len--;
      }
    }
    if (len > 7 && strncmp(start, "extern ", 7) == 0) {
      start += 7;
      len -= 7;
      while (len > 0 && isspace((unsigned char)*start)) {
        start++;
        len--;
      }
    } else if (len > 7 && strncmp(start, "packed ", 7) == 0) {
      start += 7;
      len -= 7;
      while (len > 0 && isspace((unsigned char)*start)) {
        start++;
        len--;
      }
    }
    const char *after_keyword = NULL;
    const char *kind = NULL;
    const char *keywords[] = {"fun ", "shape ", "interface ", "enum ", "choice ", "const ", "type ", NULL};
    const char *kinds[] = {"function", "shape", "interface", "enum", "choice", "const", "alias", NULL};
    for (int i = 0; keywords[i]; i++) {
      size_t keyword_len = strlen(keywords[i]);
      if (len > keyword_len && strncmp(start, keywords[i], keyword_len) == 0) {
        after_keyword = start + keyword_len;
        kind = kinds[i];
        break;
      }
    }
    if (after_keyword) {
      size_t name_len = 0;
      while (after_keyword[name_len] && (isalnum((unsigned char)after_keyword[name_len]) || after_keyword[name_len] == '_')) name_len++;
      if (name_len > 0) {
        char *name = z_strndup(after_keyword, name_len);
        bool ok = source_input_push_symbol(input, module_name, kind, name, is_public, diag);
        free(name);
        if (!ok) return false;
      }
    }
    if (!end) break;
    line = end + 1;
  }
  return diag->code == 0;
}

static void format_cycle_chain(const char *src_root, char **stack, size_t stack_len, const char *path, ZBuf *out) {
  bool started = false;
  for (size_t i = 0; i < stack_len; i++) {
    if (!started && strcmp(stack[i], path) != 0) continue;
    started = true;
    if (out->len > 0) zbuf_append(out, " -> ");
    char *module = module_name_from_path(src_root, stack[i]);
    zbuf_append(out, module);
    free(module);
  }
  if (out->len > 0) zbuf_append(out, " -> ");
  char *module = module_name_from_path(src_root, path);
  zbuf_append(out, module);
  free(module);
}

static bool import_ident_start(char ch) {
  return isalpha((unsigned char)ch) || ch == '_';
}

static bool import_ident_continue(char ch) {
  return isalnum((unsigned char)ch) || ch == '_';
}

static bool import_ident_is_keyword(const char *text, size_t len) {
  const char *keywords[] = {
    "as", "break", "check", "choice", "const", "continue", "defer", "else", "enum", "export", "extern", "false", "for", "fun",
    "if", "import", "in", "let", "match", "meta", "mut", "null", "packed", "pub",
    "raise", "raises", "rescue", "return", "shape", "static", "test", "true", "type",
    "use", "var", "while", NULL
  };
  for (int i = 0; keywords[i]; i++) {
    if (strlen(keywords[i]) == len && strncmp(text, keywords[i], len) == 0) return true;
  }
  return false;
}

static bool parse_use_import_ident_segment(const char *text, size_t len, size_t *pos, const char **segment_out, size_t *segment_len_out) {
  if (*pos >= len || !import_ident_start(text[*pos])) return false;
  size_t start = *pos;
  (*pos)++;
  while (*pos < len && import_ident_continue(text[*pos])) (*pos)++;
  if (import_ident_is_keyword(text + start, *pos - start)) return false;
  if (segment_out) *segment_out = text + start;
  if (segment_len_out) *segment_len_out = *pos - start;
  return true;
}

static bool import_line_comment_at(const char *text, size_t pos, size_t len) {
  return pos + 1 < len && text[pos] == '/' && text[pos + 1] == '/';
}

static void import_line_skip_ws(const char *text, size_t len, size_t *pos) {
  while (*pos < len && isspace((unsigned char)text[*pos])) (*pos)++;
}

static void import_line_append_span(ZBuf *buf, const char *text, size_t len) {
  for (size_t i = 0; i < len; i++) zbuf_append_char(buf, text[i]);
}

static bool parse_use_import_line_module(const char *text, size_t len, char **module_out, int *length_out) {
  size_t pos = 0;
  import_line_skip_ws(text, len, &pos);
  ZBuf module;
  zbuf_init(&module);
  bool wrote_segment = false;
  size_t last_token_end = 0;
  for (;;) {
    const char *segment = NULL;
    size_t segment_len = 0;
    if (!parse_use_import_ident_segment(text, len, &pos, &segment, &segment_len)) {
      zbuf_free(&module);
      return false;
    }
    if (wrote_segment) zbuf_append_char(&module, '.');
    import_line_append_span(&module, segment, segment_len);
    wrote_segment = true;
    last_token_end = pos;
    import_line_skip_ws(text, len, &pos);
    if (pos < len && text[pos] == '.') {
      pos++;
      import_line_skip_ws(text, len, &pos);
      continue;
    }
    break;
  }
  import_line_skip_ws(text, len, &pos);
  if (import_line_comment_at(text, pos, len)) pos = len;
  if (pos < len) {
    if (pos + 2 > len || strncmp(text + pos, "as", 2) != 0) {
      zbuf_free(&module);
      return false;
    }
    pos += 2;
    if (pos >= len || !isspace((unsigned char)text[pos])) {
      zbuf_free(&module);
      return false;
    }
    import_line_skip_ws(text, len, &pos);
    if (!parse_use_import_ident_segment(text, len, &pos, NULL, NULL)) {
      zbuf_free(&module);
      return false;
    }
    last_token_end = pos;
    import_line_skip_ws(text, len, &pos);
    if (import_line_comment_at(text, pos, len)) pos = len;
    if (pos < len) {
      zbuf_free(&module);
      return false;
    }
  }
  *module_out = module.data ? module.data : z_strdup("");
  if (length_out) *length_out = 3 + (int)last_token_end;
  return true;
}

static void set_import_source_diag(ZDiag *diag, int code, const char *path, int line, int column, int length) {
  diag->code = code;
  diag->path = z_strdup(path ? path : "");
  diag->line = line > 0 ? line : 1;
  diag->column = column > 0 ? column : 1;
  diag->length = length > 0 ? length : 1;
}

static bool scan_imports_and_append_dependencies(const char *source, const char *src_root, const char *current_module, const char *current_path, SourceInput *input, ZBuf *combined, ZDiag *diag, char ***stack, size_t *stack_len) {
  const char *line = source;
  int original_line = 1;
  while (*line && diag->code == 0) {
    const char *end = strchr(line, '\n');
    size_t len = end ? (size_t)(end - line) : strlen(line);
    const char *start = line;
    while (len > 0 && isspace((unsigned char)*start)) {
      start++;
      len--;
    }
    char *module_name = NULL;
    int import_column = (int)(start - line) + 1;
    int import_length = (int)len;
    if (len > 3 && strncmp(start, "use", 3) == 0 && isspace((unsigned char)start[3])) {
      parse_use_import_line_module(start + 3, len - 3, &module_name, &import_length);
    } else if (len >= 7 && strncmp(start, "import ", 7) == 0) {
      const char *module = start + 7;
      size_t module_len = len - 7;
      while (module_len > 0 && isspace((unsigned char)module[module_len - 1])) module_len--;
      const char *as_kw = NULL;
      for (size_t i = 0; i + 4 <= module_len; i++) {
        if (strncmp(module + i, " as ", 4) == 0) {
          as_kw = module + i;
          break;
        }
      }
      if (as_kw) module_len = (size_t)(as_kw - module);
      module_name = z_strndup(module, module_len);
      import_length = 7 + (int)module_len;
    }
    if (module_name) {
      if (strncmp(module_name, "std.", 4) == 0) {
        free(module_name);
      } else {
        source_input_push_import(input, module_name);
        char *module_path = module_path_to_source(src_root, module_name);
        if (!module_path) {
          set_import_source_diag(diag, 7001, current_path, original_line, import_column, import_length);
          snprintf(diag->message, sizeof(diag->message), "unknown package-local import '%s'", module_name);
          snprintf(diag->expected, sizeof(diag->expected), "src/%s.0 or src/%s/mod.0", module_name, module_name);
          snprintf(diag->actual, sizeof(diag->actual), "missing source file");
          snprintf(diag->help, sizeof(diag->help), "create the module source file or remove the import");
          free(module_name);
          return false;
        }
        if (import_stack_contains(*stack, *stack_len, module_path)) {
          ZBuf chain;
          zbuf_init(&chain);
          format_cycle_chain(src_root, *stack, *stack_len, module_path, &chain);
          set_import_source_diag(diag, 7002, current_path, original_line, import_column, import_length);
          snprintf(diag->message, sizeof(diag->message), "import cycle detected");
          snprintf(diag->actual, sizeof(diag->actual), "%s", chain.data ? chain.data : module_path);
          snprintf(diag->help, sizeof(diag->help), "break the module cycle by moving shared declarations into a third module");
          zbuf_free(&chain);
          free(module_path);
          free(module_name);
          return false;
        }
        source_input_push_import_edge(input, current_module, module_name, module_path, current_path, original_line, import_column, import_length);
        bool ok = resolve_imported_source(module_path, src_root, input, combined, diag, stack, stack_len);
        free(module_path);
        free(module_name);
        if (!ok) return false;
      }
    }
    if (!end) break;
    line = end + 1;
    original_line++;
  }
  return diag->code == 0;
}

static bool resolve_imported_source(const char *path, const char *src_root, SourceInput *input, ZBuf *combined, ZDiag *diag, char ***stack, size_t *stack_len) {
  if (source_input_has_file(input, path)) return true;
  if (import_stack_contains(*stack, *stack_len, path)) {
    ZBuf chain;
    zbuf_init(&chain);
    format_cycle_chain(src_root, *stack, *stack_len, path, &chain);
    diag->code = 7002;
    diag->path = z_strdup(input->source_file);
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "import cycle detected");
    snprintf(diag->actual, sizeof(diag->actual), "%s", chain.data ? chain.data : path);
    snprintf(diag->help, sizeof(diag->help), "break the module cycle by moving shared declarations into a third module");
    zbuf_free(&chain);
    return false;
  }
  char *source = z_read_file(path, diag);
  if (!source) return false;
  char *module_name = module_name_from_path(src_root, path);
  *stack = z_checked_reallocarray(*stack, *stack_len + 1, sizeof(char *));
  (*stack)[(*stack_len)++] = z_strdup(path);
  bool ok = scan_imports_and_append_dependencies(source, src_root, module_name, path, input, combined, diag, stack, stack_len);
  free((*stack)[--(*stack_len)]);
  if (ok) ok = scan_top_level_symbols(source, module_name, input, diag);
  if (ok) {
    source_input_push_file(input, path);
    source_input_push_module(input, module_name, path);
    append_source_without_imports(input, combined, path, source);
  }
  free(module_name);
  free(source);
  return ok;
}

static const char *json_skip_ws(const char *cursor) {
  while (*cursor && isspace((unsigned char)*cursor)) cursor++;
  return cursor;
}

static const char *json_skip_string(const char *cursor) {
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

static char *json_parse_string_copy(const char *cursor, const char **end_out) {
  if (*cursor != '"') return NULL;
  cursor++;
  ZBuf out;
  zbuf_init(&out);
  while (*cursor) {
    if (*cursor == '"') {
      if (end_out) *end_out = cursor + 1;
      if (!out.data) zbuf_append(&out, "");
      return out.data;
    }
    if (*cursor == '\\' && cursor[1]) {
      cursor++;
      switch (*cursor) {
        case '"': zbuf_append_char(&out, '"'); break;
        case '\\': zbuf_append_char(&out, '\\'); break;
        case '/': zbuf_append_char(&out, '/'); break;
        case 'b': zbuf_append_char(&out, '\b'); break;
        case 'f': zbuf_append_char(&out, '\f'); break;
        case 'n': zbuf_append_char(&out, '\n'); break;
        case 'r': zbuf_append_char(&out, '\r'); break;
        case 't': zbuf_append_char(&out, '\t'); break;
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

static const char *json_skip_value(const char *cursor) {
  cursor = json_skip_ws(cursor);
  if (*cursor == '"') return json_skip_string(cursor);
  if (*cursor == '{') {
    cursor++;
    while (*cursor) {
      cursor = json_skip_ws(cursor);
      if (*cursor == '}') return cursor + 1;
      const char *key_end = json_skip_string(cursor);
      if (!key_end) return NULL;
      cursor = json_skip_ws(key_end);
      if (*cursor != ':') return NULL;
      cursor = json_skip_value(cursor + 1);
      if (!cursor) return NULL;
      cursor = json_skip_ws(cursor);
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
      cursor = json_skip_ws(cursor);
      if (*cursor == ']') return cursor + 1;
      cursor = json_skip_value(cursor);
      if (!cursor) return NULL;
      cursor = json_skip_ws(cursor);
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

static bool json_find_member_span(const char *object, const char *name, const char **value_start, const char **value_end) {
  const char *cursor = json_skip_ws(object);
  if (*cursor != '{') return false;
  cursor++;
  while (*cursor) {
    cursor = json_skip_ws(cursor);
    if (*cursor == '}') return false;
    const char *key_end = NULL;
    char *key = json_parse_string_copy(cursor, &key_end);
    if (!key) return false;
    cursor = json_skip_ws(key_end);
    if (*cursor != ':') {
      free(key);
      return false;
    }
    cursor = json_skip_ws(cursor + 1);
    const char *end = json_skip_value(cursor);
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
    cursor = json_skip_ws(end);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == '}') return false;
    return false;
  }
  return false;
}

static bool json_get_span_path(const char *json, const char **path, size_t path_len, const char **value_start, const char **value_end) {
  const char *start = json;
  const char *end = NULL;
  for (size_t i = 0; i < path_len; i++) {
    if (!json_find_member_span(start, path[i], &start, &end)) return false;
  }
  *value_start = start;
  *value_end = end;
  return true;
}

static char *json_get_string_path(const char *json, const char **path, size_t path_len) {
  const char *start = NULL;
  const char *end = NULL;
  if (!json_get_span_path(json, path, path_len, &start, &end)) return NULL;
  (void)end;
  return json_parse_string_copy(start, NULL);
}

static char *json_span_copy(const char *start, const char *end) {
  while (end > start && isspace((unsigned char)end[-1])) end--;
  return z_strndup(start, (size_t)(end - start));
}

static char *json_array_from_value_span(const char *start, const char *end) {
  start = json_skip_ws(start);
  if (*start == '[') return json_span_copy(start, end);
  if (*start == '"') {
    char *value = json_span_copy(start, end);
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

static char *json_get_array_path(const char *json, const char **path, size_t path_len) {
  const char *start = NULL;
  const char *end = NULL;
  if (!json_get_span_path(json, path, path_len, &start, &end)) return z_strdup("[]");
  return json_array_from_value_span(start, end);
}

static void manifest_push_c_lib(ZManifest *manifest, ZManifestCLib lib) {
  manifest->c_libs = z_checked_reallocarray(manifest->c_libs, manifest->c_lib_count + 1, sizeof(ZManifestCLib));
  manifest->c_libs[manifest->c_lib_count++] = lib;
}

static void manifest_push_dependency(ZManifest *manifest, ZManifestDependency dep) {
  manifest->dependencies = z_checked_reallocarray(manifest->dependencies, manifest->dependency_count + 1, sizeof(ZManifestDependency));
  manifest->dependencies[manifest->dependency_count++] = dep;
}

static void parse_manifest_dependencies_object(const char *deps, ZManifest *out) {
  const char *cursor = json_skip_ws(deps);
  if (*cursor != '{') return;
  cursor++;
  while (*cursor) {
    cursor = json_skip_ws(cursor);
    if (*cursor == '}') break;
    const char *key_end = NULL;
    char *name = json_parse_string_copy(cursor, &key_end);
    if (!name) break;
    cursor = json_skip_ws(key_end);
    if (*cursor != ':') {
      free(name);
      break;
    }
    cursor = json_skip_ws(cursor + 1);
    const char *value_end = json_skip_value(cursor);
    if (!value_end) {
      free(name);
      break;
    }
    ZManifestDependency dep = {0};
    dep.name = name;
    dep.targets_json = z_strdup("[]");
    if (*cursor == '"') {
      dep.version = json_parse_string_copy(cursor, NULL);
      dep.path = z_strdup("");
    } else if (*cursor == '{') {
      const char *path_path[] = {"path"};
      const char *version_path[] = {"version"};
      const char *targets_path[] = {"targets"};
      const char *target_path[] = {"target"};
      dep.path = json_get_string_path(cursor, path_path, 1);
      if (!dep.path) dep.path = z_strdup("");
      dep.version = json_get_string_path(cursor, version_path, 1);
      if (!dep.version) dep.version = z_strdup("");
      free(dep.targets_json);
      dep.targets_json = json_get_array_path(cursor, targets_path, 1);
      if (!dep.targets_json || strcmp(dep.targets_json, "[]") == 0) {
        free(dep.targets_json);
        dep.targets_json = json_get_array_path(cursor, target_path, 1);
      }
    } else {
      dep.version = z_strdup("");
      dep.path = z_strdup("");
    }
    manifest_push_dependency(out, dep);
    cursor = json_skip_ws(value_end);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == '}') break;
    break;
  }
}

bool z_parse_manifest_json(const char *manifest, ZManifest *out, ZDiag *diag) {
  memset(out, 0, sizeof(*out));
  if (*json_skip_ws(manifest) != '{') {
    if (diag) {
      diag->code = 2002;
      diag->line = 1;
      diag->column = 1;
      snprintf(diag->message, sizeof(diag->message), "zero.json must be a JSON object");
      snprintf(diag->expected, sizeof(diag->expected), "JSON object with targets.cli.main");
      snprintf(diag->help, sizeof(diag->help), "create zero.json with package and targets metadata");
    }
    return false;
  }

  const char *package_name_path[] = {"package", "name"};
  const char *package_version_path[] = {"package", "version"};
  const char *main_path[] = {"targets", "cli", "main"};
  const char *kind_path[] = {"targets", "cli", "kind"};
  out->package_name = json_get_string_path(manifest, package_name_path, 2);
  out->package_version = json_get_string_path(manifest, package_version_path, 2);
  out->main_path = json_get_string_path(manifest, main_path, 3);
  out->kind = json_get_string_path(manifest, kind_path, 3);

  const char *dependencies_path[] = {"dependencies"};
  const char *deps_alias_path[] = {"deps"};
  const char *deps = NULL;
  const char *deps_end = NULL;
  if (json_get_span_path(manifest, dependencies_path, 1, &deps, &deps_end) ||
      json_get_span_path(manifest, deps_alias_path, 1, &deps, &deps_end)) {
    parse_manifest_dependencies_object(deps, out);
    (void)deps_end;
  }

  const char *libs_path[] = {"c", "libs"};
  const char *libs = NULL;
  const char *libs_end = NULL;
  if (json_get_span_path(manifest, libs_path, 2, &libs, &libs_end)) {
    const char *cursor = json_skip_ws(libs);
    if (*cursor == '{') {
      cursor++;
      while (*cursor) {
        cursor = json_skip_ws(cursor);
        if (*cursor == '}') break;
        const char *key_end = NULL;
        char *name = json_parse_string_copy(cursor, &key_end);
        if (!name) break;
        cursor = json_skip_ws(key_end);
        if (*cursor != ':') {
          free(name);
          break;
        }
        cursor = json_skip_ws(cursor + 1);
        const char *value_end = json_skip_value(cursor);
        if (!value_end) {
          free(name);
          break;
        }
        if (*cursor == '{') {
          const char *headers_path[] = {"headers"};
          const char *include_path[] = {"include"};
          const char *lib_path[] = {"lib"};
          const char *link_path[] = {"link"};
          const char *mode_path[] = {"mode"};
          const char *pkg_config_path[] = {"pkg_config"};
          const char *pkg_config_camel_path[] = {"pkgConfig"};
          ZManifestCLib lib = {0};
          lib.name = name;
          lib.headers_json = json_get_array_path(cursor, headers_path, 1);
          lib.include_json = json_get_array_path(cursor, include_path, 1);
          lib.lib_json = json_get_array_path(cursor, lib_path, 1);
          lib.link_json = json_get_array_path(cursor, link_path, 1);
          lib.mode = json_get_string_path(cursor, mode_path, 1);
          if (!lib.mode) lib.mode = z_strdup("static");
          lib.pkg_config = json_get_string_path(cursor, pkg_config_path, 1);
          if (!lib.pkg_config) lib.pkg_config = json_get_string_path(cursor, pkg_config_camel_path, 1);
          if (!lib.pkg_config) lib.pkg_config = z_strdup("");
          manifest_push_c_lib(out, lib);
        } else {
          free(name);
        }
        cursor = json_skip_ws(value_end);
        if (*cursor == ',') {
          cursor++;
          continue;
        }
        if (*cursor == '}') break;
        break;
      }
    }
    (void)libs_end;
  }

  return true;
}

void z_free_manifest(ZManifest *manifest) {
  free(manifest->package_name);
  free(manifest->package_version);
  free(manifest->main_path);
  free(manifest->kind);
  for (size_t i = 0; i < manifest->dependency_count; i++) {
    free(manifest->dependencies[i].name);
    free(manifest->dependencies[i].version);
    free(manifest->dependencies[i].path);
    free(manifest->dependencies[i].targets_json);
  }
  for (size_t i = 0; i < manifest->c_lib_count; i++) {
    free(manifest->c_libs[i].name);
    free(manifest->c_libs[i].headers_json);
    free(manifest->c_libs[i].include_json);
    free(manifest->c_libs[i].lib_json);
    free(manifest->c_libs[i].link_json);
    free(manifest->c_libs[i].mode);
    free(manifest->c_libs[i].pkg_config);
  }
  free(manifest->dependencies);
  free(manifest->c_libs);
  memset(manifest, 0, sizeof(*manifest));
}

static char *dependency_manifest_path(const char *current_manifest_path, const char *dependency_path) {
  if (!dependency_path || !dependency_path[0]) return NULL;
  char *dep_root = NULL;
  if (dependency_path[0] == '/') dep_root = z_strdup(dependency_path);
  else {
    char *base = dirname_of(current_manifest_path);
    dep_root = join_path(base, dependency_path);
    free(base);
  }
  if (ends_with(dep_root, "zero.json")) {
    char *normalized = normalize_path_text(dep_root);
    free(dep_root);
    return normalized;
  }
  char *manifest = join_path(dep_root, "zero.json");
  free(dep_root);
  char *normalized = normalize_path_text(manifest);
  free(manifest);
  return normalized;
}

static bool dependency_stack_contains(char **stack, size_t stack_len, const char *manifest_path) {
  for (size_t i = 0; i < stack_len; i++) {
    if (strcmp(stack[i], manifest_path) == 0) return true;
  }
  return false;
}

static void set_package_diag(ZDiag *diag, int code, const char *path, const char *message, const char *expected, const char *actual, const char *help) {
  diag->code = code;
  diag->path = z_strdup(path ? path : "");
  diag->line = 1;
  diag->column = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "package dependency error");
  snprintf(diag->expected, sizeof(diag->expected), "%s", expected ? expected : "valid package dependency graph");
  snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "invalid package dependency graph");
  snprintf(diag->help, sizeof(diag->help), "%s", help ? help : "repair zero.json dependency metadata");
}

static unsigned long long source_dependency_graph_hash(const SourceInput *input) {
  unsigned long long hash = fs_fnv1a_text("zero-dependency-graph-v1");
  hash = fs_mix_hash_text(hash, input->package_name);
  hash = fs_mix_hash_text(hash, input->package_version);
  hash = fs_mix_hash_text(hash, input->manifest_path);
  for (size_t i = 0; i < input->dependency_count; i++) {
    const SourceDependency *dep = &input->dependencies[i];
    hash ^= dep->fingerprint;
    hash *= 1099511628211ull;
    hash = fs_mix_hash_text(hash, dep->status);
  }
  return hash;
}

static bool write_package_lockfile(SourceInput *input) {
  input->dependency_graph_hash = source_dependency_graph_hash(input);
  ZBuf lock;
  zbuf_init(&lock);
  zbuf_append(&lock, "{\n  \"schemaVersion\": 1,\n  \"format\": \"zero-lock-v1\",\n  \"package\": {\"name\": ");
  zbuf_append_char(&lock, '"');
  zbuf_append(&lock, input->package_name ? input->package_name : "");
  zbuf_append(&lock, "\", \"version\": \"");
  zbuf_append(&lock, input->package_version ? input->package_version : "");
  zbuf_append(&lock, "\"},\n  \"dependencyGraphHash\": \"");
  zbuf_appendf(&lock, "%016llx", input->dependency_graph_hash);
  zbuf_append(&lock, "\",\n  \"dependencies\": [");
  for (size_t i = 0; i < input->dependency_count; i++) {
    SourceDependency *dep = &input->dependencies[i];
    if (i > 0) zbuf_append(&lock, ", ");
    zbuf_append(&lock, "{\"name\":\"");
    zbuf_append(&lock, dep->name ? dep->name : "");
    zbuf_append(&lock, "\",\"version\":\"");
    zbuf_append(&lock, dep->version ? dep->version : "");
    zbuf_append(&lock, "\",\"resolvedName\":\"");
    zbuf_append(&lock, dep->resolved_name ? dep->resolved_name : "");
    zbuf_append(&lock, "\",\"resolvedVersion\":\"");
    zbuf_append(&lock, dep->resolved_version ? dep->resolved_version : "");
    zbuf_append(&lock, "\",\"status\":\"");
    zbuf_append(&lock, dep->status ? dep->status : "");
    zbuf_append(&lock, "\",\"fingerprint\":\"");
    zbuf_appendf(&lock, "%016llx", dep->fingerprint);
    zbuf_append(&lock, "\"}");
  }
  zbuf_append(&lock, "]\n}\n");
  input->lockfile_hash = fs_fnv1a_text(lock.data ? lock.data : "");
  char path[256];
  snprintf(path, sizeof(path), ".zero/package-locks/%016llx.lock.json", input->dependency_graph_hash);
  input->lockfile_path = z_strdup(path);
  ZDiag ignored = {0};
  bool ok = z_write_file(input->lockfile_path, lock.data ? lock.data : "", &ignored);
  zbuf_free(&lock);
  return ok;
}

static bool resolve_manifest_dependencies(const char *manifest_path, const ZManifest *manifest, SourceInput *out, ZDiag *diag, char ***stack, size_t *stack_len, bool direct) {
  for (size_t i = 0; i < manifest->dependency_count; i++) {
    const ZManifestDependency *dep = &manifest->dependencies[i];
    if (!dep->path || !dep->path[0]) {
      source_input_push_dependency(out, dep, "", dep->name, dep->version, "registry-reference", direct);
      continue;
    }
    char *dep_manifest_path = dependency_manifest_path(manifest_path, dep->path);
    if (!dep_manifest_path || !file_exists(dep_manifest_path)) {
      set_package_diag(diag, 9001, manifest_path, "package dependency manifest not found", "dependency path containing zero.json", dep->path, "create the dependency package or update the dependency path");
      free(dep_manifest_path);
      return false;
    }
    if (dependency_stack_contains(*stack, *stack_len, dep_manifest_path)) {
      set_package_diag(diag, 9002, manifest_path, "package dependency cycle detected", "acyclic package dependency graph", dep_manifest_path, "move shared code into a third dependency or remove the cycle");
      free(dep_manifest_path);
      return false;
    }
    char *dep_manifest_text = z_read_file(dep_manifest_path, diag);
    if (!dep_manifest_text) {
      free(dep_manifest_path);
      return false;
    }
    ZManifest parsed_dep = {0};
    if (!z_parse_manifest_json(dep_manifest_text, &parsed_dep, diag)) {
      diag->path = dep_manifest_path;
      free(dep_manifest_text);
      return false;
    }
    const char *resolved_name = parsed_dep.package_name ? parsed_dep.package_name : dep->name;
    const char *resolved_version = parsed_dep.package_version ? parsed_dep.package_version : "";
    if (dep->version && dep->version[0] && resolved_version[0] && strcmp(dep->version, resolved_version) != 0) {
      set_package_diag(diag, 9003, manifest_path, "package dependency version mismatch", dep->version, resolved_version, "update the requested dependency version or the dependency package version");
      z_free_manifest(&parsed_dep);
      free(dep_manifest_text);
      free(dep_manifest_path);
      return false;
    }
    const char *seen_version = source_input_package_version(out, resolved_name);
    if (seen_version && resolved_version[0] && strcmp(seen_version, resolved_version) != 0) {
      set_package_diag(diag, 9003, manifest_path, "package dependency version conflict", seen_version, resolved_version, "choose one version of the dependency package for this graph");
      z_free_manifest(&parsed_dep);
      free(dep_manifest_text);
      free(dep_manifest_path);
      return false;
    }
    source_input_push_dependency(out, dep, dep_manifest_path, resolved_name, resolved_version, "path-resolved", direct);
    *stack = z_checked_reallocarray(*stack, *stack_len + 1, sizeof(char *));
    (*stack)[(*stack_len)++] = z_strdup(dep_manifest_path);
    bool ok = resolve_manifest_dependencies(dep_manifest_path, &parsed_dep, out, diag, stack, stack_len, false);
    free((*stack)[--(*stack_len)]);
    z_free_manifest(&parsed_dep);
    free(dep_manifest_text);
    free(dep_manifest_path);
    if (!ok) return false;
  }
  return true;
}

bool z_resolve_source(const char *input, SourceInput *out, ZDiag *diag) {
  char *manifest_path = NULL;
  if (is_directory(input)) {
    manifest_path = join_path(input, "zero.json");
  } else if (ends_with(input, "zero.json")) {
    manifest_path = z_strdup(input);
  }

  if (manifest_path) {
    char *manifest = z_read_file(manifest_path, diag);
    if (!manifest) {
      // Keep manifest_path alive for the diagnostic; callers print it after return.
      return false;
    }
    ZManifest parsed_manifest = {0};
    if (!z_parse_manifest_json(manifest, &parsed_manifest, diag)) {
      diag->path = manifest_path;
      free(manifest);
      // Keep manifest_path alive for the diagnostic; callers print it after return.
      return false;
    }
    if (parsed_manifest.kind && strcmp(parsed_manifest.kind, "exe") != 0) {
      diag->code = 2002;
      diag->path = manifest_path;
      diag->line = 1;
      diag->column = 1;
      snprintf(diag->message, sizeof(diag->message), "unsupported target kind '%s'", parsed_manifest.kind);
      snprintf(diag->expected, sizeof(diag->expected), "targets.cli.kind = \"exe\"");
      snprintf(diag->actual, sizeof(diag->actual), "%s", parsed_manifest.kind);
      snprintf(diag->help, sizeof(diag->help), "use an exe target for the native bootstrap compiler");
      z_free_manifest(&parsed_manifest);
      free(manifest);
      // Keep manifest_path alive for the diagnostic; callers print it after return.
      return false;
    }
    if (!parsed_manifest.main_path) {
      diag->code = 2;
      diag->path = manifest_path;
      diag->line = 1;
      diag->column = 1;
      snprintf(diag->message, sizeof(diag->message), "zero.json is missing targets.cli.main");
      z_free_manifest(&parsed_manifest);
      free(manifest);
      // Keep manifest_path alive for the diagnostic; callers print it after return.
      return false;
    }
    char *dir = dirname_of(manifest_path);
    out->manifest_path = z_strdup(manifest_path);
    out->package_root = z_strdup(dir);
    out->package_name = z_strdup(parsed_manifest.package_name ? parsed_manifest.package_name : "");
    out->package_version = z_strdup(parsed_manifest.package_version ? parsed_manifest.package_version : "");
    out->manifest_hash = fs_fnv1a_text(manifest);
    char **dependency_stack = NULL;
    size_t dependency_stack_len = 0;
    dependency_stack = z_checked_reallocarray(dependency_stack, 1, sizeof(char *));
    dependency_stack[dependency_stack_len++] = z_strdup(manifest_path);
    bool deps_ok = resolve_manifest_dependencies(manifest_path, &parsed_manifest, out, diag, &dependency_stack, &dependency_stack_len, true);
    while (dependency_stack_len > 0) free(dependency_stack[--dependency_stack_len]);
    free(dependency_stack);
    if (!deps_ok) {
      free(dir);
      z_free_manifest(&parsed_manifest);
      free(manifest);
      // Keep manifest_path alive for the diagnostic; callers print it after return.
      return false;
    }
    write_package_lockfile(out);
    out->source_file = join_path(dir, parsed_manifest.main_path);
    if (!file_exists(out->source_file)) {
      diag->code = 2002;
      diag->path = manifest_path;
      diag->line = 1;
      diag->column = 1;
      snprintf(diag->message, sizeof(diag->message), "target main source does not exist");
      snprintf(diag->expected, sizeof(diag->expected), "%s", out->source_file);
      snprintf(diag->actual, sizeof(diag->actual), "missing source file");
      snprintf(diag->help, sizeof(diag->help), "create the main source file or update targets.cli.main");
      free(dir);
      z_free_manifest(&parsed_manifest);
      free(manifest);
      // Keep manifest_path alive for the diagnostic; callers print it after return.
      return false;
    }
    ZBuf source;
    zbuf_init(&source);
    char *src_dir = join_path(dir, "src");
    char **stack = NULL;
    size_t stack_len = 0;
    resolve_imported_source(out->source_file, src_dir, out, &source, diag, &stack, &stack_len);
    if (diag->code == 0) source_input_push_final_source_line(out);
    free(stack);
    free(src_dir);
    out->source = diag->code == 0 ? source.data : NULL;
    if (diag->code != 0) zbuf_free(&source);
    free(dir);
    z_free_manifest(&parsed_manifest);
    free(manifest);
    free(manifest_path);
    return out->source != NULL;
  }

  out->source_file = z_strdup(input);
  ZBuf source;
  zbuf_init(&source);
  char *dir = dirname_of(input);
  char **stack = NULL;
  size_t stack_len = 0;
  resolve_imported_source(out->source_file, dir, out, &source, diag, &stack, &stack_len);
  if (diag->code == 0) source_input_push_final_source_line(out);
  free(stack);
  free(dir);
  out->source = diag->code == 0 ? source.data : NULL;
  if (diag->code != 0) zbuf_free(&source);
  return out->source != NULL;
}

bool z_map_source_diag(const SourceInput *input, ZDiag *diag) {
  if (!input || !diag || diag->line <= 0 || input->source_line_count == 0) return false;
  size_t index = (size_t)diag->line - 1;
  if (index >= input->source_line_count) return false;
  diag->path = input->source_line_paths[index];
  diag->line = input->source_line_numbers[index] > 0 ? input->source_line_numbers[index] : 1;
  for (size_t i = 0; i < diag->borrow_trace_count; i++) {
    ZBorrowTrace *trace = &diag->borrow_traces[i];
    if (trace->binding_line <= 0) continue;
    size_t binding_index = (size_t)trace->binding_line - 1;
    if (binding_index >= input->source_line_count) continue;
    trace->binding_decl_path = input->source_line_paths[binding_index];
    trace->binding_line = input->source_line_numbers[binding_index] > 0 ? input->source_line_numbers[binding_index] : 1;
  }
  return true;
}

void z_free_source(SourceInput *input) {
  free(input->source_file);
  free(input->source);
  free(input->package_root);
  free(input->manifest_path);
  free(input->package_name);
  free(input->package_version);
  free(input->lockfile_path);
  for (size_t i = 0; i < input->source_file_count; i++) free(input->source_files[i]);
  for (size_t i = 0; i < input->source_line_count; i++) free(input->source_line_paths[i]);
  for (size_t i = 0; i < input->import_count; i++) free(input->imports[i]);
  for (size_t i = 0; i < input->module_count; i++) {
    free(input->module_names[i]);
    free(input->module_paths[i]);
  }
  for (size_t i = 0; i < input->import_edge_count; i++) {
    free(input->import_from[i]);
    free(input->import_to[i]);
    free(input->import_paths[i]);
    free(input->import_source_paths[i]);
  }
  for (size_t i = 0; i < input->symbol_count; i++) {
    free(input->symbol_names[i]);
    free(input->symbol_modules[i]);
    free(input->symbol_kinds[i]);
  }
  for (size_t i = 0; i < input->dependency_count; i++) {
    free(input->dependencies[i].name);
    free(input->dependencies[i].version);
    free(input->dependencies[i].path);
    free(input->dependencies[i].resolved_manifest);
    free(input->dependencies[i].resolved_name);
    free(input->dependencies[i].resolved_version);
    free(input->dependencies[i].targets_json);
    free(input->dependencies[i].status);
  }
  free(input->source_files);
  free(input->source_line_paths);
  free(input->source_line_numbers);
  free(input->imports);
  free(input->module_names);
  free(input->module_paths);
  free(input->import_from);
  free(input->import_to);
  free(input->import_paths);
  free(input->import_source_paths);
  free(input->import_lines);
  free(input->import_columns);
  free(input->import_lengths);
  free(input->symbol_names);
  free(input->symbol_modules);
  free(input->symbol_kinds);
  free(input->dependencies);
  free(input->symbol_public);
}

char *z_default_out_path(const char *source_file) {
  const char *slash = strrchr(source_file, '/');
  const char *base = slash ? slash + 1 : source_file;
  const char *dot = strrchr(base, '.');
  size_t len = dot ? (size_t)(dot - base) : strlen(base);
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, ".zero/out/");
  for (size_t i = 0; i < len; i++) zbuf_append_char(&buf, base[i]);
  return buf.data;
}

static bool command_exists(const char *command) {
  ZBuf probe;
  zbuf_init(&probe);
  zbuf_appendf(&probe, "command -v '%s' >/dev/null 2>&1", command);
  bool ok = system(probe.data) == 0;
  zbuf_free(&probe);
  return ok;
}

static bool dir_exists_for_cc(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool profile_should_strip_artifact(const char *profile);

static bool target_uses_emscripten(const ZTargetInfo *target) {
  return target &&
         ((target->linker && (strcmp(target->linker, "emcc") == 0 || strcmp(target->linker, "emscripten") == 0)) ||
          (target->libc_mode && strcmp(target->libc_mode, "emscripten") == 0));
}

static const char *sysroot_status_for(const ZTargetInfo *target, const char *env_name, const char *sysroot) {
  if (!z_target_requires_sysroot(target)) return "not-required";
  if (!env_name || !env_name[0] || !sysroot || !sysroot[0]) return "missing";
  if (strstr(sysroot, "/usr/include") || strstr(sysroot, "/usr/lib")) return "host-leakage";
  if (!dir_exists_for_cc(sysroot)) return "missing";
  return "present";
}

ZToolchainPlan z_plan_toolchain(const char *cc, const char *profile, const ZTargetInfo *target) {
  const char *cli_override = cc && cc[0] ? cc : NULL;
  const char *env_override = getenv("ZERO_CC");
  if (env_override && !env_override[0]) env_override = NULL;
  bool host_target = !target || z_target_is_host(target);
  bool emscripten_target = target_uses_emscripten(target);
  const char *sysroot_env = target && z_target_requires_sysroot(target) ? z_target_sysroot_env_name(target) : "";
  const char *sysroot = sysroot_env && sysroot_env[0] ? getenv(sysroot_env) : NULL;
  const char *sysroot_status = sysroot_status_for(target, sysroot_env, sysroot);
  ZToolchainPlan plan = {
    .driver_kind = "host-cc",
    .selection_source = "host-default",
    .compiler = "cc",
    .target_triple = target && target->zig_target ? target->zig_target : z_host_target(),
    .linker_flavor = target && target->linker ? target->linker : "cc",
    .libc_mode = target ? z_target_libc_mode(target) : "host-default",
    .sysroot_env = sysroot_env ? sysroot_env : "",
    .sysroot_path = sysroot ? sysroot : "",
    .sysroot_status = sysroot_status,
    .requires_sysroot = z_target_requires_sysroot(target),
    .uses_target_flag = false,
    .uses_zig_cache = false,
    .strip_artifact = profile_should_strip_artifact(profile) && host_target
  };

  if (cli_override) {
    plan.driver_kind = "override-cc";
    plan.selection_source = "cli";
    plan.compiler = cli_override;
    return plan;
  }

  if (env_override) {
    plan.driver_kind = "override-cc";
    plan.selection_source = "env";
    plan.compiler = env_override;
    return plan;
  }

  if (emscripten_target) {
    plan.driver_kind = "emcc";
    plan.selection_source = "target-manifest";
    plan.compiler = "emcc";
    plan.uses_target_flag = false;
    plan.strip_artifact = false;
    return plan;
  }

  if (!host_target) {
    plan.driver_kind = "zig-cc";
    plan.selection_source = "target-manifest";
    plan.compiler = "zig cc";
    plan.uses_target_flag = true;
    plan.uses_zig_cache = true;
    plan.strip_artifact = false;
  }

  return plan;
}

static bool validate_toolchain_plan(const ZToolchainPlan *plan, const ZTargetInfo *target) {
  if (strcmp(plan->driver_kind, "override-cc") == 0) return true;

  if (plan->requires_sysroot && strcmp(plan->sysroot_status, "present") != 0) {
    if (strcmp(plan->sysroot_status, "host-leakage") == 0) {
      fprintf(stderr, "target '%s' sysroot points at host headers/libs; refusing host header leakage from %s\n", target->name, plan->sysroot_path);
    } else if (plan->sysroot_path && plan->sysroot_path[0]) {
      fprintf(stderr, "target '%s' sysroot does not exist: %s\n", target->name, plan->sysroot_path);
    } else {
      fprintf(stderr, "target '%s' requires sysroot mode; set %s to the target SDK/sysroot\n", target->name, plan->sysroot_env);
    }
    return false;
  }

  if (strcmp(plan->driver_kind, "zig-cc") == 0 && !command_exists("zig")) {
    fprintf(stderr, "cross target '%s' requires a target-capable C compiler; pass --cc/ZERO_CC or install the bundled-toolchain default\n", target->name);
    return false;
  }

  if (strcmp(plan->driver_kind, "emcc") == 0 && !command_exists("emcc")) {
    fprintf(stderr, "target '%s' requires emcc; install Emscripten or pass --cc/ZERO_CC for a browser-capable C compiler\n", target->name);
    return false;
  }

  if (strcmp(plan->driver_kind, "host-cc") == 0 && !command_exists("cc")) {
    fprintf(stderr, "host target requires cc; install a native C compiler or pass --cc/ZERO_CC\n");
    return false;
  }

  return true;
}

static const char *profile_c_flags(const char *profile) {
  if (!profile || strcmp(profile, "release") == 0 || strcmp(profile, "release-small") == 0 || strcmp(profile, "small") == 0) return "-Os -DNDEBUG";
  if (strcmp(profile, "tiny") == 0) return "-Os -DNDEBUG";
  if (strcmp(profile, "release-fast") == 0 || strcmp(profile, "fast") == 0) return "-O2 -DNDEBUG";
  if (strcmp(profile, "debug") == 0 || strcmp(profile, "dev") == 0) return "-O0 -g -DZERO_DEBUG";
  if (strcmp(profile, "audit") == 0) return "-O0 -g -DZERO_AUDIT";
  return "-Os -DNDEBUG";
}

static bool profile_should_strip_artifact(const char *profile) {
  return !profile || strcmp(profile, "release") == 0 || strcmp(profile, "release-small") == 0 || strcmp(profile, "small") == 0 || strcmp(profile, "tiny") == 0;
}

static void append_toolchain_driver_command(ZBuf *cmd, const ZToolchainPlan *plan) {
  if (strcmp(plan->driver_kind, "override-cc") == 0) {
    zbuf_appendf(cmd, "'%s'", plan->compiler);
  } else if (strcmp(plan->driver_kind, "host-cc") == 0) {
    zbuf_append(cmd, "cc");
  } else if (strcmp(plan->driver_kind, "emcc") == 0) {
    zbuf_append(cmd, "emcc");
  } else {
    zbuf_append(cmd, "mkdir -p .zero/zig-global-cache .zero/zig-local-cache && ZIG_GLOBAL_CACHE_DIR=.zero/zig-global-cache ZIG_LOCAL_CACHE_DIR=.zero/zig-local-cache zig cc");
    zbuf_appendf(cmd, " -target '%s'", plan->target_triple);
  }
}

bool z_toolchain_compile_c_object(const ZToolchainPlan *plan, const char *profile, const ZTargetInfo *target, const char *c_file, const char *object_file, const char *include_dir, const char *extra_c_flags) {
  if (!validate_toolchain_plan(plan, target)) return false;

  ZBuf cmd;
  zbuf_init(&cmd);
  append_toolchain_driver_command(&cmd, plan);
  zbuf_appendf(&cmd, " %s", profile_c_flags(profile));
  if (extra_c_flags && extra_c_flags[0]) zbuf_appendf(&cmd, " %s", extra_c_flags);
  if (include_dir && include_dir[0]) zbuf_appendf(&cmd, " -I '%s'", include_dir);
  zbuf_appendf(&cmd, " -c '%s' -o '%s'", c_file, object_file);
  bool ok = system(cmd.data) == 0;
  zbuf_free(&cmd);
  return ok;
}

bool z_toolchain_link_objects(const ZToolchainPlan *plan, const ZTargetInfo *target, const char *const *object_files, size_t object_count, const char *exe_file, const char *pre_link_flags, const char *post_object_flags) {
  if (!validate_toolchain_plan(plan, target)) return false;

  ZBuf cmd;
  zbuf_init(&cmd);
  append_toolchain_driver_command(&cmd, plan);
  if (pre_link_flags && pre_link_flags[0]) zbuf_appendf(&cmd, " %s", pre_link_flags);
  for (size_t i = 0; i < object_count; i++) {
    if (object_files[i] && object_files[i][0]) zbuf_appendf(&cmd, " '%s'", object_files[i]);
  }
  zbuf_appendf(&cmd, " -o '%s'", exe_file);
  if (post_object_flags && post_object_flags[0]) zbuf_appendf(&cmd, " %s", post_object_flags);
  bool ok = system(cmd.data) == 0;
  zbuf_free(&cmd);
  return ok;
}

bool z_run_cc(const char *c_file, const char *exe_file, const char *cc, const char *profile, const ZTargetInfo *target) {
  ZToolchainPlan plan = z_plan_toolchain(cc, profile, target);
  if (!validate_toolchain_plan(&plan, target)) return false;

  ZBuf cmd;
  zbuf_init(&cmd);
  append_toolchain_driver_command(&cmd, &plan);
  zbuf_appendf(&cmd, " %s '%s' -o '%s'", profile_c_flags(profile), c_file, exe_file);
  bool ok = system(cmd.data) == 0;
  zbuf_free(&cmd);
  if (!ok) {
    fprintf(
      stderr,
      "toolchain '%s' failed for target '%s' (%s selected by %s)\n",
      plan.compiler,
      target && target->name ? target->name : z_host_target(),
      plan.driver_kind,
      plan.selection_source
    );
  }
  if (ok && plan.strip_artifact && command_exists("strip")) {
    ZBuf strip_cmd;
    zbuf_init(&strip_cmd);
    zbuf_appendf(&strip_cmd, "strip '%s' >/dev/null 2>&1 || true", exe_file);
    system(strip_cmd.data);
    zbuf_free(&strip_cmd);
  }
  return ok;
}
