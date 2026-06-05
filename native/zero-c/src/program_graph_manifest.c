#include "program_graph_manifest.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *pgm_skip_ws(const char *cursor) {
  while (cursor && isspace((unsigned char)*cursor)) cursor++;
  return cursor;
}

static const char *pgm_skip_string(const char *cursor) {
  if (!cursor || *cursor != '"') return NULL;
  for (cursor++; *cursor; cursor++) {
    if (*cursor == '\\') {
      if (!cursor[1]) return NULL;
      cursor++;
      continue;
    }
    if (*cursor == '"') return cursor + 1;
  }
  return NULL;
}

static const char *pgm_skip_value(const char *cursor) {
  cursor = pgm_skip_ws(cursor);
  if (!cursor || !*cursor) return NULL;
  if (*cursor == '"') return pgm_skip_string(cursor);
  if (*cursor == '{') {
    for (cursor++; *cursor;) {
      cursor = pgm_skip_ws(cursor);
      if (*cursor == '}') return cursor + 1;
      const char *key_end = pgm_skip_string(cursor);
      if (!key_end) return NULL;
      cursor = pgm_skip_ws(key_end);
      if (*cursor != ':') return NULL;
      cursor = pgm_skip_value(cursor + 1);
      if (!cursor) return NULL;
      cursor = pgm_skip_ws(cursor);
      if (*cursor == ',') { cursor++; continue; }
      if (*cursor == '}') return cursor + 1;
      return NULL;
    }
    return NULL;
  }
  if (*cursor == '[') {
    for (cursor++; *cursor;) {
      cursor = pgm_skip_ws(cursor);
      if (*cursor == ']') return cursor + 1;
      cursor = pgm_skip_value(cursor);
      if (!cursor) return NULL;
      cursor = pgm_skip_ws(cursor);
      if (*cursor == ',') { cursor++; continue; }
      if (*cursor == ']') return cursor + 1;
      return NULL;
    }
    return NULL;
  }
  while (*cursor && *cursor != ',' && *cursor != '}' && *cursor != ']') cursor++;
  return cursor;
}

static bool pgm_find_member_span(const char *object, const char *name, const char **start, const char **end) {
  const char *cursor = pgm_skip_ws(object);
  if (!cursor || *cursor != '{') return false;
  for (cursor++; *cursor;) {
    cursor = pgm_skip_ws(cursor);
    if (*cursor == '}') return false;
    const char *key_start = cursor + 1;
    const char *key_end = pgm_skip_string(cursor);
    if (!key_end) return false;
    size_t len = (size_t)((key_end - 1) - key_start);
    cursor = pgm_skip_ws(key_end);
    if (*cursor != ':') return false;
    cursor = pgm_skip_ws(cursor + 1);
    const char *value_end = pgm_skip_value(cursor);
    if (!value_end) return false;
    if (strlen(name) == len && strncmp(key_start, name, len) == 0) {
      *start = cursor;
      *end = value_end;
      return true;
    }
    cursor = pgm_skip_ws(value_end);
    if (*cursor == ',') { cursor++; continue; }
    if (*cursor == '}') return false;
    return false;
  }
  return false;
}

static bool pgm_bool_field(const char *json, const char *parent, const char *field, bool *present, bool *value) {
  if (present) *present = false;
  if (value) *value = false;
  const char *object = NULL;
  const char *object_end = NULL;
  if (!pgm_find_member_span(json, parent, &object, &object_end)) return true;
  (void)object_end;
  const char *start = NULL;
  const char *end = NULL;
  if (!pgm_find_member_span(object, field, &start, &end)) return true;
  if (present) *present = true;
  start = pgm_skip_ws(start);
  while (end > start && isspace((unsigned char)end[-1])) end--;
  size_t len = (size_t)(end - start);
  if (len == 4 && strncmp(start, "true", 4) == 0) {
    if (value) *value = true;
    return true;
  }
  if (len == 5 && strncmp(start, "false", 5) == 0) return true;
  return false;
}

