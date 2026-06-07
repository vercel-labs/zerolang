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
  else if (enabled) *enabled = parsed_manifest.repository_graph_compiler_input_present && parsed_manifest.repository_graph_compiler_input;
  z_free_manifest(&parsed_manifest);
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
