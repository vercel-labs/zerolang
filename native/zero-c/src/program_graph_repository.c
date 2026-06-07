#include "program_graph_repository.h"

#include "program_graph_manifest.h"
#include "program_graph_projection.h"
#include "program_graph_repository_repair.h"
#include "program_graph_repository_merge.h"
#include "program_graph_store.h"
#include "program_graph_store_tables.h"
#include "zero.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPO_KIND(kind, literal) ((kind) && memcmp((kind), (literal), sizeof(literal)) == 0)

typedef struct {
  const char *input;
  char *root;
  char *store_path;
  bool store_present;
  bool store_valid;
  ZProgramGraphStoreFormat store_format;
  char *store_error;
  char *projection_error;
  char *graph_hash;
  char *module_identity;
  char *expected_module_identity;
  char *module_identity_error;
  char *expected_main_path;
  char *target_main_error;
  size_t node_count;
  size_t edge_count;
  size_t source_count;
  bool projection_checked;
  bool projection_current;
  bool projection_missing;
  bool compiler_input_valid;
  bool compiler_input_enabled;
  bool compiler_store_available;
  ZProgramGraphStoreTableCounts compiler_tables;
  ZDiag compiler_input_diag;
} RepositoryGraphState;

static const char *repo_strip_dot_slash(const char *path) {
  while (path && path[0] == '.' && path[1] == '/') path += 2;
  return path ? path : "";
}

static bool repo_paths_equal(const char *left, const char *right) {
  return strcmp(repo_strip_dot_slash(left), repo_strip_dot_slash(right)) == 0;
}

static bool repo_store_has_source_path(const ZProgramGraphStore *store, const char *path) {
  for (size_t i = 0; store && path && path[0] && i < store->source_path_len; i++) {
    if (repo_paths_equal(store->source_paths[i], path)) return true;
  }
  return false;
}

static RepositoryGraphState repo_graph_state(const char *input, const ZTargetInfo *target) {
  RepositoryGraphState state = {.input = input && input[0] ? input : ".", .compiler_input_valid = true};
  state.root = z_program_graph_store_root_for_input(state.input);
  state.store_path = z_program_graph_store_path_for_root(state.root);
  state.store_present = z_program_graph_store_file_exists(state.store_path);
  ZDiag compiler_input_diag = {0};
  state.compiler_input_valid = z_program_graph_manifest_compiler_input_enabled(state.input, &state.compiler_input_enabled, &compiler_input_diag);
  if (!state.compiler_input_valid) state.compiler_input_diag = compiler_input_diag;
  if (state.store_present) {
    ZProgramGraphStore store;
    ZDiag diag = {0};
    if (z_program_graph_store_load_path(state.store_path, &store, &diag)) {
      state.store_valid = true;
      state.store_format = store.format;
      state.graph_hash = z_strdup(store.graph.graph_hash ? store.graph.graph_hash : "");
      state.module_identity = z_strdup(store.graph.module_identity ? store.graph.module_identity : "");
      state.node_count = store.graph.node_len;
      state.edge_count = store.graph.edge_len;
      state.source_count = store.source_path_len;
      state.compiler_store_available = true;
      char *expected_identity = NULL;
      ZDiag identity_diag = {0};
      if (z_program_graph_manifest_module_identity(state.input, &expected_identity, &identity_diag)) {
        state.expected_module_identity = z_strdup(expected_identity ? expected_identity : "");
        if (expected_identity && strcmp(expected_identity, store.graph.module_identity ? store.graph.module_identity : "") != 0) {
          state.module_identity_error = z_strdup("repository graph store module identity does not match package manifest");
        }
      }
      free(expected_identity);
      free((char *)identity_diag.path);
      char *expected_main_path = NULL;
      ZDiag main_path_diag = {0};
      if (z_program_graph_manifest_main_path(state.input, &expected_main_path, &main_path_diag)) {
        if (expected_main_path && expected_main_path[0] && !repo_store_has_source_path(&store, expected_main_path)) {
          state.expected_main_path = z_strdup(expected_main_path);
          state.target_main_error = z_strdup("repository graph store target main does not match package manifest");
        }
      }
      free(expected_main_path);
      free((char *)main_path_diag.path);
      z_program_graph_store_table_counts_for_graph(&store.graph, store.source_path_len, store.projection_len, &state.compiler_tables);
      ZDiag projection_diag = {0};
      state.projection_missing = z_program_graph_projection_sources_missing(&store);
      z_program_graph_projection_state_label(&store, target, &state.projection_checked, &state.projection_current, &projection_diag);
      if (!state.projection_checked && projection_diag.message[0]) state.projection_error = z_strdup(projection_diag.message);
      z_program_graph_store_free(&store);
    } else {
      state.store_error = z_strdup(diag.message[0] ? diag.message : "invalid zero.graph");
    }
  }
  return state;
}

static void repo_graph_state_free(RepositoryGraphState *state) {
  if (!state) return;
  free(state->root);
  free(state->store_path);
  free(state->store_error);
  free(state->projection_error);
  free(state->graph_hash);
  free(state->module_identity);
  free(state->expected_module_identity);
  free(state->module_identity_error);
  free(state->expected_main_path);
  free(state->target_main_error);
  free((char *)state->compiler_input_diag.path);
  memset(state, 0, sizeof(*state));
}

static void repo_append_json_string(ZBuf *buf, const char *text) {
  zbuf_append_char(buf, '"');
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
    switch (*p) {
      case '\\': zbuf_append(buf, "\\\\"); break;
      case '"': zbuf_append(buf, "\\\""); break;
      case '\n': zbuf_append(buf, "\\n"); break;
      case '\r': zbuf_append(buf, "\\r"); break;
      case '\t': zbuf_append(buf, "\\t"); break;
      default:
        if (*p < 0x20) zbuf_appendf(buf, "\\u%04x", *p);
        else zbuf_append_char(buf, (char)*p);
        break;
    }
  }
  zbuf_append_char(buf, '"');
}

