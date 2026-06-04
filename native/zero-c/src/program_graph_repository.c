#include "program_graph_repository.h"

#include "zero.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
  const char *input;
  char *root;
  char *store_path;
  bool store_present;
} RepositoryGraphState;

static bool repo_path_is_dir(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool repo_path_is_file(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool repo_ends_with(const char *text, const char *suffix) {
  size_t text_len = text ? strlen(text) : 0;
  size_t suffix_len = suffix ? strlen(suffix) : 0;
  return text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0;
}

static char *repo_dirname(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  if (!slash) return z_strdup(".");
  if (slash == path) return z_strdup("/");
  return z_strndup(path, (size_t)(slash - path));
}

static char *repo_join_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left && left[0] ? left : ".");
  if (buf.len > 0 && buf.data[buf.len - 1] != '/') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right ? right : "");
  return buf.data;
}

static bool repo_same_existing_dir(const char *left, const char *right) {
  struct stat left_stat;
  struct stat right_stat;
  return left && right &&
         stat(left, &left_stat) == 0 && stat(right, &right_stat) == 0 &&
         S_ISDIR(left_stat.st_mode) && S_ISDIR(right_stat.st_mode) &&
         left_stat.st_dev == right_stat.st_dev && left_stat.st_ino == right_stat.st_ino;
}

static bool repo_relative_dotdot_chain(const char *path) {
  if (!path || !path[0] || path[0] == '/') return false;
  const char *cursor = path;
  while (*cursor) {
    if (cursor[0] != '.' || cursor[1] != '.') return false;
    cursor += 2;
    if (!*cursor) return true;
    if (*cursor != '/') return false;
    cursor++;
  }
  return false;
}

static char *repo_parent_dir(const char *path) {
  if (!path || !path[0] || (path[0] == '/' && path[1] == 0)) return NULL;
  if (path[0] == '.' && path[1] == 0) {
    if (repo_same_existing_dir(".", "..")) return NULL;
    return z_strdup("..");
  }
  if (repo_relative_dotdot_chain(path)) {
    char *parent = repo_join_path(path, "..");
    if (repo_same_existing_dir(path, parent)) {
      free(parent);
      return NULL;
    }
    return parent;
  }
  char *parent = repo_dirname(path);
  if (!parent || strcmp(parent, path) == 0) {
    free(parent);
    return NULL;
  }
  return parent;
}

static char *repo_find_root(const char *input) {
  if (!input || !input[0]) return z_strdup(".");
  char *cursor = NULL;
  if (repo_ends_with(input, "/zero.json") || strcmp(input, "zero.json") == 0) cursor = repo_dirname(input);
  else if (repo_path_is_dir(input)) cursor = z_strdup(input);
  else cursor = repo_dirname(input);
  char *fallback = cursor && cursor[0] ? z_strdup(cursor) : z_strdup(".");

  while (cursor && cursor[0]) {
    char *manifest = repo_join_path(cursor, "zero.json");
    bool found = repo_path_is_file(manifest);
    free(manifest);
    if (found) {
      free(fallback);
      return cursor;
    }
    char *parent = repo_parent_dir(cursor);
    if (!parent) break;
    free(cursor);
    cursor = parent;
  }
  free(cursor);
  return fallback;
}

static RepositoryGraphState repo_graph_state(const char *input) {
  RepositoryGraphState state = {.input = input && input[0] ? input : "."};
  state.root = repo_find_root(state.input);
  state.store_path = repo_join_path(state.root, "zero.graph");
  state.store_present = repo_path_is_file(state.store_path);
  return state;
}

static void repo_graph_state_free(RepositoryGraphState *state) {
  if (!state) return;
  free(state->root);
  free(state->store_path);
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
  (void)state;
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
  zbuf_append(buf, "\"verifySync\":{\"writes\":false,\"available\":false,\"repairCommands\":[");
  repo_append_json_string(buf, from_source);
  zbuf_append(buf, ",");
  repo_append_json_string(buf, from_graph);
  zbuf_append(buf, "]},");
  zbuf_append(buf, "\"syncFromSource\":{\"writes\":true,\"available\":false},");
  zbuf_append(buf, "\"syncFromGraph\":{\"writes\":true,\"available\":false}");
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
  zbuf_append(buf, ", \"enabled\": false");
  zbuf_append(buf, ", \"syncState\": ");
  repo_append_json_string(buf, repo_sync_state(state));
  zbuf_append(buf, ", \"canonicalSourceExtension\": \".0\", \"compilerInput\": \"source-text\"}");
  zbuf_append(buf, ",\n  \"contract\": ");
  repo_append_contract_json(buf, state);
}

