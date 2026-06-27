#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "program_graph_projection.h"

#include "zero.h"

#include <errno.h>
#if !defined(_WIN32)
#include <fcntl.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

typedef struct {
  char *path;
  char *temp_path;
  const char *text;
  char *before_text;
  bool before_exists;
  bool changed;
  bool temp_written;
  bool committed;
} ProjectionWrite;

typedef struct {
  ProjectionWrite *items;
  size_t len;
  size_t cap;
} ProjectionWritePlan;

typedef enum {
  PROJECTION_TEMP_WRITE_OK,
  PROJECTION_TEMP_WRITE_EXISTS,
  PROJECTION_TEMP_WRITE_ERROR
} ProjectionTempWriteResult;

static bool projection_add_changed_path(ZProgramGraphProjection *projection, const char *path);

static bool projection_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static char *projection_join_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left && left[0] ? left : ".");
  if (buf.len > 0 && buf.data[buf.len - 1] != '/') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right && right[0] ? right : ".");
  return buf.data;
}

static void projection_set_io_diag(ZDiag *diag, const char *path, const char *action) {
  if (!diag) return;
  diag->code = 1;
  z_diag_set_path_copy(diag, path);
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "failed to %s '%s': %s", action ? action : "write", path ? path : "", strerror(errno));
}

static unsigned long projection_process_id(void) {
#if defined(_WIN32)
  return 0;
#else
  return (unsigned long)getpid();
#endif
}

static char *projection_temp_path(const char *path, size_t index, size_t attempt) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, path ? path : "source.0");
  zbuf_appendf(&buf, ".zero-sync-%lu-%zu-%zu.tmp", projection_process_id(), index, attempt);
  return buf.data;
}

static bool projection_path_exists(const char *path) {
  struct stat st;
#if defined(_WIN32)
  return path && stat(path, &st) == 0;
#else
  return path && lstat(path, &st) == 0;
#endif
}

static bool projection_existing_path_is_dir(const char *path, ZDiag *diag) {
  struct stat st;
  if (stat(path, &st) != 0) {
    projection_set_io_diag(diag, path, "inspect");
    return false;
  }
  if (!S_ISDIR(st.st_mode)) {
    if (diag) {
      diag->code = 1;
      z_diag_set_path_copy(diag, path);
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "projection parent path is not a directory: '%s'", path ? path : "");
    }
    return false;
  }
  return true;
}

static bool projection_mkdir_one(const char *path, ZDiag *diag) {
#if defined(_WIN32)
  if (mkdir(path) == 0) return true;
#else
  if (mkdir(path, 0777) == 0) return true;
#endif
  if (errno == EEXIST) return projection_existing_path_is_dir(path, diag);
  projection_set_io_diag(diag, path, "create");
  return false;
}

static bool projection_ensure_parent_dirs(const char *path, ZDiag *diag) {
  char *copy = z_strdup(path ? path : "");
  for (char *cursor = copy + 1; *cursor; cursor++) {
    if (*cursor != '/') continue;
    *cursor = 0;
    if (copy[0] && !projection_mkdir_one(copy, diag)) {
      free(copy);
      return false;
    }
    *cursor = '/';
  }
  free(copy);
  return true;
}

static bool projection_current_text_for_target(const ZProgramGraphStore *store, const char *path, bool before_exists, char **current, ZDiag *diag) {
  *current = NULL;
  if (!before_exists) return true;
  ZDiag read_diag = {0};
  *current = z_read_file(path, &read_diag);
  if (!*current) {
    if (diag && read_diag.message[0]) *diag = read_diag;
    else projection_set_io_diag(diag, path, "read");
    return false;
  }
  (void)store;
  return true;
}

static void projection_write_plan_free(ProjectionWritePlan *plan) {
  if (!plan) return;
  for (size_t i = 0; i < plan->len; i++) {
    ProjectionWrite *write = &plan->items[i];
    if (write->temp_written && write->temp_path) remove(write->temp_path);
    free(write->path);
    free(write->temp_path);
    free(write->before_text);
  }
  free(plan->items);
  *plan = (ProjectionWritePlan){0};
}