static char *repo_command_text(const char *command, const char *input) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, command);
  zbuf_append_char(&buf, ' ');
  zbuf_append(&buf, input && input[0] ? input : ".");
  return buf.data;
}

static const char *repo_projection_state(const RepositoryGraphState *state) {
  if (state && state->store_present && state->store_valid && state->module_identity_error) return "conflict";
  if (state && state->store_present && state->store_valid && state->target_main_error) return "conflict";
  if (state && state->store_present && state->store_valid && state->projection_error) return "conflict";
  if (state && state->store_present && state->store_valid && state->projection_missing) return "source-missing";
  if (state && state->store_present && state->store_valid && state->projection_checked && !state->projection_current) return "source-stale";
  if (state && state->store_present && state->store_valid && state->projection_checked) return state->projection_current ? "clean" : "source-stale";
  if (state && state->store_present && state->store_valid) return "store-valid";
  if (state && state->store_present) return "store-invalid";
  return "not-enabled";
}

static const char *repo_compiler_input_label(const RepositoryGraphState *state) {
  if (!state || !state->compiler_input_valid) return "invalid";
  return state->compiler_input_enabled ? "repository-graph" : "source-text";
}

static bool repo_requested_store_format(const char *format_name, ZProgramGraphStoreFormat fallback, ZProgramGraphStoreFormat *out, ZDiag *diag) {
  if (!format_name || !format_name[0]) {
    if (out) *out = fallback;
    return true;
  }
  if (z_program_graph_store_format_from_name(format_name, out)) return true;
  if (diag) {
    *diag = (ZDiag){0};
    diag->code = 2002;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "repository graph store format is not supported");
    snprintf(diag->expected, sizeof(diag->expected), "--format text|binary");
    snprintf(diag->actual, sizeof(diag->actual), "%s", format_name);
    snprintf(diag->help, sizeof(diag->help), "omit --format for the binary zero.graph default, or pass --format text for a readable debug store");
  }
  return false;
}

static const char *repo_semantic_validity_label(const RepositoryGraphState *state) {
  return state && state->store_valid ? "shape-valid" : "unavailable";
}

static const char *repo_projection_validity_label(const RepositoryGraphState *state) {
  if (!state || !state->store_valid) return "unavailable";
  if (state->projection_error) return "conflict";
  if (state->projection_missing) return "missing";
  if (!state->projection_checked) return "unchecked";
  return state->projection_current ? "clean" : "stale";
}

static const char *repo_diag_code(const ZDiag *diag) {
  if (diag && diag->code == 2002) return "BLD002";
  return "RGP008";
}

static const char *repo_diagnostic_path(const RepositoryGraphState *state) {
  if (state && !state->compiler_input_valid && state->compiler_input_diag.path) return state->compiler_input_diag.path;
  return state ? state->input : "zero.graph";
}

static void repo_append_contract_json(ZBuf *buf, const RepositoryGraphState *state) {
  char *from_source = repo_command_text("zero import", state->input);
  char *from_graph = repo_command_text("zero export", state->input);
  zbuf_append(buf, "{");
  zbuf_append(buf, "\"artifact\":\"zero.graph\",\"sourceProjection\":\"checked-in .0 source text\",");
  zbuf_append(buf, "\"optIn\":\"repository graph loader plus checked-in zero.graph at the package root\",");
  zbuf_append(buf, "\"commands\":{");
  zbuf_append(buf, "\"status\":{\"writes\":false,\"available\":true},");
  zbuf_append(buf, "\"verifyProjection\":{\"writes\":false,\"available\":");
  zbuf_append(buf, state && state->store_valid ? "true" : "false");
  zbuf_append(buf, ",\"repairCommands\":[");
  repo_append_json_string(buf, from_source);
  zbuf_append(buf, ",");
  repo_append_json_string(buf, from_graph);
  zbuf_append(buf, "]},");
  zbuf_append(buf, "\"import\":{\"writes\":true,\"available\":true},");
  zbuf_append(buf, "\"export\":{\"writes\":true,\"available\":");
  zbuf_append(buf, state && state->store_valid ? "true" : "false");
  zbuf_append(buf, "},");
  zbuf_append(buf, "\"merge\":{\"writes\":true,\"available\":true,\"requires\":[\"--base\",\"--left\",\"--right\"]}");
  zbuf_append(buf, "}}");
  free(from_source);
  free(from_graph);
}

