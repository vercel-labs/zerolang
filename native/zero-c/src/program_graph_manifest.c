#include "program_graph_manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  ZManifest parsed_manifest = {0};
  bool ok = z_parse_manifest_json(manifest, &parsed_manifest, diag);
  if (!ok && diag && !diag->path) diag->path = z_strdup(manifest_path);
  else if (enabled) *enabled = true;
  z_free_manifest(&parsed_manifest);
  free(manifest);
  free(manifest_path);
  return ok;
}

bool z_program_graph_manifest_command_can_use_compiler_input(const char *command) {
  static const char *const graph_commands[] = {"check", "build", "run", "test", "size", "mem", "doc", "dev", "time", "abi", "fix"};
  for (size_t i = 0; command && i < sizeof(graph_commands) / sizeof(graph_commands[0]); i++) {
    if (strcmp(command, graph_commands[i]) == 0) return true;
  }
  return false;
}

static char *pgm_dirname(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  if (!slash) return z_strdup(".");
  if (slash == path) return z_strdup("/");
  return z_strndup(path, (size_t)(slash - path));
}

static unsigned long long pgm_fnv1a_text(const char *text) {
  unsigned long long hash = 1469598103934665603ull;
  for (const unsigned char *cursor = (const unsigned char *)(text ? text : ""); *cursor; cursor++) {
    hash ^= (unsigned long long)*cursor;
    hash *= 1099511628211ull;
  }
  return hash;
}

static bool pgm_manifest_kind_ok(const char *manifest_path, const ZManifest *parsed_manifest, ZDiag *diag) {
  if (!parsed_manifest || !parsed_manifest->kind || strcmp(parsed_manifest->kind, "exe") == 0) return true;
  if (diag) {
    diag->code = 2002;
    diag->path = z_strdup(manifest_path ? manifest_path : "zero.toml or zero.json");
    diag->line = 1;
    diag->column = 1;
    snprintf(diag->message, sizeof(diag->message), "unsupported target kind '%s'", parsed_manifest->kind);
    snprintf(diag->expected, sizeof(diag->expected), "targets.cli.kind = \"exe\"");
    snprintf(diag->actual, sizeof(diag->actual), "%s", parsed_manifest->kind);
    snprintf(diag->help, sizeof(diag->help), "use an exe target for the native bootstrap compiler");
  }
  return false;
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

static void pgm_attach_dependency_free_cache_metadata(SourceInput *input, const char *manifest_path, const char *manifest, const ZManifest *parsed_manifest) {
  pgm_clear_source_package_metadata(input);
  input->manifest_path = z_strdup(manifest_path ? manifest_path : "");
  input->package_root = pgm_dirname(manifest_path);
  input->package_name = z_strdup(parsed_manifest && parsed_manifest->package_name ? parsed_manifest->package_name : "");
  input->package_version = z_strdup(parsed_manifest && parsed_manifest->package_version ? parsed_manifest->package_version : "");
  input->manifest_hash = pgm_fnv1a_text(manifest);
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

bool z_program_graph_manifest_attach_cache_metadata_to_input(SourceInput *input, const char *manifest_input, bool *needs_c_library_validation, ZDiag *diag) {
  if (needs_c_library_validation) *needs_c_library_validation = true;
  char *manifest_path = z_manifest_path_for_input(manifest_input);
  if (!manifest_path) {
    if (needs_c_library_validation) *needs_c_library_validation = false;
    return true;
  }

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
  if (!pgm_manifest_kind_ok(manifest_path, &parsed_manifest, diag)) {
    z_free_manifest(&parsed_manifest);
    free(manifest);
    free(manifest_path);
    return false;
  }

  if (parsed_manifest.dependency_count == 0 && parsed_manifest.c_lib_count == 0) {
    pgm_attach_dependency_free_cache_metadata(input, manifest_path, manifest, &parsed_manifest);
    if (needs_c_library_validation) *needs_c_library_validation = false;
    z_free_manifest(&parsed_manifest);
    free(manifest);
    free(manifest_path);
    return true;
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