static bool projection_write_plan_add(ProjectionWritePlan *plan, ProjectionWrite write) {
  if (plan->len == plan->cap) {
    size_t next = z_grow_capacity(plan->cap, plan->len + 1, 8);
    plan->items = z_checked_reallocarray(plan->items, next, sizeof(ProjectionWrite));
    plan->cap = next;
  }
  plan->items[plan->len++] = write;
  return true;
}

static bool projection_set_temp_path_diag(ZDiag *diag, const ProjectionWrite *write) {
  if (diag) {
    diag->code = 1;
    diag->path = write ? write->path : NULL;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "failed to create temporary projection path for '%s'", write && write->path ? write->path : "");
  }
  return false;
}

static ProjectionTempWriteResult projection_write_temp_exclusive(const char *path, const char *text, ZDiag *diag) {
  if (!projection_ensure_parent_dirs(path, diag)) return PROJECTION_TEMP_WRITE_ERROR;
#if defined(_WIN32)
  if (projection_path_exists(path)) return PROJECTION_TEMP_WRITE_EXISTS;
  return z_write_file(path, text ? text : "", diag) ? PROJECTION_TEMP_WRITE_OK : PROJECTION_TEMP_WRITE_ERROR;
#else
  int flags = O_WRONLY | O_CREAT | O_EXCL;
#if defined(O_NOFOLLOW)
  flags |= O_NOFOLLOW;
#endif
  int fd = open(path, flags, 0600);
  if (fd < 0) {
    if (errno == EEXIST || errno == ELOOP) return PROJECTION_TEMP_WRITE_EXISTS;
    projection_set_io_diag(diag, path, "stage");
    return PROJECTION_TEMP_WRITE_ERROR;
  }

  const char *data = text ? text : "";
  size_t len = strlen(data);
  size_t written = 0;
  while (written < len) {
    ssize_t n = write(fd, data + written, len - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      int saved_errno = errno;
      close(fd);
      unlink(path);
      errno = saved_errno;
      projection_set_io_diag(diag, path, "stage");
      return PROJECTION_TEMP_WRITE_ERROR;
    }
    if (n == 0) {
      close(fd);
      unlink(path);
      errno = EIO;
      projection_set_io_diag(diag, path, "stage");
      return PROJECTION_TEMP_WRITE_ERROR;
    }
    written += (size_t)n;
  }
  if (close(fd) != 0) {
    int saved_errno = errno;
    unlink(path);
    errno = saved_errno;
    projection_set_io_diag(diag, path, "stage");
    return PROJECTION_TEMP_WRITE_ERROR;
  }
  return PROJECTION_TEMP_WRITE_OK;
#endif
}

static void projection_write_plan_rollback(ProjectionWritePlan *plan) {
  for (size_t i = 0; plan && i < plan->len; i++) {
    ProjectionWrite *write = &plan->items[i];
    if (!write->committed) continue;
    if (write->before_exists) {
      ZDiag ignored = {0};
      z_write_file(write->path, write->before_text ? write->before_text : "", &ignored);
    } else {
      remove(write->path);
    }
    write->committed = false;
  }
}

static bool projection_write_plan_stage(ProjectionWritePlan *plan, ZDiag *diag) {
  for (size_t i = 0; plan && i < plan->len; i++) {
    ProjectionWrite *write = &plan->items[i];
    if (!write->changed) continue;
    for (size_t attempt = 0; attempt < 100; attempt++) {
      char *candidate = projection_temp_path(write->path, i, attempt);
      ProjectionTempWriteResult result = projection_write_temp_exclusive(candidate, write->text, diag);
      if (result == PROJECTION_TEMP_WRITE_OK) {
        write->temp_path = candidate;
        write->temp_written = true;
        break;
      }
      free(candidate);
      if (result == PROJECTION_TEMP_WRITE_ERROR) return false;
    }
    if (!write->temp_written) return projection_set_temp_path_diag(diag, write);
  }
  return true;
}