static void repo_append_state_json(ZBuf *buf, const RepositoryGraphState *state, bool ok, const char *mode, bool writes) {
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": ");
  zbuf_append(buf, ok ? "true" : "false");
  zbuf_append(buf, ",\n  \"mode\": ");
  repo_append_json_string(buf, mode);
  zbuf_append(buf, ",\n  \"writes\": ");
  zbuf_append(buf, writes ? "true" : "false");
  zbuf_append(buf, ",\n  \"repositoryGraph\": {\"root\": ");
  repo_append_json_string(buf, state->root);
  zbuf_append(buf, ", \"storePath\": ");
  repo_append_json_string(buf, state->store_path);
  zbuf_append(buf, ", \"storePresent\": ");
  zbuf_append(buf, state->store_present ? "true" : "false");
  zbuf_append(buf, ", \"storeValid\": ");
  zbuf_append(buf, state->store_valid ? "true" : "false");
  zbuf_append(buf, ", \"enabled\": ");
  zbuf_append(buf, state->store_valid ? "true" : "false");
  zbuf_append(buf, ", \"projectionState\": ");
  repo_append_json_string(buf, repo_projection_state(state));
  zbuf_append(buf, ", \"possibleProjectionStates\": [\"not-enabled\", \"store-invalid\", \"clean\", \"source-missing\", \"source-stale\", \"conflict\"], \"canonicalSourceExtension\": \".0\", \"compilerInput\": ");
  repo_append_json_string(buf, repo_compiler_input_label(state));
  zbuf_append(buf, ", \"semanticValidity\": ");
  repo_append_json_string(buf, repo_semantic_validity_label(state));
  zbuf_append(buf, ", \"projectionValidity\": ");
  repo_append_json_string(buf, repo_projection_validity_label(state));
  zbuf_append(buf, "}");
  if (state->store_valid) {
    zbuf_append(buf, ",\n  \"store\": {\"format\":\"zero-repository-graph\",\"encoding\":");
    repo_append_json_string(buf, z_program_graph_store_format_name(state->store_format));
    zbuf_append(buf, ",\"schemaVersion\":1,\"graphHash\":");
    repo_append_json_string(buf, state->graph_hash);
    zbuf_appendf(buf, ",\"nodes\":%zu,\"edges\":%zu,\"sources\":%zu}", state->node_count, state->edge_count, state->source_count);
    zbuf_append(buf, ",\n  \"compilerStore\": {\"schemaVersion\":1,\"shape\":\"compiler-oriented-tables\",\"sourceFreeInspection\":true,\"sourceProjectionRequiredForSemanticFacts\":false,\"sourceMapRequiredForSemanticFacts\":false,\"semanticValidity\":{\"state\":");
    repo_append_json_string(buf, repo_semantic_validity_label(state));
    zbuf_append(buf, ",\"ok\":true},\"projectionValidity\":{\"state\":");
    repo_append_json_string(buf, repo_projection_validity_label(state));
    zbuf_append(buf, ",\"checked\":");
    zbuf_append(buf, state->projection_checked ? "true" : "false");
    zbuf_append(buf, ",\"current\":");
    zbuf_append(buf, state->projection_current ? "true" : "false");
    zbuf_append(buf, "},\"tables\":");
    z_program_graph_store_append_table_counts_json(buf, state->compiler_store_available ? &state->compiler_tables : NULL);
    zbuf_append(buf, ",\"hashInputs\":");
    z_program_graph_store_append_compiler_hash_inputs_json(buf);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, ",\n  \"storage\": {\"encoding\":");
  repo_append_json_string(buf, !state->store_valid || state->store_format == Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY ? "single-file-binary" : "single-file-text");
  zbuf_append(buf, ",\"artifact\":\"zero.graph\",\"interface\":\"ProgramGraphStore\",\"schemaVersion\":1,\"evolvable\":true,\"binaryAvailable\":true,\"defaultEncoding\":\"binary\"}");
  zbuf_appendf(buf, ",\n  \"scale\": {\"nodes\":%zu,\"edges\":%zu,\"sources\":%zu}", state->node_count, state->edge_count, state->source_count);
  zbuf_append(buf, ",\n  \"contract\": ");
  repo_append_contract_json(buf, state);
}

static void repo_append_changed_paths_json(ZBuf *buf, const char *const *changed_paths, size_t changed_len) {
  zbuf_append(buf, "[");
  for (size_t i = 0; changed_paths && i < changed_len; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    repo_append_json_string(buf, changed_paths[i]);
  }
  zbuf_append(buf, "]");
}

static void repo_append_diagnostic_json(ZBuf *buf, const RepositoryGraphState *state, const char *code, const char *message, const char *expected, const char *actual, const char *help, ZRepositoryGraphRepair repair) {
  zbuf_append(buf, ",\n  \"diagnostics\": [{\"severity\":\"error\",\"code\":");
  repo_append_json_string(buf, code);
  zbuf_append(buf, ",\"message\":");
  repo_append_json_string(buf, message);
  zbuf_append(buf, ",\"path\":");
  repo_append_json_string(buf, repo_diagnostic_path(state));
  zbuf_append(buf, ",\"line\":1,\"column\":1,\"length\":1,\"expected\":");
  repo_append_json_string(buf, expected);
  zbuf_append(buf, ",\"actual\":");
  repo_append_json_string(buf, actual);
  zbuf_append(buf, ",\"help\":");
  repo_append_json_string(buf, help);
  zbuf_append(buf, ",\"fixSafety\":\"requires-human-review\",\"repair\":{\"id\":\"inspect-repository-graph-status\",\"summary\":\"Inspect projection state before choosing import or export.\"},\"related\":[]}],");
  zbuf_append(buf, "\n  \"repairCommands\": ");
  z_repository_graph_append_repair_commands_json(buf, state->input, repair);
  zbuf_append(buf, "\n}\n");
}

static int repo_graph_error_paths(const RepositoryGraphState *state, bool json, const char *mode, const char *code, const char *message, const char *expected, const char *actual, const char *help, bool writes, const char *const *changed_paths, size_t changed_len, ZRepositoryGraphRepair repair) {
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    repo_append_state_json(&buf, state, false, mode, writes);
    if (writes || changed_len > 0) {
      zbuf_append(&buf, ",\n  \"changedPaths\": ");
      repo_append_changed_paths_json(&buf, changed_paths, changed_len);
    }
    repo_append_diagnostic_json(&buf, state, code, message, expected, actual, help, repair);
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    fprintf(stderr, "%s:1:1 %s: %s\n", repo_diagnostic_path(state), code, message);
    fprintf(stderr, "  expected: %s\n", expected);
    fprintf(stderr, "  actual: %s\n", actual);
    fprintf(stderr, "  help: %s\n", help);
    z_repository_graph_print_repair_commands(stderr, state->input, repair);
  }
  return 1;
}