static void pgm_bool_diag(ZDiag *diag, const char *path) {
  if (!diag) return;
  diag->code = 2002;
  diag->path = z_strdup(path ? path : "");
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "repositoryGraph.compilerInput must be a boolean");
  snprintf(diag->expected, sizeof(diag->expected), "true or false");
  snprintf(diag->actual, sizeof(diag->actual), "non-boolean repositoryGraph.compilerInput");
  snprintf(diag->help, sizeof(diag->help), "set repositoryGraph.compilerInput to true only when a valid zero.graph store is checked in");
}

bool z_program_graph_manifest_compiler_input_enabled(const char *input_path, bool *enabled, ZDiag *diag) {
  if (enabled) *enabled = false;
  char *manifest_path = z_manifest_path_for_input(input_path);
  if (!manifest_path) return true;
  char *manifest = z_read_file(manifest_path, diag);
  if (!manifest) {
    if (diag) diag->path = z_strdup(manifest_path);
    free(manifest_path);
    return false;
  }
  bool present = false;
  bool value = false;
  bool ok = pgm_bool_field(manifest, "repositoryGraph", "compilerInput", &present, &value);
  if (!ok) pgm_bool_diag(diag, manifest_path);
  else if (enabled) *enabled = present && value;
  free(manifest);
  free(manifest_path);
  return ok;
}

bool z_program_graph_manifest_command_can_use_compiler_input(const char *command) {
  return command &&
         (strcmp(command, "check") == 0 ||
          strcmp(command, "build") == 0 ||
          strcmp(command, "run") == 0 ||
          strcmp(command, "test") == 0 ||
          strcmp(command, "size") == 0 ||
          strcmp(command, "ship") == 0 ||
          strcmp(command, "mem") == 0);
}

static void pgm_clear_source_package_metadata(SourceInput *input) {
  if (!input) return;
  free(input->package_root);
  free(input->manifest_path);
  free(input->package_name);
  free(input->package_version);
  free(input->lockfile_path);
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
  free(input->dependencies);
  input->package_root = NULL;
  input->manifest_path = NULL;
  input->package_name = NULL;
  input->package_version = NULL;
  input->lockfile_path = NULL;
  input->manifest_hash = 0;
  input->dependency_graph_hash = 0;
  input->lockfile_hash = 0;
  input->dependencies = NULL;
  input->dependency_count = 0;
}

static void pgm_move_source_package_metadata(SourceInput *input, SourceInput *metadata) {
  if (!input || !metadata) return;
  pgm_clear_source_package_metadata(input);
  input->package_root = metadata->package_root; metadata->package_root = NULL;
  input->manifest_path = metadata->manifest_path; metadata->manifest_path = NULL;
  input->package_name = metadata->package_name; metadata->package_name = NULL;
  input->package_version = metadata->package_version; metadata->package_version = NULL;
  input->lockfile_path = metadata->lockfile_path; metadata->lockfile_path = NULL;
  input->manifest_hash = metadata->manifest_hash; metadata->manifest_hash = 0;
  input->dependency_graph_hash = metadata->dependency_graph_hash; metadata->dependency_graph_hash = 0;
  input->lockfile_hash = metadata->lockfile_hash; metadata->lockfile_hash = 0;
  input->dependencies = metadata->dependencies; metadata->dependencies = NULL;
  input->dependency_count = metadata->dependency_count; metadata->dependency_count = 0;
}

bool z_program_graph_manifest_attach_metadata_to_input(SourceInput *input, const char *manifest_input, ZDiag *diag) {
  char *manifest_path = z_manifest_path_for_input(manifest_input);
  if (!manifest_path) return true;

  char *manifest = z_read_file(manifest_path, diag);
  if (!manifest) {
    if (diag && !diag->path) diag->path = z_strdup(manifest_path);
    free(manifest_path);
    return false;
  }

  ZManifest parsed_manifest = {0};
  if (!z_parse_manifest_json(manifest, &parsed_manifest, diag)) {
    if (diag && !diag->path) diag->path = z_strdup(manifest_path);
    z_free_manifest(&parsed_manifest);
    free(manifest);
    free(manifest_path);
    return false;
  }

  SourceInput metadata = {0};
  bool ok = z_resolve_package_metadata(manifest_path, manifest, &parsed_manifest, &metadata, diag);
  if (ok) pgm_move_source_package_metadata(input, &metadata);
  z_free_source(&metadata);
  z_free_manifest(&parsed_manifest);
  free(manifest);
  free(manifest_path);
  return ok;
}