static bool projection_write_plan_commit(ProjectionWritePlan *plan, ZProgramGraphProjection *projection, ZDiag *diag) {
  for (size_t i = 0; plan && i < plan->len; i++) {
    ProjectionWrite *write = &plan->items[i];
    if (!write->changed) {
      if (projection) projection->unchanged_count++;
      continue;
    }
    if (rename(write->temp_path, write->path) != 0) {
      projection_set_io_diag(diag, write->path, "write");
      projection_write_plan_rollback(plan);
      return false;
    }
    write->temp_written = false;
    write->committed = true;
    if (projection) projection_add_changed_path(projection, write->path);
  }
  return true;
}

static bool projection_add_changed_path(ZProgramGraphProjection *projection, const char *path) {
  if (!projection || !path || !path[0]) return true;
  if (projection->changed_len == projection->changed_cap) {
    size_t next = z_grow_capacity(projection->changed_cap, projection->changed_len + 1, 4);
    projection->changed_paths = z_checked_reallocarray(projection->changed_paths, next, sizeof(char *));
    projection->changed_cap = next;
  }
  projection->changed_paths[projection->changed_len++] = z_strdup(path);
  return true;
}

static bool projection_source_text(const ZProgramGraphStore *store, const char *source_path, const char **out, ZDiag *diag) {
  if (!z_program_graph_store_source_path_is_local(source_path)) {
    if (out) *out = NULL;
    if (diag) {
      diag->code = 1008;
      diag->path = store && store->path ? store->path : "zero.graph";
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "repository graph source projection path is not local");
      snprintf(diag->expected, sizeof(diag->expected), "relative source projection path inside the repository graph root");
      snprintf(diag->actual, sizeof(diag->actual), "%s", source_path && source_path[0] ? source_path : "missing source path");
    }
    return false;
  }
  const char *text = z_program_graph_store_projection_text(store, source_path);
  if (out) *out = text;
  if (!text) {
    if (diag) {
      diag->code = 1008;
      diag->path = store && store->path ? store->path : "zero.graph";
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "repository graph source projection is missing source text");
      snprintf(diag->expected, sizeof(diag->expected), "source projection text in zero.graph");
      snprintf(diag->actual, sizeof(diag->actual), "%s", source_path && source_path[0] ? source_path : "missing source path");
    }
    return false;
  }
  return true;
}

static bool projection_source_text_current(const ZProgramGraphStore *store, const char *source_path, const char **projection_out, char **current_out, ZDiag *diag) {
  *projection_out = NULL;
  *current_out = NULL;
  if (!projection_source_text(store, source_path, projection_out, diag)) return false;
  char *path = projection_join_path(store->root, source_path);
  bool before_exists = false;
  if (!z_program_graph_projection_target_path_safe(store, path, &before_exists, diag)) {
    free(path);
    return false;
  }
  if (!projection_current_text_for_target(store, path, before_exists, current_out, diag)) {
    free(path);
    return false;
  }
  free(path);
  return true;
}

static bool projection_source_text_matches(const ZProgramGraphStore *store, const char *source_path, bool *matches, ZDiag *diag) {
  *matches = false;
  const char *projection = NULL;
  char *current = NULL;
  if (!projection_source_text_current(store, source_path, &projection, &current, diag)) return false;
  if (current) *matches = projection_text_eq(current, projection);
  free(current);
  return true;
}

void z_program_graph_projection_init(ZProgramGraphProjection *projection) {
  if (projection) *projection = (ZProgramGraphProjection){0};
}

void z_program_graph_projection_free(ZProgramGraphProjection *projection) {
  if (!projection) return;
  for (size_t i = 0; i < projection->changed_len; i++) free(projection->changed_paths[i]);
  free(projection->changed_paths);
  *projection = (ZProgramGraphProjection){0};
}

bool z_program_graph_projection_sources_match(const ZProgramGraphStore *store, const ZTargetInfo *target, bool *matches, ZDiag *diag) {
  if (matches) *matches = false;
  if (!store || store->projection_len == 0) {
    if (diag) {
      diag->code = 1008;
      diag->path = store && store->path ? store->path : "zero.graph";
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "repository graph store has no source projections");
      snprintf(diag->expected, sizeof(diag->expected), "one or more source projection rows");
      snprintf(diag->actual, sizeof(diag->actual), "empty projection table");
    }
    return false;
  }
  if (!z_program_graph_projection_store_matches_graph(store, target, diag)) return false;
  return z_program_graph_projection_source_files_match(store, matches, diag);
}

