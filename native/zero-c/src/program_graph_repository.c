#include "program_graph_repository.h"

#include "program_graph_projection.h"
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
  char *graph_error;
  char *projection_error;
  char *graph_hash;
  size_t node_count;
  size_t edge_count;
  size_t source_count;
  bool graph_checked;
  bool graph_current;
  bool projection_checked;
  bool projection_current;
} RepositoryGraphState;

static RepositoryGraphState repo_graph_state(const char *input, const ZProgramGraph *source_graph, const ZDiag *source_graph_diag) {
  RepositoryGraphState state = {.input = input && input[0] ? input : "."};
  state.root = z_program_graph_store_root_for_input(state.input);
  state.store_path = z_program_graph_store_path_for_root(state.root);
  state.store_present = z_program_graph_store_file_exists(state.store_path);
  if (state.store_present) {
    ZProgramGraphStore store;
    ZDiag diag = {0};
    if (z_program_graph_store_load_path(state.store_path, &store, &diag)) {
      state.store_valid = true;
      state.graph_hash = z_strdup(store.graph.graph_hash ? store.graph.graph_hash : "");
      state.node_count = store.graph.node_len;
      state.edge_count = store.graph.edge_len;
      state.source_count = store.source_path_len;
      if (source_graph) {
        state.graph_checked = true;
        state.graph_current = z_program_graph_store_graph_matches_source(&store, source_graph);
      } else if (source_graph_diag && (source_graph_diag->code != 0 || source_graph_diag->message[0])) {
        state.graph_checked = true;
        state.graph_error = z_strdup(source_graph_diag->message[0] ? source_graph_diag->message : "source graph could not be built");
      }
      ZDiag projection_diag = {0};
      state.projection_checked = z_program_graph_projection_sources_match(&store, &state.projection_current, &projection_diag);
      if (!state.projection_checked) state.projection_error = z_strdup(projection_diag.message[0] ? projection_diag.message : "source projection failed");
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
  free(state->graph_error);
  free(state->projection_error);
  free(state->graph_hash);
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

static const char *repo_sync_state(const RepositoryGraphState *state) {
  if (state && state->store_present && state->store_valid && state->graph_error) return "conflict";
  if (state && state->store_present && state->store_valid && state->projection_error) return "conflict";
  if (state && state->store_present && state->store_valid && state->projection_checked && !state->projection_current) return "source-stale";
  if (state && state->store_present && state->store_valid && state->graph_checked && !state->graph_current) return "graph-stale";
  if (state && state->store_present && state->store_valid && state->projection_checked) return state->projection_current ? "clean" : "source-stale";
  if (state && state->store_present && state->store_valid) return "store-valid";
  if (state && state->store_present) return "store-invalid";
  return "not-enabled";
}

static void repo_append_contract_json(ZBuf *buf, const RepositoryGraphState *state) {
  char *from_source = repo_command_text("zero graph sync --from-source", state->input);
  char *from_graph = repo_command_text("zero graph sync --from-graph", state->input);
  zbuf_append(buf, "{");
  zbuf_append(buf, "\"artifact\":\"zero.graph\",\"sourceProjection\":\"checked-in .0 source text\",");
  zbuf_append(buf, "\"optIn\":\"repository graph loader plus checked-in zero.graph at the package root\",");
  zbuf_append(buf, "\"commands\":{");
  zbuf_append(buf, "\"status\":{\"writes\":false,\"available\":true},");
  zbuf_append(buf, "\"verifySync\":{\"writes\":false,\"available\":");
  zbuf_append(buf, state && state->store_valid ? "true" : "false");
  zbuf_append(buf, ",\"repairCommands\":[");
  repo_append_json_string(buf, from_source);
  zbuf_append(buf, ",");
  repo_append_json_string(buf, from_graph);
  zbuf_append(buf, "]},");
  zbuf_append(buf, "\"syncFromSource\":{\"writes\":true,\"available\":true},");
  zbuf_append(buf, "\"syncFromGraph\":{\"writes\":true,\"available\":");
  zbuf_append(buf, state && state->store_valid ? "true" : "false");
  zbuf_append(buf, "}");
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
  zbuf_append(buf, ", \"syncState\": ");
  repo_append_json_string(buf, repo_sync_state(state));
  zbuf_append(buf, ", \"possibleSyncStates\": [\"not-enabled\", \"store-invalid\", \"clean\", \"source-stale\", \"graph-stale\", \"conflict\"], \"canonicalSourceExtension\": \".0\", \"compilerInput\": \"source-text\"}");
  if (state->store_valid) {
    zbuf_append(buf, ",\n  \"store\": {\"format\":\"zero-repository-graph\",\"schemaVersion\":1,\"graphHash\":");
    repo_append_json_string(buf, state->graph_hash);
    zbuf_appendf(buf, ",\"nodes\":%zu,\"edges\":%zu,\"sources\":%zu}", state->node_count, state->edge_count, state->source_count);
  }
  zbuf_append(buf, ",\n  \"contract\": ");
  repo_append_contract_json(buf, state);
}

static void repo_append_diagnostic_json(ZBuf *buf, const RepositoryGraphState *state, const char *code, const char *message, const char *expected, const char *actual, const char *help, bool include_repairs) {
  char *from_source = repo_command_text("zero graph sync --from-source", state->input);
  char *from_graph = repo_command_text("zero graph sync --from-graph", state->input);
  zbuf_append(buf, ",\n  \"diagnostics\": [{\"severity\":\"error\",\"code\":");
  repo_append_json_string(buf, code);
  zbuf_append(buf, ",\"message\":");
  repo_append_json_string(buf, message);
  zbuf_append(buf, ",\"path\":");
  repo_append_json_string(buf, state->input);
  zbuf_append(buf, ",\"line\":1,\"column\":1,\"length\":1,\"expected\":");
  repo_append_json_string(buf, expected);
  zbuf_append(buf, ",\"actual\":");
  repo_append_json_string(buf, actual);
  zbuf_append(buf, ",\"help\":");
  repo_append_json_string(buf, help);
  zbuf_append(buf, ",\"fixSafety\":\"requires-human-review\",\"repair\":{\"id\":\"inspect-repository-graph-status\",\"summary\":\"Inspect graph/source sync state before choosing a sync direction.\"},\"related\":[]}],");
  zbuf_append(buf, "\n  \"repairCommands\": ");
  if (include_repairs) {
    zbuf_append(buf, "[");
    repo_append_json_string(buf, from_source);
    zbuf_append(buf, ", ");
    repo_append_json_string(buf, from_graph);
    zbuf_append(buf, "]\n}\n");
  } else {
    zbuf_append(buf, "[]\n}\n");
  }
  free(from_source);
  free(from_graph);
}

static int repo_graph_error(const RepositoryGraphState *state, bool json, const char *mode, const char *code, const char *message, const char *expected, const char *actual, const char *help, bool include_repairs) {
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    repo_append_state_json(&buf, state, false, mode, false);
    repo_append_diagnostic_json(&buf, state, code, message, expected, actual, help, include_repairs);
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    fprintf(stderr, "%s:1:1 %s: %s\n", state->input, code, message);
    fprintf(stderr, "  expected: %s\n", expected);
    fprintf(stderr, "  actual: %s\n", actual);
    fprintf(stderr, "  help: %s\n", help);
  }
  return 1;
}

static int repo_missing_or_invalid_store_error(const RepositoryGraphState *state, bool json, const char *mode) {
  if (!state->store_present) {
    return repo_graph_error(state, json, mode, "RGP001", "repository graph store is missing", "checked-in zero.graph repository graph store", "missing zero.graph", "run zero graph sync --from-source to create the repository graph store", true);
  }
  if (!state->store_valid) {
    return repo_graph_error(state, json, mode, "RGP003", "repository graph store is invalid", "valid zero.graph repository graph store", state->store_error ? state->store_error : "invalid zero.graph", "run zero graph sync --from-source after reviewing the source projection", true);
  }
  return 0;
}

static int repo_graph_direction_error(const RepositoryGraphState *state, bool json, bool from_graph, bool from_source) {
  const char *actual = from_graph && from_source ? "--from-graph and --from-source" : "missing sync direction";
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    repo_append_state_json(&buf, state, false, "sync", false);
    zbuf_append(&buf, ",\n  \"diagnostics\": [{\"severity\":\"error\",\"code\":\"RGP002\",\"message\":\"graph sync requires exactly one direction\",\"path\":");
    repo_append_json_string(&buf, state->input);
    zbuf_append(&buf, ",\"line\":1,\"column\":1,\"length\":1,\"expected\":\"zero graph sync --from-source or zero graph sync --from-graph\",\"actual\":");
    repo_append_json_string(&buf, actual);
    zbuf_append(&buf, ",\"help\":\"choose whether source text or zero.graph is authoritative for this sync\",\"fixSafety\":\"requires-human-review\",\"repair\":{\"id\":\"choose-graph-sync-direction\",\"summary\":\"Choose exactly one graph sync direction.\"},\"related\":[]}],\n  \"repairCommands\": []\n}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    fprintf(stderr, "%s:1:1 RGP002: graph sync requires exactly one direction\n", state->input);
    fprintf(stderr, "  expected: zero graph sync --from-source or zero graph sync --from-graph\n");
    fprintf(stderr, "  actual: %s\n", actual);
    fprintf(stderr, "  help: choose whether source text or zero.graph is authoritative for this sync\n");
  }
  return 1;
}

static void repo_append_changed_paths_json(ZBuf *buf, const char *const *changed_paths, size_t changed_len) {
  zbuf_append(buf, "[");
  for (size_t i = 0; changed_paths && i < changed_len; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    repo_append_json_string(buf, changed_paths[i]);
  }
  zbuf_append(buf, "]");
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
    printf("repository graph sync ok\n");
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

int z_repository_graph_status_command(const char *input, bool json, bool from_graph, bool from_source, const ZProgramGraph *source_graph, const ZDiag *source_graph_diag) {
  RepositoryGraphState state = repo_graph_state(input, source_graph, source_graph_diag);
  if (from_graph || from_source) {
    int rc = repo_graph_direction_error(&state, json, from_graph, from_source);
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
    printf("repository graph status: %s\n", repo_sync_state(&state));
    printf("root: %s\n", state.root);
    printf("store: %s\n", state.store_path);
    printf("store valid: %s\n", state.store_valid ? "true" : "false");
    printf("source projection: checked-in .0 source text\n");
    printf("writes: false\n");
  }
  repo_graph_state_free(&state);
  return 0;
}

int z_repository_graph_verify_sync_command(const char *input, bool json, bool from_graph, bool from_source, const ZProgramGraph *source_graph) {
  RepositoryGraphState state = repo_graph_state(input, source_graph, NULL);
  if (from_graph || from_source) {
    int rc = repo_graph_direction_error(&state, json, from_graph, from_source);
    repo_graph_state_free(&state);
    return rc;
  }
  int rc = repo_missing_or_invalid_store_error(&state, json, "verify-sync");
  if (rc != 0) {
    repo_graph_state_free(&state);
    return rc;
  }
  ZProgramGraphStore store;
  ZDiag diag = {0};
  if (!z_program_graph_store_load_path(state.store_path, &store, &diag)) {
    rc = repo_graph_error(&state, json, "verify-sync", "RGP003", "repository graph store is invalid", "valid zero.graph repository graph store", diag.message, "run zero graph sync --from-source after reviewing the source projection", true);
    repo_graph_state_free(&state);
    return rc;
  }
  if (!z_program_graph_store_graph_matches_source(&store, source_graph)) {
    rc = repo_graph_error(&state, json, "verify-sync", "RGP005", "repository graph store is out of sync with source text", "zero.graph graph hash matching current .0 source graph", source_graph && source_graph->graph_hash ? source_graph->graph_hash : "missing source graph", "run zero graph sync --from-source after reviewing source changes", true);
    z_program_graph_store_free(&store);
    repo_graph_state_free(&state);
    return rc;
  }
  bool projection_current = false;
  bool projection_checked = z_program_graph_projection_sources_match(&store, &projection_current, &diag);
  if (!projection_checked || !projection_current) {
    const char *actual = projection_checked ? "source projection differs" : (diag.message[0] ? diag.message : "projection unavailable");
    rc = repo_graph_error(&state, json, "verify-sync", "RGP006", "source projection is out of sync with repository graph", "checked-in .0 source text matching zero.graph projection", actual, "run zero graph sync --from-graph after reviewing graph changes", true);
    z_program_graph_store_free(&store);
    repo_graph_state_free(&state);
    return rc;
  }
  z_program_graph_store_free(&store);
  rc = repo_graph_success(&state, json, "verify-sync", false, NULL);
  repo_graph_state_free(&state);
  return rc;
}

int z_repository_graph_sync_command(const char *input, bool json, bool from_graph, bool from_source, const ZProgramGraph *source_graph, const ZDiag *source_graph_diag, ZRepositoryGraphLoadSourceGraphFn load_source_graph, void *load_source_graph_ctx) {
  RepositoryGraphState state = repo_graph_state(input, from_source ? source_graph : NULL, from_graph ? source_graph_diag : NULL);
  if (from_graph == from_source) {
    int rc = repo_graph_direction_error(&state, json, from_graph, from_source);
    repo_graph_state_free(&state);
    return rc;
  }
  if (from_graph) {
    int rc = repo_missing_or_invalid_store_error(&state, json, "sync-from-graph");
    if (rc != 0) {
      repo_graph_state_free(&state);
      return rc;
    }
    ZProgramGraphStore store;
    ZDiag diag = {0};
    if (!z_program_graph_store_load_path(state.store_path, &store, &diag)) {
      rc = repo_graph_error(&state, json, "sync-from-graph", "RGP003", "repository graph store is invalid", "valid zero.graph repository graph store", diag.message, "run zero graph sync --from-source after reviewing the source projection", true);
      repo_graph_state_free(&state);
      return rc;
    }
    ZProgramGraphProjection projection;
    if (!z_program_graph_projection_write_sources(&store, &projection, &diag)) {
      rc = repo_graph_error(&state, json, "sync-from-graph", "RGP004", "repository graph projection could not be written", "canonical .0 source projection", diag.message[0] ? diag.message : "projection failed", "run zero graph check on zero.graph before syncing from graph", false);
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
    state = repo_graph_state(input, post_ok ? &post_graph : NULL, post_checked && !post_ok ? &post_diag : NULL);
    bool post_clean = state.store_present && state.store_valid && state.projection_checked && state.projection_current;
    post_clean = post_clean && (!state.graph_checked || state.graph_current) && !state.graph_error && !state.projection_error;
    if (post_checked && (!post_ok || !post_clean)) {
      rc = repo_graph_error(&state, json, "sync-from-graph", "RGP006", "repository graph store is not clean after sync from graph", "clean repository graph state", repo_sync_state(&state), "run zero graph status to inspect repository graph state before choosing a repair", true);
      z_program_graph_free(&post_graph);
      z_program_graph_projection_free(&projection);
      z_program_graph_store_free(&store);
      repo_graph_state_free(&state);
      return rc;
    }
    rc = repo_graph_success_paths(&state, json, "sync-from-graph", true, (const char *const *)projection.changed_paths, projection.changed_len);
    z_program_graph_free(&post_graph);
    z_program_graph_projection_free(&projection);
    z_program_graph_store_free(&store);
    repo_graph_state_free(&state);
    return rc;
  }
  if (!source_graph) {
    int rc = repo_graph_error(&state, json, "sync-from-source", "RGP003", "source graph could not be built", "current source graph", "missing source graph", "run zero graph check on the input first", false);
    repo_graph_state_free(&state);
    return rc;
  }
  ZProgramGraphStore saved;
  ZDiag diag = {0};
  if (!z_program_graph_store_save_for_input(input, source_graph, &saved, &diag)) {
    int rc = repo_graph_error(&state, json, "sync-from-source", "RGP003", "repository graph store could not be saved", "byte-stable zero.graph repository graph store", diag.actual[0] ? diag.actual : (diag.message[0] ? diag.message : "save failed"), "run zero graph status to inspect repository graph state", false);
    repo_graph_state_free(&state);
    return rc;
  }
  repo_graph_state_free(&state);
  state = repo_graph_state(input, source_graph, NULL);
  if (strcmp(repo_sync_state(&state), "clean") != 0) {
    int rc = repo_graph_error(&state, json, "sync-from-source", "RGP006", "repository graph store is not clean after sync from source", "clean repository graph state", repo_sync_state(&state), "run zero graph status to inspect repository graph state before choosing a repair", true);
    z_program_graph_store_free(&saved);
    repo_graph_state_free(&state);
    return rc;
  }
  int rc = repo_graph_success(&state, json, "sync-from-source", true, saved.path);
  z_program_graph_store_free(&saved);
  repo_graph_state_free(&state);
  return rc;
}

bool z_repository_graph_needs_source_graph(const char *kind, const char *input, bool from_graph, bool from_source) {
  if (kind && strcmp(kind, "sync") == 0) return from_source && !from_graph;
  if (kind && strcmp(kind, "status") == 0 && !from_graph && !from_source) {
    RepositoryGraphState state = repo_graph_state(input, NULL, NULL);
    bool needs_source = state.store_valid && state.projection_checked && state.projection_current;
    repo_graph_state_free(&state);
    return needs_source;
  }
  if (!kind || strcmp(kind, "verify-sync") != 0 || from_graph || from_source) return false;
  RepositoryGraphState state = repo_graph_state(input, NULL, NULL);
  bool needs_source = state.store_valid;
  repo_graph_state_free(&state);
  return needs_source;
}

bool z_repository_graph_source_graph_optional(const char *kind, bool from_graph, bool from_source) { return kind && strcmp(kind, "status") == 0 && !from_graph && !from_source; }

int z_repository_graph_maybe_command(const char *kind, const char *input, bool json, bool from_graph, bool from_source, const ZProgramGraph *source_graph, const ZDiag *source_graph_diag, ZRepositoryGraphLoadSourceGraphFn load_source_graph, void *load_source_graph_ctx, bool *handled) {
  if (handled) *handled = true;
  if (kind && strcmp(kind, "status") == 0) return z_repository_graph_status_command(input, json, from_graph, from_source, source_graph, source_graph_diag);
  if (kind && strcmp(kind, "verify-sync") == 0) return z_repository_graph_verify_sync_command(input, json, from_graph, from_source, source_graph);
  if (kind && strcmp(kind, "sync") == 0) return z_repository_graph_sync_command(input, json, from_graph, from_source, source_graph, source_graph_diag, load_source_graph, load_source_graph_ctx);
  if (handled) *handled = false;
  return 0;
}
