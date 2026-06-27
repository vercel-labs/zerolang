#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "program_graph_projection.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

static char *projection_path_join(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left && left[0] ? left : ".");
  if (buf.len > 0 && buf.data[buf.len - 1] != '/') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right && right[0] ? right : ".");
  return buf.data;
}

static void projection_path_set_io_diag(ZDiag *diag, const char *path, const char *action) {
  if (!diag) return;
  diag->code = 1;
  z_diag_set_path_copy(diag, path);
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "failed to %s '%s': %s", action ? action : "write", path ? path : "", strerror(errno));
}

static bool projection_path_set_diag(ZDiag *diag, const ZProgramGraphStore *store, const char *message, const char *actual) {
  if (diag) {
    diag->code = 1008;
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

#if !defined(_WIN32)
static bool projection_path_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static char *projection_path_dirname(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  if (!slash) return z_strdup(".");
  if (slash == path) return z_strdup("/");
  return z_strndup(path, (size_t)(slash - path));
}

static bool projection_path_exists(const char *path) {
  struct stat st;
  return path && lstat(path, &st) == 0;
}

static bool projection_real_path_inside_root(const char *root, const char *path) {
  if (!root || !path) return false;
  if (projection_path_text_eq(root, path)) return true;
  size_t root_len = strlen(root);
  if (root_len == 1 && root[0] == '/') return path[0] == '/';
  return strncmp(path, root, root_len) == 0 && path[root_len] == '/';
}

#define PROJECTION_REALPATH_CACHE_SLOTS 4

typedef struct {
  char *path;
  char *real;
} ProjectionRealPathCacheEntry;

static ProjectionRealPathCacheEntry projection_realpath_cache[PROJECTION_REALPATH_CACHE_SLOTS];
static size_t projection_realpath_cache_next;

static char *projection_realpath_cached(const char *path) {
  if (!path) return NULL;
  for (size_t i = 0; i < PROJECTION_REALPATH_CACHE_SLOTS; i++) {
    if (projection_realpath_cache[i].path && projection_path_text_eq(projection_realpath_cache[i].path, path)) {
      return z_strdup(projection_realpath_cache[i].real);
    }
  }
  char *real = realpath(path, NULL);
  if (!real) return NULL;
  ProjectionRealPathCacheEntry *slot = &projection_realpath_cache[projection_realpath_cache_next];
  projection_realpath_cache_next = (projection_realpath_cache_next + 1) % PROJECTION_REALPATH_CACHE_SLOTS;
  free(slot->path);
  free(slot->real);
  slot->path = z_strdup(path);
  slot->real = z_strdup(real);
  return real;
}

static bool projection_target_parent_inside_root(const ZProgramGraphStore *store, const char *path, ZDiag *diag) {
  char *root_real = projection_realpath_cached(store && store->root && store->root[0] ? store->root : ".");
  if (!root_real) {
    projection_path_set_io_diag(diag, store && store->root ? store->root : ".", "inspect");
    return false;
  }
  char *parent = projection_path_dirname(path);
  char *parent_real = projection_realpath_cached(parent);
  if (!parent_real) {
    char *ancestor = z_strdup(parent);
    while (ancestor && !projection_path_exists(ancestor)) {
      char *next = projection_path_dirname(ancestor);
      if (!next || projection_path_text_eq(next, ancestor)) {
        free(next);
        break;
      }
      free(ancestor);
      ancestor = next;
    }
    char *ancestor_real = ancestor ? realpath(ancestor, NULL) : NULL;
    bool ok = ancestor_real && projection_real_path_inside_root(root_real, ancestor_real);
    if (!ok) projection_path_set_diag(diag, store, "repository graph source projection target escapes repository graph root", path);
    free(ancestor_real);
    free(ancestor);
    free(parent);
    free(root_real);
    return ok;
  }
  bool ok = projection_real_path_inside_root(root_real, parent_real);
  if (!ok) projection_path_set_diag(diag, store, "repository graph source projection target escapes repository graph root", path);
  free(parent_real);
  free(parent);
  free(root_real);
  return ok;
}
#endif

bool z_program_graph_projection_target_path_safe(const ZProgramGraphStore *store, const char *path, bool *before_exists, ZDiag *diag) {
  if (before_exists) *before_exists = false;
#if !defined(_WIN32)
  if (!projection_target_parent_inside_root(store, path, diag)) return false;
#endif
  struct stat st;
#if defined(_WIN32)
  if (stat(path, &st) != 0) {
#else
  if (lstat(path, &st) != 0) {
#endif
    if (errno == ENOENT) return true;
    projection_path_set_io_diag(diag, path, "inspect");
    return false;
  }
  if (before_exists) *before_exists = true;
#if !defined(_WIN32)
  if (S_ISLNK(st.st_mode)) {
    return projection_path_set_diag(diag, store, "repository graph source projection target is a symlink", path);
  }
#endif
  if (S_ISDIR(st.st_mode)) {
    return projection_path_set_diag(diag, store, "repository graph source projection target is a directory", path);
  }
  return true;
}

bool z_program_graph_projection_source_path_safe_for_cached_read(const char *root, const char *source_path, bool *before_exists) {
  if (before_exists) *before_exists = false;
  if (!z_program_graph_store_source_path_is_local(source_path)) return false;
  ZProgramGraphStore store = {0};
  store.root = (char *)(root && root[0] ? root : ".");
  char *path = projection_path_join(store.root, source_path);
  bool ok = z_program_graph_projection_target_path_safe(&store, path, before_exists, NULL);
  free(path);
  return ok;
}