/*
 * Classify store/source sync by content, never by file mtimes. The store
 * records a hash of the on-disk source projection at every store write
 * (zero import, zero patch, zero export, zero init). Comparing the current
 * on-disk projection and the store's own projection table against that
 * recorded hash separates the three drift states:
 *
 * - on-disk projection matches the table: clean.
 * - on-disk projection still matches the recorded hash: only the store
 *   moved (patches landed after the record); the store is authoritative.
 * - the store's table still matches the recorded hash: only the source
 *   moved; the source is authoritative and the store needs a refresh.
 * - neither matches the record: source and store were edited independently.
 *
 * Stores written before the recorded hash existed degrade to one
 * reconciling source refresh instead of an error wall, so freshly staged,
 * cloned, or extracted workspaces classify identically everywhere.
 */
bool z_program_graph_projection_source_sync_state(const ZProgramGraphStore *store, ZProgramGraphProjectionSourceSync *sync, ZDiag *diag) {
  if (sync) *sync = Z_PROGRAM_GRAPH_PROJECTION_SYNC_CLEAN;
  if (!store || store->projection_len == 0) return false;
  bool any_changed = false;
  uint64_t disk_state = z_program_graph_store_source_hash_seed();
  uint64_t table_state = z_program_graph_store_source_hash_seed();
  for (size_t i = 0; i < store->projection_len; i++) {
    const char *projection = NULL;
    char *current = NULL;
    if (!projection_source_text_current(store, store->projection_paths[i], &projection, &current, diag)) return false;
    if (!current || !projection_text_eq(current, projection)) any_changed = true;
    disk_state = z_program_graph_store_source_hash_fold(disk_state, store->projection_paths[i], current);
    table_state = z_program_graph_store_source_hash_fold(table_state, store->projection_paths[i], projection ? projection : "");
    free(current);
  }
  if (!sync || !any_changed) return true;
  const char *recorded = store->source_projection_hash;
  if (!recorded || !recorded[0]) {
    *sync = Z_PROGRAM_GRAPH_PROJECTION_SYNC_SOURCE_NEWER;
    return true;
  }
  char *disk_hash = z_program_graph_store_source_hash_text(disk_state);
  char *table_hash = z_program_graph_store_source_hash_text(table_state);
  if (projection_text_eq(disk_hash, recorded)) *sync = Z_PROGRAM_GRAPH_PROJECTION_SYNC_STORE_NEWER;
  else if (projection_text_eq(table_hash, recorded)) *sync = Z_PROGRAM_GRAPH_PROJECTION_SYNC_SOURCE_NEWER;
  else *sync = Z_PROGRAM_GRAPH_PROJECTION_SYNC_DIVERGED;
  free(disk_hash); free(table_hash); return true;
}