static int repo_graph_error(const RepositoryGraphState *state, bool json, const char *mode, const char *code, const char *message, const char *expected, const char *actual, const char *help, ZRepositoryGraphRepair repair) {
  return repo_graph_error_paths(state, json, mode, code, message, expected, actual, help, false, NULL, 0, repair);
}

static int repo_compiler_input_error(const RepositoryGraphState *state, bool json, const char *mode) {
  const ZDiag *diag = state ? &state->compiler_input_diag : NULL;
  return repo_graph_error(state,
                          json,
                          mode,
                          repo_diag_code(diag),
                          diag && diag->message[0] ? diag->message : "repositoryGraph.compilerInput is invalid",
                          diag && diag->expected[0] ? diag->expected : "true or false",
                          diag && diag->actual[0] ? diag->actual : "invalid repositoryGraph.compilerInput",
                          diag && diag->help[0] ? diag->help : "set repositoryGraph.compilerInput to true only when a valid zero.graph store is checked in",
                          REPO_GRAPH_REPAIR_NONE);
}

static bool repo_diag_is_identity_reconcile_error(const ZDiag *diag) {
  const char *prefix = "repository graph source identity ";
  return diag && strncmp(diag->message, prefix, strlen(prefix)) == 0;
}

static int repo_missing_or_invalid_store_error(const RepositoryGraphState *state, bool json, const char *mode) {
  if (!state->store_present) {
    return repo_graph_error(state, json, mode, "RGP001", "repository graph store is missing", "checked-in zero.graph repository graph store", "missing zero.graph", "run zero import to create the repository graph store", REPO_GRAPH_REPAIR_FROM_SOURCE);
  }
  if (!state->store_valid) {
    return repo_graph_error(state, json, mode, "RGP003", "repository graph store is invalid", "valid zero.graph repository graph store", state->store_error ? state->store_error : "invalid zero.graph", "run zero import after reviewing the source projection", REPO_GRAPH_REPAIR_FROM_SOURCE);
  }
  return 0;
}

static int repo_module_identity_error(const RepositoryGraphState *state, bool json, const char *mode) {
  if (!state || !state->module_identity_error) return 0;
  return repo_graph_error(state,
                          json,
                          mode,
                          "RGP007",
                          state->module_identity_error,
                          state->expected_module_identity && state->expected_module_identity[0] ? state->expected_module_identity : "zero.toml or zero.json package identity",
                          state->module_identity && state->module_identity[0] ? state->module_identity : "missing module identity",
                          "check in the zero.graph generated for this package, or update the package manifest after reviewing the package identity",
                          REPO_GRAPH_REPAIR_STATUS);
}

static int repo_target_main_error(const RepositoryGraphState *state, bool json, const char *mode) {
  if (!state || !state->target_main_error) return 0;
  return repo_graph_error(state,
                          json,
                          mode,
                          "RGP007",
                          state->target_main_error,
                          "targets.cli.main source path present in zero.graph",
                          state->expected_main_path && state->expected_main_path[0] ? state->expected_main_path : "missing targets.cli.main",
                          "check in the zero.graph generated for this target main, or update the package manifest after reviewing the package entry point",
                          REPO_GRAPH_REPAIR_STATUS);
}

static int repo_graph_import_export_error(const RepositoryGraphState *state, bool json, const char *mode, const char *actual) {
  return repo_graph_error(state, json, mode, "RGP002", "repository graph import/export requires zero import or zero export", "zero import or zero export", actual ? actual : "missing import/export direction", "choose whether source text or zero.graph is authoritative", REPO_GRAPH_REPAIR_NONE);
}

static int repo_graph_success_paths(const RepositoryGraphState *state, bool json, const char *mode, bool writes, const char *const *changed_paths, size_t changed_len) {
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    repo_append_state_json(&buf, state, true, mode, writes);
    zbuf_append(&buf, ",\n  \"changedPaths\": ");
    repo_append_changed_paths_json(&buf, changed_paths, changed_len);
    zbuf_append(&buf, ",\n  \"diagnostics\": [],\n  \"repairCommands\": []\n}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else if (writes && changed_len > 0) {
    printf("repository graph %s ok\n", mode);
    for (size_t i = 0; changed_paths && i < changed_len; i++) printf("wrote: %s\n", changed_paths[i]);
  } else {
    printf("repository graph %s ok\n", mode);
  }
  return 0;
}

static int repo_graph_success(const RepositoryGraphState *state, bool json, const char *mode, bool writes, const char *changed_path) {
  const char *paths[1] = {changed_path};
  return repo_graph_success_paths(state, json, mode, writes, changed_path && changed_path[0] ? paths : NULL, changed_path && changed_path[0] ? 1 : 0);
}

int z_repository_graph_status_command(const char *input, const ZTargetInfo *target, bool json, bool from_graph, bool from_source) {
  RepositoryGraphState state = repo_graph_state(input, target);
  if (from_graph || from_source) {
    int rc = repo_graph_import_export_error(&state, json, "status", "unexpected import/export direction");
    repo_graph_state_free(&state);
    return rc;
  }
  if (!state.compiler_input_valid) {
    int rc = repo_compiler_input_error(&state, json, "status");
    repo_graph_state_free(&state);
    return rc;
  }
  if (state.store_present && !state.store_valid) {
    int rc = repo_missing_or_invalid_store_error(&state, json, "status");
    repo_graph_state_free(&state);
    return rc;
  }
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    repo_append_state_json(&buf, &state, true, "status", false);
    zbuf_append(&buf, ",\n  \"diagnostics\": [],\n  \"repairCommands\": []\n}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    printf("repository graph status: %s\n", repo_projection_state(&state));
    printf("root: %s\n", state.root);
    printf("store: %s\n", state.store_path);
    if (state.store_valid) printf("store format: %s\n", z_program_graph_store_format_name(state.store_format));
    printf("store valid: %s\n", state.store_valid ? "true" : "false");
    printf("source projection: checked-in .0 source text\n");
    printf("semantic validity: %s\n", repo_semantic_validity_label(&state));
    printf("projection validity: %s\n", repo_projection_validity_label(&state));
    if (state.store_valid) printf("compiler store: compiler-oriented-tables\n");
    printf("compiler input: %s\n", repo_compiler_input_label(&state));
    printf("writes: false\n");
  }
  repo_graph_state_free(&state);
  return 0;
}

