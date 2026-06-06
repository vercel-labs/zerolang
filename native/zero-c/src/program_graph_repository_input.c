#include "program_graph_repository_input.h"

#include "program_graph_manifest.h"
#include "program_graph_projection.h"
#include "program_graph_repository_repair.h"
#include "program_graph_store.h"
#include "zero.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *input;
  char *root;
  char *store_path;
  bool store_present;
  bool store_valid;
  char *store_error;
  char *projection_error;
  char *module_identity;
  char *graph_hash;
  size_t node_count;
  size_t edge_count;
  size_t source_count;
  bool projection_checked;
  bool projection_current;
  bool projection_missing;
} RepositoryGraphInputState;

static void input_append_json_string(ZBuf *buf, const char *text) {
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

static bool input_text_eq(const char *left, const char *right) {
  const char *l = left ? left : "";
  const char *r = right ? right : "";
  while (*l || *r) {
    if (*l != *r) return false;
    l++;
    r++;
  }
  return true;
}

static RepositoryGraphInputState input_state(const char *input, const ZTargetInfo *target) {
  RepositoryGraphInputState state = {.input = input && input[0] ? input : "."};
  state.root = z_program_graph_store_root_for_input(state.input);
  state.store_path = z_program_graph_store_path_for_root(state.root);
  state.store_present = z_program_graph_store_file_exists(state.store_path);
  if (state.store_present) {
    ZProgramGraphStore store;
    ZDiag diag = {0};
    if (z_program_graph_store_load_path(state.store_path, &store, &diag)) {
      state.store_valid = true;
      state.module_identity = z_strdup(store.graph.module_identity ? store.graph.module_identity : "");
      state.graph_hash = z_strdup(store.graph.graph_hash ? store.graph.graph_hash : "");
      state.node_count = store.graph.node_len;
      state.edge_count = store.graph.edge_len;
      state.source_count = store.source_path_len;
      state.projection_missing = z_program_graph_projection_sources_missing(&store);
      z_program_graph_projection_state_label(&store, target, &state.projection_checked, &state.projection_current, &diag);
      if (!state.projection_checked && diag.message[0]) state.projection_error = z_strdup(diag.message);
      z_program_graph_store_free(&store);
    } else {
      state.store_error = z_strdup(diag.message[0] ? diag.message : "invalid zero.graph");
    }
  }
  return state;
}

static void input_state_free(RepositoryGraphInputState *state) {
  if (!state) return;
  free(state->root);
  free(state->store_path);
  free(state->store_error);
  free(state->projection_error);
  free(state->module_identity);
  free(state->graph_hash);
  memset(state, 0, sizeof(*state));
}

static const char *input_sync_state(const RepositoryGraphInputState *state) {
  if (state && state->store_present && state->store_valid && state->projection_error) return "conflict";
  if (state && state->store_present && state->store_valid && state->projection_missing) return "source-missing";
  if (state && state->store_present && state->store_valid && state->projection_checked && !state->projection_current) return "source-stale";
  if (state && state->store_present && state->store_valid && state->projection_checked) return state->projection_current ? "clean" : "source-stale";
  if (state && state->store_present && state->store_valid) return "store-valid";
  if (state && state->store_present) return "store-invalid";
  return "not-enabled";
}

static const char *input_projection_validity_label(const RepositoryGraphInputState *state) {
  if (!state || !state->store_valid) return "unavailable";
  if (state->projection_error) return "conflict";
  if (state->projection_missing) return "missing";
  if (!state->projection_checked) return "unavailable";
  return state->projection_current ? "clean" : "stale";
}

static void input_append_state_json(ZBuf *buf, const RepositoryGraphInputState *state) {
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": false,\n  \"mode\": \"compiler-input\",\n  \"writes\": false,\n  \"repositoryGraph\": {\"root\": ");
  input_append_json_string(buf, state->root);
  zbuf_append(buf, ", \"storePath\": ");
  input_append_json_string(buf, state->store_path);
  zbuf_append(buf, ", \"storePresent\": ");
  zbuf_append(buf, state->store_present ? "true" : "false");
  zbuf_append(buf, ", \"storeValid\": ");
  zbuf_append(buf, state->store_valid ? "true" : "false");
  zbuf_append(buf, ", \"enabled\": ");
  zbuf_append(buf, state->store_valid ? "true" : "false");
  zbuf_append(buf, ", \"syncState\": ");
  input_append_json_string(buf, input_sync_state(state));
  zbuf_append(buf, ", \"possibleSyncStates\": [\"not-enabled\", \"store-invalid\", \"clean\", \"source-missing\", \"source-stale\", \"conflict\"], \"canonicalSourceExtension\": \".0\", \"compilerInput\": \"repository-graph\", \"semanticValidity\": ");
  input_append_json_string(buf, state->store_valid ? "shape-valid" : "unavailable");
  zbuf_append(buf, ", \"projectionValidity\": ");
  input_append_json_string(buf, input_projection_validity_label(state));
  zbuf_append(buf, "}");
  if (state->store_valid) {
    zbuf_append(buf, ",\n  \"store\": {\"format\":\"zero-repository-graph\",\"schemaVersion\":1,\"graphHash\":");
    input_append_json_string(buf, state->graph_hash);
    zbuf_appendf(buf, ",\"nodes\":%zu,\"edges\":%zu,\"sources\":%zu}", state->node_count, state->edge_count, state->source_count);
  }
}