bool z_program_graph_projection_cached_run_allows_cache(const char *input) {
  char *root = z_program_graph_store_root_for_input(input), *store_path = root ? z_program_graph_store_path_for_root(root) : NULL;
  ZProgramGraphProjectionSourceSync sync = Z_PROGRAM_GRAPH_PROJECTION_SYNC_CLEAN; bool allow = false;
  if (store_path && z_program_graph_store_path_exists(store_path) && z_program_graph_projection_source_sync_state_binary_fast(store_path, root, &sync)) allow = sync == Z_PROGRAM_GRAPH_PROJECTION_SYNC_CLEAN || sync == Z_PROGRAM_GRAPH_PROJECTION_SYNC_STORE_NEWER;
  else if (store_path && z_program_graph_store_path_exists(store_path)) {
    ZProgramGraphStore store; ZDiag store_diag = {0};
    if (z_program_graph_store_load_path(store_path, &store, &store_diag)) {
      bool sources_missing = z_program_graph_projection_sources_missing(&store); ZProgramGraphProjectionSourceSync store_sync = Z_PROGRAM_GRAPH_PROJECTION_SYNC_CLEAN; ZDiag sync_diag = {0};
      if (sources_missing) allow = !store.source_projection_hash || !store.source_projection_hash[0];
      else if (z_program_graph_projection_source_sync_state(&store, &store_sync, &sync_diag)) allow = store_sync == Z_PROGRAM_GRAPH_PROJECTION_SYNC_CLEAN || store_sync == Z_PROGRAM_GRAPH_PROJECTION_SYNC_STORE_NEWER;
      z_program_graph_store_free(&store);
    }
  }
  free(store_path); free(root); return allow;
}
bool z_program_graph_projection_source_files_match(const ZProgramGraphStore *store, bool *matches, ZDiag *diag) {
  if (matches) *matches = false;
  if (!store || store->projection_len == 0) return false;
  bool all_match = true;
  for (size_t i = 0; i < store->projection_len; i++) {
    bool file_matches = false;
    if (!projection_source_text_matches(store, store->projection_paths[i], &file_matches, diag)) return false;
    if (!file_matches) all_match = false;
  }
  if (matches) *matches = all_match;
  return true;
}

bool z_program_graph_projection_sources_missing(const ZProgramGraphStore *store) {
  if (!store || store->projection_len == 0) return true;
  for (size_t i = 0; i < store->projection_len; i++) {
    char *path = projection_join_path(store->root, store->projection_paths[i]);
    bool missing = !projection_path_exists(path);
    free(path);
    if (missing) return true;
  }
  return false;
}

const char *z_program_graph_projection_state_label(const ZProgramGraphStore *store, const ZTargetInfo *target, bool *checked, bool *current, ZDiag *diag) {
  if (checked) *checked = false;
  if (current) *current = false;
  if (!store || store->projection_len == 0) return "unavailable";
  if (!z_program_graph_projection_store_matches_graph(store, target, diag)) return "conflict";
  if (z_program_graph_projection_sources_missing(store)) return "missing";
  bool matches = false;
  bool ok = z_program_graph_projection_source_files_match(store, &matches, diag);
  if (checked) *checked = ok;
  if (current) *current = ok && matches;
  if (!ok) return "conflict";
  return matches ? "clean" : "stale";
}

bool z_program_graph_projection_write_sources(const ZProgramGraphStore *store, const ZTargetInfo *target, ZProgramGraphProjection *projection, ZDiag *diag) {
  if (projection) z_program_graph_projection_init(projection);
  if (!store || store->projection_len == 0) return z_program_graph_projection_sources_match(store, target, NULL, diag);
  if (!z_program_graph_projection_store_matches_graph(store, target, diag)) return false;
  ProjectionWritePlan plan = {0};
  for (size_t i = 0; i < store->projection_len; i++) {
    const char *source_path = store->projection_paths[i];
    const char *source_text = NULL;
    if (!projection_source_text(store, source_path, &source_text, diag)) {
      projection_write_plan_free(&plan);
      return false;
    }
    char *path = projection_join_path(store->root, source_path);
    bool before_exists = false;
    if (!z_program_graph_projection_target_path_safe(store, path, &before_exists, diag)) {
      free(path);
      projection_write_plan_free(&plan);
      return false;
    }
    char *current = NULL;
    if (!projection_current_text_for_target(store, path, before_exists, &current, diag)) {
      free(path);
      projection_write_plan_free(&plan);
      return false;
    }
    bool changed = !current || !projection_text_eq(current, source_text);
    ProjectionWrite write = {
      .path = path,
      .text = source_text,
      .before_text = current,
      .before_exists = before_exists,
      .changed = changed,
    };
    projection_write_plan_add(&plan, write);
    if (projection) projection->source_count++;
  }
  if (!projection_write_plan_stage(&plan, diag)) {
    projection_write_plan_free(&plan);
    return false;
  }
  if (!projection_write_plan_commit(&plan, projection, diag)) {
    projection_write_plan_free(&plan);
    return false;
  }
  projection_write_plan_free(&plan);
  return true;
}