int z_repository_graph_verify_projection_command(const char *input, const ZTargetInfo *target, bool json, bool from_graph, bool from_source) {
  RepositoryGraphState state = repo_graph_state(input, target);
  if (from_graph || from_source) {
    int rc = repo_graph_import_export_error(&state, json, "verify-projection", "unexpected import/export direction");
    repo_graph_state_free(&state);
    return rc;
  }
  if (!state.compiler_input_valid) {
    int rc = repo_compiler_input_error(&state, json, "verify-projection");
    repo_graph_state_free(&state);
    return rc;
  }
  int rc = repo_missing_or_invalid_store_error(&state, json, "verify-projection");
  if (rc != 0) {
    repo_graph_state_free(&state);
    return rc;
  }
  rc = repo_module_identity_error(&state, json, "verify-projection");
  if (rc != 0) {
    repo_graph_state_free(&state);
    return rc;
  }
  rc = repo_target_main_error(&state, json, "verify-projection");
  if (rc != 0) {
    repo_graph_state_free(&state);
    return rc;
  }
  ZProgramGraphStore store;
  ZDiag diag = {0};
  if (!z_program_graph_store_load_path(state.store_path, &store, &diag)) {
    rc = repo_graph_error(&state, json, "verify-projection", "RGP003", "repository graph store is invalid", "valid zero.graph repository graph store", diag.message, "run zero import after reviewing the source projection", REPO_GRAPH_REPAIR_FROM_SOURCE);
    repo_graph_state_free(&state);
    return rc;
  }
  if (state.projection_missing) {
    rc = repo_graph_error(&state, json, "verify-projection", "RGP006", "source projection is missing", "checked-in .0 source text matching zero.graph projection", "missing source projection file", "run zero export after reviewing graph changes", REPO_GRAPH_REPAIR_FROM_GRAPH);
    z_program_graph_store_free(&store);
    repo_graph_state_free(&state);
    return rc;
  }
  bool projection_current = false;
  bool projection_checked = z_program_graph_projection_sources_match(&store, target, &projection_current, &diag);
  if (!projection_checked || !projection_current) {
    const char *actual = projection_checked ? "source projection differs" : (diag.message[0] ? diag.message : "projection unavailable");
    rc = repo_graph_error(&state, json, "verify-projection", "RGP006", "source projection differs from repository graph", "checked-in .0 source text matching zero.graph projection", actual, "review the diff, then run zero import if the .0 projection is authoritative or zero export if zero.graph is authoritative", REPO_GRAPH_REPAIR_IMPORT_OR_EXPORT);
    z_program_graph_store_free(&store);
    repo_graph_state_free(&state);
    return rc;
  }
  z_program_graph_store_free(&store);
  rc = repo_graph_success(&state, json, "verify-projection", false, NULL);
  repo_graph_state_free(&state);
  return rc;
}