static int input_error(const RepositoryGraphInputState *state, bool json, const char *code, const char *message, const char *expected, const char *actual, const char *help, ZRepositoryGraphRepair repair) {
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    input_append_state_json(&buf, state);
    zbuf_append(&buf, ",\n  \"diagnostics\": [{\"severity\":\"error\",\"code\":");
    input_append_json_string(&buf, code);
    zbuf_append(&buf, ",\"message\":");
    input_append_json_string(&buf, message);
    zbuf_append(&buf, ",\"path\":");
    input_append_json_string(&buf, state->input);
    zbuf_append(&buf, ",\"line\":1,\"column\":1,\"length\":1,\"expected\":");
    input_append_json_string(&buf, expected);
    zbuf_append(&buf, ",\"actual\":");
    input_append_json_string(&buf, actual);
    zbuf_append(&buf, ",\"help\":");
    input_append_json_string(&buf, help);
    zbuf_append(&buf, ",\"fixSafety\":\"requires-human-review\",\"repair\":{\"id\":\"inspect-repository-graph-status\",\"summary\":\"Inspect graph/source sync state before choosing a sync direction.\"},\"related\":[]}],\n  \"repairCommands\": ");
    z_repository_graph_append_repair_commands_json(&buf, state->input, repair);
    zbuf_append(&buf, "\n}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    fprintf(stderr, "%s:1:1 %s: %s\n", state->input, code, message);
    fprintf(stderr, "  expected: %s\n", expected);
    fprintf(stderr, "  actual: %s\n", actual);
    fprintf(stderr, "  help: %s\n", help);
    z_repository_graph_print_repair_commands(stderr, state->input, repair);
  }
  return 1;
}
static int input_manifest_identity_error(const RepositoryGraphInputState *state, bool json) {
  char *expected = NULL;
  ZDiag diag = {0};
  if (!z_program_graph_manifest_module_identity(state ? state->input : NULL, &expected, &diag)) {
    char actual[512];
    const bool identity_error = diag.code == 1007;
    if (identity_error) {
      snprintf(actual, sizeof(actual), "missing package.name; zero.graph module identity is %s", state && state->module_identity ? state->module_identity : "missing module identity");
    }
    int rc = input_error(state,
                         json,
                         identity_error ? "RGP007" : "RGP003",
                         diag.message[0] ? diag.message : (identity_error ? "repository graph compiler input requires package.name" : "package manifest could not be read"),
                         diag.expected[0] ? diag.expected : (identity_error ? "zero.json package.name matching zero.graph module identity" : "valid zero.json package manifest"),
                         identity_error ? actual : (diag.message[0] ? diag.message : "manifest unavailable"),
                         diag.help[0] ? diag.help : (identity_error ? "add package.name before using repository graph compiler input" : "fix zero.json before using repository graph compiler input"),
                         identity_error ? REPO_GRAPH_REPAIR_STATUS : REPO_GRAPH_REPAIR_NONE);
    free((char *)diag.path);
    return rc;
  }
  if (expected && !input_text_eq(expected, state ? state->module_identity : NULL)) {
    int rc = input_error(state,
                         json,
                         "RGP007",
                         "repository graph store module identity does not match package manifest",
                         expected,
                         state && state->module_identity ? state->module_identity : "missing module identity",
                         "check in the zero.graph generated for this package, or update zero.json after reviewing the package identity",
                         REPO_GRAPH_REPAIR_STATUS);
    free(expected);
    return rc;
  }
  free(expected);
  return 0;
}

int z_repository_graph_verify_compiler_input(const char *input, const ZTargetInfo *target, bool json, char **out_store_path) {
  if (out_store_path) *out_store_path = NULL;
  RepositoryGraphInputState state = input_state(input, target);
  if (!state.store_present) {
    int rc = input_error(&state, json, "RGP001", "repository graph store is missing", "checked-in zero.graph repository graph store", "missing zero.graph", "run zero graph sync --from-source to create the repository graph store", REPO_GRAPH_REPAIR_FROM_SOURCE);
    input_state_free(&state);
    return rc;
  }
  if (!state.store_valid) {
    int rc = input_error(&state, json, "RGP003", "repository graph store is invalid", "valid zero.graph repository graph store", state.store_error ? state.store_error : "invalid zero.graph", "run zero graph sync --from-source after reviewing the source projection", REPO_GRAPH_REPAIR_FROM_SOURCE);
    input_state_free(&state);
    return rc;
  }
  int identity_rc = input_manifest_identity_error(&state, json);
  if (identity_rc != 0) {
    input_state_free(&state);
    return identity_rc;
  }
  if (out_store_path) *out_store_path = z_strdup(state.store_path);
  input_state_free(&state);
  return 0;
}

int z_repository_graph_require_compiler_store(const char *input, const ZTargetInfo *target, bool json, char **out_store_path) {
  return z_repository_graph_verify_compiler_input(input, target, json, out_store_path);
}
