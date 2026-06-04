#include "program_graph_projection.h"

#include "zero.h"

#include <errno.h>
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
  diag->path = path;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "failed to %s '%s': %s", action ? action : "write", path ? path : "", strerror(errno));
}

static bool projection_set_path_diag(ZDiag *diag, const ZProgramGraphStore *store, const char *message, const char *actual) {
  if (diag) {
    diag->code = 1002;
    diag->path = store && store->path ? store->path : "zero.graph";
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "repository graph source projection target is not writable");
    snprintf(diag->expected, sizeof(diag->expected), "writable checked-in .0 source projection path");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "invalid source projection target");
  }
  return false;
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
  return path && stat(path, &st) == 0;
}

static bool projection_target_can_replace(const ZProgramGraphStore *store, const char *path, const char *current, const ZDiag *read_diag, bool *before_exists, ZDiag *diag) {
  if (before_exists) *before_exists = false;
  struct stat st;
  if (stat(path, &st) != 0) {
    if (errno == ENOENT) return true;
    projection_set_io_diag(diag, path, "inspect");
    return false;
  }
  if (before_exists) *before_exists = true;
  if (S_ISDIR(st.st_mode)) {
    return projection_set_path_diag(diag, store, "repository graph source projection target is a directory", path);
  }
  if (!current) {
    if (diag && read_diag && read_diag->message[0]) *diag = *read_diag;
    else projection_set_io_diag(diag, path, "read");
    return false;
  }
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

static bool projection_assign_temp_path(ProjectionWrite *write, size_t index, ZDiag *diag) {
  if (!write || !write->changed) return true;
  for (size_t attempt = 0; attempt < 100; attempt++) {
    char *candidate = projection_temp_path(write->path, index, attempt);
    if (!projection_path_exists(candidate)) {
      write->temp_path = candidate;
      return true;
    }
    free(candidate);
  }
  if (diag) {
    diag->code = 1;
    diag->path = write->path;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "failed to create temporary projection path for '%s'", write->path ? write->path : "");
  }
  return false;
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
    if (!projection_assign_temp_path(write, i, diag)) return false;
    if (!z_write_file(write->temp_path, write->text ? write->text : "", diag)) return false;
    write->temp_written = true;
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
      diag->code = 1002;
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
      diag->code = 1002;
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

static bool projection_source_text_matches(const ZProgramGraphStore *store, const char *source_path, bool *matches, ZDiag *diag) {
  *matches = false;
  const char *projection = NULL;
  if (!projection_source_text(store, source_path, &projection, diag)) return false;
  char *path = projection_join_path(store->root, source_path);
  ZDiag read_diag = {0};
  char *current = z_read_file(path, &read_diag);
  if (current) *matches = projection_text_eq(current, projection);
  free(current);
  free(path);
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

bool z_program_graph_projection_sources_match(const ZProgramGraphStore *store, bool *matches, ZDiag *diag) {
  if (matches) *matches = false;
  if (!store || store->projection_len == 0) {
    if (diag) {
      diag->code = 1002;
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
  if (!z_program_graph_projection_store_matches_graph(store, diag)) return false;
  bool all_match = true;
  for (size_t i = 0; i < store->projection_len; i++) {
    bool file_matches = false;
    if (!projection_source_text_matches(store, store->projection_paths[i], &file_matches, diag)) return false;
    if (!file_matches) all_match = false;
  }
  if (matches) *matches = all_match;
  return true;
}

bool z_program_graph_projection_write_sources(const ZProgramGraphStore *store, ZProgramGraphProjection *projection, ZDiag *diag) {
  if (!store || store->projection_len == 0) return z_program_graph_projection_sources_match(store, NULL, diag);
  if (!z_program_graph_projection_store_matches_graph(store, diag)) return false;
  if (projection) z_program_graph_projection_init(projection);
  ProjectionWritePlan plan = {0};
  for (size_t i = 0; i < store->projection_len; i++) {
    const char *source_path = store->projection_paths[i];
    const char *source_text = NULL;
    if (!projection_source_text(store, source_path, &source_text, diag)) {
      projection_write_plan_free(&plan);
      return false;
    }
    char *path = projection_join_path(store->root, source_path);
    ZDiag read_diag = {0};
    char *current = z_read_file(path, &read_diag);
    bool before_exists = false;
    if (!projection_target_can_replace(store, path, current, &read_diag, &before_exists, diag)) {
      free(current);
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