int z_repository_graph_import_export_command(const char *input, const ZTargetInfo *target, bool json, bool from_graph, bool from_source, const char *store_format, const ZProgramGraph *source_graph, ZRepositoryGraphLoadSourceGraphFn load_source_graph, void *load_source_graph_ctx) {
  RepositoryGraphState state = repo_graph_state(input, target);
  const char *mode = from_graph ? "export" : "import";
  if (from_graph == from_source) {
    int rc = repo_graph_import_export_error(&state, json, mode, from_graph ? "import and export directions" : "missing import/export direction");
    repo_graph_state_free(&state);
    return rc;
  }
  if (!state.compiler_input_valid) {
    int rc = repo_compiler_input_error(&state, json, mode);
    repo_graph_state_free(&state);
    return rc;
  }
  if (from_graph) {
    int rc = repo_missing_or_invalid_store_error(&state, json, "export");
    if (rc != 0) {
      repo_graph_state_free(&state);
      return rc;
    }
    rc = repo_module_identity_error(&state, json, "export");
    if (rc != 0) {
      repo_graph_state_free(&state);
      return rc;
    }
    ZProgramGraphStore store;
    ZDiag diag = {0};
    if (!z_program_graph_store_load_path(state.store_path, &store, &diag)) {
      rc = repo_graph_error(&state, json, "export", "RGP003", "repository graph store is invalid", "valid zero.graph repository graph store", diag.message, "run zero import after reviewing the source projection", REPO_GRAPH_REPAIR_FROM_SOURCE);
      repo_graph_state_free(&state);
      return rc;
    }
    ZProgramGraphProjection projection;
    if (!z_program_graph_projection_write_sources(&store, target, &projection, &diag)) {
      rc = repo_graph_error(&state, json, "export", "RGP004", "repository graph projection could not be written", "canonical .0 source projection", diag.message[0] ? diag.message : "projection failed", "run zero check zero.graph before exporting from graph", REPO_GRAPH_REPAIR_NONE);
      z_program_graph_projection_free(&projection);
      z_program_graph_store_free(&store);
      repo_graph_state_free(&state);
      return rc;
    }
    repo_graph_state_free(&state);
    ZProgramGraph post_graph;
    z_program_graph_init(&post_graph);
    ZDiag post_diag = {0};
    bool post_checked = load_source_graph != NULL;
    bool post_ok = post_checked && load_source_graph(load_source_graph_ctx, &post_graph, &post_diag);
    state = repo_graph_state(input, target);
    bool post_clean = state.store_present && state.store_valid && state.projection_checked && state.projection_current;
    post_clean = post_clean && !state.projection_error;
    if (post_checked && (!post_ok || !post_clean)) {
      const char *actual = post_ok ? repo_projection_state(&state) : (post_diag.message[0] ? post_diag.message : "exported source projection could not be checked");
      const char *help = post_ok ? "run zero status to inspect repository graph projection state before choosing a repair" : "inspect the exported .0 projection and run zero import after reviewing human-source changes";
      rc = repo_graph_error_paths(&state, json, "export", "RGP006", "repository graph store is not clean after export", "clean repository graph projection state", actual, help, projection.changed_len > 0, (const char *const *)projection.changed_paths, projection.changed_len, REPO_GRAPH_REPAIR_STATUS);
      z_program_graph_free(&post_graph);
      z_program_graph_projection_free(&projection);
      z_program_graph_store_free(&store);
      repo_graph_state_free(&state);
      return rc;
    }
    rc = repo_graph_success_paths(&state, json, "export", true, (const char *const *)projection.changed_paths, projection.changed_len);
    z_program_graph_free(&post_graph);
    z_program_graph_projection_free(&projection);
    z_program_graph_store_free(&store);
    repo_graph_state_free(&state);
    return rc;
  }
  if (!source_graph) {
    int rc = repo_graph_error(&state, json, "import", "RGP003", "source graph could not be built", "current source graph", "missing source graph", "run zero check on the input first", REPO_GRAPH_REPAIR_NONE);
    repo_graph_state_free(&state);
    return rc;
  }
  int identity_rc = repo_module_identity_error(&state, json, "import");
  if (identity_rc != 0) {
    repo_graph_state_free(&state);
    return identity_rc;
  }
  ZProgramGraphStore saved;
  ZDiag diag = {0};
  ZProgramGraphStoreFormat requested_format = state.store_valid ? state.store_format : Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY;
  if (!repo_requested_store_format(store_format, requested_format, &requested_format, &diag)) {
    int rc = repo_graph_error(&state, json, "import", "RGP003", diag.message, diag.expected, diag.actual, diag.help, REPO_GRAPH_REPAIR_STATUS);
    repo_graph_state_free(&state);
    return rc;
  }
  if (!z_program_graph_store_save_for_input_format(input, source_graph, requested_format, &saved, &diag)) {
    bool identity_error = repo_diag_is_identity_reconcile_error(&diag);
    bool module_identity_error = identity_error && ((strncmp(diag.expected, "module:", 7) == 0) || (strncmp(diag.expected, "package:", 8) == 0));
    int rc = repo_graph_error(&state,
                              json,
                              "import",
                              identity_error ? "RGP007" : "RGP003",
                              identity_error ? diag.message : "repository graph store could not be saved",
                              module_identity_error ? diag.expected : (identity_error ? "unambiguous graph identity match between zero.graph and edited source" : "byte-stable zero.graph repository graph store"),
                              diag.actual[0] ? diag.actual : (diag.message[0] ? diag.message : "save failed"),
                              module_identity_error ? "import from the original source path, or recreate zero.graph after reviewing the module rename" : (identity_error ? "split the source edit or make it through zero patch so node identity is explicit" : "run zero status to inspect repository graph state"),
                              identity_error ? REPO_GRAPH_REPAIR_NONE : REPO_GRAPH_REPAIR_STATUS);
    repo_graph_state_free(&state);
    return rc;
  }
  repo_graph_state_free(&state);
  state = repo_graph_state(input, target);
  if (strcmp(repo_projection_state(&state), "clean") != 0) {
    const char *paths[1] = {saved.path};
    int rc = repo_graph_error_paths(&state, json, "import", "RGP006", "repository graph store is not clean after import", "clean repository graph projection state", repo_projection_state(&state), "run zero status to inspect repository graph projection state before choosing a repair", saved.path && saved.path[0], saved.path && saved.path[0] ? paths : NULL, saved.path && saved.path[0] ? 1 : 0, REPO_GRAPH_REPAIR_STATUS);
    z_program_graph_store_free(&saved);
    repo_graph_state_free(&state);
    return rc;
  }
  int rc = repo_graph_success(&state, json, "import", true, saved.path);
  z_program_graph_store_free(&saved);
  repo_graph_state_free(&state);
  return rc;
}

static int repo_graph_merge_direction_error(const RepositoryGraphState *state, bool json, const char *actual) {
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    repo_append_state_json(&buf, state, false, "merge", false);
    zbuf_append(&buf, ",\n  \"diagnostics\": [{\"severity\":\"error\",\"code\":\"RGM001\",\"message\":\"graph merge requires base, left, and right stores\",\"path\":");
    repo_append_json_string(&buf, state->input);
    zbuf_append(&buf, ",\"line\":1,\"column\":1,\"length\":1,\"expected\":\"zero merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> <project|zero.toml|zero.json|file.0>\",\"actual\":");
    repo_append_json_string(&buf, actual ? actual : "missing merge input");
    zbuf_append(&buf, ",\"help\":\"pass the common ancestor store and both side stores\",\"fixSafety\":\"requires-human-review\",\"repair\":{\"id\":\"choose-repository-graph-merge-inputs\",\"summary\":\"Choose the three repository graph stores to merge.\"},\"related\":[]}],\n  \"repairCommands\": []\n}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    fprintf(stderr, "%s:1:1 RGM001: graph merge requires base, left, and right stores\n", state->input);
    fprintf(stderr, "  expected: zero merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> <project|zero.toml|zero.json|file.0>\n");
    fprintf(stderr, "  actual: %s\n", actual ? actual : "missing merge input");
    fprintf(stderr, "  help: pass the common ancestor store and both side stores\n");
  }
  return 1;
}