static void repo_append_diagnostic_json(ZBuf *buf, const RepositoryGraphState *state, const char *message, const char *actual) {
  char *from_source = repo_command_text("zero graph sync --from-source", state->input);
  char *from_graph = repo_command_text("zero graph sync --from-graph", state->input);
  zbuf_append(buf, ",\n  \"diagnostics\": [{\"severity\":\"error\",\"code\":\"RGP001\",\"message\":");
  repo_append_json_string(buf, message);
  zbuf_append(buf, ",\"path\":");
  repo_append_json_string(buf, state->input);
  zbuf_append(buf, ",\"line\":1,\"column\":1,\"length\":1,\"expected\":\"checked-in zero.graph synchronized with .0 projections\",\"actual\":");
  repo_append_json_string(buf, actual);
  zbuf_append(buf, ",\"help\":\"run zero graph status to inspect repository graph sync state\",\"fixSafety\":\"requires-human-review\",\"repair\":{\"id\":\"inspect-repository-graph-status\",\"summary\":\"Inspect graph/source sync state before choosing a sync direction.\"},\"related\":[]}],");
  zbuf_append(buf, "\n  \"repairCommands\": [");
  repo_append_json_string(buf, from_source);
  zbuf_append(buf, ", ");
  repo_append_json_string(buf, from_graph);
  zbuf_append(buf, "]\n}\n");
  free(from_source);
  free(from_graph);
}

static int repo_graph_error(const RepositoryGraphState *state, bool json, const char *mode, const char *message, const char *actual) {
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    repo_append_state_json(&buf, state, false, mode, false);
    repo_append_diagnostic_json(&buf, state, message, actual);
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    fprintf(stderr, "%s:1:1 RGP001: %s\n", state->input, message);
    fprintf(stderr, "  expected: checked-in zero.graph synchronized with .0 projections\n");
    fprintf(stderr, "  actual: %s\n", actual);
    fprintf(stderr, "  help: run zero graph status %s\n", state->input);
  }
  return 1;
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

int z_repository_graph_status_command(const char *input, bool json, bool from_graph, bool from_source) {
  RepositoryGraphState state = repo_graph_state(input);
  if (from_graph || from_source) {
    int rc = repo_graph_direction_error(&state, json, from_graph, from_source);
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
    printf("source projection: checked-in .0 source text\n");
    printf("writes: false\n");
  }
  repo_graph_state_free(&state);
  return 0;
}

int z_repository_graph_verify_sync_command(const char *input, bool json, bool from_graph, bool from_source) {
  RepositoryGraphState state = repo_graph_state(input);
  if (from_graph || from_source) {
    int rc = repo_graph_direction_error(&state, json, from_graph, from_source);
    repo_graph_state_free(&state);
    return rc;
  }
  int rc = repo_graph_error(&state, json, "verify-sync", "repository graph sync verification is not enabled", state.store_present ? "zero.graph store present but no repository store loader is active" : "missing zero.graph");
  repo_graph_state_free(&state);
  return rc;
}

int z_repository_graph_sync_command(const char *input, bool json, bool from_graph, bool from_source) {
  RepositoryGraphState state = repo_graph_state(input);
  if (from_graph == from_source) {
    int rc = repo_graph_direction_error(&state, json, from_graph, from_source);
    repo_graph_state_free(&state);
    return rc;
  }
  const char *direction = from_source ? "sync-from-source" : "sync-from-graph";
  const char *actual = state.store_present ? "zero.graph store present but no repository store loader is active" : "missing zero.graph";
  int rc = repo_graph_error(&state, json, direction, "repository graph sync is not enabled", actual);
  repo_graph_state_free(&state);
  return rc;
}

int z_repository_graph_maybe_command(const char *kind, const char *input, bool json, bool from_graph, bool from_source, bool *handled) {
  if (handled) *handled = true;
  if (kind && strcmp(kind, "status") == 0) return z_repository_graph_status_command(input, json, from_graph, from_source);
  if (kind && strcmp(kind, "verify-sync") == 0) return z_repository_graph_verify_sync_command(input, json, from_graph, from_source);
  if (kind && strcmp(kind, "sync") == 0) return z_repository_graph_sync_command(input, json, from_graph, from_source);
  if (handled) *handled = false;
  return 0;
}