static void repo_graph_merge_copy(char *out, size_t out_len, const char *text) {
  if (!out || out_len == 0) return;
  const char *value = text ? text : "";
  size_t len = strlen(value);
  if (len >= out_len) len = out_len - 1;
  memcpy(out, value, len);
  out[len] = '\0';
}

static int repo_graph_merge_load_error(const RepositoryGraphState *state, bool json, const char *label, const char *path, const ZDiag *diag) {
  char message[160];
  snprintf(message, sizeof(message), "repository graph merge %s store is invalid", label ? label : "input");
  ZBuf actual;
  zbuf_init(&actual);
  zbuf_append(&actual, path ? path : "");
  zbuf_append(&actual, ": ");
  zbuf_append(&actual, diag && diag->message[0] ? diag->message : "invalid store");
  int rc = repo_graph_error(state, json, "merge", "RGM001", message, "valid zero.graph repository graph store", actual.data ? actual.data : "", "check the merge input path and rerun zero merge", REPO_GRAPH_REPAIR_NONE);
  zbuf_free(&actual);
  return rc;
}

static void repo_append_merge_json(ZBuf *buf, const RepositoryGraphState *state, const ZRepositoryGraphMergeResult *merge, bool ok) {
  repo_append_state_json(buf, state, ok, "merge", ok);
  zbuf_append(buf, ",\n  \"merge\": {\"target\": ");
  repo_append_json_string(buf, merge ? merge->target_path : "");
  zbuf_appendf(buf,
               ", \"mergedNodes\": %zu, \"mergedEdges\": %zu, \"leftChanges\": %zu, \"rightChanges\": %zu, \"conflicts\": %zu}",
               merge ? merge->merged_nodes : 0,
               merge ? merge->merged_edges : 0,
               merge ? merge->left_changes : 0,
               merge ? merge->right_changes : 0,
               merge ? merge->conflicts : 0);
}

static int repo_graph_merge_conflict(const RepositoryGraphState *state, bool json, const ZRepositoryGraphMergeResult *merge) {
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    repo_append_merge_json(&buf, state, merge, false);
    zbuf_append(&buf, ",\n  \"diagnostics\": [{\"severity\":\"error\",\"code\":");
    repo_append_json_string(&buf, merge && merge->code[0] ? merge->code : "RGM002");
    zbuf_append(&buf, ",\"message\":");
    repo_append_json_string(&buf, merge && merge->message[0] ? merge->message : "repository graph merge conflict");
    zbuf_append(&buf, ",\"path\":");
    repo_append_json_string(&buf, merge && merge->source_path[0] ? merge->source_path : state->input);
    zbuf_append(&buf, ",\"line\":1,\"column\":1,\"length\":1,\"expected\":\"independent repository graph edits\",\"actual\":");
    repo_append_json_string(&buf, merge && merge->node_id[0] ? merge->node_id : "conflicting graph facts");
    zbuf_append(&buf, ",\"help\":\"resolve the graph conflict by editing one side, then rerun zero merge\",\"fixSafety\":\"requires-human-review\",\"repair\":{\"id\":\"resolve-repository-graph-merge-conflict\",\"summary\":\"Resolve the conflicting graph node or edge before merging.\"},\"related\":[{\"kind\":\"graphNode\",\"id\":");
    repo_append_json_string(&buf, merge ? merge->node_id : "");
    zbuf_append(&buf, "},{\"kind\":\"semanticObject\",\"id\":");
    repo_append_json_string(&buf, merge ? merge->semantic_object : "");
    zbuf_append(&buf, "},{\"kind\":\"field\",\"id\":");
    repo_append_json_string(&buf, merge ? merge->field : "");
    zbuf_append(&buf, "}]}],\n  \"repairCommands\": []\n}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    fprintf(stderr, "%s:1:1 %s: %s\n",
            merge && merge->source_path[0] ? merge->source_path : state->input,
            merge && merge->code[0] ? merge->code : "RGM002",
            merge && merge->message[0] ? merge->message : "repository graph merge conflict");
    fprintf(stderr, "  node: %s\n", merge && merge->node_id[0] ? merge->node_id : "unknown");
    fprintf(stderr, "  semantic object: %s\n", merge && merge->semantic_object[0] ? merge->semantic_object : "unknown");
    fprintf(stderr, "  field: %s\n", merge && merge->field[0] ? merge->field : "unknown");
    fprintf(stderr, "  help: resolve the graph conflict by editing one side, then rerun zero merge\n");
  }
  return 1;
}

int z_repository_graph_merge_command(const char *input, const ZTargetInfo *target, const char *base_path, const char *left_path, const char *right_path, const char *store_format, bool json) {
  RepositoryGraphState state = repo_graph_state(input, target);
  if (!base_path || !left_path || !right_path) {
    const char *actual = !base_path ? "missing --base" : (!left_path ? "missing --left" : "missing --right");
    int rc = repo_graph_merge_direction_error(&state, json, actual);
    repo_graph_state_free(&state);
    return rc;
  }

  ZProgramGraphStore base;
  ZProgramGraphStore left;
  ZProgramGraphStore right;
  ZProgramGraphStore merged;
  ZDiag diag = {0};
  if (!z_program_graph_store_load_path(base_path, &base, &diag)) {
    int rc = repo_graph_merge_load_error(&state, json, "base", base_path, &diag);
    repo_graph_state_free(&state);
    return rc;
  }
  if (!z_program_graph_store_load_path(left_path, &left, &diag)) {
    int rc = repo_graph_merge_load_error(&state, json, "left", left_path, &diag);
    z_program_graph_store_free(&base);
    repo_graph_state_free(&state);
    return rc;
  }
  if (!z_program_graph_store_load_path(right_path, &right, &diag)) {
    int rc = repo_graph_merge_load_error(&state, json, "right", right_path, &diag);
    z_program_graph_store_free(&base);
    z_program_graph_store_free(&left);
    repo_graph_state_free(&state);
    return rc;
  }
  char *root = z_program_graph_store_root_for_input(input && input[0] ? input : ".");
  char *target_path = z_program_graph_store_path_for_root(root);
  ZProgramGraphStoreFormat requested_format = state.store_valid
    ? state.store_format
    : Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY;
  if (!repo_requested_store_format(store_format, requested_format, &requested_format, &diag)) {
    int rc = repo_graph_error(&state,
                              json,
                              "merge",
                              "RGM001",
                              diag.message,
                              diag.expected,
                              diag.actual,
                              diag.help,
                              REPO_GRAPH_REPAIR_NONE);
    z_program_graph_store_free(&base);
    z_program_graph_store_free(&left);
    z_program_graph_store_free(&right);
    free(root);
    free(target_path);
    repo_graph_state_free(&state);
    return rc;
  }
  ZRepositoryGraphMergeResult merge = {0};
  bool ok = z_repository_graph_merge_stores(&base, &left, &right, target, target_path, &merged, &merge, &diag);
  if (ok) {
    ok = z_program_graph_store_write_path_format(target_path, &merged, requested_format, &diag);
    merge.wrote = ok;
    if (!ok) {
      snprintf(merge.code, sizeof(merge.code), "RGM007");
      repo_graph_merge_copy(merge.message, sizeof(merge.message), diag.message[0] ? diag.message : "repository graph merge could not write target store");
    }
  }
  repo_graph_state_free(&state);
  state = repo_graph_state(input, target);
  int rc = 0;
  if (!ok) {
    if (!merge.code[0]) {
      snprintf(merge.code, sizeof(merge.code), "RGM002");
      repo_graph_merge_copy(merge.message, sizeof(merge.message), diag.message[0] ? diag.message : "repository graph merge conflict");
    }
    rc = repo_graph_merge_conflict(&state, json, &merge);
  } else if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    repo_append_merge_json(&buf, &state, &merge, true);
    zbuf_append(&buf, ",\n  \"changedPaths\": [");
    repo_append_json_string(&buf, target_path);
    zbuf_append(&buf, "],\n  \"diagnostics\": [],\n  \"repairCommands\": []\n}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    printf("repository graph merge ok\n");
    printf("wrote: %s\n", target_path);
    printf("merged nodes: %zu\n", merge.merged_nodes);
    printf("merged edges: %zu\n", merge.merged_edges);
  }
  z_program_graph_store_free(&base);
  z_program_graph_store_free(&left);
  z_program_graph_store_free(&right);
  z_program_graph_store_free(&merged);
  free(root);
  free(target_path);
  repo_graph_state_free(&state);
  return rc;
}

bool z_repository_graph_needs_source_graph(const char *kind, const char *input, const ZTargetInfo *target, bool from_graph, bool from_source) {
  if (REPO_KIND(kind, "import")) return true;
  (void)input;
  (void)target;
  (void)from_graph;
  (void)from_source;
  return false;
}

int z_repository_graph_maybe_command(const char *kind, const char *input, const ZTargetInfo *target, bool json, bool from_graph, bool from_source, const char *merge_base, const char *merge_left, const char *merge_right, const char *store_format, const char *out, const ZProgramGraph *source_graph, ZRepositoryGraphLoadSourceGraphFn load_source_graph, void *load_source_graph_ctx, bool *handled) {
  if (handled) *handled = true;
  if (REPO_KIND(kind, "status")) return z_repository_graph_status_command(input, target, json, from_graph, from_source);
  if (REPO_KIND(kind, "verify-projection")) return z_repository_graph_verify_projection_command(input, target, json, from_graph, from_source);
  bool is_import = REPO_KIND(kind, "import");
  bool is_export = REPO_KIND(kind, "export");
  if (is_import || is_export) {
    RepositoryGraphState state = repo_graph_state(input, target);
    bool has_out = out && out[0];
    bool repository_command = is_export ||
                              (state.compiler_input_valid &&
                               (state.compiler_input_enabled || state.store_present || (!has_out && store_format && store_format[0])));
    if (!repository_command) {
      repo_graph_state_free(&state);
      if (handled) *handled = false;
      return 0;
    }
    if (has_out) {
      int rc = repo_graph_error(&state, json, kind, "RGP002", "repository graph import/export writes fixed repository paths and does not support --out", "zero import <package> or zero export <package>", is_export ? "zero export --out" : "zero import --out", "omit --out; import writes zero.graph and export writes checked-in .0 projections", REPO_GRAPH_REPAIR_NONE);
      repo_graph_state_free(&state);
      return rc;
    }
    repo_graph_state_free(&state);
    return z_repository_graph_import_export_command(input, target, json, is_export, is_import, store_format, source_graph, load_source_graph, load_source_graph_ctx);
  }
  if (REPO_KIND(kind, "merge")) return z_repository_graph_merge_command(input, target, merge_base, merge_left, merge_right, store_format, json);
  if (handled) *handled = false;
  return 0;
}
